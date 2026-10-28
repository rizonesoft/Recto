// Thread-safe, immutable DisplayList command buffer and DisplayListBuilder
// (foundation plan sections 3.3, 4.1 item 4, 4.2, 4.4, 4.5, 4.8; D01 T02
// section 1).
//
// Thread-ownership and freeze contract (`frozen: true`):
// Once `DisplayListBuilder::finish()` returns `std::shared_ptr<const DisplayList>`,
// the `DisplayList` exposes `const`-only accessors over contiguous immutable
// vectors and `std::shared_ptr<const DecodedBitmap>` buffers, with zero mutable
// members or lazy caches. Any number of `RenderPool` worker threads may read
// the same `DisplayList` concurrently without locks.
#pragma once

#include "error.h"
#include "object.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace recto::pdf {

class DisplayList;

// Returns the number of currently live `DisplayList` instances across the
// process. Used by `DocumentActor` (D01 T02 section 8) and `Runtime::~Runtime()`
// (D01 T02 section 10; foundation plan section 4.8 step 6) to assert zero leaked
// display lists on shutdown.
[[nodiscard]] int64_t debug_live_display_lists() noexcept;

// 2D point in PDF user/page/text space (1/72 inch points).
struct PdfPoint {
	double x{0.0};
	double y{0.0};

	constexpr PdfPoint() noexcept = default;
	constexpr PdfPoint(double px, double py) noexcept
		: x(px), y(py)
	{
	}

	constexpr bool operator==(const PdfPoint&) const noexcept = default;
};

// Returns true if all six coefficients of `m` are finite numbers.
[[nodiscard]] inline bool is_finite_matrix(const PdfMatrix& m) noexcept
{
	return std::isfinite(m.a) && std::isfinite(m.b) &&
		std::isfinite(m.c) && std::isfinite(m.d) &&
		std::isfinite(m.e) && std::isfinite(m.f);
}

// Concatenates `pre * post` per PDF `cm` operator semantics (`M' = M_new * CTM`).
[[nodiscard]] constexpr PdfMatrix concat_matrix(
	const PdfMatrix& pre,
	const PdfMatrix& post) noexcept
{
	return PdfMatrix{
		pre.a * post.a + pre.b * post.c,
		pre.a * post.b + pre.b * post.d,
		pre.c * post.a + pre.d * post.c,
		pre.c * post.b + pre.d * post.d,
		pre.e * post.a + pre.f * post.c + post.e,
		pre.e * post.b + pre.f * post.d + post.f};
}

// Transforms a 2D point by an affine `PdfMatrix`.
[[nodiscard]] constexpr PdfPoint transform_point(
	const PdfMatrix& m,
	PdfPoint p) noexcept
{
	return PdfPoint{
		m.a * p.x + m.c * p.y + m.e,
		m.b * p.x + m.d * p.y + m.f};
}

// Transforms an axis-aligned `PdfRect` by an affine `PdfMatrix` and returns the
// axis-aligned bounding box of its four transformed corners.
[[nodiscard]] PdfRect transform_rect(
	const PdfMatrix& m,
	const PdfRect& rect) noexcept;

// Computes the smallest axis-aligned rectangle containing both `a` and `b`.
[[nodiscard]] PdfRect union_rect(
	const PdfRect& a,
	const PdfRect& b) noexcept;

// Computes the intersection of two axis-aligned rectangles, or `std::nullopt`
// if they are disjoint.
[[nodiscard]] std::optional<PdfRect> intersect_rect(
	const PdfRect& a,
	const PdfRect& b) noexcept;

// Normalized sRGB colour with unpremultiplied alpha in `[0.0f, 1.0f]`.
struct ColorRgba {
	float r{0.0f};
	float g{0.0f};
	float b{0.0f};
	float a{1.0f};

	constexpr ColorRgba() noexcept = default;
	constexpr ColorRgba(float cr, float cg, float cb, float ca = 1.0f) noexcept
		: r(cr), g(cg), b(cb), a(ca)
	{
	}

