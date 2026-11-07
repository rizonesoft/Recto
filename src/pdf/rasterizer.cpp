// Per-worker Direct2D replay of a DisplayList (D01 T02 section 6; rasterizer.h has the contract).
#include "rasterizer.h"

#include "font.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <new>
#include <numbers>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace recto::pdf {

namespace {

using Microsoft::WRL::ComPtr;

thread_local size_t g_image_cache_bytes = kRasterImageCacheBytes;

[[nodiscard]] PdfError raster_failure(HRESULT hr, const char* what) noexcept
{
	if (hr == E_OUTOFMEMORY) {
		return make_pdf_error(ErrorCode::OutOfMemory, what);
	}
	return make_pdf_error(ErrorCode::InternalError, what);
}

[[nodiscard]] double snapped(double value) noexcept
{
	return std::round(std::clamp(value, -kMaxDeviceCoordinate, kMaxDeviceCoordinate) * kDeviceGrid) / kDeviceGrid;
}

[[nodiscard]] PdfMatrix translate(double x, double y) noexcept
{
	return PdfMatrix{1.0, 0.0, 0.0, 1.0, x, y};
}

// Whole-page pixels: `page_to_tile_matrix` before the tile's origin.
[[nodiscard]] PdfMatrix page_to_full(const PdfRect& box, double s, int32_t rotation) noexcept
{
	switch (rotation) {
	case 90:
		return PdfMatrix{0.0, s, s, 0.0, -box.y_min * s, -box.x_min * s};
	case 180:
		return PdfMatrix{-s, 0.0, 0.0, s, box.x_max * s, -box.y_min * s};
	case 270:
		return PdfMatrix{0.0, -s, -s, 0.0, box.y_max * s, box.x_max * s};
	default:
		return PdfMatrix{s, 0.0, 0.0, -s, -box.x_min * s, box.y_max * s};
	}
}

[[nodiscard]] bool axis_aligned(const PdfMatrix& m) noexcept
{
	return (m.b == 0.0 && m.c == 0.0) || (m.a == 0.0 && m.d == 0.0);
}

[[nodiscard]] double largest_scale(const PdfMatrix& m) noexcept
{
	return std::sqrt(std::max(m.a * m.a + m.b * m.b, m.c * m.c + m.d * m.d));
}

// How many straight pieces hold a cubic within 1/32 of a pixel (Wang's formula: `sqrt(d(d-1)/8 * M / tolerance)` for
// degree d and M the largest second difference), `px_scale` taking its points to pixels.
[[nodiscard]] uint32_t cubic_pieces(PdfPoint p0, PdfPoint p1, PdfPoint p2, PdfPoint p3, double px_scale) noexcept
{
	constexpr double kTolerancePx = 1.0 / 32.0;
	constexpr double kMaxPieces = 1024.0;
	const double a = std::hypot(p0.x - 2.0 * p1.x + p2.x, p0.y - 2.0 * p1.y + p2.y);
	const double b = std::hypot(p1.x - 2.0 * p2.x + p3.x, p1.y - 2.0 * p2.y + p3.y);
	const double pieces = std::ceil(std::sqrt(0.75 * std::max(a, b) * px_scale / kTolerancePx));
	return std::isfinite(pieces) ? static_cast<uint32_t>(std::clamp(pieces, 1.0, kMaxPieces)) : 1u;
}

[[nodiscard]] D2D1_COLOR_F d2d_color(const ColorRgba& color, float alpha) noexcept
{
	return D2D1_COLOR_F{color.r, color.g, color.b, color.a * alpha};
}

// Receives Direct2D's widened outline and re-emits it through `place`, so its coordinates land on the page's grid. Lives
// on the stack for one `Widen`, so its reference count does nothing.
template <typename Place>
class WidenedOutlineSink final : public ID2D1SimplifiedGeometrySink {
public:
	WidenedOutlineSink(ID2D1GeometrySink* out, double px_scale, Place place) noexcept
		: out_(out), px_scale_(px_scale), place_(std::move(place))
	{
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) noexcept override
	{
		if (object == nullptr) {
			return E_POINTER;
		}
		if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1SimplifiedGeometrySink)) {
			*object = static_cast<ID2D1SimplifiedGeometrySink*>(this);
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() noexcept override { return 1u; }
	ULONG STDMETHODCALLTYPE Release() noexcept override { return 1u; }

	void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) noexcept override {} // an outline's overlaps are all inside it
	void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) noexcept override {}
	void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) noexcept override
	{
		current_ = start;
		out_->BeginFigure(place_(start), D2D1_FIGURE_BEGIN_FILLED);
	}
	void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) noexcept override
	{
		for (UINT32 i = 0; i < count; ++i) {
			out_->AddLine(place_(points[i]));
			current_ = points[i];
		}
	}
	void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 count) noexcept override
	{
		const auto point = [](D2D1_POINT_2F p) { return PdfPoint{static_cast<double>(p.x), static_cast<double>(p.y)}; };
		for (UINT32 i = 0; i < count; ++i) {
			const PdfPoint p0 = point(current_);
			const PdfPoint p1 = point(beziers[i].point1);
			const PdfPoint p2 = point(beziers[i].point2);
			const PdfPoint p3 = point(beziers[i].point3);
			const uint32_t pieces = cubic_pieces(p0, p1, p2, p3, px_scale_);
			for (uint32_t k = 1u; k < pieces; ++k) {
				const double t = static_cast<double>(k) / static_cast<double>(pieces);
				const double u = 1.0 - t;
				const double b0 = u * u * u;
				const double b1 = 3.0 * u * u * t;
				const double b2 = 3.0 * u * t * t;
				const double b3 = t * t * t;
				out_->AddLine(place_(D2D1_POINT_2F{static_cast<float>(b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x),
					static_cast<float>(b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y)}));
			}
			out_->AddLine(place_(beziers[i].point3));
			current_ = beziers[i].point3;
		}
	}
	void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END end) noexcept override { out_->EndFigure(end); }
	HRESULT STDMETHODCALLTYPE Close() noexcept override { return S_OK; }

private:
	ID2D1GeometrySink* out_;
	double px_scale_;
	Place place_;
	D2D1_POINT_2F current_{};
};

struct ClipEntry {
	enum class Kind : uint8_t { None, Rect, Layer };
	Kind kind{Kind::None};
};

// The graphics state replay keeps, saved and restored with the list.
struct ReplayState {
	PdfMatrix ctm{PdfMatrix::identity()};
	SetStrokeStyleCommand stroke{};
	float stroke_alpha{1.0f};
	float fill_alpha{1.0f};
	size_t clip_count{0u};
};

struct GlyphKey {
	const LoadedFont* font{nullptr};
	uint16_t glyph{0};

	bool operator<(const GlyphKey& other) const noexcept
	{
		return font != other.font ? font < other.font : glyph < other.glyph;
	}
};

} // namespace

namespace detail {

ScopedRasterImageCacheForTesting::ScopedRasterImageCacheForTesting(size_t bytes) noexcept
	: previous_(g_image_cache_bytes)
{
	g_image_cache_bytes = bytes;
}

ScopedRasterImageCacheForTesting::~ScopedRasterImageCacheForTesting() noexcept
{
	g_image_cache_bytes = previous_;
}

} // namespace detail

PdfMatrix page_to_tile_matrix(const PdfRect& page_box_pt, const TileSpec& spec) noexcept
{
	PdfMatrix m = page_to_full(page_box_pt, spec.dpi_scale, spec.rotation_deg);
	m.e -= static_cast<double>(spec.tile_x_px);
	m.f -= static_cast<double>(spec.tile_y_px);
	return m;
}

