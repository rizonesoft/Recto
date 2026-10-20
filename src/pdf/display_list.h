// Thread-safe, immutable DisplayList command buffer and DisplayListBuilder
// (foundation plan sections 3.3, 4.1 item 4, 4.2, 4.4, 4.5, 4.8; D01 T02
// section 1).
//
// Freeze contract (foundation plan section 4.2):
// Once `DisplayListBuilder::finish()` returns `std::shared_ptr<const DisplayList>`,
// the `DisplayList` is deeply immutable (`const` data members, `const`-only
// accessors over contiguous flat vectors and `std::shared_ptr<const DecodedBitmap>`
// buffers, zero `mutable` fields, and zero lazy caches). Any number of
// `RenderPool` worker threads may read a `DisplayList` concurrently without
// locks.
#pragma once

#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace recto::pdf {

class DisplayList;
class DisplayListBuilder;

// Returns the number of currently live `DisplayList` instances across the
// process (foundation plan section 4.8 step 6).
[[nodiscard]] int64_t debug_live_display_lists() noexcept;

struct PdfPoint {
	double x{0.0};
	double y{0.0};

	constexpr PdfPoint() noexcept = default;
	constexpr PdfPoint(double px, double py) noexcept
		: x(px)
		, y(py)
	{
	}

	constexpr bool operator==(const PdfPoint&) const noexcept = default;
};

// Transforms a point `(p.x, p.y)` by affine matrix `m` per ISO 32000-1
// section 8.3.3:
//   x' = a * x + c * y + e
//   y' = b * x + d * y + f
[[nodiscard]] constexpr PdfPoint transform_point(
	const PdfMatrix& m,
	PdfPoint p) noexcept
{
	return PdfPoint{
		m.a * p.x + m.c * p.y + m.e,
		m.b * p.x + m.d * p.y + m.f};
}

// Pre-concatenates matrix `lhs` before `rhs` (`lhs * rhs` in PDF row-vector
// convention per ISO 32000-1 section 8.3.4): a point transformed by
// `concat_matrix(lhs, rhs)` is transformed first by `lhs`, then by `rhs`.
[[nodiscard]] constexpr PdfMatrix concat_matrix(
	const PdfMatrix& lhs,
	const PdfMatrix& rhs) noexcept
{
	return PdfMatrix{
		lhs.a * rhs.a + lhs.b * rhs.c,
		lhs.a * rhs.b + lhs.b * rhs.d,
		lhs.c * rhs.a + lhs.d * rhs.c,
		lhs.c * rhs.b + lhs.d * rhs.d,
		lhs.e * rhs.a + lhs.f * rhs.c + rhs.e,
		lhs.e * rhs.b + lhs.f * rhs.d + rhs.f};
}

// Transforms the four corners of `rect` by `m` and returns their axis-aligned
// bounding box.
[[nodiscard]] PdfRect transform_rect(
	const PdfMatrix& m,
	const PdfRect& rect) noexcept;

// Returns the union of two non-empty bounding boxes.
[[nodiscard]] PdfRect union_rect(
	const PdfRect& a,
	const PdfRect& b) noexcept;

// Intersects two bounding boxes (`std::nullopt` when disjoint).
[[nodiscard]] std::optional<PdfRect> intersect_rect(
	const PdfRect& a,
	const PdfRect& b) noexcept;

struct ColorRgba {
	float r{0.0f};
	float g{0.0f};
	float b{0.0f};
	float a{1.0f};

	constexpr ColorRgba() noexcept = default;
	constexpr ColorRgba(float red, float green, float blue, float alpha = 1.0f) noexcept
		: r(red)
		, g(green)
		, b(blue)
		, a(alpha)
	{
	}

	[[nodiscard]] static constexpr ColorRgba black(float alpha = 1.0f) noexcept
	{
		return ColorRgba{0.0f, 0.0f, 0.0f, alpha};
	}

	[[nodiscard]] static constexpr ColorRgba white(float alpha = 1.0f) noexcept
	{
		return ColorRgba{1.0f, 1.0f, 1.0f, alpha};
	}

