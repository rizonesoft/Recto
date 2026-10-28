// Thread-safe, immutable DisplayList command buffer and DisplayListBuilder
// (foundation plan sections 3.3, 4.1 item 4, 4.2, 4.4, 4.5, 4.8; D01 T02
// section 1).
#include "display_list.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <type_traits>
#include <utility>

namespace recto::pdf {

namespace {

std::atomic<int64_t> g_live_display_lists{0};

constexpr uint32_t kMaxBitmapDimension = 65536u;
constexpr uint64_t kMaxBitmapBytes = 256ull * 1024ull * 1024ull;

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

[[nodiscard]] bool checked_add_size(size_t a, size_t b, size_t& out) noexcept
{
	if (b > std::numeric_limits<size_t>::max() - a) {
		return false;
	}
	out = a + b;
	return true;
}

[[nodiscard]] bool checked_mul_size(size_t a, size_t b, size_t& out) noexcept
{
	if (a != 0u && b > std::numeric_limits<size_t>::max() / a) {
		return false;
	}
	out = a * b;
	return true;
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

// Validates that transforming every vertex in `(verbs, points)` by `ctm`
// produces strictly finite coordinates.
[[nodiscard]] bool validate_transformed_path_finite(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points,
	const PdfMatrix& ctm) noexcept
{
	if (!is_finite_matrix(ctm)) {
		return false;
	}
	size_t pt_idx = 0u;
	for (const PathVerb verb : verbs) {
		switch (verb) {
		case PathVerb::MoveTo:
		case PathVerb::LineTo: {
			const PdfPoint p = points[pt_idx++];
			if (!is_finite_point(transform_point(ctm, p))) {
				return false;
			}
			break;
		}
		case PathVerb::CubicBezierTo: {
			for (size_t k = 0u; k < 3u; ++k) {
				const PdfPoint p = points[pt_idx++];
				if (!is_finite_point(transform_point(ctm, p))) {
					return false;
				}
			}
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
				if (!is_finite_point(c) || !is_finite_point(transform_point(ctm, c))) {
					return false;
				}
			}
			break;
		}
		case PathVerb::CloseSubpath:
			break;
		}
	}
	return true;
}

[[nodiscard]] bool validate_transformed_rect_finite(
	const PdfRect& rect,
	const PdfMatrix& m) noexcept
{
	if (!is_valid_rect(rect) || !is_finite_matrix(m)) {
		return false;
	}
	const std::array<PdfPoint, 4> corners = {
		transform_point(m, PdfPoint{rect.x_min, rect.y_min}),
		transform_point(m, PdfPoint{rect.x_max, rect.y_min}),
		transform_point(m, PdfPoint{rect.x_max, rect.y_max}),
		transform_point(m, PdfPoint{rect.x_min, rect.y_max}),
	};
	for (const PdfPoint& c : corners) {
		if (!is_finite_point(c)) {
			return false;
		}
	}
	return true;
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

// The parameters where the cubic `p0..p3` has a 1D extremum: the roots of
// `a t^2 + 2 b t + c` (its derivative over 3), by the cancellation-free form
// `q = -(b + sign(b) sqrt(b^2 - a c))`, roots `q / a` and `c / q`, so a large
// coefficient never swallows a small root. Writes up to two finite roots,
// unfiltered, to `roots`; false when the arithmetic overflows.
[[nodiscard]] bool cubic_extremum_roots(
	double p0,
	double p1,
	double p2,
	double p3,
	std::array<double, 2>& roots,
	size_t& root_count) noexcept
{
	root_count = 0u;
	const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
	const double b = p0 - 2.0 * p1 + p2;
	const double c = p1 - p0;
	if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) {
		return false;
	}
	const auto add = [&](double t) noexcept {
		if (std::isfinite(t)) {
			roots[root_count++] = t;
		}
	};
	if (a == 0.0) {
		if (b != 0.0) {
			add(-c / (2.0 * b));
		}
		return true;
	}
	const double disc = b * b - a * c;
	if (!std::isfinite(disc)) {
		return false;
	}
	if (disc < 0.0) {
		return true;
	}
	const double q = -(b + std::copysign(std::sqrt(disc), b));
	if (q == 0.0) {
		add(0.0); // b == 0 and a c == 0: the double root t = 0
		return true;
	}
	add(q / a);
	add(c / q);
	return true;
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

	// A cubic lies inside the hull of its control points, so when the extremum
	// arithmetic overflows (finite coordinates near the double range) the hull
	// keeps the box a superset instead of falling back to the endpoints.
	const auto include_hull = [&]() noexcept {
		inout_min = std::min({inout_min, p1, p2});
		inout_max = std::max({inout_max, p1, p2});
	};
	const auto include_value = [&](double t) noexcept {
		const double val = eval_cubic_1d(p0, p1, p2, p3, t);
		if (!std::isfinite(val)) {
			include_hull();
			return;
		}
		inout_min = std::min(inout_min, val);
		inout_max = std::max(inout_max, val);
	};

	std::array<double, 2> roots{};
	size_t root_count = 0u;
	if (!cubic_extremum_roots(p0, p1, p2, p3, roots, root_count)) {
		include_hull();
		return;
	}
	for (size_t i = 0u; i < root_count; ++i) {
		if (roots[i] > 0.0 && roots[i] < 1.0) {
			include_value(roots[i]);
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

// Appends the parameters in `(0, 1)` where the cubic `p0..p3` has a 1D
// extremum (roots of its derivative) to `cuts`. False when the arithmetic
// overflows, which the caller answers with a looser box.
[[nodiscard]] bool collect_cubic_extremum_ts(
	double p0,
	double p1,
	double p2,
	double p3,
	std::array<double, 16>& cuts,
	size_t& cut_count) noexcept
{
	std::array<double, 2> roots{};
	size_t root_count = 0u;
	if (!cubic_extremum_roots(p0, p1, p2, p3, roots, root_count)) {
		return false;
	}
	for (size_t i = 0u; i < root_count; ++i) {
		if (roots[i] > 0.0 && roots[i] < 1.0 && cut_count < cuts.size()) {
			cuts[cut_count++] = roots[i];
		}
	}
	return true;
}

// The control points of the part of the cubic `p0..p3` between `ta` and `tb`
// (`0 <= ta < tb <= 1`), by de Casteljau splitting.
[[nodiscard]] std::array<PdfPoint, 4> cubic_sub_curve(
	PdfPoint p0,
	PdfPoint p1,
	PdfPoint p2,
	PdfPoint p3,
	double ta,
	double tb) noexcept
{
	const auto lerp = [](PdfPoint u, PdfPoint v, double t) noexcept {
		return PdfPoint{u.x + (v.x - u.x) * t, u.y + (v.y - u.y) * t};
	};
	// The part up to `tb`, then that part's tail from `ta / tb`.
	const auto split_head = [&](std::array<PdfPoint, 4> q, double t) noexcept {
		const PdfPoint a = lerp(q[0], q[1], t);
		const PdfPoint b = lerp(q[1], q[2], t);
		const PdfPoint c = lerp(q[2], q[3], t);
		const PdfPoint d = lerp(a, b, t);
		const PdfPoint e = lerp(b, c, t);
		return std::array<PdfPoint, 4>{q[0], a, d, lerp(d, e, t)};
	};
	const auto split_tail = [&](std::array<PdfPoint, 4> q, double t) noexcept {
		const PdfPoint a = lerp(q[0], q[1], t);
		const PdfPoint b = lerp(q[1], q[2], t);
		const PdfPoint c = lerp(q[2], q[3], t);
		const PdfPoint d = lerp(a, b, t);
		const PdfPoint e = lerp(b, c, t);
		return std::array<PdfPoint, 4>{lerp(d, e, t), e, c, q[3]};
	};
	const std::array<PdfPoint, 4> head = split_head({p0, p1, p2, p3}, tb);
	return ta > 0.0 ? split_tail(head, ta / tb) : head;
}

// The interval `[lo, hi]` of tangent angles the cubic `q` can have (the cone
// of its non-zero control-polygon legs), or none when the legs span a quarter
// turn or more, are all zero, or do not give finite angles.
[[nodiscard]] std::optional<std::pair<double, double>> tangent_cone(
	const std::array<PdfPoint, 4>& q) noexcept
{
	std::optional<double> base;
	double lo = 0.0;
	double hi = 0.0;
	for (size_t i = 0u; i < 3u; ++i) {
		const double dx = q[i + 1u].x - q[i].x;
		const double dy = q[i + 1u].y - q[i].y;
		if (!std::isfinite(dx) || !std::isfinite(dy)) {
			return std::nullopt;
		}
		if (dx == 0.0 && dy == 0.0) {
			continue;
		}
		const double angle = std::atan2(dy, dx);
		if (!base.has_value()) {
			base = angle;
			continue;
		}
		double delta = angle - *base;
		while (delta > std::numbers::pi) {
			delta -= 2.0 * std::numbers::pi;
		}
		while (delta <= -std::numbers::pi) {
			delta += 2.0 * std::numbers::pi;
		}
		lo = std::min(lo, delta);
		hi = std::max(hi, delta);
	}
	if (!base.has_value() || hi - lo >= 0.5 * std::numbers::pi) {
		return std::nullopt;
	}
	return std::pair<double, double>{*base + lo, *base + hi};
}

// `max |p cos a + q sin a|` for `a` in `[lo, hi]`: the ends, and the stationary
// points `a = atan2(q, p) + k pi` that fall inside.
[[nodiscard]] double max_abs_trig(double p, double q, double lo, double hi) noexcept
{
	const auto value = [&](double a) noexcept {
		return std::abs(p * std::cos(a) + q * std::sin(a));
	};
	double best = std::max(value(lo), value(hi));
	const double stationary = std::atan2(q, p);
	for (int k = -3; k <= 3; ++k) {
		const double a = stationary + static_cast<double>(k) * std::numbers::pi;
		if (a > lo && a < hi) {
			best = std::max(best, value(a));
		}
	}
	return best;
}

// Running axis-aligned box of page-space points. `overflowed` records a
// non-finite point, which the caller refuses as transformed-coordinate
// overflow rather than dropping ink.
struct BoxAccumulator {
	double x_min{0.0};
	double y_min{0.0};
	double x_max{0.0};
	double y_max{0.0};
	bool has_any{false};
	bool overflowed{false};

	void include(PdfPoint p) noexcept
	{
		if (!is_finite_point(p)) {
			overflowed = true;
			return;
		}
		include_point_in_bounds(p, x_min, y_min, x_max, y_max, has_any);
	}

	void include(const PdfRect& r) noexcept
	{
		include(PdfPoint{r.x_min, r.y_min});
		include(PdfPoint{r.x_max, r.y_max});
	}

	[[nodiscard]] std::optional<PdfRect> box() const noexcept
	{
		if (!has_any || overflowed) {
			return std::nullopt;
		}
		return PdfRect{x_min, y_min, x_max, y_max};
	}
};

// Stroke parameters in user space, `half_width` being half the line width.
struct StrokeGeometry {
	double half_width{0.5};
	LineCap cap{LineCap::Butt};
	LineJoin join{LineJoin::Miter};
	double miter_limit{10.0};
	// A dash pattern is active: every dash end carries a cap (ISO 32000-1
	// 8.4.3.6), so caps can stand out past joins anywhere along the path.
	bool dashed{false};
};

// Widening for a stroke whose outline is known only by its box (glyph runs):
// every stroked point lies within `half_width * f` of the outline, with `f`
// the miter limit for miter joins (the apex is at most that far from its
// vertex), `sqrt(2)` for projecting caps (the cap square's corners), else 1.
[[nodiscard]] double conservative_stroke_outset(const StrokeGeometry& g) noexcept
{
	double factor = 1.0;
	if (g.join == LineJoin::Miter) {
		factor = std::max(factor, g.miter_limit);
	}
	if (g.cap == LineCap::ProjectingSquare) {
		factor = std::max(factor, std::numbers::sqrt2);
	}
	return g.half_width * factor;
}

// Exact page-space box of a stroked path, computed in user space where the
// PDF stroke is defined (ISO 32000-1 8.4.3.2 to 8.4.3.5, 8.5.3.2) and mapped by
// the CTM. With `r` half the line width, `t` a segment's unit tangent and `n`
// its left normal `(-t.y, t.x)`, the stroke lies inside the union of:
// - each line segment `a -> b`: the parallelogram `a +- r n`, `b +- r n`;
// - each cubic: the curve swept by a disk of radius `r`, bounded by the
//   curve's exact page box (analytic extrema of the transformed control
//   points) grown by the half extents of the disk's image, the ellipse
//   `(r * hypot(ctm.a, ctm.c), r * hypot(ctm.b, ctm.d))`;
// - each open end `p` with outward tangent `u`: butt adds `p +- r n`, round the
//   disk at `p`, projecting square `p +- r n + r u` (up to `r * sqrt(2)` away);
// - each join at vertex `v`: bevel `v +- r n_in` and `v +- r n_out`, round the
//   disk at `v`, miter the apex `v + r (u_in + u_out) / (1 + t_in . t_out)` with
//   `u` the outer normals, at most `r * miter_limit` from `v`, when the miter
//   ratio `1 / sin(phi / 2)` is within the limit (bevel otherwise);
// - each degenerate subpath (a closed single point, or segments of zero
//   length): round caps paint the disk at the point; projecting caps, which
//   ISO 32000-1 8.5.3.2 leaves unpainted but some renderers draw as a square,
//   contribute the disk of radius `r * sqrt(2)` so bounds never cut that ink.
// Every vertex is transformed exactly, so a polyline's box is exact under any
// affine CTM (rotation and shear included); a curve's box is exact where its
// extremum's normal is axis-aligned and a superset elsewhere. Dash patterns
// change only the caps: a dashed stroke paints the solid one's body and joins
// at most, plus a cap at every dash end, which can fall anywhere. So with round
// or projecting caps every line segment also gets its caps at both ends (a cap
// inside a segment stays inside its body or reaches past one of its ends), and
// a curve's disk is widened by `sqrt(2)` for projecting caps. A hairline
// (`r == 0`) gives the path's own box; its device-pixel width belongs to the
// renderer and is absorbed by the consumer's padding.
class StrokeBoundsWalker {
public:
	StrokeBoundsWalker(const PdfMatrix& ctm, const StrokeGeometry& geometry) noexcept
		: ctm_(ctm)
		, g_(geometry)
		, disk_x_(geometry.half_width * std::hypot(ctm.a, ctm.c))
		, disk_y_(geometry.half_width * std::hypot(ctm.b, ctm.d))
	{
	}

	void move_to(PdfPoint p) noexcept
	{
		end_open_subpath();
		start_ = p;
		cur_ = p;
		in_subpath_ = true;
		has_segment_ = false;
		has_draw_verb_ = false;
	}

	void line_to(PdfPoint p) noexcept
	{
		has_draw_verb_ = true;
		add_line(cur_, p);
		cur_ = p;
	}

	void cubic_to(PdfPoint c1, PdfPoint c2, PdfPoint p3) noexcept
	{
		has_draw_verb_ = true;
		const PdfPoint p0 = cur_;
		std::optional<PdfPoint> t_start = unit_direction(p0, c1);
		if (!t_start.has_value()) {
			t_start = unit_direction(p0, c2);
		}
		if (!t_start.has_value()) {
			t_start = unit_direction(p0, p3);
		}
		if (!t_start.has_value()) {
			cur_ = p3; // all four points coincide: a zero-length segment
			return;
		}
		std::optional<PdfPoint> t_end = unit_direction(c2, p3);
		if (!t_end.has_value()) {
			t_end = unit_direction(c1, p3);
		}
		if (!t_end.has_value()) {
			t_end = unit_direction(p0, p3);
		}
		if (!t_end.has_value()) {
			t_end = t_start;
		}

		begin_segment(p0, *t_start);
		const PdfPoint q0 = transform_point(ctm_, p0);
		const PdfPoint q1 = transform_point(ctm_, c1);
		const PdfPoint q2 = transform_point(ctm_, c2);
		const PdfPoint q3 = transform_point(ctm_, p3);
		if (!is_finite_point(q0) || !is_finite_point(q1) ||
			!is_finite_point(q2) || !is_finite_point(q3)) {
			acc_.overflowed = true;
			return;
		}
		if (g_.dashed && g_.cap != LineCap::Butt) {
			// Dash caps can sit anywhere on the curve: the whole pen disk, and
			// for projecting caps the square's corner, `sqrt(2)` times as far.
			double x_min = q0.x;
			double x_max = q0.x;
			double y_min = q0.y;
			double y_max = q0.y;
			expand_cubic_1d_bounds(q0.x, q1.x, q2.x, q3.x, x_min, x_max);
			expand_cubic_1d_bounds(q0.y, q1.y, q2.y, q3.y, y_min, y_max);
			const double reach = g_.cap == LineCap::ProjectingSquare ? std::numbers::sqrt2 : 1.0;
			acc_.include(PdfPoint{x_min - disk_x_ * reach, y_min - disk_y_ * reach});
			acc_.include(PdfPoint{x_max + disk_x_ * reach, y_max + disk_y_ * reach});
		} else {
			include_cubic_body(p0, c1, c2, p3, q0, q1, q2, q3);
		}
		last_tangent_ = *t_end;
		cur_ = p3;
	}

	void close() noexcept
	{
		if (!in_subpath_) {
			return;
		}
		has_draw_verb_ = true;
		add_line(cur_, start_);
		if (has_segment_) {
			include_join(start_, last_tangent_, first_tangent_);
		} else {
			include_degenerate(start_);
		}
		// After `h` the current point is the subpath's start, and a following
		// segment begins a new subpath there.
		cur_ = start_;
		has_segment_ = false;
		has_draw_verb_ = false;
	}

	void finish() noexcept
	{
		end_open_subpath();
		in_subpath_ = false;
	}

	[[nodiscard]] const BoxAccumulator& accumulator() const noexcept
	{
		return acc_;
	}

private:
	[[nodiscard]] std::optional<PdfPoint> unit_direction(PdfPoint a, PdfPoint b) noexcept
	{
		const double dx = b.x - a.x;
		const double dy = b.y - a.y;
		if (!std::isfinite(dx) || !std::isfinite(dy)) {
			acc_.overflowed = true;
			return std::nullopt;
		}
		// Scaled by the larger component first, so a finite segment whose
		// length overflows a double keeps its direction.
		const double scale = std::max(std::abs(dx), std::abs(dy));
		if (!(scale > 0.0)) {
			return std::nullopt;
		}
		const double ux = dx / scale;
		const double uy = dy / scale;
		const double len = std::hypot(ux, uy);
		return PdfPoint{ux / len, uy / len};
	}

	[[nodiscard]] static constexpr PdfPoint left_normal(PdfPoint t) noexcept
	{
		return PdfPoint{-t.y, t.x};
	}

	// The body of a stroked cubic (its ends belong to caps and joins): the
	// points `C(t) + s n(t)`, `|s| <= r`, `n` the unit normal. The curve is cut
	// at its page-space extrema and into eight even pieces, so each piece is
	// monotone on the page and its centre line spans exactly its endpoints;
	// each piece then widens by the largest page reach of `r n` over the
	// tangents it can have, which lie in the cone of its control-polygon legs
	// (the derivative is their non-negative combination). A straight curve
	// widens only across itself, as its line would; a piece whose tangents
	// turn by a quarter turn or more takes the whole disk.
	void include_cubic_body(
		PdfPoint p0,
		PdfPoint p1,
		PdfPoint p2,
		PdfPoint p3,
		PdfPoint q0,
		PdfPoint q1,
		PdfPoint q2,
		PdfPoint q3) noexcept
	{
		std::array<double, 16> cuts{};
		size_t cut_count = 0u;
		for (int i = 1; i < 8; ++i) {
			cuts[cut_count++] = static_cast<double>(i) / 8.0;
		}
		bool extrema_ok = collect_cubic_extremum_ts(q0.x, q1.x, q2.x, q3.x, cuts, cut_count) &&
			collect_cubic_extremum_ts(q0.y, q1.y, q2.y, q3.y, cuts, cut_count);
		if (!extrema_ok) {
			// Extremum arithmetic overflowed: the hull box and the whole disk.
			double x_min = q0.x;
			double x_max = q0.x;
			double y_min = q0.y;
			double y_max = q0.y;
			expand_cubic_1d_bounds(q0.x, q1.x, q2.x, q3.x, x_min, x_max);
			expand_cubic_1d_bounds(q0.y, q1.y, q2.y, q3.y, y_min, y_max);
			acc_.include(PdfPoint{x_min - disk_x_, y_min - disk_y_});
			acc_.include(PdfPoint{x_max + disk_x_, y_max + disk_y_});
			return;
		}
		std::sort(cuts.begin(), cuts.begin() + static_cast<std::ptrdiff_t>(cut_count));
		double t_prev = 0.0;
		for (size_t i = 0u; i <= cut_count; ++i) {
			const double t_next = i < cut_count ? cuts[i] : 1.0;
			if (t_next > t_prev) {
				include_cubic_piece(p0, p1, p2, p3, t_prev, t_next);
			}
			t_prev = std::max(t_prev, t_next);
		}
	}

	void include_cubic_piece(
		PdfPoint p0,
		PdfPoint p1,
		PdfPoint p2,
		PdfPoint p3,
		double ta,
		double tb) noexcept
	{
		const std::array<PdfPoint, 4> piece = cubic_sub_curve(p0, p1, p2, p3, ta, tb);
		const PdfPoint a = transform_point(ctm_, piece[0]);
		const PdfPoint b = transform_point(ctm_, piece[3]);
		if (!is_finite_point(a) || !is_finite_point(b)) {
			acc_.overflowed = true;
			return;
		}
		const double r = g_.half_width;
		double reach_x = disk_x_;
		double reach_y = disk_y_;
		if (const auto cone = tangent_cone(piece)) {
			// `r n` with `n = (-sin a, cos a)` reaches `r |c cos a - a' sin a|`
			// across x (`a'` the CTM's `a`) and `r |d cos a - b sin a|` across y.
			reach_x = r * max_abs_trig(ctm_.c, -ctm_.a, cone->first, cone->second);
			reach_y = r * max_abs_trig(ctm_.d, -ctm_.b, cone->first, cone->second);
		}
		acc_.include(PdfPoint{std::min(a.x, b.x) - reach_x, std::min(a.y, b.y) - reach_y});
		acc_.include(PdfPoint{std::max(a.x, b.x) + reach_x, std::max(a.y, b.y) + reach_y});
	}

	void include_user(PdfPoint base, PdfPoint dir, double scale) noexcept
	{
		acc_.include(transform_point(
			ctm_,
			PdfPoint{base.x + scale * dir.x, base.y + scale * dir.y}));
	}

	void include_disk(PdfPoint center, double scale) noexcept
	{
		const PdfPoint q = transform_point(ctm_, center);
		acc_.include(PdfPoint{q.x - disk_x_ * scale, q.y - disk_y_ * scale});
		acc_.include(PdfPoint{q.x + disk_x_ * scale, q.y + disk_y_ * scale});
	}

	void begin_segment(PdfPoint start, PdfPoint start_tangent) noexcept
	{
		if (has_segment_) {
			include_join(start, last_tangent_, start_tangent);
		} else {
			first_tangent_ = start_tangent;
			has_segment_ = true;
		}
	}

	void add_line(PdfPoint a, PdfPoint b) noexcept
	{
		const std::optional<PdfPoint> t = unit_direction(a, b);
		if (!t.has_value()) {
			return;
		}
		begin_segment(a, *t);
		const double r = g_.half_width;
		const PdfPoint n = left_normal(*t);
		include_user(a, n, r);
		include_user(a, n, -r);
		include_user(b, n, r);
		include_user(b, n, -r);
		if (g_.dashed && g_.cap != LineCap::Butt) {
			include_cap(b, *t);
			include_cap(a, PdfPoint{-t->x, -t->y});
		}
		last_tangent_ = *t;
	}

	void include_cap(PdfPoint p, PdfPoint outward) noexcept
	{
		const double r = g_.half_width;
		const PdfPoint n = left_normal(outward);
		include_user(p, n, r);
		include_user(p, n, -r);
		switch (g_.cap) {
		case LineCap::Butt:
			break;
		case LineCap::Round:
			include_disk(p, 1.0);
			break;
		case LineCap::ProjectingSquare: {
			const PdfPoint tip{p.x + r * outward.x, p.y + r * outward.y};
			include_user(tip, n, r);
			include_user(tip, n, -r);
			break;
		}
		}
	}

	void include_join(PdfPoint v, PdfPoint t_in, PdfPoint t_out) noexcept
	{
		const double r = g_.half_width;
		const PdfPoint n_in = left_normal(t_in);
		const PdfPoint n_out = left_normal(t_out);
		include_user(v, n_in, r);
		include_user(v, n_in, -r);
		include_user(v, n_out, r);
		include_user(v, n_out, -r);
		switch (g_.join) {
		case LineJoin::Bevel:
			break;
		case LineJoin::Round:
			include_disk(v, 1.0);
			break;
		case LineJoin::Miter: {
			const double cross = t_in.x * t_out.y - t_in.y * t_out.x;
			// `1 + t_in . t_out` is `|t_in + t_out|^2 / 2` for unit tangents; the
			// sum keeps its precision where the dot product of two nearly
			// opposite directions rounds to -1 and would lose an allowed miter.
			const double sum_x = t_in.x + t_out.x;
			const double sum_y = t_in.y + t_out.y;
			const double one_plus_dot = 0.5 * (sum_x * sum_x + sum_y * sum_y);
			const double sin_half_phi = 0.5 * std::hypot(sum_x, sum_y);
			// The small tolerance keeps the apex when rounding puts a ratio that
			// equals the limit a hair over it, where a renderer may still miter.
			if (g_.miter_limit * sin_half_phi >= 1.0 - 1e-9 && sin_half_phi > 0.0) {
				const PdfPoint u_in = cross <= 0.0 ? n_in : PdfPoint{-n_in.x, -n_in.y};
				const PdfPoint u_out = cross <= 0.0 ? n_out : PdfPoint{-n_out.x, -n_out.y};
				const double denom = one_plus_dot; // 2 sin^2(phi / 2) > 0
				include_user(
					v,
					PdfPoint{(u_in.x + u_out.x) / denom, (u_in.y + u_out.y) / denom},
					r);
			}
			break;
		}
		}
	}

	void include_degenerate(PdfPoint p) noexcept
	{
		if (g_.cap == LineCap::Round) {
			include_disk(p, 1.0);
		} else if (g_.cap == LineCap::ProjectingSquare) {
			include_disk(p, std::numbers::sqrt2);
		}
	}

	void end_open_subpath() noexcept
	{
		if (!in_subpath_ || !has_draw_verb_) {
			return;
		}
		if (has_segment_) {
			include_cap(start_, PdfPoint{-first_tangent_.x, -first_tangent_.y});
			include_cap(cur_, last_tangent_);
		} else {
			include_degenerate(start_);
		}
		has_draw_verb_ = false;
	}

	PdfMatrix ctm_{};
	StrokeGeometry g_{};
	double disk_x_{0.0};
	double disk_y_{0.0};
	BoxAccumulator acc_{};
	PdfPoint start_{};
	PdfPoint cur_{};
	PdfPoint first_tangent_{};
	PdfPoint last_tangent_{};
	bool in_subpath_{false};
	bool has_segment_{false};
	bool has_draw_verb_{false};
};

// Walks a validated `(verbs, points)` slice through `StrokeBoundsWalker`.
// `re` is the closed subpath `m`, three `l`, `h` (ISO 32000-1 Table 59).
[[nodiscard]] BoxAccumulator compute_stroke_box(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points,
	const PdfMatrix& ctm,
	const StrokeGeometry& geometry) noexcept
{
	StrokeBoundsWalker walker(ctm, geometry);
	size_t pt_idx = 0u;
	for (const PathVerb verb : verbs) {
		switch (verb) {
		case PathVerb::MoveTo:
			walker.move_to(points[pt_idx++]);
			break;
		case PathVerb::LineTo:
			walker.line_to(points[pt_idx++]);
			break;
		case PathVerb::CubicBezierTo: {
			const PdfPoint c1 = points[pt_idx];
			const PdfPoint c2 = points[pt_idx + 1u];
			const PdfPoint end = points[pt_idx + 2u];
			pt_idx += 3u;
			walker.cubic_to(c1, c2, end);
			break;
		}
		case PathVerb::Rect: {
			const PdfPoint origin = points[pt_idx];
			const PdfPoint size = points[pt_idx + 1u];
			pt_idx += 2u;
			walker.move_to(origin);
			walker.line_to(PdfPoint{origin.x + size.x, origin.y});
			walker.line_to(PdfPoint{origin.x + size.x, origin.y + size.y});
			walker.line_to(PdfPoint{origin.x, origin.y + size.y});
			walker.close();
			break;
		}
		case PathVerb::CloseSubpath:
			walker.close();
			break;
		}
	}
	walker.finish();
	return walker.accumulator();
}

// Box of `bitmap`'s non-transparent cells in unit-image space (the cell
// convention documented on `DrawImageCommand`), grown by half a cell when the
// image is interpolated (bilinear sampling reaches that far into a transparent
// neighbour) and clamped to the unit square. `std::nullopt` when the bitmap is
// fully transparent.
[[nodiscard]] std::optional<PdfRect> unit_image_coverage(
	const DecodedBitmap& bitmap,
	bool interpolate) noexcept
{
	if (!bitmap.opaque_pixel_box.has_value()) {
		return std::nullopt;
	}
	const PixelBox& cells = *bitmap.opaque_pixel_box;
	const double w = static_cast<double>(bitmap.width);
	const double h = static_cast<double>(bitmap.height);
	const double pad = interpolate ? 0.5 : 0.0;
	// Row 0 is the image's first row, at the top of the unit square (ISO 32000-1 section 8.9.4).
	return PdfRect{
		std::clamp((static_cast<double>(cells.x_min) - pad) / w, 0.0, 1.0),
		std::clamp(1.0 - (static_cast<double>(cells.y_max) + pad) / h, 0.0, 1.0),
		std::clamp((static_cast<double>(cells.x_max) + pad) / w, 0.0, 1.0),
		std::clamp(1.0 - (static_cast<double>(cells.y_min) - pad) / h, 0.0, 1.0)};
}

[[nodiscard]] std::optional<PdfRect> include_rect(
	std::optional<PdfRect> acc,
	std::optional<PdfRect> r) noexcept
{
	if (!r.has_value()) {
		return acc;
	}
	if (!acc.has_value()) {
		return r;
	}
	return union_rect(*acc, *r);
}

// Capacity a pool of `size` elements and `capacity` slots needs to take
// `extra` more: unchanged when they fit, else doubled (capped at
// `max_elements`, never below what is needed) or exactly enough when
// `geometric` is false. The caller has checked `size + extra <= max_elements`.
[[nodiscard]] size_t planned_capacity(
	size_t size,
	size_t capacity,
	size_t extra,
	size_t max_elements,
	double growth) noexcept
{
	const size_t needed = size + extra;
	if (needed <= capacity) {
		return capacity;
	}
	const size_t doubled = std::max(needed, capacity > max_elements / 2u ? max_elements : capacity * 2u);
	return needed + static_cast<size_t>(growth * static_cast<double>(doubled - needed));
}

// Allocator for the `std::shared_ptr<const DisplayList>` control block that
// `finish()` creates, so `DisplayListLimits::fail_finish_control_block_allocation`
// fails exactly that allocation.
template <typename T>
struct FinishControlBlockAllocator {
	using value_type = T;

	bool fail{false};

	explicit FinishControlBlockAllocator(bool should_fail) noexcept
		: fail(should_fail)
	{
	}

	template <typename U>
	FinishControlBlockAllocator(const FinishControlBlockAllocator<U>& other) noexcept
		: fail(other.fail)
	{
	}

	[[nodiscard]] T* allocate(size_t n)
	{
		if (fail) {
			throw std::bad_alloc();
		}
		return std::allocator<T>{}.allocate(n);
	}

	void deallocate(T* p, size_t n) noexcept
	{
		std::allocator<T>{}.deallocate(p, n);
	}

	template <typename U>
	bool operator==(const FinishControlBlockAllocator<U>& other) const noexcept
	{
		return fail == other.fail;
	}
};

constexpr size_t kMaxU32 = std::numeric_limits<uint32_t>::max();

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
	if (!is_finite_matrix(transform) ||
		!validate_path_slice(verbs, points) ||
		!validate_transformed_path_finite(verbs, points, transform)) {
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
	// Both products fit in 64 bits: stride < 2^32 and height <= 2^16.
	const uint64_t unpadded_last_row_bytes =
		static_cast<uint64_t>(stride) * static_cast<uint64_t>(height - 1u) + min_stride;
	const uint64_t padded_bytes =
		static_cast<uint64_t>(stride) * static_cast<uint64_t>(height);
	const uint64_t actual_bytes = static_cast<uint64_t>(bgra_pixels.size());
	if (unpadded_last_row_bytes > kMaxBitmapBytes ||
		(actual_bytes != unpadded_last_row_bytes &&
		 (actual_bytes != padded_bytes || padded_bytes > kMaxBitmapBytes))) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DecodedBitmap pixel buffer size does not match stride * height");
	}

	std::optional<PixelBox> opaque_box;
	for (uint32_t y = 0u; y < height; ++y) {
		const uint8_t* row = bgra_pixels.data() + static_cast<size_t>(y) * stride;
		uint32_t first = width;
		uint32_t last = 0u;
		for (uint32_t x = 0u; x < width; ++x) {
			if (row[static_cast<size_t>(x) * 4u + 3u] != 0u) {
				first = std::min(first, x);
				last = x;
			}
		}
		if (first == width) {
			continue;
		}
		if (!opaque_box.has_value()) {
			opaque_box = PixelBox{first, y, last + 1u, y + 1u};
		} else {
			opaque_box->x_min = std::min(opaque_box->x_min, first);
			opaque_box->x_max = std::max(opaque_box->x_max, last + 1u);
			opaque_box->y_max = y + 1u;
		}
	}

	try {
		return std::shared_ptr<const DecodedBitmap>(new DecodedBitmap(
			width,
			height,
			stride,
			std::move(bgra_pixels),
			is_monochrome_mask,
			opaque_box));
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
	std::vector<CommandBounds>&& command_bounds,
	std::vector<RecordedPath>&& paths,
	std::vector<PathVerb>&& path_verbs,
	std::vector<PdfPoint>&& path_points,
	std::vector<PositionedGlyph>&& glyphs,
	std::vector<float>&& dash_values) noexcept
	: page_box_pt_(page_box_pt)
	, content_bounds_pt_(content_bounds_pt)
	, has_content_bounds_(has_content_bounds)
	, commands_(std::move(commands))
	, command_bounds_(std::move(command_bounds))
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
	// The page box is the outermost clip: nothing outside it is content, and a
	// page box without area holds none.
	current_state_.clip_box_pt = page_box_pt_;
	current_state_.clip_is_empty = page_box_pt_.is_empty();
}

PdfResult<void> DisplayListBuilder::charge_work(size_t ops) noexcept
{
	return charge_geometry_work(ops);
}

size_t DisplayListBuilder::glyph_room() const noexcept
{
	const size_t max_glyphs = std::min(limits_.max_glyphs, kMaxU32);
	const size_t by_count = glyphs_.size() < max_glyphs ? max_glyphs - glyphs_.size() : 0u;
	const size_t used = current_pool_bytes();
	const size_t by_bytes = used < limits_.max_total_bytes ? (limits_.max_total_bytes - used) / sizeof(PositionedGlyph) : 0u;
	return std::min(by_count, by_bytes);
}

size_t DisplayListBuilder::current_pool_bytes() const noexcept
{
	// Capacities, not sizes: a pool holds its whole reserved buffer. Each term
	// is bounded by an allocation that succeeded, so the sum cannot overflow.
	return commands_.capacity() * sizeof(DisplayCommand) +
		command_bounds_.capacity() * sizeof(CommandBounds) +
		paths_.capacity() * sizeof(RecordedPath) +
		path_verbs_.capacity() * sizeof(PathVerb) +
		path_points_.capacity() * sizeof(PdfPoint) +
		glyphs_.capacity() * sizeof(PositionedGlyph) +
		dash_values_.capacity() * sizeof(float) +
		state_stack_.capacity() * sizeof(TrackingState);
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

PdfResult<void> DisplayListBuilder::charge_allocation_op() noexcept
{
	if (limits_.fail_allocation_after_ops != 0u) {
		++allocation_ops_;
		if (allocation_ops_ >= limits_.fail_allocation_after_ops) {
			return make_pdf_error(
				ErrorCode::OutOfMemory,
				"Simulated out-of-memory in DisplayListBuilder");
		}
	}
	return {};
}

PdfResult<void> DisplayListBuilder::charge_geometry_work(size_t ops) noexcept
{
	if (ops > limits_.max_geometry_work_ops ||
		geometry_work_ops_ > limits_.max_geometry_work_ops - ops) {
		return make_pdf_error(
			ErrorCode::ResourceLimit,
			"DisplayListBuilder geometry work budget exceeded");
	}
	geometry_work_ops_ += ops;
	return {};
}

// Checks every pool's count limit, then that the peak heap while growing (the
// pools' current capacities plus each new buffer, since a reallocating pool
// frees its old buffer only after the copy) stays within `max_total_bytes`,
// trying doubling growth first and exact growth second, and only then charges
// one allocation step and reserves. Nothing is allocated when it refuses.
template <typename... Ts>
PdfResult<void> DisplayListBuilder::reserve_growth(Growth<Ts>... growths) noexcept
{
	bool within_limits = true;
	((within_limits = within_limits &&
		growths.extra <= growths.max_elements &&
		growths.pool->size() <= growths.max_elements - growths.extra),
	 ...);
	if (!within_limits) {
		return make_pdf_error(
			ErrorCode::ResourceLimit,
			"DisplayListBuilder count limit exceeded");
	}

	const size_t used = current_pool_bytes();
	const auto peak_fits = [&](double growth) noexcept {
		size_t peak = used;
		bool ok = true;
		const auto add = [&](const auto& g) noexcept {
			using Element = typename std::remove_cvref_t<decltype(*g.pool)>::value_type;
			const size_t capacity = planned_capacity(
				g.pool->size(),
				g.pool->capacity(),
				g.extra,
				g.max_elements,
				growth);
			if (capacity > g.pool->capacity()) {
				size_t bytes = 0u;
				ok = ok && checked_mul_size(capacity, sizeof(Element), bytes) &&
					checked_add_size(peak, bytes, peak);
			}
		};
		(add(growths), ...);
		return ok && peak <= limits_.max_total_bytes;
	};
	// Doubling when it fits; otherwise the largest growth toward doubling that
	// fits, so a pool near the budget reallocates once more at most instead of
	// once per append (a pool past half the remaining budget cannot grow again).
	double growth = 1.0;
	if (!peak_fits(1.0)) {
		if (!peak_fits(0.0)) {
			return make_pdf_error(
				ErrorCode::ResourceLimit,
				"DisplayListBuilder total byte budget exceeded");
		}
		double low = 0.0;
		double high = 1.0;
		for (int step = 0; step < 32; ++step) {
			const double mid = 0.5 * (low + high);
			(peak_fits(mid) ? low : high) = mid;
		}
		growth = low;
	}
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}
	try {
		(growths.pool->reserve(planned_capacity(
			 growths.pool->size(),
			 growths.pool->capacity(),
			 growths.extra,
			 growths.max_elements,
			 growth)),
		 ...);
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory growing DisplayListBuilder pools");
	}
}

PdfResult<void> DisplayListBuilder::reserve_command_slot() noexcept
{
	return reserve_growth(
		Growth<DisplayCommand>{&commands_, 1u, limits_.max_commands},
		Growth<CommandBounds>{&command_bounds_, 1u, limits_.max_commands});
}

std::optional<PdfRect> DisplayListBuilder::clip_to_state(const PdfRect& box) const noexcept
{
	if (current_state_.clip_is_empty) {
		return std::nullopt;
	}
	const auto inter = intersect_rect(box, current_state_.clip_box_pt);
	if (!inter.has_value()) {
		return std::nullopt;
	}
	// A primitive with area that only touches the clip along an edge paints
	// nothing; a hairline's zero-area box keeps its zero-area intersection.
	if ((inter->width() <= 0.0 && box.width() > 0.0) ||
		(inter->height() <= 0.0 && box.height() > 0.0)) {
		return std::nullopt;
	}
	return inter;
}

void DisplayListBuilder::commit_command(
	DisplayCommand&& command,
	std::optional<PdfRect> bounds) noexcept
{
	// `reserve_growth()` reserved both slots, so neither push reallocates.
	commands_.push_back(std::move(command));
	CommandBounds entry{};
	if (bounds.has_value()) {
		entry.bounds_pt = *bounds;
		entry.has_bounds = true;
		content_bounds_pt_ = has_content_bounds_
			? union_rect(content_bounds_pt_, *bounds)
			: *bounds;
		has_content_bounds_ = true;
	}
	command_bounds_.push_back(entry);
}

PdfResult<void> DisplayListBuilder::reserve_commands(size_t count) noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	const size_t extra = count > commands_.size() ? count - commands_.size() : 0u;
	return reserve_growth(
		Growth<DisplayCommand>{&commands_, extra, limits_.max_commands},
		Growth<CommandBounds>{&command_bounds_, extra, limits_.max_commands});
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
	if (auto ok = reserve_growth(
			Growth<PathVerb>{&path_verbs_, verbs.size(), std::min(limits_.max_path_verbs, kMaxU32)},
			Growth<PdfPoint>{&path_points_, points.size(), std::min(limits_.max_path_points, kMaxU32)},
			Growth<RecordedPath>{&paths_, 1u, std::min(limits_.max_paths, kMaxU32)});
		!ok.has_value()) {
		return ok.error();
	}

	RecordedPath rec{};
	rec.verb_offset = static_cast<uint32_t>(path_verbs_.size());
	rec.verb_count = static_cast<uint32_t>(verbs.size());
	rec.point_offset = static_cast<uint32_t>(path_points_.size());
	rec.point_count = static_cast<uint32_t>(points.size());
	if (const auto bounds = compute_recorded_path_bounds(verbs, points)) {
		rec.bounds_pt = *bounds;
		rec.has_bounds = true;
	}

	// Capacity is reserved, so these inserts do not reallocate.
	path_verbs_.insert(path_verbs_.end(), verbs.begin(), verbs.end());
	path_points_.insert(path_points_.end(), points.begin(), points.end());
	const uint32_t index = static_cast<uint32_t>(paths_.size());
	paths_.push_back(rec);
	return index;
}

PdfResult<void> DisplayListBuilder::save_state() noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (state_stack_.size() >= limits_.max_save_depth) {
		// Sticky: the caller's matching `restore_state()` would otherwise pop
		// the enclosing frame.
		sticky_error_ = PdfError{
			ErrorCode::ResourceLimit,
			"DisplayListBuilder::save_state exceeded max_save_depth"};
		return *sticky_error_;
	}
	if (auto ok = reserve_growth(
			Growth<DisplayCommand>{&commands_, 1u, limits_.max_commands},
			Growth<CommandBounds>{&command_bounds_, 1u, limits_.max_commands},
			Growth<TrackingState>{&state_stack_, 1u, limits_.max_save_depth});
		!ok.has_value()) {
		return ok;
	}
	state_stack_.push_back(current_state_);
	commit_command(DisplayCommand{SaveStateCommand{}}, std::nullopt);
	return {};
}

PdfResult<void> DisplayListBuilder::restore_state() noexcept
{
	if (auto ok = check_can_mutate(); !ok.has_value()) {
		return ok;
	}
	if (state_stack_.empty()) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::restore_state has no saved state to restore");
	}
	if (auto ok = reserve_command_slot(); !ok.has_value()) {
		return ok;
	}
	current_state_ = state_stack_.back();
	state_stack_.pop_back();
	commit_command(DisplayCommand{RestoreStateCommand{}}, std::nullopt);
	return {};
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
	if (auto ok = reserve_command_slot(); !ok.has_value()) {
		return ok;
	}
	commit_command(DisplayCommand{ConcatMatrixCommand{matrix}}, std::nullopt);
	current_state_.ctm = combined;
	return {};
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

