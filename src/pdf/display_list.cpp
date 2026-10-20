// Thread-safe, immutable DisplayList command buffer and DisplayListBuilder
// (foundation plan sections 3.3, 4.1 item 4, 4.2, 4.4, 4.5, 4.8; D01 T02
// section 1).
#include "display_list.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <utility>

namespace recto::pdf {

namespace {

std::atomic<int64_t> g_live_display_lists{0};

constexpr uint32_t kMaxBitmapDimension = 65536u;
constexpr uint64_t kMaxBitmapBytes = 256ull * 1024ull * 1024ull;
constexpr double kGeomEps = 1e-9;

[[nodiscard]] bool is_finite_point(PdfPoint p) noexcept
{
	return std::isfinite(p.x) && std::isfinite(p.y);
}

[[nodiscard]] bool is_valid_rect(const PdfRect& r) noexcept
{
	return std::isfinite(r.x_min) && std::isfinite(r.y_min) &&
		std::isfinite(r.x_max) && std::isfinite(r.y_max) &&
		r.x_min <= r.x_max && r.y_min <= r.y_max;
}

[[nodiscard]] constexpr bool is_valid_line_cap(LineCap cap) noexcept
{
	switch (cap) {
	case LineCap::Butt:
	case LineCap::Round:
	case LineCap::ProjectingSquare:
		return true;
	}
	return false;
}

[[nodiscard]] constexpr bool is_valid_line_join(LineJoin join) noexcept
{
	switch (join) {
	case LineJoin::Miter:
	case LineJoin::Round:
	case LineJoin::Bevel:
		return true;
	}
	return false;
}

[[nodiscard]] constexpr bool is_valid_fill_rule(FillRule rule) noexcept
{
	switch (rule) {
	case FillRule::NonZero:
	case FillRule::EvenOdd:
		return true;
	}
	return false;
}

[[nodiscard]] constexpr bool is_valid_blend_mode(BlendMode mode) noexcept
{
	switch (mode) {
	case BlendMode::Normal:
	case BlendMode::Multiply:
	case BlendMode::Screen:
	case BlendMode::Overlay:
	case BlendMode::Darken:
	case BlendMode::Lighten:
	case BlendMode::ColorDodge:
	case BlendMode::ColorBurn:
	case BlendMode::HardLight:
	case BlendMode::SoftLight:
	case BlendMode::Difference:
	case BlendMode::Exclusion:
	case BlendMode::Hue:
	case BlendMode::Saturation:
	case BlendMode::Color:
	case BlendMode::Luminosity:
		return true;
	}
	return false;
}

[[nodiscard]] constexpr bool is_valid_text_render_mode(TextRenderMode mode) noexcept
{
	switch (mode) {
	case TextRenderMode::Fill:
	case TextRenderMode::Stroke:
	case TextRenderMode::FillAndStroke:
	case TextRenderMode::Invisible:
	case TextRenderMode::FillAndClip:
	case TextRenderMode::StrokeAndClip:
	case TextRenderMode::FillStrokeAndClip:
	case TextRenderMode::Clip:
		return true;
	}
	return false;
}

// Validates that `verbs` and `points` form a well-formed, finite path sequence
// with exact point arity and valid subpath sequencing.
[[nodiscard]] bool validate_path_slice(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points) noexcept
{
	for (const PdfPoint& pt : points) {
		if (!is_finite_point(pt)) {
			return false;
		}
	}

	size_t pt_idx = 0u;
	bool has_cur = false;
	for (const PathVerb verb : verbs) {
		switch (verb) {
		case PathVerb::MoveTo:
			if (pt_idx + 1u > points.size()) {
				return false;
			}
			pt_idx += 1u;
			has_cur = true;
			break;
		case PathVerb::LineTo:
			if (!has_cur || pt_idx + 1u > points.size()) {
				return false;
			}
			pt_idx += 1u;
			break;
		case PathVerb::CubicBezierTo:
			if (!has_cur || pt_idx + 3u > points.size()) {
				return false;
			}
			pt_idx += 3u;
			break;
		case PathVerb::Rect: {
			if (pt_idx + 2u > points.size()) {
				return false;
			}
			const PdfPoint origin = points[pt_idx];
			const PdfPoint size = points[pt_idx + 1u];
			if (!std::isfinite(origin.x + size.x) ||
				!std::isfinite(origin.y + size.y)) {
				return false;
			}
			pt_idx += 2u;
			has_cur = true;
			break;
		}
		case PathVerb::CloseSubpath:
			if (!has_cur) {
				return false;
			}
			break;
		default:
			return false;
		}
	}

	return pt_idx == points.size();
}

[[nodiscard]] constexpr double eval_cubic_1d(
	double p0,
	double p1,
	double p2,
	double p3,
	double t) noexcept
{
	const double u = 1.0 - t;
	return u * u * u * p0 +
		3.0 * u * u * t * p1 +
		3.0 * u * t * t * p2 +
		t * t * t * p3;
}

[[nodiscard]] constexpr PdfPoint eval_cubic_2d(
	PdfPoint p0,
	PdfPoint p1,
	PdfPoint p2,
	PdfPoint p3,
	double t) noexcept
{
	return PdfPoint{
		eval_cubic_1d(p0.x, p1.x, p2.x, p3.x, t),
		eval_cubic_1d(p0.y, p1.y, p2.y, p3.y, t)};
}

void collect_cubic_1d_extrema_t(
	double p0,
	double p1,
	double p2,
	double p3,
	std::vector<double>& out_t)
{
	const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
	const double b = p0 - 2.0 * p1 + p2;
	const double c = p1 - p0;

	constexpr double kEps = 1e-12;
	if (std::abs(a) <= kEps) {
		if (std::abs(b) > kEps) {
			const double t = -c / (2.0 * b);
			if (t > 0.0 && t < 1.0) {
				out_t.push_back(t);
			}
		}
		return;
	}

	const double disc = b * b - a * c;
	if (disc < 0.0) {
		return;
	}
	const double sqrt_disc = std::sqrt(disc);
	const double t1 = (-b - sqrt_disc) / a;
	const double t2 = (-b + sqrt_disc) / a;
	for (double t : {t1, t2}) {
		if (t > 0.0 && t < 1.0) {
			out_t.push_back(t);
		}
	}
}

void expand_cubic_1d_bounds(
	double p0,
	double p1,
	double p2,
	double p3,
	double& inout_min,
	double& inout_max) noexcept
{
	inout_min = std::min({inout_min, p0, p3});
	inout_max = std::max({inout_max, p0, p3});

	const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
	const double b = p0 - 2.0 * p1 + p2;
	const double c = p1 - p0;

	constexpr double kEps = 1e-12;
	if (std::abs(a) <= kEps) {
		if (std::abs(b) > kEps) {
			const double t = -c / (2.0 * b);
			if (t > 0.0 && t < 1.0) {
				const double val = eval_cubic_1d(p0, p1, p2, p3, t);
				inout_min = std::min(inout_min, val);
				inout_max = std::max(inout_max, val);
			}
		}
		return;
	}

	const double disc = b * b - a * c;
	if (disc < 0.0) {
		return;
	}
	const double sqrt_disc = std::sqrt(disc);
	const double t1 = (-b - sqrt_disc) / a;
	const double t2 = (-b + sqrt_disc) / a;
	for (double t : {t1, t2}) {
		if (t > 0.0 && t < 1.0) {
			const double val = eval_cubic_1d(p0, p1, p2, p3, t);
			inout_min = std::min(inout_min, val);
			inout_max = std::max(inout_max, val);
		}
	}
}

void include_point_in_bounds(
	PdfPoint pt,
	double& x_min,
	double& y_min,
	double& x_max,
	double& y_max,
	bool& has_any) noexcept
{
	if (!is_finite_point(pt)) {
		return;
	}
	if (!has_any) {
		x_min = x_max = pt.x;
		y_min = y_max = pt.y;
		has_any = true;
	} else {
		x_min = std::min(x_min, pt.x);
		y_min = std::min(y_min, pt.y);
		x_max = std::max(x_max, pt.x);
		y_max = std::max(y_max, pt.y);
	}
}

struct UserSubpath {
	std::vector<PdfPoint> points;
	bool closed{false};
	bool has_draw_verb{false};
};

// Extracts subpaths in user space, flattening cubic Beziers into piecewise
// linear segments that include the exact analytic extrema of `ctm(B(t))`.
std::vector<UserSubpath> extract_user_subpaths(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points,
	const PdfMatrix& ctm)
{
	std::vector<UserSubpath> subpaths;
	UserSubpath cur_sub;

	auto flush_subpath = [&]() {
		if (!cur_sub.points.empty()) {
			subpaths.push_back(std::move(cur_sub));
			cur_sub = UserSubpath{};
		}
	};

	size_t pt_idx = 0u;
	for (const PathVerb verb : verbs) {
		switch (verb) {
		case PathVerb::MoveTo: {
			flush_subpath();
			cur_sub.points.push_back(points[pt_idx++]);
			cur_sub.closed = false;
			cur_sub.has_draw_verb = false;
			break;
		}
		case PathVerb::LineTo: {
			cur_sub.points.push_back(points[pt_idx++]);
			cur_sub.has_draw_verb = true;
			break;
		}
		case PathVerb::CubicBezierTo: {
			const PdfPoint p0 = cur_sub.points.back();
			const PdfPoint cp1 = points[pt_idx++];
			const PdfPoint cp2 = points[pt_idx++];
			const PdfPoint p3 = points[pt_idx++];

			std::vector<double> ts;
			constexpr size_t kSteps = 32u;
			ts.reserve(kSteps + 4u);
			for (size_t s = 1u; s <= kSteps; ++s) {
				ts.push_back(static_cast<double>(s) / static_cast<double>(kSteps));
			}
			const PdfPoint q0 = transform_point(ctm, p0);
			const PdfPoint q1 = transform_point(ctm, cp1);
			const PdfPoint q2 = transform_point(ctm, cp2);
			const PdfPoint q3 = transform_point(ctm, p3);
			collect_cubic_1d_extrema_t(q0.x, q1.x, q2.x, q3.x, ts);
			collect_cubic_1d_extrema_t(q0.y, q1.y, q2.y, q3.y, ts);
			std::sort(ts.begin(), ts.end());

			double prev_t = 0.0;
			for (double t : ts) {
				if (t - prev_t > 1e-9) {
					cur_sub.points.push_back(eval_cubic_2d(p0, cp1, cp2, p3, t));
					prev_t = t;
				}
			}
			cur_sub.points.back() = p3;
			cur_sub.has_draw_verb = true;
			break;
		}
		case PathVerb::Rect: {
			flush_subpath();
			const PdfPoint origin = points[pt_idx++];
			const PdfPoint size = points[pt_idx++];
			UserSubpath rect_sub;
			rect_sub.closed = true;
			rect_sub.has_draw_verb = true;
			rect_sub.points.push_back(PdfPoint{origin.x, origin.y});
			rect_sub.points.push_back(PdfPoint{origin.x + size.x, origin.y});
			rect_sub.points.push_back(PdfPoint{origin.x + size.x, origin.y + size.y});
			rect_sub.points.push_back(PdfPoint{origin.x, origin.y + size.y});
			subpaths.push_back(std::move(rect_sub));

			cur_sub.points.push_back(origin);
			cur_sub.closed = false;
			cur_sub.has_draw_verb = false;
			break;
		}
		case PathVerb::CloseSubpath: {
			if (!cur_sub.points.empty()) {
				cur_sub.closed = true;
				cur_sub.has_draw_verb = true;
				const PdfPoint start = cur_sub.points.front();
				flush_subpath();
				cur_sub.points.push_back(start);
			}
			break;
		}
		}
	}
	flush_subpath();
	return subpaths;
}

} // namespace

int64_t debug_live_display_lists() noexcept
{
	return g_live_display_lists.load(std::memory_order_relaxed);
}

PdfRect transform_rect(
	const PdfMatrix& m,
	const PdfRect& rect) noexcept
{
	const std::array<PdfPoint, 4> corners = {
		transform_point(m, PdfPoint{rect.x_min, rect.y_min}),
		transform_point(m, PdfPoint{rect.x_max, rect.y_min}),
		transform_point(m, PdfPoint{rect.x_max, rect.y_max}),
		transform_point(m, PdfPoint{rect.x_min, rect.y_max}),
	};

	double x_min = corners[0].x;
	double y_min = corners[0].y;
	double x_max = corners[0].x;
	double y_max = corners[0].y;
	for (size_t i = 1u; i < corners.size(); ++i) {
		x_min = std::min(x_min, corners[i].x);
		y_min = std::min(y_min, corners[i].y);
		x_max = std::max(x_max, corners[i].x);
		y_max = std::max(y_max, corners[i].y);
	}
	return PdfRect{x_min, y_min, x_max, y_max};
}

PdfRect union_rect(
	const PdfRect& a,
	const PdfRect& b) noexcept
{
	return PdfRect{
		std::min(a.x_min, b.x_min),
		std::min(a.y_min, b.y_min),
		std::max(a.x_max, b.x_max),
		std::max(a.y_max, b.y_max)};
}

std::optional<PdfRect> intersect_rect(
	const PdfRect& a,
	const PdfRect& b) noexcept
{
	const double ix_min = std::max(a.x_min, b.x_min);
	const double iy_min = std::max(a.y_min, b.y_min);
	const double ix_max = std::min(a.x_max, b.x_max);
	const double iy_max = std::min(a.y_max, b.y_max);
	if (ix_min > ix_max || iy_min > iy_max) {
		return std::nullopt;
	}
	return PdfRect{ix_min, iy_min, ix_max, iy_max};
}

std::optional<PdfRect> compute_recorded_path_bounds(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points,
	const PdfMatrix& transform) noexcept
{
	if (!is_finite_matrix(transform) || !validate_path_slice(verbs, points)) {
		return std::nullopt;
	}

	double x_min = 0.0;
	double y_min = 0.0;
	double x_max = 0.0;
	double y_max = 0.0;
	bool has_any = false;

	PdfPoint cur{0.0, 0.0};
	PdfPoint sub_start{0.0, 0.0};
	bool has_cur = false;

	size_t pt_idx = 0u;
	for (const PathVerb verb : verbs) {
		switch (verb) {
		case PathVerb::MoveTo: {
			cur = points[pt_idx++];
			sub_start = cur;
			has_cur = true;
			break;
		}
		case PathVerb::LineTo: {
			const PdfPoint p1 = points[pt_idx++];
			if (has_cur) {
				include_point_in_bounds(
					transform_point(transform, cur),
					x_min,
					y_min,
					x_max,
					y_max,
					has_any);
			}
			include_point_in_bounds(
				transform_point(transform, p1),
				x_min,
				y_min,
				x_max,
				y_max,
				has_any);
			cur = p1;
			has_cur = true;
			break;
		}
		case PathVerb::CubicBezierTo: {
			const PdfPoint cp1 = points[pt_idx++];
			const PdfPoint cp2 = points[pt_idx++];
			const PdfPoint end = points[pt_idx++];
			const PdfPoint p0_raw = has_cur ? cur : cp1;

			const PdfPoint p0 = transform_point(transform, p0_raw);
			const PdfPoint p1 = transform_point(transform, cp1);
			const PdfPoint p2 = transform_point(transform, cp2);
			const PdfPoint p3 = transform_point(transform, end);
			if (is_finite_point(p0) && is_finite_point(p1) &&
				is_finite_point(p2) && is_finite_point(p3)) {
				include_point_in_bounds(p0, x_min, y_min, x_max, y_max, has_any);
				include_point_in_bounds(p3, x_min, y_min, x_max, y_max, has_any);
				expand_cubic_1d_bounds(p0.x, p1.x, p2.x, p3.x, x_min, x_max);
				expand_cubic_1d_bounds(p0.y, p1.y, p2.y, p3.y, y_min, y_max);
			}
			cur = end;
			has_cur = true;
			break;
		}
		case PathVerb::Rect: {
			const PdfPoint origin = points[pt_idx++];
			const PdfPoint size = points[pt_idx++];
			const std::array<PdfPoint, 4> corners = {
				PdfPoint{origin.x, origin.y},
				PdfPoint{origin.x + size.x, origin.y},
				PdfPoint{origin.x + size.x, origin.y + size.y},
				PdfPoint{origin.x, origin.y + size.y},
			};
			for (const PdfPoint& c : corners) {
				include_point_in_bounds(
					transform_point(transform, c),
					x_min,
					y_min,
					x_max,
					y_max,
					has_any);
			}
			cur = origin;
			sub_start = origin;
			has_cur = true;
			break;
		}
		case PathVerb::CloseSubpath: {
			if (has_cur) {
				include_point_in_bounds(
					transform_point(transform, cur),
					x_min,
					y_min,
					x_max,
					y_max,
					has_any);
				include_point_in_bounds(
					transform_point(transform, sub_start),
					x_min,
					y_min,
					x_max,
					y_max,
					has_any);
				cur = sub_start;
			}
			break;
		}
		}
	}

	if (!has_any) {
		return std::nullopt;
	}
	return PdfRect{x_min, y_min, x_max, y_max};
}

void PathBuilder::move_to(double x, double y)
{
	verbs_.push_back(PathVerb::MoveTo);
	points_.emplace_back(x, y);
}

void PathBuilder::line_to(double x, double y)
{
	verbs_.push_back(PathVerb::LineTo);
	points_.emplace_back(x, y);
}

void PathBuilder::cubic_bezier_to(
	PdfPoint cp1,
	PdfPoint cp2,
	PdfPoint end)
{
	verbs_.push_back(PathVerb::CubicBezierTo);
	points_.push_back(cp1);
	points_.push_back(cp2);
	points_.push_back(end);
}

void PathBuilder::rect(double x, double y, double width, double height)
{
	verbs_.push_back(PathVerb::Rect);
	points_.emplace_back(x, y);
	points_.emplace_back(width, height);
}

void PathBuilder::close_subpath()
{
	verbs_.push_back(PathVerb::CloseSubpath);
}

void PathBuilder::clear() noexcept
{
	verbs_.clear();
	points_.clear();
}

std::optional<PdfRect> PathBuilder::compute_bounds(
	const PdfMatrix& transform) const noexcept
{
	return compute_recorded_path_bounds(verbs_, points_, transform);
}

PdfResult<std::shared_ptr<const DecodedBitmap>> DecodedBitmap::create(
	uint32_t width,
	uint32_t height,
	uint32_t stride,
	std::vector<uint8_t> bgra_pixels,
	bool is_monochrome_mask) noexcept
{
	if (width == 0u || height == 0u ||
		width > kMaxBitmapDimension || height > kMaxBitmapDimension) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DecodedBitmap dimensions must be in 1..65536");
	}
	const uint64_t min_stride = static_cast<uint64_t>(width) * 4u;
	if (static_cast<uint64_t>(stride) < min_stride) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DecodedBitmap stride is smaller than width * 4");
	}
	const uint64_t min_bytes =
		static_cast<uint64_t>(stride) * static_cast<uint64_t>(height - 1u) + min_stride;
	if (min_bytes > kMaxBitmapBytes ||
		bgra_pixels.size() < static_cast<size_t>(min_bytes) ||
		bgra_pixels.size() > static_cast<size_t>(kMaxBitmapBytes)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DecodedBitmap pixel buffer size does not match stride * height");
	}

	bool has_non_transparent = false;
	for (uint32_t y = 0u; y < height && !has_non_transparent; ++y) {
		const uint8_t* row = bgra_pixels.data() + static_cast<size_t>(y) * stride;
		for (uint32_t x = 0u; x < width; ++x) {
			if (row[static_cast<size_t>(x) * 4u + 3u] != 0u) {
				has_non_transparent = true;
				break;
			}
		}
	}

	try {
		return std::shared_ptr<const DecodedBitmap>(new DecodedBitmap(
			width,
			height,
			stride,
			std::move(bgra_pixels),
			is_monochrome_mask,
			has_non_transparent));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory allocating DecodedBitmap");
	}
}

