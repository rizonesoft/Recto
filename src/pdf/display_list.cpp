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
			if (t > 0.0 && t < 1.0 && std::isfinite(t)) {
				out_t.push_back(t);
			}
		}
		return;
	}

	const double disc = b * b - a * c;
	if (disc < 0.0 || !std::isfinite(disc)) {
		return;
	}
	const double sqrt_disc = std::sqrt(disc);
	const double t1 = (-b - sqrt_disc) / a;
	const double t2 = (-b + sqrt_disc) / a;
	for (double t : {t1, t2}) {
		if (t > 0.0 && t < 1.0 && std::isfinite(t)) {
			out_t.push_back(t);
		}
	}
}

// Solves all roots `t in (0, 1)` of the 1D cubic Bernstein polynomial
// `B(t) = (1-t)^3 d0 + 3(1-t)^2 t d1 + 3(1-t) t^2 d2 + t^3 d3 == 0`
// by splitting at derivative extrema and bisecting monotone intervals to `1e-13`.
void collect_cubic_1d_roots_t(
	double d0,
	double d1,
	double d2,
	double d3,
	std::vector<double>& out_t)
{
	if (!std::isfinite(d0) || !std::isfinite(d1) ||
		!std::isfinite(d2) || !std::isfinite(d3)) {
		return;
	}
	std::array<double, 4> breaks = {0.0, 1.0, 0.0, 0.0};
	size_t count = 2u;

	const double a = -d0 + 3.0 * d1 - 3.0 * d2 + d3;
	const double b = d0 - 2.0 * d1 + d2;
	const double c = d1 - d0;
	constexpr double kEps = 1e-12;
	if (std::abs(a) <= kEps) {
		if (std::abs(b) > kEps) {
			const double t = -c / (2.0 * b);
			if (t > 0.0 && t < 1.0 && std::isfinite(t)) {
				breaks[count++] = t;
			}
		}
	} else {
		const double disc = b * b - a * c;
		if (disc >= 0.0 && std::isfinite(disc)) {
			const double s = std::sqrt(disc);
			for (double t : {(-b - s) / a, (-b + s) / a}) {
				if (t > 0.0 && t < 1.0 && std::isfinite(t)) {
					breaks[count++] = t;
				}
			}
		}
	}
	std::sort(breaks.begin(), breaks.begin() + static_cast<std::ptrdiff_t>(count));

	for (size_t i = 0u; i + 1u < count; ++i) {
		double lo = breaks[i];
		double hi = breaks[i + 1u];
		if (hi - lo <= 1e-13) {
			continue;
		}
		double flo = eval_cubic_1d(d0, d1, d2, d3, lo);
		double fhi = eval_cubic_1d(d0, d1, d2, d3, hi);
		if (std::abs(flo) <= 1e-12) {
			if (lo > 0.0 && lo < 1.0) {
				out_t.push_back(lo);
			}
		}
		if (std::abs(fhi) <= 1e-12) {
			if (hi > 0.0 && hi < 1.0) {
				out_t.push_back(hi);
			}
		}
		if ((flo < 0.0 && fhi > 0.0) || (flo > 0.0 && fhi < 0.0)) {
			for (int iter = 0; iter < 52; ++iter) {
				const double mid = 0.5 * (lo + hi);
				const double fmid = eval_cubic_1d(d0, d1, d2, d3, mid);
				if (fmid == 0.0) {
					lo = hi = mid;
					break;
				}
				if ((flo < 0.0 && fmid < 0.0) || (flo > 0.0 && fmid > 0.0)) {
					lo = mid;
					flo = fmid;
				} else {
					hi = mid;
					fhi = fmid;
				}
			}
			const double root = 0.5 * (lo + hi);
			if (root > 0.0 && root < 1.0 && std::isfinite(root)) {
				out_t.push_back(root);
			}
		}
	}
}

// Collects all parameters `t in (0, 1)` where the page-space cubic Bezier
// `(q0, q1, q2, q3)` intersects the line segment `e0 -> e1` or crosses its
// endpoint scanlines `y = e0.y`, `y = e1.y`.
void collect_cubic_segment_intersections_t(
	PdfPoint q0,
	PdfPoint q1,
	PdfPoint q2,
	PdfPoint q3,
	PdfPoint e0,
	PdfPoint e1,
	std::vector<double>& out_t)
{
	const double nx = -(e1.y - e0.y);
	const double ny = e1.x - e0.x;
	const double len = std::hypot(nx, ny);
	if (len > 1e-12 && std::isfinite(len)) {
		const double d0 = (q0.x - e0.x) * nx + (q0.y - e0.y) * ny;
		const double d1 = (q1.x - e0.x) * nx + (q1.y - e0.y) * ny;
		const double d2 = (q2.x - e0.x) * nx + (q2.y - e0.y) * ny;
		const double d3 = (q3.x - e0.x) * nx + (q3.y - e0.y) * ny;
		std::vector<double> line_roots;
		collect_cubic_1d_roots_t(d0, d1, d2, d3, line_roots);
		const double ex = e1.x - e0.x;
		const double ey = e1.y - e0.y;
		const double elen2 = ex * ex + ey * ey;
		for (double t : line_roots) {
			const PdfPoint pt = eval_cubic_2d(q0, q1, q2, q3, t);
			const double u = ((pt.x - e0.x) * ex + (pt.y - e0.y) * ey) / elen2;
			if (u >= -1e-7 && u <= 1.0 + 1e-7) {
				out_t.push_back(t);
			}
		}
	}
	collect_cubic_1d_roots_t(q0.y - e0.y, q1.y - e0.y, q2.y - e0.y, q3.y - e0.y, out_t);
	collect_cubic_1d_roots_t(q0.y - e1.y, q1.y - e1.y, q2.y - e1.y, q3.y - e1.y, out_t);
}

