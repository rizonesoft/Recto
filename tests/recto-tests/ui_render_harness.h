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
// DPI). The target is cleared to transparent black first. Throws
// std::runtime_error naming the failed step when a COM call fails.
[[nodiscard]] Bitmap render_to_bitmap(
	const DrawRoot& root,
	uint32_t width_px,
	uint32_t height_px,
	float dpi_scale,
	const RenderOptions& options = {});

} // namespace recto::ui::test
