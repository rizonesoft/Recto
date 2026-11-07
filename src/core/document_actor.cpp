// One thread per open document (D01 T02 section 8; document_actor.h has the contract).
#include "document_actor.h"

#include "atomic_write.h"

#include "incremental_writer.h"
#include "interpreter.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <mutex>
#include <new>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace recto::core {

namespace {

using pdf::CancelToken;
using pdf::ErrorCode;
using pdf::make_pdf_error;
using pdf::PdfError;
using pdf::PdfResult;

std::atomic<int64_t> g_live_documents{0};
std::atomic<uint64_t> g_wiped_passwords{0u};
std::atomic<uint8_t> g_failure{0u};

[[nodiscard]] bool failing(detail::ActorFailure failure) noexcept
{
	uint8_t expected = static_cast<uint8_t>(failure);
	return g_failure.compare_exchange_strong(expected, 0u);
}

// A password in one buffer that never moves, wiped when its owner goes, on every path a message can take.
class Secret {
public:
	explicit Secret(std::string_view text)
		: bytes_(text.begin(), text.end())
	{
	}
	Secret(Secret&& other) noexcept
		: bytes_(std::move(other.bytes_))
	{
		other.bytes_.clear();
	}
	Secret(const Secret&) = delete;
	Secret& operator=(const Secret&) = delete;
	Secret& operator=(Secret&&) = delete;
	~Secret() noexcept
	{
		if (bytes_.capacity() != 0u) {
			if (!bytes_.empty()) {
				SecureZeroMemory(bytes_.data(), bytes_.size());
			}
			g_wiped_passwords.fetch_add(1u, std::memory_order_relaxed);
		}
	}
	[[nodiscard]] std::string_view view() const noexcept
	{
		return std::string_view(bytes_.data(), bytes_.size());
	}

private:
	std::vector<char> bytes_;
};

} // namespace

struct DocumentHandle::State {
	explicit State(const ActorOptions& given)
		: options(given)
	{
		auto made = pdf::CancelSource::create();
		if (!made.has_value()) {
			throw std::bad_alloc();
		}
		close_source = std::make_unique<pdf::CancelSource>(*made);
		close_token = close_source->token();
		closed_future = closed_promise.get_future().share();
	}

	struct Message {
		virtual ~Message() noexcept = default;
		virtual void run(State& state) noexcept = 0;
	};

	template <typename T>
	struct Request final : Message {
		template <typename Work>
		Request(Work&& given, const CancelToken& token)
			: work(std::forward<Work>(given))
			, cancel(token)
		{
		}

		// Dropped unanswered, by a close or a full queue, it still answers once.
		~Request() noexcept override
		{
			answer(pdf::detail::cancelled_error());
		}

		void answer(PdfResult<T> result) noexcept
		{
			if (answered) {
				return;
			}
			answered = true;
			try {
				promise.set_value(std::move(result));
			} catch (const std::exception&) {
				// Its shared state is already allocated, and a result moves without allocating.
			}
		}

		void run(State& state) noexcept override
		{
			const CancelToken either = CancelToken::either(cancel, state.close_token);
			if (either.is_cancelled()) {
				answer(pdf::detail::cancelled_error());
				return;
			}
			const pdf::detail::ScopedCancelScope scope(either);
			try {
				if (failing(detail::ActorFailure::ActorCopy)) {
					throw std::bad_alloc();
				}
				answer(work(state, either));
			} catch (const std::bad_alloc&) {
				answer(make_pdf_error(ErrorCode::OutOfMemory, "Out of memory answering a document request"));
			} catch (const std::exception&) {
				answer(make_pdf_error(ErrorCode::InternalError, "A document request failed unexpectedly"));
			}
		}

		std::promise<PdfResult<T>> promise;
		std::move_only_function<PdfResult<T>(State&, const CancelToken&)> work;
		CancelToken cancel;
		bool answered{false};
	};

