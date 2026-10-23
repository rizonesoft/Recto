// Headless render harness for recto-ui (D10 T01 section 1).
//
// render_to_bitmap draws into a WIC BGRA bitmap through a Direct2D WIC render
// target, so layout, tokens and pixels are asserted in recto-tests without a
// display session or a D3D11 device. The harness lives in tests/recto-tests and
// links the static library; recto-ui itself carries no test code.
//
// `root` is a draw callback until the Element tree exists (D10 T01 section 6
// adds the Element overload; section 5 moves the factory behind
// GraphicsDevice::create_headless). The context hands it the render target with
// the DPI already set, so the callback draws in effective pixels (epx) and the
// harness scales to device pixels exactly as a window at that scale would.
#pragma once

#include <d2d1_3.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace recto::ui::test {

// A rendered image: premultiplied BGRA, 8 bits per channel, rows `stride` bytes
// apart. `stride` is never assumed to be `width * 4`; bytes past `width * 4` in
// a row are padding and carry no pixel data.
struct Bitmap {
	uint32_t width{0};
	uint32_t height{0};
	size_t stride{0};
	std::vector<uint8_t> bytes;
};

struct RenderContext {
	ID2D1RenderTarget* target{nullptr}; // DPI set to 96 * dpi_scale; one DIP is one epx
	float dpi_scale{1.0F};
	float width_epx{0.0F};
	float height_epx{0.0F};
};

using DrawRoot = std::function<void(const RenderContext&)>;

struct RenderOptions {
	// The returned stride is at least this many bytes. A value wider than the
	// natural stride makes the harness hand back padded rows, so a consumer that
	// assumes width * 4 is caught.
	size_t min_stride{0};
};

// Renders `root` into a width_px x height_px bitmap at `dpi_scale` (1.0 is 96
// DPI). The target is cleared to transparent black first.
//
// Refusals and failures throw std::runtime_error, and a refused request never
// calls `root`: a zero or larger-than-16384 side, a scale that is not finite or
// outside 0.25 to 16, or a min_stride wider than 16 MiB. A failed COM call
// names its step and HRESULT; a failed EndDraw also names the two tags the
// failing drawing call carried (ID2D1RenderTarget::SetTags) and the request.
// An exception thrown by `root` reaches the caller unchanged.
//
// COM: the calling thread keeps its apartment. A thread with none gets a
// multithreaded one for the call only; an STA or MTA caller is left as it was.
[[nodiscard]] Bitmap render_to_bitmap(
	const DrawRoot& root,
	uint32_t width_px,
	uint32_t height_px,
	float dpi_scale,
	const RenderOptions& options = {});

// The bitmap as packed RGBA rows (width * 4 bytes, no padding), still
// premultiplied, for the recto::test comparison helpers (compare_rgba,
// expect_rgba_equal, compute_ssim), which read red first. The harness itself
// returns BGRA, so pass this, never Bitmap::bytes, to an RGBA helper.
// Throws std::runtime_error, reading nothing, when the stride is narrower
// than a row or wider than 16 MiB, or the buffer is shorter than the rows it
// claims; an empty bitmap gives an empty vector.
[[nodiscard]] std::vector<uint8_t> rgba_bytes(const Bitmap& bitmap);

} // namespace recto::ui::test