// Collects all angles `theta in [-pi, pi]` where the transformed circle
// `ctm(center + r * (cos theta, sin theta))` intersects segment `e0 -> e1`
// or crosses its endpoint scanlines `y = e0.y`, `y = e1.y`.
void collect_circle_segment_intersection_angles(
	PdfPoint center,
	double r,
	const PdfMatrix& ctm,
	PdfPoint e0,
	PdfPoint e1,
	std::vector<double>& out_angles)
{
	const PdfPoint pc = transform_point(ctm, center);
	const PdfPoint vx{ctm.a * r, ctm.b * r};
	const PdfPoint vy{ctm.c * r, ctm.d * r};
	if (!is_finite_point(pc) || !is_finite_point(vx) || !is_finite_point(vy)) {
		return;
	}

	auto solve_linear_trig = [&](double kc, double ks, double k0, bool check_seg) {
		const double amp = std::hypot(kc, ks);
		if (amp <= 1e-12 || !std::isfinite(amp)) {
			return;
		}
		const double rhs = -k0 / amp;
		if (rhs < -1.0 - 1e-9 || rhs > 1.0 + 1e-9) {
			return;
		}
		const double clamped = std::clamp(rhs, -1.0, 1.0);
		const double phi = std::atan2(ks, kc);
		const double alpha = std::acos(clamped);
		for (double raw : {phi + alpha, phi - alpha}) {
			const double theta = std::atan2(std::sin(raw), std::cos(raw));
			if (check_seg) {
				const double c = std::cos(theta);
				const double s = std::sin(theta);
				const PdfPoint pt{
					pc.x + vx.x * c + vy.x * s,
					pc.y + vx.y * c + vy.y * s};
				const double ex = e1.x - e0.x;
				const double ey = e1.y - e0.y;
				const double elen2 = ex * ex + ey * ey;
				if (elen2 > 1e-18) {
					const double u = ((pt.x - e0.x) * ex + (pt.y - e0.y) * ey) / elen2;
					if (u < -1e-7 || u > 1.0 + 1e-7) {
						continue;
					}
				}
			}
			out_angles.push_back(theta);
		}
	};

	const double nx = -(e1.y - e0.y);
	const double ny = e1.x - e0.x;
	if (std::hypot(nx, ny) > 1e-12) {
		const double k0 = (pc.x - e0.x) * nx + (pc.y - e0.y) * ny;
		const double kc = vx.x * nx + vx.y * ny;
		const double ks = vy.x * nx + vy.y * ny;
		solve_linear_trig(kc, ks, k0, true);
	}
	solve_linear_trig(vx.y, vy.y, pc.y - e0.y, false);
	solve_linear_trig(vx.y, vy.y, pc.y - e1.y, false);
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

// Recursively refines a cubic Bezier interval `[t0, t1]` until its page-space
// midpoint deviation from the chord is `<= 1e-4` pt (capped at depth 6).
void append_adaptive_cubic_points(
	PdfPoint p0,
	PdfPoint cp1,
	PdfPoint cp2,
	PdfPoint p3,
	const PdfMatrix& ctm,
	double t0,
	double t1,
	int depth,
	std::vector<PdfPoint>& out_pts)
{
	const double tm = 0.5 * (t0 + t1);
	const PdfPoint pm = eval_cubic_2d(p0, cp1, cp2, p3, tm);
	const PdfPoint a = transform_point(ctm, eval_cubic_2d(p0, cp1, cp2, p3, t0));
	const PdfPoint b = transform_point(ctm, eval_cubic_2d(p0, cp1, cp2, p3, t1));
	const PdfPoint m = transform_point(ctm, pm);
	const double dev = std::hypot(m.x - 0.5 * (a.x + b.x), m.y - 0.5 * (a.y + b.y));
	if (depth < 6 && dev > 1e-4) {
		append_adaptive_cubic_points(p0, cp1, cp2, p3, ctm, t0, tm, depth + 1, out_pts);
		append_adaptive_cubic_points(p0, cp1, cp2, p3, ctm, tm, t1, depth + 1, out_pts);
	} else {
		out_pts.push_back(eval_cubic_2d(p0, cp1, cp2, p3, t1));
	}
}

// Extracts subpaths in user space, splitting cubic Beziers at their exact
// page-space extrema, their exact intersections with all `reference_clip_edges`
// (and offset edges by `+-stroke_half_width`), and adaptive flatness refinement.
template <typename EdgeT>
std::vector<UserSubpath> extract_user_subpaths(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points,
	const PdfMatrix& ctm,
	std::span<const EdgeT> reference_clip_edges = {},
	double stroke_half_width = 0.0)
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
			ts.reserve(kSteps + 16u + reference_clip_edges.size() * 6u);
			ts.push_back(0.0);
			for (size_t s = 1u; s <= kSteps; ++s) {
				ts.push_back(static_cast<double>(s) / static_cast<double>(kSteps));
			}
			const PdfPoint q0 = transform_point(ctm, p0);
			const PdfPoint q1 = transform_point(ctm, cp1);
			const PdfPoint q2 = transform_point(ctm, cp2);
			const PdfPoint q3 = transform_point(ctm, p3);
			collect_cubic_1d_extrema_t(q0.x, q1.x, q2.x, q3.x, ts);
			collect_cubic_1d_extrema_t(q0.y, q1.y, q2.y, q3.y, ts);

			// Also include user-space X/Y extrema so stroke normal vectors
			// capture axis-aligned tangents before CTM transformation:
			collect_cubic_1d_extrema_t(p0.x, cp1.x, cp2.x, p3.x, ts);
			collect_cubic_1d_extrema_t(p0.y, cp1.y, cp2.y, p3.y, ts);

			for (const EdgeT& ce : reference_clip_edges) {
				collect_cubic_segment_intersections_t(q0, q1, q2, q3, ce.p0, ce.p1, ts);
				if (stroke_half_width > 0.0) {
					// Also split where the curve approaches within stroke_half_width
					// of the clip edge in user space (approximated via CTM-transformed
					// cardinal and chord normal offsets):
					const PdfPoint chord_u{p3.x - p0.x, p3.y - p0.y};
					const double clen = std::hypot(chord_u.x, chord_u.y);
					if (clen > kGeomEps) {
						const PdfPoint nu{
							-chord_u.y / clen * stroke_half_width,
							chord_u.x / clen * stroke_half_width};
						const PdfPoint dp{
							ctm.a * nu.x + ctm.c * nu.y,
							ctm.b * nu.x + ctm.d * nu.y};
						if (is_finite_point(dp)) {
							collect_cubic_segment_intersections_t(
								PdfPoint{q0.x + dp.x, q0.y + dp.y},
								PdfPoint{q1.x + dp.x, q1.y + dp.y},
								PdfPoint{q2.x + dp.x, q2.y + dp.y},
								PdfPoint{q3.x + dp.x, q3.y + dp.y},
								ce.p0,
								ce.p1,
								ts);
							collect_cubic_segment_intersections_t(
								PdfPoint{q0.x - dp.x, q0.y - dp.y},
								PdfPoint{q1.x - dp.x, q1.y - dp.y},
								PdfPoint{q2.x - dp.x, q2.y - dp.y},
								PdfPoint{q3.x - dp.x, q3.y - dp.y},
								ce.p0,
								ce.p1,
								ts);
						}
					}
				}
			}

			std::sort(ts.begin(), ts.end());

			double prev_t = 0.0;
			for (double t : ts) {
				const double clamped_t = std::clamp(t, 0.0, 1.0);
				if (clamped_t - prev_t > 1e-11) {
					append_adaptive_cubic_points(
						p0,
						cp1,
						cp2,
						p3,
						ctm,
						prev_t,
						clamped_t,
						0,
						cur_sub.points);
					prev_t = clamped_t;
				}
			}
			if (1.0 - prev_t > 1e-11) {
				append_adaptive_cubic_points(
					p0,
					cp1,
					cp2,
					p3,
					ctm,
					prev_t,
					1.0,
					0,
					cur_sub.points);
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

// Decomposes a single `UserSubpath` according to PDF dash semantics
// (ISO 32000-1 §8.4.3.6: odd-length pattern arrays are doubled to form an
// even on/off cycle, `dash_phase` advances into the cycle before the subpath
// begins, and each painted "on" interval becomes an open `UserSubpath`).
std::vector<UserSubpath> apply_dash_pattern(
	const UserSubpath& sp,
	std::span<const float> dash_array,
	float dash_phase)
{
	if (dash_array.empty()) {
		std::vector<UserSubpath> out;
		out.push_back(sp);
		return out;
	}
	if (!sp.has_draw_verb || sp.points.empty()) {
		return {};
	}

	std::vector<double> pattern;
	const size_t n = dash_array.size();
	pattern.reserve((n % 2u == 1u) ? (2u * n) : n);
	for (float d : dash_array) {
		pattern.push_back(static_cast<double>(d));
	}
	if (n % 2u == 1u) {
		for (float d : dash_array) {
			pattern.push_back(static_cast<double>(d));
		}
	}

	double cycle_len = 0.0;
	for (double d : pattern) {
		cycle_len += d;
	}
	if (!(cycle_len > 0.0) || !std::isfinite(cycle_len)) {
		return {};
	}

	double phase = std::fmod(static_cast<double>(dash_phase), cycle_len);
	if (phase < 0.0) {
		phase += cycle_len;
	}

	size_t pat_idx = 0u;
	double pat_rem = pattern[0];
	while (phase > 0.0 && pat_idx < pattern.size()) {
		const double d = pattern[pat_idx];
		if (phase < d - 1e-12) {
			pat_rem = d - phase;
			phase = 0.0;
			break;
		}
		phase -= d;
		pat_idx = (pat_idx + 1u) % pattern.size();
		pat_rem = pattern[pat_idx];
	}
	// Advance past any zero-length initial entries if we are in an off-phase:
	while (pat_rem <= 1e-12) {
		pat_idx = (pat_idx + 1u) % pattern.size();
		pat_rem = pattern[pat_idx];
	}

	std::vector<UserSubpath> pieces;
	UserSubpath cur_piece;

	auto flush_piece = [&]() {
		if (cur_piece.points.size() >= 2u ||
			(cur_piece.points.size() == 1u && cur_piece.has_draw_verb)) {
			cur_piece.closed = false;
			pieces.push_back(std::move(cur_piece));
		}
		cur_piece = UserSubpath{};
	};

	const size_t seg_count =
		sp.closed ? sp.points.size() : (sp.points.size() - 1u);
	bool any_non_zero_seg = false;

	for (size_t i = 0u; i < seg_count; ++i) {
		const PdfPoint a = sp.points[i];
		const PdfPoint b = sp.points[(i + 1u) % sp.points.size()];
		const double seg_len = std::hypot(b.x - a.x, b.y - a.y);
		if (seg_len <= kGeomEps) {
			continue;
		}
		any_non_zero_seg = true;

		double pos = 0.0;
		while (pos < seg_len - 1e-12) {
			const bool is_on = (pat_idx % 2u) == 0u;
			if (pat_rem <= 1e-12) {
				if (is_on) {
					// Zero-length "on" dash produces a point (visible under Round/Square cap):
					const double t = pos / seg_len;
					const PdfPoint pt{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
					UserSubpath dot_piece;
					dot_piece.points.push_back(pt);
					dot_piece.points.push_back(pt);
					dot_piece.closed = false;
					dot_piece.has_draw_verb = true;
					pieces.push_back(std::move(dot_piece));
				}
				pat_idx = (pat_idx + 1u) % pattern.size();
				pat_rem = pattern[pat_idx];
				continue;
			}

			const double step = std::min(seg_len - pos, pat_rem);
			const double t0 = pos / seg_len;
			const double t1 = (pos + step) / seg_len;
			const PdfPoint p0{a.x + t0 * (b.x - a.x), a.y + t0 * (b.y - a.y)};
			const PdfPoint p1{a.x + t1 * (b.x - a.x), a.y + t1 * (b.y - a.y)};

			if (is_on) {
				if (cur_piece.points.empty()) {
					cur_piece.points.push_back(p0);
				}
				cur_piece.points.push_back(p1);
				cur_piece.has_draw_verb = true;
			} else {
				flush_piece();
			}

			pos += step;
			pat_rem -= step;
			if (pat_rem <= 1e-12) {
				if (is_on) {
					flush_piece();
				}
				pat_idx = (pat_idx + 1u) % pattern.size();
				pat_rem = pattern[pat_idx];
			}
		}
	}

	if (!any_non_zero_seg) {
		if ((pat_idx % 2u) == 0u) {
			UserSubpath dot_piece;
			dot_piece.points.push_back(sp.points.front());
			dot_piece.points.push_back(sp.points.front());
			dot_piece.closed = false;
			dot_piece.has_draw_verb = true;
			pieces.push_back(std::move(dot_piece));
		}
		return pieces;
	}

	flush_piece();
	return pieces;
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

void DisplayListBuilder::BoundsAccumulator::include(const PdfRect& r) noexcept
{
	if (!is_valid_rect(r)) {
		return;
	}
	if (!has_bounds) {
		bounds = r;
		has_bounds = true;
	} else {
		bounds = union_rect(bounds, r);
	}
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
	if (has_content_bounds_) {
		content_bounds_pt_ = PdfRect{};
		has_content_bounds_ = false;
	}
	if (!page_box_pt_.is_empty()) {
		try {
			auto page_clip = std::make_shared<ClipRegion>();
			page_clip->fill_rule = FillRule::NonZero;
			page_clip->bbox = page_box_pt_;
			page_clip->has_bbox = true;
			const PdfPoint p0{page_box_pt_.x_min, page_box_pt_.y_min};
			const PdfPoint p1{page_box_pt_.x_max, page_box_pt_.y_min};
			const PdfPoint p2{page_box_pt_.x_max, page_box_pt_.y_max};
			const PdfPoint p3{page_box_pt_.x_min, page_box_pt_.y_max};
			page_clip->edges.push_back(ClipEdge{p0, p1});
			page_clip->edges.push_back(ClipEdge{p1, p2});
			page_clip->edges.push_back(ClipEdge{p2, p3});
			page_clip->edges.push_back(ClipEdge{p3, p0});
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
	size_t total =
		commands_.size() * sizeof(DisplayCommand) +
		paths_.size() * sizeof(RecordedPath) +
		path_verbs_.size() * sizeof(PathVerb) +
		path_points_.size() * sizeof(PdfPoint) +
		glyphs_.size() * sizeof(PositionedGlyph) +
		dash_values_.size() * sizeof(float);

	// Account for retained tracking state (active state + saved state stack):
	total += current_state_.dash_array.size() * sizeof(float);
	total += current_state_.clips.size() * sizeof(std::shared_ptr<const ClipRegion>);
	for (const auto& clip : current_state_.clips) {
		if (clip) {
			total += sizeof(ClipRegion) + clip->edges.size() * sizeof(ClipEdge);
		}
	}

	total += state_stack_.size() * sizeof(TrackingState);
	for (const TrackingState& st : state_stack_) {
		total += st.dash_array.size() * sizeof(float);
		total += st.clips.size() * sizeof(std::shared_ptr<const ClipRegion>);
		for (const auto& clip : st.clips) {
			if (clip) {
				// Avoid double-counting ClipRegion objects already present in `current_state_`:
				bool shared_with_current = false;
				for (const auto& cur_clip : current_state_.clips) {
					if (cur_clip.get() == clip.get()) {
						shared_with_current = true;
						break;
					}
				}
				if (!shared_with_current) {
					total += sizeof(ClipRegion) + clip->edges.size() * sizeof(ClipEdge);
				}
			}
		}
	}
	return total;
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
			ErrorCode::DamagedFormat,
			"DisplayListBuilder geometry work budget exceeded");
	}
	geometry_work_ops_ += ops;
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
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
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

	size_t verb_bytes = 0u;
	size_t point_bytes = 0u;
	size_t total_add = 0u;
	if (!checked_mul_size(verbs.size(), sizeof(PathVerb), verb_bytes) ||
		!checked_mul_size(points.size(), sizeof(PdfPoint), point_bytes) ||
		!checked_add_size(verb_bytes, point_bytes, total_add) ||
		!checked_add_size(total_add, sizeof(RecordedPath), total_add)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::add_path byte size overflow");
	}
	if (auto ok = check_additional_bytes(total_add); !ok.has_value()) {
		return ok.error();
	}
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
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
	const size_t state_copy_bytes =
		sizeof(TrackingState) +
		current_state_.dash_array.size() * sizeof(float) +
		current_state_.clips.size() * sizeof(std::shared_ptr<const ClipRegion>) +
		sizeof(DisplayCommand);
	if (auto ok = check_additional_bytes(state_copy_bytes); !ok.has_value()) {
		return ok;
	}
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}

	try {
		commands_.reserve(commands_.size() + 1u);
		state_stack_.reserve(state_stack_.size() + 1u);
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
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
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
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}

	try {
		commands_.reserve(commands_.size() + 1u);
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
	if (auto ok = check_additional_bytes(2u * dash_bytes + sizeof(DisplayCommand));
		!ok.has_value()) {
		return ok;
	}
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
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
		std::vector<float> new_state_dashes(dash_array.begin(), dash_array.end());
		dash_values_.reserve(dash_values_.size() + dash_array.size());
		commands_.reserve(commands_.size() + 1u);
		dash_values_.insert(dash_values_.end(), dash_array.begin(), dash_array.end());
		commands_.emplace_back(cmd);
		current_state_.line_width = line_width;
		current_state_.line_cap = line_cap;
		current_state_.line_join = line_join;
		current_state_.miter_limit = miter_limit;
		current_state_.dash_array = std::move(new_state_dashes);
		current_state_.dash_phase = dash_phase;
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
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}

	SetAlphaAndBlendCommand cmd{};
	cmd.stroke_alpha = stroke_alpha;
	cmd.fill_alpha = fill_alpha;
	cmd.blend_mode = blend_mode;

	try {
		commands_.reserve(commands_.size() + 1u);
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
	if (t >= 0.0 && t <= 1.0 && u >= -1e-9 && u <= 1.0 + 1e-9 && std::isfinite(t)) {
		ts.push_back(t);
	}
}

// Computes coalesced non-transparent coverage rectangles in unit image space
// `[0, 1] x [0, 1]` for `bitmap` combined with optional `soft_mask`.
std::vector<PdfRect> compute_image_coverage_rects(
	const DecodedBitmap& bitmap,
	const DecodedBitmap* soft_mask)
{
	const uint32_t w = bitmap.width;
	const uint32_t h = bitmap.height;
	const double inv_w = 1.0 / static_cast<double>(w);
	const double inv_h = 1.0 / static_cast<double>(h);

	auto mask_overlaps_non_transparent = [&](uint32_t bx, uint32_t by) noexcept -> bool {
		if (!soft_mask) {
			return true;
		}
		if (!soft_mask->has_non_transparent_pixels) {
			return false;
		}
		const uint32_t mw = soft_mask->width;
		const uint32_t mh = soft_mask->height;
		if (mw == w && mh == h) {
			const uint8_t* mrow =
				soft_mask->pixels.data() + static_cast<size_t>(by) * soft_mask->stride;
			return mrow[static_cast<size_t>(bx) * 4u + 3u] != 0u;
		}
		const uint32_t mx0 = static_cast<uint32_t>(
			(static_cast<uint64_t>(bx) * mw) / w);
		uint32_t mx1 = static_cast<uint32_t>(
			(static_cast<uint64_t>(bx + 1u) * mw + w - 1u) / w);
		if (mx1 <= mx0) {
			mx1 = std::min(mw, mx0 + 1u);
		}
		const uint32_t my0 = static_cast<uint32_t>(
			(static_cast<uint64_t>(by) * mh) / h);
		uint32_t my1 = static_cast<uint32_t>(
			(static_cast<uint64_t>(by + 1u) * mh + h - 1u) / h);
		if (my1 <= my0) {
			my1 = std::min(mh, my0 + 1u);
		}
		for (uint32_t my = my0; my < my1; ++my) {
			const uint8_t* mrow =
				soft_mask->pixels.data() + static_cast<size_t>(my) * soft_mask->stride;
			for (uint32_t mx = mx0; mx < mx1; ++mx) {
				if (mrow[static_cast<size_t>(mx) * 4u + 3u] != 0u) {
					return true;
				}
			}
		}
		return false;
	};

	struct ActiveSpan {
		uint32_t x0{0};
		uint32_t x1{0};
		uint32_t y0{0};
		uint32_t y1{0};
	};

	std::vector<ActiveSpan> active;
	std::vector<ActiveSpan> next_active;
	std::vector<PdfRect> rects;

	auto emit_span = [&](const ActiveSpan& s) {
		// In PDF image space `[0,1]x[0,1]`, row 0 of a top-down bitmap is at the
		// top (`y in [1 - (y1)/h, 1 - (y0)/h]`), except when `h == 1`:
		const double x_min = static_cast<double>(s.x0) * inv_w;
		const double x_max = static_cast<double>(s.x1) * inv_w;
		const double y_min = static_cast<double>(s.y0) * inv_h;
		const double y_max = static_cast<double>(s.y1) * inv_h;
		rects.push_back(PdfRect{x_min, y_min, x_max, y_max});
	};

	for (uint32_t y = 0u; y < h; ++y) {
		const uint8_t* row = bitmap.pixels.data() + static_cast<size_t>(y) * bitmap.stride;
		next_active.clear();
		uint32_t x = 0u;
		while (x < w) {
			while (x < w &&
				(row[static_cast<size_t>(x) * 4u + 3u] == 0u ||
				 !mask_overlaps_non_transparent(x, y))) {
				++x;
			}
			if (x >= w) {
				break;
			}
			const uint32_t x_start = x;
			while (x < w &&
				row[static_cast<size_t>(x) * 4u + 3u] != 0u &&
				mask_overlaps_non_transparent(x, y)) {
				++x;
			}
			const uint32_t x_end = x;

			bool merged = false;
			for (ActiveSpan& act : active) {
				if (act.x0 == x_start && act.x1 == x_end && act.y1 == y) {
					next_active.push_back(ActiveSpan{x_start, x_end, act.y0, y + 1u});
					act.y1 = 0u; // mark consumed
					merged = true;
					break;
				}
			}
			if (!merged) {
				next_active.push_back(ActiveSpan{x_start, x_end, y, y + 1u});
			}
		}
		for (const ActiveSpan& act : active) {
			if (act.y1 != 0u) {
				emit_span(act);
			}
		}
		active.swap(next_active);
	}
	for (const ActiveSpan& act : active) {
		if (act.y1 != 0u) {
			emit_span(act);
		}
	}
	return rects;
}

} // namespace

PdfResult<void> DisplayListBuilder::accumulate_clipped_region(
	std::span<const ClipEdge> shape_edges,
	FillRule shape_rule,
	BoundsAccumulator& accum) noexcept
{
	const auto shape_bbox = compute_edges_bbox(shape_edges);
	if (!shape_bbox.has_value()) {
		return {};
	}

	PdfRect window = *shape_bbox;
	for (const auto& clip_ptr : current_state_.clips) {
		if (!clip_ptr || !clip_ptr->has_bbox) {
			return {};
		}
		const auto inter = intersect_rect(window, clip_ptr->bbox);
		if (!inter.has_value()) {
			return {};
		}
		window = *inter;
	}

	size_t total_edge_count = shape_edges.size();
	for (const auto& clip_ptr : current_state_.clips) {
		if (!checked_add_size(total_edge_count, clip_ptr->edges.size(), total_edge_count)) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder clip edge count overflow");
		}
	}

	size_t pair_ops = 0u;
	if (!checked_mul_size(total_edge_count, total_edge_count, pair_ops)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder geometry work overflow");
	}
	if (auto ok = charge_geometry_work(pair_ops); !ok.has_value()) {
		return ok;
	}

	const size_t max_breaks = total_edge_count * 2u + pair_ops / 2u + 4u;
	const size_t scratch_bytes =
		total_edge_count * sizeof(ClipEdge) +
		max_breaks * sizeof(double) +
		total_edge_count * sizeof(double) * 3u;
	if (auto ok = check_additional_bytes(scratch_bytes); !ok.has_value()) {
		return ok;
	}
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}

	try {
		std::vector<ClipEdge> all_edges;
		all_edges.reserve(total_edge_count);
		all_edges.insert(all_edges.end(), shape_edges.begin(), shape_edges.end());
		for (const auto& clip_ptr : current_state_.clips) {
			all_edges.insert(all_edges.end(), clip_ptr->edges.begin(), clip_ptr->edges.end());
		}

		std::vector<double> y_breaks;
		y_breaks.reserve(total_edge_count * 2u + 16u);
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
			if (auto ok = charge_geometry_work(all_edges.size()); !ok.has_value()) {
				return ok;
			}
			const double ym = 0.5 * (y0 + y1);

			crossings.clear();
			for (const ClipEdge& e : all_edges) {
				const double ey_min = std::min(e.p0.y, e.p1.y);
				const double ey_max = std::max(e.p0.y, e.p1.y);
				const double dy = e.p1.y - e.p0.y;
				if (std::abs(dy) <= kGeomEps || ey_min > ym || ey_max < ym) {
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
				for (const auto& clip_ptr : current_state_.clips) {
					if (!point_in_edges<ClipEdge>(
							test_pt,
							clip_ptr->edges,
							clip_ptr->fill_rule)) {
						inside_all_clips = false;
						break;
					}
				}
				if (inside_all_clips) {
					const double tx_min = std::min({left.x0, left.x1, right.x0, right.x1});
					const double tx_max = std::max({left.x0, left.x1, right.x0, right.x1});
					accum.include(PdfRect{tx_min, y0, tx_max, y1});
				}
			}
		}
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::accumulate_clipped_region");
	}
}

PdfResult<void> DisplayListBuilder::accumulate_clipped_segment(
	PdfPoint p0,
	PdfPoint p1,
	BoundsAccumulator& accum) noexcept
{
	if (!is_finite_point(p0) || !is_finite_point(p1)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::accumulate_clipped_segment encountered non-finite segment");
	}
	size_t clip_edge_count = 0u;
	for (const auto& clip_ptr : current_state_.clips) {
		if (!clip_ptr || !clip_ptr->has_bbox) {
			return {};
		}
		clip_edge_count += clip_ptr->edges.size();
	}

	if (auto ok = charge_geometry_work(clip_edge_count + 2u); !ok.has_value()) {
		return ok;
	}
	if (auto ok = check_additional_bytes((clip_edge_count + 2u) * sizeof(double));
		!ok.has_value()) {
		return ok;
	}
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}

	try {
		std::vector<double> ts;
		ts.reserve(clip_edge_count + 2u);
		ts.push_back(0.0);
		ts.push_back(1.0);
		for (const auto& clip_ptr : current_state_.clips) {
			for (const ClipEdge& e : clip_ptr->edges) {
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
			for (const auto& clip_ptr : current_state_.clips) {
				if (!point_in_edges<ClipEdge>(mid, clip_ptr->edges, clip_ptr->fill_rule)) {
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
				accum.include(PdfRect{
					std::min(q0.x, q1.x),
					std::min(q0.y, q1.y),
					std::max(q0.x, q1.x),
					std::max(q0.y, q1.y)});
			}
		}
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::accumulate_clipped_segment");
	}
}

PdfResult<void> DisplayListBuilder::accumulate_clipped_rect(
	const PdfRect& local_rect,
	const PdfMatrix& to_page,
	BoundsAccumulator& accum) noexcept
{
	if (!validate_transformed_rect_finite(local_rect, to_page)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::accumulate_clipped_rect transformed coordinates overflowed");
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
	return accumulate_clipped_region(edges, FillRule::NonZero, accum);
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
	// Subtract the implicit initial page_box clip when comparing against `max_clip_depth`:
	const size_t user_clip_depth =
		(!page_box_pt_.is_empty() && !current_state_.clips.empty())
			? (current_state_.clips.size() - 1u)
			: current_state_.clips.size();
	if (user_clip_depth >= limits_.max_clip_depth) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::push_clip_path exceeded max_clip_depth");
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}

	const RecordedPath& p = paths_[path_index];
	const auto verbs = std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	const auto points = std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);

	if (!validate_transformed_path_finite(verbs, points, current_state_.ctm)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::push_clip_path transformed coordinates overflowed");
	}
	if (auto ok = charge_geometry_work(verbs.size() + points.size() + 1u); !ok.has_value()) {
		return ok;
	}
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}

	try {
		std::vector<ClipEdge> ref_edges;
		for (const auto& clip_ptr : current_state_.clips) {
			if (clip_ptr) {
				ref_edges.insert(
					ref_edges.end(),
					clip_ptr->edges.begin(),
					clip_ptr->edges.end());
			}
		}

		auto region = std::make_shared<ClipRegion>();
		region->fill_rule = fill_rule;
		const auto subpaths = extract_user_subpaths<ClipEdge>(
			verbs,
			points,
			current_state_.ctm,
			ref_edges,
			0.0);
		for (const UserSubpath& sp : subpaths) {
			if (sp.points.size() < 2u || !sp.has_draw_verb) {
				continue;
			}
			for (size_t i = 0u; i < sp.points.size(); ++i) {
				const PdfPoint a = transform_point(current_state_.ctm, sp.points[i]);
				const PdfPoint b = transform_point(
					current_state_.ctm,
					sp.points[(i + 1u) % sp.points.size()]);
				if (!is_finite_point(a) || !is_finite_point(b)) {
					return make_pdf_error(
						ErrorCode::DamagedFormat,
						"DisplayListBuilder::push_clip_path transformed clip edge is non-finite");
				}
				if (std::hypot(b.x - a.x, b.y - a.y) > kGeomEps) {
					region->edges.push_back(ClipEdge{a, b});
				}
			}
		}
		if (const auto bbox = compute_edges_bbox<ClipEdge>(region->edges)) {
			region->bbox = *bbox;
			region->has_bbox = true;
		}

		const size_t new_bytes =
			sizeof(DisplayCommand) +
			sizeof(std::shared_ptr<const ClipRegion>) +
			sizeof(ClipRegion) +
			region->edges.size() * sizeof(ClipEdge);
		if (auto ok = check_additional_bytes(new_bytes); !ok.has_value()) {
			return ok;
		}

		commands_.reserve(commands_.size() + 1u);
		current_state_.clips.reserve(current_state_.clips.size() + 1u);
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

	const RecordedPath& p = paths_[path_index];
	const auto verbs = std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	const auto points = std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);

	if (!validate_transformed_path_finite(verbs, points, current_state_.ctm)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_path transformed path coordinates overflowed");
	}

	const bool visible_fill =
		fill && (current_state_.fill_alpha > 0.0f) && (fill_color.a > 0.0f);
	const bool visible_stroke =
		stroke && (current_state_.stroke_alpha > 0.0f) && (stroke_color.a > 0.0f);

	BoundsAccumulator local_accum{};

	if (visible_fill || visible_stroke) {
		try {
			std::vector<ClipEdge> ref_clip_edges;
			for (const auto& clip_ptr : current_state_.clips) {
				if (clip_ptr) {
					ref_clip_edges.insert(
						ref_clip_edges.end(),
						clip_ptr->edges.begin(),
						clip_ptr->edges.end());
				}
			}

			const double stroke_r = visible_stroke
				? (0.5 * static_cast<double>(current_state_.line_width))
				: 0.0;
			const auto subpaths = extract_user_subpaths<ClipEdge>(
				verbs,
				points,
				current_state_.ctm,
				ref_clip_edges,
				stroke_r);

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
						if (!is_finite_point(a) || !is_finite_point(b)) {
							return make_pdf_error(
								ErrorCode::DamagedFormat,
								"DisplayListBuilder::draw_path fill edge overflowed");
						}
						if (std::hypot(b.x - a.x, b.y - a.y) > kGeomEps) {
							fill_edges.push_back(ClipEdge{a, b});
						}
					}
				}
				if (!fill_edges.empty()) {
					if (auto ok = accumulate_clipped_region(fill_edges, fill_rule, local_accum);
						!ok.has_value()) {
						return ok;
					}
				}
			}

			if (visible_stroke) {
				std::vector<UserSubpath> stroke_subpaths;
				if (current_state_.dash_array.empty()) {
					stroke_subpaths = subpaths;
				} else {
					for (const UserSubpath& sp : subpaths) {
						auto pieces = apply_dash_pattern(
							sp,
							current_state_.dash_array,
							current_state_.dash_phase);
						for (UserSubpath& piece : pieces) {
							stroke_subpaths.push_back(std::move(piece));
						}
					}
				}

				const double w = static_cast<double>(current_state_.line_width);
				if (w <= 0.0) {
					for (const UserSubpath& sp : stroke_subpaths) {
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
							if (auto ok = accumulate_clipped_segment(a, b, local_accum);
								!ok.has_value()) {
								return ok;
							}
						}
					}
				} else {
					const double r = 0.5 * w;
					const PdfMatrix& ctm = current_state_.ctm;

					auto emit_user_poly = [&](std::span<const PdfPoint> user_pts) -> PdfResult<void> {
						if (user_pts.size() < 3u) {
							return {};
						}
						for (const PdfPoint& up : user_pts) {
							if (!is_finite_point(up) || !is_finite_point(transform_point(ctm, up))) {
								return make_pdf_error(
									ErrorCode::DamagedFormat,
									"DisplayListBuilder::draw_path stroke polygon overflowed");
							}
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
							return accumulate_clipped_region(edges, FillRule::NonZero, local_accum);
						}
						return {};
					};

					const double tx = std::atan2(ctm.c, ctm.a);
					const double ty = std::atan2(ctm.d, ctm.b);
					constexpr double kPi = std::numbers::pi;
					const std::array<double, 4> crit_angles = {
						tx,
						tx + kPi,
						ty,
						ty + kPi,
					};

					auto emit_user_disk = [&](PdfPoint center) -> PdfResult<void> {
						std::vector<double> angles;
						constexpr size_t kCircleSteps = 32u;
						angles.reserve(kCircleSteps + 8u + ref_clip_edges.size() * 6u);
						for (size_t s = 0u; s < kCircleSteps; ++s) {
							angles.push_back(
								-kPi + (2.0 * kPi * static_cast<double>(s)) /
								static_cast<double>(kCircleSteps));
						}
						for (double ca : crit_angles) {
							angles.push_back(std::atan2(std::sin(ca), std::cos(ca)));
						}
						for (const ClipEdge& ce : ref_clip_edges) {
							collect_circle_segment_intersection_angles(
								center,
								r,
								ctm,
								ce.p0,
								ce.p1,
								angles);
						}
						std::sort(angles.begin(), angles.end());

						std::vector<PdfPoint> poly;
						poly.reserve(angles.size());
						for (size_t i = 0u; i < angles.size(); ++i) {
							if (i > 0u && angles[i] - angles[i - 1u] <= 1e-11) {
								continue;
							}
							poly.emplace_back(
								center.x + r * std::cos(angles[i]),
								center.y + r * std::sin(angles[i]));
						}
						return emit_user_poly(poly);
					};

					auto emit_user_arc_fan = [&](
						PdfPoint center,
						PdfPoint start_unit,
						PdfPoint end_unit,
						PdfPoint halfspace_dir,
						bool has_second_halfspace = false,
						PdfPoint second_dir = PdfPoint{}) -> PdfResult<void> {
						std::vector<double> angles;
						constexpr size_t kArcSteps = 32u;
						angles.reserve(kArcSteps + 8u + ref_clip_edges.size() * 6u);
						angles.push_back(std::atan2(start_unit.y, start_unit.x));
						angles.push_back(std::atan2(end_unit.y, end_unit.x));
						for (size_t s = 0u; s < kArcSteps; ++s) {
							const double a =
								-kPi + (2.0 * kPi * static_cast<double>(s)) /
								static_cast<double>(kArcSteps);
							angles.push_back(a);
						}
						for (double ca : crit_angles) {
							angles.push_back(std::atan2(std::sin(ca), std::cos(ca)));
						}
						for (const ClipEdge& ce : ref_clip_edges) {
							collect_circle_segment_intersection_angles(
								center,
								r,
								ctm,
								ce.p0,
								ce.p1,
								angles);
						}
						std::sort(angles.begin(), angles.end());

						std::vector<PdfPoint> poly;
						poly.reserve(angles.size() + 1u);
						poly.push_back(center);
						for (size_t i = 0u; i < angles.size(); ++i) {
							if (i > 0u && angles[i] - angles[i - 1u] <= 1e-11) {
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
							return emit_user_poly(poly);
						}
						return {};
					};

					struct StrokeSeg {
						PdfPoint a{};
						PdfPoint b{};
						PdfPoint t{};
						PdfPoint n{};
					};

					for (const UserSubpath& sp : stroke_subpaths) {
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
								if (auto ok = emit_user_disk(sp.points.front()); !ok.has_value()) {
									return ok;
								}
							} else if (current_state_.line_cap == LineCap::ProjectingSquare) {
								const PdfPoint c = sp.points.front();
								const std::array<PdfPoint, 4> sq = {
									PdfPoint{c.x - r, c.y - r},
									PdfPoint{c.x + r, c.y - r},
									PdfPoint{c.x + r, c.y + r},
									PdfPoint{c.x - r, c.y + r},
								};
								if (auto ok = emit_user_poly(sq); !ok.has_value()) {
									return ok;
								}
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
							if (auto ok = emit_user_poly(quad); !ok.has_value()) {
								return ok;
							}
						}

						// 2. Open-endpoint caps:
						if (!sp.closed) {
							const auto emit_cap = [&](PdfPoint p, PdfPoint t_out, PdfPoint n)
								-> PdfResult<void> {
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
									return emit_user_poly(cap_quad);
								}
								if (current_state_.line_cap == LineCap::Round) {
									return emit_user_arc_fan(
										p,
										PdfPoint{-n.x, -n.y},
										n,
										t_out);
								}
								return {};
							};

							const StrokeSeg& first = segs.front();
							if (auto ok = emit_cap(
									first.a,
									PdfPoint{-first.t.x, -first.t.y},
									PdfPoint{-first.n.x, -first.n.y});
								!ok.has_value()) {
								return ok;
							}

							const StrokeSeg& last = segs.back();
							if (auto ok = emit_cap(last.b, last.t, last.n); !ok.has_value()) {
								return ok;
							}
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
							if (auto ok = emit_user_poly(bevel_tri); !ok.has_value()) {
								return ok;
							}

							if (current_state_.line_join == LineJoin::Round) {
								if (auto ok = emit_user_arc_fan(
										v,
										u1,
										u2,
										s_in.t,
										true,
										PdfPoint{-s_out.t.x, -s_out.t.y});
									!ok.has_value()) {
									return ok;
								}
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
									if (auto ok = emit_user_poly(miter_quad); !ok.has_value()) {
										return ok;
									}
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
	}

	if (auto ok = charge_allocation_op(); !ok.has_value()) {
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
		commands_.reserve(commands_.size() + 1u);
		commands_.emplace_back(cmd);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::draw_path");
	}

	if (local_accum.has_bounds) {
		if (!has_content_bounds_) {
			content_bounds_pt_ = local_accum.bounds;
			has_content_bounds_ = true;
		} else {
			content_bounds_pt_ = union_rect(content_bounds_pt_, local_accum.bounds);
		}
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

	const PdfMatrix combined = recto::pdf::concat_matrix(text_matrix, current_state_.ctm);
	if (!is_finite_matrix(combined)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run produced non-finite text-to-page matrix");
	}

	for (const PositionedGlyph& g : glyphs) {
		if (!is_finite_point(g.origin_pt) ||
			!std::isfinite(g.advance_pt) ||
			!is_finite_point(transform_point(combined, g.origin_pt))) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayListBuilder::draw_glyph_run glyph origin and advance must be finite in text and page space");
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

	std::optional<PdfRect> computed_local_bounds;
	std::vector<PdfRect> per_glyph_local_rects;

	try {
		if (type3_sub_list != nullptr) {
			if (type3_sub_list->has_content_bounds()) {
				const PdfRect& sub_box = type3_sub_list->content_bounds_pt();
				per_glyph_local_rects.reserve(glyphs.size());
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
					per_glyph_local_rects.push_back(placed);
					computed_local_bounds = computed_local_bounds.has_value()
						? union_rect(*computed_local_bounds, placed)
						: placed;
				}
			}
		} else {
			bool all_have_glyph_bounds = true;
			per_glyph_local_rects.reserve(glyphs.size());
			for (const PositionedGlyph& g : glyphs) {
				if (g.is_blank) {
					continue;
				}
				if (!g.ink_bounds_pt.has_value()) {
					all_have_glyph_bounds = false;
					break;
				}
				const PdfRect placed{
					g.origin_pt.x + g.ink_bounds_pt->x_min,
					g.origin_pt.y + g.ink_bounds_pt->y_min,
					g.origin_pt.x + g.ink_bounds_pt->x_max,
					g.origin_pt.y + g.ink_bounds_pt->y_max};
				per_glyph_local_rects.push_back(placed);
				computed_local_bounds = computed_local_bounds.has_value()
					? union_rect(*computed_local_bounds, placed)
					: placed;
			}

			if (!all_have_glyph_bounds) {
				per_glyph_local_rects.clear();
				if (local_bounds_pt.has_value()) {
					if (!validate_transformed_rect_finite(*local_bounds_pt, combined)) {
						return make_pdf_error(
							ErrorCode::DamagedFormat,
							"DisplayListBuilder::draw_glyph_run local_bounds_pt overflowed under CTM");
					}
					computed_local_bounds = *local_bounds_pt;
					per_glyph_local_rects.push_back(*local_bounds_pt);
				} else if (mode_fills || mode_strokes) {
					return make_pdf_error(
						ErrorCode::DamagedFormat,
						"DisplayListBuilder::draw_glyph_run visible glyph requires ink_bounds_pt, is_blank, local_bounds_pt, or type3_sub_list");
				}
			} else if (local_bounds_pt.has_value()) {
				if (!validate_transformed_rect_finite(*local_bounds_pt, combined)) {
					return make_pdf_error(
						ErrorCode::DamagedFormat,
						"DisplayListBuilder::draw_glyph_run local_bounds_pt overflowed under CTM");
				}
				computed_local_bounds = *local_bounds_pt;
			}
		}
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::draw_glyph_run");
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

	size_t glyph_bytes = 0u;
	size_t total_add = 0u;
	if (!checked_mul_size(glyphs.size(), sizeof(PositionedGlyph), glyph_bytes) ||
		!checked_add_size(glyph_bytes, sizeof(DisplayCommand), total_add)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_glyph_run byte size overflow");
	}
	if (auto ok = check_additional_bytes(total_add); !ok.has_value()) {
		return ok;
	}

	const bool visible_fill =
		mode_fills && (current_state_.fill_alpha > 0.0f) && (fill_color.a > 0.0f);
	const bool visible_stroke =
		mode_strokes && (current_state_.stroke_alpha > 0.0f) && (stroke_color.a > 0.0f);

	BoundsAccumulator local_accum{};
	if ((visible_fill || visible_stroke) && !per_glyph_local_rects.empty()) {
		const double pad = (visible_stroke && current_state_.line_width > 0.0f)
			? (0.5 * static_cast<double>(current_state_.line_width))
			: 0.0;
		for (PdfRect effective_local : per_glyph_local_rects) {
			if (pad > 0.0) {
				effective_local.x_min -= pad;
				effective_local.y_min -= pad;
				effective_local.x_max += pad;
				effective_local.y_max += pad;
			}
			if (auto ok = accumulate_clipped_rect(effective_local, combined, local_accum);
				!ok.has_value()) {
				return ok;
			}
		}
	}

	if (auto ok = charge_allocation_op(); !ok.has_value()) {
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

	if (local_accum.has_bounds) {
		if (!has_content_bounds_) {
			content_bounds_pt_ = local_accum.bounds;
			has_content_bounds_ = true;
		} else {
			content_bounds_pt_ = union_rect(content_bounds_pt_, local_accum.bounds);
		}
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
	if (!is_finite_matrix(image_to_page) ||
		!validate_transformed_rect_finite(PdfRect{0.0, 0.0, 1.0, 1.0}, image_to_page)) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder::draw_image produced non-finite image-to-page coordinates");
	}
	if (commands_.size() >= limits_.max_commands) {
		return make_pdf_error(
			ErrorCode::DamagedFormat,
			"DisplayListBuilder command count limit exceeded");
	}
	if (auto ok = check_additional_bytes(sizeof(DisplayCommand)); !ok.has_value()) {
		return ok;
	}

	const bool potentially_visible =
		(current_state_.fill_alpha > 0.0f) &&
		bitmap->has_non_transparent_pixels &&
		(!bitmap->is_monochrome_mask || mask_tint_color.a > 0.0f) &&
		(!soft_mask || soft_mask->has_non_transparent_pixels);

	BoundsAccumulator local_accum{};
	if (potentially_visible) {
		const size_t pixel_work =
			static_cast<size_t>(bitmap->width) * static_cast<size_t>(bitmap->height);
		if (auto ok = charge_geometry_work(pixel_work); !ok.has_value()) {
			return ok;
		}
		try {
			const auto coverage_rects =
				compute_image_coverage_rects(*bitmap, soft_mask.get());
			for (const PdfRect& cov : coverage_rects) {
				if (auto ok = accumulate_clipped_rect(cov, image_to_page, local_accum);
					!ok.has_value()) {
					return ok;
				}
			}
		} catch (const std::bad_alloc&) {
			return make_pdf_error(
				ErrorCode::OutOfMemory,
				"Out of memory computing image coverage in DisplayListBuilder::draw_image");
		}
	}

	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		return ok;
	}

	DrawImageCommand cmd{};
	cmd.bitmap = std::move(bitmap);
	cmd.dest_transform = dest_transform;
	cmd.interpolate = interpolate;
	cmd.soft_mask = std::move(soft_mask);
	cmd.mask_tint_color = mask_tint_color;

	try {
		commands_.reserve(commands_.size() + 1u);
		commands_.emplace_back(std::move(cmd));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::draw_image");
	}

	if (local_accum.has_bounds) {
		if (!has_content_bounds_) {
			content_bounds_pt_ = local_accum.bounds;
			has_content_bounds_ = true;
		} else {
			content_bounds_pt_ = union_rect(content_bounds_pt_, local_accum.bounds);
		}
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
	if (auto ok = charge_allocation_op(); !ok.has_value()) {
		sticky_error_ = ok.error();
		finished_ = true;
		return ok.error();
	}

	try {
		std::unique_ptr<DisplayList> raw(new DisplayList(
			page_box_pt_,
			content_bounds_pt_,
			has_content_bounds_,
			std::move(commands_),
			std::move(paths_),
			std::move(path_verbs_),
			std::move(path_points_),
			std::move(glyphs_),
			std::move(dash_values_)));
		std::shared_ptr<const DisplayList> result(std::move(raw));
		finished_ = true;
		return result;
	} catch (const std::bad_alloc&) {
		sticky_error_ = PdfError{
			ErrorCode::OutOfMemory,
			"Out of memory in DisplayListBuilder::finish"};
		finished_ = true;
		return *sticky_error_;
	}
}

} // namespace recto::pdf