	template <typename T, typename Work>
	[[nodiscard]] ActorFuture<T> post(Work&& work, const CancelToken& cancel = {}) noexcept
	{
		std::unique_ptr<Request<T>> request;
		ActorFuture<T> future;
		try {
			request = std::make_unique<Request<T>>(std::forward<Work>(work), cancel);
			future = request->promise.get_future();
		} catch (const std::exception&) {
			return {}; // not valid: there was no memory to make an answer in
		}
		const std::lock_guard lock(mutex);
		if (closing) {
			return future; // dropped on the way out, so a ready `Cancelled`
		}
		if (queue.size() >= options.max_queue) {
			request->answer(pdf::make_refusal_error(ErrorCode::ResourceLimit, pdf::RefusalReason::ActorQueueFull, queue.size() + 1u,
				options.max_queue, "A document's request queue is full; the request was not taken"));
			return future;
		}
		try {
			if (failing(detail::ActorFailure::QueueInsert)) {
				throw std::bad_alloc();
			}
			queue.push_back(std::move(request));
		} catch (const std::exception&) {
			request->answer(make_pdf_error(ErrorCode::OutOfMemory, "Out of memory taking a document request"));
			return future;
		}
		wake.notify_one();
		return future;
	}

	// What the state says a query or a save may do.
	[[nodiscard]] PdfResult<void> check_open() const noexcept
	{
		switch (document_state.load()) {
		case DocumentState::Opened:
			return {};
		case DocumentState::NeedsPassword:
		case DocumentState::WrongPassword:
			return make_pdf_error(ErrorCode::NeedsPassword, "The document needs its password first");
		case DocumentState::Closed:
			return pdf::detail::cancelled_error();
		default:
			return make_pdf_error(ErrorCode::InvalidArgument, "The document is not open");
		}
	}

	void touch_stats() noexcept
	{
		const std::lock_guard lock(stats_mutex);
		stats.cached_pages = lists.size();
		stats.cached_bytes = list_bytes;
	}

	void evict() noexcept
	{
		// Past either limit the oldest goes; the newest always stays, or the page just asked for could never be held.
		while (lists.size() > 1u && (lists.size() > options.max_cached_pages || list_bytes > options.max_list_bytes)) {
			const CachedList& oldest = lists.back();
			list_bytes -= oldest.list->memory_bytes();
			index.erase(oldest.page);
			lists.pop_back();
		}
	}

	[[nodiscard]] PdfResult<std::shared_ptr<const pdf::DisplayList>> display_list(size_t page, const CancelToken& cancel)
	{
		RECTO_PDF_RETURN_IF_ERROR(check_open());
		if (const auto found = index.find(page); found != index.end()) {
			lists.splice(lists.begin(), lists, found->second);
			{
				const std::lock_guard lock(stats_mutex);
				++stats.cache_hits;
			}
			return lists.front().list;
		}
		{
			const std::lock_guard lock(stats_mutex);
			++stats.cache_misses;
		}
		RECTO_PDF_TRY(std::shared_ptr<const pdf::DisplayList> list, pdf::interpret_page(*document, page, {}, nullptr, cancel));
		lists.push_front(CachedList{page, list});
		index[page] = lists.begin();
		list_bytes += list->memory_bytes();
		evict();
		touch_stats();
		return list;
	}

	void main() noexcept
	{
		g_live_documents.fetch_add(1, std::memory_order_relaxed);
		const pdf::detail::ScopedCancelPollHookForTesting hook(options.poll_hook, options.poll_hook_context);
		for (;;) {
			std::unique_ptr<Message> message;
			{
				std::unique_lock lock(mutex);
				wake.wait(lock, [&] { return closing || !queue.empty(); });
				if (closing) {
					break;
				}
				message = std::move(queue.front());
				queue.pop_front();
			}
			message->run(*this);
		}
		// Answered outside the lock, in order, each `Cancelled`.
		std::deque<std::unique_ptr<Message>> dropped;
		{
			const std::lock_guard lock(mutex);
			dropped.swap(queue);
		}
		dropped.clear();
		index.clear();
		lists.clear();
		list_bytes = 0u;
		touch_stats();
		document.reset(); // here, on its own thread
		document_state.store(DocumentState::Closed);
		g_live_documents.fetch_sub(1, std::memory_order_relaxed);
		try {
			closed_promise.set_value();
		} catch (const std::exception&) {
			// Set once, by this thread alone.
		}
	}

