// Which tiles render first (D01 T02 section 9; tile_scheduler.h has the contract).
#include "tile_scheduler.h"

#include <array>
#include <condition_variable>
#include <exception>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace recto::core {

namespace {

using pdf::CancelSource;
using pdf::ErrorCode;
using pdf::make_pdf_error;
using pdf::PdfResult;

using Delivered = PdfResult<std::shared_ptr<const TileImage>>;

[[nodiscard]] uint64_t mixed(uint64_t h, uint64_t v) noexcept
{
	h ^= v + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
	return h;
}

struct PageKey {
	uint64_t doc_id{0};
	uint32_t page_index{0};

	constexpr bool operator==(const PageKey&) const noexcept = default;
};

struct PageKeyHash {
	[[nodiscard]] size_t operator()(const PageKey& key) const noexcept
	{
		return static_cast<size_t>(mixed(mixed(0u, key.doc_id), key.page_index));
	}
};

} // namespace

size_t TileKeyHash::operator()(const TileKey& key) const noexcept
{
	uint64_t h = mixed(0u, key.doc_id);
	h = mixed(h, key.page_index);
	h = mixed(h, key.zoom_bucket_milli);
	h = mixed(h, static_cast<uint32_t>(key.rotation_deg));
	h = mixed(h, key.tile_x);
	h = mixed(h, key.tile_y);
	return static_cast<size_t>(h);
}

struct TileScheduler::State final : TileJobSource, std::enable_shared_from_this<TileScheduler::State> {
	enum class Stage : uint8_t { Fetching, Queued, Rendering };

	struct Entry {
		TileRequest request;
		uint64_t serial{0};
		Stage stage{Stage::Fetching};
		std::optional<CancelSource> cancel;
		bool wanted{true};
		std::optional<TileRequest> again; // asked for once more while its cancelled render finishes
	};

	struct Fetch {
		uint64_t generation{0};
		size_t users{0};
		std::optional<CancelSource> cancel;
		std::shared_ptr<const pdf::DisplayList> list;
	};

	struct PendingStart {
		PageKey page;
		uint64_t generation{0};
		std::shared_ptr<DocumentHandle> document;
		pdf::CancelToken token;
	};

	using Out = std::vector<std::pair<TileKey, Delivered>>;

	explicit State(TileDelivery given)
		: deliver(std::move(given))
	{
	}

	TileDelivery deliver;
	std::unique_ptr<RenderPool> pool;

	std::mutex mutex;
	std::condition_variable work_ready;
	bool stopping{false};
	bool paused{false};
	std::unordered_map<uint64_t, std::shared_ptr<DocumentHandle>> documents;
	std::unordered_map<TileKey, Entry> entries;
	std::unordered_map<PageKey, Fetch, PageKeyHash> fetches;
	std::array<std::map<uint64_t, TileKey>, 4> queues; // by tier, first come first
	std::unordered_map<uint64_t, TileKey> rendering;    // by job ticket
	uint64_t next_serial{1u};
	uint64_t next_ticket{1u};
	uint64_t next_generation{1u};

	[[nodiscard]] static size_t tier(TilePriority priority) noexcept
	{
		const auto value = static_cast<size_t>(priority);
		return value >= 1u && value <= 4u ? value - 1u : 3u;
	}

	[[nodiscard]] static PageKey page_of(const TileKey& key) noexcept
	{
		return PageKey{key.doc_id, key.page_index};
	}

	void hand_out(Out& out) noexcept
	{
		for (auto& [key, result] : out) {
			deliver(key, std::move(result));
		}
		out.clear();
	}

	void enqueue(Entry& entry) noexcept
	{
		entry.stage = Stage::Queued;
		queues[tier(entry.request.priority)].emplace(entry.serial, entry.request.key);
	}

	void dequeue(const Entry& entry) noexcept
	{
		queues[tier(entry.request.priority)].erase(entry.serial);
	}

	void release_page(const PageKey& page) noexcept
	{
		const auto found = fetches.find(page);
		if (found == fetches.end()) {
			return;
		}
		if (found->second.users > 0u) {
			--found->second.users;
		}
		if (found->second.users == 0u) {
			if (found->second.cancel.has_value()) {
				found->second.cancel->cancel(); // no tile of the page wants its list any more
			}
			fetches.erase(found);
		}
	}

