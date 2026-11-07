// The content-stream interpreter: graphics state, paths, painting, clipping
// and device colour (ISO 32000-1 sections 7.8 and 8; D01 T02 section 3).
#include "interpreter.h"

#include "color.h"
#include "text_font.h"
#include "lexer.h"
#include "filters.h"
#include "parser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

namespace recto::pdf {

namespace {

// Hashes a std::string key by its characters, so a std::string_view finds it without a copy.
struct StringHash {
	using is_transparent = void;

	[[nodiscard]] size_t operator()(std::string_view key) const noexcept
	{
		return std::hash<std::string_view>{}(key);
	}
};


[[nodiscard]] float unit(double v) noexcept
{
	return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

// A stroke parameter as the float the display list keeps, or none when it does not fit one.
[[nodiscard]] std::optional<float> stroke_float(double v) noexcept
{
	const float f = static_cast<float>(v);
	if (!std::isfinite(f)) {
		return std::nullopt;
	}
	return f;
}

// A colour: its space (null for a space that cannot be used or is section 16's `Pattern`, which paints black) and its
// components, as many as the space has (up to 32 for DeviceN, section 33).
struct Color {
	std::shared_ptr<const ColorSpace> space{ColorSpace::device(ColorSpaceKind::DeviceGray)};
	ColorComponents components{};

	void reset(std::shared_ptr<const ColorSpace> to) noexcept
	{
		space = std::move(to);
		components = space != nullptr ? space->initial() : ColorComponents{};
	}

	// False for a space that marks nothing (a `/None` colorant, section 33).
	[[nodiscard]] bool paints() const noexcept
	{
		return space == nullptr || space->paints();
	}
};

// The graphics state the interpreter keeps beside the builder's (ISO 32000-1 8.4). `style_recorded` and
// `alpha_recorded` say whether the builder already holds this state's stroke style and alphas, so they are recorded
// only when they change before a paint.
struct GraphicsState {
	Color fill{};
	Color stroke{};
	float line_width{1.0f};
	LineCap line_cap{LineCap::Butt};
	LineJoin line_join{LineJoin::Miter};
	float miter_limit{10.0f};
	std::vector<float> dash{};
	float dash_phase{0.0f};
	float stroke_alpha{1.0f};
	float fill_alpha{1.0f};
	BlendMode blend_mode{BlendMode::Normal};
	RenderingIntent rendering_intent{RenderingIntent::RelativeColorimetric};
	double flatness{1.0};
	bool style_recorded{true};
	bool alpha_recorded{true};
	// The text state (ISO 32000-1 9.3), saved and restored by `q` and `Q` with the rest; the text matrices are not
	// part of it (9.4.1). A null font shows text with `TextFont::fallback()`.
	double char_spacing{0.0};
	double word_spacing{0.0};
	double horizontal_scaling{1.0}; // Tz / 100
	double leading{0.0};
	std::shared_ptr<const TextFont> font{};
	double font_size{0.0};
	TextRenderMode text_mode{TextRenderMode::Fill};
	double rise{0.0};
};

[[nodiscard]] constexpr bool mode_fills(TextRenderMode mode) noexcept
{
	return mode == TextRenderMode::Fill || mode == TextRenderMode::FillAndStroke || mode == TextRenderMode::FillAndClip ||
		mode == TextRenderMode::FillStrokeAndClip;
}

[[nodiscard]] constexpr bool mode_strokes(TextRenderMode mode) noexcept
{
	return mode == TextRenderMode::Stroke || mode == TextRenderMode::FillAndStroke ||
		mode == TextRenderMode::StrokeAndClip || mode == TextRenderMode::FillStrokeAndClip;
}

// A text rendering mode with the fill or the stroke dropped when its colour space marks nothing (a `/None` colorant,
// 8.6.6.4): the run keeps its clip and its positions, and only the paint that cannot mark goes.
[[nodiscard]] constexpr TextRenderMode painting_mode(TextRenderMode mode, bool fill_paints, bool stroke_paints) noexcept
{
	const bool fill = mode_fills(mode) && fill_paints;
	const bool stroke = mode_strokes(mode) && stroke_paints;
	if (mode >= TextRenderMode::FillAndClip) {
		return fill && stroke ? TextRenderMode::FillStrokeAndClip
			: fill            ? TextRenderMode::FillAndClip
			: stroke          ? TextRenderMode::StrokeAndClip
							  : TextRenderMode::Clip;
	}
	return fill && stroke ? TextRenderMode::FillAndStroke
		: fill            ? TextRenderMode::Fill
		: stroke          ? TextRenderMode::Stroke
						  : TextRenderMode::Invisible;
}

[[nodiscard]] std::optional<BlendMode> blend_mode_named(std::string_view name) noexcept
{
	static constexpr std::array<std::pair<std::string_view, BlendMode>, 17> kModes = {{
		{"Normal", BlendMode::Normal},
		{"Compatible", BlendMode::Normal},
		{"Multiply", BlendMode::Multiply},
		{"Screen", BlendMode::Screen},
		{"Overlay", BlendMode::Overlay},
		{"Darken", BlendMode::Darken},
		{"Lighten", BlendMode::Lighten},
		{"ColorDodge", BlendMode::ColorDodge},
		{"ColorBurn", BlendMode::ColorBurn},
		{"HardLight", BlendMode::HardLight},
		{"SoftLight", BlendMode::SoftLight},
		{"Difference", BlendMode::Difference},
		{"Exclusion", BlendMode::Exclusion},
		{"Hue", BlendMode::Hue},
		{"Saturation", BlendMode::Saturation},
		{"Color", BlendMode::Color},
		{"Luminosity", BlendMode::Luminosity},
	}};
	for (const auto& [n, mode] : kModes) {
		if (n == name) {
			return mode;
		}
	}
	return std::nullopt;
}

// The operators this section reads (ISO 32000-1 Annex A), and `BI`, whose data section 4 reads but which is skipped here
// so its bytes are never read as operators.
enum class Op : uint8_t {
	Other,
	Save,
	Restore,
	Concat,
	LineWidth,
	LineCap,
	LineJoin,
	MiterLimit,
	Dash,
	Intent,
	Flatness,
	ExtGState,
	MoveTo,
	LineTo,
	CurveTo,
	CurveV,
	CurveY,
	ClosePath,
	Rect,
	Stroke,
	CloseStroke,
	Fill,
	FillEvenOdd,
	FillStroke,
	FillStrokeEvenOdd,
	CloseFillStroke,
	CloseFillStrokeEvenOdd,
	EndPath,
	Clip,
	ClipEvenOdd,
	FillGray,
	StrokeGray,
	FillRgb,
	StrokeRgb,
	FillCmyk,
	StrokeCmyk,
	FillSpace,
	StrokeSpace,
	FillColor,
	StrokeColor,
	BeginInlineImage,
	DrawXObject,
	// Text (D01 T02 section 42).
	BeginText,
	EndText,
	CharSpacing,
	WordSpacing,
	HorizontalScale,
	Leading,
	Font,
	TextMode,
	Rise,
	TextMove,
	TextMoveLeading,
	TextMatrix,
	NextLine,
	ShowText,
	ShowTextArray,
	NextLineShow,
	NextLineShowSpaced,
	BeginMarked,
	BeginMarkedProperties,
	EndMarked,
};

// What an `/OC` value says of its content (ISO 32000-1 8.11.2): only `Off` hides it.
enum class OcAnswer : uint8_t { NoEffect, On, Off };

// Sorted by name (byte order) for the binary search in `op_named`.
inline constexpr std::array<std::pair<std::string_view, Op>, 64> kOperators = {{
	{"\"", Op::NextLineShowSpaced},
	{"'", Op::NextLineShow},
	{"B", Op::FillStroke},
	{"B*", Op::FillStrokeEvenOdd},
	{"BDC", Op::BeginMarkedProperties},
	{"BI", Op::BeginInlineImage},
	{"BMC", Op::BeginMarked},
	{"BT", Op::BeginText},
	{"CS", Op::StrokeSpace},
	{"Do", Op::DrawXObject},
	{"EMC", Op::EndMarked},
	{"ET", Op::EndText},
	{"F", Op::Fill},
	{"G", Op::StrokeGray},
	{"J", Op::LineCap},
	{"K", Op::StrokeCmyk},
	{"M", Op::MiterLimit},
	{"Q", Op::Restore},
	{"RG", Op::StrokeRgb},
	{"S", Op::Stroke},
	{"SC", Op::StrokeColor},
	{"SCN", Op::StrokeColor},
	{"T*", Op::NextLine},
	{"TD", Op::TextMoveLeading},
	{"TJ", Op::ShowTextArray},
	{"TL", Op::Leading},
	{"Tc", Op::CharSpacing},
	{"Td", Op::TextMove},
	{"Tf", Op::Font},
	{"Tj", Op::ShowText},
	{"Tm", Op::TextMatrix},
	{"Tr", Op::TextMode},
	{"Ts", Op::Rise},
	{"Tw", Op::WordSpacing},
	{"Tz", Op::HorizontalScale},
	{"W", Op::Clip},
	{"W*", Op::ClipEvenOdd},
	{"b", Op::CloseFillStroke},
	{"b*", Op::CloseFillStrokeEvenOdd},
	{"c", Op::CurveTo},
	{"cm", Op::Concat},
	{"cs", Op::FillSpace},
	{"d", Op::Dash},
	{"f", Op::Fill},
	{"f*", Op::FillEvenOdd},
	{"g", Op::FillGray},
	{"gs", Op::ExtGState},
	{"h", Op::ClosePath},
	{"i", Op::Flatness},
	{"j", Op::LineJoin},
	{"k", Op::FillCmyk},
	{"l", Op::LineTo},
	{"m", Op::MoveTo},
	{"n", Op::EndPath},
	{"q", Op::Save},
	{"re", Op::Rect},
	{"rg", Op::FillRgb},
	{"ri", Op::Intent},
	{"s", Op::CloseStroke},
	{"sc", Op::FillColor},
	{"scn", Op::FillColor},
	{"v", Op::CurveV},
	{"w", Op::LineWidth},
	{"y", Op::CurveY},
}};

static_assert(std::ranges::is_sorted(kOperators, {}, &std::pair<std::string_view, Op>::first));

[[nodiscard]] Op op_named(std::string_view name) noexcept
{
	const auto it = std::ranges::lower_bound(kOperators, name, {}, &std::pair<std::string_view, Op>::first);
	return it != kOperators.end() && it->first == name ? it->second : Op::Other;
}

// The bitmaps a page may hold and the data it may decode, each, until D01 T02 section 19 reduces images instead.
thread_local size_t page_image_budget = size_t{512} << 20u;

class Interpreter {
public:
	Interpreter(const Document& document, const PdfDict& resources, const PdfRect& page_box, const DisplayListLimits& limits,
		const OptionalContentVisibility* visibility)
		: document_(document)
		, builder_(page_box, limits)
		, uses_groups_(visibility != nullptr)
		, max_depth_(std::min(kMaxGraphicsStateDepth, limits.max_save_depth))
	{
		if (visibility != nullptr) {
			off_groups_ = sorted_keys(visibility->off);
			excluded_groups_ = sorted_keys(visibility->excluded);
		}
		auto page = std::make_unique<ResourceScope>();
		page->serial = ++scope_serial_;
		page->resources = &resources;
		scope_ = page.get();
		scopes_.push_back(std::move(page));
	}

	[[nodiscard]] PdfResult<std::shared_ptr<const DisplayList>> run(std::span<const uint8_t> content) noexcept
	{
		try {
			RECTO_PDF_RETURN_IF_ERROR(interpret(content));
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory interpreting a content stream");
		}
		return builder_.finish();
	}

private:
	struct ImageSource;
	struct InlineImage;

	// Damaged content (`DamagedFormat`) skips the operator it belongs to; every other lexer, parser or resolver failure
	// (memory, a decoder or recursion limit, an internal error) stops the page and is returned.
	[[nodiscard]] static bool fatal(const PdfError& error) noexcept
	{
		return error.code != ErrorCode::DamagedFormat;
	}

	// Interprets `content` and every Form XObject it draws, on an explicit stack of frames rather than the native one
	// (a form pushes a frame and its content runs in this loop), so nested forms cost heap, not stack. A frame that
	// ends gives its form's caller back its state; a fatal error unwinds every frame the same way before it returns.
	[[nodiscard]] PdfResult<void> interpret(std::span<const uint8_t> content)
	{
		frames_.push_back(Frame{nullptr, std::make_unique<Parser>(content), {}, nullptr});
		auto result = run_frames();
		while (!frames_.empty()) {
			(void)end_frame();
		}
		return result;
	}

	[[nodiscard]] PdfResult<void> run_frames()
	{
		while (!frames_.empty()) {
			const size_t top = frames_.size() - 1u;
			Parser& parser = *frames_[top].parser; // owned by the frame, so it stays put while frames are pushed
			Lexer& lexer = parser.lexer();
			const size_t before = lexer.position();
			auto peeked = lexer.peek_token();
			if (!peeked.has_value()) {
				if (fatal(peeked.error())) {
					return peeked.error();
				}
				// A token the lexer cannot read: the operator it belongs to is skipped, and reading resumes past it.
				drop_operands(frames_[top].operands);
				resync(lexer, before);
				continue;
			}
			if (peeked->kind == TokenKind::Eof) {
				RECTO_PDF_RETURN_IF_ERROR(end_frame());
				continue;
			}
			if (peeked->kind == TokenKind::Keyword) {
				auto op = lexer.next_token();
				if (!op.has_value()) {
					if (fatal(op.error())) {
						return op.error();
					}
					drop_operands(frames_[top].operands);
					resync(lexer, before);
					continue;
				}
				const Op code = op_named(op->text.view());
				if (code == Op::BeginInlineImage) {
					auto skipped = read_inline_image(parser, !skip_next_);
					if (resolve_failure_.has_value()) {
						return *resolve_failure_; // a colour space the image named did not resolve, the cause of any refusal
					}
					RECTO_PDF_RETURN_IF_ERROR(skipped);
					frames_[top].operands.clear();
					skip_next_ = false;
					continue;
				}
				// `frames_` is a deque, so the operands stay where they are if `Do` pushes a form's frame.
				RECTO_PDF_RETURN_IF_ERROR(dispatch(code, frames_[top].operands));
				frames_[top].operands.clear();
				continue;
			}
			auto operand = parser.parse_direct_object();
			if (!operand.has_value()) {
				if (fatal(operand.error())) {
					return operand.error();
				}
				drop_operands(frames_[top].operands);
				resync(lexer, before);
				continue;
			}
			if (frames_[top].operands.size() >= kMaxContentOperands) {
				drop_operands(frames_[top].operands);
				continue;
			}
			frames_[top].operands.push_back(std::move(*operand));
		}
		return {};
	}

	// Ends the top frame: a form's gives its caller back everything the form changed.
	[[nodiscard]] PdfResult<void> end_frame()
	{
		std::unique_ptr<FormExit> exit = std::move(frames_.back().form);
		frames_.pop_back();
		if (exit == nullptr) {
			return {};
		}
		return leave_form(*exit);
	}

	// The operator these operands belong to is skipped: an operand was unreadable or past the ceiling.
	void drop_operands(std::vector<PdfValue>& operands) noexcept
	{
		operands.clear();
		skip_next_ = true;
	}

	// Moves past an unreadable token: to where the lexer stopped, or one byte on if it did not move.
	static void resync(Lexer& lexer, size_t before) noexcept
	{
		const size_t after = lexer.position();
		lexer.seek(after > before ? after : before + 1u);
	}

	[[nodiscard]] static bool is_white(uint8_t b) noexcept
	{
		return b == 0x20u || b == 0x0Au || b == 0x0Du || b == 0x09u || b == 0x0Cu || b == 0x00u;
	}

	// The components of an inline image's colour space (ISO 32000-1 Table 93 abbreviations, or a `/ColorSpace`
	// resource), or none when this section cannot tell.
	[[nodiscard]] std::optional<size_t> inline_components(const PdfValue& space, int depth = 0)
	{
		const auto by_name = [](std::string_view n) -> std::optional<size_t> {
			if (n == "G" || n == "DeviceGray" || n == "I" || n == "Indexed" || n == "CalGray" || n == "Separation") {
				return 1u;
			}
			if (n == "RGB" || n == "DeviceRGB" || n == "CalRGB" || n == "Lab") {
				return 3u;
			}
			if (n == "CMYK" || n == "DeviceCMYK") {
				return 4u;
			}
			return std::nullopt;
		};
		if (const auto n = space.as_name()) {
			if (const auto count = by_name(*n)) {
				return count;
			}
			if (depth == 0) {
				if (const auto named = resource("ColorSpace", scope_->color_spaces, *n)) {
					return inline_components(*named, 1);
				}
			}
			return std::nullopt;
		}
		const PdfArray* array = space.as_array();
		if (array == nullptr || array->empty()) {
			return std::nullopt;
		}
		const auto head = resolved((*array)[0]);
		const auto family = head.has_value() ? head->as_name() : std::nullopt;
		if (!family.has_value()) {
			return std::nullopt;
		}
		if (*family == "DeviceN" && array->size() >= 2u) {
			const auto names = resolved((*array)[1]);
			if (names.has_value() && names->as_array() != nullptr && !names->as_array()->empty()) {
				return names->as_array()->size();
			}
			return std::nullopt;
		}
		if (*family == "ICCBased" && array->size() >= 2u) {
			const auto profile = resolved((*array)[1]);
			const PdfStream* stream = profile.has_value() ? profile->as_stream() : nullptr;
			const PdfValue* n = stream != nullptr ? stream->dict.get("N") : nullptr;
			const auto count = n != nullptr ? n->as_int() : std::nullopt;
			if (count.has_value() && *count >= 1 && *count <= 32) {
				return static_cast<size_t>(*count);
			}
			return std::nullopt;
		}
		return by_name(*family);
	}

	using InlineEntries = std::vector<std::pair<std::string, PdfValue>>;

	// An inline image's dictionary keys that are read, abbreviated and in full (Table 93).
	static constexpr std::array<std::pair<std::string_view, std::string_view>, 10> kInlineKeys = {{
		{"W", "Width"},
		{"H", "Height"},
		{"BPC", "BitsPerComponent"},
		{"CS", "ColorSpace"},
		{"D", "Decode"},
		{"DP", "DecodeParms"},
		{"F", "Filter"},
		{"IM", "ImageMask"},
		{"I", "Interpolate"},
		{"Intent", "Intent"},
	}};

	// An inline image entry by its abbreviated or full key (ISO 32000-1 Table 92).
	[[nodiscard]] static const PdfValue* inline_entry(const InlineEntries& entries, std::string_view abbreviation, std::string_view key) noexcept
	{
		for (const auto& [name, value] : entries) {
			if (name == abbreviation || name == key) {
				return &value;
			}
		}
		return nullptr;
	}

	[[nodiscard]] static bool inline_filtered(const InlineEntries& entries) noexcept
	{
		const PdfValue* filter = inline_entry(entries, "F", "Filter");
		return filter != nullptr && !(filter->as_array() != nullptr && filter->as_array()->empty());
	}

	// The byte length of an inline image's samples once decoded, or none when this section cannot tell.
	[[nodiscard]] std::optional<size_t> inline_sample_bytes(const InlineEntries& entries)
	{
		const PdfValue* w = inline_entry(entries, "W", "Width");
		const PdfValue* h = inline_entry(entries, "H", "Height");
		if (w == nullptr || h == nullptr) {
			return std::nullopt;
		}
		const auto width = w->as_int();
		const auto height = h->as_int();
		if (!width.has_value() || !height.has_value() || *width <= 0 || *height <= 0 || *width > 65'536 || *height > 65'536) {
			return std::nullopt;
		}
		size_t bpc = 0;
		size_t comps = 0;
		const PdfValue* mask = inline_entry(entries, "IM", "ImageMask");
		if (mask != nullptr && mask->as_bool().value_or(false)) {
			bpc = 1u;
			comps = 1u;
		} else {
			const PdfValue* b = inline_entry(entries, "BPC", "BitsPerComponent");
			const PdfValue* cs = inline_entry(entries, "CS", "ColorSpace");
			const auto bits = b != nullptr ? b->as_int() : std::nullopt;
			if (!bits.has_value() || (*bits != 1 && *bits != 2 && *bits != 4 && *bits != 8 && *bits != 16) || cs == nullptr) {
				return std::nullopt;
			}
			const auto count = inline_components(*cs);
			if (!count.has_value() || *count == 0u || *count > 32u) {
				return std::nullopt;
			}
			bpc = static_cast<size_t>(*bits);
			comps = *count;
		}
		// At most 65536 x 32 x 16 bits a row and 65536 rows: no overflow in 64 bits.
		const size_t row = (static_cast<size_t>(*width) * comps * bpc + 7u) / 8u;
		return row * static_cast<size_t>(*height);
	}

	// An `EI` that could end the image's data at `at`: whitespace (or the data's start) before it, whitespace or the
	// end after it.
	[[nodiscard]] static bool ei_candidate(std::span<const uint8_t> input, size_t data_start, size_t at) noexcept
	{
		return at + 2u <= input.size() && input[at] == 'E' && input[at + 1u] == 'I' &&
			(at == data_start || (at > 0u && is_white(input[at - 1u]))) && (at + 2u == input.size() || is_white(input[at + 2u]));
	}

	// `BI ... ID data EI`, framed and then drawn through the image path (D01 T02 section 45); the data is never read as
	// operators. The dictionary is parsed to find `ID`. An unfiltered image's data is skipped by its length. A
	// filtered one ends at the first `EI` before which the data decodes through its filters to the image's full size:
	// an `EI` inside compressed data truncates it, so it cannot. Checking candidates may decode at most 64 MiB in all;
	// past that the page is refused (`InlineImageFraming`), and a candidate the decoder refuses for any reason but
	// damage refuses the page with that error. An image whose end cannot be verified this way (its size unknown, its
	// data damaged) refuses the page with `DamagedFormat`: no `EI` is ever taken on trust.
	[[nodiscard]] PdfResult<void> read_inline_image(Parser& parser, bool draws)
	{
		static constexpr size_t kMaxInlineEntries = 64u;
		static constexpr size_t kMaxFramingWork = 64u * 1024u * 1024u;
		Lexer& lexer = parser.lexer();
		const std::span<const uint8_t> input = lexer.input();
		InlineEntries entries;
		std::optional<size_t> data_start;
		for (size_t pair = 0; pair <= kMaxInlineEntries; ++pair) {
			auto peeked = lexer.peek_token();
			if (!peeked.has_value()) {
				if (fatal(peeked.error())) {
					return peeked.error();
				}
				break;
			}
			if (peeked->kind == TokenKind::Eof) {
				return {};
			}
			if (peeked->kind == TokenKind::Keyword) {
				const bool id = peeked->text.view() == "ID";
				auto keyword = lexer.next_token();
				if (!keyword.has_value()) {
					return fatal(keyword.error()) ? PdfResult<void>{keyword.error()} : PdfResult<void>{};
				}
				if (!id) {
					return {}; // `BI` without `ID`: nothing to skip
				}
				data_start = std::min(lexer.position() + 1u, input.size()); // one whitespace byte after `ID`
				break;
			}
			auto key = parser.parse_direct_object();
			if (!key.has_value()) {
				if (fatal(key.error())) {
					return key.error();
				}
				break;
			}
			auto value = parser.parse_direct_object();
			if (!value.has_value()) {
				if (fatal(value.error())) {
					return value.error();
				}
				break;
			}
			if (const auto name = key->as_name()) {
				entries.emplace_back(std::string(*name), std::move(*value));
			}
		}
		// An image whose end cannot be verified is never skipped by guessing, which could run its pixels as operators:
		// the page is refused (`D01 T02 §19` keeps what was recorded before it).
		const auto unframed = [](std::string_view why) {
			return make_pdf_error(ErrorCode::DamagedFormat, why);
		};
		if (!data_start.has_value()) {
			return unframed("An inline image's dictionary could not be read to its ID");
		}
		// A form drawn again reads the same bytes: its verified end and its source are kept by the data's address, in
		// content the page holds for its life, so nothing is framed or decoded again.
		const uint8_t* const memo_key = input.data() + *data_start;
		if (const auto it = inline_images_.find(memo_key); it != inline_images_.end()) {
			lexer.seek(it->second.end + 2u);
			return draws ? draw_inline_image(it->second, std::nullopt) : PdfResult<void>{};
		}
		const auto framed = [&](size_t start, size_t length, size_t end, std::optional<std::vector<uint8_t>> decoded) -> PdfResult<void> {
			lexer.seek(end + 2u);
			InlineImage image;
			image.end = end;
			image.data = input.subspan(start, length);
			// Kept only what is read, and charged: a dictionary can hold far more than the image it describes.
			size_t dictionary_bytes = 0;
			for (auto& entry : entries) {
				const bool read = std::any_of(kInlineKeys.begin(), kInlineKeys.end(), [&](const auto& key) {
					return entry.first == key.first || entry.first == key.second;
				});
				if (read) {
					dictionary_bytes += entry.first.capacity() + entry.second.heap_bytes();
					image.entries.push_back(std::move(entry));
				}
			}
			if (!hold_image_bytes(dictionary_bytes)) {
				record_image_refusal(dictionary_bytes);
				image.entries.clear();
				image.prepared = true;
				image.source.usable = false;
			}
			if (inline_images_.size() < kMaxHeldResources) {
				InlineImage& kept = inline_images_.emplace(memo_key, std::move(image)).first->second;
				return draws ? draw_inline_image(kept, std::move(decoded)) : PdfResult<void>{};
			}
			return draws ? draw_inline_image(image, std::move(decoded)) : PdfResult<void>{};
		};
		const auto samples = inline_sample_bytes(entries);
		if (!samples.has_value()) {
			return unframed("An inline image's size could not be determined, so its end could not be found");
		}
		if (!inline_filtered(entries)) {
			// The data, then optional whitespace, then `EI`. A writer that put CR LF after `ID` gets its data from the
			// byte after the LF.
			for (size_t start : {*data_start, *data_start + 1u}) {
				if (start > input.size() || *samples > input.size() - start) {
					continue;
				}
				if (start != *data_start && !(start >= 2u && input[start - 2u] == '\r' && input[start - 1u] == '\n')) {
					continue;
				}
				size_t after = start + *samples;
				while (after < input.size() && is_white(input[after])) {
					++after;
				}
				if (ei_candidate(input, after, after)) {
					return framed(start, *samples, after, std::nullopt);
				}
			}
			return unframed("An inline image's data is not followed by EI where its size says it ends");
		}
		PdfDict filters;
		filters.set("Filter", *inline_entry(entries, "F", "Filter"));
		if (const PdfValue* parms = inline_entry(entries, "DP", "DecodeParms")) {
			filters.set("DecodeParms", *parms);
		}
		size_t work = 0;
		for (size_t end = *data_start; end + 2u <= input.size(); ++end) {
			if (!ei_candidate(input, *data_start, end)) {
				continue;
			}
			const size_t length = end - *data_start;
			if (length > kMaxFramingWork - work) {
				return make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::InlineImageFraming,
					static_cast<uint64_t>(work) + length, kMaxFramingWork,
					"An inline image's end was not found within the decoding work limit");
			}
			work += length;
			const auto data = input.subspan(*data_start, length);
			// Room past the samples for padding a writer adds; data that decodes past it is refused with the decoder's
			// limit.
			auto decoded = decode_stream(data, filters, *samples + std::max<size_t>(*samples, 64u * 1024u));
			if (decoded.has_value()) {
				if (decoded->size() >= *samples) {
					return framed(*data_start, length, end, std::move(*decoded));
				}
				continue; // too short: this `EI` cuts the data
			}
			if (fatal(decoded.error())) {
				return decoded.error(); // a filter this build cannot decode, a decoder limit, memory or an internal error
			}
			// Damaged: this `EI` truncates the data.
		}
		return unframed("An inline image's data decodes at no EI after it, so its end could not be found");
	}

	// An inline image's colour space in full (8.9.7): the device abbreviations, and an Indexed array with its base's.
	[[nodiscard]] static PdfResult<PdfValue> inline_space_expanded(const PdfValue& space)
	{
		const auto expanded_name = [](std::string_view name) -> std::string_view {
			return name == "G" ? "DeviceGray" : name == "RGB" ? "DeviceRGB" : name == "CMYK" ? "DeviceCMYK" : name;
		};
		if (const std::optional<std::string_view> name = space.as_name()) {
			return PdfValue(PdfName::from_decoded(expanded_name(*name)));
		}
		const PdfArray* array = space.as_array();
		const std::optional<std::string_view> family = array != nullptr && !array->empty() ? (*array)[0].as_name() : std::nullopt;
		if (family != std::optional<std::string_view>("I") && family != std::optional<std::string_view>("Indexed")) {
			return space.deep_clone();
		}
		PdfArray out;
		out.push_back(PdfValue(PdfName::from_decoded("Indexed")));
		for (size_t i = 1u; i < array->size(); ++i) {
			const std::optional<std::string_view> name = i == 1u ? (*array)[i].as_name() : std::nullopt;
			if (name.has_value()) {
				out.push_back(PdfValue(PdfName::from_decoded(expanded_name(*name))));
			} else {
				RECTO_PDF_TRY_REF(element, (*array)[i].deep_clone());
				out.push_back(std::move(element));
			}
		}
		return PdfValue(std::move(out));
	}

	// An inline image's source: a stream built from its dictionary (Table 93's keys in full, /I here being /Interpolate)
	// and its data, read as an image object is, built when it is first drawn, so a hidden one costs only its framing.
	// False when nothing can be drawn, recorded.
	[[nodiscard]] PdfResult<bool> inline_source(InlineImage& image, std::optional<std::vector<uint8_t>> framing_decode)
	{
		ImageSource& source = image.source;
		const InlineEntries& entries = image.entries;
		const std::span<const uint8_t> data = image.data;
		auto stream = std::make_shared<PdfStream>();
		for (const auto& [abbreviation, key] : kInlineKeys) {
			const PdfValue* value = inline_entry(entries, abbreviation, key);
			if (value == nullptr) {
				continue;
			}
			if (key == "ColorSpace") {
				RECTO_PDF_TRY_REF(space, inline_space_expanded(*value));
				stream->dict.set(key, std::move(space));
			} else {
				RECTO_PDF_TRY_REF(cloned, value->deep_clone());
				stream->dict.set(key, std::move(cloned));
			}
		}
		const std::string filter = last_filter(stream->dict);
		if (filter == "JBIG2Decode" || filter == "JPXDecode") {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An inline image cannot use JBIG2Decode or JPXDecode (8.9.7); it is not drawn"); // recorded
			return false;
		}
		if (!hold_image_bytes(data.size())) {
			record_image_refusal(data.size());
			return false;
		}
		stream->raw_bytes.assign(data.begin(), data.end());
		stream->raw_length = data.size();
		source.inline_stream = stream;
		RECTO_PDF_RETURN_IF_ERROR(read_image_dictionary(source, stream->dict));
		RECTO_PDF_RETURN_IF_ERROR(prepare_stencil(source, *stream));
		// The chain's samples, already decoded to frame it. An image filter's stream form is not the image path's.
		if (framing_decode.has_value() && source.usable && !source.stencil && !source.image_filter) {
			const size_t bytes = framing_decode->size();
			if (hold_image_bytes(bytes)) {
				image_decoded_ = std::min(image_budget_, image_decoded_ + bytes);
				source.samples = std::make_shared<const std::vector<uint8_t>>(std::move(*framing_decode));
				source.decoded = true;
			}
		}
		return source.usable;
	}

	// Hidden content draws nothing and costs nothing past its framing, and what the builder refuses is skipped as a
	// malformed operator is.
	// `framing_decode` is what framing just decoded, a parameter so that it lives only for this draw and cannot be
	// kept uncharged whichever way preparing the image ends.
	[[nodiscard]] PdfResult<void> draw_inline_image(InlineImage& image, std::optional<std::vector<uint8_t>> framing_decode)
	{
		if (hidden()) {
			return {};
		}
		if (!image.prepared) {
			image.prepared = true;
			RECTO_PDF_TRY(const bool usable, inline_source(image, std::move(framing_decode)));
			image.source.usable = usable;
		}
		if (!image.source.usable) {
			return {};
		}
		auto drawn = draw_image_source(image.source);
		if (!drawn.has_value() && drawn.error().code == ErrorCode::DamagedFormat) {
			return {};
		}
		return drawn;
	}

	[[nodiscard]] static std::optional<double> number(const PdfValue& v) noexcept
	{
		const auto n = v.as_number();
		if (!n.has_value() || !std::isfinite(*n)) {
			return std::nullopt;
		}
		return n;
	}

	// The operands as `count` finite numbers (at most a colour's 32 components), or none when they are not exactly that.
	[[nodiscard]] static std::optional<ColorComponents> numbers(const std::vector<PdfValue>& operands, size_t count) noexcept
	{
		ColorComponents out{};
		if (operands.size() != count || count > out.size()) {
			return std::nullopt;
		}
		for (size_t i = 0; i < count; ++i) {
			const auto n = number(operands[i]);
			if (!n.has_value()) {
				return std::nullopt;
			}
			out[i] = *n;
		}
		return out;
	}

	[[nodiscard]] PdfResult<void> dispatch(Op op, const std::vector<PdfValue>& operands) noexcept
	{
		if (op == Op::EndMarked) {
			skip_next_ = false;
			end_marked(); // whatever its operands: marked-content nesting stays balanced through damage
			return {};
		}
		if (skip_next_) {
			skip_next_ = false;
			if (op == Op::BeginMarked || op == Op::BeginMarkedProperties) {
				++marked_depth_; // a skipped begin still has its EMC to come
			}
			return {};
		}
		try {
			auto done = dispatch_operator(op, operands);
			if (resolve_failure_.has_value()) {
				return *resolve_failure_;
			}
			if (!done.has_value() && done.error().code == ErrorCode::DamagedFormat) {
				// The builder refused this operator's geometry (coordinates or a matrix that overflow): the operator is
				// skipped like any malformed one. A refusal the builder keeps (a bad page box) comes back from finish().
				return {};
			}
			return done;
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory interpreting a content stream");
		}
	}

	[[nodiscard]] static constexpr bool takes_no_operands(Op op) noexcept
	{
		switch (op) {
		case Op::Save:
		case Op::Restore:
		case Op::ClosePath:
		case Op::Stroke:
		case Op::CloseStroke:
		case Op::Fill:
		case Op::FillEvenOdd:
		case Op::FillStroke:
		case Op::FillStrokeEvenOdd:
		case Op::CloseFillStroke:
		case Op::CloseFillStrokeEvenOdd:
		case Op::EndPath:
		case Op::Clip:
		case Op::ClipEvenOdd:
		case Op::BeginText:
		case Op::EndText:
		case Op::NextLine:
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] PdfResult<void> dispatch_operator(Op op, const std::vector<PdfValue>& operands)
	{
		if (takes_no_operands(op) && !operands.empty()) {
			return {}; // given operands it does not take: skipped, as every operator with the wrong operands is
		}
		switch (op) {
		// Marked content (14.6), for optional content (8.11.3.2).
		case Op::BeginMarked: {
			++marked_depth_;
			return {};
		}
		case Op::BeginMarkedProperties: {
			return begin_marked_properties(operands);
		}
		// Graphics state.
		case Op::Save: {
			return save();
		}
		case Op::DrawXObject: {
			if (operands.size() != 1u || !operands[0].as_name().has_value()) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "Do takes one XObject name"); // recorded
				return {};
			}
			return draw_xobject(*operands[0].as_name());
		}
		case Op::Restore: {
			return restore();
		}
		case Op::Concat: {
			const auto m = numbers(operands, 6u);
			if (!m.has_value()) {
				return {};
			}
			return builder_.concat_matrix(PdfMatrix{(*m)[0], (*m)[1], (*m)[2], (*m)[3], (*m)[4], (*m)[5]});
		}
		case Op::LineWidth: {
			if (const auto n = numbers(operands, 1u)) {
				set_line_width((*n)[0]);
			}
			return {};
		}
		case Op::LineCap: {
			if (const auto n = numbers(operands, 1u)) {
				set_line_cap((*n)[0]);
			}
			return {};
		}
		case Op::LineJoin: {
			if (const auto n = numbers(operands, 1u)) {
				set_line_join((*n)[0]);
			}
			return {};
		}
		case Op::MiterLimit: {
			if (const auto n = numbers(operands, 1u)) {
				set_miter_limit((*n)[0]);
			}
			return {};
		}
		case Op::Dash: {
			if (operands.size() == 2u) {
				set_dash(operands[0], operands[1]);
			}
			return {};
		}
		case Op::Intent: {
			if (operands.size() == 1u) {
				if (const auto name = operands[0].as_name()) {
					state_.rendering_intent = rendering_intent_named(*name);
				}
			}
			return {};
		}
		case Op::Flatness: {
			if (const auto n = numbers(operands, 1u); n.has_value() && (*n)[0] >= 0.0 && (*n)[0] <= 100.0) {
				state_.flatness = (*n)[0];
			}
			return {};
		}
		case Op::ExtGState: {
			if (operands.size() == 1u) {
				if (const auto name = operands[0].as_name()) {
					apply_ext_gstate(*name);
				}
			}
			return {};
		}
		// Path construction.
		case Op::MoveTo: {
			if (const auto n = numbers(operands, 2u)) {
				path_.move_to((*n)[0], (*n)[1]);
				current_ = PdfPoint{(*n)[0], (*n)[1]};
				subpath_start_ = current_;
				closed_ = false;
			}
			return {};
		}
		case Op::LineTo: {
			if (const auto n = numbers(operands, 2u); n.has_value() && current_.has_value()) {
				begin_segment();
				path_.line_to((*n)[0], (*n)[1]);
				current_ = PdfPoint{(*n)[0], (*n)[1]};
			}
			return {};
		}
		case Op::CurveTo: {
			if (const auto n = numbers(operands, 6u); n.has_value() && current_.has_value()) {
				begin_segment();
				path_.cubic_bezier_to(PdfPoint{(*n)[0], (*n)[1]}, PdfPoint{(*n)[2], (*n)[3]}, PdfPoint{(*n)[4], (*n)[5]});
				current_ = PdfPoint{(*n)[4], (*n)[5]};
			}
			return {};
		}
		case Op::CurveV: {
			// The first control point is the current point.
			if (const auto n = numbers(operands, 4u); n.has_value() && current_.has_value()) {
				begin_segment();
				path_.cubic_bezier_to(*current_, PdfPoint{(*n)[0], (*n)[1]}, PdfPoint{(*n)[2], (*n)[3]});
				current_ = PdfPoint{(*n)[2], (*n)[3]};
			}
			return {};
		}
		case Op::CurveY: {
			// The second control point is the end point.
			if (const auto n = numbers(operands, 4u); n.has_value() && current_.has_value()) {
				begin_segment();
				const PdfPoint end{(*n)[2], (*n)[3]};
				path_.cubic_bezier_to(PdfPoint{(*n)[0], (*n)[1]}, end, end);
				current_ = end;
			}
			return {};
		}
		case Op::ClosePath: {
			close_subpath();
			return {};
		}
		case Op::Rect: {
			// A closed subpath whose current point is (x, y) afterwards (ISO 32000-1 8.5.2.1).
			if (const auto n = numbers(operands, 4u)) {
				path_.rect((*n)[0], (*n)[1], (*n)[2], (*n)[3]);
				current_ = PdfPoint{(*n)[0], (*n)[1]};
				subpath_start_ = current_;
				closed_ = true;
			}
			return {};
		}
		// Painting and clipping.
		case Op::Stroke: {
			return paint(false, true, FillRule::NonZero, false);
		}
		case Op::CloseStroke: {
			return paint(false, true, FillRule::NonZero, true);
		}
		case Op::Fill: {
			return paint(true, false, FillRule::NonZero, false);
		}
		case Op::FillEvenOdd: {
			return paint(true, false, FillRule::EvenOdd, false);
		}
		case Op::FillStroke: {
			return paint(true, true, FillRule::NonZero, false);
		}
		case Op::FillStrokeEvenOdd: {
			return paint(true, true, FillRule::EvenOdd, false);
		}
		case Op::CloseFillStroke: {
			return paint(true, true, FillRule::NonZero, true);
		}
		case Op::CloseFillStrokeEvenOdd: {
			return paint(true, true, FillRule::EvenOdd, true);
		}
		case Op::EndPath: {
			return paint(false, false, FillRule::NonZero, false);
		}
		case Op::Clip: {
			pending_clip_ = FillRule::NonZero;
			return {};
		}
		case Op::ClipEvenOdd: {
			pending_clip_ = FillRule::EvenOdd;
			return {};
		}
		// Colour.
		case Op::FillGray:
			return set_device_color(false, ColorSpaceKind::DeviceGray, operands);
		case Op::StrokeGray:
			return set_device_color(true, ColorSpaceKind::DeviceGray, operands);
		case Op::FillRgb:
			return set_device_color(false, ColorSpaceKind::DeviceRGB, operands);
		case Op::StrokeRgb:
			return set_device_color(true, ColorSpaceKind::DeviceRGB, operands);
		case Op::FillCmyk:
			return set_device_color(false, ColorSpaceKind::DeviceCMYK, operands);
		case Op::StrokeCmyk:
			return set_device_color(true, ColorSpaceKind::DeviceCMYK, operands);
		case Op::FillSpace:
		case Op::StrokeSpace: {
			if (operands.size() == 1u) {
				if (const auto name = operands[0].as_name()) {
					Color& target = op == Op::StrokeSpace ? state_.stroke : state_.fill;
					RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, spaces().named(*name));
					target.reset(std::move(space));
				}
			}
			return {};
		}
		case Op::FillColor:
		case Op::StrokeColor: {
			const bool stroking = op == Op::StrokeColor;
			Color& target = stroking ? state_.stroke : state_.fill;
			if (target.space != nullptr) {
				set_components(target, operands);
			}
			return {};
		}
		// Text (ISO 32000-1 9.3 and 9.4).
		case Op::BeginText:
			// A nested BT, as readers read it, starts a new text object: both matrices reset.
			text_matrix_ = line_matrix_ = PdfMatrix::identity();
			return {};
		case Op::EndText:
			// Text shown outside a BT/ET pair is still positioned from the current matrices, as readers do.
			return {};
		case Op::CharSpacing:
		case Op::WordSpacing:
		case Op::HorizontalScale:
		case Op::Leading:
		case Op::Rise: {
			const auto n = numbers(operands, 1u);
			if (!n.has_value()) {
				return {};
			}
			const double v = (*n)[0];
			if (op == Op::CharSpacing) {
				state_.char_spacing = v;
			} else if (op == Op::WordSpacing) {
				state_.word_spacing = v;
			} else if (op == Op::HorizontalScale) {
				state_.horizontal_scaling = v / 100.0;
			} else if (op == Op::Leading) {
				state_.leading = v;
			} else {
				state_.rise = v;
			}
			return {};
		}
		case Op::TextMode: {
			const auto n = numbers(operands, 1u);
			if (n.has_value() && (*n)[0] == std::floor((*n)[0]) && (*n)[0] >= 0.0 && (*n)[0] <= 7.0) {
				state_.text_mode = static_cast<TextRenderMode>(static_cast<int>((*n)[0]));
			}
			return {};
		}
		case Op::Font: {
			if (operands.size() != 2u) {
				return {};
			}
			const auto name = operands[0].as_name();
			const auto size = number(operands[1]);
			if (!name.has_value() || !size.has_value()) {
				return {};
			}
			RECTO_PDF_TRY(std::shared_ptr<const TextFont> font, named_font(*name));
			state_.font = std::move(font);
			state_.font_size = *size;
			return {};
		}
		case Op::TextMove:
		case Op::TextMoveLeading: {
			const auto n = numbers(operands, 2u);
			if (!n.has_value()) {
				return {};
			}
			if (op == Op::TextMoveLeading) {
				state_.leading = -(*n)[1];
			}
			move_line((*n)[0], (*n)[1]);
			return {};
		}
		case Op::TextMatrix: {
			const auto m = numbers(operands, 6u);
			if (m.has_value()) {
				text_matrix_ = line_matrix_ = PdfMatrix{(*m)[0], (*m)[1], (*m)[2], (*m)[3], (*m)[4], (*m)[5]};
			}
			return {};
		}
		case Op::NextLine:
			move_line(0.0, -state_.leading);
			return {};
		case Op::ShowText:
		case Op::NextLineShow: {
			if (operands.size() != 1u || operands[0].as_string() == nullptr) {
				return {};
			}
			if (op == Op::NextLineShow) {
				move_line(0.0, -state_.leading);
			}
			return show(operands[0].as_string()->span());
		}
		case Op::NextLineShowSpaced: {
			if (operands.size() != 3u || operands[2].as_string() == nullptr) {
				return {};
			}
			const auto aw = number(operands[0]);
			const auto ac = number(operands[1]);
			if (!aw.has_value() || !ac.has_value()) {
				return {};
			}
			state_.word_spacing = *aw;
			state_.char_spacing = *ac;
			move_line(0.0, -state_.leading);
			return show(operands[2].as_string()->span());
		}
		case Op::ShowTextArray: {
			const PdfArray* array = operands.size() == 1u ? operands[0].as_array() : nullptr;
			if (array == nullptr) {
				return {};
			}
			for (const PdfValue& element : *array) {
				if (const PdfString* text = element.as_string()) {
					RECTO_PDF_RETURN_IF_ERROR(show(text->span()));
				} else if (const auto n = number(element)) {
					// A number moves the text matrix by thousandths of the font size, with no character or word
					// spacing, wherever it stands in the array (9.4.3).
					const double shift = -(*n) / 1000.0 * state_.font_size;
					const bool vertical = state_.font != nullptr && state_.font->vertical();
					advance_text(vertical ? 0.0 : shift * state_.horizontal_scaling, vertical ? shift : 0.0);
				}
			}
			return {};
		}
		case Op::Other:
		case Op::BeginInlineImage:
			break;
		}
		// Anything else (text, images, XObjects, marked content and unknown operators) is another section's or
		// skipped with its operands (ISO 32000-1 7.8.2).
		return {};
	}

