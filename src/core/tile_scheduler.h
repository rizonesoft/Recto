// Which tiles render first, and the ones that stop when the view moves on (D01 T02 section 9; foundation plan sections
// 4.4, 4.5).
//
// The viewer lays tiles out and says which it wants, each with a priority; the scheduler fetches each page's list from
// its `DocumentActor` once, queues the tiles by priority (first come first within one), hands them to its
// `RenderPool`, and cancels every tile, and every fetch, a new request set no longer holds.
#pragma once

#include "document_actor.h"
#include "render_pool.h"

#include "error.h"
#include "rasterizer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace recto::core {

inline constexpr size_t kMaxScheduledTiles = 4'096u;

enum class TilePriority : uint8_t {
	VisibleCurrentPage = 1,
	VisibleAdjacentPage = 2,
	PrefetchRing = 3,
	Thumbnail = 4,
};

// One tile of one page of one document at one zoom bucket and rotation.
struct TileKey {
	uint64_t doc_id{0};
	uint32_t page_index{0};
	uint32_t zoom_bucket_milli{0};
	int32_t rotation_deg{0};
	uint32_t tile_x{0};
	uint32_t tile_y{0};

	constexpr bool operator==(const TileKey&) const noexcept = default;
};

struct TileKeyHash {
	[[nodiscard]] size_t operator()(const TileKey& key) const noexcept;
};

struct TileRequest {
	TileKey key{};
	TilePriority priority{TilePriority::VisibleCurrentPage};
	pdf::TileSpec spec{}; // the same key always comes with the same spec
};

// Called exactly once for each request taken, never under the scheduler's lock, on a worker or an actor thread, so it
// must be safe to call from any thread.
using TileDelivery = std::move_only_function<void(const TileKey&, pdf::PdfResult<std::shared_ptr<const TileImage>>) noexcept>;

struct SchedulerOptions {
	uint32_t worker_count{0}; // 0: `RenderPool::default_worker_count()`; otherwise 1 to 16
	TileDelivery deliver;     // required
	// Installed on each worker, for a test to see a tile start or hold it at a named poll.
	pdf::detail::CancelPollHook poll_hook{nullptr};
	void* poll_hook_context{nullptr};
};

class TileScheduler {
public:
	[[nodiscard]] static pdf::PdfResult<std::unique_ptr<TileScheduler>> create(SchedulerOptions options) noexcept;

	// Delivers `Cancelled` for everything still held, cancels what is rendering, and joins the workers.
	~TileScheduler() noexcept;
	TileScheduler(const TileScheduler&) = delete;
	TileScheduler& operator=(const TileScheduler&) = delete;

	// The document `doc_id` names in requests. Attaching an id twice is `InvalidArgument`.
	[[nodiscard]] pdf::PdfResult<void> attach(uint64_t doc_id, std::shared_ptr<DocumentHandle> document) noexcept;
	// Cancels the document's tiles and forgets it.
	void detach(uint64_t doc_id) noexcept;

	// The tiles `doc_id` wants now, replacing what it wanted before: a tile no longer wanted is cancelled, queued or
	// rendering; one wanted again keeps its job and takes its new priority.
	void update_viewport(uint64_t doc_id, std::vector<TileRequest> tiles) noexcept;
	void cancel_document(uint64_t doc_id) noexcept;

	// Hold the workers between tiles, and let them go.
	void pause() noexcept;
	void resume() noexcept;

	[[nodiscard]] uint32_t worker_count() const noexcept;

	struct State;

private:
	explicit TileScheduler(std::shared_ptr<State> state) noexcept;
	std::shared_ptr<State> state_;
};

} // namespace recto::core

template <>
struct std::hash<recto::core::TileKey> {
	[[nodiscard]] size_t operator()(const recto::core::TileKey& key) const noexcept
	{
		return recto::core::TileKeyHash{}(key);
	}
};