	[[nodiscard]] static constexpr ColorRgba black() noexcept
	{
		return ColorRgba{0.0f, 0.0f, 0.0f, 1.0f};
	}

	[[nodiscard]] static constexpr ColorRgba white() noexcept
	{
		return ColorRgba{1.0f, 1.0f, 1.0f, 1.0f};
	}

	[[nodiscard]] bool is_valid() const noexcept
	{
		return std::isfinite(r) && r >= 0.0f && r <= 1.0f &&
			std::isfinite(g) && g >= 0.0f && g <= 1.0f &&
			std::isfinite(b) && b >= 0.0f && b <= 1.0f &&
			std::isfinite(a) && a >= 0.0f && a <= 1.0f;
	}

	constexpr bool operator==(const ColorRgba&) const noexcept = default;
};

// PDF line cap styles (`J` operator, ISO 32000-1 Table 54).
enum class LineCap : uint8_t {
	Butt = 0,
	Round = 1,
	ProjectingSquare = 2,
};

// PDF line join styles (`j` operator, ISO 32000-1 Table 55).
enum class LineJoin : uint8_t {
	Miter = 0,
	Round = 1,
	Bevel = 2,
};

// PDF path fill/clip winding rules (`f`/`W` vs `f*`/`W*`).
enum class FillRule : uint8_t {
	NonZero = 0,
	EvenOdd = 1,
};

// PDF separable and non-separable blend modes (`/BM` in `/ExtGState`,
// ISO 32000-1 Tables 136-137).
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
	Hue,
	Saturation,
	Color,
	Luminosity,
};

// PDF text rendering modes (`Tr` operator, ISO 32000-1 Table 106).
enum class TextRenderMode : uint8_t {
	Fill = 0,
	Stroke = 1,
	FillAndStroke = 2,
	Invisible = 3,
	FillAndClip = 4,
	StrokeAndClip = 5,
	FillStrokeAndClip = 6,
	Clip = 7,
};

// Path geometry segment verbs stored in `DisplayList`'s shared verb pool.
// Point counts per verb in the shared `PdfPoint` coordinate pool:
// - `MoveTo`:        1 point  `(x, y)`
// - `LineTo`:        1 point  `(x, y)`
// - `CubicBezierTo`: 3 points `(cp1, cp2, end)`
// - `Rect`:          2 points `(origin (x, y), size (w, h))`
// - `CloseSubpath`:  0 points (closes current figure to most recent `MoveTo`)
enum class PathVerb : uint8_t {
	MoveTo = 0,
	LineTo = 1,
	CubicBezierTo = 2,
	Rect = 3,
	CloseSubpath = 4,
};

// Slice descriptor into a `DisplayList`'s contiguous `path_verbs()` and
// `path_points()` pools, along with the path's tight local-space bounding box.
struct RecordedPath {
	uint32_t verb_offset{0};
	uint32_t verb_count{0};
	uint32_t point_offset{0};
	uint32_t point_count{0};
	PdfRect bounds_pt{};
	bool has_bounds{false};

	constexpr bool operator==(const RecordedPath&) const noexcept = default;
};

// Helper for assembling a multi-subpath geometry before interning it into a
// `DisplayListBuilder` via `add_path()`.
class PathBuilder {
public:
	PathBuilder() = default;

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

	// Computes the tight axis-aligned bounding box of all recorded segments
	// (including analytic cubic Bezier extrema) after transforming by `transform`.
	[[nodiscard]] std::optional<PdfRect> compute_bounds(
		const PdfMatrix& transform = PdfMatrix::identity()) const noexcept;

private:
	std::vector<PathVerb> verbs_;
	std::vector<PdfPoint> points_;
};

// Computes the tight axis-aligned bounding box of a raw `(verbs, points)` slice
// after applying `transform`. Returns `std::nullopt` if the path contains no
// drawable geometry, is malformed, or overflows to non-finite coordinates.
[[nodiscard]] std::optional<PdfRect> compute_recorded_path_bounds(
	std::span<const PathVerb> verbs,
	std::span<const PdfPoint> points,
	const PdfMatrix& transform = PdfMatrix::identity()) noexcept;