	// Takes `request` as a new entry: queued when its page's list is here, else waiting on the page's one fetch.
	void take(const TileRequest& request, Out& out, std::vector<PendingStart>& starts)
	{
		if (entries.size() >= kMaxScheduledTiles) {
			out.emplace_back(request.key, pdf::make_refusal_error(ErrorCode::ResourceLimit, pdf::RefusalReason::SchedulerFull,
				entries.size() + 1u, kMaxScheduledTiles, "More tiles were requested than the scheduler holds"));
			return;
		}
		auto cancel = CancelSource::create();
		if (!cancel.has_value()) {
			out.emplace_back(request.key, cancel.error());
			return;
		}
		const auto document = documents.find(request.key.doc_id);
		if (document == documents.end()) {
			out.emplace_back(request.key, pdf::detail::cancelled_error()); // detached while it waited
			return;
		}
		const PageKey page = page_of(request.key);
		auto [fetch, made] = fetches.try_emplace(page);
		if (made) {
			auto fetch_cancel = CancelSource::create();
			if (!fetch_cancel.has_value()) {
				fetches.erase(fetch);
				out.emplace_back(request.key, fetch_cancel.error());
				return;
			}
			fetch->second.generation = next_generation++;
			fetch->second.cancel.emplace(*fetch_cancel);
			starts.push_back(PendingStart{page, fetch->second.generation, document->second, fetch_cancel->token()});
		}
		auto [slot, inserted] = entries.try_emplace(request.key);
		Entry& entry = slot->second;
		entry.request = request;
		entry.serial = next_serial++;
		entry.cancel.emplace(*cancel);
		++fetch->second.users;
		if (fetch->second.list != nullptr) {
			enqueue(entry);
		}
	}

	// No longer wanted: a queued or fetching tile is answered `Cancelled` now; a rendering one is cancelled and answered
	// when it returns.
	void drop(Entry& entry, Out& out)
	{
		if (entry.again.has_value()) {
			out.emplace_back(entry.again->key, pdf::detail::cancelled_error());
			entry.again.reset();
		}
		if (!entry.wanted) {
			return;
		}
		if (entry.stage == Stage::Rendering) {
			entry.wanted = false;
			entry.cancel->cancel();
			return;
		}
		if (entry.stage == Stage::Queued) {
			dequeue(entry);
		}
		const TileKey key = entry.request.key;
		out.emplace_back(key, pdf::detail::cancelled_error());
		entries.erase(key);
		release_page(page_of(key));
	}

	void start_fetches(std::vector<PendingStart>& starts) noexcept
	{
		for (PendingStart& start : starts) {
			const std::weak_ptr<State> self = weak_from_this();
			start.document->get_display_list(start.page.page_index, start.token,
				[self, page = start.page, generation = start.generation](PdfResult<std::shared_ptr<const pdf::DisplayList>> list) noexcept {
					if (const auto state = self.lock()) {
						state->arrived(page, generation, std::move(list));
					}
				});
		}
		starts.clear();
	}

	void arrived(const PageKey& page, uint64_t generation, PdfResult<std::shared_ptr<const pdf::DisplayList>> list) noexcept
	{
		Out out;
		bool queued = false;
		try {
			const std::lock_guard lock(mutex);
			const auto found = fetches.find(page);
			if (stopping || found == fetches.end() || found->second.generation != generation) {
				return; // nobody waits for this one any more
			}
			if (list.has_value()) {
				found->second.list = std::move(*list);
				found->second.cancel.reset();
				for (auto& [key, entry] : entries) {
					if (page_of(key) == page && entry.stage == Stage::Fetching) {
						enqueue(entry);
						queued = true;
					}
				}
			} else {
				std::vector<TileKey> failed;
				for (auto& [key, entry] : entries) {
					if (page_of(key) == page && entry.stage == Stage::Fetching) {
						failed.push_back(key);
					}
				}
				for (const TileKey& key : failed) {
					out.emplace_back(key, list.error());
					entries.erase(key);
				}
				fetches.erase(found); // a later request asks again
			}
		} catch (const std::bad_alloc&) {
			// `out` holds what it got; the rest are answered when they leave the view or the scheduler goes.
		}
		if (queued) {
			work_ready.notify_all();
		}
		hand_out(out);
	}

