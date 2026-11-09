// The one object that owns every engine thread and stops them in order (D01 T02 section 10; runtime.h has the contract).
#include "runtime.h"

#include "display_list.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

namespace recto::core {

namespace {

using pdf::ErrorCode;
using pdf::make_pdf_error;
using pdf::PdfResult;

std::atomic<int64_t> g_live_runtimes{0};

} // namespace

struct Runtime::State {
	// Shared with the scheduler's delivery, so a tile asked for after the scheduler has gone is still answered.
	std::shared_ptr<TileDelivery> deliver;
	std::move_only_function<void(int) noexcept> on_step;
	ActorOptions actor_options;

	mutable std::mutex mutex;
	std::condition_variable calls_done;
	bool stopping{false};
	size_t calls{0u}; // calls into the scheduler running now; step 4 waits for none
	std::unique_ptr<TileScheduler> scheduler;
	std::map<DocumentId, std::shared_ptr<DocumentHandle>> documents;
	std::map<uint64_t, JobHooks> jobs;                                         // by registration order
	std::map<uint64_t, std::move_only_function<void() noexcept>> presentation; // likewise
	DocumentId next_document{1u};
	uint64_t next_registration{1u};
	std::optional<ShutdownReport> report;

	void step(int number) noexcept
	{
		if (on_step) {
			on_step(number);
		}
	}