// Half-open box of pixel cells `[x_min, x_max) x [y_min, y_max)` in a
// `DecodedBitmap`'s column and row indices.
struct PixelBox {
	uint32_t x_min{0};
	uint32_t y_min{0};
	uint32_t x_max{0};
	uint32_t y_max{0};

	constexpr bool operator==(const PixelBox&) const noexcept = default;
};

// Immutable decoded image bitmap shared zero-copy across `DisplayList` commands
// and `RenderPool` worker threads via `std::shared_ptr<const DecodedBitmap>`.
// Construction is restricted to the validated `DecodedBitmap::create()` factory.
class DecodedBitmap {
public:
	const uint32_t width{0};
	const uint32_t height{0};
	const uint32_t stride{0};
	const std::vector<uint8_t> pixels; // Premultiplied BGRA8888 (4 bytes/pixel)
	const bool is_monochrome_mask{false};
	const bool has_non_transparent_pixels{false};
	// Smallest box of pixel cells holding every pixel whose alpha is non-zero,
	// computed once by `create()`; `std::nullopt` for a fully transparent bitmap.
	const std::optional<PixelBox> opaque_pixel_box;

	DecodedBitmap() = delete;
	DecodedBitmap(const DecodedBitmap&) = delete;
	DecodedBitmap& operator=(const DecodedBitmap&) = delete;
	DecodedBitmap(DecodedBitmap&&) = delete;
	DecodedBitmap& operator=(DecodedBitmap&&) = delete;

	// Validates dimensions (`1..65536`), stride (`stride >= width * 4`), and
	// pixel buffer size (exactly `stride * (height - 1) + width * 4`, the last
	// row unpadded, or `stride * height`, every row padded, within 256 MiB)
	// before constructing an immutable shared `DecodedBitmap`.
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

private:
	DecodedBitmap(
		uint32_t w,
		uint32_t h,
		uint32_t row_stride,
		std::vector<uint8_t>&& bgra_pixels,
		bool monochrome_mask,
		std::optional<PixelBox> opaque_box) noexcept
		: width(w)
		, height(h)
		, stride(row_stride)
		, pixels(std::move(bgra_pixels))
		, is_monochrome_mask(monochrome_mask)
		, has_non_transparent_pixels(opaque_box.has_value())
		, opaque_pixel_box(opaque_box)
	{
	}
};

// Positioned glyph entry stored in `DisplayList`'s contiguous glyph pool.
struct PositionedGlyph {
	uint16_t glyph_index{0};
	uint32_t unicode_codepoint{0};
	PdfPoint origin_pt{}; // Glyph origin in text/user space
	double advance_pt{0.0};
	// The horizontal glyph-space scale the rasterizer applies to the outline about `origin_pt` before placing it
	// (D01 T02 section 43: a substitute face's glyph fitted to the advance the PDF gives it); `advance_pt`, origins
	// and `ink_bounds_pt` already include it. Finite and positive.
	double width_scale{1.0};
	// Glyph outline ink bounding box relative to `origin_pt` in text space
	// (`[left_bearing, descender, right_bearing, ascender]`), in the same
	// text-space points as `origin_pt` and `advance_pt`: the caller has already
	// scaled the glyph-space outline box by the font matrix, font size and
	// horizontal scaling, so only `text_matrix` and the CTM remain to map it to
	// the page. May have negative `x_min` (negative left side bearing), negative
	// `y_min` (descender below baseline), or `x_max > advance_pt` (right
	// overhang). Omit when `is_blank` is true or when `type3_sub_list` is
	// supplied; a run whose visible glyphs lack it falls back to the run-level
	// `local_bounds_pt`, a conservative box for the whole run.
	std::optional<PdfRect> ink_bounds_pt{};
	bool is_blank{false};

	constexpr PositionedGlyph() noexcept = default;