	[[nodiscard]] PdfResult<void> save()
	{
		if (state_stack_.size() >= max_depth_) {
			++ignored_saves_; // ignored, and so is the Q that matches it
			return {};
		}
		RECTO_PDF_RETURN_IF_ERROR(builder_.save_state());
		state_stack_.push_back(state_);
		return {};
	}

	[[nodiscard]] PdfResult<void> restore()
	{
		if (ignored_saves_ > 0u) {
			--ignored_saves_;
			return {};
		}
		if (state_stack_.size() <= save_floor_) {
			return {}; // an unmatched Q in a sloppy file is ignored, and a form's cannot pop its caller's saves
		}
		RECTO_PDF_RETURN_IF_ERROR(builder_.restore_state());
		state_ = std::move(state_stack_.back());
		state_stack_.pop_back();
		return {};
	}

	void set_line_width(double width) noexcept
	{
		if (const auto f = stroke_float(width); f.has_value() && *f >= 0.0f) {
			state_.line_width = *f;
			state_.style_recorded = false;
		}
	}

	void set_miter_limit(double limit) noexcept
	{
		if (const auto f = stroke_float(limit); f.has_value() && *f >= 1.0f) {
			state_.miter_limit = *f;
			state_.style_recorded = false;
		}
	}

	void set_line_cap(double value) noexcept
	{
		if (value == 0.0 || value == 1.0 || value == 2.0) {
			state_.line_cap = static_cast<LineCap>(static_cast<int>(value));
			state_.style_recorded = false;
		}
	}