DisplayList::DisplayList(
	PdfRect page_box_pt,
	PdfRect content_bounds_pt,
	bool has_content_bounds,
	std::vector<DisplayCommand>&& commands,
	std::vector<RecordedPath>&& paths,
	std::vector<PathVerb>&& path_verbs,
	std::vector<PdfPoint>&& path_points,
	std::vector<PositionedGlyph>&& glyphs,
	std::vector<float>&& dash_values) noexcept
	: page_box_pt_(page_box_pt)
	, content_bounds_pt_(content_bounds_pt)
	, has_content_bounds_(has_content_bounds)
	, commands_(std::move(commands))
	, paths_(std::move(paths))
	, path_verbs_(std::move(path_verbs))
	, path_points_(std::move(path_points))
	, glyphs_(std::move(glyphs))
	, dash_values_(std::move(dash_values))
{
	g_live_display_lists.fetch_add(1, std::memory_order_relaxed);
}

DisplayList::~DisplayList() noexcept
{
	g_live_display_lists.fetch_sub(1, std::memory_order_relaxed);
}

DisplayListBuilder::DisplayListBuilder(
	PdfRect page_box_pt,
	DisplayListLimits limits) noexcept
	: page_box_pt_(page_box_pt)
	, limits_(limits)
{
	if (!is_valid_rect(page_box_pt_)) {
		sticky_error_ = PdfError{
			ErrorCode::DamagedFormat,
			"DisplayListBuilder page_box_pt must be finite and non-inverted"};
		return;
	}
	if (!page_box_pt_.is_empty()) {
		try {
			ClipRegion page_clip;
			page_clip.fill_rule = FillRule::NonZero;
			page_clip.bbox = page_box_pt_;
			page_clip.has_bbox = true;
			const PdfPoint p0{page_box_pt_.x_min, page_box_pt_.y_min};
			const PdfPoint p1{page_box_pt_.x_max, page_box_pt_.y_min};
			const PdfPoint p2{page_box_pt_.x_max, page_box_pt_.y_max};
			const PdfPoint p3{page_box_pt_.x_min, page_box_pt_.y_max};
			page_clip.edges.push_back(ClipEdge{p0, p1});
			page_clip.edges.push_back(ClipEdge{p1, p2});
			page_clip.edges.push_back(ClipEdge{p2, p3});
			page_clip.edges.push_back(ClipEdge{p3, p0});
			current_state_.clips.push_back(std::move(page_clip));
		} catch (const std::bad_alloc&) {
			sticky_error_ = PdfError{
				ErrorCode::OutOfMemory,
				"Out of memory initializing DisplayListBuilder page clip"};
		}
	}
}

