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
#include <unordered_map>
#include <utility>
#include <vector>

namespace recto::pdf {

namespace {

using Microsoft::WRL::ComPtr;

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
	size_t layers_in_use{0u};

	struct CachedImage {
		std::shared_ptr<const DecodedBitmap> source; // keeps the key's address from being reused
		ComPtr<ID2D1Bitmap> bitmap;
		size_t bytes{0u};
	};
	std::list<CachedImage> images; // most recently used first
	std::unordered_map<const DecodedBitmap*, std::list<CachedImage>::iterator> image_index;
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
		image_index.clear();
		images.clear();
		image_bytes = 0u;
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
		if (round_pen || singular) {
			const double scale = singular ? max_scale : std::sqrt(xx);
			// A zero width is the thinnest line the device draws, one pixel (8.4.3.2).
			width_units = line_width > 0.0 ? std::min(line_width * scale, kMaxDeviceCoordinate) : 1.0;
			if (!(width_units > 0.0)) {
				width_units = 1.0;
			}
			dash_scale = scale > 0.0 ? scale : 1.0;
		} else {
			l = PdfMatrix{pen.a, pen.b, pen.c, pen.d, 0.0, 0.0};
			width_units = line_width > 0.0 ? line_width : 1.0 / std::sqrt(std::abs(det));
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
		target->PushLayer(D2D1::LayerParameters(content, geometry.Get()), layer);
		clips.push_back(ClipEntry{ClipEntry::Kind::Layer});
	}

	// The worker's upload of a decoded image, kept between tiles within `kRasterImageCacheBytes`.
	[[nodiscard]] ComPtr<ID2D1Bitmap> uploaded(const std::shared_ptr<const DecodedBitmap>& source)
	{
		const auto found = image_index.find(source.get());
		if (found != image_index.end()) {
			images.splice(images.begin(), images, found->second);
			return found->second->bitmap;
		}
		const UINT32 limit = target->GetMaximumBitmapSize();
		if (source->width > limit || source->height > limit) {
			(void)make_pdf_error(ErrorCode::UnsupportedFeature, "An image is larger than Direct2D can hold; it is not drawn"); // recorded
			return nullptr;
		}
		const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
		ComPtr<ID2D1Bitmap> bitmap;
		const HRESULT hr = target->CreateBitmap(D2D1::SizeU(source->width, source->height), source->pixels.data(),
			source->stride, properties, bitmap.GetAddressOf());
		if (FAILED(hr)) {
			fail(hr);
			return nullptr;
		}
		const size_t bytes = source->byte_size();
		images.push_front(CachedImage{source, bitmap, bytes});
		image_index[source.get()] = images.begin();
		image_bytes += bytes;
		while (image_bytes > kRasterImageCacheBytes && images.size() > 1u) {
			image_bytes -= images.back().bytes;
			image_index.erase(images.back().source.get());
			images.pop_back();
		}
		return bitmap;
	}

	// The matrix from a bitmap's pixels (row 0 at the top of the unit square, ISO 32000-1 8.9.4) to tile pixels.
	[[nodiscard]] std::optional<D2D1_MATRIX_3X2_F> bitmap_world(const DecodedBitmap& bitmap, const PdfMatrix& unit_to_full) const noexcept
	{
		const PdfMatrix pixels{1.0 / static_cast<double>(bitmap.width), 0.0, 0.0, -1.0 / static_cast<double>(bitmap.height), 0.0, 1.0};
		const PdfMatrix m = concat_matrix(pixels, unit_to_full);
		if (!is_finite_matrix(m)) {
			return std::nullopt;
		}
		return D2D1_MATRIX_3X2_F{static_cast<float>(m.a), static_cast<float>(m.b), static_cast<float>(m.c), static_cast<float>(m.d),
			static_cast<float>(snapped(m.e) - tile_x), static_cast<float>(snapped(m.f) - tile_y)};
	}

