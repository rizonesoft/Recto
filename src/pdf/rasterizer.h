// Per-worker Direct2D replay of an immutable `DisplayList` into caller-owned tiles (D01 T02 section 6; foundation plan
// sections 4.2, 4.5). The caller owns the premultiplied BGRA buffer and its stride, which is never assumed to be
// `width * 4`.
//
// Thread-ownership contract (`frozen: true`): a `Rasterizer` is created on, used on and destroyed on one worker thread,
// and reads the `DisplayList` through `const` only, so any number of workers may replay one list at once.
//
// Tile identity: geometry is computed in double precision on the whole page's pixel grid, snapped to `kDeviceGrid`, and
// only then moved by the tile's integer origin, so a tile and the whole page draw the same fractions of a pixel and
// tiles stitch without a seam at any zoom. Images are sampled the same way, each device pixel at its place on that grid,
// and composited as a patch no larger than the tile, so no decoded image is ever uploaded whole.
#pragma once

#include "cancel_token.h"
#include "display_list.h"
#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace recto::pdf {

inline constexpr uint32_t kEngineTileEdgePx = 512u;
inline constexpr uint32_t kMaxTileEdgePx = 32'767u;
// Device coordinates are snapped to 1/256 of a pixel, exact in single precision well past any tile.
inline constexpr double kDeviceGrid = 256.0;
// A coordinate past this many pixels is clamped: it is off every tile, and Direct2D's numbers stay finite.
inline constexpr double kMaxDeviceCoordinate = 4'000'000.0;
// A dash pattern that would cut one stroke into more pieces than this draws solid, recorded.
inline constexpr uint64_t kMaxDashSegments = 100'000u;
// Nesting of Type 3 glyph sub-lists replayed (D01 T02 section 31 bounds recording at the same depth).
inline constexpr size_t kMaxType3ReplayDepth = 8u;
// Shrunken copies of decoded images this worker keeps between tiles, so a large image is not reduced again for each tile.
inline constexpr size_t kRasterImageCacheBytes = size_t{256} << 20u;

// One tile of a page. Pixel coordinates are on the whole rotated page's grid at `dpi_scale`, y down.
struct TileSpec {
	double dpi_scale{1.0}; // Pixels per point: `dpi / 72`.
	int32_t rotation_deg{0}; // The whole rotation, clockwise: the page's `/Rotate` plus the view's. 0, 90, 180 or 270.
	uint32_t tile_x_px{0};
	uint32_t tile_y_px{0};
	uint32_t tile_width_px{kEngineTileEdgePx};
	uint32_t tile_height_px{kEngineTileEdgePx};
	ColorRgba background_rgba{ColorRgba::white()};
};

// The matrix from PDF page space (y up, points) to tile pixels (y down) for `spec`, as ISO 32000-1 7.7.3.3 rotates the
// page clockwise about its box.
[[nodiscard]] PdfMatrix page_to_tile_matrix(const PdfRect& page_box_pt, const TileSpec& spec) noexcept;

struct PagePixelSize {
	uint32_t width{0};
	uint32_t height{0};

	constexpr bool operator==(const PagePixelSize&) const noexcept = default;
};

// The whole page's size in pixels under `dpi_scale` and `rotation_deg`, each edge rounded to the nearest pixel, the
// grid every tile of the page cuts.
[[nodiscard]] PagePixelSize page_pixel_size(const PdfRect& page_box_pt, double dpi_scale, int32_t rotation_deg) noexcept;

struct RasterStats {
	uint64_t draws_replayed{0}; // Draw commands and glyphs of the last tile that were not culled.
	uint64_t target_creations{0}; // Render targets this worker has ever made.
	uint64_t flushes{0}; // Batches of the last tile handed to the target, so `EndDraw` holds at most one.
	size_t image_cache_bytes{0}; // Shrunken image copies this worker holds now.
};

namespace detail {

// The cell a sample coordinate reads, clamped in floating point first: a near-singular image matrix gives coordinates no
// integer holds, and a NaN reads cell 0.
[[nodiscard]] uint32_t raster_cell_index(double coordinate, uint32_t cells) noexcept;

// Lowers `kRasterImageCacheBytes` for the calling thread's rasterizers, so a test reaches eviction with small images.
class ScopedRasterImageCacheForTesting {
public:
	explicit ScopedRasterImageCacheForTesting(size_t bytes) noexcept;
	~ScopedRasterImageCacheForTesting() noexcept;
	ScopedRasterImageCacheForTesting(const ScopedRasterImageCacheForTesting&) = delete;
	ScopedRasterImageCacheForTesting& operator=(const ScopedRasterImageCacheForTesting&) = delete;

private:
	size_t previous_;
};

} // namespace detail

class Rasterizer {
public:
	// Must be called on the worker thread that will use it.
	[[nodiscard]] static PdfResult<std::unique_ptr<Rasterizer>> create() noexcept;

	~Rasterizer() noexcept;
	Rasterizer(const Rasterizer&) = delete;
	Rasterizer& operator=(const Rasterizer&) = delete;
	Rasterizer(Rasterizer&&) = delete;
	Rasterizer& operator=(Rasterizer&&) = delete;

	// Replays `list` into `output`, premultiplied BGRA with `stride` bytes per row; bytes past `width * 4` in a row are
	// left alone. `InvalidArgument` for a spec or buffer that cannot be drawn, with `output` untouched; a Direct2D or WIC
	// failure is a typed error, also with `output` untouched, and the next tile starts from a new target. A cancelled tile
	// returns `Cancelled` with `output` untouched, its clips unwound and the target ready for the next tile.
	[[nodiscard]] PdfResult<void> render_tile(const std::shared_ptr<const DisplayList>& list, const TileSpec& spec,
		std::span<uint8_t> output, size_t stride, const CancelToken& cancel = {}) noexcept;

	[[nodiscard]] const RasterStats& stats() const noexcept;

private:
	struct State;
	explicit Rasterizer(std::unique_ptr<State> state) noexcept;

	std::unique_ptr<State> state_;
};

} // namespace recto::pdf