size_t DisplayListBuilder::current_pool_bytes() const noexcept
{
	return commands_.size() * sizeof(DisplayCommand) +
		paths_.size() * sizeof(RecordedPath) +
		path_verbs_.size() * sizeof(PathVerb) +
		path_points_.size() * sizeof(PdfPoint) +
		glyphs_.size() * sizeof(PositionedGlyph) +
		dash_values_.size() * sizeof(float);
}

PdfResult<void> DisplayListBuilder::check_can_mutate() const noexcept
{
	if (finished_) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"DisplayListBuilder has already been finished");
	}
	if (sticky_error_.has_value()) {
		return *sticky_error_;
	}
	return {};
}

PdfResult<void> DisplayListBuilder::check_additional_bytes(size_t extra_bytes) const noexcept
{
	const size_t used = current_pool_bytes();
	if (extra_bytes > limits_.max_total_bytes ||
		used > limits_.max_total_bytes - extra_bytes) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder total byte budget exceeded");
	}
	return {};
}

PdfResult<void> DisplayListBuilder::reserve_commands(size_t count) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (count > limits_.max_commands ||
		count > limits_.max_total_bytes / sizeof(DisplayCommand)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::reserve_commands exceeds command or byte budget");
	}
	try {
		commands_.reserve(count);
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::reserve_commands");
	}
}

PdfResult<uint32_t> DisplayListBuilder::add_path(const PathBuilder& path) noexcept
{
	return add_path(path.verbs(), path.points());
}