PagePixelSize page_pixel_size(const PdfRect& page_box_pt, double dpi_scale, int32_t rotation_deg) noexcept
{
	const double w = std::round((page_box_pt.x_max - page_box_pt.x_min) * dpi_scale);
	const double h = std::round((page_box_pt.y_max - page_box_pt.y_min) * dpi_scale);
	const bool swapped = rotation_deg == 90 || rotation_deg == 270;
	const double out_w = swapped ? h : w;
	const double out_h = swapped ? w : h;
	const auto edge = [](double v) {
		return !std::isfinite(v) || v <= 0.0 ? 0u : static_cast<uint32_t>(std::min(v, 4'294'967'295.0));
	};
	return PagePixelSize{edge(out_w), edge(out_h)};
}

struct Rasterizer::State {
	std::thread::id owner{std::this_thread::get_id()};
	bool com_owned{false};
	ComPtr<IWICImagingFactory> wic;
	ComPtr<ID2D1Factory1> d2d;
	ComPtr<IDWriteFactory> dwrite;
	ComPtr<IDWriteRenderingParams> text_params;

	ComPtr<IWICBitmap> target_bitmap;
	ComPtr<ID2D1RenderTarget> target;
	uint32_t target_width{0};
	uint32_t target_height{0};
	std::vector<ComPtr<ID2D1Layer>> layers;
	std::vector<ComPtr<ID2D1BitmapRenderTarget>> clip_masks; // one at each depth of arbitrary clips, the tile's size
	size_t layers_in_use{0u};

	struct ImageKey {
		const DecodedBitmap* source{nullptr};
		uint32_t reduce_x{1u};
		uint32_t reduce_y{1u};

		bool operator<(const ImageKey& other) const noexcept
		{
			if (source != other.source) {
				return source < other.source;
			}
			return reduce_x != other.reduce_x ? reduce_x < other.reduce_x : reduce_y < other.reduce_y;
		}
	};
	struct CachedImage {
		ImageKey key;
		std::shared_ptr<const DecodedBitmap> source; // keeps the key's address from being reused
		std::shared_ptr<const std::vector<uint8_t>> pixels; // premultiplied BGRA, the reduced width's rows with no padding
	};
	std::list<CachedImage> images; // most recently used first
	std::map<ImageKey, std::list<CachedImage>::iterator> image_index;
	size_t image_bytes{0u};

	RasterStats stats;

	// One tile's replay.
	double tile_x{0.0};
	double tile_y{0.0};
	double tile_w{0.0};
	double tile_h{0.0};
	std::vector<ClipEntry> clips;
	std::map<GlyphKey, bool> glyph_checks;
	std::map<GlyphKey, std::optional<GlyphOutline>> glyph_outlines;
	HRESULT failure{S_OK};

	~State() noexcept
	{
		image_index.clear();
		images.clear();
		clip_masks.clear();
		layers.clear();
		target.Reset();
		target_bitmap.Reset();
		text_params.Reset();
		dwrite.Reset();
		d2d.Reset();
		wic.Reset();
		if (com_owned && owner == std::this_thread::get_id()) {
			CoUninitialize();
		}
	}

	void drop_target() noexcept
	{
		clip_masks.clear();
		layers.clear();
		layers_in_use = 0u;
		target.Reset();
		target_bitmap.Reset();
		target_width = 0u;
		target_height = 0u;
	}

	[[nodiscard]] HRESULT ensure_target(uint32_t width, uint32_t height) noexcept
	{
		if (target != nullptr && target_width == width && target_height == height) {
			return S_OK;
		}
		drop_target();
		HRESULT hr = wic->CreateBitmap(
			width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, target_bitmap.ReleaseAndGetAddressOf());
		if (FAILED(hr)) {
			return hr;
		}
		// Software, at 96 DPI so a DIP is a pixel: the same pixels on every graphics card.
		const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
		hr = d2d->CreateWicBitmapRenderTarget(target_bitmap.Get(), properties, target.ReleaseAndGetAddressOf());
		if (FAILED(hr)) {
			target_bitmap.Reset();
			return hr;
		}
		target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
		target->SetTextRenderingParams(text_params.Get());
		target_width = width;
		target_height = height;
		++stats.target_creations;
		return S_OK;
	}

	void fail(HRESULT hr) noexcept
	{
		if (SUCCEEDED(failure)) {
			failure = hr;
		}
	}

	// A point on the whole page's grid, moved to this tile; none when it is not a number.
	[[nodiscard]] std::optional<D2D1_POINT_2F> device_point(const PdfMatrix& to_full, PdfPoint p) const noexcept
	{
		const PdfPoint full = transform_point(to_full, p);
		if (!std::isfinite(full.x) || !std::isfinite(full.y)) {
			return std::nullopt;
		}
		return D2D1_POINT_2F{static_cast<float>(snapped(full.x) - tile_x), static_cast<float>(snapped(full.y) - tile_y)};
	}

	// A path through `map`, which gives each point's coordinates (or none, which makes the path unusable).
	template <typename Map>
	[[nodiscard]] ComPtr<ID2D1PathGeometry> build_geometry(std::span<const PathVerb> verbs, std::span<const PdfPoint> points,
		FillRule rule, double px_scale, const Map& map)
	{
		ComPtr<ID2D1PathGeometry> geometry;
		HRESULT hr = d2d->CreatePathGeometry(geometry.GetAddressOf());
		if (FAILED(hr)) {
			fail(hr);
			return nullptr;
		}
		ComPtr<ID2D1GeometrySink> sink;
		hr = geometry->Open(sink.GetAddressOf());
		if (FAILED(hr)) {
			fail(hr);
			return nullptr;
		}
		sink->SetFillMode(rule == FillRule::EvenOdd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
		bool open = false;
		PdfPoint start{};
		PdfPoint current{};
		size_t at = 0u;
		const auto begin = [&](PdfPoint p) {
			const auto mapped = map(p);
			if (!mapped.has_value()) {
				return false;
			}
			sink->BeginFigure(*mapped, D2D1_FIGURE_BEGIN_FILLED);
			open = true;
			start = p;
			current = p;
			return true;
		};
		const auto close = [&](D2D1_FIGURE_END end) {
			if (open) {
				sink->EndFigure(end);
				open = false;
			}
		};
		bool usable = true;
		for (const PathVerb verb : verbs) {
			switch (verb) {
			case PathVerb::MoveTo:
				close(D2D1_FIGURE_END_OPEN);
				usable = at < points.size() && begin(points[at]);
				at += 1u;
				break;
			case PathVerb::LineTo: {
				if (at >= points.size()) {
					usable = false;
					break;
				}
				if (!open) {
					usable = begin(current); // a segment after a close starts where that figure began (8.5.2.1)
				}
				const auto mapped = usable ? map(points[at]) : std::nullopt;
				if (!mapped.has_value()) {
					usable = false;
					break;
				}
				sink->AddLine(*mapped);
				current = points[at];
				at += 1u;
				break;
			}
			case PathVerb::CubicBezierTo: {
				if (at + 3u > points.size()) {
					usable = false;
					break;
				}
				if (!open) {
					usable = begin(current);
				}
				// Flattened here, on the whole page's grid: Direct2D flattening in single precision rounds differently at
				// each tile's offset, which would show as a seam.
				const PdfPoint p0 = current;
				const PdfPoint p1 = points[at];
				const PdfPoint p2 = points[at + 1u];
				const PdfPoint p3 = points[at + 2u];
				const uint32_t pieces = cubic_pieces(p0, p1, p2, p3, px_scale);
				for (uint32_t i = 1u; usable && i <= pieces; ++i) {
					const double t = static_cast<double>(i) / static_cast<double>(pieces);
					const double u = 1.0 - t;
					const double b0 = u * u * u;
					const double b1 = 3.0 * u * u * t;
					const double b2 = 3.0 * u * t * t;
					const double b3 = t * t * t;
					const auto mapped = map(i == pieces ? p3
						: PdfPoint{b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x, b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y});
					if (!mapped.has_value()) {
						usable = false;
						break;
					}
					sink->AddLine(*mapped);
				}
				if (!usable) {
					break;
				}
				current = p3;
				at += 3u;
				break;
			}
			case PathVerb::Rect: {
				if (at + 2u > points.size()) {
					usable = false;
					break;
				}
				close(D2D1_FIGURE_END_OPEN);
				const PdfPoint o = points[at];
				const PdfPoint size = points[at + 1u];
				at += 2u;
				const std::array<PdfPoint, 3> rest{
					PdfPoint{o.x + size.x, o.y}, PdfPoint{o.x + size.x, o.y + size.y}, PdfPoint{o.x, o.y + size.y}};
				usable = begin(o);
				for (const PdfPoint corner : rest) {
					const auto mapped = usable ? map(corner) : std::nullopt;
					if (!mapped.has_value()) {
						usable = false;
						break;
					}
					sink->AddLine(*mapped);
				}
				if (usable) {
					close(D2D1_FIGURE_END_CLOSED);
					current = o;
				}
				break;
			}
			case PathVerb::CloseSubpath:
				close(D2D1_FIGURE_END_CLOSED);
				current = start;
				break;
			}
			if (!usable) {
				break;
			}
		}
		close(D2D1_FIGURE_END_OPEN);
		hr = sink->Close();
		if (FAILED(hr)) {
			fail(hr);
			return nullptr;
		}
		return usable ? geometry : nullptr;
	}

	[[nodiscard]] ComPtr<ID2D1PathGeometry> device_geometry(
		std::span<const PathVerb> verbs, std::span<const PdfPoint> points, FillRule rule, const PdfMatrix& to_full)
	{
		return build_geometry(verbs, points, rule, largest_scale(to_full), [&](PdfPoint p) { return device_point(to_full, p); });
	}

	[[nodiscard]] ComPtr<ID2D1SolidColorBrush> brush(const D2D1_COLOR_F& color) noexcept
	{
		ComPtr<ID2D1SolidColorBrush> made;
		const HRESULT hr = target->CreateSolidColorBrush(color, made.GetAddressOf());
		if (FAILED(hr)) {
			fail(hr);
			return nullptr;
		}
		return made;
	}

	void fill_path(std::span<const PathVerb> verbs, std::span<const PdfPoint> points, FillRule rule,
		const PdfMatrix& to_full, const D2D1_COLOR_F& color)
	{
		const ComPtr<ID2D1PathGeometry> geometry = device_geometry(verbs, points, rule, to_full);
		const ComPtr<ID2D1SolidColorBrush> paint = geometry != nullptr ? brush(color) : nullptr;
		if (paint != nullptr) {
			target->SetTransform(D2D1::Matrix3x2F::Identity());
			target->FillGeometry(geometry.Get(), paint.Get());
		}
	}

	// The stroke style for a pen `width_units` wide in the units its dashes are in, or a solid one past the dash ceiling
	// for a path `length_px` long whose one unit is `px_per_unit` pixels.
	[[nodiscard]] ComPtr<ID2D1StrokeStyle> stroke_style(const DisplayList& list, const SetStrokeStyleCommand& stroke,
		double width_units, double px_per_unit, double length_px) noexcept
	{
		std::vector<float> dashes;
		float offset = 0.0f;
		const auto values = list.dash_values(stroke);
		if (values.has_value() && !values->empty() && width_units > 0.0) {
			double sum = 0.0;
			bool valid = true;
			for (const float value : *values) {
				valid = valid && std::isfinite(value) && value >= 0.0f;
				sum += static_cast<double>(value);
			}
			if (valid && sum > 0.0) {
				// An odd pattern repeats with its dashes and gaps exchanged (8.4.3.6), as Direct2D draws one, so a period is
				// two passes of it.
				const double period_px = sum * px_per_unit * (values->size() % 2u == 1u ? 2.0 : 1.0);
				const double count_per_period = static_cast<double>(values->size()) * (values->size() % 2u == 1u ? 2.0 : 1.0);
				const double pieces = period_px > 0.0 ? length_px / period_px * count_per_period : 1e300;
				if (pieces > static_cast<double>(kMaxDashSegments)) {
					record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::DashSegments,
						static_cast<uint64_t>(std::min(pieces, 1e18)), kMaxDashSegments,
						"A dash pattern would cut one stroke past the limit; it is drawn solid"));
				} else {
					try {
						dashes.reserve(values->size());
						for (const float value : *values) {
							dashes.push_back(static_cast<float>(static_cast<double>(value) / width_units));
						}
					} catch (const std::bad_alloc&) {
						fail(E_OUTOFMEMORY);
						return nullptr;
					}
					offset = static_cast<float>(static_cast<double>(stroke.dash_phase) / width_units);
				}
			}
		}
		D2D1_CAP_STYLE cap = D2D1_CAP_STYLE_FLAT;
		if (stroke.line_cap == LineCap::Round) {
			cap = D2D1_CAP_STYLE_ROUND;
		} else if (stroke.line_cap == LineCap::ProjectingSquare) {
			cap = D2D1_CAP_STYLE_SQUARE;
		}
		D2D1_LINE_JOIN join = D2D1_LINE_JOIN_MITER_OR_BEVEL; // past its limit a miter is beveled (8.4.3.5)
		if (stroke.line_join == LineJoin::Round) {
			join = D2D1_LINE_JOIN_ROUND;
		} else if (stroke.line_join == LineJoin::Bevel) {
			join = D2D1_LINE_JOIN_BEVEL;
		}
		const D2D1_STROKE_STYLE_PROPERTIES properties{cap, cap, cap, join, std::max(stroke.miter_limit, 1.0f),
			dashes.empty() ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM, dashes.empty() ? 0.0f : offset};
		ComPtr<ID2D1StrokeStyle> style;
		const HRESULT hr = d2d->CreateStrokeStyle(properties, dashes.empty() ? nullptr : dashes.data(),
			static_cast<UINT32>(dashes.size()), style.GetAddressOf());
		if (FAILED(hr)) {
			fail(hr);
			return nullptr;
		}
		return style;
	}

	// Strokes a path whose points `to_full` places, with a pen under `pen` (the CTM to pixels, ISO 32000-1 8.5.3.2). The
	// outline is widened once in a space that does not know the tile, the pen's own about the path's first point, and only
	// then placed on the page's grid and filled: Direct2D stroking in single precision rounds differently at each tile's
	// offset, which would show as a seam.
	void stroke_path(const DisplayList& list, std::span<const PathVerb> verbs, std::span<const PdfPoint> points,
		const PdfMatrix& to_full, const PdfMatrix& pen, const SetStrokeStyleCommand& stroke, const D2D1_COLOR_F& color)
	{
		if (points.empty() || !is_finite_matrix(to_full) || !is_finite_matrix(pen) || !std::isfinite(stroke.line_width)) {
			return;
		}
		const double xx = pen.a * pen.a + pen.b * pen.b;
		const double yy = pen.c * pen.c + pen.d * pen.d;
		const double xy = pen.a * pen.c + pen.b * pen.d;
		const double det = pen.a * pen.d - pen.b * pen.c;
		const double tolerance = 1e-9 * std::max(xx, yy);
		const bool round_pen = std::abs(xx - yy) <= tolerance && std::abs(xy) <= tolerance;
		const bool singular = !(std::abs(det) > 1e-12 * std::max(xx, yy)) || !(std::max(xx, yy) > 0.0);
		const double max_scale = std::sqrt(std::max(xx, yy));
		const double line_width = static_cast<double>(stroke.line_width);

		// The space the pen is round in, `L` taking it to pixels: pixels themselves for a pen already round there.
		PdfMatrix l = PdfMatrix::identity();
		double width_units = 1.0;
		double dash_scale = 1.0; // pen-space units per user-space dash unit
		// A zero width is the thinnest line the device draws, one pixel whatever the CTM (8.4.3.2), so it is drawn in device
		// space with no pen to shape it; its dashes take the CTM's mean scale.
		const bool hairline = !(line_width > 0.0);
		if (round_pen || singular || hairline) {
			const double scale = singular ? max_scale : round_pen ? std::sqrt(xx) : std::sqrt(std::abs(det));
			width_units = line_width > 0.0 ? std::min(line_width * scale, kMaxDeviceCoordinate) : 1.0;
			if (!(width_units > 0.0)) {
				width_units = 1.0;
			}
			dash_scale = scale > 0.0 ? scale : 1.0;
		} else {
			l = PdfMatrix{pen.a, pen.b, pen.c, pen.d, 0.0, 0.0};
			width_units = line_width;
		}
		const double l_det = l.a * l.d - l.b * l.c;
		const PdfMatrix inverse{l.d / l_det, -l.b / l_det, -l.c / l_det, l.a / l_det, 0.0, 0.0};

		const PdfPoint first = transform_point(to_full, points[0]);
		if (!std::isfinite(first.x) || !std::isfinite(first.y)) {
			return;
		}
		const PdfPoint origin{std::floor(snapped(first.x)), std::floor(snapped(first.y))};

		// The polygon's length in pixels, which bounds the dash count.
		double length_px = 0.0;
		std::optional<PdfPoint> previous;
		for (const PdfPoint p : points) {
			const PdfPoint full = transform_point(to_full, p);
			if (previous.has_value() && std::isfinite(full.x) && std::isfinite(full.y)) {
				length_px += std::hypot(full.x - previous->x, full.y - previous->y);
			}
			previous = full;
		}
		const double pen_scale = largest_scale(l);
		const ComPtr<ID2D1StrokeStyle> style =
			stroke_style(list, stroke, width_units / dash_scale, dash_scale * pen_scale, length_px);
		const ComPtr<ID2D1PathGeometry> source =
			build_geometry(verbs, points, FillRule::NonZero, largest_scale(to_full), [&](PdfPoint p) -> std::optional<D2D1_POINT_2F> {
				const PdfPoint full = transform_point(to_full, p);
				if (!std::isfinite(full.x) || !std::isfinite(full.y)) {
					return std::nullopt;
				}
				const PdfPoint q = transform_point(inverse, PdfPoint{snapped(full.x) - origin.x, snapped(full.y) - origin.y});
				return D2D1_POINT_2F{static_cast<float>(q.x), static_cast<float>(q.y)};
			});
		if (source == nullptr || style == nullptr) {
			return;
		}
		ComPtr<ID2D1PathGeometry> outline;
		HRESULT hr = d2d->CreatePathGeometry(outline.GetAddressOf());
		ComPtr<ID2D1GeometrySink> out;
		if (SUCCEEDED(hr)) {
			hr = outline->Open(out.GetAddressOf());
		}
		if (FAILED(hr)) {
			fail(hr);
			return;
		}
		out->SetFillMode(D2D1_FILL_MODE_WINDING);
		WidenedOutlineSink sink(out.Get(), pen_scale, [&](D2D1_POINT_2F q) {
			const PdfPoint moved = transform_point(l, PdfPoint{static_cast<double>(q.x), static_cast<double>(q.y)});
			return D2D1_POINT_2F{static_cast<float>(snapped(moved.x + origin.x) - tile_x), static_cast<float>(snapped(moved.y + origin.y) - tile_y)};
		});
		const float flatness = static_cast<float>(1.0 / 32.0 / std::max(pen_scale, 1e-9));
		hr = source->Widen(static_cast<float>(width_units), style.Get(), nullptr, flatness, &sink);
		const HRESULT closed = out->Close();
		if (hr == E_OUTOFMEMORY || closed == E_OUTOFMEMORY) {
			fail(E_OUTOFMEMORY);
			return;
		}
		if (FAILED(hr) || FAILED(closed)) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A stroke could not be widened; it is not drawn"); // recorded
			return;
		}
		const ComPtr<ID2D1SolidColorBrush> paint = brush(color);
		if (paint != nullptr) {
			target->SetTransform(D2D1::Matrix3x2F::Identity());
			target->FillGeometry(outline.Get(), paint.Get());
		}
	}

	void pop_clips(size_t count) noexcept
	{
		while (clips.size() > count) {
			switch (clips.back().kind) {
			case ClipEntry::Kind::Rect:
				target->PopAxisAlignedClip();
				break;
			case ClipEntry::Kind::Layer:
				target->PopLayer();
				--layers_in_use;
				break;
			case ClipEntry::Kind::None:
				break;
			}
			clips.pop_back();
		}
	}

	[[nodiscard]] ID2D1Layer* next_layer() noexcept
	{
		try {
			if (layers_in_use == layers.size()) {
				ComPtr<ID2D1Layer> layer;
				const HRESULT hr = target->CreateLayer(nullptr, layer.GetAddressOf());
				if (FAILED(hr)) {
					fail(hr);
					return nullptr;
				}
				layers.push_back(std::move(layer));
			}
		} catch (const std::bad_alloc&) {
			fail(E_OUTOFMEMORY);
			return nullptr;
		}
		return layers[layers_in_use++].Get();
	}

	void push_rect_clip(const D2D1_RECT_F& rect, D2D1_ANTIALIAS_MODE mode)
	{
		target->SetTransform(D2D1::Matrix3x2F::Identity());
		target->PushAxisAlignedClip(rect, mode);
		clips.push_back(ClipEntry{ClipEntry::Kind::Rect});
	}

	void push_clip(const DisplayList& list, const PushClipPathCommand& command, const PdfMatrix& to_full)
	{
		const auto path = list.path(command.path_index);
		const auto verbs = path.has_value() ? list.path_verbs(*path) : PdfResult<std::span<const PathVerb>>(std::span<const PathVerb>{});
		const auto points = path.has_value() ? list.path_points(*path) : PdfResult<std::span<const PdfPoint>>(std::span<const PdfPoint>{});
		constexpr D2D1_RECT_F kNothing{0.0f, 0.0f, 0.0f, 0.0f};
		if (!path.has_value() || !verbs.has_value() || !points.has_value() || !is_finite_matrix(to_full)) {
			push_rect_clip(kNothing, D2D1_ANTIALIAS_MODE_ALIASED); // a clip that cannot be read shows nothing rather than everything
			return;
		}
		if (verbs->size() == 1u && (*verbs)[0] == PathVerb::Rect && points->size() == 2u && axis_aligned(to_full)) {
			const PdfPoint o = (*points)[0];
			const PdfPoint size = (*points)[1];
			const auto a = device_point(to_full, o);
			const auto b = device_point(to_full, PdfPoint{o.x + size.x, o.y + size.y});
			if (!a.has_value() || !b.has_value()) {
				push_rect_clip(kNothing, D2D1_ANTIALIAS_MODE_ALIASED);
				return;
			}
			const D2D1_RECT_F rect{std::min(a->x, b->x), std::min(a->y, b->y), std::max(a->x, b->x), std::max(a->y, b->y)};
			if (static_cast<double>(rect.left) <= 0.0 && static_cast<double>(rect.top) <= 0.0 &&
				static_cast<double>(rect.right) >= tile_w && static_cast<double>(rect.bottom) >= tile_h) {
				clips.push_back(ClipEntry{ClipEntry::Kind::None}); // the tile is wholly inside it
				return;
			}
			push_rect_clip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			return;
		}
		const ComPtr<ID2D1PathGeometry> geometry = device_geometry(*verbs, *points, command.fill_rule, to_full);
		D2D1_RECT_F bounds{};
		if (geometry == nullptr || FAILED(geometry->GetBounds(nullptr, &bounds)) || bounds.left > bounds.right) {
			push_rect_clip(kNothing, D2D1_ANTIALIAS_MODE_ALIASED);
			return;
		}
		const D2D1_RECT_F content{std::max(bounds.left, 0.0f), std::max(bounds.top, 0.0f),
			std::min(bounds.right, static_cast<float>(tile_w)), std::min(bounds.bottom, static_cast<float>(tile_h))};
		if (content.left >= content.right || content.top >= content.bottom) {
			push_rect_clip(kNothing, D2D1_ANTIALIAS_MODE_ALIASED); // off this tile, so nothing it holds shows here
			return;
		}
		ID2D1Layer* const layer = next_layer();
		if (layer == nullptr) {
			push_rect_clip(kNothing, D2D1_ANTIALIAS_MODE_ALIASED);
			return;
		}
		target->SetTransform(D2D1::Matrix3x2F::Identity());
		// The layer's own geometric mask rounds by tile, up to 16 levels along a curve. Its coverage is drawn instead by the
		// same fill every path takes, and the layer only multiplies by it, pixel for pixel.
		const size_t depth = layers_in_use - 1u;
		if (depth == clip_masks.size()) {
			ComPtr<ID2D1BitmapRenderTarget> made;
			const HRESULT created = target->CreateCompatibleRenderTarget(made.GetAddressOf());
			if (FAILED(created)) {
				fail(created);
				--layers_in_use;
				push_rect_clip(kNothing, D2D1_ANTIALIAS_MODE_ALIASED);
				return;
			}
			made->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			clip_masks.push_back(std::move(made));
		}
		ID2D1BitmapRenderTarget* const mask_target = clip_masks[depth].Get();
		ComPtr<ID2D1SolidColorBrush> mask_paint;
		ComPtr<ID2D1Bitmap> mask_bitmap;
		ComPtr<ID2D1BitmapBrush> mask_brush;
		HRESULT hr = mask_target->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f}, mask_paint.GetAddressOf());
		if (SUCCEEDED(hr)) {
			mask_target->BeginDraw();
			mask_target->Clear(D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.0f});
			mask_target->FillGeometry(geometry.Get(), mask_paint.Get());
			hr = mask_target->EndDraw();
		}
		if (SUCCEEDED(hr)) {
			hr = mask_target->GetBitmap(mask_bitmap.GetAddressOf());
		}
		if (SUCCEEDED(hr)) {
			hr = target->CreateBitmapBrush(mask_bitmap.Get(),
				D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
				mask_brush.GetAddressOf());
		}
		if (FAILED(hr)) {
			fail(hr);
			--layers_in_use;
			push_rect_clip(kNothing, D2D1_ANTIALIAS_MODE_ALIASED);
			return;
		}
		const D2D1_RECT_F whole_pixels{std::floor(content.left), std::floor(content.top), std::ceil(content.right), std::ceil(content.bottom)};
		D2D1_LAYER_PARAMETERS parameters = D2D1::LayerParameters(whole_pixels);
		parameters.opacityBrush = mask_brush.Get();
		target->PushLayer(parameters, layer);
		clips.push_back(ClipEntry{ClipEntry::Kind::Layer});
	}

	// Cells `[x0, x1) x [y0, y1)` of `source` reduced by whole factors, each the mean of the source cells it covers (their
	// colours are premultiplied, so a mean is right). The grid starts at the image's corner, so every tile reads the same
	// value for a reduced cell. Row-padded sources are read through their stride.
	[[nodiscard]] static std::vector<uint8_t> reduced_cells(
		const DecodedBitmap& source, uint32_t reduce_x, uint32_t reduce_y, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
	{
		const size_t width = x1 - x0;
		const size_t height = y1 - y0;
		std::vector<uint8_t> out(width * height * 4u);
		uint8_t* to = out.data();
		const uint8_t* const pixels = source.pixels.data();
		for (uint32_t y = y0; y < y1; ++y) {
			const uint64_t row_begin = uint64_t{y} * reduce_y;
			const uint64_t row_end = std::min<uint64_t>(row_begin + reduce_y, source.height);
			for (uint32_t x = x0; x < x1; ++x) {
				const uint64_t cell_begin = uint64_t{x} * reduce_x;
				const uint64_t cell_end = std::min<uint64_t>(cell_begin + reduce_x, source.width);
				std::array<uint64_t, 4> sum{};
				for (uint64_t row = row_begin; row < row_end; ++row) {
					const uint8_t* from = pixels + row * source.stride + cell_begin * 4u;
					for (uint64_t cell = cell_begin; cell < cell_end; ++cell, from += 4) {
						for (size_t channel = 0; channel < 4u; ++channel) {
							sum[channel] += from[channel];
						}
					}
				}
				const uint64_t count = (row_end - row_begin) * (cell_end - cell_begin);
				for (size_t channel = 0; channel < 4u; ++channel) {
					*to++ = count == 0u ? uint8_t{0} : static_cast<uint8_t>((sum[channel] + count / 2u) / count);
				}
			}
		}
		return out;
	}

	// A whole image at one reduction, kept between tiles within `kRasterImageCacheBytes`.
	[[nodiscard]] std::shared_ptr<const std::vector<uint8_t>> reduced_image(
		const std::shared_ptr<const DecodedBitmap>& source, uint32_t reduce_x, uint32_t reduce_y, uint32_t width, uint32_t height)
	{
		const ImageKey key{source.get(), reduce_x, reduce_y};
		const auto found = image_index.find(key);
		if (found != image_index.end()) {
			images.splice(images.begin(), images, found->second);
			return images.front().pixels;
		}
		auto pixels = std::make_shared<const std::vector<uint8_t>>(reduced_cells(*source, reduce_x, reduce_y, 0u, 0u, width, height));
		images.push_front(CachedImage{key, source, pixels});
		image_index[key] = images.begin();
		image_bytes += pixels->size();
		// What a sampler still reads stays alive through its own reference, past any eviction here.
		while (image_bytes > g_image_cache_bytes && images.size() > 1u) {
			image_bytes -= images.back().pixels->size();
			image_index.erase(images.back().key);
			images.pop_back();
		}
		stats.image_cache_bytes = image_bytes;
		return pixels;
	}

	// An image to read a device pixel from. A shrunken image is box-reduced by whole factors first, each axis its own, so a
	// thin stroke in a scan keeps its share of each pixel where bilinear sampling would step over it.
	struct ImageSampler {
		std::shared_ptr<const std::vector<uint8_t>> reduced; // holds `pixels` when they are a shrunken copy
		const uint8_t* pixels{nullptr};
		size_t stride{0u};
		uint32_t width{0u};
		uint32_t height{0u};
		bool linear{false};
		// Whole-page pixels to cells.
		double a{0.0}, b{0.0}, c{0.0}, d{0.0}, e{0.0}, f{0.0};

		// The premultiplied channels at the centre of whole-page pixel (x, y), on no tile's account.
		[[nodiscard]] std::array<double, 4> at(double x, double y) const noexcept
		{
			const double px = x + 0.5;
			const double py = y + 0.5;
			const double u = a * px + c * py + e;
			const double v = b * px + d * py + f;
			const auto cell = [this](int64_t cx, int64_t cy) {
				const size_t x_at = static_cast<size_t>(std::clamp<int64_t>(cx, 0, int64_t{width} - 1));
				const size_t y_at = static_cast<size_t>(std::clamp<int64_t>(cy, 0, int64_t{height} - 1));
				return pixels + y_at * stride + x_at * 4u;
			};
			std::array<double, 4> out{};
			if (!linear) {
				const uint8_t* const p = cell(static_cast<int64_t>(std::floor(u)), static_cast<int64_t>(std::floor(v)));
				for (size_t i = 0; i < 4u; ++i) {
					out[i] = p[i];
				}
				return out;
			}
			const double fu = u - 0.5;
			const double fv = v - 0.5;
			const double x0 = std::floor(fu);
			const double y0 = std::floor(fv);
			const double tx = fu - x0;
			const double ty = fv - y0;
			const int64_t ix = static_cast<int64_t>(x0);
			const int64_t iy = static_cast<int64_t>(y0);
			const uint8_t* const p00 = cell(ix, iy);
			const uint8_t* const p10 = cell(ix + 1, iy);
			const uint8_t* const p01 = cell(ix, iy + 1);
			const uint8_t* const p11 = cell(ix + 1, iy + 1);
			for (size_t i = 0; i < 4u; ++i) {
				const double top = p00[i] + (p10[i] - p00[i]) * tx;
				const double bottom = p01[i] + (p11[i] - p01[i]) * tx;
				out[i] = top + (bottom - top) * ty;
			}
			return out;
		}
	};

	[[nodiscard]] std::optional<ImageSampler> sampler_for(
		const std::shared_ptr<const DecodedBitmap>& source, const PdfMatrix& unit_to_full, bool linear)
	{
		const double width = source->width;
		const double height = source->height;
		const double cell_x = std::hypot(unit_to_full.a, unit_to_full.b) / width;
		const double cell_y = std::hypot(unit_to_full.c, unit_to_full.d) / height;
		const auto factor = [](double cell, uint32_t cells) {
			return cell > 0.0 && cell < 0.5 ? static_cast<uint32_t>(std::clamp(std::floor(1.0 / cell), 1.0, static_cast<double>(cells))) : 1u;
		};
		const uint32_t reduce_x = factor(cell_x, source->width);
		const uint32_t reduce_y = factor(cell_y, source->height);
		const uint32_t reduced_width = (source->width + reduce_x - 1u) / reduce_x;
		const uint32_t reduced_height = (source->height + reduce_y - 1u) / reduce_y;
		// Row 0 is at the top of the unit square (ISO 32000-1 8.9.4).
		const PdfMatrix cells{reduce_x / width, 0.0, 0.0, -(reduce_y / height), 0.0, 1.0};
		const PdfMatrix m = concat_matrix(cells, unit_to_full);
		const double det = m.a * m.d - m.b * m.c;
		if (!is_finite_matrix(m) || det == 0.0 || !std::isfinite(det)) {
			return std::nullopt;
		}
		ImageSampler out;
		if (reduce_x == 1u && reduce_y == 1u) {
			out.pixels = source->pixels.data();
			out.stride = source->stride;
		} else {
			out.reduced = reduced_image(source, reduce_x, reduce_y, reduced_width, reduced_height);
			out.pixels = out.reduced->data();
			out.stride = size_t{reduced_width} * 4u;
		}
		out.width = reduced_width;
		out.height = reduced_height;
		out.linear = linear;
		out.a = m.d / det;
		out.b = -m.b / det;
		out.c = -m.c / det;
		out.d = m.a / det;
		out.e = (m.c * m.f - m.d * m.e) / det;
		out.f = (m.b * m.e - m.a * m.f) / det;
		return out;
	}

	// Whether a cell of `bitmap` covers less than a pixel: a shrunken image is always filtered, `/Interpolate` choosing
	// how an enlarged one looks.
	[[nodiscard]] static bool minified(const DecodedBitmap& bitmap, const PdfMatrix& unit_to_full) noexcept
	{
		const double cells = static_cast<double>(bitmap.width) * static_cast<double>(bitmap.height);
		const double area = std::abs(unit_to_full.a * unit_to_full.d - unit_to_full.b * unit_to_full.c);
		return area < cells;
	}

	// An axis-aligned image's edges on whole pixels, at least one wide: strips of one scan then meet with no partly covered
	// row between them, at any zoom, and the same edge is chosen on every tile since it is chosen on the page's grid.
	[[nodiscard]] static PdfMatrix on_whole_pixels(const PdfMatrix& m) noexcept
	{
		const auto edges = [](double origin, double extent, double& out_origin, double& out_extent) {
			const double from = std::round(origin);
			double to = std::round(origin + extent);
			if (to == from) {
				to = from + (extent < 0.0 ? -1.0 : 1.0);
			}
			out_origin = from;
			out_extent = to - from;
		};
		PdfMatrix out = m;
		if (m.b == 0.0 && m.c == 0.0) {
			edges(m.e, m.a, out.e, out.a);
			edges(m.f, m.d, out.f, out.d);
		} else if (m.a == 0.0 && m.d == 0.0) {
			edges(m.e, m.c, out.e, out.c);
			edges(m.f, m.b, out.f, out.b);
		}
		return out;
	}

	void draw_image(const DrawImageCommand& command, const PdfMatrix& ctm_full, float alpha)
	{
		if (command.bitmap == nullptr) {
			return;
		}
		PdfMatrix unit = concat_matrix(command.dest_transform, ctm_full);
		if (!is_finite_matrix(unit)) {
			return;
		}
		unit = on_whole_pixels(unit);
		// The pixels it can touch on this tile, on whole-page coordinates.
		const std::array<PdfPoint, 4> corners{transform_point(unit, PdfPoint{0.0, 0.0}), transform_point(unit, PdfPoint{1.0, 0.0}),
			transform_point(unit, PdfPoint{0.0, 1.0}), transform_point(unit, PdfPoint{1.0, 1.0})};
		double x_min = tile_x + tile_w;
		double y_min = tile_y + tile_h;
		double x_max = tile_x;
		double y_max = tile_y;
		for (const PdfPoint corner : corners) {
			x_min = std::min(x_min, corner.x);
			y_min = std::min(y_min, corner.y);
			x_max = std::max(x_max, corner.x);
			y_max = std::max(y_max, corner.y);
		}
		const double left = std::max(std::floor(x_min), tile_x);
		const double top = std::max(std::floor(y_min), tile_y);
		const double right = std::min(std::ceil(x_max), tile_x + tile_w);
		const double bottom = std::min(std::ceil(y_max), tile_y + tile_h);
		if (left >= right || top >= bottom) {
			return;
		}
		const bool stencil = command.bitmap->is_monochrome_mask;
		const auto image = sampler_for(command.bitmap, unit, command.interpolate || minified(*command.bitmap, unit));
		if (!image.has_value()) {
			return;
		}
		std::optional<ImageSampler> mask;
		if (!stencil && command.soft_mask != nullptr) {
			mask = sampler_for(command.soft_mask, unit, command.soft_mask_interpolate || minified(*command.soft_mask, unit));
			if (!mask.has_value()) {
				return;
			}
		}
		// A stencil's colour is the tint's, premultiplied by its coverage.
		const double tint_b = static_cast<double>(command.mask_tint_color.b) * command.mask_tint_color.a;
		const double tint_g = static_cast<double>(command.mask_tint_color.g) * command.mask_tint_color.a;
		const double tint_r = static_cast<double>(command.mask_tint_color.r) * command.mask_tint_color.a;
		const double tint_a = command.mask_tint_color.a;
		const uint32_t patch_w = static_cast<uint32_t>(right - left);
		const uint32_t patch_h = static_cast<uint32_t>(bottom - top);
		std::vector<uint8_t> patch(size_t{patch_w} * patch_h * 4u);
		uint8_t* to = patch.data();
		const auto byte = [](double value) {
			return static_cast<uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
		};
		for (uint32_t y = 0; y < patch_h; ++y) {
			for (uint32_t x = 0; x < patch_w; ++x) {
				const double px = left + x;
				const double py = top + y;
				std::array<double, 4> s = image->at(px, py);
				if (stencil) {
					const double coverage = s[3];
					s = {tint_b * coverage, tint_g * coverage, tint_r * coverage, tint_a * coverage};
				} else if (mask.has_value()) {
					const double coverage = mask->at(px, py)[3] / 255.0;
					for (double& channel : s) {
						channel *= coverage;
					}
				}
				for (const double channel : s) {
					*to++ = byte(channel);
				}
			}
		}
		const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
		ComPtr<ID2D1Bitmap> bitmap;
		const HRESULT hr = target->CreateBitmap(D2D1::SizeU(patch_w, patch_h), patch.data(), patch_w * 4u, properties, bitmap.GetAddressOf());
		if (FAILED(hr)) {
			fail(hr);
			return;
		}
		target->SetTransform(D2D1::Matrix3x2F::Identity());
		const float dest_x = static_cast<float>(left - tile_x);
		const float dest_y = static_cast<float>(top - tile_y);
		const D2D1_RECT_F dest{dest_x, dest_y, dest_x + static_cast<float>(patch_w), dest_y + static_cast<float>(patch_h)};
		// An axis-aligned image's edges are on whole pixels already; a turned one is filled through the unit square, whose
		// edge is antialiased as any path's is.
		if (axis_aligned(unit)) {
			target->DrawBitmap(bitmap.Get(), dest, alpha, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
			return;
		}
		const std::array<PathVerb, 1> verbs{PathVerb::Rect};
		const std::array<PdfPoint, 2> points{PdfPoint{0.0, 0.0}, PdfPoint{1.0, 1.0}};
		const ComPtr<ID2D1PathGeometry> square = device_geometry(verbs, points, FillRule::NonZero, unit);
		if (square == nullptr) {
			return;
		}
		ComPtr<ID2D1BitmapBrush> patch_brush;
		const HRESULT brush_hr = target->CreateBitmapBrush(bitmap.Get(),
			D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
			patch_brush.GetAddressOf());
		if (FAILED(brush_hr)) {
			fail(brush_hr);
			return;
		}
		patch_brush->SetTransform(D2D1::Matrix3x2F::Translation(dest_x, dest_y));
		patch_brush->SetOpacity(alpha);
		target->FillGeometry(square.Get(), patch_brush.Get());
	}

	// Whether a box in tile pixels, widened one pixel for antialiasing, can touch the tile.
	[[nodiscard]] bool touches_tile(const PdfRect& full, double widen = 0.0) const noexcept
	{
		const double reach = widen + 1.0;
		return full.x_max + reach >= tile_x && full.x_min - reach <= tile_x + tile_w && full.y_max + reach >= tile_y &&
			full.y_min - reach <= tile_y + tile_h;
	}

	[[nodiscard]] bool glyph_allowed(const LoadedFont& font, uint16_t glyph)
	{
		const GlyphKey key{&font, glyph};
		const auto found = glyph_checks.find(key);
		if (found != glyph_checks.end()) {
			return found->second;
		}
		const bool allowed = font.check_glyph(glyph).has_value(); // a refusal records itself
		glyph_checks.emplace(key, allowed);
		return allowed;
	}

	[[nodiscard]] const GlyphOutline* outline_of(const LoadedFont& font, uint16_t glyph)
	{
		const GlyphKey key{&font, glyph};
		auto found = glyph_outlines.find(key);
		if (found == glyph_outlines.end()) {
			auto outline = font.outline(glyph); // a refusal records itself
			found = glyph_outlines.emplace(key,
				outline.has_value() ? std::optional<GlyphOutline>(std::move(*outline)) : std::nullopt).first;
		}
		return found->second.has_value() ? &*found->second : nullptr;
	}

	void draw_glyph_run(const DisplayList& list, const RecordedGlyphRun& run, const PdfMatrix& base,
		const ReplayState& state, size_t depth)
	{
		const auto glyphs = list.glyphs(run);
		if (!glyphs.has_value()) {
			return;
		}
		const PdfMatrix ctm_full = concat_matrix(run.ctm, base);
		const PdfMatrix text_full = concat_matrix(run.text_matrix, ctm_full);
		if (!is_finite_matrix(text_full)) {
			return;
		}
		if (run.type3_sub_list != nullptr) {
			if (run.render_mode == TextRenderMode::Invisible || run.render_mode == TextRenderMode::Clip) {
				return;
			}
			if (depth >= kMaxType3ReplayDepth) {
				(void)make_pdf_error(ErrorCode::ResourceLimit, "Type 3 glyphs are nested past the limit; the rest are not drawn"); // recorded
				return;
			}
			for (const PositionedGlyph& glyph : *glyphs) {
				const PdfMatrix placed = concat_matrix(translate(glyph.origin_pt.x, glyph.origin_pt.y), text_full);
				replay(*run.type3_sub_list, placed, depth + 1u);
			}
			return;
		}
		if (run.font_handle == nullptr) {
			return; // advance-only glyphs: positioned, nothing to paint (D01 T02 section 43)
		}
		const bool fills = run.render_mode == TextRenderMode::Fill || run.render_mode == TextRenderMode::FillAndStroke ||
			run.render_mode == TextRenderMode::FillAndClip || run.render_mode == TextRenderMode::FillStrokeAndClip;
		const bool strokes = run.render_mode == TextRenderMode::Stroke || run.render_mode == TextRenderMode::FillAndStroke ||
			run.render_mode == TextRenderMode::StrokeAndClip || run.render_mode == TextRenderMode::FillStrokeAndClip;
		if ((!fills && !strokes) || run.font_size == 0.0 || !std::isfinite(run.font_size)) {
			return;
		}
		const auto& font = *static_cast<const LoadedFont*>(run.font_handle.get());
		const bool through_directwrite = font.kind() != LoadedFontKind::OutlineOnly && font.dwrite_face() != nullptr && !strokes;
		const D2D1_COLOR_F fill_color = d2d_color(run.fill_color, state.fill_alpha);
		const D2D1_COLOR_F stroke_color = d2d_color(run.stroke_color, state.stroke_alpha);
		ComPtr<ID2D1SolidColorBrush> text_brush;
		if (through_directwrite) {
			text_brush = brush(fill_color);
			if (text_brush == nullptr) {
				return;
			}
		}
		// How far past its outline a stroked glyph can paint, in pixels.
		double stroke_reach = 0.0;
		if (strokes) {
			const double pen_scale = std::sqrt(std::max(ctm_full.a * ctm_full.a + ctm_full.b * ctm_full.b,
				ctm_full.c * ctm_full.c + ctm_full.d * ctm_full.d));
			const double half = static_cast<double>(state.stroke.line_width) * 0.5;
			const double joins = state.stroke.line_join == LineJoin::Miter
				? std::max(static_cast<double>(state.stroke.miter_limit), std::numbers::sqrt2)
				: std::numbers::sqrt2;
			stroke_reach = std::max(half * joins * pen_scale, 1.0);
		}
		const double em = std::abs(run.font_size);
		const double sign = run.font_size < 0.0 ? -1.0 : 1.0;
		for (const PositionedGlyph& glyph : *glyphs) {
			if (glyph.is_blank) {
				continue;
			}
			const PdfMatrix origin_full = concat_matrix(translate(glyph.origin_pt.x, glyph.origin_pt.y), text_full);
			if (glyph.ink_bounds_pt.has_value() && !touches_tile(transform_rect(origin_full, *glyph.ink_bounds_pt), stroke_reach)) {
				continue;
			}
			++stats.draws_replayed;
			if (font.kind() != LoadedFontKind::OutlineOnly && !glyph_allowed(font, glyph.glyph_index)) {
				continue;
			}
			if (through_directwrite) {
				// DirectWrite's y is down and its size positive: the rest of the size, and the fit, go in the matrix.
				const PdfMatrix local{glyph.width_scale * sign, 0.0, 0.0, -sign, 0.0, 0.0};
				const PdfMatrix m = concat_matrix(local, origin_full);
				if (!is_finite_matrix(m)) {
					continue;
				}
				target->SetTransform(D2D1_MATRIX_3X2_F{static_cast<float>(m.a), static_cast<float>(m.b), static_cast<float>(m.c),
					static_cast<float>(m.d), static_cast<float>(snapped(m.e) - tile_x), static_cast<float>(snapped(m.f) - tile_y)});
				const UINT16 index = glyph.glyph_index;
				const FLOAT advance = 0.0f;
				const DWRITE_GLYPH_RUN one{font.dwrite_face(), static_cast<FLOAT>(em), 1u, &index, &advance, nullptr, FALSE, 0u};
				target->DrawGlyphRun(D2D1_POINT_2F{0.0f, 0.0f}, &one, text_brush.Get(), DWRITE_MEASURING_MODE_NATURAL);
				continue;
			}
			const GlyphOutline* const outline = outline_of(font, glyph.glyph_index);
			if (outline == nullptr) {
				continue;
			}
			// Outlines are in ems, y up: the size and the fit scale them about the glyph's origin.
			const PdfMatrix local{glyph.width_scale * run.font_size, 0.0, 0.0, run.font_size, 0.0, 0.0};
			const PdfMatrix m = concat_matrix(local, origin_full);
			if (!is_finite_matrix(m)) {
				continue;
			}
			if (fills) {
				fill_path(outline->verbs, outline->points, FillRule::NonZero, m, fill_color);
			}
			if (strokes) {
				stroke_path(list, outline->verbs, outline->points, m, ctm_full, state.stroke, stroke_color);
			}
		}
		target->SetTransform(D2D1::Matrix3x2F::Identity());
	}

	// One list under `base` (the list's own page space to whole-page pixels).
	void replay(const DisplayList& list, const PdfMatrix& base, size_t depth)
	{
		std::vector<ReplayState> saved;
		ReplayState state;
		const size_t entry_clips = clips.size();
		state.clip_count = entry_clips;
		const auto commands = list.commands();
		const auto bounds = list.command_bounds();
		for (size_t i = 0; i < commands.size() && SUCCEEDED(failure); ++i) {
			const DisplayCommand& command = commands[i];
			const bool can_paint =
				i < bounds.size() && bounds[i].has_bounds && touches_tile(transform_rect(base, bounds[i].bounds_pt));
			switch (command.kind()) {
			case DisplayCommandKind::SaveState:
				state.clip_count = clips.size();
				saved.push_back(state);
				break;
			case DisplayCommandKind::RestoreState:
				if (!saved.empty()) {
					state = saved.back();
					saved.pop_back();
					pop_clips(state.clip_count);
				}
				break;
			case DisplayCommandKind::ConcatMatrix:
				state.ctm = concat_matrix(command.as<ConcatMatrixCommand>()->matrix, state.ctm);
				break;
			case DisplayCommandKind::SetStrokeStyle:
				state.stroke = *command.as<SetStrokeStyleCommand>();
				break;
			case DisplayCommandKind::SetAlphaAndBlend: {
				const auto& alpha = *command.as<SetAlphaAndBlendCommand>();
				state.stroke_alpha = alpha.stroke_alpha;
				state.fill_alpha = alpha.fill_alpha;
				break;
			}
			case DisplayCommandKind::PushClipPath:
				push_clip(list, *command.as<PushClipPathCommand>(), concat_matrix(state.ctm, base));
				break;
			case DisplayCommandKind::DrawPath: {
				if (!can_paint) {
					break;
				}
				++stats.draws_replayed;
				const auto& draw = *command.as<DrawPathCommand>();
				const auto path = list.path(draw.path_index);
				if (!path.has_value()) {
					break;
				}
				const auto verbs = list.path_verbs(*path);
				const auto points = list.path_points(*path);
				if (!verbs.has_value() || !points.has_value()) {
					break;
				}
				const PdfMatrix ctm_full = concat_matrix(state.ctm, base);
				if (!is_finite_matrix(ctm_full)) {
					break;
				}
				if (draw.fill) {
					fill_path(*verbs, *points, draw.fill_rule, ctm_full, d2d_color(draw.fill_color, state.fill_alpha));
				}
				if (draw.stroke) {
					stroke_path(list, *verbs, *points, ctm_full, ctm_full, state.stroke, d2d_color(draw.stroke_color, state.stroke_alpha));
				}
				break;
			}
			case DisplayCommandKind::DrawGlyphRun:
				if (can_paint) {
					draw_glyph_run(list, *command.as<DrawGlyphRunCommand>(), base, state, depth);
				}
				break;
			case DisplayCommandKind::DrawImage:
				if (can_paint) {
					++stats.draws_replayed;
					draw_image(*command.as<DrawImageCommand>(), concat_matrix(state.ctm, base), state.fill_alpha);
				}
				break;
			}
		}
		pop_clips(entry_clips);
		target->SetTransform(D2D1::Matrix3x2F::Identity());
	}
};

Rasterizer::Rasterizer(std::unique_ptr<State> state) noexcept
	: state_(std::move(state))
{
}

Rasterizer::~Rasterizer() noexcept = default;

const RasterStats& Rasterizer::stats() const noexcept
{
	return state_->stats;
}

PdfResult<std::unique_ptr<Rasterizer>> Rasterizer::create() noexcept
{
	try {
		auto state = std::make_unique<State>();
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		state->com_owned = com == S_OK || com == S_FALSE;
		HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(state->wic.GetAddressOf()));
		if (FAILED(hr)) {
			return raster_failure(hr, "The rasterizer could not create its WIC factory");
		}
		D2D1_FACTORY_OPTIONS options{};
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &options,
			reinterpret_cast<void**>(state->d2d.GetAddressOf()));
		if (FAILED(hr)) {
			return raster_failure(hr, "The rasterizer could not create its Direct2D factory");
		}
		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(state->dwrite.GetAddressOf()));
		if (FAILED(hr)) {
			return raster_failure(hr, "The rasterizer could not create its DirectWrite factory");
		}
		// Fixed, so the pixels do not follow this user's ClearType tuning.
		hr = state->dwrite->CreateCustomRenderingParams(1.8f, 0.5f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
			DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, state->text_params.GetAddressOf());
		if (FAILED(hr)) {
			return raster_failure(hr, "The rasterizer could not create its text rendering parameters");
		}
		return std::unique_ptr<Rasterizer>(new Rasterizer(std::move(state)));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory creating a rasterizer");
	}
}