	// ISO 32000-1 8.4.3.6: an empty array is a solid line, and entries are
	// non-negative numbers. A non-empty array whose entries are all zero is
	// recorded as solid, as Acrobat paints it. Odd lengths and the phase are
	// recorded unchanged; replay repeats an odd array.
	bool any_positive = false;
	for (const float d : dash_array) {
		if (!std::isfinite(d) || d < 0.0f) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::set_stroke_style dash values must be finite and non-negative");
		}
		any_positive = any_positive || d > 0.0f;
	}
	const std::span<const float> recorded_dashes =
		any_positive ? dash_array : std::span<const float>{};

	if (auto ok = reserve_growth(
			Growth<DisplayCommand>{&commands_, 1u, limits_.max_commands},
			Growth<CommandBounds>{&command_bounds_, 1u, limits_.max_commands},
			Growth<float>{&dash_values_, recorded_dashes.size(), std::min(limits_.max_dash_values, kMaxU32)});
		!ok.has_value()) {
		return ok;
	}

	SetStrokeStyleCommand cmd{};
	cmd.line_width = line_width;
	cmd.line_cap = line_cap;
	cmd.line_join = line_join;
	cmd.miter_limit = miter_limit;
	cmd.dash_offset = static_cast<uint32_t>(dash_values_.size());
	cmd.dash_count = static_cast<uint32_t>(recorded_dashes.size());
	cmd.dash_phase = dash_phase;