PdfResult<uint32_t> DisplayListBuilder::add_path(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok.error();
	}
	if (!validate_path_slice(verbs, points)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::add_path rejected malformed or non-finite path geometry");
	}

	constexpr size_t kMaxU32 = std::numeric_limits<uint32_t>::max();
	if (paths_.size() >= limits_.max_paths || paths_.size() >= kMaxU32 ||
		verbs.size() > limits_.max_path_verbs ||
		path_verbs_.size() > limits_.max_path_verbs - verbs.size() ||
		verbs.size() > kMaxU32 || path_verbs_.size() > kMaxU32 - verbs.size() ||
		points.size() > limits_.max_path_points ||
		path_points_.size() > limits_.max_path_points - points.size() ||
		points.size() > kMaxU32 || path_points_.size() > kMaxU32 - points.size()) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::add_path exceeds path, verb, or point limits");
	}

	if (verbs.size() > std::numeric_limits<size_t>::max() / sizeof(PathVerb) ||
		points.size() > std::numeric_limits<size_t>::max() / sizeof(PdfPoint)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::add_path byte size overflow");
	}
	const size_t verb_bytes = verbs.size() * sizeof(PathVerb);
	const size_t point_bytes = points.size() * sizeof(PdfPoint);
	if (verb_bytes > std::numeric_limits<size_t>::max() - point_bytes ||
		verb_bytes + point_bytes > std::numeric_limits<size_t>::max() - sizeof(RecordedPath)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::add_path byte size overflow");
	}
	if (auto ok = check_additional_bytes(verb_bytes + point_bytes + sizeof(RecordedPath));
		!ok.has_value()) {
		return ok.error();
	}

	const uint32_t verb_offset = static_cast<uint32_t>(path_verbs_.size());
	const uint32_t verb_count = static_cast<uint32_t>(verbs.size());
	const uint32_t point_offset = static_cast<uint32_t>(path_points_.size());
	const uint32_t point_count = static_cast<uint32_t>(points.size());

	RecordedPath rec{};
	rec.verb_offset = verb_offset;
	rec.verb_count = verb_count;
	rec.point_offset = point_offset;
	rec.point_count = point_count;
	if (const auto bounds = compute_recorded_path_bounds(verbs, points)) {
		rec.bounds_pt = *bounds;
		rec.has_bounds = true;
	}

	try {
		path_verbs_.reserve(path_verbs_.size() + verbs.size());
		path_points_.reserve(path_points_.size() + points.size());
		paths_.reserve(paths_.size() + 1u);

		path_verbs_.insert(path_verbs_.end(), verbs.begin(), verbs.end());
		path_points_.insert(path_points_.end(), points.begin(), points.end());
		const uint32_t index = static_cast<uint32_t>(paths_.size());
		paths_.push_back(rec);
		return index;
	} catch (const std::bad_alloc&) {
		path_verbs_.resize(verb_offset);
		path_points_.resize(point_offset);
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::add_path");
	}
}

PdfResult<void> DisplayListBuilder::save_state() noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (state_stack_.size() >= limits_.max_save_depth) {
		sticky_error_ = PdfError{
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::save_state exceeded max_save_depth"};
		return *sticky_error_;
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	try {
		commands_.reserve(commands_.size() + 1u);
		state_stack_.push_back(current_state_);
		commands_.emplace_back(SaveStateCommand{});
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::save_state");
	}
}

PdfResult<void> DisplayListBuilder::restore_state() noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (state_stack_.empty()) {
		sticky_error_ = PdfError{
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::restore_state underflow"};
		return *sticky_error_;
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	try {
		commands_.reserve(commands_.size() + 1u);
		current_state_ = std::move(state_stack_.back());
		state_stack_.pop_back();
		commands_.emplace_back(RestoreStateCommand{});
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::restore_state");
	}
}

PdfResult<void> DisplayListBuilder::concat_matrix(const PdfMatrix& matrix) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (!is_finite_matrix(matrix)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::concat_matrix requires finite matrix coefficients");
	}
	const PdfMatrix combined = recto::pdf::concat_matrix(matrix, current_state_.ctm);
	if (!is_finite_matrix(combined)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::concat_matrix produced non-finite CTM");
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	try {
		commands_.emplace_back(ConcatMatrixCommand{matrix});
		current_state_.ctm = combined;
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::concat_matrix");
	}
}

PdfResult<void> DisplayListBuilder::set_stroke_style(
	float line_width,
	LineCap line_cap,
	LineJoin line_join,
	float miter_limit,
	std::span<const float> dash_array,
	float dash_phase) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (!std::isfinite(line_width) || line_width < 0.0f ||
		!is_valid_line_cap(line_cap) ||
		!is_valid_line_join(line_join) ||
		!std::isfinite(miter_limit) || miter_limit < 1.0f ||
		!std::isfinite(dash_phase)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::set_stroke_style received invalid stroke parameters");
	}

	double dash_sum = 0.0;
	for (const float d : dash_array) {
		if (!std::isfinite(d) || d < 0.0f) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::set_stroke_style dash values must be finite and non-negative");
		}
		dash_sum += static_cast<double>(d);
	}
	if (!dash_array.empty() && (!(dash_sum > 0.0) || !std::isfinite(dash_sum))) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::set_stroke_style dash pattern cannot be all zeros");
	}

	constexpr size_t kMaxU32 = std::numeric_limits<uint32_t>::max();
	if (commands_.size() >= limits_.max_commands ||
		dash_array.size() > limits_.max_dash_values ||
		dash_values_.size() > limits_.max_dash_values - dash_array.size() ||
		dash_array.size() > kMaxU32 ||
		dash_values_.size() > kMaxU32 - dash_array.size()) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::set_stroke_style exceeds command or dash limits");
	}

	const size_t dash_bytes = dash_array.size() * sizeof(float);
	if (auto ok = check_additional_bytes(dash_bytes + sizeof(DisplayCommand));
		!ok.has_value()) {
		return ok;
	}

	const uint32_t dash_offset = static_cast<uint32_t>(dash_values_.size());
	const uint32_t dash_count = static_cast<uint32_t>(dash_array.size());

	SetStrokeStyleCommand cmd{};
	cmd.line_width = line_width;
	cmd.line_cap = line_cap;
	cmd.line_join = line_join;
	cmd.miter_limit = miter_limit;
	cmd.dash_offset = dash_offset;
	cmd.dash_count = dash_count;
	cmd.dash_phase = dash_phase;

	try {
		dash_values_.reserve(dash_values_.size() + dash_array.size());
		commands_.reserve(commands_.size() + 1u);
		dash_values_.insert(dash_values_.end(), dash_array.begin(), dash_array.end());
		commands_.emplace_back(cmd);
		current_state_.line_width = line_width;
		current_state_.line_cap = line_cap;
		current_state_.line_join = line_join;
		current_state_.miter_limit = miter_limit;
		return {};
	} catch (const std::bad_alloc&) {
		dash_values_.resize(dash_offset);
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::set_stroke_style");
	}
}

PdfResult<void> DisplayListBuilder::set_alpha_and_blend(
	float stroke_alpha,
	float fill_alpha,
	BlendMode blend_mode) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (!std::isfinite(stroke_alpha) || stroke_alpha < 0.0f || stroke_alpha > 1.0f ||
		!std::isfinite(fill_alpha) || fill_alpha < 0.0f || fill_alpha > 1.0f ||
		!is_valid_blend_mode(blend_mode)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::set_alpha_and_blend received invalid alpha or blend mode");
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	SetAlphaAndBlendCommand cmd{};
	cmd.stroke_alpha = stroke_alpha;
	cmd.fill_alpha = fill_alpha;
	cmd.blend_mode = blend_mode;

	try {
		commands_.emplace_back(cmd);
		current_state_.stroke_alpha = stroke_alpha;
		current_state_.fill_alpha = fill_alpha;
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::set_alpha_and_blend");
	}
}

void DisplayListBuilder::include_raw_bounds(const PdfRect& bounds) noexcept
{
	if (!is_valid_rect(bounds)) {
		return;
	}
	if (!has_content_bounds_) {
		content_bounds_pt_ = bounds;
		has_content_bounds_ = true;
	} else {
		content_bounds_pt_ = union_rect(content_bounds_pt_, bounds);
	}
}

