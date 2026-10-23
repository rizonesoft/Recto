#include "ui_render_harness.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d2d1_3.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace recto::ui::test {

namespace {

using Microsoft::WRL::ComPtr;

// Bytes written into row padding so a consumer that reads past width * 4 sees
// a value no real pixel path produces by accident.
constexpr uint8_t kPaddingSentinel = 0xCD;

// Widest row the harness will pad to (16 MiB), far beyond any test bitmap.
constexpr size_t kMaxStride = size_t{1} << 24;

// Largest bitmap side the harness renders (Direct2D's own software limit is
// far above any test image) and the scale range Windows can apply (25 to 1600
// percent), so a request outside them is refused before anything is drawn.
constexpr uint32_t kMaxDimension = 16384;
constexpr float kMinScale = 0.25F;
constexpr float kMaxScale = 16.0F;

void check(HRESULT hr, const char* step)
{
	if (FAILED(hr)) {
		char code[16]{};
		std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(hr));
		throw std::runtime_error(std::string(step) + " failed, HRESULT 0x" + code);
	}
}

// COM for the calling thread, balanced on scope exit. A thread already
// initialised in the other apartment model keeps its model.
class ComScope {
public:
	ComScope()
	{
		const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (hr == RPC_E_CHANGED_MODE) {
			return;
		}
		check(hr, "CoInitializeEx");
		owned_ = true;
	}
	~ComScope()
	{
		if (owned_) {
			CoUninitialize();
		}
	}
	ComScope(const ComScope&) = delete;
	ComScope& operator=(const ComScope&) = delete;

private:
	bool owned_{false};
};

} // namespace

namespace {

std::string describe_request(uint32_t width_px, uint32_t height_px, float dpi_scale, const RenderOptions& options)
{
	char text[160]{};
	std::snprintf(text, sizeof(text), "%u x %u px at scale %g, min_stride %zu", width_px, height_px,
		static_cast<double>(dpi_scale), options.min_stride);
	return text;
}

[[noreturn]] void refuse(const char* why, uint32_t width_px, uint32_t height_px, float dpi_scale, const RenderOptions& options)
{
	throw std::runtime_error(std::string("render_to_bitmap: ") + why + " (" + describe_request(width_px, height_px, dpi_scale, options) + ")");
}

} // namespace

