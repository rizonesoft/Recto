#include "ui_render_harness.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d2d1_3.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
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

Bitmap render_to_bitmap(
	const DrawRoot& root,
	uint32_t width_px,
	uint32_t height_px,
	float dpi_scale,
	const RenderOptions& options)
{
	if (width_px == 0 || height_px == 0) {
		throw std::runtime_error("render_to_bitmap: width_px and height_px must be non-zero");
	}
	if (!(dpi_scale > 0.0F)) {
		throw std::runtime_error("render_to_bitmap: dpi_scale must be positive");
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
		root(context);
	}
	check(target->EndDraw(), "ID2D1RenderTarget::EndDraw");

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

} // namespace recto::ui::test