namespace {

template <typename EdgeT>
[[nodiscard]] std::optional<PdfRect> compute_edges_bbox(std::span<const EdgeT> edges) noexcept
{
	double x_min = 0.0;
	double y_min = 0.0;
	double x_max = 0.0;
	double y_max = 0.0;
	bool has_any = false;
	for (const EdgeT& e : edges) {
		include_point_in_bounds(e.p0, x_min, y_min, x_max, y_max, has_any);
		include_point_in_bounds(e.p1, x_min, y_min, x_max, y_max, has_any);
	}
	if (!has_any) {
		return std::nullopt;
	}
	return PdfRect{x_min, y_min, x_max, y_max};
}

template <typename EdgeT>
[[nodiscard]] bool point_in_edges(
	PdfPoint pt,
	std::span<const EdgeT> edges,
	FillRule rule) noexcept
{
	int winding = 0;
	for (const EdgeT& e : edges) {
		const PdfPoint a = e.p0;
		const PdfPoint b = e.p1;
		// Check if `pt` lies directly on the segment `a -> b`:
		const double min_x = std::min(a.x, b.x) - kGeomEps;
		const double max_x = std::max(a.x, b.x) + kGeomEps;
		const double min_y = std::min(a.y, b.y) - kGeomEps;
		const double max_y = std::max(a.y, b.y) + kGeomEps;
		if (pt.x >= min_x && pt.x <= max_x && pt.y >= min_y && pt.y <= max_y) {
			const double cross = (b.x - a.x) * (pt.y - a.y) - (b.y - a.y) * (pt.x - a.x);
			const double len = std::hypot(b.x - a.x, b.y - a.y);
			if (len <= kGeomEps || std::abs(cross) <= kGeomEps * len) {
				return true;
			}
		}

		if (a.y <= pt.y) {
			if (b.y > pt.y) {
				const double is_left =
					(b.x - a.x) * (pt.y - a.y) - (pt.x - a.x) * (b.y - a.y);
				if (is_left > 0.0) {
					++winding;
				}
			}
		} else {
			if (b.y <= pt.y) {
				const double is_left =
					(b.x - a.x) * (pt.y - a.y) - (pt.x - a.x) * (b.y - a.y);
				if (is_left < 0.0) {
					--winding;
				}
			}
		}
	}

	if (rule == FillRule::EvenOdd) {
		return (winding & 1) != 0;
	}
	return winding != 0;
}

void add_segment_intersection_y(
	PdfPoint a0,
	PdfPoint a1,
	PdfPoint b0,
	PdfPoint b1,
	std::vector<double>& y_breaks)
{
	const double dax = a1.x - a0.x;
	const double day = a1.y - a0.y;
	const double dbx = b1.x - b0.x;
	const double dby = b1.y - b0.y;
	const double denom = dax * dby - day * dbx;
	if (std::abs(denom) <= 1e-12) {
		return;
	}
	const double dx = b0.x - a0.x;
	const double dy = b0.y - a0.y;
	const double t = (dx * dby - dy * dbx) / denom;
	const double u = (dx * day - dy * dax) / denom;
	if (t >= -1e-9 && t <= 1.0 + 1e-9 && u >= -1e-9 && u <= 1.0 + 1e-9) {
		const double iy = a0.y + std::clamp(t, 0.0, 1.0) * day;
		if (std::isfinite(iy)) {
			y_breaks.push_back(iy);
		}
	}
}

void add_segment_intersection_t(
	PdfPoint a0,
	PdfPoint a1,
	PdfPoint b0,
	PdfPoint b1,
	std::vector<double>& ts)
{
	const double dax = a1.x - a0.x;
	const double day = a1.y - a0.y;
	const double dbx = b1.x - b0.x;
	const double dby = b1.y - b0.y;
	const double denom = dax * dby - day * dbx;
	if (std::abs(denom) <= 1e-12) {
		return;
	}
	const double dx = b0.x - a0.x;
	const double dy = b0.y - a0.y;
	const double t = (dx * dby - dy * dbx) / denom;
	const double u = (dx * day - dy * dax) / denom;
	if (t >= 0.0 && t <= 1.0 && u >= -1e-9 && u <= 1.0 + 1e-9) {
		ts.push_back(t);
	}
}

} // namespace

void DisplayListBuilder::accumulate_clipped_region(
	std::span<const ClipEdge> shape_edges,
	FillRule shape_rule) noexcept
{
	const auto shape_bbox = compute_edges_bbox(shape_edges);
	if (!shape_bbox.has_value()) {
		return;
	}

	PdfRect window = *shape_bbox;
	for (const ClipRegion& clip : current_state_.clips) {
		if (!clip.has_bbox) {
			return;
		}
		const auto inter = intersect_rect(window, clip.bbox);
		if (!inter.has_value()) {
			return;
		}
		window = *inter;
	}

	try {
		std::vector<ClipEdge> all_edges;
		size_t total_edge_count = shape_edges.size();
		for (const ClipRegion& clip : current_state_.clips) {
			total_edge_count += clip.edges.size();
		}
		all_edges.reserve(total_edge_count);
		all_edges.insert(all_edges.end(), shape_edges.begin(), shape_edges.end());
		for (const ClipRegion& clip : current_state_.clips) {
			all_edges.insert(all_edges.end(), clip.edges.begin(), clip.edges.end());
		}

		std::vector<double> y_breaks;
		y_breaks.reserve(all_edges.size() * 2u + 16u);
		y_breaks.push_back(window.y_min);
		y_breaks.push_back(window.y_max);
		for (const ClipEdge& e : all_edges) {
			if (e.p0.y >= window.y_min - kGeomEps && e.p0.y <= window.y_max + kGeomEps) {
				y_breaks.push_back(std::clamp(e.p0.y, window.y_min, window.y_max));
			}
			if (e.p1.y >= window.y_min - kGeomEps && e.p1.y <= window.y_max + kGeomEps) {
				y_breaks.push_back(std::clamp(e.p1.y, window.y_min, window.y_max));
			}
		}

		for (size_t i = 0u; i < all_edges.size(); ++i) {
			for (size_t j = i + 1u; j < all_edges.size(); ++j) {
				add_segment_intersection_y(
					all_edges[i].p0,
					all_edges[i].p1,
					all_edges[j].p0,
					all_edges[j].p1,
					y_breaks);
			}
		}

		std::sort(y_breaks.begin(), y_breaks.end());

		struct SlabCrossing {
			double xm{0.0};
			double x0{0.0};
			double x1{0.0};
		};
		std::vector<SlabCrossing> crossings;

		for (size_t i = 0u; i + 1u < y_breaks.size(); ++i) {
			const double y0 = y_breaks[i];
			const double y1 = y_breaks[i + 1u];
			if (y1 - y0 <= kGeomEps) {
				continue;
			}
			if (y1 < window.y_min - kGeomEps || y0 > window.y_max + kGeomEps) {
				continue;
			}
			const double ym = 0.5 * (y0 + y1);

			crossings.clear();
			for (const ClipEdge& e : all_edges) {
				const double ey_min = std::min(e.p0.y, e.p1.y);
				const double ey_max = std::max(e.p0.y, e.p1.y);
				const double dy = e.p1.y - e.p0.y;
				if ( std::abs(dy) <= kGeomEps || ey_min > ym || ey_max < ym) {
					continue;
				}
				const double dx = e.p1.x - e.p0.x;
				const double xm = e.p0.x + (ym - e.p0.y) * dx / dy;
				const double x0 = e.p0.x + (y0 - e.p0.y) * dx / dy;
				const double x1 = e.p0.x + (y1 - e.p0.y) * dx / dy;
				crossings.push_back(SlabCrossing{xm, x0, x1});
			}

			std::sort(
				crossings.begin(),
				crossings.end(),
				[](const SlabCrossing& a, const SlabCrossing& b) noexcept {
					if (std::abs(a.xm - b.xm) > 1e-12) {
						return a.xm < b.xm;
					}
					return (a.x0 + a.x1) < (b.x0 + b.x1);
				});

			for (size_t k = 0u; k + 1u < crossings.size(); ++k) {
				const SlabCrossing& left = crossings[k];
				const SlabCrossing& right = crossings[k + 1u];
				if (right.xm - left.xm <= kGeomEps) {
					continue;
				}
				const PdfPoint test_pt{0.5 * (left.xm + right.xm), ym};
				if (!point_in_edges<ClipEdge>(test_pt, shape_edges, shape_rule)) {
					continue;
				}
				bool inside_all_clips = true;
				for (const ClipRegion& clip : current_state_.clips) {
					if (!point_in_edges<ClipEdge>(test_pt, clip.edges, clip.fill_rule)) {
						inside_all_clips = false;
						break;
					}
				}
				if (inside_all_clips) {
					const double tx_min = std::min({left.x0, left.x1, right.x0, right.x1});
					const double tx_max = std::max({left.x0, left.x1, right.x0, right.x1});
					include_raw_bounds(PdfRect{tx_min, y0, tx_max, y1});
				}
			}
		}
	} catch (const std::bad_alloc&) {
		// Fallback to bbox intersection only if scratch vector allocation fails.
		include_raw_bounds(window);
	}
}