	constexpr PositionedGlyph(
		uint16_t index,
		uint32_t codepoint,
		PdfPoint origin,
		double advance,
		std::optional<PdfRect> ink_bounds = std::nullopt,
		bool blank = false) noexcept
		: glyph_index(index)
		, unicode_codepoint(codepoint)
		, origin_pt(origin)
		, advance_pt(advance)
		, ink_bounds_pt(ink_bounds)
		, is_blank(blank)
	{
	}

	[[nodiscard]] static constexpr PositionedGlyph blank(
		uint16_t index,
		uint32_t codepoint,
		PdfPoint origin,
		double advance) noexcept
	{
		return PositionedGlyph{index, codepoint, origin, advance, std::nullopt, true};
	}

	constexpr bool operator==(const PositionedGlyph&) const noexcept = default;
};

// Command payload value types:

struct SaveStateCommand {
	constexpr bool operator==(const SaveStateCommand&) const noexcept = default;
};

struct RestoreStateCommand {
	constexpr bool operator==(const RestoreStateCommand&) const noexcept = default;
};

struct ConcatMatrixCommand {
	PdfMatrix matrix{};

	constexpr bool operator==(const ConcatMatrixCommand&) const noexcept = default;
};

struct SetStrokeStyleCommand {
	float line_width{1.0f};
	LineCap line_cap{LineCap::Butt};
	LineJoin line_join{LineJoin::Miter};
	float miter_limit{10.0f};
	uint32_t dash_offset{0}; // Offset into `DisplayList::dash_values()`
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
	ColorRgba stroke_color{};
	ColorRgba fill_color{};
	bool stroke{false};
	bool fill{false};
	FillRule fill_rule{FillRule::NonZero};

	constexpr bool operator==(const DrawPathCommand&) const noexcept = default;
};

struct RecordedGlyphRun {
	uint32_t glyph_offset{0}; // Offset into `DisplayList::glyphs()`
	uint32_t glyph_count{0};
	double font_size{12.0};
	TextRenderMode render_mode{TextRenderMode::Fill};
	ColorRgba fill_color{};
	ColorRgba stroke_color{};
	PdfMatrix text_matrix{}; // Text/glyph-to-user matrix
	PdfMatrix ctm{};         // Active CTM when the glyph run was recorded
	PdfRect bounds_pt{};     // Glyph run ink bounds in text/local space
	bool has_bounds{false};
	// Opaque immutable font handle (`std::shared_ptr<const LoadedFont>` in §2)
	// and optional Type 3 procedural glyph sub-display-list.
	std::shared_ptr<const void> font_handle;
	std::shared_ptr<const DisplayList> type3_sub_list;
};

using DrawGlyphRunCommand = RecordedGlyphRun;

// Pixel cell `(column c, row r)` of an `width x height` bitmap (or soft mask)
// covers `[c / width, (c + 1) / width] x [1 - (r + 1) / height, 1 - r / height]`
// of the unit square, which `dest_transform` maps to user space: row 0 is the
// image's first row, at the top (ISO 32000-1 section 8.9.4). With `interpolate`
// the painted footprint of a cell may reach half a cell beyond it (bilinear
// sampling), clamped to the unit square.
struct DrawImageCommand {
	std::shared_ptr<const DecodedBitmap> bitmap;
	PdfMatrix dest_transform{}; // Maps unit square `[0,1]x[0,1]` to user space
	bool interpolate{true};
	std::shared_ptr<const DecodedBitmap> soft_mask;
	ColorRgba mask_tint_color{ColorRgba::black()};
};

// Tag enum matching `DisplayCommand::Payload` alternative indices.
enum class DisplayCommandKind : uint8_t {
	SaveState = 0,
	RestoreState = 1,
	ConcatMatrix = 2,
	SetStrokeStyle = 3,
	SetAlphaAndBlend = 4,
	PushClipPath = 5,
	DrawPath = 6,
	DrawGlyphRun = 7,
	DrawImage = 8,
};