	constexpr bool operator==(const ColorRgba&) const noexcept = default;
};

enum class LineCap : uint8_t {
	Butt = 0,
	Round = 1,
	ProjectingSquare = 2,
};

enum class LineJoin : uint8_t {
	Miter = 0,
	Round = 1,
	Bevel = 2,
};

enum class FillRule : uint8_t {
	NonZero = 0,
	EvenOdd = 1,
};

enum class BlendMode : uint8_t {
	Normal = 0,
	Multiply,
	Screen,
	Overlay,
	Darken,
	Lighten,
	ColorDodge,
	ColorBurn,
	HardLight,
	SoftLight,
	Difference,
	Exclusion,
};

// PDF text rendering mode `Tr` (ISO 32000-1 section 9.3.6 Table 106).
enum class TextRenderMode : uint8_t {
	Fill = 0,
	Stroke = 1,
	FillThenStroke = 2,
	Invisible = 3,
	FillAndClip = 4,
	StrokeAndClip = 5,
	FillStrokeAndClip = 6,
	Clip = 7,
};

enum class PathVerb : uint8_t {
	MoveTo = 0,        // 1 PdfPoint: (x, y)
	LineTo = 1,        // 1 PdfPoint: (x, y)
	CubicBezierTo = 2, // 3 PdfPoints: cp1, cp2, end
	Rect = 3,          // 2 PdfPoints: (x, y), (width, height)
	CloseSubpath = 4,  // 0 PdfPoints
};

// Contiguous slice into `DisplayList::path_verbs()` and `DisplayList::path_points()`.
struct RecordedPath {
	uint32_t verb_offset{0};
	uint32_t verb_count{0};
	uint32_t point_offset{0};
	uint32_t point_count{0};
	PdfRect bounds_pt{};
	bool has_bounds{false};

	constexpr bool operator==(const RecordedPath&) const noexcept = default;
};

// Helper for building a single path before interning it into `DisplayListBuilder`.
class PathBuilder {
public:
	PathBuilder() noexcept = default;

	void move_to(double x, double y);
	void move_to(PdfPoint pt) { move_to(pt.x, pt.y); }

	void line_to(double x, double y);
	void line_to(PdfPoint pt) { line_to(pt.x, pt.y); }

	void cubic_bezier_to(
		PdfPoint cp1,
		PdfPoint cp2,
		PdfPoint end);

	void rect(double x, double y, double width, double height);
	void close_subpath();

	void clear() noexcept;

	[[nodiscard]] bool empty() const noexcept { return verbs_.empty(); }
	[[nodiscard]] std::span<const PathVerb> verbs() const noexcept { return verbs_; }
	[[nodiscard]] std::span<const PdfPoint> points() const noexcept { return points_; }

	[[nodiscard]] std::optional<PdfRect> compute_bounds(
		const PdfMatrix& transform = PdfMatrix::identity()) const noexcept;

private:
	std::vector<PathVerb> verbs_{};
	std::vector<PdfPoint> points_{};
};

// Immutable decoded image buffer shared zero-copy across `DrawImageCommand`s
// and `RenderPool` worker threads.
struct DecodedBitmap {
	const uint32_t width{0};
	const uint32_t height{0};
	const uint32_t stride{0};
	const std::vector<uint8_t> pixels{};
	const bool is_monochrome_mask{false};

	DecodedBitmap(
		uint32_t w,
		uint32_t h,
		uint32_t row_stride,
		std::vector<uint8_t> bgra_pixels,
		bool monochrome_mask = false) noexcept
		: width(w)
		, height(h)
		, stride(row_stride)
		, pixels(std::move(bgra_pixels))
		, is_monochrome_mask(monochrome_mask)
	{
	}

	DecodedBitmap(const DecodedBitmap&) = delete;
	DecodedBitmap& operator=(const DecodedBitmap&) = delete;
	DecodedBitmap(DecodedBitmap&&) = delete;
	DecodedBitmap& operator=(DecodedBitmap&&) = delete;