void DisplayListBuilder::accumulate_clipped_segment(PdfPoint p0, PdfPoint p1) noexcept
{
	if (!is_finite_point(p0) || !is_finite_point(p1)) {
		return;
	}
	for (const ClipRegion& clip : current_state_.clips) {
		if (!clip.has_bbox) {
			return;
		}
	}

	try {
		std::vector<double> ts{0.0, 1.0};
		for (const ClipRegion& clip : current_state_.clips) {
			for (const ClipEdge& e : clip.edges) {
				add_segment_intersection_t(p0, p1, e.p0, e.p1, ts);
			}
		}
		std::sort(ts.begin(), ts.end());

		for (size_t i = 0u; i + 1u < ts.size(); ++i) {
			const double t0 = ts[i];
			const double t1 = ts[i + 1u];
			if (t1 - t0 <= 1e-12 && ts.size() > 2u) {
				continue;
			}
			const double tm = 0.5 * (t0 + t1);
			const PdfPoint mid{
				p0.x + tm * (p1.x - p0.x),
				p0.y + tm * (p1.y - p0.y)};
			bool inside_all = true;
			for (const ClipRegion& clip : current_state_.clips) {
				if (!point_in_edges<ClipEdge>(mid, clip.edges, clip.fill_rule)) {
					inside_all = false;
					break;
				}
			}
			if (inside_all) {
				const PdfPoint q0{
					p0.x + t0 * (p1.x - p0.x),
					p0.y + t0 * (p1.y - p0.y)};
				const PdfPoint q1{
					p0.x + t1 * (p1.x - p0.x),
					p0.y + t1 * (p1.y - p0.y)};
				include_raw_bounds(PdfRect{
					std::min(q0.x, q1.x),
					std::min(q0.y, q1.y),
					std::max(q0.x, q1.x),
					std::max(q0.y, q1.y)});
			}
		}
	} catch (const std::bad_alloc&) {
	}
}

void DisplayListBuilder::accumulate_clipped_rect(
	const PdfRect& local_rect,
	const PdfMatrix& to_page) noexcept
{
	if (!is_valid_rect(local_rect)) {
		return;
	}
	const std::array<PdfPoint, 4> corners = {
		transform_point(to_page, PdfPoint{local_rect.x_min, local_rect.y_min}),
		transform_point(to_page, PdfPoint{local_rect.x_max, local_rect.y_min}),
		transform_point(to_page, PdfPoint{local_rect.x_max, local_rect.y_max}),
		transform_point(to_page, PdfPoint{local_rect.x_min, local_rect.y_max}),
	};
	const std::array<ClipEdge, 4> edges = {
		ClipEdge{corners[0], corners[1]},
		ClipEdge{corners[1], corners[2]},
		ClipEdge{corners[2], corners[3]},
		ClipEdge{corners[3], corners[0]},
	};
	accumulate_clipped_region(edges, FillRule::NonZero);
}

PdfResult<void> DisplayListBuilder::push_clip_path(
	uint32_t path_index,
	FillRule fill_rule) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (path_index >= paths_.size() || !is_valid_fill_rule(fill_rule)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::push_clip_path received invalid path_index or fill_rule");
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	const RecordedPath& p = paths_[path_index];
	const auto verbs = std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	const auto points = std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);

	try {
		commands_.reserve(commands_.size() + 1u);

		ClipRegion region;
		region.fill_rule = fill_rule;
		const auto subpaths = extract_user_subpaths(verbs, points, current_state_.ctm);
		for (const UserSubpath& sp : subpaths) {
			if (sp.points.size() < 2u || !sp.has_draw_verb) {
				continue;
			}
			for (size_t i = 0u; i < sp.points.size(); ++i) {
				const PdfPoint a = transform_point(current_state_.ctm, sp.points[i]);
				const PdfPoint b = transform_point(
					current_state_.ctm,
					sp.points[(i + 1u) % sp.points.size()]);
				if (std::hypot(b.x - a.x, b.y - a.y) > kGeomEps) {
					region.edges.push_back(ClipEdge{a, b});
				}
			}
		}
		if (const auto bbox = compute_edges_bbox<ClipEdge>(region.edges)) {
			region.bbox = *bbox;
			region.has_bbox = true;
		}

		current_state_.clips.push_back(std::move(region));
		commands_.emplace_back(PushClipPathCommand{path_index, fill_rule});
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::push_clip_path");
	}
}