	std::optional<TileJob> next_job() noexcept override
	{
		std::unique_lock lock(mutex);
		const auto any_queued = [&] {
			for (const auto& queue : queues) {
				if (!queue.empty()) {
					return true;
				}
			}
			return false;
		};
		work_ready.wait(lock, [&] { return stopping || (!paused && any_queued()); });
		if (stopping) {
			return std::nullopt;
		}
		for (auto& queue : queues) {
			if (queue.empty()) {
				continue;
			}
			const TileKey key = queue.begin()->second;
			queue.erase(queue.begin());
			Entry& entry = entries.at(key);
			entry.stage = Stage::Rendering;
			const uint64_t ticket = next_ticket++;
			try {
				rendering.emplace(ticket, key);
			} catch (const std::bad_alloc&) {
				enqueue(entry);
				return std::nullopt; // this worker stops; the others and the destructor still answer every tile
			}
			return TileJob{ticket, fetches.at(page_of(key)).list, entry.request.spec, entry.cancel->token()};
		}
		return std::nullopt;
	}

	void finished(const TileJob& job, Delivered result) noexcept override
	{
		Out out;
		std::vector<PendingStart> starts;
		bool queued = false;
		{
			const std::lock_guard lock(mutex);
			const auto ticket = rendering.find(job.ticket);
			if (ticket == rendering.end()) {
				return;
			}
			const TileKey key = ticket->second;
			rendering.erase(ticket);
			const auto found = entries.find(key);
			if (found == entries.end()) {
				return;
			}
			const bool wanted = found->second.wanted;
			std::optional<TileRequest> again = std::move(found->second.again);
			entries.erase(found);
			try {
				out.emplace_back(key, wanted ? std::move(result) : Delivered(pdf::detail::cancelled_error()));
				if (again.has_value() && !stopping) {
					take(*again, out, starts); // before the page is let go, so its list stays
					queued = true;
				} else if (again.has_value()) {
					out.emplace_back(again->key, pdf::detail::cancelled_error());
				}
			} catch (const std::bad_alloc&) {
				// Nothing to give back to: the tile is answered through `out` as far as it got.
			}
			release_page(page_of(key));
		}
		start_fetches(starts);
		if (queued) {
			work_ready.notify_one();
		}
		hand_out(out);
	}

	void shutdown() noexcept
	{
		Out out;
		{
			const std::lock_guard lock(mutex);
			stopping = true;
			try {
				std::vector<TileKey> keys;
				keys.reserve(entries.size());
				for (const auto& [key, entry] : entries) {
					keys.push_back(key);
				}
				for (const TileKey& key : keys) {
					if (const auto found = entries.find(key); found != entries.end()) {
						drop(found->second, out);
					}
				}
			} catch (const std::bad_alloc&) {
				// What is left is answered by the loop below without allocating more.
			}
			for (auto& [page, fetch] : fetches) {
				if (fetch.cancel.has_value()) {
					fetch.cancel->cancel();
				}
			}
			documents.clear();
		}
		work_ready.notify_all();
		hand_out(out);
		pool.reset(); // joins: a tile still rendering is answered by `finished` on its way out
		// Whatever could not be listed above for want of memory.
		const std::lock_guard lock(mutex);
		for (auto& [key, entry] : entries) {
			if (entry.wanted || entry.again.has_value()) {
				deliver(key, pdf::detail::cancelled_error());
			}
		}
		entries.clear();
		fetches.clear();
	}
};

TileScheduler::TileScheduler(std::shared_ptr<State> state) noexcept
	: state_(std::move(state))
{
}

PdfResult<std::unique_ptr<TileScheduler>> TileScheduler::create(SchedulerOptions options) noexcept
{
	if (!options.deliver) {
		return make_pdf_error(ErrorCode::InvalidArgument, "A tile scheduler needs somewhere to deliver its tiles");
	}
	if (options.worker_count > kMaxRenderWorkers) {
		return make_pdf_error(ErrorCode::InvalidArgument, "A tile scheduler has 1 to 16 workers, or 0 for the default");
	}
	const uint32_t workers = options.worker_count == 0u ? RenderPool::default_worker_count() : options.worker_count;
	std::shared_ptr<State> state;
	std::unique_ptr<TileScheduler> scheduler;
	try {
		state = std::make_shared<State>(std::move(options.deliver));
		scheduler.reset(new TileScheduler(state));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory creating a tile scheduler");
	}
	auto pool = RenderPool::start(*state, workers, options.poll_hook, options.poll_hook_context);
	if (!pool.has_value()) {
		scheduler->state_.reset(); // no worker ever asked it for a job, so there is nothing to shut down
		return pool.error();
	}
	state->pool = std::move(*pool);
	return scheduler;
}