	void set_line_join(double value) noexcept
	{
		if (value == 0.0 || value == 1.0 || value == 2.0) {
			state_.line_join = static_cast<LineJoin>(static_cast<int>(value));
			state_.style_recorded = false;
		}
	}

	// `[array] phase`: non-negative entries that fit a float; anything else leaves the dash as it was.
	void set_dash(const PdfValue& array_value, const PdfValue& phase_value)
	{
		const PdfArray* array = array_value.as_array();
		const auto phase_number = number(phase_value);
		const auto phase = phase_number.has_value() ? stroke_float(*phase_number) : std::nullopt;
		if (array == nullptr || !phase.has_value()) {
			return;
		}
		std::vector<float> dash;
		dash.reserve(array->size());
		for (const PdfValue& entry : *array) {
			const auto n = number(entry);
			const auto f = n.has_value() ? stroke_float(*n) : std::nullopt;
			if (!f.has_value() || *f < 0.0f) {
				return;
			}
			dash.push_back(*f);
		}
		state_.dash = std::move(dash);
		state_.dash_phase = *phase;
		state_.style_recorded = false;
	}

	// A resource category (`/ExtGState`, `/ColorSpace`), resolved once per interpretation: a stream that names it on
	// every operator costs one lookup each, not a copy of the whole category.
	// A category dictionary of a scope's resources, shared, never copied: `owner` holds its object when it is
	// indirect, and a direct one lives in the scope's `/Resources`, which the scope keeps alive.
	struct Category {
		bool resolved{false};
		std::shared_ptr<const PdfValue> owner{};
		const PdfDict* dict{nullptr};
	};

	// A value read from the resources without copying it: `value` points into `owner` (an indirect object, shared
	// with the document's cache) or into a dictionary the interpreter already keeps alive.
	struct Held {
		std::shared_ptr<const PdfValue> owner{};
		const PdfValue* value{nullptr};

		explicit operator bool() const noexcept
		{
			return value != nullptr;
		}
		const PdfValue& operator*() const noexcept
		{
			return *value;
		}
		const PdfValue* operator->() const noexcept
		{
			return value;
		}
	};

	// The resources a content stream draws with (ISO 32000-1 7.8.3): the page's at the bottom of `scopes_`, a Form
	// XObject's while it runs; each keeps its own category caches and font-by-name cache, since one name means
	// different things in different scopes. Kept for the page by the `/Resources` object, at most kMaxResourceScopes.
	struct ResourceScope {
		// Never repeated on a page, where an address can: a scope past kMaxResourceScopes ends with its form.
		uint64_t serial{0};
		const PdfDict* resources{nullptr};
		std::shared_ptr<const PdfValue> owner{}; // what `resources` lives in: a form's `/Resources` object, or the form
		Category ext_gstates{};
		Category color_spaces{};
		Category font_resources{};
		Category xobjects{};
		Category properties{};
		std::unordered_map<std::string, std::shared_ptr<const TextFont>, StringHash, std::equal_to<>> fonts{};
	};

	// What a form's caller gets back when the form's content ends.
	struct FormExit {
		size_t floor{0};
		size_t caller_floor{0};
		size_t caller_ignored{0};
		size_t marked_depth{0};
		bool caller_skip{false};
		PdfMatrix text{};
		PdfMatrix line{};
		PathBuilder path{};
		std::optional<PdfPoint> current{};
		std::optional<PdfPoint> start{};
		std::optional<FillRule> clip{};
		bool closed{false};
		ResourceScope* scope{nullptr};
		std::unique_ptr<ResourceScope> transient{};
	};
	// What a page knows of an XObject it has met, worked out once from its object (ISO 32000-1 8.8 and Table 95) so a
	// repeated `Do` resolves nothing: its kind and, for a form, its object, matrix and box. A refusal is remembered.
	struct XObjectEntry {
		enum class Kind : uint8_t { Refused, Form, Image, PostScript };
		Kind kind{Kind::Refused};
		std::shared_ptr<const PdfValue> object{};
		PdfMatrix matrix{};
		PdfRect bbox{};
		bool hidden{false}; // its `/OC` entry is off (8.11.3.3)
	};

	// One content stream being interpreted: the page's, or a form's (its content kept alive here).
	struct Frame {
		std::shared_ptr<const std::vector<uint8_t>> content;
		std::unique_ptr<Parser> parser;
		std::vector<PdfValue> operands;
		std::unique_ptr<FormExit> form;
	};

	[[nodiscard]] const PdfDict* category(std::string_view name, Category& slot)
	{
		if (!slot.resolved) {
			slot.resolved = true;
			if (const PdfValue* value = scope_->resources->get(name)) {
				Held held = shared(*value);
				if (held && held->as_dict() != nullptr) {
					slot.dict = held->as_dict();
					slot.owner = std::move(held.owner);
				}
			}
		}
		return slot.dict;
	}

	// `value`, an indirect reference resolved without a copy. A damaged reference or a null reads as missing; any
	// other failure is kept for `dispatch` as `resolved` keeps it.
	[[nodiscard]] Held shared(const PdfValue& value)
	{
		const std::optional<IndirectRef> ref = value.as_ref();
		if (!ref.has_value()) {
			return Held{nullptr, &value};
		}
		std::shared_ptr<const PdfValue> object = held_object(*ref);
		const PdfValue* target = object.get();
		return Held{std::move(object), target};
	}

	// A resource object for this page: resolved once and held here by object, so every scope naming it shares one
	// instance whether or not the document's cache admits it (it keeps nothing over a quarter of its budget). What
	// the page holds is bounded by kMaxHeldResourceBytes; an object past it is refused once, remembered, and never
	// parsed again. Null when missing, null, damaged or refused.
	[[nodiscard]] std::shared_ptr<const PdfValue> held_object(IndirectRef ref)
	{
		const uint64_t key = object_key(ref);
		if (const auto it = held_.find(key); it != held_.end()) {
			return it->second;
		}
		auto object = document_.resolve_object_shared(ref);
		std::shared_ptr<const PdfValue> kept;
		if (!object.has_value()) {
			if (fatal(object.error())) {
				if (!resolve_failure_.has_value()) {
					resolve_failure_ = object.error();
				}
				return nullptr; // the page stops, so nothing is remembered
			}
			// A damaged object reads the same way every time, so it is remembered and never parsed again.
		} else {
			kept = std::move(*object);
		}
		return held_admitted(key, std::move(kept));
	}