PdfResult<void> DisplayListBuilder::draw_path(
	uint32_t path_index,
	ColorRgba stroke_color,
	ColorRgba fill_color,
	bool stroke,
	bool fill,
	FillRule fill_rule) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (path_index >= paths_.size() ||
		!stroke_color.is_valid() ||
		!fill_color.is_valid() ||
		!is_valid_fill_rule(fill_rule)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_path received invalid path_index, colour, or fill_rule");
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	DrawPathCommand cmd{};
	cmd.path_index = path_index;
	cmd.stroke_color = stroke_color;
	cmd.fill_color = fill_color;
	cmd.stroke = stroke;
	cmd.fill = fill;
	cmd.fill_rule = fill_rule;

	try {
		commands_.emplace_back(cmd);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::draw_path");
	}

	const bool visible_fill =
		fill && (current_state_.fill_alpha > 0.0f) && (fill_color.a > 0.0f);
	const bool visible_stroke =
		stroke && (current_state_.stroke_alpha > 0.0f) && (stroke_color.a > 0.0f);
	if (!visible_fill && !visible_stroke) {
		return {};
	}

	const RecordedPath& p = paths_[path_index];
	const auto verbs = std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	const auto points = std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);

	try {
		const auto subpaths = extract_user_subpaths(verbs, points, current_state_.ctm);

		if (visible_fill) {
			std::vector<ClipEdge> fill_edges;
			for (const UserSubpath& sp : subpaths) {
				if (sp.points.size() < 2u || !sp.has_draw_verb) {
					continue;
				}
				for (size_t i = 0u; i < sp.points.size(); ++i) {
					const PdfPoint a = transform_point(current_state_.ctm, sp.points[i]);
					const PdfPoint b = transform_point(
						current_state_.ctm,
						sp.points[(i + 1u) % sp.points.size()]);
					if (std::hypot(b.x - a.x, b.y - a.y) > kGeomEps) {
						fill_edges.push_back(ClipEdge{a, b});
					}
				}
			}
			if (!fill_edges.empty()) {
				accumulate_clipped_region(fill_edges, fill_rule);
			}
		}

		if (visible_stroke) {
			const double w = static_cast<double>(current_state_.line_width);
			if (w <= 0.0) {
				// Hairline stroke (0 width in user space): clip 1D segments directly.
				for (const UserSubpath& sp : subpaths) {
					if (!sp.has_draw_verb || sp.points.empty()) {
						continue;
					}
					const size_t seg_count =
						sp.closed ? sp.points.size() : (sp.points.size() - 1u);
					for (size_t i = 0u; i < seg_count; ++i) {
						const PdfPoint a = transform_point(current_state_.ctm, sp.points[i]);
						const PdfPoint b = transform_point(
							current_state_.ctm,
							sp.points[(i + 1u) % sp.points.size()]);
						accumulate_clipped_segment(a, b);
					}
				}
			} else {
				const double r = 0.5 * w;
				const PdfMatrix& ctm = current_state_.ctm;

				auto emit_user_poly = [&](std::span<const PdfPoint> user_pts) {
					if (user_pts.size() < 3u) {
						return;
					}
					std::vector<ClipEdge> edges;
					edges.reserve(user_pts.size());
					for (size_t i = 0u; i < user_pts.size(); ++i) {
						const PdfPoint a = transform_point(ctm, user_pts[i]);
						const PdfPoint b = transform_point(
							ctm,
							user_pts[(i + 1u) % user_pts.size()]);
						if (std::hypot(b.x - a.x, b.y - a.y) > kGeomEps) {
							edges.push_back(ClipEdge{a, b});
						}
					}
					if (!edges.empty()) {
						accumulate_clipped_region(edges, FillRule::NonZero);
					}
				};

				// Critical user-space angles where `(a cos t + c sin t)` and
				// `(b cos t + d sin t)` attain their exact extrema under `ctm`.
				const double tx = std::atan2(ctm.c, ctm.a);
				const double ty = std::atan2(ctm.d, ctm.b);
				constexpr double kPi = std::numbers::pi;
				const std::array<double, 4> crit_angles = {
					tx,
					tx + kPi,
					ty,
					ty + kPi,
				};

				auto emit_user_disk = [&](PdfPoint center) {
					std::vector<double> angles;
					constexpr size_t kCircleSteps = 32u;
					angles.reserve(kCircleSteps + 4u);
					for (size_t s = 0u; s < kCircleSteps; ++s) {
						angles.push_back(
							(2.0 * kPi * static_cast<double>(s)) /
							static_cast<double>(kCircleSteps));
					}
					for (double ca : crit_angles) {
						double norm = std::fmod(ca, 2.0 * kPi);
						if (norm < 0.0) {
							norm += 2.0 * kPi;
						}
						angles.push_back(norm);
					}
					std::sort(angles.begin(), angles.end());

					std::vector<PdfPoint> poly;
					poly.reserve(angles.size());
					for (size_t i = 0u; i < angles.size(); ++i) {
						if (i > 0u && angles[i] - angles[i - 1u] <= 1e-9) {
							continue;
						}
						poly.emplace_back(
							center.x + r * std::cos(angles[i]),
							center.y + r * std::sin(angles[i]));
					}
					emit_user_poly(poly);
				};

				// Emits a circular fan centered at `center` covering all unit
				// vectors `u` that satisfy `u dot halfspace_dir >= -1e-9` and,
				// when `has_second_halfspace` is true, `u dot second_dir >= -1e-9`.
				auto emit_user_arc_fan = [&](
					PdfPoint center,
					PdfPoint start_unit,
					PdfPoint end_unit,
					PdfPoint halfspace_dir,
					bool has_second_halfspace = false,
					PdfPoint second_dir = PdfPoint{}) {
					std::vector<double> angles;
					constexpr size_t kArcSteps = 48u;
					angles.reserve(kArcSteps + 6u);
					angles.push_back(std::atan2(start_unit.y, start_unit.x));
					angles.push_back(std::atan2(end_unit.y, end_unit.x));
					for (size_t s = 0u; s < kArcSteps; ++s) {
						const double a =
							-kPi + (2.0 * kPi * static_cast<double>(s)) /
							static_cast<double>(kArcSteps);
						angles.push_back(a);
					}
					for (double ca : crit_angles) {
						angles.push_back( std::atan2(std::sin(ca), std::cos(ca)));
					}
					std::sort(angles.begin(), angles.end());

					std::vector<PdfPoint> poly;
					poly.reserve(angles.size() + 1u);
					poly.push_back(center);
					for (size_t i = 0u; i < angles.size(); ++i) {
						if (i > 0u && angles[i] - angles[i - 1u] <= 1e-9) {
							continue;
						}
						const double ux = std::cos(angles[i]);
						const double uy = std::sin(angles[i]);
						if (ux * halfspace_dir.x + uy * halfspace_dir.y < -1e-7) {
							continue;
						}
						if (has_second_halfspace &&
							(ux * second_dir.x + uy * second_dir.y < -1e-7)) {
							continue;
						}
						poly.emplace_back(center.x + r * ux, center.y + r * uy);
					}
					if (poly.size() >= 3u) {
						emit_user_poly(poly);
					}
				};

				struct StrokeSeg {
					PdfPoint a{};
					PdfPoint b{};
					PdfPoint t{};
					PdfPoint n{};
				};

				for (const UserSubpath& sp : subpaths) {
					if (!sp.has_draw_verb || sp.points.empty()) {
						continue;
					}
					std::vector<StrokeSeg> segs;
					const size_t raw_seg_count =
						sp.closed ? sp.points.size() : (sp.points.size() - 1u);
					for (size_t i = 0u; i < raw_seg_count; ++i) {
						const PdfPoint a = sp.points[i];
						const PdfPoint b = sp.points[(i + 1u) % sp.points.size()];
						const double len = std::hypot(b.x - a.x, b.y - a.y);
						if (len <= kGeomEps) {
							continue;
						}
						const PdfPoint t{(b.x - a.x) / len, (b.y - a.y) / len};
						const PdfPoint n{-t.y, t.x};
						segs.push_back(StrokeSeg{a, b, t, n});
					}

					if (segs.empty()) {
						if (current_state_.line_cap == LineCap::Round) {
							emit_user_disk(sp.points.front());
						}
						continue;
					}

					// 1. Segment shafts:
					for (const StrokeSeg& s : segs) {
						const std::array<PdfPoint, 4> quad = {
							PdfPoint{s.a.x + r * s.n.x, s.a.y + r * s.n.y},
							PdfPoint{s.b.x + r * s.n.x, s.b.y + r * s.n.y},
							PdfPoint{s.b.x - r * s.n.x, s.b.y - r * s.n.y},
							PdfPoint{s.a.x - r * s.n.x, s.a.y - r * s.n.y},
						};
						emit_user_poly(quad);
					}

					// 2. Open-endpoint caps:
					if (!sp.closed) {
						const auto emit_cap = [&](PdfPoint p, PdfPoint t_out, PdfPoint n) {
							if (current_state_.line_cap == LineCap::ProjectingSquare) {
								const std::array<PdfPoint, 4> cap_quad = {
									PdfPoint{p.x - r * n.x, p.y - r * n.y},
									PdfPoint{p.x + r * n.x, p.y + r * n.y},
									PdfPoint{
										p.x + r * n.x + r * t_out.x,
										p.y + r * n.y + r * t_out.y},
									PdfPoint{
										p.x - r * n.x + r * t_out.x,
										p.y - r * n.y + r * t_out.y},
								};
								emit_user_poly(cap_quad);
							} else if (current_state_.line_cap == LineCap::Round) {
								emit_user_arc_fan(
									p,
									PdfPoint{-n.x, -n.y},
									n,
									t_out);
							}
						};

						const StrokeSeg& first = segs.front();
						emit_cap(
							first.a,
							PdfPoint{-first.t.x, -first.t.y},
							PdfPoint{-first.n.x, -first.n.y});

						const StrokeSeg& last = segs.back();
						emit_cap(last.b, last.t, last.n);
					}

					// 3. Segment joins:
					const size_t join_count =
						sp.closed ? segs.size() : (segs.size() - 1u);
					for (size_t i = 0u; i < join_count; ++i) {
						const StrokeSeg& s_in = segs[i];
						const StrokeSeg& s_out = segs[(i + 1u) % segs.size()];
						if (std::hypot(s_in.b.x - s_out.a.x, s_in.b.y - s_out.a.y) > 1e-6) {
							continue;
						}
						const PdfPoint v = s_in.b;
						const double cross = s_in.t.x * s_out.t.y - s_in.t.y * s_out.t.x;
						const double dot = s_in.t.x * s_out.t.x + s_in.t.y * s_out.t.y;

						const PdfPoint u1 =
							(cross <= 0.0) ? s_in.n : PdfPoint{-s_in.n.x, -s_in.n.y};
						const PdfPoint u2 =
							(cross <= 0.0) ? s_out.n : PdfPoint{-s_out.n.x, -s_out.n.y};

						const std::array<PdfPoint, 3> bevel_tri = {
							v,
							PdfPoint{v.x + r * u1.x, v.y + r * u1.y},
							PdfPoint{v.x + r * u2.x, v.y + r * u2.y},
						};
						emit_user_poly(bevel_tri);

						if (current_state_.line_join == LineJoin::Round) {
							// Outer wedge between `u1` and `u2` has `u dot s_in.t >= 0`
							// and `u dot (-s_out.t) >= 0`:
							emit_user_arc_fan(
								v,
								u1,
								u2,
								s_in.t,
								true,
								PdfPoint{-s_out.t.x, -s_out.t.y});
						} else if (current_state_.line_join == LineJoin::Miter) {
							const double sin_half_alpha =
								std::sqrt(std::max(0.0, 0.5 * (1.0 + dot)));
							if (sin_half_alpha > 1e-9 &&
								(1.0 / sin_half_alpha) <=
									static_cast<double>(current_state_.miter_limit)) {
								const double denom = 1.0 + dot;
								const PdfPoint apex{
									v.x + r * (u1.x + u2.x) / denom,
									v.y + r * (u1.y + u2.y) / denom};
								const std::array<PdfPoint, 4> miter_quad = {
									v,
									PdfPoint{v.x + r * u1.x, v.y + r * u1.y},
									apex,
									PdfPoint{v.x + r * u2.x, v.y + r * u2.y},
								};
								emit_user_poly(miter_quad);
							}
						}
					}
				}
			}
		}
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory computing path bounds in DisplayListBuilder::draw_path");
	}

	return {};
}