TileScheduler::~TileScheduler() noexcept
{
	if (state_ != nullptr) {
		state_->shutdown();
	}
}

PdfResult<void> TileScheduler::attach(uint64_t doc_id, std::shared_ptr<DocumentHandle> document) noexcept
{
	if (document == nullptr) {
		return make_pdf_error(ErrorCode::InvalidArgument, "A tile scheduler needs a document to attach");
	}
	State& s = *state_;
	try {
		const std::lock_guard lock(s.mutex);
		if (s.stopping) {
			return pdf::detail::cancelled_error();
		}
		if (!s.documents.try_emplace(doc_id, std::move(document)).second) {
			return make_pdf_error(ErrorCode::InvalidArgument, "That document id is already attached");
		}
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory attaching a document");
	}
	return {};
}

void TileScheduler::detach(uint64_t doc_id) noexcept
{
	cancel_document(doc_id);
	const std::lock_guard lock(state_->mutex);
	state_->documents.erase(doc_id);
}

void TileScheduler::update_viewport(uint64_t doc_id, std::vector<TileRequest> tiles) noexcept
{
	State& s = *state_;
	State::Out out;
	std::vector<State::PendingStart> starts;
	try {
		const std::lock_guard lock(s.mutex);
		const bool known = s.documents.contains(doc_id);
		std::unordered_set<TileKey> wanted;
		wanted.reserve(tiles.size());
		for (const TileRequest& request : tiles) {
			if (s.stopping || !known || request.key.doc_id != doc_id) {
				out.emplace_back(request.key, s.stopping ? pdf::PdfError(pdf::detail::cancelled_error())
														 : make_pdf_error(ErrorCode::InvalidArgument, "That tile's document is not attached here"));
				continue;
			}
			if (!wanted.insert(request.key).second) {
				continue; // a key twice in one set is one request
			}
			const auto found = s.entries.find(request.key);
			if (found == s.entries.end()) {
				s.take(request, out, starts);
				continue;
			}
			State::Entry& entry = found->second;
			if (!entry.wanted) {
				entry.again = request; // its cancelled render finishes first, then it is taken again
				continue;
			}
			if (entry.request.priority != request.priority) {
				if (entry.stage == State::Stage::Queued) {
					s.dequeue(entry);
					entry.request.priority = request.priority;
					s.enqueue(entry);
				} else {
					entry.request.priority = request.priority;
				}
			}
		}
		std::vector<TileKey> leaving;
		for (const auto& [key, entry] : s.entries) {
			if (key.doc_id == doc_id && !wanted.contains(key)) {
				leaving.push_back(key);
			}
		}
		for (const TileKey& key : leaving) {
			if (const auto found = s.entries.find(key); found != s.entries.end()) {
				s.drop(found->second, out);
			}
		}
	} catch (const std::bad_alloc&) {
		// What was taken before memory ran out stays taken, and is answered.
	}
	s.start_fetches(starts);
	s.work_ready.notify_all();
	s.hand_out(out);
}

void TileScheduler::cancel_document(uint64_t doc_id) noexcept
{
	State& s = *state_;
	State::Out out;
	try {
		const std::lock_guard lock(s.mutex);
		std::vector<TileKey> leaving;
		for (const auto& [key, entry] : s.entries) {
			if (key.doc_id == doc_id) {
				leaving.push_back(key);
			}
		}
		for (const TileKey& key : leaving) {
			if (const auto found = s.entries.find(key); found != s.entries.end()) {
				s.drop(found->second, out);
			}
		}
	} catch (const std::bad_alloc&) {
		// The rest are answered when the scheduler goes.
	}
	s.hand_out(out);
}

void TileScheduler::pause() noexcept
{
	const std::lock_guard lock(state_->mutex);
	state_->paused = true;
}

void TileScheduler::resume() noexcept
{
	{
		const std::lock_guard lock(state_->mutex);
		state_->paused = false;
	}
	state_->work_ready.notify_all();
}

uint32_t TileScheduler::worker_count() const noexcept
{
	return state_->pool != nullptr ? state_->pool->size() : 0u;
}

} // namespace recto::core