Bitmap render_to_bitmap(
	const DrawRoot& root,
	uint32_t width_px,
	uint32_t height_px,
	float dpi_scale,
	const RenderOptions& options)
{
	// Every refusal happens here, before COM, any allocation or the callback.
	if (width_px == 0 || height_px == 0) {
		refuse("width_px and height_px must be non-zero", width_px, height_px, dpi_scale, options);
	}
	if (width_px > kMaxDimension || height_px > kMaxDimension) {
		refuse("width_px and height_px must be at most 16384", width_px, height_px, dpi_scale, options);
	}
	if (!std::isfinite(dpi_scale) || !(dpi_scale >= kMinScale && dpi_scale <= kMaxScale)) {
		refuse("dpi_scale must be finite and between 0.25 and 16", width_px, height_px, dpi_scale, options);
	}
	// A stride is a row length, never a size in the gigabytes: refuse one that
	// cannot be a row of this bitmap rather than let stride * height overflow.
	if (options.min_stride > kMaxStride) {
		refuse("min_stride is larger than any bitmap row", width_px, height_px, dpi_scale, options);
	}

	const ComScope com;

	ComPtr<IWICImagingFactory2> wic;
	check(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)),
		"CoCreateInstance(CLSID_WICImagingFactory2)");

	ComPtr<IWICBitmap> wic_bitmap;
	check(wic->CreateBitmap(width_px, height_px, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wic_bitmap),
		"IWICImagingFactory2::CreateBitmap");

	ComPtr<ID2D1Factory7> factory;
	check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory7), nullptr,
			  reinterpret_cast<void**>(factory.GetAddressOf())),
		"D2D1CreateFactory");

	// Software rendering keeps the output deterministic across machines and
	// needs no display session or GPU.
	const float dpi = 96.0F * dpi_scale;
	const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_SOFTWARE,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		dpi,
		dpi);
	ComPtr<ID2D1RenderTarget> target;
	check(factory->CreateWicBitmapRenderTarget(wic_bitmap.Get(), properties, &target),
		"ID2D1Factory::CreateWicBitmapRenderTarget");

	RenderContext context;
	context.target = target.Get();
	context.dpi_scale = dpi_scale;
	context.width_epx = static_cast<float>(width_px) / dpi_scale;
	context.height_epx = static_cast<float>(height_px) / dpi_scale;

	target->BeginDraw();
	target->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));
	if (root) {
		try {
			root(context);
		} catch (...) {
			// Close the draw before the target is released, then hand the
			// callback's own exception to the caller unchanged.
			(void)target->EndDraw();
			throw;
		}
	}
	D2D1_TAG tag1 = 0;
	D2D1_TAG tag2 = 0;
	const HRESULT drawn = target->EndDraw(&tag1, &tag2);
	if (FAILED(drawn)) {
		// The tags name the drawing call that failed (ID2D1RenderTarget::SetTags).
		char detail[96]{};
		std::snprintf(detail, sizeof(detail), ", tags %llu, %llu", static_cast<unsigned long long>(tag1),
			static_cast<unsigned long long>(tag2));
		char code[16]{};
		std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(drawn));
		throw std::runtime_error(std::string("ID2D1RenderTarget::EndDraw failed, HRESULT 0x") + code + detail + " (" +
			describe_request(width_px, height_px, dpi_scale, options) + ")");
	}

	const WICRect rect{0, 0, static_cast<INT>(width_px), static_cast<INT>(height_px)};
	ComPtr<IWICBitmapLock> lock;
	check(wic_bitmap->Lock(&rect, WICBitmapLockRead, &lock), "IWICBitmap::Lock");

	UINT wic_stride = 0;
	check(lock->GetStride(&wic_stride), "IWICBitmapLock::GetStride");
	UINT data_size = 0;
	BYTE* data = nullptr;
	check(lock->GetDataPointer(&data_size, &data), "IWICBitmapLock::GetDataPointer");

	const size_t row_bytes = static_cast<size_t>(width_px) * 4;
	if (wic_stride < row_bytes) {
		throw std::runtime_error("render_to_bitmap: WIC stride is narrower than a pixel row");
	}
	if (static_cast<size_t>(data_size) < static_cast<size_t>(wic_stride) * (height_px - 1) + row_bytes) {
		throw std::runtime_error("render_to_bitmap: WIC buffer is smaller than the bitmap");
	}

	Bitmap out;
	out.width = width_px;
	out.height = height_px;
	out.stride = std::max<size_t>(wic_stride, options.min_stride);
	out.bytes.assign(out.stride * height_px, kPaddingSentinel);
	for (uint32_t y = 0; y < height_px; ++y) {
		std::memcpy(out.bytes.data() + static_cast<size_t>(y) * out.stride,
			data + static_cast<size_t>(y) * wic_stride,
			row_bytes);
	}
	return out;
}

std::vector<uint8_t> rgba_bytes(const Bitmap& bitmap)
{
	// A Bitmap is a plain struct a caller can change, so its shape is checked
	// before any pixel is read: a row must fit in the stride, the buffer must
	// hold every row, and no product may overflow.
	if (bitmap.width == 0 || bitmap.height == 0) {
		return {};
	}
	const size_t row_bytes = static_cast<size_t>(bitmap.width) * 4;
	if (bitmap.stride < row_bytes || bitmap.stride > kMaxStride) {
		throw std::runtime_error("rgba_bytes: the stride does not fit a row of this bitmap");
	}
	if (bitmap.bytes.size() < bitmap.stride * (static_cast<size_t>(bitmap.height) - 1) + row_bytes) {
		throw std::runtime_error("rgba_bytes: the buffer is smaller than the bitmap");
	}
	std::vector<uint8_t> rgba(row_bytes * bitmap.height);
	for (uint32_t y = 0; y < bitmap.height; ++y) {
		const uint8_t* row = bitmap.bytes.data() + static_cast<size_t>(y) * bitmap.stride;
		uint8_t* out = rgba.data() + static_cast<size_t>(y) * bitmap.width * 4;
		for (uint32_t x = 0; x < bitmap.width; ++x) {
			out[x * 4 + 0] = row[x * 4 + 2];
			out[x * 4 + 1] = row[x * 4 + 1];
			out[x * 4 + 2] = row[x * 4 + 0];
			out[x * 4 + 3] = row[x * 4 + 3];
		}
	}
	return rgba;
}

} // namespace recto::ui::test