PdfResult<void> DisplayListBuilder::draw_glyph_run(
	std::span<const PositionedGlyph> glyphs,
	double font_size,
	TextRenderMode render_mode,
	ColorRgba fill_color,
	ColorRgba stroke_color,
	const PdfMatrix& text_matrix,
	std::optional<PdfRect> local_bounds_pt,
	std::shared_ptr<const void> font_handle,
	std::shared_ptr<const DisplayList> type3_sub_list) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (!std::isfinite(font_size) ||
		!is_valid_text_render_mode(render_mode) ||
		!fill_color.is_valid() ||
		!stroke_color.is_valid() ||
		!is_finite_matrix(text_matrix)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run received invalid parameters");
	}
	if (local_bounds_pt.has_value() && !is_valid_rect(*local_bounds_pt)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run local_bounds_pt must be finite and non-inverted");
	}

	for (const PositionedGlyph& g : glyphs) {
		if (!is_finite_point(g.origin_pt) || !std::isfinite(g.advance_pt)) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::draw_glyph_run glyph origin and advance must be finite");
		}
		if (g.ink_bounds_pt.has_value()) {
			if (!is_valid_rect(*g.ink_bounds_pt) ||
				!std::isfinite(g.origin_pt.x + g.ink_bounds_pt->x_min) ||
				!std::isfinite(g.origin_pt.y + g.ink_bounds_pt->y_min) ||
				!std::isfinite(g.origin_pt.x + g.ink_bounds_pt->x_max) ||
				!std::isfinite(g.origin_pt.y + g.ink_bounds_pt->y_max)) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"DisplayListBuilder::draw_glyph_run glyph ink_bounds_pt must be finite and non-inverted");
			}
		}
	}

	const PdfMatrix combined = recto::pdf::concat_matrix(text_matrix, current_state_.ctm);
	if (!is_finite_matrix(combined)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run produced non-finite text-to-page matrix");
	}

	const bool mode_fills =
		render_mode == TextRenderMode::Fill ||
		render_mode == TextRenderMode::FillAndStroke ||
		render_mode == TextRenderMode::FillAndClip ||
		render_mode == TextRenderMode::FillStrokeAndClip;
	const bool mode_strokes =
		render_mode == TextRenderMode::Stroke ||
		render_mode == TextRenderMode::FillAndStroke ||
		render_mode == TextRenderMode::StrokeAndClip ||
		render_mode == TextRenderMode::FillStrokeAndClip;

	// Compute glyph run ink bounds from explicit `local_bounds_pt`,
	// `type3_sub_list->content_bounds_pt()`, or per-glyph `ink_bounds_pt`:
	std::optional<PdfRect> computed_local_bounds;
	if (local_bounds_pt.has_value()) {
		computed_local_bounds = *local_bounds_pt;
	} else if (type3_sub_list != nullptr) {
		if (type3_sub_list->has_content_bounds()) {
			const PdfRect& sub_box = type3_sub_list->content_bounds_pt();
			for (const PositionedGlyph& g : glyphs) {
				if (g.is_blank) {
					continue;
				}
				const PdfRect placed{
					g.origin_pt.x + sub_box.x_min,
					g.origin_pt.y + sub_box.y_min,
					g.origin_pt.x + sub_box.x_max,
					g.origin_pt.y + sub_box.y_max};
				if (!is_valid_rect(placed)) {
					return make_pdf_error(
						ErrorCode::DamagedFormat,
						"DisplayListBuilder::draw_glyph_run Type 3 glyph bounds overflowed");
				}
				computed_local_bounds = computed_local_bounds.has_value()
					? union_rect(*computed_local_bounds, placed)
					: placed;
			}
		}
	} else {
		for (const PositionedGlyph& g : glyphs) {
			if (g.is_blank) {
				continue;
			}
			if (!g.ink_bounds_pt.has_value()) {
				if (mode_fills || mode_strokes) {
					return make_pdf_error(
						ErrorCode::DamagedFormat,
						"DisplayListBuilder::draw_glyph_run visible glyph requires ink_bounds_pt, is_blank, local_bounds_pt, or type3_sub_list");
				}
				continue;
			}
			const PdfRect placed{
				g.origin_pt.x + g.ink_bounds_pt->x_min,
				g.origin_pt.y + g.ink_bounds_pt->y_min,
				g.origin_pt.x + g.ink_bounds_pt->x_max,
				g.origin_pt.y + g.ink_bounds_pt->y_max};
			computed_local_bounds = computed_local_bounds.has_value()
				? union_rect(*computed_local_bounds, placed)
				: placed;
		}
	}

	constexpr size_t kMaxU32 = std::numeric_limits<uint32_t>::max();
	if (commands_.size() >= limits_.max_commands ||
		glyphs.size() > limits_.max_glyphs ||
		glyphs_.size() > limits_.max_glyphs - glyphs.size() ||
		glyphs.size() > kMaxU32 ||
		glyphs_.size() > kMaxU32 - glyphs.size()) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run exceeds command or glyph limits");
	}

	if (glyphs.size() > std::numeric_limits<size_t>::max() / sizeof(PositionedGlyph)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run byte size overflow");
	}
	const size_t glyph_bytes = glyphs.size() * sizeof(PositionedGlyph);
	if (auto ok = check_additional_bytes(glyph_bytes + sizeof(DisplayCommand));
		!ok.has_value()) {
		return ok;
	}

	const uint32_t glyph_offset = static_cast<uint32_t>(glyphs_.size());
	const uint32_t glyph_count = static_cast<uint32_t>(glyphs.size());

	RecordedGlyphRun cmd{};
	cmd.glyph_offset = glyph_offset;
	cmd.glyph_count = glyph_count;
	cmd.font_size = font_size;
	cmd.render_mode = render_mode;
	cmd.fill_color = fill_color;
	cmd.stroke_color = stroke_color;
	cmd.text_matrix = text_matrix;
	cmd.ctm = current_state_.ctm;
	cmd.font_handle = std::move(font_handle);
	cmd.type3_sub_list = std::move(type3_sub_list);
	if (computed_local_bounds.has_value()) {
		cmd.bounds_pt = *computed_local_bounds;
		cmd.has_bounds = true;
	}

	try {
		glyphs_.reserve(glyphs_.size() + glyphs.size());
		commands_.reserve(commands_.size() + 1u);
		glyphs_.insert(glyphs_.end(), glyphs.begin(), glyphs.end());
		commands_.emplace_back(std::move(cmd));
	} catch (const std::bad_alloc&) {
		glyphs_.resize(glyph_offset);
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::draw_glyph_run");
	}

	const bool visible_fill =
		mode_fills && (current_state_.fill_alpha > 0.0f) && (fill_color.a > 0.0f);
	const bool visible_stroke =
		mode_strokes && (current_state_.stroke_alpha > 0.0f) && (stroke_color.a > 0.0f);
	if ((visible_fill || visible_stroke) && computed_local_bounds.has_value()) {
		PdfRect effective_local = *computed_local_bounds;
		if (visible_stroke && current_state_.line_width > 0.0f) {
			const double pad = 0.5 * static_cast<double>(current_state_.line_width);
			effective_local.x_min -= pad;
			effective_local.y_min -= pad;
			effective_local.x_max += pad;
			effective_local.y_max += pad;
		}
		accumulate_clipped_rect(effective_local, combined);
	}

	return {};
}

PdfResult<void> DisplayListBuilder::draw_image(
	std::shared_ptr<const DecodedBitmap> bitmap,
	const PdfMatrix& dest_transform,
	bool interpolate,
	std::shared_ptr<const DecodedBitmap> soft_mask,
	ColorRgba mask_tint_color) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (!bitmap || !is_finite_matrix(dest_transform) || !mask_tint_color.is_valid()) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_image requires non-null bitmap, finite transform, and valid tint colour");
	}
	const PdfMatrix image_to_page = recto::pdf::concat_matrix(dest_transform, current_state_.ctm);
	if (!is_finite_matrix(image_to_page)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_image produced non-finite image-to-page matrix");
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	const bool visible_image =
		(current_state_.fill_alpha > 0.0f) &&
		bitmap->has_non_transparent_pixels &&
		(!bitmap->is_monochrome_mask || mask_tint_color.a > 0.0f) &&
		(!soft_mask || soft_mask->has_non_transparent_pixels);

	DrawImageCommand cmd{};
	cmd.bitmap = std::move(bitmap);
	cmd.dest_transform = dest_transform;
	cmd.interpolate = interpolate;
	cmd.soft_mask = std::move(soft_mask);
	cmd.mask_tint_color = mask_tint_color;

	try {
		commands_.emplace_back(std::move(cmd));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::draw_image");
	}

	if (visible_image) {
		accumulate_clipped_rect(PdfRect{0.0, 0.0, 1.0, 1.0}, image_to_page);
	}
	return {};
}

PdfResult<std::shared_ptr<const DisplayList>> DisplayListBuilder::finish() noexcept
{
	if (finished_) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"DisplayListBuilder::finish called more than once");
	}
	if (sticky_error_.has_value()) {
		return *sticky_error_;
	}

	try {
		auto result = std::shared_ptr<const DisplayList>(new DisplayList(
			page_box_pt_,
			content_bounds_pt_,
			has_content_bounds_,
			std::move(commands_),
			std::move(paths_),
			std::move(path_verbs_),
			std::move(path_points_),
			std::move(glyphs_),
			std::move(dash_values_)));
		finished_ = true;
		return result;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::finish");
	}
}

} // namespace recto::pdf