	[[nodiscard]] static PdfResult<std::shared_ptr<const DecodedBitmap>> create(
		uint32_t width,
		uint32_t height,
		uint32_t stride,
		std::vector<uint8_t> bgra_pixels,
		bool is_monochrome_mask = false) noexcept;

	[[nodiscard]] std::span<const uint8_t> span() const noexcept
	{
		return pixels;
	}

	[[nodiscard]] size_t byte_size() const noexcept
	{
		return pixels.size();
	}
};

struct PositionedGlyph {
	uint16_t glyph_index{0};
	uint32_t unicode_codepoint{0};
	PdfPoint origin_pt{0.0, 0.0};
	double advance_pt{0.0};

	constexpr bool operator==(const PositionedGlyph&) const noexcept = default;
};

struct SaveStateCommand {
	constexpr bool operator==(const SaveStateCommand&) const noexcept = default;
};

struct RestoreStateCommand {
	constexpr bool operator==(const RestoreStateCommand&) const noexcept = default;
};

struct ConcatMatrixCommand {
	PdfMatrix matrix{PdfMatrix::identity()};

	constexpr bool operator==(const ConcatMatrixCommand&) const noexcept = default;
};

struct SetStrokeStyleCommand {
	float line_width{1.0f};
	LineCap line_cap{LineCap::Butt};
	LineJoin line_join{LineJoin::Miter};
	float miter_limit{10.0f};
	uint32_t dash_offset{0};
	uint32_t dash_count{0};
	float dash_phase{0.0f};

	constexpr bool operator==(const SetStrokeStyleCommand&) const noexcept = default;
};

struct SetAlphaAndBlendCommand {
	float stroke_alpha{1.0f};
	float fill_alpha{1.0f};
	BlendMode blend_mode{BlendMode::Normal};

	constexpr bool operator==(const SetAlphaAndBlendCommand&) const noexcept = default;
};

struct PushClipPathCommand {
	uint32_t path_index{0};
	FillRule fill_rule{FillRule::NonZero};

	constexpr bool operator==(const PushClipPathCommand&) const noexcept = default;
};

struct DrawPathCommand {
	uint32_t path_index{0};
	ColorRgba stroke_color{ColorRgba::black()};
	ColorRgba fill_color{ColorRgba::black()};
	bool stroke{false};
	bool fill{false};
	FillRule fill_rule{FillRule::NonZero};

	constexpr bool operator==(const DrawPathCommand&) const noexcept = default;
};

struct RecordedGlyphRun {
	uint32_t glyph_offset{0};
	uint32_t glyph_count{0};
	double font_size{12.0};
	TextRenderMode render_mode{TextRenderMode::Fill};
	ColorRgba fill_color{ColorRgba::black()};
	ColorRgba stroke_color{ColorRgba::black()};
	PdfMatrix text_matrix{PdfMatrix::identity()};
	PdfMatrix ctm{PdfMatrix::identity()};
	PdfRect bounds_pt{};
	bool has_bounds{false};
	std::shared_ptr<const void> font_handle{nullptr};
	std::shared_ptr<const DisplayList> type3_sub_list{nullptr};
};

using DrawGlyphRunCommand = RecordedGlyphRun;

struct DrawImageCommand {
	std::shared_ptr<const DecodedBitmap> bitmap{nullptr};
	PdfMatrix dest_transform{PdfMatrix::identity()};
	bool interpolate{true};
	std::shared_ptr<const DecodedBitmap> soft_mask{nullptr};
	ColorRgba mask_tint_color{ColorRgba::black()};
};

enum class DisplayCommandKind : uint8_t {
	SaveState = 0,
	RestoreState,
	ConcatMatrix,
	SetStrokeStyle,
	SetAlphaAndBlend,
	PushClipPath,
	DrawPath,
	DrawGlyphRun,
	DrawImage,
};

struct DisplayCommand {
	using Payload = std::variant<
		SaveStateCommand,
		RestoreStateCommand,
		ConcatMatrixCommand,
		SetStrokeStyleCommand,
		SetAlphaAndBlendCommand,
		PushClipPathCommand,
		DrawPathCommand,
		DrawGlyphRunCommand,
		DrawImageCommand>;