PdfResult<void> Rasterizer::render_tile(const std::shared_ptr<const DisplayList>& list, const TileSpec& spec,
	std::span<uint8_t> output, size_t stride) noexcept
{
	State& s = *state_;
	if (s.owner != std::this_thread::get_id()) {
		return make_pdf_error(ErrorCode::InternalError, "A Rasterizer was used off the thread that made it");
	}
	const auto invalid = [](const char* message) -> PdfResult<void> { return make_pdf_error(ErrorCode::InvalidArgument, message); };
	if (list == nullptr) {
		return invalid("A tile needs a display list");
	}
	if (spec.rotation_deg != 0 && spec.rotation_deg != 90 && spec.rotation_deg != 180 && spec.rotation_deg != 270) {
		return invalid("A tile's rotation must be 0, 90, 180 or 270 degrees");
	}
	if (!std::isfinite(spec.dpi_scale) || spec.dpi_scale <= 0.0) {
		return invalid("A tile's scale must be finite and positive");
	}
	if (spec.tile_width_px == 0u || spec.tile_height_px == 0u || spec.tile_width_px > kMaxTileEdgePx || spec.tile_height_px > kMaxTileEdgePx) {
		return invalid("A tile's size is empty or past the limit");
	}
	const size_t row_bytes = size_t{spec.tile_width_px} * 4u;
	if (stride < row_bytes) {
		return invalid("A tile's stride is shorter than its row");
	}
	const size_t height = spec.tile_height_px;
	if ((height - 1u) > (std::numeric_limits<size_t>::max() - row_bytes) / stride ||
		output.size() < stride * (height - 1u) + row_bytes) {
		return invalid("A tile's buffer is shorter than its rows");
	}
	if (!spec.background_rgba.is_valid()) {
		return invalid("A tile's background is not a colour");
	}
	s.stats.draws_replayed = 0u;
	try {
		HRESULT hr = s.ensure_target(spec.tile_width_px, spec.tile_height_px);
		if (FAILED(hr)) {
			s.drop_target();
			return raster_failure(hr, "The rasterizer could not create its render target");
		}
		s.failure = S_OK;
		s.tile_x = spec.tile_x_px;
		s.tile_y = spec.tile_y_px;
		s.tile_w = spec.tile_width_px;
		s.tile_h = spec.tile_height_px;
		s.clips.clear();
		s.layers_in_use = 0u;
		s.glyph_checks.clear();
		s.glyph_outlines.clear();

		const PdfRect& box = list->page_box_pt();
		const PdfMatrix full = page_to_full(box, spec.dpi_scale, spec.rotation_deg);
		const PagePixelSize page = page_pixel_size(box, spec.dpi_scale, spec.rotation_deg);

		s.target->BeginDraw();
		s.target->SetTransform(D2D1::Matrix3x2F::Identity());
		const ColorRgba& bg = spec.background_rgba;
		s.target->Clear(D2D1_COLOR_F{bg.r, bg.g, bg.b, bg.a});
		// Only the page paints: a tile past its edge shows the background there, on whole pixels.
		s.push_rect_clip(D2D1_RECT_F{static_cast<float>(-s.tile_x), static_cast<float>(-s.tile_y),
							 static_cast<float>(static_cast<double>(page.width) - s.tile_x),
							 static_cast<float>(static_cast<double>(page.height) - s.tile_y)},
			D2D1_ANTIALIAS_MODE_ALIASED);
		s.replay(*list, full, 0u);
		s.pop_clips(0u);
		hr = s.target->EndDraw();
		if (SUCCEEDED(hr) && FAILED(s.failure)) {
			hr = s.failure;
		}
		if (FAILED(hr)) {
			s.drop_target();
			return raster_failure(hr, "Direct2D could not draw a tile");
		}
		const WICRect whole{0, 0, static_cast<INT>(spec.tile_width_px), static_cast<INT>(spec.tile_height_px)};
		ComPtr<IWICBitmapLock> lock;
		hr = s.target_bitmap->Lock(&whole, WICBitmapLockRead, lock.GetAddressOf());
		UINT source_stride = 0u;
		UINT source_size = 0u;
		WICInProcPointer source = nullptr;
		if (SUCCEEDED(hr)) {
			hr = lock->GetStride(&source_stride);
		}
		if (SUCCEEDED(hr)) {
			hr = lock->GetDataPointer(&source_size, &source);
		}
		if (FAILED(hr) || source == nullptr) {
			s.drop_target();
			return raster_failure(FAILED(hr) ? hr : E_FAIL, "The rasterizer could not read its target");
		}
		for (size_t row = 0; row < height; ++row) {
			std::memcpy(output.data() + row * stride, source + row * source_stride, row_bytes);
		}
		return {};
	} catch (const std::bad_alloc&) {
		s.drop_target();
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory drawing a tile");
	}
}

} // namespace recto::pdf