	// `kept`, an object resolved for `key`, taken into what the page holds within its budget, and remembered either way.
	[[nodiscard]] std::shared_ptr<const PdfValue> held_admitted(uint64_t key, std::shared_ptr<const PdfValue> kept)
	{
		if (const auto it = held_.find(key); it != held_.end()) {
			return it->second;
		}
		if (kept != nullptr && kept->is_null()) {
			kept = nullptr;
		}
		if (kept != nullptr) {
			const size_t bytes = kept->heap_bytes();
			if (bytes > kMaxHeldResourceBytes - held_bytes_) {
				record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, held_bytes_ + bytes,
					kMaxHeldResourceBytes, "A resource past the page's held-resource budget is not used"));
				kept = nullptr;
			} else {
				held_bytes_ += bytes;
			}
		}
		if (held_.size() < kMaxHeldResources) {
			held_.emplace(key, kept);
		}
		return kept;
	}

	// The resource named `name` in `slot`'s category, resolved, or none.
	[[nodiscard]] Held resource(std::string_view category_name, Category& slot, std::string_view name)
	{
		const PdfDict* dict = category(category_name, slot);
		if (dict == nullptr) {
			return Held{};
		}
		const PdfValue* entry = dict->get(name);
		if (entry == nullptr) {
			return Held{};
		}
		Held held = shared(*entry);
		if (held && held.owner == nullptr) {
			held.owner = slot.owner; // a direct entry lives in the category's object
		}
		return held;
	}

	// `value` with an indirect reference resolved. A damaged reference reads as missing; any other failure (memory, a
	// decoder limit on the object stream holding it) is kept, and `dispatch` returns it after the operator.
	[[nodiscard]] std::optional<PdfValue> resolved(const PdfValue& value)
	{
		auto r = document_.resolve_value(value);
		if (!r.has_value()) {
			if (fatal(r.error()) && !resolve_failure_.has_value()) {
				resolve_failure_ = r.error();
			}
			return std::nullopt;
		}
		return std::move(*r);
	}

	// A named value inside an `/ExtGState` dictionary, resolved.
	[[nodiscard]] std::optional<PdfValue> entry(const PdfDict& dict, std::string_view key)
	{
		const PdfValue* v = dict.get(key);
		if (v == nullptr) {
			return std::nullopt;
		}
		return resolved(*v);
	}

	// `gs /Name`: the line, dash, alpha and blend entries of the named `/ExtGState` (ISO 32000-1 Table 58). `/SMask` is
	// section 15's; a dictionary missing from the resources is skipped.
	void apply_ext_gstate(std::string_view name)
	{
		const Held value = resource("ExtGState", scope_->ext_gstates, name);
		if (!value || value->as_dict() == nullptr) {
			return;
		}
		const PdfDict& gs = *value->as_dict();
		// `/Font [font size]` sets the font and size as `Tf` does (Table 58).
		if (const auto font = entry(gs, "Font"); font.has_value() && font->as_array() != nullptr &&
			font->as_array()->size() == 2u) {
			const PdfArray& pair = *font->as_array();
			const std::optional<PdfValue> size_value = resolved(pair[1]); // the size may be indirect too
			if (const auto size = size_value.has_value() ? number(*size_value) : std::nullopt) {
				auto resolved_font = font_value(pair[0]);
				if (!resolved_font.has_value()) {
					if (!resolve_failure_.has_value()) {
						resolve_failure_ = resolved_font.error(); // a failure that stops the page, after this operator
					}
				} else {
					state_.font = std::move(*resolved_font);
					state_.font_size = *size;
				}
			}
		}
		if (const auto ri = entry(gs, "RI"); ri.has_value()) {
			if (const auto n = ri->as_name()) {
				state_.rendering_intent = rendering_intent_named(*n);
			}
		}
		if (const auto lw = entry(gs, "LW"); lw.has_value()) {
			if (const auto n = number(*lw)) {
				set_line_width(*n);
			}
		}
		if (const auto lc = entry(gs, "LC"); lc.has_value()) {
			if (const auto n = number(*lc)) {
				set_line_cap(*n);
			}
		}
		if (const auto lj = entry(gs, "LJ"); lj.has_value()) {
			if (const auto n = number(*lj)) {
				set_line_join(*n);
			}
		}
		if (const auto ml = entry(gs, "ML"); ml.has_value()) {
			if (const auto n = number(*ml)) {
				set_miter_limit(*n);
			}
		}
		if (const auto d = entry(gs, "D"); d.has_value()) {
			// `[dash_array phase]`, any of whose elements may be indirect (ISO 32000-1 7.3.10).
			if (const PdfArray* pair = d->as_array(); pair != nullptr && pair->size() == 2u) {
				const auto dashes = resolved((*pair)[0]);
				const auto phase = resolved((*pair)[1]);
				if (dashes.has_value() && phase.has_value() && dashes->as_array() != nullptr) {
					PdfArray direct;
					direct.reserve(dashes->as_array()->size());
					for (const PdfValue& element : *dashes->as_array()) {
						direct.push_back(resolved(element).value_or(PdfValue{}));
					}
					set_dash(PdfValue{std::move(direct)}, *phase);
				}
			}
		}
		if (const auto ca = entry(gs, "CA"); ca.has_value()) {
			if (const auto n = number(*ca)) {
				state_.stroke_alpha = unit(*n);
				state_.alpha_recorded = false;
			}
		}
		if (const auto ca = entry(gs, "ca"); ca.has_value()) {
			if (const auto n = number(*ca)) {
				state_.fill_alpha = unit(*n);
				state_.alpha_recorded = false;
			}
		}
		if (const auto bm = entry(gs, "BM"); bm.has_value()) {
			// A name, or an array whose first name this reader knows (ISO 32000-1 11.3.5); unknown means Normal.
			std::optional<BlendMode> mode;
			if (const auto single = bm->as_name()) {
				mode = blend_mode_named(*single);
			} else if (const PdfArray* list = bm->as_array()) {
				for (const PdfValue& item : *list) {
					const auto element = resolved(item);
					if (const auto n = element.has_value() ? element->as_name() : std::nullopt;
						n.has_value() && (mode = blend_mode_named(*n)).has_value()) {
						break;
					}
				}
			}
			state_.blend_mode = mode.value_or(BlendMode::Normal);
			state_.alpha_recorded = false;
		}
	}

	// The colour space resolution of this interpretation: the `/ColorSpace` resources, `/Default*` spaces among them.
	[[nodiscard]] ColorSpaces& spaces()
	{
		if (!spaces_.has_value()) {
			spaces_.emplace(document_, category("ColorSpace", scope_->color_spaces));
		}
		return *spaces_;
	}

	// `g`, `rg`, `k` and their stroking forms: the device space, or the `/Default*` space replacing it (8.6.5.6).
	[[nodiscard]] PdfResult<void> set_device_color(
		bool stroking, ColorSpaceKind kind, const std::vector<PdfValue>& operands)
	{
		const size_t count = kind == ColorSpaceKind::DeviceGray ? 1u : kind == ColorSpaceKind::DeviceRGB ? 3u : 4u;
		if (!numbers(operands, count).has_value()) {
			return {};
		}
		RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, spaces().device(kind));
		Color& target = stroking ? state_.stroke : state_.fill;
		target.reset(std::move(space));
		set_components(target, operands);
		return {};
	}

	// `sc`/`scn` and `SC`/`SCN`: as many numbers as the space has components, else the operator is skipped.
	void set_components(Color& target, const std::vector<PdfValue>& operands) noexcept
	{
		const size_t count = target.space->components();
		const auto n = numbers(operands, count);
		if (!n.has_value()) {
			return;
		}
		for (size_t i = 0; i < count && i < target.components.size(); ++i) {
			target.components[i] = (*n)[i];
		}
	}

	// A colour in sRGB with the rendering intent in force; black for a space that cannot be used. Each side remembers its
	// last conversion, every component compared, so a page of paints in one colour converts it once.
	struct Converted {
		std::shared_ptr<const ColorSpace> space;
		ColorComponents components{};
		RenderingIntent intent{RenderingIntent::RelativeColorimetric};
		ColorRgba value{};
	};

	[[nodiscard]] PdfResult<ColorRgba> rgba(const Color& color, bool stroking) noexcept
	{
		if (color.space == nullptr) {
			return ColorRgba{0.0f, 0.0f, 0.0f, 1.0f};
		}
		std::optional<Converted>& last = stroking ? last_stroke_ : last_fill_;
		if (last.has_value() && last->space == color.space && last->components == color.components &&
			last->intent == state_.rendering_intent) {
			return last->value;
		}
		RECTO_PDF_TRY(const ColorRgba value, color.space->rgba(color.components, state_.rendering_intent, document_));
		last = Converted{color.space, color.components, state_.rendering_intent, value};
		return value;
	}

	// Records the stroke style and the alphas when they changed since the builder last had them.
	[[nodiscard]] PdfResult<void> sync_state(bool stroking)
	{
		if (stroking && !state_.style_recorded) {
			RECTO_PDF_RETURN_IF_ERROR(builder_.set_stroke_style(state_.line_width, state_.line_cap, state_.line_join,
				state_.miter_limit, state_.dash, state_.dash_phase));
			state_.style_recorded = true;
		}
		if (!state_.alpha_recorded) {
			RECTO_PDF_RETURN_IF_ERROR(builder_.set_alpha_and_blend(state_.stroke_alpha, state_.fill_alpha, state_.blend_mode));
			state_.alpha_recorded = true;
		}
		return {};
	}

	// Paints the current path, then intersects the pending clip (ISO 32000-1 8.5.4: the paint uses the clip in force
	// before it), then ends the path. `n` paints nothing.
	// A segment after a closed subpath (`h`, a closing paint's close or `re`) starts a new subpath at the closed one's
	// start, so the recorded path says where it starts.
	void begin_segment()
	{
		if (closed_) {
			path_.move_to(*current_);
			closed_ = false;
		}
	}

	// `h` and the closing painting operators; a subpath already closed (by `h` or `re`) is not closed again (ISO 32000-1
	// Table 59).
	void close_subpath()
	{
		if (current_.has_value() && !closed_) {
			path_.close_subpath();
			current_ = subpath_start_;
			closed_ = true;
		}
	}

	[[nodiscard]] PdfResult<void> paint(bool fill, bool stroke, FillRule rule, bool close)
	{
		if (close) {
			close_subpath();
		}
		const std::optional<FillRule> clip = pending_clip_;
		pending_clip_.reset();
		// A side whose colour marks nothing (a `/None` colorant, ISO 32000-1 8.6.6.4) is not painted, and hidden
		// optional content paints nothing while its clip still applies (8.11.3.2).
		fill = fill && state_.fill.paints() && !hidden();
		stroke = stroke && state_.stroke.paints() && !hidden();
		// A path that is neither painted nor clipped (`n` with no `W`) records nothing.
		const bool records = !path_.empty() && (fill || stroke || clip.has_value());
		auto done = records ? record_paint(fill, stroke, rule, clip) : PdfResult<void>{};
		end_path(); // the path ends with its painting operator, recorded or refused
		return done;
	}

	[[nodiscard]] PdfResult<void> record_paint(bool fill, bool stroke, FillRule rule, std::optional<FillRule> clip)
	{
		RECTO_PDF_TRY(const uint32_t id, builder_.add_path(path_));
		if (fill || stroke) {
			RECTO_PDF_RETURN_IF_ERROR(sync_state(stroke));
			// Converted now, with the rendering intent in force at the paint (ISO 32000-1 8.6.5.8).
			RECTO_PDF_TRY(const ColorRgba stroke_color, rgba(state_.stroke, true));
			RECTO_PDF_TRY(const ColorRgba fill_color, rgba(state_.fill, false));
			RECTO_PDF_RETURN_IF_ERROR(builder_.draw_path(id, stroke_color, fill_color, stroke, fill, rule));
		}
		if (clip.has_value()) {
			RECTO_PDF_RETURN_IF_ERROR(builder_.push_clip_path(id, *clip));
		}
		return {};
	}

	void end_path() noexcept
	{
		path_.clear();
		current_.reset();
		subpath_start_.reset();
		closed_ = false;
	}

	// The font a `/Font` resource names, resolved once per interpretation. A missing resource positions its text with
	// the fallback, its diagnostic recorded.
	[[nodiscard]] PdfResult<std::shared_ptr<const TextFont>> named_font(std::string_view name)
	{
		if (const auto it = scope_->fonts.find(name); it != scope_->fonts.end()) {
			return it->second;
		}
		const PdfDict* fonts = category("Font", scope_->font_resources);
		const PdfValue* entry_value = fonts != nullptr ? fonts->get(name) : nullptr;
		std::shared_ptr<const TextFont> font;
		if (entry_value == nullptr) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "Tf names a font the resources do not hold"); // recorded
			RECTO_PDF_TRY(font, fallback());
		} else {
			RECTO_PDF_TRY(font, font_value(*entry_value));
		}
		if (scope_->fonts.size() < kMaxCachedFonts) {
			scope_->fonts.emplace(std::string(name), font);
		}
		return font;
	}

	// The font a font value (a reference or a dictionary) gives, an indirect one resolved once per interpretation.
	[[nodiscard]] PdfResult<std::shared_ptr<const TextFont>> font_value(const PdfValue& value)
	{
		const std::optional<IndirectRef> ref = value.as_ref();
		if (ref.has_value()) {
			if (const auto it = font_objects_.find(object_key(*ref)); it != font_objects_.end()) {
				return it->second;
			}
		}
		// Each page resolves at most kMaxCachedFonts fonts within kMaxTextFontBytes: past either, a font takes the
		// fallback unresolved, so a page of many distinct fonts never rebuilds or holds them without bound.
		const size_t font_heap = font_bytes_ + type0_memo_.memory_bytes(); // the fonts' own, and what they share once
		if (fonts_resolved_ >= kMaxCachedFonts || font_heap >= kMaxTextFontBytes) {
			record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, font_heap,
				kMaxTextFontBytes, "Fonts past the page's limit; drawn with the fallback"));
			return fallback();
		}
		++fonts_resolved_;
		RECTO_PDF_TRY(std::shared_ptr<const TextFont> font, TextFont::resolve(document_, value, &type0_memo_));
		font_bytes_ += font->memory_bytes();
		if (ref.has_value()) {
			font_objects_.emplace(object_key(*ref), font);
		}
		return font;
	}
	// The fallback font, built once per page.
	[[nodiscard]] PdfResult<std::shared_ptr<const TextFont>> fallback()
	{
		if (fallback_ == nullptr) {
			RECTO_PDF_TRY(fallback_, TextFont::fallback());
		}
		return fallback_;
	}

	// `Td`: the line matrix moved by (tx, ty) and the text matrix set to it (9.4.2).
	void move_line(double tx, double ty) noexcept
	{
		line_matrix_ = concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, tx, ty}, line_matrix_);
		text_matrix_ = line_matrix_;
	}

	// The text matrix moved by (tx, ty) in text space (9.4.4).
	void advance_text(double tx, double ty) noexcept
	{
		text_matrix_ = concat_matrix(PdfMatrix{1.0, 0.0, 0.0, 1.0, tx, ty}, text_matrix_);
	}

	// One shown string (9.4.3, 9.4.4): each code's glyph origin and displacement, recorded as one glyph run whose
	// matrix carries the horizontal scaling (`[Tz/100 0 0 1 0 0] x Tm`), then the text matrix advanced past it. Glyph
	// indices and outlines are section 43's; the run's bounds are the font's box at the font size over its glyphs.
	[[nodiscard]] PdfResult<void> show(std::span<const uint8_t> bytes)
	{
		if (bytes.empty()) {
			return {}; // nothing to draw, and the text matrix stays where it is
		}
		std::shared_ptr<const TextFont> font = state_.font;
		if (font == nullptr) {
			RECTO_PDF_TRY(font, fallback());
		}
		if (hidden() && state_.text_mode < TextRenderMode::FillAndClip) {
			return advance_hidden_text(*font, bytes);
		}
		const double size = state_.font_size;
		const bool vertical = font->vertical();
		std::vector<PositionedGlyph> glyphs;
		std::vector<uint8_t> faces; // each glyph's face in `font->faces()`, TextGlyph::kNoFace for none
		double x = 0.0;
		double y = 0.0;
		// A run that could not be recorded is refused before it is built, so a long string never allocates past the
		// list's glyph or byte limit; every code takes at least a byte, so the reservation is exact and never doubles.
		const size_t room = builder_.glyph_room();
		// Decoding is work whether or not the builder keeps the run: each byte may be checked against every codespace
		// range, charged before it is done so a refused run never escapes the page's work budget.
		RECTO_PDF_RETURN_IF_ERROR(builder_.charge_work(std::min(bytes.size(), room) * font->code_cost()));
		try {
			glyphs.reserve(std::min(bytes.size(), room));
			faces.reserve(std::min(bytes.size(), room));
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory positioning a text string");
		}
		for (size_t at = 0; at < bytes.size();) {
			if (glyphs.size() >= room) {
				return make_pdf_error(ErrorCode::ResourceLimit, "Text string exceeds the display list's glyph limit");
			}
			const TextCode code = font->next(bytes.subspan(at));
			at += code.length != 0u ? code.length : 1u; // a code no codespace matches skips one byte
			const double spacing = state_.char_spacing + (code.word_space ? state_.word_spacing : 0.0);
			RECTO_PDF_TRY(const TextGlyph drawn, glyph_of(*font, code));
			PositionedGlyph glyph;
			glyph.glyph_index = drawn.glyph;
			glyph.width_scale = drawn.width_scale;
			glyph.is_blank = drawn.blank;
			if (drawn.ink.has_value() && !drawn.blank) {
				// The glyph's own ink in text space points, relative to its origin (Tz is in the run's matrix); PdfRect
				// orders the corners, so a negative font size's mirrored box stays valid.
				glyph.ink_bounds_pt = PdfRect{drawn.ink->x_min * size, drawn.ink->y_min * size, drawn.ink->x_max * size,
					drawn.ink->y_max * size};
			}
			faces.push_back(drawn.face);
			if (!vertical) {
				glyph.origin_pt = PdfPoint{x, state_.rise};
				glyph.advance_pt = code.w0 * size + spacing;
				x += glyph.advance_pt;
			} else {
				// Vertical writing: the glyph sits at its position vector below the current point (9.7.4.3).
				glyph.origin_pt = PdfPoint{-code.vx * size, y - code.vy * size + state_.rise};
				glyph.advance_pt = code.w1 * size + spacing;
				y += glyph.advance_pt;
			}
			glyphs.push_back(glyph);
		}
		if (glyphs.empty()) {
			return {};
		}
		const PdfMatrix run_matrix =
			concat_matrix(PdfMatrix{state_.horizontal_scaling, 0.0, 0.0, 1.0, 0.0, 0.0}, text_matrix_);
		// Hidden text in a clipping mode paints nothing and still adds to the clip (8.11.3.2).
		const TextRenderMode mode =
			painting_mode(state_.text_mode, state_.fill.paints() && !hidden(), state_.stroke.paints() && !hidden());
		// A font whose box promises nothing takes one a thousand ems wide, which holds any glyph a file draws.
		const PdfRect box = font->bbox_known() ? font->bbox() : PdfRect{-1000.0, -1000.0, 1000.0, 1000.0};
		const double bx0 = std::min(box.x_min * size, box.x_max * size);
		const double bx1 = std::max(box.x_min * size, box.x_max * size);
		const double by0 = std::min(box.y_min * size, box.y_max * size);
		const double by1 = std::max(box.y_min * size, box.y_max * size);
		RECTO_PDF_RETURN_IF_ERROR(sync_state(mode_strokes(mode)));
		RECTO_PDF_TRY(const ColorRgba fill_color, rgba(state_.fill, false));
		RECTO_PDF_TRY(const ColorRgba stroke_color, rgba(state_.stroke, true));
		// One run per face within the string (a standard font can draw a code from a second face), each under the
		// string's run matrix with its glyphs' origins continuing; a glyph with no face (advance-only, or a Type 3
		// glyph) goes in a run whose font handle is null. Each run's conservative box (the font box over its glyph
		// origins) holds any glyph whose own ink is unknown.
		PdfResult<void> drawn;
		const std::span<const PositionedGlyph> all(glyphs);
		for (size_t first = 0; first < glyphs.size() && drawn.has_value();) {
			size_t last = first + 1u;
			while (last < glyphs.size() && faces[last] == faces[first]) {
				++last;
			}
			const std::span<const PositionedGlyph> run = all.subspan(first, last - first);
			PdfRect bounds{run.front().origin_pt.x + bx0, run.front().origin_pt.y + by0, run.front().origin_pt.x + bx1,
				run.front().origin_pt.y + by1};
			for (const PositionedGlyph& glyph : run) {
				bounds.x_min = std::min(bounds.x_min, glyph.origin_pt.x + bx0);
				bounds.y_min = std::min(bounds.y_min, glyph.origin_pt.y + by0);
				bounds.x_max = std::max(bounds.x_max, glyph.origin_pt.x + bx1);
				bounds.y_max = std::max(bounds.y_max, glyph.origin_pt.y + by1);
			}
			std::shared_ptr<const void> handle;
			if (faces[first] != TextGlyph::kNoFace && faces[first] < font->faces().size()) {
				handle = font->faces()[faces[first]];
			}
			// The font-wide box only for a glyph whose own ink is unknown: with every ink known the run's recorded
			// bounds are exactly its glyphs' ink.
			const bool unknown_ink = std::any_of(run.begin(), run.end(),
				[](const PositionedGlyph& g) { return !g.is_blank && !g.ink_bounds_pt.has_value(); });
			drawn = builder_.draw_glyph_run(run, size, mode, fill_color, stroke_color, run_matrix,
				unknown_ink ? std::optional<PdfRect>(bounds) : std::nullopt, std::move(handle));
			first = last;
		}
		// The text matrix advances whether or not the builder kept the run (a run it refuses is skipped like any
		// malformed operator), so the text after it stays where the file put it.
		advance_text(vertical ? 0.0 : x * state_.horizontal_scaling, vertical ? y : 0.0);
		return drawn;
	}

	// A hidden string that does not clip records nothing, and its text matrix advances as if it were drawn (8.11.3.2).
	[[nodiscard]] PdfResult<void> advance_hidden_text(const TextFont& font, std::span<const uint8_t> bytes)
	{
		RECTO_PDF_RETURN_IF_ERROR(builder_.charge_work(bytes.size() * font.code_cost()));
		const double size = state_.font_size;
		const bool vertical = font.vertical();
		double along = 0.0;
		for (size_t at = 0; at < bytes.size();) {
			const TextCode code = font.next(bytes.subspan(at));
			at += code.length != 0u ? code.length : 1u;
			const double spacing = state_.char_spacing + (code.word_space ? state_.word_spacing : 0.0);
			along += (vertical ? code.w1 : code.w0) * size + spacing;
		}
		advance_text(vertical ? 0.0 : along * state_.horizontal_scaling, vertical ? along : 0.0);
		return {};
	}

	// `code`'s glyph, with a composite font's ink read from its face once per face and glyph on the page (at most
	// kMaxInkMemo of them kept; past that each is read as it is shown).
	[[nodiscard]] PdfResult<TextGlyph> glyph_of(const TextFont& font, const TextCode& code)
	{
		TextGlyph drawn = font.glyph(code);
		if (drawn.ink_known || drawn.face == TextGlyph::kNoFace || drawn.face >= font.faces().size()) {
			return drawn;
		}
		const std::shared_ptr<const LoadedFont>& face = font.faces()[drawn.face];
		const InkKey key{face, drawn.glyph};
		if (const auto it = ink_memo_.find(key); it != ink_memo_.end()) {
			drawn.ink = it->second.first;
			drawn.blank = it->second.second;
			return drawn;
		}
		auto ink = face->ink_box(drawn.glyph);
		if (!ink.has_value()) {
			if (ink.error().code == ErrorCode::OutOfMemory) {
				return ink.error();
			}
		} else if (ink->has_value()) {
			drawn.ink = **ink;
		} else {
			drawn.blank = true;
		}
		if (ink_memo_.size() < kMaxInkMemo) {
			try {
				ink_memo_.emplace(key, std::make_pair(drawn.ink, drawn.blank));
			} catch (const std::bad_alloc&) {
				return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory keeping a glyph's ink box");
			}
		}
		return drawn;
	}

	// `Do` (ISO 32000-1 8.8): a Form XObject drawn, an Image XObject left to section 44, a PostScript one never run.
	[[nodiscard]] PdfResult<void> draw_xobject(std::string_view name)
	{
		if (hidden()) {
			return {}; // inside a hidden block, whatever it is, unread (8.11.3.3)
		}
		const PdfDict* xobjects = category("XObject", scope_->xobjects);
		const PdfValue* entry = xobjects != nullptr ? xobjects->get(name) : nullptr;
		if (entry == nullptr) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "Do names an XObject the resources do not hold"); // recorded
			return {};
		}
		const std::optional<IndirectRef> ref = entry->as_ref();
		if (!ref.has_value()) {
			// A stream is always an indirect object (7.3.8), so a direct value never names an XObject.
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An XObject is not a stream"); // recorded
			return {};
		}
		const XObjectEntry& target = xobject_entry(*ref);
		if (target.hidden) {
			return {}; // before a form is charged or decoded, and before any kind is drawn
		}
		if (target.kind == XObjectEntry::Kind::Form) {
			return draw_form(target, object_key(*ref));
		}
		if (target.kind == XObjectEntry::Kind::Image) {
			return draw_image_object(*ref);
		}
		return {}; // refused, or a PostScript XObject: recorded when first met
	}

	// An Image XObject for the page (D01 T02 section 44), read once from its dictionary. Its samples are decoded once and
	// kept raw, before /Decode, while the budget allows; a conversion is kept per effective colour space and intent,
	// since one image can be drawn under different /Default* resources or rendering intents.
	struct ConvertedImage {
		std::shared_ptr<const ColorSpace> space;
		RenderingIntent intent{RenderingIntent::RelativeColorimetric};
		std::shared_ptr<const DecodedBitmap> bitmap; // null: refused, and remembered
	};
	struct ImageSource {
		IndirectRef ref{};
		bool usable{true};
		bool stencil{false};
		bool decoded{false};
		bool image_filter{false}; // ends in DCTDecode or JPXDecode: the decoder says the size and the components
		bool jpx{false};
		bool one_bit_filter{false}; // ends in CCITTFaxDecode or JBIG2Decode
		bool interpolate{false};
		bool mask_interpolate{false};
		uint32_t width{0};
		uint32_t height{0};
		uint32_t bits{8};
		uint32_t source_bits{8}; // a JPX stream's own, its samples scaled to eight bits for us
		size_t components{0}; // the decoder's, for an image filter
		std::optional<RenderingIntent> intent{};
		std::optional<PdfValue> color_space{};
		std::vector<double> decode{};
		std::vector<int64_t> color_key{};
		std::vector<double> matte{};
		int64_t alpha_in_data{0};
		std::shared_ptr<const PdfStream> inline_stream{}; // an inline image's dictionary and data, built once
		std::shared_ptr<const std::vector<uint8_t>> samples{}; // raw, while kept
		std::shared_ptr<const std::vector<uint8_t>> alpha{};   // a JPX image's own, for /SMaskInData
		std::shared_ptr<const ColorSpace> own_space{}; // a JPX image's own profile
		std::shared_ptr<const DecodedBitmap> stencil_bitmap{};
		std::shared_ptr<const DecodedBitmap> mask{};
		std::vector<ConvertedImage> conversions{};
		std::vector<std::pair<uint64_t, std::shared_ptr<const ColorSpace>>> spaces{}; // by the scope's serial
	};

	// An inline image: where its `EI` is, and its source.
	struct InlineImage {
		size_t end{0};
		InlineEntries entries{};
		std::span<const uint8_t> data{}; // in content the page holds for its life
		bool prepared{false};
		ImageSource source{};
	};

	// An object read for an image, outside the held-resource budget and not kept. Null when missing, null or damaged; a
	// fatal failure is kept for `dispatch`.
	[[nodiscard]] std::shared_ptr<const PdfValue> image_object(IndirectRef ref)
	{
		auto object = document_.resolve_object_shared(ref);
		if (!object.has_value()) {
			if (fatal(object.error()) && !resolve_failure_.has_value()) {
				resolve_failure_ = object.error();
			}
			return nullptr;
		}
		return (*object != nullptr && (*object)->is_null()) ? nullptr : std::move(*object);
	}

	[[nodiscard]] std::optional<int64_t> int_of(const PdfDict& dict, std::string_view key)
	{
		const Held value = entry_of(dict, key);
		return value ? value->as_int() : std::nullopt;
	}

	[[nodiscard]] bool bool_of(const PdfDict& dict, std::string_view key)
	{
		const Held value = entry_of(dict, key);
		return value && value->as_bool() == std::optional<bool>(true);
	}

	// The numbers of an array entry, resolved; empty when it is absent, too long or holds anything else.
	[[nodiscard]] std::vector<double> numbers_of(const PdfDict& dict, std::string_view key)
	{
		std::vector<double> out;
		const Held value = entry_of(dict, key);
		const PdfArray* array = value ? value->as_array() : nullptr;
		if (array == nullptr || array->size() > kMaxImageArrayEntries) {
			return out;
		}
		for (const PdfValue& element : *array) {
			const Held number = shared(element);
			const std::optional<double> n = number ? number->as_number() : std::nullopt;
			if (!n.has_value()) {
				return {};
			}
			out.push_back(*n);
		}
		return out;
	}

	// The name of a stream's last filter, or empty.
	[[nodiscard]] std::string last_filter(const PdfDict& dict)
	{
		const Held filter = entry_of(dict, "Filter");
		if (!filter) {
			return {};
		}
		if (const PdfArray* stages = filter->as_array()) {
			if (stages->empty()) {
				return {};
			}
			const Held stage = shared(*std::prev(stages->end()));
			const std::optional<std::string_view> name = stage ? stage->as_name() : std::nullopt;
			return name.has_value() ? std::string(*name) : std::string();
		}
		const std::optional<std::string_view> name = filter->as_name();
		return name.has_value() ? std::string(*name) : std::string();
	}

	[[nodiscard]] static bool usable_size(uint64_t width, uint64_t height) noexcept
	{
		return width >= 1u && height >= 1u && width <= 65'536u && height <= 65'536u &&
			width * height * 4u <= kMaxDecodedStreamBytes;
	}

	// A stream's data, within what the page may decode: every byte the decoders produce counts, kept or not, so distinct
	// damaged images cannot each cost a full decode. None when it cannot be drawn, recorded; OutOfMemory is returned.
	[[nodiscard]] PdfResult<std::optional<ImageStream>> decoded_image(const PdfStream& stream)
	{
		const auto refused = [&] {
			record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, image_decoded_,
				image_budget_, "Image data past the page's budget is not drawn"));
		};
		if (image_decoded_ >= image_budget_) {
			refused();
			return std::optional<ImageStream>{};
		}
		const size_t limit = std::min(kMaxDecodedStreamBytes, image_budget_ - image_decoded_);
		detail::ScopedDecodeWorkMeter work;
		auto decoded = document_.decode_image_stream(stream, limit);
		const auto charge = [&](uint64_t bytes) {
			image_decoded_ = static_cast<size_t>(std::min<uint64_t>(image_budget_, uint64_t{image_decoded_} + bytes));
		};
		if (!decoded.has_value()) {
			const ErrorCode code = decoded.error().code;
			if (code == ErrorCode::OutOfMemory) {
				return decoded.error();
			}
			if (code == ErrorCode::DecompressionLimitExceeded) {
				charge(limit); // what it decoded used all it was allowed
				refused();
			} else {
				charge(work.bytes());
				record_diagnostic(decoded.error()); // a filter's error does not record itself
			}
			return std::optional<ImageStream>{};
		}
		charge(std::max<uint64_t>(work.bytes(), decoded->samples.size() + (decoded->image.has_value() ? decoded->image->pixels.size() : 0u)));
		return std::optional<ImageStream>(std::move(*decoded));
	}

	// Whether `bytes` more can be held for the page's images.
	[[nodiscard]] bool hold_image_bytes(size_t bytes) noexcept
	{
		if (bytes > image_budget_ - std::min(image_budget_, image_held_)) {
			return false;
		}
		image_held_ += bytes;
		return true;
	}

	void record_image_refusal(size_t bytes)
	{
		record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, image_held_ + bytes,
			image_budget_, "An image past the page's bitmap budget is not drawn"));
	}

	[[nodiscard]] static uint32_t sample_at(std::span<const uint8_t> row, size_t index, uint32_t bits) noexcept
	{
		switch (bits) {
		case 16u:
			return (uint32_t{row[index * 2u]} << 8u) | row[index * 2u + 1u];
		case 8u:
			return row[index];
		default: {
			const size_t bit = index * bits;
			const uint32_t shift = 8u - bits - static_cast<uint32_t>(bit % 8u);
			return (uint32_t{row[bit / 8u]} >> shift) & ((1u << bits) - 1u);
		}
		}
	}

	// A stencil's cells as a bitmap whose alpha says which are painted (8.9.6.2: a sample of 0 paints, or of 1 under
	// /Decode [1 0]); rows the stream does not hold paint nothing. Null when it cannot be used, recorded.
	[[nodiscard]] PdfResult<std::shared_ptr<const DecodedBitmap>> stencil_from(const PdfStream& stream, bool tinted)
	{
		const PdfDict& dict = stream.dict;
		const std::vector<double> decode = numbers_of(dict, "Decode");
		const bool one_paints = decode.size() == 2u && decode[0] > decode[1];
		const std::string filter = last_filter(dict);
		const bool image_filter = filter == "JPXDecode" || filter == "DCTDecode" || filter == "DCT";
		uint64_t width = static_cast<uint64_t>(std::max<int64_t>(0, int_of(dict, "Width").value_or(0)));
		uint64_t height = static_cast<uint64_t>(std::max<int64_t>(0, int_of(dict, "Height").value_or(0)));
		if (!image_filter && !usable_size(width, height)) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A stencil mask's size cannot be used; it is not drawn"); // recorded
			return std::shared_ptr<const DecodedBitmap>{};
		}
		// 8.9.6.2: if present it shall be 1. Other data read as one bit would mask the wrong cells.
		if (!image_filter && int_of(dict, "BitsPerComponent").value_or(1) != 1) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A stencil mask's /BitsPerComponent is not 1; it is not used"); // recorded
			return std::shared_ptr<const DecodedBitmap>{};
		}
		RECTO_PDF_TRY(std::optional<ImageStream> data, decoded_image(stream));
		if (!data.has_value()) {
			return std::shared_ptr<const DecodedBitmap>{};
		}
		// JPX and DCT have no one-bit form: a sample at or past the middle reads as 1.
		const bool wide = data->image.has_value();
		if (wide) {
			width = data->image->width;
			height = data->image->height;
			if (data->image->components != 1u || !usable_size(width, height)) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "A stencil mask's image data cannot be used; it is not drawn"); // recorded
				return std::shared_ptr<const DecodedBitmap>{};
			}
		}
		const std::vector<uint8_t>& samples = wide ? data->image->pixels : data->samples;
		const size_t row_bytes = wide ? static_cast<size_t>(width) : static_cast<size_t>((width + 7u) / 8u);
		const size_t rows = std::min<size_t>(static_cast<size_t>(height), samples.size() / row_bytes);
		if (rows < height) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A stencil mask's data is short; the rest paints nothing"); // recorded
		}
		const size_t bytes = static_cast<size_t>(width * height * 4u);
		if (!hold_image_bytes(bytes)) {
			record_image_refusal(bytes);
			return std::shared_ptr<const DecodedBitmap>{};
		}
		std::vector<uint8_t> pixels(bytes, 0u);
		for (size_t y = 0; y < rows; ++y) {
			const std::span<const uint8_t> row(samples.data() + y * row_bytes, row_bytes);
			uint8_t* out = pixels.data() + y * static_cast<size_t>(width) * 4u;
			for (size_t x = 0; x < width; ++x) {
				const bool one = wide ? row[x] >= 128u : ((row[x / 8u] >> (7u - x % 8u)) & 1u) != 0u;
				if (one == one_paints) {
					std::memset(out + x * 4u, 0xFF, 4u);
				}
			}
		}
		auto bitmap = DecodedBitmap::create(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
			static_cast<uint32_t>(width) * 4u, std::move(pixels), tinted);
		if (!bitmap.has_value()) {
			if (bitmap.error().code == ErrorCode::OutOfMemory) {
				return bitmap.error();
			}
			return std::shared_ptr<const DecodedBitmap>{};
		}
		return std::shared_ptr<const DecodedBitmap>(std::move(*bitmap));
	}

	// An /SMask: its gray samples, through its /Decode, as the alpha of a bitmap of its own size. Null when it cannot be
	// used, recorded.
	[[nodiscard]] PdfResult<std::shared_ptr<const DecodedBitmap>> soft_mask_from(const PdfStream& stream)
	{
		const PdfDict& dict = stream.dict;
		const std::string filter = last_filter(dict);
		const bool image_filter = filter == "JPXDecode" || filter == "DCTDecode" || filter == "DCT";
		const bool one_bit = filter == "CCITTFaxDecode" || filter == "CCF" || filter == "JBIG2Decode";
		uint64_t width = static_cast<uint64_t>(std::max<int64_t>(0, int_of(dict, "Width").value_or(0)));
		uint64_t height = static_cast<uint64_t>(std::max<int64_t>(0, int_of(dict, "Height").value_or(0)));
		uint32_t bits = static_cast<uint32_t>(std::clamp<int64_t>(int_of(dict, "BitsPerComponent").value_or(one_bit ? 1 : 0), 0, 32));
		if (one_bit) {
			bits = 1u;
		}
		if (!image_filter && (!usable_size(width, height) || (bits != 1u && bits != 2u && bits != 4u && bits != 8u && bits != 16u))) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An /SMask's size or /BitsPerComponent cannot be used; the image is drawn without it"); // recorded
			return std::shared_ptr<const DecodedBitmap>{};
		}
		RECTO_PDF_TRY(std::optional<ImageStream> data, decoded_image(stream));
		if (!data.has_value()) {
			return std::shared_ptr<const DecodedBitmap>{};
		}
		if (data->image.has_value()) {
			width = data->image->width;
			height = data->image->height;
			bits = 8u;
			if (data->image->components != 1u || !usable_size(width, height)) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "An /SMask is not one gray component; the image is drawn without it"); // recorded
				return std::shared_ptr<const DecodedBitmap>{};
			}
		}
		const std::vector<uint8_t>& samples = data->image.has_value() ? data->image->pixels : data->samples;
		const size_t row_bytes = static_cast<size_t>((width * bits + 7u) / 8u);
		const size_t rows = std::min<size_t>(static_cast<size_t>(height), samples.size() / row_bytes);
		if (rows < height) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An /SMask's data is short; what it does not hold is transparent"); // recorded
		}
		const size_t bytes = static_cast<size_t>(width * height * 4u);
		if (!hold_image_bytes(bytes)) {
			record_image_refusal(bytes);
			return std::shared_ptr<const DecodedBitmap>{};
		}
		std::vector<double> decode = numbers_of(dict, "Decode");
		if (decode.size() != 2u || filter == "JPXDecode") {
			decode.clear(); // a JPX image ignores /Decode unless it is an image mask (Table 89)
		}
		const std::shared_ptr<const ColorSpace> gray = ColorSpace::device(ColorSpaceKind::DeviceGray);
		std::vector<uint8_t> pixels(bytes, 0u);
		for (size_t y = 0; y < rows; ++y) {
			const std::span<const uint8_t> row(samples.data() + y * row_bytes, row_bytes);
			RECTO_PDF_TRY(const std::vector<uint8_t> rgba,
				gray->convert_samples(row, static_cast<size_t>(width), bits, decode, RenderingIntent::RelativeColorimetric, document_));
			uint8_t* out = pixels.data() + y * static_cast<size_t>(width) * 4u;
			for (size_t x = 0; x < width; ++x) {
				const uint8_t a = rgba[x * 4u];
				out[x * 4u] = out[x * 4u + 1u] = out[x * 4u + 2u] = out[x * 4u + 3u] = a;
			}
		}
		auto bitmap = DecodedBitmap::create(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
			static_cast<uint32_t>(width) * 4u, std::move(pixels));
		if (!bitmap.has_value()) {
			if (bitmap.error().code == ErrorCode::OutOfMemory) {
				return bitmap.error();
			}
			return std::shared_ptr<const DecodedBitmap>{};
		}
		return std::shared_ptr<const DecodedBitmap>(std::move(*bitmap));
	}

	// The image's dictionary, read once; its data waits for the first draw that needs it. Null past the page's limit.
	[[nodiscard]] PdfResult<ImageSource*> image_source(IndirectRef ref, const PdfDict& dict)
	{
		ImageSource src;
		src.ref = ref;
		RECTO_PDF_RETURN_IF_ERROR(read_image_dictionary(src, dict));
		if (image_sources_.size() >= kMaxHeldResources) {
			(void)make_pdf_error(ErrorCode::ResourceLimit, "Image objects past the page's limit are not drawn"); // recorded
			return static_cast<ImageSource*>(nullptr);
		}
		return &image_sources_.emplace(object_key(ref), std::move(src)).first->second;
	}

	[[nodiscard]] PdfResult<void> prepare_stencil(ImageSource& src, const PdfStream& stream)
	{
		if (src.stencil && src.usable) {
			RECTO_PDF_TRY(src.stencil_bitmap, stencil_from(stream, true));
			src.usable = src.stencil_bitmap != nullptr;
		}
		return {};
	}

	[[nodiscard]] PdfResult<void> read_image_dictionary(ImageSource& src, const PdfDict& dict)
	{
		const std::string filter = last_filter(dict);
		src.jpx = filter == "JPXDecode";
		src.image_filter = src.jpx || filter == "DCTDecode" || filter == "DCT";
		src.one_bit_filter = filter == "CCITTFaxDecode" || filter == "CCF" || filter == "JBIG2Decode";
		src.stencil = bool_of(dict, "ImageMask");
		src.interpolate = bool_of(dict, "Interpolate");
		const int64_t width = int_of(dict, "Width").value_or(0);
		const int64_t height = int_of(dict, "Height").value_or(0);
		if (width > 0 && height > 0) {
			src.width = static_cast<uint32_t>(std::min<int64_t>(width, 0xFFFFFFFFll));
			src.height = static_cast<uint32_t>(std::min<int64_t>(height, 0xFFFFFFFFll));
		}
		if (!src.stencil) {
			if (src.one_bit_filter) {
				src.bits = 1u;
			} else if (!src.image_filter) {
				const int64_t bits = int_of(dict, "BitsPerComponent").value_or(0);
				src.bits = bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16 ? static_cast<uint32_t>(bits) : 0u;
				if (src.bits == 0u) {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "An image's /BitsPerComponent cannot be used; it is not drawn"); // recorded
					src.usable = false;
				}
			}
			if (!src.image_filter && !usable_size(src.width, src.height)) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "An image's size cannot be used; it is not drawn"); // recorded
				src.usable = false;
			}
			if (const PdfValue* space = dict.get("ColorSpace"); space != nullptr && !space->is_null()) {
				RECTO_PDF_TRY_REF(cloned, space->deep_clone());
				src.color_space = std::move(cloned);
			} else if (!src.image_filter && !src.one_bit_filter) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "An image names no /ColorSpace; it is not drawn"); // recorded
				src.usable = false;
			}
			src.decode = numbers_of(dict, "Decode");
			if (const Held intent = entry_of(dict, "Intent"); intent && intent->as_name().has_value()) {
				src.intent = rendering_intent_named(*intent->as_name());
			}
			src.alpha_in_data = src.jpx ? int_of(dict, "SMaskInData").value_or(0) : 0;
		}
		return {};
	}

	// The image's masks and its own alpha, read when its data first is: /SMask over /Mask (11.6.5.3, 8.9.6). One that
	// cannot be used leaves the image drawn without it, recorded.
	[[nodiscard]] PdfResult<void> read_masks(ImageSource& src, const PdfDict& dict)
	{
		const PdfValue* soft_value = dict.get("SMask");
		const Held soft = soft_value != nullptr ? shared_outside_budget(*soft_value) : Held{};
		if (const PdfStream* stream = soft ? soft->as_stream() : nullptr) {
			std::vector<double> matte = numbers_of(stream->dict, "Matte");
			src.mask_interpolate = bool_of(stream->dict, "Interpolate");
			RECTO_PDF_TRY(src.mask, soft_mask_from(*stream));
			if (src.mask != nullptr && !matte.empty()) {
				if (src.mask->width == src.width && src.mask->height == src.height) {
					src.matte = std::move(matte); // undone from the mask's own alpha, which is already held and charged
				} else {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "/Matte needs an /SMask of the image's size; it is ignored"); // recorded
				}
			}
			return {};
		}
		if (soft && !soft->is_null()) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An /SMask is not a stream; the image is drawn without it"); // recorded
		}
		if (const PdfValue* mask_value = dict.get("Mask")) {
			const Held mask = shared_outside_budget(*mask_value);
			if (mask && mask->as_stream() != nullptr) {
				const PdfStream& stream = *mask->as_stream();
				if (bool_of(stream.dict, "ImageMask")) {
					src.mask_interpolate = bool_of(stream.dict, "Interpolate");
					RECTO_PDF_TRY(src.mask, stencil_from(stream, false));
				} else {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "An explicit /Mask is not an image mask; the image is drawn without it"); // recorded
				}
				return {};
			}
			if (mask && mask->as_array() != nullptr) {
				// Whole numbers a sample can hold, read before any becomes an integer: a real past int64_t has no conversion.
				const std::vector<double> key = numbers_of(dict, "Mask");
				const bool whole = !key.empty() && std::all_of(key.begin(), key.end(), [](double n) {
					return n >= 0.0 && n <= 65'535.0 && n == std::floor(n);
				});
				if (whole) {
					for (const double n : key) {
						src.color_key.push_back(static_cast<int64_t>(n));
					}
				} else {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "A colour key /Mask cannot be used; it is ignored"); // recorded
				}
				return {};
			}
		}
		return {};
	}

	// A value, an indirect one read outside the held-resource budget, since a mask's data can be large.
	[[nodiscard]] Held shared_outside_budget(const PdfValue& value)
	{
		const std::optional<IndirectRef> ref = value.as_ref();
		if (!ref.has_value()) {
			return Held{nullptr, &value};
		}
		std::shared_ptr<const PdfValue> object = image_object(*ref);
		const PdfValue* target = object.get();
		return Held{std::move(object), target};
	}

	// The image's data, decoding it (again, if it was released for the budget). Null when it cannot be drawn.
	[[nodiscard]] PdfResult<std::shared_ptr<const std::vector<uint8_t>>> image_samples(ImageSource& src)
	{
		if (src.samples != nullptr) {
			return src.samples;
		}
		const std::shared_ptr<const PdfValue> object = src.inline_stream == nullptr ? image_object(src.ref) : nullptr;
		const PdfStream* stream = src.inline_stream != nullptr ? src.inline_stream.get() : (object != nullptr ? object->as_stream() : nullptr);
		if (stream == nullptr) {
			src.usable = false;
			return std::shared_ptr<const std::vector<uint8_t>>{};
		}
		RECTO_PDF_TRY(std::optional<ImageStream> data, decoded_image(*stream));
		if (!data.has_value()) {
			src.usable = false;
			return std::shared_ptr<const std::vector<uint8_t>>{};
		}
		std::shared_ptr<const std::vector<uint8_t>> samples;
		if (data->image.has_value()) {
			WicDecodedImage& image = *data->image;
			if (!usable_size(image.width, image.height)) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "An image's size cannot be used; it is not drawn"); // recorded
				src.usable = false;
				return std::shared_ptr<const std::vector<uint8_t>>{};
			}
			src.width = image.width;
			src.height = image.height;
			src.components = image.components;
			src.source_bits = image.source_bits;
			if (!src.decoded && src.jpx && !image.icc_profile.empty()) {
				RECTO_PDF_TRY(src.own_space, ColorSpace::from_icc_profile(image.icc_profile, image.components));
			}
			if (!src.decoded && src.alpha_in_data != 0 && image.rgba_pixels.size() == size_t{image.width} * image.height * 4u) {
				const size_t alpha_bytes = size_t{image.width} * image.height;
				if (hold_image_bytes(alpha_bytes)) {
					std::vector<uint8_t> alpha(alpha_bytes);
					for (size_t i = 0; i < alpha.size(); ++i) {
						alpha[i] = image.rgba_pixels[i * 4u + 3u];
					}
					src.alpha = std::make_shared<const std::vector<uint8_t>>(std::move(alpha));
				} else {
					record_image_refusal(alpha_bytes); // its own alpha cannot be kept, so it draws opaque
				}
			}
			samples = std::make_shared<const std::vector<uint8_t>>(std::move(image.pixels));
		} else {
			samples = std::make_shared<const std::vector<uint8_t>>(std::move(data->samples));
		}
		if (!src.decoded) {
			src.decoded = true;
			RECTO_PDF_RETURN_IF_ERROR(read_masks(src, stream->dict));
			if (src.mask != nullptr) {
				src.alpha = nullptr; // /SMask and /Mask win over /SMaskInData
			}
		}
		if (hold_image_bytes(samples->size())) {
			src.samples = samples;
		}
		return samples;
	}

	// The colour space `src` draws in here: its /ColorSpace in the drawing scope, kept per scope; null when none can be.
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> declared_space(ImageSource& src)
	{
		if (!src.color_space.has_value()) {
			return std::shared_ptr<const ColorSpace>{};
		}
		for (const auto& [serial, space] : src.spaces) {
			if (serial == scope_->serial) {
				return space;
			}
		}
		RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, spaces().resolve(*src.color_space));
		if (src.spaces.size() < kMaxImageConversions) {
			src.spaces.emplace_back(scope_->serial, space);
		}
		return space;
	}

	[[nodiscard]] PdfResult<void> draw_image_object(IndirectRef ref)
	{
		ImageSource* src = nullptr;
		if (const auto it = image_sources_.find(object_key(ref)); it != image_sources_.end()) {
			src = &it->second;
		} else {
			const std::shared_ptr<const PdfValue> object = image_object(ref);
			const PdfStream* stream = object != nullptr ? object->as_stream() : nullptr;
			if (stream == nullptr) {
				return {};
			}
			RECTO_PDF_TRY(src, image_source(ref, stream->dict));
			if (src == nullptr) {
				return {};
			}
			RECTO_PDF_RETURN_IF_ERROR(prepare_stencil(*src, *stream));
		}
		if (!src->usable) {
			return {};
		}
		return draw_image_source(*src);
	}

	[[nodiscard]] PdfResult<void> draw_image_source(ImageSource& source)
	{
		ImageSource* const src = &source;
		if (src->stencil) {
			// Painted in the current non-stroking colour; a colour that marks nothing draws nothing (8.6.6.4).
			if (!state_.fill.paints()) {
				return {};
			}
			RECTO_PDF_TRY(const ColorRgba tint, rgba(state_.fill, false));
			RECTO_PDF_RETURN_IF_ERROR(sync_state(false));
			return builder_.draw_image(src->stencil_bitmap, PdfMatrix::identity(), src->interpolate, nullptr, tint);
		}
		RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> declared, declared_space(*src));
		if (declared != nullptr && !declared->paints()) {
			return {}; // its own colour space marks nothing (8.6.6.4), so nothing is decoded
		}
		if (declared == nullptr && src->color_space.has_value()) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An image's /ColorSpace cannot be used; it is not drawn"); // recorded
			return {};
		}
		std::shared_ptr<const std::vector<uint8_t>> samples;
		if (!src->decoded) {
			RECTO_PDF_TRY(samples, image_samples(*src));
			if (samples == nullptr) {
				return {};
			}
		}
		// What the data says wins over what the dictionary says for a decoder that reports its own components.
		std::shared_ptr<const ColorSpace> space = declared;
		if (src->image_filter && (space == nullptr || space->components() != src->components)) {
			space = src->own_space;
			if (space == nullptr || space->components() != src->components) {
				if (src->components != 1u && src->components != 3u && src->components != 4u) {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "An image has components no device space holds; it is not drawn"); // recorded
					return {};
				}
				RECTO_PDF_TRY(space, spaces().device(src->components == 1u ? ColorSpaceKind::DeviceGray
						: src->components == 3u                            ? ColorSpaceKind::DeviceRGB
																		   : ColorSpaceKind::DeviceCMYK));
			}
		} else if (space == nullptr) {
			RECTO_PDF_TRY(space, spaces().device(ColorSpaceKind::DeviceGray)); // CCITT and JBIG2 data is one gray bit
		}
		if (space == nullptr || !space->paints()) {
			return {};
		}
		const RenderingIntent intent = src->intent.value_or(state_.rendering_intent);
		std::shared_ptr<const DecodedBitmap> bitmap;
		bool known = false;
		for (const ConvertedImage& conversion : src->conversions) {
			if (conversion.space == space && conversion.intent == intent) {
				bitmap = conversion.bitmap;
				known = true;
				break;
			}
		}
		if (!known) {
			if (samples == nullptr) {
				RECTO_PDF_TRY(samples, image_samples(*src));
				if (samples == nullptr) {
					return {};
				}
			}
			RECTO_PDF_TRY(bitmap, converted_image(*src, *samples, *space, intent));
			if (src->conversions.size() < kMaxImageConversions) {
				src->conversions.push_back(ConvertedImage{space, intent, bitmap});
			}
		}
		if (bitmap == nullptr) {
			return {};
		}
		RECTO_PDF_RETURN_IF_ERROR(sync_state(false));
		return builder_.draw_image(std::move(bitmap), PdfMatrix::identity(), src->interpolate, src->mask, ColorRgba::black(), src->mask_interpolate);
	}

	// The image's samples in `space` as premultiplied BGRA: /Decode applied by `convert_samples` (never for a JPX image,
	// which ignores it, ISO 32000-2 Table 87), a colour key compared with the raw samples, /Matte undone on the components
	// before conversion. Rows the data does not hold stay transparent. Null when it cannot be drawn, recorded.
	[[nodiscard]] PdfResult<std::shared_ptr<const DecodedBitmap>> converted_image(
		ImageSource& src, const std::vector<uint8_t>& samples, const ColorSpace& space, RenderingIntent intent)
	{
		const size_t width = src.width;
		const size_t height = src.height;
		const size_t comps = space.components();
		const size_t bytes = width * height * 4u;
		const auto over_budget = [&] { return bytes > image_budget_ - std::min(image_budget_, image_held_); };
		if (over_budget() && src.samples != nullptr) {
			// Its raw samples give way, decoded again if needed; the caller's `samples` still holds them for this conversion.
			image_held_ -= std::min(image_held_, src.samples->size());
			src.samples = nullptr;
		}
		if (!hold_image_bytes(bytes)) {
			record_image_refusal(bytes);
			return std::shared_ptr<const DecodedBitmap>{};
		}
		const uint64_t row_bits = uint64_t{width} * comps * src.bits;
		const size_t row_bytes = static_cast<size_t>((row_bits + 7u) / 8u);
		const size_t rows = row_bytes != 0u ? std::min(height, samples.size() / row_bytes) : 0u;
		if (rows < height) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An image's data is short; the rest is transparent"); // recorded
		}
		std::vector<double> decode = src.jpx ? std::vector<double>{} : src.decode;
		if (!decode.empty() && decode.size() != comps * 2u) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An image's /Decode does not fit its components; the default is used"); // recorded
			decode.clear();
		}
		const uint32_t max_sample = (uint32_t{1} << src.bits) - 1u;
		bool use_key = !src.color_key.empty();
		if (use_key) {
			use_key = src.color_key.size() == comps * 2u;
			for (size_t i = 0; use_key && i < comps; ++i) {
				use_key = src.color_key[i * 2u] <= src.color_key[i * 2u + 1u] && src.color_key[i * 2u + 1u] <= int64_t{max_sample};
			}
			if (!use_key) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "A colour key /Mask does not fit its image; it is ignored"); // recorded
			}
		}
		bool use_matte = !src.matte.empty();
		if (use_matte && (src.matte.size() != comps || space.kind() == ColorSpaceKind::Indexed)) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "/Matte does not fit its image; it is ignored"); // recorded
			use_matte = false;
		}
		std::vector<uint8_t> pixels(bytes, 0u);
		std::vector<uint8_t> unmatted;
		// An Indexed JPX image of fewer than eight bits: the decoder scaled its samples, and these are its palette indices.
		const bool index_samples = src.jpx && space.kind() == ColorSpaceKind::Indexed && src.source_bits < 8u;
		const uint32_t source_max = (uint32_t{1} << std::min<uint32_t>(src.source_bits, 8u)) - 1u;
		std::vector<uint8_t> indices;
		for (size_t y = 0; y < rows; ++y) {
			std::span<const uint8_t> row(samples.data() + y * row_bytes, row_bytes);
			if (index_samples) {
				indices.resize(row_bytes);
				std::transform(row.begin(), row.end(), indices.begin(),
					[source_max](uint8_t v) { return static_cast<uint8_t>((uint32_t{v} * source_max + 127u) / 255u); });
				row = indices;
			}
			std::span<const uint8_t> source = row;
			uint32_t bits = src.bits;
			std::span<const double> row_decode = decode;
			if (use_matte) {
				// 11.6.5.3: c = m + (c' - m) / alpha on the decoded components; the result goes on as 16-bit samples
				// over each component's default range.
				unmatted.resize(width * comps * 2u);
				for (size_t x = 0; x < width; ++x) {
					const double alpha = src.mask->pixels[(y * width + x) * 4u + 3u] / 255.0;
					for (size_t c = 0; c < comps; ++c) {
						const auto [low, high] = space.component_range(c);
						const double d0 = decode.empty() ? low : decode[c * 2u];
						const double d1 = decode.empty() ? high : decode[c * 2u + 1u];
						double value = d0 + sample_at(row, x * comps + c, src.bits) * (d1 - d0) / max_sample;
						if (alpha > 0.0) {
							value = src.matte[c] + (value - src.matte[c]) / alpha;
						}
						const double unit = high > low ? std::clamp((value - low) / (high - low), 0.0, 1.0) : 0.0;
						const auto encoded = static_cast<uint32_t>(std::lround(unit * 65535.0));
						unmatted[(x * comps + c) * 2u] = static_cast<uint8_t>(encoded >> 8u);
						unmatted[(x * comps + c) * 2u + 1u] = static_cast<uint8_t>(encoded & 0xFFu);
					}
				}
				source = unmatted;
				bits = 16u;
				row_decode = {};
			}
			auto converted = space.convert_samples(source, width, bits, row_decode, intent, document_);
			if (!converted.has_value()) {
				if (converted.error().code == ErrorCode::OutOfMemory) {
					return converted.error();
				}
				return std::shared_ptr<const DecodedBitmap>{};
			}
			const std::vector<uint8_t>& rgba = *converted;
			uint8_t* out = pixels.data() + y * width * 4u;
			for (size_t x = 0; x < width; ++x) {
				uint32_t alpha = src.alpha != nullptr ? (*src.alpha)[y * width + x] : 255u;
				if (use_key) {
					bool keyed = true;
					for (size_t c = 0; keyed && c < comps; ++c) {
						const int64_t value = sample_at(row, x * comps + c, src.bits);
						keyed = value >= src.color_key[c * 2u] && value <= src.color_key[c * 2u + 1u];
					}
					if (keyed) {
						alpha = 0u;
					}
				}
				const auto premultiplied = [alpha](uint8_t v) { return static_cast<uint8_t>((uint32_t{v} * alpha + 127u) / 255u); };
				out[x * 4u] = premultiplied(rgba[x * 4u + 2u]);
				out[x * 4u + 1u] = premultiplied(rgba[x * 4u + 1u]);
				out[x * 4u + 2u] = premultiplied(rgba[x * 4u]);
				out[x * 4u + 3u] = static_cast<uint8_t>(alpha);
			}
		}
		auto bitmap = DecodedBitmap::create(src.width, src.height, src.width * 4u, std::move(pixels));
		if (!bitmap.has_value()) {
			if (bitmap.error().code == ErrorCode::OutOfMemory) {
				return bitmap.error();
			}
			return std::shared_ptr<const DecodedBitmap>{};
		}
		return std::shared_ptr<const DecodedBitmap>(std::move(*bitmap));
	}

	// The XObject `ref` names, classified once for the page. Its object is held through `held_object` (once per page,
	// within the held budget, a failure or refusal remembered); a form whose `/Matrix` or `/BBox` cannot be used, and
	// a target that is not a stream, are refused once and their object released from the budget.
	[[nodiscard]] const XObjectEntry& xobject_entry(IndirectRef ref)
	{
		static const XObjectEntry kRefused{};
		const uint64_t key = object_key(ref);
		if (const auto it = xobject_entries_.find(key); it != xobject_entries_.end()) {
			return it->second;
		}
		XObjectEntry entry;
		// Read outside the held-resource budget until it is known to be a form: an image's data can be far larger, and
		// is not kept (D01 T02 section 44).
		std::shared_ptr<const PdfValue> object = image_object(ref);
		const PdfStream* stream = object != nullptr ? object->as_stream() : nullptr;
		if (object != nullptr && stream == nullptr) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An XObject is not a stream"); // recorded
		}
		if (stream != nullptr) {
			const Held subtype_value = entry_of(stream->dict, "Subtype");
			const std::optional<std::string_view> subtype = subtype_value ? subtype_value->as_name() : std::nullopt;
			if (subtype == std::optional<std::string_view>("Form")) {
				const std::optional<PdfMatrix> matrix = form_matrix(stream->dict);
				const std::optional<PdfRect> bbox = form_bbox(stream->dict);
				if (matrix.has_value() && bbox.has_value()) {
					object = held_admitted(key, std::move(object));
					if (object != nullptr) {
						entry = XObjectEntry{XObjectEntry::Kind::Form, object, *matrix, *bbox};
					}
				} else {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "A Form XObject's /Matrix or /BBox cannot be used; it is not drawn"); // recorded
				}
			} else if (subtype == std::optional<std::string_view>("Image")) {
				entry.kind = XObjectEntry::Kind::Image;
			} else if (subtype == std::optional<std::string_view>("PS")) {
				record_diagnostic(make_pdf_error(ErrorCode::UnsupportedFeature, "PostScript XObjects are never run (8.8.2)"));
				entry.kind = XObjectEntry::Kind::PostScript;
			} else {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "An XObject has no known /Subtype"); // recorded
			}
			if (entry.kind == XObjectEntry::Kind::Form || entry.kind == XObjectEntry::Kind::Image) {
				entry.hidden = hidden_by_entry(stream->dict);
			}
		}
		if (xobject_entries_.size() >= kMaxHeldResources) {
			return entry.kind == XObjectEntry::Kind::Form || entry.kind == XObjectEntry::Kind::Image
				? (transient_entry_ = std::move(entry))
				: kRefused;
		}
		return xobject_entries_.emplace(key, std::move(entry)).first->second;
	}

	// Whether content is hidden now (ISO 32000-1 8.11.3.2): `hidden_at_` is the marked-content level of the outermost
	// hidden block, 0 for none.
	[[nodiscard]] bool hidden() const noexcept
	{
		return hidden_at_ != 0u;
	}

	// `/Tag /Properties BDC`: a level whatever its operands, hiding its block when `/OC` names a property list that is
	// off. A block inside a hidden one is not evaluated: it cannot change what is hidden.
	[[nodiscard]] PdfResult<void> begin_marked_properties(const std::vector<PdfValue>& operands)
	{
		OcAnswer answer = OcAnswer::NoEffect;
		if (uses_groups_ && !hidden() && operands.size() == 2u && operands[0].as_name() == std::optional<std::string_view>("OC")) {
			const std::optional<std::string_view> name = operands[1].as_name();
			const PdfDict* properties = name.has_value() ? category("Properties", scope_->properties) : nullptr;
			const PdfValue* value = properties != nullptr ? properties->get(*name) : nullptr;
			if (value != nullptr) {
				RECTO_PDF_TRY(answer, evaluate_oc(*value));
			} else {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "/OC BDC names no property list in the resources"); // recorded
			}
		}
		++marked_depth_;
		if (answer == OcAnswer::Off) {
			hidden_at_ = marked_depth_;
		}
		return {};
	}

	// `EMC`: closes the innermost level; one with none open is ignored. A form's extra `EMC`s cannot affect its caller:
	// a form runs only outside hidden content, and `leave_form` gives its caller back its own depth.
	void end_marked() noexcept
	{
		if (marked_depth_ == 0u) {
			return;
		}
		if (marked_depth_ == hidden_at_) {
			hidden_at_ = 0u;
		}
		--marked_depth_;
	}

	// Whether an XObject's `/OC` entry hides it. A failure that stops the page is kept for `dispatch`.
	[[nodiscard]] bool hidden_by_entry(const PdfDict& dict)
	{
		const PdfValue* value = uses_groups_ ? dict.get("OC") : nullptr;
		if (value == nullptr) {
			return false;
		}
		auto answer = evaluate_oc(*value);
		if (!answer.has_value()) {
			if (!resolve_failure_.has_value()) {
				resolve_failure_ = answer.error();
			}
			return false;
		}
		return *answer == OcAnswer::Off;
	}

	[[nodiscard]] static std::vector<uint64_t> sorted_keys(std::span<const IndirectRef> refs)
	{
		std::vector<uint64_t> keys;
		keys.reserve(refs.size());
		for (const IndirectRef& ref : refs) {
			keys.push_back(object_key(ref));
		}
		std::sort(keys.begin(), keys.end());
		return keys;
	}

	// A group's state (8.11.2.3: exclusion wins, since an excluded group has no effect whatever its state).
	[[nodiscard]] OcAnswer group_answer(IndirectRef ref) const noexcept
	{
		const uint64_t key = object_key(ref);
		if (std::binary_search(excluded_groups_.begin(), excluded_groups_.end(), key)) {
			return OcAnswer::NoEffect;
		}
		return std::binary_search(off_groups_.begin(), off_groups_.end(), key) ? OcAnswer::Off : OcAnswer::On;
	}

	[[nodiscard]] bool is_group(const PdfDict& dict)
	{
		const Held type = entry_of(dict, "Type");
		return type && type->as_name() == std::optional<std::string_view>("OCG");
	}

	// An `/OC` value's answer, kept once per page per object: an `/OCG` by its object, an `/OCMD` by its members, and
	// anything else (a direct `/OCG`, which has no identity, or a damaged value) no effect. Work not answered from the
	// memo is charged; a refusal, and a fatal failure kept in `resolve_failure_`, stop the page.
	[[nodiscard]] PdfResult<OcAnswer> evaluate_oc(const PdfValue& value)
	{
		const std::optional<IndirectRef> ref = value.as_ref();
		if (ref.has_value()) {
			if (const auto it = oc_answers_.find(object_key(*ref)); it != oc_answers_.end()) {
				return it->second;
			}
		}
		RECTO_PDF_RETURN_IF_ERROR(builder_.charge_work(1u));
		OcAnswer answer = OcAnswer::NoEffect;
		const Held target = shared(value);
		if (const PdfDict* dict = target ? target->as_dict() : nullptr) {
			const Held type = entry_of(*dict, "Type");
			const std::optional<std::string_view> kind = type ? type->as_name() : std::nullopt;
			if (kind == std::optional<std::string_view>("OCG")) {
				if (ref.has_value()) {
					answer = group_answer(*ref);
				}
			} else if (kind == std::optional<std::string_view>("OCMD")) {
				RECTO_PDF_TRY(answer, membership_answer(*dict));
			} else {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "An /OC value is neither an /OCG nor an /OCMD; it has no effect"); // recorded
			}
		}
		if (ref.has_value() && oc_answers_.size() < kMaxHeldResources) {
			oc_answers_.emplace(object_key(*ref), answer);
		}
		return answer;
	}

	// An `/OCMD` (Table 99): by `/VE` when it can be evaluated, else, as NOTE 2 gives them to readers that cannot use
	// it, by `/OCGs` and `/P`.
	[[nodiscard]] PdfResult<OcAnswer> membership_answer(const PdfDict& dict)
	{
		// A null `/VE`, direct or indirect, is absent (7.3.9), and only an array is an expression, not a lone group.
		if (const Held expression = entry_of(dict, "VE"); expression) {
			size_t nodes = 0;
			std::optional<OcAnswer> answer;
			if (expression->as_array() != nullptr) {
				RECTO_PDF_TRY(answer, expression_answer(*expression, 0u, nodes));
			}
			RECTO_PDF_RETURN_IF_ERROR(builder_.charge_work(nodes));
			if (answer.has_value()) {
				return *answer;
			}
			(void)make_pdf_error(ErrorCode::DamagedFormat, "An /OCMD's /VE cannot be evaluated; /OCGs and /P decide"); // recorded
		}
		const PdfValue* groups = dict.get("OCGs");
		if (groups == nullptr) {
			return OcAnswer::NoEffect;
		}
		size_t on = 0;
		size_t off = 0;
		const auto count = [&](const PdfValue& member) {
			const std::optional<IndirectRef> ref = member.as_ref();
			if (!ref.has_value()) {
				return; // null, or a direct dictionary, which has no identity
			}
			const Held group = shared(member);
			const PdfDict* entry = group ? group->as_dict() : nullptr;
			if (entry == nullptr || !is_group(*entry)) {
				return;
			}
			const OcAnswer state = group_answer(*ref);
			on += state == OcAnswer::On ? 1u : 0u;
			off += state == OcAnswer::Off ? 1u : 0u;
		};
		// An array comes from an indirect reference too; a reference to a group is the single dictionary form.
		const Held list = shared(*groups);
		if (const PdfArray* members = list ? list->as_array() : nullptr) {
			if (members->size() > kMaxOcGroupsListed) {
				(void)make_pdf_error(ErrorCode::ResourceLimit, "An /OCMD lists more /OCGs than its limit; it has no effect"); // recorded
				return OcAnswer::NoEffect;
			}
			RECTO_PDF_RETURN_IF_ERROR(builder_.charge_work(members->size()));
			for (const PdfValue& member : *members) {
				count(member);
			}
		} else {
			count(*groups);
		}
		if (on == 0u && off == 0u) {
			return OcAnswer::NoEffect; // Table 99: with no group left it has no effect
		}
		const Held policy_value = entry_of(dict, "P");
		const std::string_view policy = policy_value ? policy_value->as_name().value_or("AnyOn") : std::string_view("AnyOn");
		bool visible = on != 0u; // AnyOn, the default
		if (policy == "AllOn") {
			visible = off == 0u;
		} else if (policy == "AnyOff") {
			visible = off != 0u;
		} else if (policy == "AllOff") {
			visible = on == 0u;
		}
		return visible ? OcAnswer::On : OcAnswer::Off;
	}

	// A visibility expression (8.11.2.2), none when it cannot be evaluated: a value that is neither a group nor an
	// array, an unknown operator, the wrong operand count, or past its bounds, which also end a self-reference. An
	// operand with no effect drops out of `And` and `Or` and makes `Not` no effect, so an excluded group never decides.
	[[nodiscard]] PdfResult<std::optional<OcAnswer>> expression_answer(const PdfValue& value, size_t depth, size_t& nodes)
	{
		using Answer = std::optional<OcAnswer>;
		if (depth >= kMaxOcExpressionDepth || nodes >= kMaxOcExpressionNodes) {
			return Answer{};
		}
		++nodes;
		const Held node = shared(value);
		if (!node || node->is_null()) {
			return Answer{OcAnswer::NoEffect}; // null or deleted: ignored, as in `/OCGs`
		}
		if (const PdfDict* dict = node->as_dict()) {
			if (!is_group(*dict)) {
				return Answer{};
			}
			const std::optional<IndirectRef> ref = value.as_ref();
			return Answer{ref.has_value() ? group_answer(*ref) : OcAnswer::NoEffect};
		}
		const PdfArray* expression = node->as_array();
		if (expression == nullptr || expression->size() < 2u) {
			return Answer{};
		}
		const Held first = shared(*expression->begin());
		const std::optional<std::string_view> name = first ? first->as_name() : std::nullopt;
		const bool is_not = name == std::optional<std::string_view>("Not");
		const bool is_and = name == std::optional<std::string_view>("And");
		if (!is_not && !is_and && name != std::optional<std::string_view>("Or")) {
			return Answer{};
		}
		if (is_not && expression->size() != 2u) {
			return Answer{};
		}
		bool any_on = false;
		bool any_off = false;
		for (auto it = std::next(expression->begin()); it != expression->end(); ++it) {
			RECTO_PDF_TRY(const Answer operand, expression_answer(*it, depth + 1u, nodes));
			if (!operand.has_value()) {
				return Answer{};
			}
			any_on = any_on || *operand == OcAnswer::On;
			any_off = any_off || *operand == OcAnswer::Off;
		}
		if (!any_on && !any_off) {
			return Answer{OcAnswer::NoEffect};
		}
		if (is_not) {
			return Answer{any_on ? OcAnswer::Off : OcAnswer::On};
		}
		const bool truth = is_and ? !any_off : any_on;
		return Answer{truth ? OcAnswer::On : OcAnswer::Off};
	}

	// A dictionary entry, an indirect value held through `held_object`, never copied; an entry whose value is null is
	// absent (ISO 32000-1 7.3.9).
	[[nodiscard]] Held entry_of(const PdfDict& dict, std::string_view key)
	{
		const PdfValue* value = dict.get(key);
		if (value == nullptr) {
			return Held{};
		}
		Held entry = shared(*value);
		if (entry && entry->is_null()) {
			return Held{};
		}
		return entry;
	}

	// A form's `/Matrix` (identity when absent), or none when it is not six finite numbers or is singular.
	[[nodiscard]] std::optional<PdfMatrix> form_matrix(const PdfDict& dict)
	{
		const Held value = entry_of(dict, "Matrix");
		if (!value) {
			return PdfMatrix::identity();
		}
		const PdfArray* array = value->as_array();
		if (array == nullptr || array->size() != 6u) {
			return std::nullopt;
		}
		double m[6]{};
		for (size_t i = 0; i < 6u; ++i) {
			const Held n = shared((*array)[i]);
			const std::optional<double> number = n ? n->as_number() : std::nullopt;
			if (!number.has_value() || !std::isfinite(*number)) {
				return std::nullopt;
			}
			m[i] = *number;
		}
		const double det = m[0] * m[3] - m[1] * m[2];
		if (!std::isfinite(det) || det == 0.0) {
			return std::nullopt;
		}
		return PdfMatrix{m[0], m[1], m[2], m[3], m[4], m[5]};
	}

	// A form's `/BBox` (required, Table 95), or none when it is missing, not four finite numbers, or empty.
	[[nodiscard]] std::optional<PdfRect> form_bbox(const PdfDict& dict)
	{
		const Held value = entry_of(dict, "BBox");
		const PdfArray* array = value ? value->as_array() : nullptr;
		if (array == nullptr || array->size() != 4u) {
			return std::nullopt;
		}
		double v[4]{};
		for (size_t i = 0; i < 4u; ++i) {
			const Held n = shared((*array)[i]);
			const std::optional<double> number = n ? n->as_number() : std::nullopt;
			if (!number.has_value() || !std::isfinite(*number)) {
				return std::nullopt;
			}
			v[i] = *number;
		}
		const PdfRect box{v[0], v[1], v[2], v[3]}; // ordered by PdfRect
		if (!(box.x_max > box.x_min) || !(box.y_max > box.y_min)) {
			return std::nullopt;
		}
		return box;
	}

	// The scope a form draws in: its own `/Resources`, kept per page by that object (or the form's when the dictionary
	// is direct), else the page's (7.8.3). Past kMaxResourceScopes a scope is built for this one invocation.
	[[nodiscard]] ResourceScope* form_scope(const std::shared_ptr<const PdfValue>& form, uint64_t form_key,
		std::unique_ptr<ResourceScope>& transient)
	{
		const PdfDict& dict = form->as_stream()->dict;
		const PdfValue* entry = dict.get("Resources");
		if (entry == nullptr) {
			return scopes_.front().get();
		}
		const std::optional<IndirectRef> ref = entry->as_ref();
		const uint64_t key = ref.has_value() ? object_key(*ref) : (form_key | (uint64_t{1} << 63u));
		if (const auto it = scope_by_key_.find(key); it != scope_by_key_.end()) {
			return it->second;
		}
		// Shared, not copied: the scope keeps alive the object its dictionary lives in.
		std::shared_ptr<const PdfValue> owner = form;
		const PdfDict* resources = entry->as_dict();
		if (ref.has_value()) {
			owner = held_object(*ref);
			resources = owner != nullptr ? owner->as_dict() : nullptr;
		}
		if (resources == nullptr) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A form's /Resources is not a dictionary; it draws with the page's");
			return scopes_.front().get();
		}
		auto scope = std::make_unique<ResourceScope>();
		scope->serial = ++scope_serial_;
		scope->owner = std::move(owner);
		scope->resources = resources;
		ResourceScope* raw = scope.get();
		if (scopes_.size() < kMaxResourceScopes) {
			scope_by_key_.emplace(key, raw);
			scopes_.push_back(std::move(scope));
		} else {
			transient = std::move(scope);
		}
		return raw;
	}

	// Makes `scope` the current one, colour space names included.
	void use_scope(ResourceScope* scope)
	{
		scope_ = scope;
		if (spaces_.has_value()) {
			spaces_->set_resources(category("ColorSpace", scope_->color_spaces));
		}
	}

	// Whether every `/Filter` stage, references resolved, is one the decode work meter measures
	// (`filters_are_metered_lossless`): image filters never produce content, and their buffers are not metered.
	[[nodiscard]] bool metered_filters(const PdfDict& dict)
	{
		const Held filter = entry_of(dict, "Filter");
		if (!filter) {
			return true;
		}
		const auto metered = [](const Held& stage) {
			const std::optional<std::string_view> name = stage ? stage->as_name() : std::nullopt;
			return name.has_value() && filter_is_metered_lossless(*name);
		};
		if (const PdfArray* stages = filter->as_array()) {
			if (stages->size() > kMaxFilterChainDepth) {
				return false;
			}
			return std::all_of(stages->begin(), stages->end(), [&](const PdfValue& stage) { return metered(shared(stage)); });
		}
		return metered(filter);
	}

	// A form's decoded content: kept per form for the page, decoded at most once, each decode capped at what remains
	// of kMaxFormContentBytes. Null when it cannot be drawn (damaged, or past the budget), its diagnostic recorded and
	// the refusal remembered; OutOfMemory and other fatal errors are returned.
	[[nodiscard]] PdfResult<std::shared_ptr<const std::vector<uint8_t>>> form_content(const PdfStream& stream, uint64_t key)
	{
		if (key != 0u) {
			if (const auto it = form_contents_.find(key); it != form_contents_.end()) {
				return it->second;
			}
		}
		const auto remember = [&](std::shared_ptr<const std::vector<uint8_t>> content) {
			if (key != 0u) {
				form_contents_.emplace(key, content);
			}
			return content;
		};
		if (form_decoded_ >= kMaxFormContentBytes) {
			record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, form_decoded_,
				kMaxFormContentBytes, "Form XObject content past the page's budget is not drawn"));
			return remember(nullptr);
		}
		if (!metered_filters(stream.dict)) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A Form XObject's content names an image filter; it is not drawn"); // recorded
			return remember(nullptr);
		}
		// Every byte the decoders produce counts, kept or not: a stream that inflates and then fails its checksum
		// spends the budget too, so distinct damaged forms cannot each cost a full decode.
		detail::ScopedDecodeWorkMeter work;
		auto decoded = document_.decode_stream(stream, kMaxFormContentBytes - form_decoded_);
		const auto charge = [&](uint64_t bytes) {
			form_decoded_ = static_cast<size_t>(std::min<uint64_t>(kMaxFormContentBytes, uint64_t{form_decoded_} + bytes));
		};
		if (!decoded.has_value()) {
			const ErrorCode code = decoded.error().code;
			if (code == ErrorCode::OutOfMemory) {
				return decoded.error();
			}
			if (code == ErrorCode::DecompressionLimitExceeded) {
				form_decoded_ = kMaxFormContentBytes; // what it decoded used the rest of the budget
				record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, form_decoded_,
					kMaxFormContentBytes, "Form XObject content past the page's budget is not drawn"));
			} else {
				charge(work.bytes());
				record_diagnostic(decoded.error()); // a filter's error does not record itself
			}
			return remember(nullptr);
		}
		charge(std::max<uint64_t>(work.bytes(), decoded->size()));
		return remember(std::make_shared<const std::vector<uint8_t>>(std::move(*decoded)));
	}

	// Draws a Form XObject (8.10): in a saved state under its `/Matrix`, clipped to its `/BBox`, in its scope, its
	// content charged to the work budget. The form's content runs as a new frame of `interpret`; when it ends,
	// `leave_form` gives the caller back everything it left (saves, the clip, a path, a pending clip, the text
	// matrices).
	[[nodiscard]] PdfResult<void> draw_form(const XObjectEntry& form, uint64_t key)
	{
		const PdfStream& stream = *form.object->as_stream();
		const PdfMatrix* const matrix = &form.matrix;
		const PdfRect* const bbox = &form.bbox;
		if (key != 0u && std::find(form_stack_.begin(), form_stack_.end(), key) != form_stack_.end()) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A Form XObject draws itself; the repeat is not drawn"); // recorded
			return {};
		}
		if (form_stack_.size() >= kMaxFormDepth) {
			record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::FormDepth, form_stack_.size() + 1u,
				kMaxFormDepth, "Form XObjects nested past the depth limit are not drawn"));
			return {};
		}
		if (state_stack_.size() >= max_depth_) {
			(void)make_pdf_error(ErrorCode::ResourceLimit, "No graphics state save is left for a Form XObject; it is not drawn"); // recorded
			return {};
		}
		RECTO_PDF_TRY(std::shared_ptr<const std::vector<uint8_t>> content, form_content(stream, key));
		if (content == nullptr) {
			return {};
		}
		RECTO_PDF_RETURN_IF_ERROR(builder_.charge_work(content->size()));

		// The caller's state, set aside and given back whatever the form does.
		RECTO_PDF_RETURN_IF_ERROR(builder_.save_state());
		state_stack_.push_back(state_);
		auto exit = std::make_unique<FormExit>();
		exit->floor = state_stack_.size();
		exit->caller_floor = std::exchange(save_floor_, exit->floor);
		exit->caller_ignored = std::exchange(ignored_saves_, size_t{0});
		exit->caller_skip = std::exchange(skip_next_, false);
		exit->marked_depth = marked_depth_;
		exit->text = text_matrix_;
		exit->line = line_matrix_;
		exit->path = std::move(path_);
		path_ = PathBuilder{};
		exit->current = std::exchange(current_, std::nullopt);
		exit->start = std::exchange(subpath_start_, std::nullopt);
		exit->clip = std::exchange(pending_clip_, std::nullopt);
		exit->closed = std::exchange(closed_, false);
		exit->scope = scope_;
		form_stack_.push_back(key);

		auto entered = [&]() -> PdfResult<void> {
			RECTO_PDF_RETURN_IF_ERROR(builder_.concat_matrix(*matrix));
			PathBuilder box;
			box.rect(bbox->x_min, bbox->y_min, bbox->x_max - bbox->x_min, bbox->y_max - bbox->y_min);
			RECTO_PDF_TRY(const uint32_t id, builder_.add_path(box));
			return builder_.push_clip_path(id, FillRule::NonZero);
		}();
		if (!entered.has_value()) {
			auto left = leave_form(*exit);
			if (entered.error().code == ErrorCode::DamagedFormat) {
				return left; // a matrix or box the builder cannot place: the form is skipped like a malformed operator
			}
			return entered;
		}
		use_scope(form_scope(form.object, key, exit->transient));
		auto parser = std::make_unique<Parser>(std::span<const uint8_t>(*content));
		frames_.push_back(Frame{std::move(content), std::move(parser), {}, std::move(exit)});
		return {};
	}

	// Gives a form's caller back its state (8.10.1: the graphics state as before `Do`), whatever the form left.
	[[nodiscard]] PdfResult<void> leave_form(FormExit& exit)
	{
		use_scope(exit.scope);
		form_stack_.pop_back();
		while (state_stack_.size() > exit.floor) {
			(void)builder_.restore_state();
			state_stack_.pop_back();
		}
		auto restored = builder_.restore_state();
		state_ = std::move(state_stack_.back());
		state_stack_.pop_back();
		save_floor_ = exit.caller_floor;
		ignored_saves_ = exit.caller_ignored;
		skip_next_ = exit.caller_skip; // an operator the form's damaged tail would skip is the form's, not the caller's
		// A form is drawn only outside hidden content, so a block it left open ends with it and nothing else hides.
		marked_depth_ = exit.marked_depth;
		hidden_at_ = 0u;
		text_matrix_ = exit.text;
		line_matrix_ = exit.line;
		path_ = std::move(exit.path);
		current_ = exit.current;
		subpath_start_ = exit.start;
		pending_clip_ = exit.clip;
		closed_ = exit.closed;
		return restored;
	}

	static constexpr size_t kMaxCachedFonts = 4'096u;
	static constexpr size_t kMaxFormDepth = 32u;
	static constexpr size_t kMaxResourceScopes = 256u;
	static constexpr size_t kMaxHeldResourceBytes = size_t{64} << 20u; // resource objects one page holds
	static constexpr size_t kMaxHeldResources = 65'536u;
	static constexpr size_t kMaxFormContentBytes = size_t{64} << 20u;
	static constexpr size_t kMaxTextFontBytes = size_t{64} << 20u;
	static constexpr size_t kMaxInkMemo = 65'536u;
	static constexpr size_t kMaxImageArrayEntries = 64u;             // two numbers for each of 32 components
	static constexpr size_t kMaxImageConversions = 8u;               // kept per image; more are made as they are drawn
	static constexpr size_t kMaxOcExpressionDepth = 32u;
	static constexpr size_t kMaxOcExpressionNodes = 4'096u;
	static constexpr size_t kMaxOcGroupsListed = 65'536u;

	const Document& document_;
	DisplayListBuilder builder_;
	// Optional content (D01 T02 section 47): whether groups are used at all, their states, each object's answer for
	// the page, and marked-content nesting.
	bool uses_groups_;
	std::vector<uint64_t> off_groups_{};
	std::vector<uint64_t> excluded_groups_{};
	std::unordered_map<uint64_t, OcAnswer> oc_answers_{};
	// Image XObjects (D01 T02 section 44): each object's source and conversions, what they hold and what has been decoded.
	std::unordered_map<uint64_t, ImageSource> image_sources_{};
	std::unordered_map<const uint8_t*, InlineImage> inline_images_{}; // by the address of the data, in content held for the page
	size_t image_held_{0};
	size_t image_decoded_{0};
	const size_t image_budget_{page_image_budget}; // bitmaps held and data decoded, each, per page
	size_t marked_depth_{0};
	size_t hidden_at_{0};
	uint64_t scope_serial_{0};
	std::vector<std::unique_ptr<ResourceScope>> scopes_{};
	std::unordered_map<uint64_t, ResourceScope*> scope_by_key_{};
	ResourceScope* scope_{nullptr};
	// Form XObjects (D01 T02 section 46): the forms being drawn (by object, for cycles), the decoded content kept per
	// form for the page (null for one whose decode failed, never decoded again), the bytes decoded so far against
	// kMaxFormContentBytes, and the state-stack floor a form's own `Q` cannot pop below.
	std::deque<Frame> frames_{};
	// The resource objects this page holds, by object (null for one missing or refused), and their heap.
	std::unordered_map<uint64_t, std::shared_ptr<const PdfValue>> held_{};
	size_t held_bytes_{0};
	// The XObjects this page has met, by object, and the one form met past kMaxHeldResources entries.
	std::unordered_map<uint64_t, XObjectEntry> xobject_entries_{};
	XObjectEntry transient_entry_{};
	std::vector<uint64_t> form_stack_{};
	std::unordered_map<uint64_t, std::shared_ptr<const std::vector<uint8_t>>> form_contents_{};
	size_t form_decoded_{0};
	size_t save_floor_{0};
	std::unordered_map<uint64_t, std::shared_ptr<const TextFont>> font_objects_{};
	std::shared_ptr<const TextFont> fallback_{};
	// Keyed by the face's control block (a weak reference, so a face freed during the page is never mistaken for a
	// later one at the same address) and the glyph.
	struct InkKey {
		std::weak_ptr<const LoadedFont> face;
		uint16_t glyph{0};
	};
	struct InkKeyLess {
		bool operator()(const InkKey& a, const InkKey& b) const noexcept
		{
			if (a.face.owner_before(b.face)) {
				return true;
			}
			if (b.face.owner_before(a.face)) {
				return false;
			}
			return a.glyph < b.glyph;
		}
	};
	std::map<InkKey, std::pair<std::optional<PdfRect>, bool>, InkKeyLess> ink_memo_{};
	size_t fonts_resolved_{0};
	size_t font_bytes_{0};
	Type0Memo type0_memo_{}; // the page's built CIDFonts and encoding CMaps (D01 T02 section 48), bounded by fonts_resolved_
	PdfMatrix text_matrix_{};
	PdfMatrix line_matrix_{};
	std::optional<ColorSpaces> spaces_{};
	std::optional<Converted> last_stroke_{};
	std::optional<Converted> last_fill_{};
	GraphicsState state_{};
	size_t max_depth_;
	std::vector<GraphicsState> state_stack_{};
	size_t ignored_saves_{0};
	PathBuilder path_{};
	std::optional<PdfPoint> current_{};
	std::optional<PdfPoint> subpath_start_{};
	std::optional<FillRule> pending_clip_{};
	std::optional<PdfError> resolve_failure_{};
	bool closed_{false};
	bool skip_next_{false};
};

} // namespace

