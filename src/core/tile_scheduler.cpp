// Which tiles render first (D01 T02 section 9; tile_scheduler.h has the contract).
//
// Nothing is allocated for a request after it is taken: its entry, its index slot and its queue node are made when it
// arrives, and from then on it only moves between lists by `splice` and between queues by node handle. So memory running
// out can refuse a new request, never lose one already taken, end a worker, or stop a cancel.
#include "tile_scheduler.h"

#include <array>
#include <condition_variable>
#include <exception>
#include <list>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <unordered_map>
#include <utility>

namespace recto::core {

namespace {

using pdf::CancelSource;
using pdf::ErrorCode;
using pdf::make_pdf_error;
using pdf::PdfResult;

using Delivered = PdfResult<std::shared_ptr<const TileImage>>;

// Page lists asked of one document's actor at once. More would sit in its queue, where a visible page cannot overtake
// them; fewer would leave a worker idle between pages.
constexpr size_t kFetchesInFlight = 2u;

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

[[nodiscard]] size_t tier(TilePriority priority) noexcept
{
	const auto value = static_cast<size_t>(priority);
	return value >= 1u && value <= 4u ? value - 1u : 3u;
}

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

	struct Entry;
	using Entries = std::list<Entry>;
	using Queue = std::map<uint64_t, Entry*>; // by serial: first come first

	struct Entry {
		TileRequest request;
		uint64_t serial{0};
		Stage stage{Stage::Fetching};
		std::optional<CancelSource> cancel;
		bool wanted{true};
		uint64_t seen{0};            // the `update_viewport` round that last named it
		Queue::node_type queue_node; // made when taken, so queueing it never allocates
		Entry* again{nullptr};       // asked for once more while its cancelled render finishes (held in `waiting`)
		std::optional<Delivered> answer;
		Entry* next_out{nullptr};    // the next answer handed out with it
		std::list<Entry>::iterator self{}; // where it waits in `answered`, to be erased once delivered
	};

	// Answers to hand out once the lock is let go: a chain through the entries, so collecting it allocates nothing (an
	// MSVC `std::list` allocates even when default-constructed).
	struct Out {
		Entry* head{nullptr};
		Entry* tail{nullptr};
		[[nodiscard]] bool empty() const noexcept
		{
			return head == nullptr;
		}
	};

	struct Fetch {
		uint64_t generation{0};
		size_t users{0};
		bool started{false};
		std::optional<CancelSource> cancel;
		std::shared_ptr<const pdf::DisplayList> list;
	};

	struct Start {
		PageKey page;
		uint64_t generation{0};
		std::shared_ptr<DocumentHandle> document;
		pdf::CancelToken token;
	};

	explicit State(TileDelivery given)
		: deliver(std::move(given))
	{
	}

	TileDelivery deliver;
	std::unique_ptr<RenderPool> pool;

	std::mutex mutex;
	std::condition_variable work_ready;
	std::condition_variable drained;
	bool stopping{false};
	bool paused{false};
	size_t delivering{0u}; // answers handed out but not yet returned from `deliver`
	std::unordered_map<uint64_t, std::shared_ptr<DocumentHandle>> documents;
	// Made with the scheduler and swapped with `documents` at shutdown, so letting the handles go allocates nothing (an
	// MSVC `unordered_map` allocates even when empty).
	std::unordered_map<uint64_t, std::shared_ptr<DocumentHandle>> released;
	Entries live;    // every request taken and not yet answered, `again` ones apart
	Entries waiting; // `again` requests, each behind its cancelled render
	Entries answered; // answered, until the thread that answered them has delivered them
	std::unordered_map<TileKey, Entries::iterator> index; // into `live`
	std::unordered_map<PageKey, Fetch, PageKeyHash> fetches;
	std::array<Queue, 4> queues;
	uint64_t next_serial{1u};
	uint64_t next_generation{1u};
	uint64_t round{0u};

	[[nodiscard]] static PageKey page_of(const TileKey& key) noexcept
	{
		return PageKey{key.doc_id, key.page_index};
	}

	// --- Answering. ---------------------------------------------------------------------------------------------

	// Moves `entry` from `from` to `answered`, answered `result`, and chains it on `out`: no allocation, so it cannot fail.
	void answer(Entries& from, Entries::iterator entry, Delivered result, Out& out) noexcept
	{
		entry->answer.emplace(std::move(result));
		entry->self = entry;
		entry->next_out = nullptr;
		answered.splice(answered.end(), from, entry);
		Entry* const at = &*entry;
		if (out.tail == nullptr) {
			out.head = at;
		} else {
			out.tail->next_out = at;
		}
		out.tail = at;
	}