	// Whether a cell of `bitmap` covers less than a pixel: a shrunken image is always filtered, `/Interpolate` choosing
	// how an enlarged one looks.
	[[nodiscard]] static bool minified(const DecodedBitmap& bitmap, const PdfMatrix& unit_to_full) noexcept
	{
		const double cells = static_cast<double>(bitmap.width) * static_cast<double>(bitmap.height);
		const double area = std::abs(unit_to_full.a * unit_to_full.d - unit_to_full.b * unit_to_full.c);
		return area < cells;
	}

	void draw_image(const DrawImageCommand& command, const PdfMatrix& ctm_full, float alpha)
	{
		if (command.bitmap == nullptr) {
			return;
		}
		const PdfMatrix unit = concat_matrix(command.dest_transform, ctm_full);
		const auto world = bitmap_world(*command.bitmap, unit);
		if (!world.has_value()) {
			return;
		}
		const bool stencil = command.bitmap->is_monochrome_mask;
		const DecodedBitmap* const mask_source = stencil ? command.bitmap.get() : command.soft_mask.get();
		const ComPtr<ID2D1Bitmap> bitmap = uploaded(command.bitmap);
		if (bitmap == nullptr) {
			return;
		}
		const bool linear = command.interpolate || minified(*command.bitmap, unit);
		if (mask_source == nullptr) {
			target->SetTransform(*world);
			target->DrawBitmap(bitmap.Get(), nullptr, alpha,
				linear ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
			target->SetTransform(D2D1::Matrix3x2F::Identity());
			return;
		}
		// Through the mask's coverage at its own resolution, inside the unit square so a clamped edge does not bleed.
		const ComPtr<ID2D1Bitmap> mask_bitmap = stencil ? bitmap : uploaded(command.soft_mask);
		const auto mask_world = bitmap_world(*mask_source, unit);
		if (mask_bitmap == nullptr || !mask_world.has_value()) {
			return;
		}
		const bool mask_linear =
			(stencil ? command.interpolate : command.soft_mask_interpolate) || minified(*mask_source, unit);
		ComPtr<ID2D1BitmapBrush> mask_brush;
		HRESULT hr = target->CreateBitmapBrush(mask_bitmap.Get(),
			D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP,
				mask_linear ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
			mask_brush.GetAddressOf());
		if (FAILED(hr)) {
			fail(hr);
			return;
		}
		mask_brush->SetTransform(*mask_world);
		const std::array<PathVerb, 1> verbs{PathVerb::Rect};
		const std::array<PdfPoint, 2> points{PdfPoint{0.0, 0.0}, PdfPoint{1.0, 1.0}};
		const ComPtr<ID2D1PathGeometry> square = device_geometry(verbs, points, FillRule::NonZero, unit);
		if (square == nullptr) {
			return;
		}
		D2D1_RECT_F bounds{};
		if (FAILED(square->GetBounds(nullptr, &bounds))) {
			return;
		}
		const D2D1_RECT_F content{std::max(bounds.left, 0.0f), std::max(bounds.top, 0.0f),
			std::min(bounds.right, static_cast<float>(tile_w)), std::min(bounds.bottom, static_cast<float>(tile_h))};
		if (content.left >= content.right || content.top >= content.bottom) {
			return;
		}
		ID2D1Layer* const layer = next_layer();
		if (layer == nullptr) {
			return;
		}
		target->SetTransform(D2D1::Matrix3x2F::Identity());
		D2D1_LAYER_PARAMETERS parameters = D2D1::LayerParameters(content, square.Get());
		parameters.opacityBrush = mask_brush.Get();
		target->PushLayer(parameters, layer);
		if (stencil) {
			const ComPtr<ID2D1SolidColorBrush> paint = brush(d2d_color(command.mask_tint_color, alpha));
			if (paint != nullptr) {
				target->FillGeometry(square.Get(), paint.Get());
			}
		} else {
			target->SetTransform(*world);
			target->DrawBitmap(bitmap.Get(), nullptr, alpha,
				linear ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
			target->SetTransform(D2D1::Matrix3x2F::Identity());
		}
		target->PopLayer();
		--layers_in_use;
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