namespace detail {

ScopedPageImageBudgetForTesting::ScopedPageImageBudgetForTesting(size_t bytes) noexcept
	: previous_(std::exchange(page_image_budget, bytes))
{
}

ScopedPageImageBudgetForTesting::~ScopedPageImageBudgetForTesting() noexcept
{
	page_image_budget = previous_;
}

} // namespace detail

PdfResult<std::shared_ptr<const DisplayList>> interpret_content(
	const Document& document,
	std::span<const uint8_t> content,
	const PdfDict& resources,
	const PdfRect& page_box,
	const DisplayListLimits& limits,
	const OptionalContentVisibility* visibility) noexcept
{
	try {
		Interpreter interpreter(document, resources, page_box, limits, visibility);
		return interpreter.run(content);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory interpreting a content stream");
	}
}

PdfResult<std::shared_ptr<const DisplayList>> interpret_page(
	const Document& document,
	size_t page_index,
	const DisplayListLimits& limits,
	const OptionalContentVisibility* visibility) noexcept
{
	if (page_index >= document.page_count()) {
		// A caller's mistake, not a damaged file: recorded here, as InvalidArgument refusals are (error.h).
		PdfError err = make_refusal_error(ErrorCode::InvalidArgument, RefusalReason::PageIndex, page_index,
			document.page_count() == 0u ? 0u : document.page_count() - 1u, "Page index past the document's last page");
		record_diagnostic(err);
		return err;
	}
	RECTO_PDF_TRY(const PageDescription page, document.page_description(page_index));
	RECTO_PDF_TRY(const std::vector<uint8_t> content, document.decoded_page_contents(page_index));
	RECTO_PDF_TRY(const PdfDict resources, document.page_resources(page_index));
	return interpret_content(document, content, resources, page.crop_box, limits, visibility);
}

} // namespace recto::pdf