	// Runs `use` on the scheduler unless it is gone or going, counted so step 4 waits for it.
	template <typename Use>
	[[nodiscard]] bool with_scheduler(Use&& use) noexcept
	{
		TileScheduler* scheduler_now = nullptr;
		{
			const std::lock_guard lock(mutex);
			if (stopping || scheduler == nullptr) {
				return false;
			}
			scheduler_now = scheduler.get();
			++calls;
		}
		use(*scheduler_now);
		{
			const std::lock_guard lock(mutex);
			--calls;
		}
		calls_done.notify_all();
		return true;
	}
};

Runtime::Runtime(std::unique_ptr<State> state) noexcept
	: state_(std::move(state))
{
	g_live_runtimes.fetch_add(1, std::memory_order_relaxed);
}

PdfResult<std::unique_ptr<Runtime>> Runtime::create(RuntimeOptions options) noexcept
{
	if (!options.deliver) {
		return make_pdf_error(ErrorCode::InvalidArgument, "A runtime needs somewhere to deliver its tiles");
	}
	std::unique_ptr<State> state;
	try {
		state = std::make_unique<State>();
		state->deliver = std::make_shared<TileDelivery>(std::move(options.deliver));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory creating a runtime");
	}
	state->on_step = std::move(options.on_step);
	state->actor_options = options.actor_options;
	SchedulerOptions scheduler_options;
	scheduler_options.worker_count = options.worker_count;
	scheduler_options.poll_hook = options.worker_poll_hook;
	scheduler_options.poll_hook_context = options.worker_poll_hook_context;
	try {
		scheduler_options.deliver = [deliver = state->deliver](const TileKey& key, pdf::PdfResult<std::shared_ptr<const TileImage>> result) noexcept {
			(*deliver)(key, std::move(result));
		};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory creating a runtime");
	}
	auto scheduler = TileScheduler::create(std::move(scheduler_options));
	if (!scheduler.has_value()) {
		return scheduler.error();
	}
	state->scheduler = std::move(*scheduler);
	try {
		return std::unique_ptr<Runtime>(new Runtime(std::move(state)));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory creating a runtime");
	}
}

Runtime::~Runtime() noexcept
{
	(void)shutdown();
	g_live_runtimes.fetch_sub(1, std::memory_order_relaxed);
}

PdfResult<OpenedDocument> Runtime::open_document(std::filesystem::path path) noexcept
{
	State& s = *state_;
	DocumentId id = 0u;
	{
		const std::lock_guard lock(s.mutex);
		if (s.stopping) {
			return pdf::detail::cancelled_error();
		}
		id = s.next_document++;
	}
	RECTO_PDF_TRY(std::shared_ptr<DocumentHandle> handle, DocumentHandle::start(s.actor_options));
	// Recorded before it is attached, so a shutdown that starts now finds it; each failure below undoes what was done.
	try {
		const std::lock_guard lock(s.mutex);
		if (s.stopping) {
			handle->join();
			return pdf::detail::cancelled_error();
		}
		s.documents.emplace(id, handle);
	} catch (const std::bad_alloc&) {
		handle->join();
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory recording a document");
	}
	// Once shutdown has begun the map is its to walk: it closes and joins this one with the rest.
	const auto forget = [&] {
		{
			const std::lock_guard lock(s.mutex);
			if (s.stopping) {
				return;
			}
			s.documents.erase(id);
		}
		handle->join();
	};
	PdfResult<void> attached = pdf::detail::cancelled_error();
	if (!s.with_scheduler([&](TileScheduler& scheduler) { attached = scheduler.attach(id, handle); })) {
		forget();
		return pdf::detail::cancelled_error();
	}
	if (!attached.has_value()) {
		forget();
		return attached.error();
	}
	ActorFuture<void> opened = handle->open(std::move(path));
	if (!opened.valid()) {
		(void)s.with_scheduler([&](TileScheduler& scheduler) { scheduler.detach(id); });
		forget();
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory asking a document to open");
	}
	return OpenedDocument{id, std::move(opened)};
}

std::shared_ptr<DocumentHandle> Runtime::document(DocumentId id) const noexcept
{
	const std::lock_guard lock(state_->mutex);
	const auto found = state_->documents.find(id);
	return found != state_->documents.end() && !state_->stopping ? found->second : nullptr;
}

PdfResult<void> Runtime::close_document(DocumentId id) noexcept
{
	State& s = *state_;
	std::shared_ptr<DocumentHandle> handle;
	{
		const std::lock_guard lock(s.mutex);
		const auto found = s.documents.find(id);
		if (found == s.documents.end() || s.stopping) {
			return make_pdf_error(ErrorCode::InvalidArgument, "That document is not open here");
		}
		handle = std::move(found->second);
		s.documents.erase(found);
	}
	(void)s.with_scheduler([&](TileScheduler& scheduler) { scheduler.detach(id); });
	handle->join();
	return {};
}

void Runtime::update_viewport(DocumentId id, std::vector<TileRequest> tiles) noexcept
{
	State& s = *state_;
	// Moved in only if the scheduler takes them; otherwise each is answered here.
	if (!s.with_scheduler([&](TileScheduler& scheduler) { scheduler.update_viewport(id, std::move(tiles)); })) {
		for (const TileRequest& request : tiles) {
			(*s.deliver)(request.key, pdf::detail::cancelled_error());
		}
	}
}

void Runtime::cancel_document(DocumentId id) noexcept
{
	(void)state_->with_scheduler([&](TileScheduler& scheduler) { scheduler.cancel_document(id); });
}

PdfResult<uint64_t> Runtime::register_job(JobHooks hooks) noexcept
{
	State& s = *state_;
	try {
		const std::lock_guard lock(s.mutex);
		if (s.stopping) {
			return pdf::detail::cancelled_error();
		}
		const uint64_t id = s.next_registration++;
		s.jobs.emplace(id, std::move(hooks));
		return id;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory registering a job");
	}
}

void Runtime::unregister_job(uint64_t id) noexcept
{
	const std::lock_guard lock(state_->mutex);
	if (!state_->stopping) {
		state_->jobs.erase(id);
	}
}

PdfResult<uint64_t> Runtime::register_presentation_hook(std::move_only_function<void() noexcept> hook) noexcept
{
	State& s = *state_;
	try {
		const std::lock_guard lock(s.mutex);
		if (s.stopping) {
			return pdf::detail::cancelled_error();
		}
		const uint64_t id = s.next_registration++;
		s.presentation.emplace(id, std::move(hook));
		return id;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory registering a presentation hook");
	}
}

void Runtime::unregister_presentation_hook(uint64_t id) noexcept
{
	const std::lock_guard lock(state_->mutex);
	if (!state_->stopping) {
		state_->presentation.erase(id);
	}
}

ShutdownReport Runtime::shutdown() noexcept
{
	State& s = *state_;
	{
		const std::lock_guard lock(s.mutex);
		if (s.report.has_value()) {
			return *s.report;
		}
		s.stopping = true; // nothing new is taken from here on; what is registered stays where this thread reads it
	}
	// Nothing below allocates: it walks what was registered, and releases it.

	// 1. Cancel every tile, request and job.
	s.step(1);
	if (s.scheduler != nullptr) {
		for (const auto& [id, handle] : s.documents) {
			s.scheduler->cancel_document(id);
		}
	}
	for (auto& [id, job] : s.jobs) {
		if (job.cancel) {
			job.cancel();
		}
	}

	// 2. Close every document, then join every actor: all are cancelled before any is waited for.
	s.step(2);
	for (const auto& [id, handle] : s.documents) {
		(void)handle->close();
	}
	for (const auto& [id, handle] : s.documents) {
		handle->join();
	}

	// 3. Join every job.
	s.step(3);
	for (auto& [id, job] : s.jobs) {
		if (job.join) {
			job.join();
		}
	}

	// 4. Drain and join the render workers, once no call is still inside the scheduler.
	s.step(4);
	{
		std::unique_lock lock(s.mutex);
		s.calls_done.wait(lock, [&] { return s.calls == 0u; });
	}
	s.scheduler.reset();

	// 5. The presentation hooks, in the order they came.
	s.step(5);
	for (auto& [id, hook] : s.presentation) {
		if (hook) {
			hook();
		}
	}

	// 6. What is still alive.
	s.step(6);
	s.documents.clear();
	s.jobs.clear();
	s.presentation.clear();
	const ShutdownReport report{debug_live_documents(), pdf::debug_live_display_lists(), debug_live_workers()};
#ifndef NDEBUG
	if (g_live_runtimes.load(std::memory_order_relaxed) == 1) {
		assert(report == ShutdownReport{} && "a document, display list or worker outlived the runtime's shutdown");
	}
#endif
	const std::lock_guard lock(s.mutex);
	s.report = report;
	return report;
}

int64_t debug_live_runtimes() noexcept
{
	return g_live_runtimes.load(std::memory_order_relaxed);
}

} // namespace recto::core