	dash_values_.insert(dash_values_.end(), recorded_dashes.begin(), recorded_dashes.end());
	commit_command(DisplayCommand{cmd}, std::nullopt);
	current_state_.line_width = line_width;
	current_state_.line_cap = line_cap;
	current_state_.line_join = line_join;
	current_state_.miter_limit = miter_limit;
	current_state_.dashed = !recorded_dashes.empty();
	return {};
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
	if (auto ok = reserve_command_slot(); !ok.has_value()) {
		return ok;
	}

	SetAlphaAndBlendCommand cmd{};
	cmd.stroke_alpha = stroke_alpha;
	cmd.fill_alpha = fill_alpha;
	cmd.blend_mode = blend_mode;
	commit_command(DisplayCommand{cmd}, std::nullopt);
	current_state_.stroke_alpha = stroke_alpha;
	current_state_.fill_alpha = fill_alpha;
	return {};
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
	if (current_state_.clip_depth >= limits_.max_clip_depth) {
		return make_pdf_error(
			ErrorCode::ResourceLimit,
			"DisplayListBuilder::push_clip_path exceeded max_clip_depth");
	}

	const RecordedPath& p = paths_[path_index];
	const auto verbs = std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	const auto points = std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);
	if (auto ok = charge_geometry_work(verbs.size() + points.size() + 1u); !ok.has_value()) {
		return ok;
	}
	if (!validate_transformed_path_finite(verbs, points, current_state_.ctm)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::push_clip_path transformed coordinates overflowed");
	}

	// A clip narrows bounds by its exact page-space box under either fill rule;
	// a clip path that encloses no area clips everything away.
	const auto clip_box = compute_recorded_path_bounds(verbs, points, current_state_.ctm);
	if (auto ok = reserve_command_slot(); !ok.has_value()) {
		return ok;
	}
	commit_command(DisplayCommand{PushClipPathCommand{path_index, fill_rule}}, std::nullopt);

	++current_state_.clip_depth;
	if (!current_state_.clip_is_empty) {
		const auto narrowed = clip_box.has_value()
			? intersect_rect(current_state_.clip_box_pt, *clip_box)
			: std::nullopt;
		if (narrowed.has_value() && !narrowed->is_empty()) {
			current_state_.clip_box_pt = *narrowed;
		} else {
			current_state_.clip_is_empty = true;
		}
	}
	return {};
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

	const RecordedPath& p = paths_[path_index];
	const auto verbs = std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	const auto points = std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);
	if (auto ok = charge_geometry_work(verbs.size() + points.size() + 1u); !ok.has_value()) {
		return ok;
	}
	if (!validate_transformed_path_finite(verbs, points, current_state_.ctm)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_path transformed path coordinates overflowed");
	}

	const bool visible_fill =
		fill && (current_state_.fill_alpha > 0.0f) && (fill_color.a > 0.0f);
	const bool visible_stroke =
		stroke && (current_state_.stroke_alpha > 0.0f) && (stroke_color.a > 0.0f);

	std::optional<PdfRect> painted;
	if (visible_fill) {
		if (const auto box = compute_recorded_path_bounds(verbs, points, current_state_.ctm)) {
			painted = include_rect(painted, clip_to_state(*box));
		}
	}
	if (visible_stroke) {
		StrokeGeometry geometry{};
		geometry.half_width = 0.5 * static_cast<double>(current_state_.line_width);
		geometry.cap = current_state_.line_cap;
		geometry.join = current_state_.line_join;
		geometry.miter_limit = static_cast<double>(current_state_.miter_limit);
		geometry.dashed = current_state_.dashed;
		const BoxAccumulator stroke_box =
			compute_stroke_box(verbs, points, current_state_.ctm, geometry);
		if (stroke_box.overflowed) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::draw_path stroke outline overflowed");
		}
		if (const auto box = stroke_box.box()) {
			painted = include_rect(painted, clip_to_state(*box));
		}
	}

	if (auto ok = reserve_command_slot(); !ok.has_value()) {
		return ok;
	}
	DrawPathCommand cmd{};
	cmd.path_index = path_index;
	cmd.stroke_color = stroke_color;
	cmd.fill_color = fill_color;
	cmd.stroke = stroke;
	cmd.fill = fill;
	cmd.fill_rule = fill_rule;
	commit_command(DisplayCommand{cmd}, painted);
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
	const size_t max_glyphs = std::min(limits_.max_glyphs, kMaxU32);
	if (glyphs.size() > max_glyphs || glyphs_.size() > max_glyphs - glyphs.size()) {
		return make_pdf_error(
			ErrorCode::ResourceLimit,
			"DisplayListBuilder::draw_glyph_run exceeds the glyph limit");
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

	const PdfMatrix combined = recto::pdf::concat_matrix(text_matrix, current_state_.ctm);
	if (!is_finite_matrix(combined)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run produced non-finite text-to-page matrix");
	}
	if (auto ok = charge_geometry_work(glyphs.size() + 1u); !ok.has_value()) {
		return ok;
	}

	for (const PositionedGlyph& g : glyphs) {
		if (!is_finite_point(g.origin_pt) ||
			!std::isfinite(g.advance_pt) ||
			!is_finite_point(transform_point(combined, g.origin_pt))) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::draw_glyph_run glyph origin and advance must be finite in text and page space");
		}
		if (!std::isfinite(g.width_scale) || g.width_scale <= 0.0) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::draw_glyph_run glyph width_scale must be finite and positive");
		}
		if (g.ink_bounds_pt.has_value()) {
			if (!is_valid_rect(*g.ink_bounds_pt)) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"DisplayListBuilder::draw_glyph_run glyph ink_bounds_pt must be finite and non-inverted");
			}
			const PdfRect placed{
				g.origin_pt.x + g.ink_bounds_pt->x_min,
				g.origin_pt.y + g.ink_bounds_pt->y_min,
				g.origin_pt.x + g.ink_bounds_pt->x_max,
				g.origin_pt.y + g.ink_bounds_pt->y_max};
			if (!validate_transformed_rect_finite(placed, combined)) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"DisplayListBuilder::draw_glyph_run placed glyph ink_bounds_pt overflowed");
			}
		}
	}

	// `Tr 3` (invisible) and `Tr 7` (clip only) paint nothing; the clip that
	// modes 4 to 7 accumulate belongs to D01 T02 section 16.
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
	// A Type 3 glyph paints by its own procedure (ISO 32000-1 9.6.5), whose
	// colours and graphics state it can set itself, so its sub-list counts
	// whatever the run's colours and alphas; only an invisible run (`Tr 3`)
	// paints nothing. Its sub-list's bounds already hold its own strokes.
	const bool is_type3 = type3_sub_list != nullptr;
	const bool visible_fill = is_type3
		? render_mode != TextRenderMode::Invisible
		: mode_fills && (current_state_.fill_alpha > 0.0f) && (fill_color.a > 0.0f);
	const bool visible_stroke = !is_type3 &&
		mode_strokes && (current_state_.stroke_alpha > 0.0f) && (stroke_color.a > 0.0f);

	StrokeGeometry geometry{};
	geometry.half_width = 0.5 * static_cast<double>(current_state_.line_width);
	geometry.cap = current_state_.line_cap;
	geometry.join = current_state_.line_join;
	geometry.miter_limit = static_cast<double>(current_state_.miter_limit);
	const double stroke_outset = visible_stroke ? conservative_stroke_outset(geometry) : 0.0;

	std::optional<PdfRect> text_space_bounds;
	std::optional<PdfRect> painted;
	bool overflowed = false;
	// Each glyph box is clipped on its own, so a clip between glyphs leaves
	// out the gap. A stroke widens the box in user space, where the line width
	// is defined, before the CTM maps it to the page.
	const auto place = [&](const PdfRect& text_box) noexcept {
		text_space_bounds = include_rect(text_space_bounds, text_box);
		if (!visible_fill && !visible_stroke) {
			return;
		}
		PdfRect page_box{};
		if (stroke_outset > 0.0) {
			PdfRect user_box = transform_rect(text_matrix, text_box);
			user_box.x_min -= stroke_outset;
			user_box.y_min -= stroke_outset;
			user_box.x_max += stroke_outset;
			user_box.y_max += stroke_outset;
			page_box = transform_rect(current_state_.ctm, user_box);
		} else {
			page_box = transform_rect(combined, text_box);
		}
		if (!is_valid_rect(page_box)) {
			overflowed = true;
			return;
		}
		painted = include_rect(painted, clip_to_state(page_box));
	};

	if (type3_sub_list != nullptr) {
		// A Type 3 glyph paints its sub-list, placed at the glyph origin.
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
				if (!validate_transformed_rect_finite(placed, combined)) {
					return make_pdf_error(
						ErrorCode::DamagedFormat,
						"DisplayListBuilder::draw_glyph_run Type 3 glyph bounds overflowed");
				}
				place(placed);
			}
		}
	} else {
		bool missing_ink_box = false;
		for (const PositionedGlyph& g : glyphs) {
			if (g.is_blank) {
				continue;
			}
			if (!g.ink_bounds_pt.has_value()) {
				missing_ink_box = true;
				continue;
			}
			place(PdfRect{
				g.origin_pt.x + g.ink_bounds_pt->x_min,
				g.origin_pt.y + g.ink_bounds_pt->y_min,
				g.origin_pt.x + g.ink_bounds_pt->x_max,
				g.origin_pt.y + g.ink_bounds_pt->y_max});
		}
		if (local_bounds_pt.has_value()) {
			if (!validate_transformed_rect_finite(*local_bounds_pt, combined)) {
				return make_pdf_error(
					ErrorCode::DamagedFormat,
					"DisplayListBuilder::draw_glyph_run local_bounds_pt overflowed under CTM");
			}
			// The run box is the conservative fallback for glyphs without
			// their own ink box; with every box present it is metadata only.
			if (missing_ink_box) {
				place(*local_bounds_pt);
			} else {
				text_space_bounds = include_rect(text_space_bounds, *local_bounds_pt);
			}
		} else if (missing_ink_box && (mode_fills || mode_strokes)) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::draw_glyph_run visible glyph requires ink_bounds_pt, is_blank, local_bounds_pt, or type3_sub_list");
		}
	}
	if (overflowed) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run stroked glyph bounds overflowed");
	}

	if (auto ok = reserve_growth(
			Growth<DisplayCommand>{&commands_, 1u, limits_.max_commands},
			Growth<CommandBounds>{&command_bounds_, 1u, limits_.max_commands},
			Growth<PositionedGlyph>{&glyphs_, glyphs.size(), max_glyphs});
		!ok.has_value()) {
		return ok;
	}

	RecordedGlyphRun cmd{};
	cmd.glyph_offset = static_cast<uint32_t>(glyphs_.size());
	cmd.glyph_count = static_cast<uint32_t>(glyphs.size());
	cmd.font_size = font_size;
	cmd.render_mode = render_mode;
	cmd.fill_color = fill_color;
	cmd.stroke_color = stroke_color;
	cmd.text_matrix = text_matrix;
	cmd.ctm = current_state_.ctm;
	cmd.font_handle = std::move(font_handle);
	cmd.type3_sub_list = std::move(type3_sub_list);
	if (text_space_bounds.has_value()) {
		cmd.bounds_pt = *text_space_bounds;
		cmd.has_bounds = true;
	}

	glyphs_.insert(glyphs_.end(), glyphs.begin(), glyphs.end());
	commit_command(DisplayCommand{std::move(cmd)}, painted);
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
	if (!is_finite_matrix(image_to_page) ||
		!validate_transformed_rect_finite(PdfRect{0.0, 0.0, 1.0, 1.0}, image_to_page)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_image produced non-finite image-to-page coordinates");
	}

	const bool potentially_visible =
		(current_state_.fill_alpha > 0.0f) &&
		(!bitmap->is_monochrome_mask || mask_tint_color.a > 0.0f);

	// Coverage is the bitmap's opaque-cell box intersected with the soft mask's
	// in unit-image space, which handles masks of any resolution; a zero-area
	// overlap paints nothing.
	std::optional<PdfRect> painted;
	if (potentially_visible) {
		std::optional<PdfRect> coverage = unit_image_coverage(*bitmap, interpolate);
		if (coverage.has_value() && soft_mask != nullptr) {
			const auto mask_coverage = unit_image_coverage(*soft_mask, interpolate);
			coverage = mask_coverage.has_value()
				? intersect_rect(*coverage, *mask_coverage)
				: std::nullopt;
		}
		if (coverage.has_value() && !coverage->is_empty()) {
			painted = clip_to_state(transform_rect(image_to_page, *coverage));
		}
	}

	if (auto ok = reserve_command_slot(); !ok.has_value()) {
		return ok;
	}
	DrawImageCommand cmd{};
	cmd.bitmap = std::move(bitmap);
	cmd.dest_transform = dest_transform;
	cmd.interpolate = interpolate;
	cmd.soft_mask = std::move(soft_mask);
	cmd.mask_tint_color = mask_tint_color;
	commit_command(DisplayCommand{std::move(cmd)}, painted);
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
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		sticky_error_ = ok.error();
		finished_ = true;
		return ok.error();
	}

	// The builder is finished from here whatever happens: once the pools are
	// moved into the list, a retry must not publish an empty one.
	finished_ = true;
	try {
		std::unique_ptr<DisplayList> list(new DisplayList(
			page_box_pt_,
			content_bounds_pt_,
			has_content_bounds_,
			std::move(commands_),
			std::move(command_bounds_),
			std::move(paths_),
			std::move(path_verbs_),
			std::move(path_points_),
			std::move(glyphs_),
			std::move(dash_values_)));
		// If the control block cannot be allocated, this constructor deletes
		// the list itself, so nothing is published.
		std::shared_ptr<const DisplayList> result(
			list.release(),
			std::default_delete<const DisplayList>{},
			FinishControlBlockAllocator<DisplayList>{limits_.fail_finish_control_block_allocation});
		return result;
	} catch (const std::bad_alloc&) {
		sticky_error_ = PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::finish"};
		return *sticky_error_;
	}
}

} // namespace recto::pdf
