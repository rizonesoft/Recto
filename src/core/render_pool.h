// Worker threads that rasterize tiles, each with a `pdf::Rasterizer` of its own (D01 T02 section 9; foundation plan
// sections 4.3, 4.5).
//
// Thread-ownership contract (`frozen: true`): a worker never touches a `pdf::Document`. It renders only the
// `std::shared_ptr<const DisplayList>` a job carries, with the `Rasterizer` it made on its own thread and destroys there.
#pragma once

#include "cancel_token.h"

#include "display_list.h"
#include "error.h"
#include "rasterizer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace recto::core {

inline constexpr uint32_t kMinRenderWorkers = 1u;
inline constexpr uint32_t kMaxRenderWorkers = 16u;

// Worker threads running now, for the shutdown check (D01 T02 section 10).
[[nodiscard]] int64_t debug_live_workers() noexcept;

// One rendered tile: premultiplied BGRA, `stride` bytes per row.
struct TileImage {
	uint32_t width{0};
	uint32_t height{0};
	size_t stride{0};
	std::vector<uint8_t> pixels;
};

// What a worker renders: the list, where on the page, and the token that stops it.
struct TileJob {
	uint64_t ticket{0}; // the source's own name for the job, handed back with its result
	std::shared_ptr<const pdf::DisplayList> list;
	pdf::TileSpec spec{};
	pdf::CancelToken cancel;
};

// Where workers take their jobs from and hand their results to.
class TileJobSource {
public:
	virtual ~TileJobSource() = default;
	// Waits for the next job; none means the pool is stopping.
	[[nodiscard]] virtual std::optional<TileJob> next_job() noexcept = 0;
	// Called once for every job taken, on the worker that took it.
	virtual void finished(const TileJob& job, pdf::PdfResult<std::shared_ptr<const TileImage>> result) noexcept = 0;
};

class RenderPool {
public:
	// `clamp(max(cores, 2) - 2, 2, 8)`, so the subtraction never wraps; `hardware_concurrency()` for the machine.
	[[nodiscard]] static uint32_t default_worker_count(unsigned cores) noexcept;
	[[nodiscard]] static uint32_t default_worker_count() noexcept;

	// Starts `workers` threads (`kMinRenderWorkers..kMaxRenderWorkers`, else `InvalidArgument`) on `source`, which must
	// outlive the pool and must stop answering `next_job` before the pool is destroyed. It returns once every worker has
	// made its `Rasterizer`; a thread that cannot start or a rasterizer that cannot be made stops and joins the others
	// and is the error, and no worker has asked `source` for a job. `poll_hook` is installed on each worker, for a test.
	[[nodiscard]] static pdf::PdfResult<std::unique_ptr<RenderPool>> start(TileJobSource& source, uint32_t workers,
		pdf::detail::CancelPollHook poll_hook = nullptr, void* poll_hook_context = nullptr) noexcept;

	// Joins every worker; each returns once `next_job` answers none.
	~RenderPool() noexcept;
	RenderPool(const RenderPool&) = delete;
	RenderPool& operator=(const RenderPool&) = delete;

	[[nodiscard]] uint32_t size() const noexcept;

	struct State;

private:
	explicit RenderPool(std::unique_ptr<State> state) noexcept;
	std::unique_ptr<State> state_;
};

namespace detail {

// Makes the `nth` worker thread started from now on fail to start, for a test: as the system refusing a thread, or as
// `std::thread` failing to allocate its callable.
void fail_worker_start_for_testing(uint32_t nth, bool out_of_memory = false) noexcept;
// Makes the next worker's `Rasterizer` fail to be made, for a test.
void fail_next_worker_rasterizer_for_testing() noexcept;

} // namespace detail

} // namespace recto::core