// Discriminated value type for a single recorded display-list command.
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

	Payload payload;

	DisplayCommand() = default;

	template <typename CmdT>
		requires (!std::is_same_v<std::remove_cvref_t<CmdT>, DisplayCommand>)
	explicit DisplayCommand(CmdT&& cmd)
		: payload(std::forward<CmdT>(cmd))
	{
	}

	[[nodiscard]] DisplayCommandKind kind() const noexcept
	{
		return static_cast<DisplayCommandKind>(payload.index());
	}

	template <typename CmdT>
	[[nodiscard]] const CmdT* as() const noexcept
	{
		return std::get_if<CmdT>(&payload);
	}
};

// Page-space box a recorded command paints, entry `i` of
// `DisplayList::command_bounds()` describing `DisplayList::commands()[i]`.
// `has_bounds` is false for state commands and for draws that paint nothing
// (invisible, fully transparent or fully clipped).
struct CommandBounds {
	PdfRect bounds_pt{};
	bool has_bounds{false};

	constexpr bool operator==(const CommandBounds&) const noexcept = default;
};

// Immutable, reference-counted display list for a single PDF page (or Type 3
// glyph / Form XObject sub-list).
//
// Invariant (`frozen: true` check):
// - Constructed exclusively by `DisplayListBuilder::finish()`.
// - Exposes `const`-only accessors over contiguous vectors and
//   `std::shared_ptr<const DecodedBitmap>` handles.
// - Contains zero non-`const` methods, zero `mutable` members, and zero lazy
//   caches.
class DisplayList {
public:
	~DisplayList() noexcept;

	DisplayList(const DisplayList&) = delete;
	DisplayList& operator=(const DisplayList&) = delete;
	DisplayList(DisplayList&&) = delete;
	DisplayList& operator=(DisplayList&&) = delete;

	// The page box for which this display list was recorded, exactly as the
	// caller passed it: the page's crop box in unrotated default user space
	// (points), non-zero origins kept. `/Rotate` is applied by the consumer;
	// `/UserUnit` belongs to D01 T02 section 21.
	[[nodiscard]] const PdfRect& page_box_pt() const noexcept
	{
		return page_box_pt_;
	}

	// True if at least one visible path, glyph run, or image contributed a box
	// inside the active clip boxes and the page box.
	[[nodiscard]] bool has_content_bounds() const noexcept
	{
		return has_content_bounds_;
	}

	// Bounding-box-device content bounds in the same space as `page_box_pt()`
	// (the contract a bounding-box rendering device keeps): the union over painted
	// primitives of each primitive's exact page-space box (analytic cubic
	// extrema; strokes widened by half the line width, projecting caps and
	// miter joins up to the miter limit; glyph ink boxes; the opaque-pixel box
	// of an image intersected with its soft mask's), each intersected with the
	// bounding boxes of the active clips and with `page_box_pt()`. It contains
	// every painted point (it never cuts ink); it is tight under rectangular
	// clips and looser under curved clips and dashes. Used by `Auto-Detect
	// White Margins` in `D05 T01 §4`. Returns an empty `PdfRect{}` when
	// `has_content_bounds()` is false.
	[[nodiscard]] const PdfRect& content_bounds_pt() const noexcept
	{
		return content_bounds_pt_;
	}

	// Per-command painted boxes under the same contract as
	// `content_bounds_pt()`, parallel to `commands()`; their union is
	// `content_bounds_pt()`. Lets `D05 T01 §4` leave out a full-page background
	// fill when detecting margins.
	[[nodiscard]] std::span<const CommandBounds> command_bounds() const noexcept
	{
		return command_bounds_;
	}

	[[nodiscard]] size_t command_count() const noexcept
	{
		return commands_.size();
	}

	[[nodiscard]] std::span<const DisplayCommand> commands() const noexcept
	{
		return commands_;
	}

	[[nodiscard]] std::span<const RecordedPath> paths() const noexcept
	{
		return paths_;
	}