	struct CachedList {
		size_t page{0u};
		std::shared_ptr<const pdf::DisplayList> list;
	};

	ActorOptions options;
	std::unique_ptr<pdf::CancelSource> close_source;
	CancelToken close_token;
	std::promise<void> closed_promise;
	std::shared_future<void> closed_future;

	std::mutex mutex;
	std::condition_variable wake;
	std::deque<std::unique_ptr<Message>> queue;
	bool closing{false};

	std::atomic<DocumentState> document_state{DocumentState::Unopened};

	// The actor thread's alone.
	std::unique_ptr<pdf::Document> document;
	std::list<CachedList> lists; // most recent first
	std::unordered_map<size_t, std::list<CachedList>::iterator> index;
	size_t list_bytes{0u};

	mutable std::mutex stats_mutex;
	ActorStats stats;

	std::thread thread;
};

DocumentHandle::DocumentHandle(std::unique_ptr<State> state) noexcept
	: state_(std::move(state))
{
}

PdfResult<std::shared_ptr<DocumentHandle>> DocumentHandle::start(const ActorOptions& options) noexcept
{
	try {
		auto state = std::make_unique<State>(options);
		if (failing(detail::ActorFailure::ThreadStart)) {
			throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
		}
		State* const raw = state.get();
		state->thread = std::thread([raw] { raw->main(); });
		return std::shared_ptr<DocumentHandle>(new DocumentHandle(std::move(state)));
	} catch (const std::system_error&) {
		return make_pdf_error(ErrorCode::InternalError, "A document's thread could not start");
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory starting a document's thread");
	}
}

DocumentHandle::~DocumentHandle() noexcept
{
	(void)close();
	if (state_->thread.joinable()) {
		state_->thread.join();
	}
}

std::shared_future<void> DocumentHandle::close() noexcept
{
	State& s = *state_;
	{
		const std::lock_guard lock(s.mutex);
		if (!s.closing) {
			s.closing = true;
			s.close_source->cancel();
			s.wake.notify_all();
		}
	}
	return s.closed_future;
}

DocumentState DocumentHandle::state() const noexcept
{
	return state_->document_state.load();
}

ActorStats DocumentHandle::stats() const noexcept
{
	const std::lock_guard lock(state_->stats_mutex);
	return state_->stats;
}

ActorFuture<void> DocumentHandle::open(std::filesystem::path path, const CancelToken& cancel) noexcept
{
	return state_->post<void>(
		[path = std::move(path)](State& s, const CancelToken&) -> PdfResult<void> {
			const DocumentState now = s.document_state.load();
			if (now != DocumentState::Unopened && now != DocumentState::Failed) {
				return make_pdf_error(ErrorCode::InvalidArgument, "This document is already open");
			}
			s.document_state.store(DocumentState::Opening);
			auto opened = pdf::Document::open_unauthenticated(path);
			if (!opened.has_value()) {
				s.document_state.store(DocumentState::Failed);
				return opened.error();
			}
			s.document = std::move(*opened);
			if (s.document->needs_password()) {
				s.document_state.store(DocumentState::NeedsPassword);
				return make_pdf_error(ErrorCode::NeedsPassword, "The document needs its password");
			}
			s.document_state.store(DocumentState::Opened);
			return {};
		},
		cancel);
}

ActorFuture<void> DocumentHandle::authenticate(std::string_view password) noexcept
{
	std::shared_ptr<Secret> secret;
	try {
		secret = std::make_shared<Secret>(password);
	} catch (const std::bad_alloc&) {
		return {};
	}
	return state_->post<void>([secret = std::move(secret)](State& s, const CancelToken&) -> PdfResult<void> {
		const DocumentState now = s.document_state.load();
		if (now != DocumentState::NeedsPassword && now != DocumentState::WrongPassword) {
			return make_pdf_error(ErrorCode::InvalidArgument, "This document is not waiting for a password");
		}
		auto done = s.document->authenticate(secret->view());
		if (!done.has_value()) {
			if (done.error().code == ErrorCode::WrongPassword) {
				s.document_state.store(DocumentState::WrongPassword);
			}
			return done.error();
		}
		s.document_state.store(DocumentState::Opened);
		return {};
	});
}

