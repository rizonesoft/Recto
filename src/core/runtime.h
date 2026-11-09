// The one object that owns every engine thread and stops them in order (D01 T02 section 10; foundation plan sections
// 4.3, 4.8).
//
// Shutdown order (`frozen: true`): 1 cancel every tile, request and job; 2 close every document and join its actor;
// 3 join every job; 4 drain and join the render workers; 5 run the presentation hooks; 6 read the live counters, which a
// Debug build asserts are zero when this was the only `Runtime`.
#pragma once

#include "document_actor.h"
#include "render_pool.h"
#include "tile_scheduler.h"

#include "cancel_token.h"
#include "error.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace recto::core {

using DocumentId = uint64_t;

struct OpenedDocument {
	DocumentId id{0};
	ActorFuture<void> opened; // `NeedsPassword`, the typed failure, or `Cancelled` when it is closed first
};

// What a job registers so shutdown can stop it: `cancel` in step 1, `join` in step 3. Both are called once, on the
// thread that shuts down, and must not call back into the `Runtime`.
struct JobHooks {
	std::move_only_function<void() noexcept> cancel;
	std::move_only_function<void() noexcept> join;
};

struct ShutdownReport {
	int64_t live_documents{0};
	int64_t live_display_lists{0};
	int64_t live_workers{0};

	constexpr bool operator==(const ShutdownReport&) const noexcept = default;
};

struct RuntimeOptions {
	uint32_t worker_count{0}; // 0: `RenderPool::default_worker_count()`
	TileDelivery deliver;     // required: every tile requested through this runtime is answered here, exactly once
	ActorOptions actor_options{};
	// For tests: each step's number as it starts, and a poll hook on every render worker.
	std::move_only_function<void(int step) noexcept> on_step;
	pdf::detail::CancelPollHook worker_poll_hook{nullptr};
	void* worker_poll_hook_context{nullptr};
};

class Runtime {
public:
	[[nodiscard]] static pdf::PdfResult<std::unique_ptr<Runtime>> create(RuntimeOptions options) noexcept;

	// `shutdown()`, if it has not run.
	~Runtime() noexcept;
	Runtime(const Runtime&) = delete;
	Runtime& operator=(const Runtime&) = delete;

	// Starts an actor for `path` and returns at once: the id works straight away, `opened` answers the open. A document
	// whose actor cannot start, or that cannot be recorded, is the error, with nothing left behind.
	[[nodiscard]] pdf::PdfResult<OpenedDocument> open_document(std::filesystem::path path) noexcept;
	// The document's handle, for its requests; null for an id that is unknown or closed.
	[[nodiscard]] std::shared_ptr<DocumentHandle> document(DocumentId id) const noexcept;
	// Cancels its tiles, closes it and joins its actor. `InvalidArgument` for an unknown or closed id.
	[[nodiscard]] pdf::PdfResult<void> close_document(DocumentId id) noexcept;

	void update_viewport(DocumentId id, std::vector<TileRequest> tiles) noexcept;
	void cancel_document(DocumentId id) noexcept;

	// `Cancelled` once shut down, so a caller knows nothing will stop its thread for it.
	[[nodiscard]] pdf::PdfResult<uint64_t> register_job(JobHooks hooks) noexcept;
	void unregister_job(uint64_t id) noexcept;
	[[nodiscard]] pdf::PdfResult<uint64_t> register_presentation_hook(std::move_only_function<void() noexcept> hook) noexcept;
	void unregister_presentation_hook(uint64_t id) noexcept;

	// Runs the six steps once, on the thread that owns this `Runtime`, never from inside a delivery, hook or job; a later
	// call returns the first report.
	ShutdownReport shutdown() noexcept;

	struct State;

private:
	explicit Runtime(std::unique_ptr<State> state) noexcept;
	std::unique_ptr<State> state_;
};

// `Runtime`s alive now, so step 6 asserts only when it is the last.
[[nodiscard]] int64_t debug_live_runtimes() noexcept;

} // namespace recto::core
