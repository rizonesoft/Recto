// One thread per open document (D01 T02 section 8; foundation plan sections 4.2, 4.4).
//
// Thread-ownership contract (`frozen: true`): a `recto::pdf::Document` is made, used and destroyed on its actor's thread
// and never leaves it. Requests return at once with a future; only values and `std::shared_ptr<const DisplayList>` cross
// out. `close()` is not a queued message, so a full queue cannot refuse it.
#pragma once

#include "cancel_token.h"

#include "display_list.h"
#include "document.h"
#include "error.h"
#include "staged_revisions.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace recto::core {

inline constexpr size_t kMaxActorQueue = 4'096u;
inline constexpr size_t kMaxActorCachedPages = 32u;
inline constexpr size_t kMaxActorListBytes = size_t{1} << 30u;

// Requests answer by the state they find:
//   open                 from Unopened or Failed, otherwise InvalidArgument.
//   authenticate         from NeedsPassword or WrongPassword, otherwise InvalidArgument.
//   a query or a save    from Opened; NeedsPassword from either password state; InvalidArgument from Unopened or Failed.
//   anything once closing is Cancelled.
enum class DocumentState : uint8_t {
	Unopened = 0,
	Opening,
	NeedsPassword,
	WrongPassword,
	Opened,
	Failed,
	Closed,
};

// Actors running now, for the shutdown check (D01 T02 section 10).
[[nodiscard]] int64_t debug_live_documents() noexcept;

struct ActorOptions {
	size_t max_queue{kMaxActorQueue};
	size_t max_cached_pages{kMaxActorCachedPages};
	size_t max_list_bytes{kMaxActorListBytes};
	// Installed on the actor's thread, for a test to hold or cancel work at a named poll.
	pdf::detail::CancelPollHook poll_hook{nullptr};
	void* poll_hook_context{nullptr};
	// Called on the actor's thread just after a request has answered, before the next is taken, for a test to hold it
	// there and read what the answer left behind.
	void (*answered_hook)(void* context) noexcept {nullptr};
	void* answered_hook_context{nullptr};
};

struct ActorStats {
	uint64_t cache_hits{0};
	uint64_t cache_misses{0};
	size_t cached_pages{0};
	size_t cached_bytes{0};
};

template <typename T>
using ActorFuture = std::future<pdf::PdfResult<T>>;

class DocumentHandle {
public:
	// Starts the actor's thread; a thread that cannot start is the error.
	[[nodiscard]] static pdf::PdfResult<std::shared_ptr<DocumentHandle>> start(const ActorOptions& options = {}) noexcept;

	// Closes, then joins: the actor has been cancelled, so the join waits only on its next poll. Let go on the actor's own
	// thread (inside one of its callbacks), it cannot join itself and lets the actor finish alone instead.
	~DocumentHandle() noexcept;
	DocumentHandle(const DocumentHandle&) = delete;
	DocumentHandle& operator=(const DocumentHandle&) = delete;

	// Every request returns at once. A future that is not `valid()` means there was no memory to make it; otherwise it is
	// answered exactly once. Past the queue's limit it is a ready `ResourceLimit` (`RefusalReason::ActorQueueFull`). `cancel`
	// stops a request at its turn in the queue and in whatever polls; so does `close()`.
	[[nodiscard]] ActorFuture<void> open(std::filesystem::path path, const pdf::CancelToken& cancel = {}) noexcept;
	[[nodiscard]] ActorFuture<void> authenticate(std::string_view password) noexcept;
	[[nodiscard]] ActorFuture<pdf::DocumentDescription> get_description() noexcept;
	[[nodiscard]] ActorFuture<std::shared_ptr<const pdf::DisplayList>> get_display_list(size_t page_index, const pdf::CancelToken& cancel = {}) noexcept;
	// The same request answered through `done`, exactly once: on the actor's thread, or at once on the calling thread when
	// it is refused at admission (closing or closed, a full queue, no memory), so a caller must not hold a lock `done`
	// takes while it asks.
	using DisplayListCallback = std::move_only_function<void(pdf::PdfResult<std::shared_ptr<const pdf::DisplayList>>) noexcept>;
	void get_display_list(size_t page_index, const pdf::CancelToken& cancel, DisplayListCallback done) noexcept;
	[[nodiscard]] ActorFuture<std::vector<pdf::OutlineNode>> get_outline() noexcept;
	[[nodiscard]] ActorFuture<std::vector<pdf::PageLink>> get_page_links(size_t page_index) noexcept;
	[[nodiscard]] ActorFuture<std::vector<pdf::FontSummary>> get_fonts() noexcept;
	// The first number a change set on this document allocates from.
	[[nodiscard]] ActorFuture<uint32_t> get_first_free_object() noexcept;
	// Applies `changes` as a new revision and writes it temp-file-then-replace to `path`. The open document does not
	// change. Cancelled before the file is replaced, nothing changed; past it, the save succeeded.
	[[nodiscard]] ActorFuture<void> save_incremental(pdf::ObjectChangeSet changes, std::filesystem::path path, const pdf::CancelToken& cancel = {}) noexcept;

	// Stops the actor, at once and from any thread: pending requests answer `Cancelled`, then the document is destroyed on
	// its own thread and this future becomes ready. Every call returns the same future.
	[[nodiscard]] std::shared_future<void> close() noexcept;

	// Closes and waits for the actor's thread to end, from any thread but its own, whoever else still holds this handle.
	// Once joined, the destructor has nothing to wait for.
	void join() noexcept;

	[[nodiscard]] DocumentState state() const noexcept;
	[[nodiscard]] ActorStats stats() const noexcept;

	struct State;

private:
	explicit DocumentHandle(std::shared_ptr<State> state) noexcept;

	std::shared_ptr<State> state_;
};

namespace detail {

// What `DocumentHandle` can be made to fail at, for a test.
enum class ActorFailure : uint8_t {
	None = 0,
	ThreadStart,
	QueueInsert,
	ActorCopy,
	HandleCreate,
	CacheIndex,
};

class ScopedActorFailureForTesting {
public:
	explicit ScopedActorFailureForTesting(ActorFailure failure) noexcept;
	~ScopedActorFailureForTesting() noexcept;
	ScopedActorFailureForTesting(const ScopedActorFailureForTesting&) = delete;
	ScopedActorFailureForTesting& operator=(const ScopedActorFailureForTesting&) = delete;
};

// Passwords whose buffers have been wiped since the process began.
[[nodiscard]] uint64_t debug_wiped_passwords() noexcept;

} // namespace detail

} // namespace recto::core