	[[nodiscard]] PdfResult<RecordedPath> path(uint32_t path_index) const noexcept
	{
		if (path_index >= paths_.size()) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayList::path index out of range");
		}
		return paths_[path_index];
	}

	[[nodiscard]] std::span<const PathVerb> path_verbs() const noexcept
	{
		return path_verbs_;
	}

	[[nodiscard]] PdfResult<std::span<const PathVerb>> path_verbs(
		const RecordedPath& p) const noexcept
	{
		if (p.verb_offset > path_verbs_.size() ||
			p.verb_count > path_verbs_.size() - p.verb_offset) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayList::path_verbs slice out of range");
		}
		return std::span<const PathVerb>(path_verbs_).subspan(p.verb_offset, p.verb_count);
	}

	[[nodiscard]] std::span<const PdfPoint> path_points() const noexcept
	{
		return path_points_;
	}

	[[nodiscard]] PdfResult<std::span<const PdfPoint>> path_points(
		const RecordedPath& p) const noexcept
	{
		if (p.point_offset > path_points_.size() ||
			p.point_count > path_points_.size() - p.point_offset) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayList::path_points slice out of range");
		}
		return std::span<const PdfPoint>(path_points_).subspan(p.point_offset, p.point_count);
	}

	[[nodiscard]] std::span<const PositionedGlyph> glyphs() const noexcept
	{
		return glyphs_;
	}

	[[nodiscard]] PdfResult<std::span<const PositionedGlyph>> glyphs(
		const RecordedGlyphRun& run) const noexcept
	{
		if (run.glyph_offset > glyphs_.size() ||
			run.glyph_count > glyphs_.size() - run.glyph_offset) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayList::glyphs slice out of range");
		}
		return std::span<const PositionedGlyph>(glyphs_).subspan(run.glyph_offset, run.glyph_count);
	}

	[[nodiscard]] std::span<const float> dash_values() const noexcept
	{
		return dash_values_;
	}

	[[nodiscard]] PdfResult<std::span<const float>> dash_values(
		const SetStrokeStyleCommand& style) const noexcept
	{
		if (style.dash_offset > dash_values_.size() ||
			style.dash_count > dash_values_.size() - style.dash_offset) {
			return make_pdf_error(
				ErrorCode::DamagedFormat,
				"DisplayList::dash_values slice out of range");
		}
		return std::span<const float>(dash_values_).subspan(style.dash_offset, style.dash_count);
	}

private:
	friend class DisplayListBuilder;

	DisplayList(
		PdfRect page_box_pt,
		PdfRect content_bounds_pt,
		bool has_content_bounds,
		std::vector<DisplayCommand>&& commands,
		std::vector<CommandBounds>&& command_bounds,
		std::vector<RecordedPath>&& paths,
		std::vector<PathVerb>&& path_verbs,
		std::vector<PdfPoint>&& path_points,
		std::vector<PositionedGlyph>&& glyphs,
		std::vector<float>&& dash_values) noexcept;

	const PdfRect page_box_pt_;
	const PdfRect content_bounds_pt_;
	const bool has_content_bounds_{false};
	const std::vector<DisplayCommand> commands_;
	const std::vector<CommandBounds> command_bounds_;
	const std::vector<RecordedPath> paths_;
	const std::vector<PathVerb> path_verbs_;
	const std::vector<PdfPoint> path_points_;
	const std::vector<PositionedGlyph> glyphs_;
	const std::vector<float> dash_values_;
};

