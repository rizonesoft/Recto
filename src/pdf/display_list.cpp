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
#include <utility>

namespace recto::pdf {

namespace {

std::atomic<int64_t> g_live_display_lists{0};

constexpr size_t kMaxBuilderSaveDepth = 64u;
constexpr uint32_t kMaxBitmapDimension = 65536u;
constexpr uint64_t kMaxBitmapBytes = 256ull * 1024ull * 1024ull;

[[nodiscard]] bool is_finite_point(PdfPoint p) noexcept
{
	return std::isfinite(p.x) && std::isfinite(p.y);
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

	// Derivative B'(t) / 3 = a * t^2 + 2 * b * t + c = 0
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
			if (pt_idx >= points.size()) {
				return has_any ? std::optional<PdfRect>(PdfRect{x_min, y_min, x_max, y_max}) : std::nullopt;
			}
			cur = points[pt_idx++];
			sub_start = cur;
			has_cur = true;
			break;
		}
		case PathVerb::LineTo: {
			if (pt_idx >= points.size()) {
				return has_any ? std::optional<PdfRect>(PdfRect{x_min, y_min, x_max, y_max}) : std::nullopt;
			}
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
			if (pt_idx + 3u > points.size()) {
				return has_any ? std::optional<PdfRect>(PdfRect{x_min, y_min, x_max, y_max}) : std::nullopt;
			}
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
			if (pt_idx + 2u > points.size()) {
				return has_any ? std::optional<PdfRect>(PdfRect{x_min, y_min, x_max, y_max}) : std::nullopt;
			}
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

	try {
		return std::shared_ptr<const DecodedBitmap>(new DecodedBitmap(
			width,
			height,
			stride,
			std::move(bgra_pixels),
			is_monochrome_mask));
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

DisplayListBuilder::DisplayListBuilder(PdfRect page_box_pt)
	: page_box_pt_(page_box_pt)
{
	if (!page_box_pt_.is_empty()) {
		current_state_.clip_bounds = page_box_pt_;
	}
}

void DisplayListBuilder::reserve_commands(size_t count)
{
	if (finished_) {
		return;
	}
	commands_.reserve(count);
}

uint32_t DisplayListBuilder::add_path(const PathBuilder& path)
{
	return add_path(path.verbs(), path.points());
}

uint32_t DisplayListBuilder::add_path(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points)
{
	if (finished_) {
		return 0u;
	}
	const uint32_t verb_offset = static_cast<uint32_t>(path_verbs_.size());
	const uint32_t verb_count = static_cast<uint32_t>(verbs.size());
	const uint32_t point_offset = static_cast<uint32_t>(path_points_.size());
	const uint32_t point_count = static_cast<uint32_t>(points.size());

	path_verbs_.insert(path_verbs_.end(), verbs.begin(), verbs.end());
	path_points_.insert(path_points_.end(), points.begin(), points.end());

	RecordedPath rec{};
	rec.verb_offset = verb_offset;
	rec.verb_count = verb_count;
	rec.point_offset = point_offset;
	rec.point_count = point_count;
	if (const auto bounds = compute_recorded_path_bounds(verbs, points)) {
		rec.bounds_pt = *bounds;
		rec.has_bounds = true;
	}

	const uint32_t index = static_cast<uint32_t>(paths_.size());
	paths_.push_back(rec);
	return index;
}

void DisplayListBuilder::save_state()
{
	if (finished_) {
		return;
	}
	if (state_stack_.size() < kMaxBuilderSaveDepth) {
		state_stack_.push_back(current_state_);
	} else {
		++overflow_save_count_;
	}
	commands_.emplace_back(SaveStateCommand{});
}

void DisplayListBuilder::restore_state()
{
	if (finished_) {
		return;
	}
	if (overflow_save_count_ > 0u) {
		--overflow_save_count_;
	} else if (!state_stack_.empty()) {
		current_state_ = state_stack_.back();
		state_stack_.pop_back();
	}
	commands_.emplace_back(RestoreStateCommand{});
}

void DisplayListBuilder::concat_matrix(const PdfMatrix& matrix)
{
	if (finished_) {
		return;
	}
	current_state_.ctm = recto::pdf::concat_matrix(matrix, current_state_.ctm);
	commands_.emplace_back(ConcatMatrixCommand{matrix});
}

void DisplayListBuilder::set_stroke_style(
	float line_width,
	LineCap line_cap,
	LineJoin line_join,
	float miter_limit,
	std::span<const float> dash_array,
	float dash_phase)
{
	if (finished_) {
		return;
	}
	current_state_.line_width = std::max(0.0f, line_width);
	const uint32_t dash_offset = static_cast<uint32_t>(dash_values_.size());
	const uint32_t dash_count = static_cast<uint32_t>(dash_array.size());
	dash_values_.insert(dash_values_.end(), dash_array.begin(), dash_array.end());

	SetStrokeStyleCommand cmd{};
	cmd.line_width = current_state_.line_width;
	cmd.line_cap = line_cap;
	cmd.line_join = line_join;
	cmd.miter_limit = miter_limit;
	cmd.dash_offset = dash_offset;
	cmd.dash_count = dash_count;
	cmd.dash_phase = dash_phase;
	commands_.emplace_back(cmd);
}

void DisplayListBuilder::set_alpha_and_blend(
	float stroke_alpha,
	float fill_alpha,
	BlendMode blend_mode)
{
	if (finished_) {
		return;
	}
	SetAlphaAndBlendCommand cmd{};
	cmd.stroke_alpha = std::clamp(stroke_alpha, 0.0f, 1.0f);
	cmd.fill_alpha = std::clamp(fill_alpha, 0.0f, 1.0f);
	cmd.blend_mode = blend_mode;
	commands_.emplace_back(cmd);
}

std::optional<PdfRect> DisplayListBuilder::compute_path_page_bounds(
	uint32_t path_index,
	const PdfMatrix& transform) const noexcept
{
	if (path_index >= paths_.size()) {
		return std::nullopt;
	}
	const RecordedPath& p = paths_[path_index];
	const auto verbs = std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	const auto points = std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);
	return compute_recorded_path_bounds(verbs, points, transform);
}

void DisplayListBuilder::accumulate_content_bounds(const PdfRect& page_space_bounds) noexcept
{
	if (!std::isfinite(page_space_bounds.x_min) ||
		!std::isfinite(page_space_bounds.y_min) ||
		!std::isfinite(page_space_bounds.x_max) ||
		!std::isfinite(page_space_bounds.y_max)) {
		return;
	}
	PdfRect clipped = page_space_bounds;
	if (current_state_.clip_bounds.has_value()) {
		const auto inter = intersect_rect(clipped, *current_state_.clip_bounds);
		if (!inter.has_value()) {
			return;
		}
		clipped = *inter;
	}
	if (!has_content_bounds_) {
		content_bounds_pt_ = clipped;
		has_content_bounds_ = true;
	} else {
		content_bounds_pt_ = union_rect(content_bounds_pt_, clipped);
	}
}

void DisplayListBuilder::push_clip_path(
	uint32_t path_index,
	FillRule fill_rule)
{
	if (finished_ || path_index >= paths_.size()) {
		return;
	}
	if (const auto path_bounds = compute_path_page_bounds(path_index, current_state_.ctm)) {
		if (current_state_.clip_bounds.has_value()) {
			const auto inter = intersect_rect(*current_state_.clip_bounds, *path_bounds);
			// When a pushed clip path does not intersect the active clip rect,
			// collapse to a degenerate inverted box outside everything so
			// subsequent drawing in this state is completely clipped out.
			current_state_.clip_bounds = inter.value_or(PdfRect{0.0, 0.0, 0.0, 0.0});
			if (!inter.has_value()) {
				current_state_.clip_bounds->x_min = 1.0;
				current_state_.clip_bounds->x_max = -1.0;
			}
		} else {
			current_state_.clip_bounds = *path_bounds;
		}
	} else {
		// An empty clip path clips out all subsequent drawing in this state.
		PdfRect empty_clip{};
		empty_clip.x_min = 1.0;
		empty_clip.x_max = -1.0;
		current_state_.clip_bounds = empty_clip;
	}
	commands_.emplace_back(PushClipPathCommand{path_index, fill_rule});
}

void DisplayListBuilder::draw_path(
	uint32_t path_index,
	ColorRgba stroke_color,
	ColorRgba fill_color,
	bool stroke,
	bool fill,
	FillRule fill_rule)
{
	if (finished_ || path_index >= paths_.size()) {
		return;
	}
	if (stroke || fill) {
		if (const auto path_bounds = compute_path_page_bounds(path_index, current_state_.ctm)) {
			accumulate_content_bounds(*path_bounds);
		}
	}
	DrawPathCommand cmd{};
	cmd.path_index = path_index;
	cmd.stroke_color = stroke_color;
	cmd.fill_color = fill_color;
	cmd.stroke = stroke;
	cmd.fill = fill;
	cmd.fill_rule = fill_rule;
	commands_.emplace_back(cmd);
}

void DisplayListBuilder::draw_glyph_run(
	std::span<const PositionedGlyph> glyphs,
	double font_size,
	TextRenderMode render_mode,
	ColorRgba fill_color,
	ColorRgba stroke_color,
	const PdfMatrix& text_matrix,
	std::optional<PdfRect> local_bounds_pt,
	std::shared_ptr<const void> font_handle,
	std::shared_ptr<const DisplayList> type3_sub_list)
{
	if (finished_) {
		return;
	}
	const uint32_t glyph_offset = static_cast<uint32_t>(glyphs_.size());
	const uint32_t glyph_count = static_cast<uint32_t>(glyphs.size());
	glyphs_.insert(glyphs_.end(), glyphs.begin(), glyphs.end());

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

	if (local_bounds_pt.has_value()) {
		cmd.bounds_pt = *local_bounds_pt;
		cmd.has_bounds = true;
	} else if (!glyphs.empty()) {
		const double abs_size = std::abs(font_size);
		double gx_min = std::min(glyphs[0].origin_pt.x, glyphs[0].origin_pt.x + glyphs[0].advance_pt);
		double gy_min = glyphs[0].origin_pt.y;
		double gx_max = std::max(glyphs[0].origin_pt.x, glyphs[0].origin_pt.x + glyphs[0].advance_pt);
		double gy_max = glyphs[0].origin_pt.y + abs_size;
		for (size_t i = 1u; i < glyphs.size(); ++i) {
			const double x0 = glyphs[i].origin_pt.x;
			const double y0 = glyphs[i].origin_pt.y;
			const double x1 = x0 + glyphs[i].advance_pt;
			const double y1 = y0 + abs_size;
			gx_min = std::min(gx_min, std::min(x0, x1));
			gy_min = std::min(gy_min, std::min(y0, y1));
			gx_max = std::max(gx_max, std::max(x0, x1));
			gy_max = std::max(gy_max, std::max(y0, y1));
		}
		cmd.bounds_pt = PdfRect{gx_min, gy_min, gx_max, gy_max};
		cmd.has_bounds = true;
	}

	const bool paints_ink =
		render_mode != TextRenderMode::Invisible &&
		render_mode != TextRenderMode::Clip;
	if (paints_ink && cmd.has_bounds) {
		const PdfMatrix combined = recto::pdf::concat_matrix(text_matrix, current_state_.ctm);
		accumulate_content_bounds(transform_rect(combined, cmd.bounds_pt));
	}

	commands_.emplace_back(std::move(cmd));
}

void DisplayListBuilder::draw_image(
	std::shared_ptr<const DecodedBitmap> bitmap,
	const PdfMatrix& dest_transform,
	bool interpolate,
	std::shared_ptr<const DecodedBitmap> soft_mask,
	ColorRgba mask_tint_color)
{
	if (finished_ || !bitmap) {
		return;
	}
	const PdfMatrix image_to_page = recto::pdf::concat_matrix(dest_transform, current_state_.ctm);
	const PdfRect page_bounds = transform_rect(
		image_to_page,
		PdfRect{0.0, 0.0, 1.0, 1.0});
	accumulate_content_bounds(page_bounds);

	DrawImageCommand cmd{};
	cmd.bitmap = std::move(bitmap);
	cmd.dest_transform = dest_transform;
	cmd.interpolate = interpolate;
	cmd.soft_mask = std::move(soft_mask);
	cmd.mask_tint_color = mask_tint_color;
	commands_.emplace_back(std::move(cmd));
}

std::shared_ptr<const DisplayList> DisplayListBuilder::finish()
{
	if (finished_) {
		if (auto existing = sealed_result_.lock()) {
			return existing;
		}
	}
	finished_ = true;
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
	sealed_result_ = result;
	return result;
}

} // namespace recto::pdf