	// Hands `out` to `deliver` outside the lock, counted so the destructor waits for the last one. Only this thread
	// touches the entries on its chain until it erases them.
	void hand_out(Out& out) noexcept
	{
		if (out.empty()) {
			return;
		}
		for (Entry* entry = out.head; entry != nullptr; entry = entry->next_out) {
			deliver(entry->request.key, std::move(*entry->answer));
		}
		const std::lock_guard lock(mutex);
		for (Entry* entry = out.head; entry != nullptr;) {
			Entry* const next = entry->next_out;
			answered.erase(entry->self);
			entry = next;
		}
		out = Out{};
		--delivering;
		if (delivering == 0u) {
			drained.notify_all();
		}
	}

	// Called under the lock before it is released with answers in `out`.
	void counting(const Out& out) noexcept
	{
		if (!out.empty()) {
			++delivering;
		}
	}

	// --- Queues and pages. ----------------------------------------------------------------------------------------

	void enqueue(Entry& entry) noexcept
	{
		entry.stage = Stage::Queued;
		queues[tier(entry.request.priority)].insert(std::move(entry.queue_node));
	}

	void dequeue(Entry& entry) noexcept
	{
		entry.queue_node = queues[tier(entry.request.priority)].extract(entry.serial);
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

	// Makes `request` an entry in a list of its own, ready to join `live`, or the reason it cannot be taken.
	[[nodiscard]] std::optional<pdf::PdfError> make(const TileRequest& request, Entries& made)
	{
		if (index.size() + waiting.size() >= kMaxScheduledTiles) {
			return pdf::make_refusal_error(ErrorCode::ResourceLimit, pdf::RefusalReason::SchedulerFull,
				index.size() + waiting.size() + 1u, kMaxScheduledTiles, "More tiles were requested than the scheduler holds");
		}
		if (!documents.contains(request.key.doc_id)) {
			return pdf::detail::cancelled_error(); // detached while it waited
		}
		auto cancel = CancelSource::create();
		if (!cancel.has_value()) {
			return cancel.error();
		}
		Entry& entry = made.emplace_back();
		entry.request = request;
		entry.serial = next_serial++;
		entry.cancel.emplace(*cancel);
		Queue node_maker;
		node_maker.emplace(entry.serial, &entry);
		entry.queue_node = node_maker.extract(entry.serial);
		return std::nullopt;
	}

	// Takes a made entry into `live`: queued when its page's list is here, else waiting on the page's one fetch.
	void admit(Entries& made, uint64_t seen)
	{
		Entry& entry = made.front();
		const PageKey page = page_of(entry.request.key);
		auto [fetch, created] = fetches.try_emplace(page);
		if (created) {
			auto fetch_cancel = CancelSource::create();
			if (!fetch_cancel.has_value()) {
				fetches.erase(fetch);
				throw std::bad_alloc();
			}
			fetch->second.generation = next_generation++;
			fetch->second.cancel.emplace(*fetch_cancel);
		}
		try {
			index.emplace(entry.request.key, made.begin());
		} catch (...) {
			if (created) {
				fetches.erase(fetch);
			}
			throw;
		}
		entry.seen = seen;
		++fetch->second.users;
		live.splice(live.end(), made, made.begin());
		if (fetch->second.list != nullptr) {
			enqueue(entry);
		}
	}

	// The best tier any live tile of `page` wants.
	[[nodiscard]] size_t page_tier(const PageKey& page) const noexcept
	{
		size_t best = 4u;
		for (const Entry& entry : live) {
			if (entry.wanted && page_of(entry.request.key) == page) {
				best = std::min(best, tier(entry.request.priority));
			}
		}
		return best;
	}

	// Fetches of `doc_id` asked of its actor and not yet answered.
	[[nodiscard]] size_t in_flight(uint64_t doc_id) const noexcept
	{
		size_t count = 0u;
		for (const auto& [page, fetch] : fetches) {
			count += page.doc_id == doc_id && fetch.started && fetch.list == nullptr ? 1u : 0u;
		}
		return count;
	}

	// Answers every tile waiting on `page`'s list with `error` and forgets the fetch, so a later request asks again.
	void fail_page(const PageKey& page, const pdf::PdfError& error, Out& out) noexcept
	{
		for (auto it = live.begin(); it != live.end();) {
			const auto next = std::next(it);
			if (page_of(it->request.key) == page && it->stage == Stage::Fetching) {
				index.erase(it->request.key);
				answer(live, it, error, out);
			}
			it = next;
		}
		fetches.erase(page);
	}

	// The page fetches to send now: for each document, the best-placed waiting pages, up to `kFetchesInFlight` asked of
	// its actor at once, so a thumbnail backlog never stands in a visible page's way at the actor. Choosing allocates
	// nothing; a page whose fetch cannot be queued for want of memory has its tiles answered, so none is left waiting.
	void plan_fetches(std::vector<Start>& starts, Out& out) noexcept
	{
		for (;;) {
			const Fetch* best = nullptr;
			PageKey best_page{};
			size_t best_tier = 5u;
			for (const auto& [page, fetch] : fetches) {
				if (fetch.started) {
					continue;
				}
				const size_t t = page_tier(page);
				if ((t < best_tier || (t == best_tier && fetch.generation < best->generation)) && in_flight(page.doc_id) < kFetchesInFlight) {
					best = &fetch;
					best_page = page;
					best_tier = t;
				}
			}
			if (best == nullptr) {
				return;
			}
			Fetch& chosen = fetches.at(best_page);
			const auto document = documents.find(best_page.doc_id);
			if (document == documents.end()) {
				chosen.started = true; // detached: its tiles are answered, and the fetch goes with the last of them
				continue;
			}
			try {
				starts.push_back(Start{best_page, chosen.generation, document->second, chosen.cancel->token()});
			} catch (const std::bad_alloc&) {
				fail_page(best_page, make_pdf_error(ErrorCode::OutOfMemory, "Out of memory asking for a page's list"), out);
				continue;
			}
			chosen.started = true;
		}
	}
	void start_fetches(std::vector<Start>& starts) noexcept
	{
		for (Start& start : starts) {
			const std::weak_ptr<State> self = weak_from_this();
			DocumentHandle::DisplayListCallback done;
			try {
				done = [self, page = start.page, generation = start.generation](PdfResult<std::shared_ptr<const pdf::DisplayList>> list) noexcept {
					if (const auto state = self.lock()) {
						state->arrived(page, generation, std::move(list));
					}
				};
			} catch (const std::bad_alloc&) {
				// No memory for the callback: the page's tiles are answered as its fetch would have failed.
				arrived(start.page, start.generation, make_pdf_error(ErrorCode::OutOfMemory, "Out of memory asking for a page's list"));
				continue;
			}
			start.document->get_display_list(start.page.page_index, start.token, std::move(done));
		}
		starts.clear();
	}

	// Plans and sends what can go now; called with the lock released. A fetch refused at admission answers at once, on
	// this thread, and that answer pumps again: the inner call only asks the outer one for another pass, so a backlog of
	// refusals is drained in a loop, not by recursion.
	void pump() noexcept
	{
		thread_local State* pumping = nullptr; // the scheduler this thread is pumping, if any
		thread_local bool again = false;
		if (pumping == this) {
			again = true;
			return;
		}
		State* const outer = std::exchange(pumping, this);
		const bool outer_again = std::exchange(again, false);
		do {
			again = false;
			std::vector<Start> starts;
			Out out;
			{
				const std::lock_guard lock(mutex);
				if (stopping) {
					break;
				}
				plan_fetches(starts, out);
				counting(out);
			}
			hand_out(out);
			start_fetches(starts);
		} while (again);
		pumping = outer;
		again = outer_again;
	}

	// No longer wanted: a queued or fetching tile is answered `Cancelled`; a rendering one is cancelled and answered
	// when it returns. Its `again` request, if any, is answered `Cancelled` too.
	void drop(Entries::iterator entry, Out& out) noexcept
	{
		if (entry->again != nullptr) {
			drop_again(*entry, out);
		}
		if (!entry->wanted) {
			return;
		}
		if (entry->stage == Stage::Rendering) {
			entry->wanted = false;
			entry->cancel->cancel();
			return;
		}
		if (entry->stage == Stage::Queued) {
			dequeue(*entry);
		}
		const TileKey key = entry->request.key;
		index.erase(key);
		answer(live, entry, pdf::detail::cancelled_error(), out);
		release_page(page_of(key));
	}

	void drop_again(Entry& entry, Out& out) noexcept
	{
		for (auto it = waiting.begin(); it != waiting.end(); ++it) {
			if (&*it == entry.again) {
				answer(waiting, it, pdf::detail::cancelled_error(), out);
				break;
			}
		}
		entry.again = nullptr;
	}

	// --- From actors. ---------------------------------------------------------------------------------------------

	void arrived(const PageKey& page, uint64_t generation, PdfResult<std::shared_ptr<const pdf::DisplayList>> list) noexcept
	{
		Out out;
		bool queued = false;
		{
			const std::lock_guard lock(mutex);
			const auto found = fetches.find(page);
			if (stopping || found == fetches.end() || found->second.generation != generation) {
				return; // nobody waits for this one any more
			}
			if (list.has_value()) {
				found->second.list = std::move(*list);
				found->second.cancel.reset();
				for (Entry& entry : live) {
					if (page_of(entry.request.key) == page && entry.stage == Stage::Fetching) {
						enqueue(entry);
						queued = true;
					}
				}
			} else {
				fail_page(page, list.error(), out); // a later request asks again
			}
			counting(out);
		}
		if (queued) {
			work_ready.notify_all();
		}
		hand_out(out);
		pump();
	}

	// --- From workers. --------------------------------------------------------------------------------------------

	std::optional<TileJob> next_job() noexcept override
	{
		std::unique_lock lock(mutex);
		const auto any_queued = [&] {
			for (const Queue& queue : queues) {
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
		for (Queue& queue : queues) {
			if (queue.empty()) {
				continue;
			}
			Entry& entry = *queue.begin()->second;
			entry.queue_node = queue.extract(queue.begin());
			entry.stage = Stage::Rendering;
			// The entry stays in `live` until its result comes back, so its address names the job.
			return TileJob{reinterpret_cast<uint64_t>(&entry), fetches.at(page_of(entry.request.key)).list, entry.request.spec,
				entry.cancel->token()};
		}
		return std::nullopt;
	}

	void finished(const TileJob& job, Delivered result) noexcept override
	{
		Out out;
		bool queued = false;
		{
			const std::lock_guard lock(mutex);
			const auto entry = std::find_if(live.begin(), live.end(), [&](const Entry& e) { return reinterpret_cast<uint64_t>(&e) == job.ticket; });
			if (entry == live.end()) {
				return;
			}
			const TileKey key = entry->request.key;
			Entry* const again = entry->again;
			entry->again = nullptr;
			index.erase(key);
			answer(live, entry, entry->wanted ? std::move(result) : Delivered(pdf::detail::cancelled_error()), out);
			if (again != nullptr) {
				const auto waiting_at = std::find_if(waiting.begin(), waiting.end(), [&](const Entry& e) { return &e == again; });
				if (waiting_at == waiting.end()) {
					// Not held: nothing to answer.
				} else if (stopping) {
					answer(waiting, waiting_at, pdf::detail::cancelled_error(), out);
				} else {
					// Its slot and queue node were made when it was asked for, so taking it now allocates only the index slot;
					// without that, it is answered.
					try {
						index.emplace(key, waiting_at);
						live.splice(live.end(), waiting, waiting_at);
						++fetches.at(page_of(key)).users; // the page is still held by the entry just answered
						if (fetches.at(page_of(key)).list != nullptr) {
							enqueue(*again);
							queued = true;
						}
					} catch (const std::exception&) {
						answer(waiting, waiting_at, make_pdf_error(ErrorCode::OutOfMemory, "Out of memory taking a tile again"), out);
					}
				}
			}
			release_page(page_of(key));
			counting(out);
		}
		if (queued) {
			work_ready.notify_one();
		}
		hand_out(out);
		pump();
	}

	// --- Shutdown. ------------------------------------------------------------------------------------------------

	void shutdown() noexcept
	{
		Out out;
		{
			const std::lock_guard lock(mutex);
			stopping = true;
			for (auto it = live.begin(); it != live.end();) {
				const auto next = std::next(it);
				drop(it, out);
				it = next;
			}
			for (auto it = waiting.begin(); it != waiting.end();) {
				const auto next = std::next(it);
				answer(waiting, it, pdf::detail::cancelled_error(), out);
				it = next;
			}
			for (auto& [page, fetch] : fetches) {
				if (fetch.cancel.has_value()) {
					fetch.cancel->cancel();
				}
			}
			released.swap(documents); // let go below, outside the lock
			counting(out);
		}
		work_ready.notify_all();
		hand_out(out);
		pool.reset(); // joins: a tile still rendering is answered by `finished` on its way out
		// An actor's answer may be on its way out of `arrived` still: the caller's `deliver` is not touched after this.
		std::unique_lock lock(mutex);
		drained.wait(lock, [&] { return delivering == 0u; });
		fetches.clear();
		lock.unlock();
		released.clear(); // a last handle joins its actor here, with no lock held
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
	std::shared_ptr<DocumentHandle> released; // a last handle joins its actor after the lock is let go
	{
		const std::lock_guard lock(state_->mutex);
		if (const auto found = state_->documents.find(doc_id); found != state_->documents.end()) {
			released = std::move(found->second);
			state_->documents.erase(found);
		}
	}
}

void TileScheduler::update_viewport(uint64_t doc_id, std::vector<TileRequest> tiles) noexcept
{
	State& s = *state_;
	State::Out out;
	// What could not be taken is answered straight from `tiles`, by position, so refusing needs no memory either.
	std::vector<std::optional<pdf::PdfError>> refusals;
	try {
		refusals.resize(tiles.size());
	} catch (const std::bad_alloc&) {
		for (const TileRequest& request : tiles) {
			s.deliver(request.key, make_pdf_error(ErrorCode::OutOfMemory, "Out of memory taking a tile request"));
		}
		return;
	}
	bool queued = false;
	{
		const std::lock_guard lock(s.mutex);
		const uint64_t round = ++s.round;
		const bool known = s.documents.contains(doc_id);
		// First what is wanted again keeps its place, then what is no longer wanted goes, and only then are new tiles
		// taken: a full queue being replaced makes room for its replacement in the same call.
		for (size_t i = 0; i < tiles.size(); ++i) {
			const TileRequest& request = tiles[i];
			if (s.stopping || !known || request.key.doc_id != doc_id) {
				refusals[i] = s.stopping ? pdf::PdfError(pdf::detail::cancelled_error())
										 : make_pdf_error(ErrorCode::InvalidArgument, "That tile's document is not attached here");
				continue;
			}
			const auto found = s.index.find(request.key);
			if (found == s.index.end()) {
				continue;
			}
			State::Entry& entry = *found->second;
			if (entry.seen == round) {
				refusals[i] = std::nullopt; // the same key twice in one set is one request
				continue;
			}
			entry.seen = round;
			if (!entry.wanted) {
				// Its cancelled render finishes first, then it is taken again.
				if (entry.again != nullptr) {
					entry.again->request.priority = request.priority;
				} else {
					try {
						State::Entries made;
						if (auto refused = s.make(request, made)) {
							refusals[i] = *refused;
						} else {
							made.front().seen = round;
							entry.again = &made.front();
							s.waiting.splice(s.waiting.end(), made, made.begin());
						}
					} catch (const std::bad_alloc&) {
						refusals[i] = make_pdf_error(ErrorCode::OutOfMemory, "Out of memory taking a tile request");
					}
				}
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
		for (auto it = s.live.begin(); it != s.live.end();) {
			const auto next = std::next(it);
			if (it->request.key.doc_id == doc_id && it->seen != round) {
				s.drop(it, out);
			}
			it = next;
		}
		for (size_t i = 0; i < tiles.size(); ++i) {
			const TileRequest& request = tiles[i];
			if (refusals[i].has_value() || s.stopping || !known || request.key.doc_id != doc_id) {
				continue;
			}
			if (const auto found = s.index.find(request.key); found != s.index.end()) {
				continue; // kept above, or taken earlier in this call
			}
			try {
				State::Entries made;
				if (auto refused = s.make(request, made)) {
					refusals[i] = *refused;
					continue;
				}
				s.admit(made, round);
				queued = queued || s.live.back().stage == State::Stage::Queued;
			} catch (const std::bad_alloc&) {
				refusals[i] = make_pdf_error(ErrorCode::OutOfMemory, "Out of memory taking a tile request");
			}
		}
		s.counting(out);
	}
	if (queued) {
		s.work_ready.notify_all();
	}
	s.hand_out(out);
	for (size_t i = 0; i < tiles.size(); ++i) {
		if (refusals[i].has_value()) {
			s.deliver(tiles[i].key, *refusals[i]);
		}
	}
	s.pump();
}

void TileScheduler::cancel_document(uint64_t doc_id) noexcept
{
	State& s = *state_;
	State::Out out;
	{
		const std::lock_guard lock(s.mutex);
		for (auto it = s.live.begin(); it != s.live.end();) {
			const auto next = std::next(it);
			if (it->request.key.doc_id == doc_id) {
				s.drop(it, out);
			}
			it = next;
		}
		s.counting(out);
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