// Configurable resource budgets for `DisplayListBuilder` to bound memory,
// clip/save nesting depth, and computational geometry work against hostile
// content streams. Every count limit and the byte budget is checked before the
// pool it guards grows; a call that would pass one returns
// `ErrorCode::ResourceLimit` and records nothing.
struct DisplayListLimits {
	size_t max_commands{1'000'000u};
	size_t max_paths{250'000u};
	size_t max_path_verbs{4'000'000u};
	size_t max_path_points{8'000'000u};
	size_t max_glyphs{4'000'000u};
	size_t max_dash_values{250'000u};
	size_t max_save_depth{64u};
	size_t max_clip_depth{64u};
	// Path verbs, points and glyphs walked by bounds computation, summed over
	// every clip and draw (a hostile stream can draw one huge path many times).
	size_t max_geometry_work_ops{100'000'000u};
	// Heap the builder's pools hold by capacity, including the old buffer a
	// growing pool still holds while it reallocates.
	size_t max_total_bytes{256ull * 1024ull * 1024ull};
	// Optional deterministic fault-injection counter (`0` = disabled): when
	// non-zero, the N-th mutating builder call (each charges one step before it
	// allocates, `finish()` included) and every later one fail with
	// `ErrorCode::OutOfMemory`.
	size_t fail_allocation_after_ops{0u};
	// Optional deterministic fault injection specifically targeting the
	// `std::shared_ptr<const DisplayList>` control-block allocation in `finish()`
	// after the `DisplayList` object itself has been constructed and the
	// builder's storage moved into it.
	bool fail_finish_control_block_allocation{false};
};

// Single-threaded builder used on the `DocumentActor` thread to record a page's
// display list and seal it into an immutable `std::shared_ptr<const DisplayList>`.
class DisplayListBuilder {
public:
	explicit DisplayListBuilder(
		PdfRect page_box_pt = PdfRect{0.0, 0.0, 612.0, 792.0},
		DisplayListLimits limits = {}) noexcept;

	DisplayListBuilder(const DisplayListBuilder&) = delete;
	DisplayListBuilder& operator=(const DisplayListBuilder&) = delete;

	[[nodiscard]] PdfResult<void> reserve_commands(size_t count) noexcept;

	// Interns a path geometry into the builder's contiguous verb/point pools
	// and returns its `path_index` for `push_clip_path()` and `draw_path()`.
	[[nodiscard]] PdfResult<uint32_t> add_path(const PathBuilder& path) noexcept;
	[[nodiscard]] PdfResult<uint32_t> add_path(
		std::span<const PathVerb> verbs,
		std::span<const PdfPoint> points) noexcept;

	[[nodiscard]] PdfResult<void> save_state() noexcept;
	[[nodiscard]] PdfResult<void> restore_state() noexcept;
	[[nodiscard]] PdfResult<void> concat_matrix(const PdfMatrix& matrix) noexcept;

	[[nodiscard]] PdfResult<void> set_stroke_style(
		float line_width,
		LineCap line_cap = LineCap::Butt,
		LineJoin line_join = LineJoin::Miter,
		float miter_limit = 10.0f,
		std::span<const float> dash_array = {},
		float dash_phase = 0.0f) noexcept;

	[[nodiscard]] PdfResult<void> set_alpha_and_blend(
		float stroke_alpha,
		float fill_alpha,
		BlendMode blend_mode = BlendMode::Normal) noexcept;

	[[nodiscard]] PdfResult<void> push_clip_path(
		uint32_t path_index,
		FillRule fill_rule = FillRule::NonZero) noexcept;

	[[nodiscard]] PdfResult<void> draw_path(
		uint32_t path_index,
		ColorRgba stroke_color,
		ColorRgba fill_color,
		bool stroke,
		bool fill,
		FillRule fill_rule = FillRule::NonZero) noexcept;

	// Charges `ops` units of work a caller did toward a command, recorded or not, against
	// `DisplayListLimits::max_geometry_work_ops`; `ResourceLimit` past it.
	[[nodiscard]] PdfResult<void> charge_work(size_t ops) noexcept;

	// Glyphs this list may still take before `DisplayListLimits::max_glyphs` or its byte budget, so a caller can stop
	// building a run that could not be recorded instead of allocating all of it first.
	[[nodiscard]] size_t glyph_room() const noexcept;

	// Records a positioned glyph run.
	[[nodiscard]] PdfResult<void> draw_glyph_run(
		std::span<const PositionedGlyph> glyphs,
		double font_size,
		TextRenderMode render_mode,
		ColorRgba fill_color,
		ColorRgba stroke_color,
		const PdfMatrix& text_matrix = PdfMatrix::identity(),
		std::optional<PdfRect> local_bounds_pt = std::nullopt,
		std::shared_ptr<const void> font_handle = nullptr,
		std::shared_ptr<const DisplayList> type3_sub_list = nullptr) noexcept;

	[[nodiscard]] PdfResult<void> draw_image(
		std::shared_ptr<const DecodedBitmap> bitmap,
		const PdfMatrix& dest_transform,
		bool interpolate = true,
		std::shared_ptr<const DecodedBitmap> soft_mask = nullptr,
		ColorRgba mask_tint_color = ColorRgba::black()) noexcept;

	[[nodiscard]] bool is_finished() const noexcept { return finished_; }
	[[nodiscard]] size_t command_count() const noexcept { return commands_.size(); }
	[[nodiscard]] size_t path_count() const noexcept { return paths_.size(); }

	// Heap bytes the builder's pools hold by capacity (commands, command
	// bounds, paths, verbs, points, glyphs, dash values, saved states), the
	// figure `DisplayListLimits::max_total_bytes` bounds.
	[[nodiscard]] size_t retained_bytes() const noexcept { return current_pool_bytes(); }

	// Seals the builder (one-shot) and returns the immutable `DisplayList`.
	// Subsequent calls to `finish()` or any mutation method after `finish()`
	// return `ErrorCode::InternalError`.
	[[nodiscard]] PdfResult<std::shared_ptr<const DisplayList>> finish() noexcept;

private:
	// Graphics state the builder tracks for bounds. Fixed size, so a saved
	// state costs exactly one `state_stack_` slot.
	struct TrackingState {
		PdfMatrix ctm{PdfMatrix::identity()};
		float line_width{1.0f};
		LineCap line_cap{LineCap::Butt};
		LineJoin line_join{LineJoin::Miter};
		float miter_limit{10.0f};
		bool dashed{false}; // a dash pattern is set (caps at every dash end)
		float stroke_alpha{1.0f};
		float fill_alpha{1.0f};
		// Intersection of `page_box_pt_` and the exact page-space boxes of every
		// pushed clip path; `clip_is_empty` once that intersection has no area.
		PdfRect clip_box_pt{};
		bool clip_is_empty{false};
		size_t clip_depth{0u};
	};

	// One pool's planned growth for `reserve_growth()`: append `extra`
	// elements to `*pool` without passing `max_elements`.
	template <typename T>
	struct Growth {
		std::vector<T>* pool{nullptr};
		size_t extra{0u};
		size_t max_elements{0u};
	};

	[[nodiscard]] size_t current_pool_bytes() const noexcept;
	[[nodiscard]] PdfResult<void> check_can_mutate() const noexcept;
	[[nodiscard]] PdfResult<void> charge_allocation_op() noexcept;
	[[nodiscard]] PdfResult<void> charge_geometry_work(size_t ops) noexcept;
	template <typename... Ts>
	[[nodiscard]] PdfResult<void> reserve_growth(Growth<Ts>... growths) noexcept;
	[[nodiscard]] PdfResult<void> reserve_command_slot() noexcept;
	[[nodiscard]] std::optional<PdfRect> clip_to_state(const PdfRect& box) const noexcept;
	void commit_command(DisplayCommand&& command, std::optional<PdfRect> bounds) noexcept;

	PdfRect page_box_pt_{0.0, 0.0, 612.0, 792.0};
	DisplayListLimits limits_{};
	PdfRect content_bounds_pt_{};
	bool has_content_bounds_{false};
	bool finished_{false};
	std::optional<PdfError> sticky_error_;
	size_t allocation_ops_{0u};
	size_t geometry_work_ops_{0u};

	TrackingState current_state_{};
	std::vector<TrackingState> state_stack_;

	std::vector<DisplayCommand> commands_;
	std::vector<CommandBounds> command_bounds_;
	std::vector<RecordedPath> paths_;
	std::vector<PathVerb> path_verbs_;
	std::vector<PdfPoint> path_points_;
	std::vector<PositionedGlyph> glyphs_;
	std::vector<float> dash_values_;
};

} // namespace recto::pdf