	Payload payload{SaveStateCommand{}};

	DisplayCommand() noexcept = default;

	template <typename T>
		requires (!std::is_same_v<std::remove_cvref_t<T>, DisplayCommand>)
	explicit DisplayCommand(T&& cmd) noexcept(std::is_nothrow_constructible_v<Payload, T&&>)
		: payload(std::forward<T>(cmd))
	{
	}

	[[nodiscard]] DisplayCommandKind kind() const noexcept
	{
		return static_cast<DisplayCommandKind>(payload.index());
	}

	template <typename T>
	[[nodiscard]] const T* as() const noexcept
	{
		return std::get_if<T>(&payload);
	}
};

// Immutable, reference-counted display list for one PDF page (or Type 3 / Form
// sub-stream). Exposes `const`-only accessors over flat contiguous arrays.
class DisplayList {
public:
	~DisplayList() noexcept;

	DisplayList(const DisplayList&) = delete;
	DisplayList& operator=(const DisplayList&) = delete;
	DisplayList(DisplayList&&) = delete;
	DisplayList& operator=(DisplayList&&) = delete;

	[[nodiscard]] const PdfRect& page_box_pt() const noexcept
	{
		return page_box_pt_;
	}

	[[nodiscard]] const PdfRect& content_bounds_pt() const noexcept
	{
		return content_bounds_pt_;
	}

	[[nodiscard]] bool has_content_bounds() const noexcept
	{
		return has_content_bounds_;
	}

	[[nodiscard]] std::span<const DisplayCommand> commands() const noexcept
	{
		return commands_;
	}

	[[nodiscard]] size_t command_count() const noexcept
	{
		return commands_.size();
	}

	[[nodiscard]] bool empty() const noexcept
	{
		return commands_.empty();
	}

	[[nodiscard]] std::span<const RecordedPath> paths() const noexcept
	{
		return paths_;
	}

	[[nodiscard]] const RecordedPath& path(uint32_t path_index) const noexcept
	{
		return paths_[path_index];
	}

	[[nodiscard]] std::span<const PathVerb> path_verbs() const noexcept
	{
		return path_verbs_;
	}

	[[nodiscard]] std::span<const PathVerb> path_verbs(const RecordedPath& p) const noexcept
	{
		return std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	}

	[[nodiscard]] std::span<const PdfPoint> path_points() const noexcept
	{
		return path_points_;
	}

	[[nodiscard]] std::span<const PdfPoint> path_points(const RecordedPath& p) const noexcept
	{
		return std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);
	}

	[[nodiscard]] std::span<const PositionedGlyph> glyphs() const noexcept
	{
		return glyphs_;
	}

	[[nodiscard]] std::span<const PositionedGlyph> glyphs(const RecordedGlyphRun& run) const noexcept
	{
		return std::span<const PositionedGlyph>(glyphs_).subspan(run.glyph_offset, run.glyph_count);
	}

	[[nodiscard]] std::span<const float> dash_values() const noexcept
	{
		return dash_values_;
	}

	[[nodiscard]] std::span<const float> dash_values(const SetStrokeStyleCommand& style) const noexcept
	{
		return std::span<const float>(dash_values_).subspan(style.dash_offset, style.dash_count);
	}

private:
	friend class DisplayListBuilder;

	DisplayList(
		PdfRect page_box_pt,
		PdfRect content_bounds_pt,
		bool has_content_bounds,
		std::vector<DisplayCommand>&& commands,
		std::vector<RecordedPath>&& paths,
		std::vector<PathVerb>&& path_verbs,
		std::vector<PdfPoint>&& path_points,
		std::vector<PositionedGlyph>&& glyphs,
		std::vector<float>&& dash_values) noexcept;

	const PdfRect page_box_pt_;
	const PdfRect content_bounds_pt_;
	const bool has_content_bounds_;
	const std::vector<DisplayCommand> commands_;
	const std::vector<RecordedPath> paths_;
	const std::vector<PathVerb> path_verbs_;
	const std::vector<PdfPoint> path_points_;
	const std::vector<PositionedGlyph> glyphs_;
	const std::vector<float> dash_values_;
};