ActorFuture<pdf::DocumentDescription> DocumentHandle::get_description() noexcept
{
	return state_->post<pdf::DocumentDescription>([](State& s, const CancelToken&) -> PdfResult<pdf::DocumentDescription> {
		RECTO_PDF_RETURN_IF_ERROR(s.check_open());
		return s.document->description();
	});
}

ActorFuture<std::shared_ptr<const pdf::DisplayList>> DocumentHandle::get_display_list(size_t page_index, const CancelToken& cancel) noexcept
{
	return state_->post<std::shared_ptr<const pdf::DisplayList>>(
		[page_index](State& s, const CancelToken& token) { return s.display_list(page_index, token); }, cancel);
}

ActorFuture<std::vector<pdf::OutlineNode>> DocumentHandle::get_outline() noexcept
{
	return state_->post<std::vector<pdf::OutlineNode>>([](State& s, const CancelToken&) -> PdfResult<std::vector<pdf::OutlineNode>> {
		RECTO_PDF_RETURN_IF_ERROR(s.check_open());
		return s.document->outline();
	});
}

ActorFuture<std::vector<pdf::PageLink>> DocumentHandle::get_page_links(size_t page_index) noexcept
{
	return state_->post<std::vector<pdf::PageLink>>([page_index](State& s, const CancelToken&) -> PdfResult<std::vector<pdf::PageLink>> {
		RECTO_PDF_RETURN_IF_ERROR(s.check_open());
		return s.document->page_links(page_index);
	});
}

ActorFuture<std::vector<pdf::FontSummary>> DocumentHandle::get_fonts() noexcept
{
	return state_->post<std::vector<pdf::FontSummary>>([](State& s, const CancelToken&) -> PdfResult<std::vector<pdf::FontSummary>> {
		RECTO_PDF_RETURN_IF_ERROR(s.check_open());
		return s.document->fonts();
	});
}

ActorFuture<uint32_t> DocumentHandle::get_first_free_object() noexcept
{
	return state_->post<uint32_t>([](State& s, const CancelToken&) -> PdfResult<uint32_t> {
		RECTO_PDF_RETURN_IF_ERROR(s.check_open());
		return static_cast<uint32_t>(pdf::IncrementalWriter::first_new_number(*s.document));
	});
}

ActorFuture<void> DocumentHandle::save_incremental(pdf::ObjectChangeSet changes, std::filesystem::path path, const CancelToken& cancel) noexcept
{
	std::shared_ptr<pdf::ObjectChangeSet> held;
	try {
		held = std::make_shared<pdf::ObjectChangeSet>(std::move(changes));
	} catch (const std::bad_alloc&) {
		return {};
	}
	return state_->post<void>(
		[held = std::move(held), path = std::move(path)](State& s, const CancelToken& token) -> PdfResult<void> {
			RECTO_PDF_RETURN_IF_ERROR(s.check_open());
			RECTO_PDF_TRY(pdf::IncrementalWriter writer, pdf::IncrementalWriter::create(*s.document));
			for (const auto& [number, entry] : held->entries()) {
				RECTO_PDF_RETURN_IF_ERROR(writer.set_object(pdf::IndirectRef(number, entry.generation), entry.value));
			}
			if (token.is_cancelled()) {
				return pdf::detail::cancelled_error();
			}
			RECTO_PDF_TRY(const std::vector<uint8_t> bytes, writer.write());
			return atomic_write_file(path, bytes, token);
		},
		cancel);
}

namespace detail {

ScopedActorFailureForTesting::ScopedActorFailureForTesting(ActorFailure failure) noexcept
{
	g_failure.store(static_cast<uint8_t>(failure));
}

ScopedActorFailureForTesting::~ScopedActorFailureForTesting() noexcept
{
	g_failure.store(0u);
}

uint64_t debug_wiped_passwords() noexcept
{
	return g_wiped_passwords.load(std::memory_order_relaxed);
}

} // namespace detail

int64_t debug_live_documents() noexcept
{
	return g_live_documents.load(std::memory_order_relaxed);
}

} // namespace recto::core