// Builder for recording commands, interning paths/glyphs/dashes into contiguous
// buffers, tracking CTM and clip bounds for `content_bounds_pt`, and sealing
// into an immutable `std::shared_ptr<const DisplayList>`.
class DisplayListBuilder {
public:
	explicit DisplayListBuilder(PdfRect page_box_pt = PdfRect{0.0, 0.0, 612.0, 792.0});

	void reserve_commands(size_t count);

	// Interns a path into the shared verb/point pools and returns its `path_index`.
	[[nodiscard]] uint32_t add_path(const PathBuilder& path);
	[[nodiscard]] uint32_t add_path(
		std::span<const PathVerb> verbs,
		std::span<const PdfPoint> points);

	void save_state();
	void restore_state();
	void concat_matrix(const PdfMatrix& matrix);

	void set_stroke_style(
		float line_width,
		LineCap line_cap = LineCap::Butt,
		LineJoin line_join = LineJoin::Miter,
		float miter_limit = 10.0f,
		std::span<const float> dash_array = {},
		float dash_phase = 0.0f);

	void set_alpha_and_blend(
		float stroke_alpha,
		float fill_alpha,
		BlendMode blend_mode = BlendMode::Normal);

	void push_clip_path(
		uint32_t path_index,
		FillRule fill_rule = FillRule::NonZero);

	void draw_path(
		uint32_t path_index,
		ColorRgba stroke_color,
		ColorRgba fill_color,
		bool stroke,
		bool fill,
		FillRule fill_rule = FillRule::NonZero);

	void draw_glyph_run(
		std::span<const PositionedGlyph> glyphs,
		double font_size,
		TextRenderMode render_mode,
		ColorRgba fill_color,
		ColorRgba stroke_color,
		const PdfMatrix& text_matrix = PdfMatrix::identity(),
		std::optional<PdfRect> local_bounds_pt = std::nullopt,
		std::shared_ptr<const void> font_handle = nullptr,
		std::shared_ptr<const DisplayList> type3_sub_list = nullptr);

	void draw_image(
		std::shared_ptr<const DecodedBitmap> bitmap,
		const PdfMatrix& dest_transform = PdfMatrix::identity(),
		bool interpolate = true,
		std::shared_ptr<const DecodedBitmap> soft_mask = nullptr,
		ColorRgba mask_tint_color = ColorRgba::black());

	[[nodiscard]] bool is_finished() const noexcept
	{
		return finished_;
	}

	// Seals this builder and returns the immutable `DisplayList`.
	[[nodiscard]] std::shared_ptr<const DisplayList> finish();

private:
	struct TrackingState {
		PdfMatrix ctm{PdfMatrix::identity()};
		std::optional<PdfRect> clip_bounds{};
		float line_width{1.0f};
	};

	void accumulate_content_bounds(const PdfRect& page_space_bounds) noexcept;
	[[nodiscard]] std::optional<PdfRect> compute_path_page_bounds(
		uint32_t path_index,
		const PdfMatrix& transform) const noexcept;

	PdfRect page_box_pt_{};
	PdfRect content_bounds_pt_{};
	bool has_content_bounds_{false};
	bool finished_{false};

	TrackingState current_state_{};
	std::vector<TrackingState> state_stack_{};
	size_t overflow_save_count_{0u};
	std::weak_ptr<const DisplayList> sealed_result_{};

	std::vector<DisplayCommand> commands_{};
	std::vector<RecordedPath> paths_{};
	std::vector<PathVerb> path_verbs_{};
	std::vector<PdfPoint> path_points_{};
	std::vector<PositionedGlyph> glyphs_{};
	std::vector<float> dash_values_{};
};

// Evaluates exact axis-aligned bounding box of a recorded path under `transform`
// (including analytic extrema of cubic Bezier segments).
[[nodiscard]] std::optional<PdfRect> compute_recorded_path_bounds(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points,
	const PdfMatrix& transform = PdfMatrix::identity()) noexcept;

} // namespace recto::pdf
