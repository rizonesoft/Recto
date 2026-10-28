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
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {

namespace {

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
};

// Sorted by name (byte order) for the binary search in `op_named`.
inline constexpr std::array<std::pair<std::string_view, Op>, 60> kOperators = {{
	{"\"", Op::NextLineShowSpaced},
	{"'", Op::NextLineShow},
	{"B", Op::FillStroke},
	{"B*", Op::FillStrokeEvenOdd},
	{"BI", Op::BeginInlineImage},
	{"BT", Op::BeginText},
	{"CS", Op::StrokeSpace},
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

class Interpreter {
public:
	Interpreter(const Document& document, const PdfDict& resources, const PdfRect& page_box, const DisplayListLimits& limits)
		: document_(document)
		, resources_(resources)
		, builder_(page_box, limits)
		, max_depth_(std::min(kMaxGraphicsStateDepth, limits.max_save_depth))
	{
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
	// Damaged content (`DamagedFormat`) skips the operator it belongs to; every other lexer, parser or resolver failure
	// (memory, a decoder or recursion limit, an internal error) stops the page and is returned.
	[[nodiscard]] static bool fatal(const PdfError& error) noexcept
	{
		return error.code != ErrorCode::DamagedFormat;
	}

	[[nodiscard]] PdfResult<void> interpret(std::span<const uint8_t> content)
	{
		Parser parser(content);
		Lexer& lexer = parser.lexer();
		std::vector<PdfValue> operands;
		for (;;) {
			const size_t before = lexer.position();
			auto peeked = lexer.peek_token();
			if (!peeked.has_value()) {
				if (fatal(peeked.error())) {
					return peeked.error();
				}
				// A token the lexer cannot read: the operator it belongs to is skipped, and reading resumes past it.
				drop_operands(operands);
				resync(lexer, before);
				continue;
			}
			if (peeked->kind == TokenKind::Eof) {
				break;
			}
			if (peeked->kind == TokenKind::Keyword) {
				auto op = lexer.next_token();
				if (!op.has_value()) {
					if (fatal(op.error())) {
						return op.error();
					}
					drop_operands(operands);
					resync(lexer, before);
					continue;
				}
				const Op code = op_named(op->text.view());
				if (code == Op::BeginInlineImage) {
					auto skipped = skip_inline_image(parser);
					if (resolve_failure_.has_value()) {
						return *resolve_failure_; // a colour space the image named did not resolve, the cause of any refusal
					}
					RECTO_PDF_RETURN_IF_ERROR(skipped);
					operands.clear();
					skip_next_ = false;
					continue;
				}
				RECTO_PDF_RETURN_IF_ERROR(dispatch(code, operands));
				operands.clear();
				continue;
			}
			auto operand = parser.parse_direct_object();
			if (!operand.has_value()) {
				if (fatal(operand.error())) {
					return operand.error();
				}
				drop_operands(operands);
				resync(lexer, before);
				continue;
			}
			if (operands.size() >= kMaxContentOperands) {
				drop_operands(operands);
				continue;
			}
			operands.push_back(std::move(*operand));
		}
		return {};
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
				if (const auto named = resource("ColorSpace", color_spaces_, *n)) {
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

	// Inline images are read by section 4; until then `BI ... ID data EI` is skipped whole, so the data is never read
	// as operators. The dictionary is parsed to find `ID`. An unfiltered image's data is skipped by its length. A
	// filtered one ends at the first `EI` before which the data decodes through its filters to the image's full size:
	// an `EI` inside compressed data truncates it, so it cannot. Checking candidates may decode at most 64 MiB in all;
	// past that the page is refused (`InlineImageFraming`), and a candidate the decoder refuses for any reason but
	// damage refuses the page with that error. An image whose end cannot be verified this way (its size unknown, its
	// data damaged) refuses the page with `DamagedFormat`: no `EI` is ever taken on trust.
	[[nodiscard]] PdfResult<void> skip_inline_image(Parser& parser)
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
					lexer.seek(after + 2u);
					return {};
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
					lexer.seek(end + 2u);
					return {};
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
		if (skip_next_) {
			skip_next_ = false;
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
		// Graphics state.
		case Op::Save: {
			return save();
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
			in_text_ = true;
			text_matrix_ = line_matrix_ = PdfMatrix::identity();
			return {};
		case Op::EndText:
			in_text_ = false;
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
		if (state_stack_.empty()) {
			return {}; // an unmatched Q in a sloppy file is ignored
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
	struct Category {
		bool resolved{false};
		std::optional<PdfDict> dict{};
	};

	[[nodiscard]] const PdfDict* category(std::string_view name, Category& slot)
	{
		if (!slot.resolved) {
			slot.resolved = true;
			if (const PdfValue* value = resources_.get(name)) {
				if (auto r = resolved(*value); r.has_value() && r->as_dict() != nullptr) {
					slot.dict = *r->as_dict();
				}
			}
		}
		return slot.dict.has_value() ? &*slot.dict : nullptr;
	}

	// The resource named `name` in `slot`'s category, resolved, or none.
	[[nodiscard]] std::optional<PdfValue> resource(std::string_view category_name, Category& slot, std::string_view name)
	{
		const PdfDict* dict = category(category_name, slot);
		if (dict == nullptr) {
			return std::nullopt;
		}
		const PdfValue* entry = dict->get(name);
		if (entry == nullptr) {
			return std::nullopt;
		}
		return resolved(*entry);
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
		const auto value = resource("ExtGState", ext_gstates_, name);
		if (!value.has_value() || value->as_dict() == nullptr) {
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
			spaces_.emplace(document_, category("ColorSpace", color_spaces_));
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
		// A side whose colour marks nothing (a `/None` colorant, ISO 32000-1 8.6.6.4) is not painted.
		fill = fill && state_.fill.paints();
		stroke = stroke && state_.stroke.paints();
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
		for (const auto& [key, font] : fonts_) {
			if (key == name) {
				return font;
			}
		}
		const PdfDict* fonts = category("Font", font_resources_);
		const PdfValue* entry_value = fonts != nullptr ? fonts->get(name) : nullptr;
		std::shared_ptr<const TextFont> font;
		if (entry_value == nullptr) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "Tf names a font the resources do not hold"); // recorded
			RECTO_PDF_TRY(font, TextFont::fallback());
		} else {
			RECTO_PDF_TRY(font, font_value(*entry_value));
		}
		if (fonts_.size() < kMaxCachedFonts) {
			fonts_.emplace_back(std::string(name), font);
		}
		return font;
	}

	// The font a font value (a reference or a dictionary) gives, an indirect one resolved once per interpretation.
	[[nodiscard]] PdfResult<std::shared_ptr<const TextFont>> font_value(const PdfValue& value)
	{
		const std::optional<IndirectRef> ref = value.as_ref();
		if (ref.has_value()) {
			for (const auto& [key, font] : font_objects_) {
				if (key == object_key(*ref)) {
					return font;
				}
			}
		}
		// Each page resolves at most kMaxCachedFonts fonts within kMaxTextFontBytes: past either, a font takes the
		// fallback unresolved, so a page of many distinct fonts never rebuilds or holds them without bound.
		if (fonts_resolved_ >= kMaxCachedFonts || font_bytes_ >= kMaxTextFontBytes) {
			record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::WorkingBytes, font_bytes_,
				kMaxTextFontBytes, "Fonts past the page's limit; drawn with the fallback"));
			return TextFont::fallback();
		}
		++fonts_resolved_;
		RECTO_PDF_TRY(std::shared_ptr<const TextFont> font, TextFont::resolve(document_, value));
		font_bytes_ += font->memory_bytes();
		if (ref.has_value()) {
			font_objects_.emplace_back(object_key(*ref), font);
		}
		return font;
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
		std::shared_ptr<const TextFont> font = state_.font;
		if (font == nullptr) {
			RECTO_PDF_TRY(font, TextFont::fallback());
		}
		const double size = state_.font_size;
		const bool vertical = font->vertical();
		std::vector<PositionedGlyph> glyphs;
		double x = 0.0;
		double y = 0.0;
		// A run that could not be recorded is refused before it is built, so a long string never allocates past the
		// list's glyph or byte limit; every code takes at least a byte, so the reservation is exact and never doubles.
		const size_t room = builder_.glyph_room();
		try {
			glyphs.reserve(std::min(bytes.size(), room));
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
			PositionedGlyph glyph;
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
		const TextRenderMode mode = painting_mode(state_.text_mode, state_.fill.paints(), state_.stroke.paints());
		// A font whose box promises nothing takes one a thousand ems wide, which holds any glyph a file draws.
		const PdfRect box = font->bbox_known() ? font->bbox() : PdfRect{-1000.0, -1000.0, 1000.0, 1000.0};
		const double bx0 = std::min(box.x_min * size, box.x_max * size);
		const double bx1 = std::max(box.x_min * size, box.x_max * size);
		const double by0 = std::min(box.y_min * size, box.y_max * size);
		const double by1 = std::max(box.y_min * size, box.y_max * size);
		PdfRect bounds{glyphs.front().origin_pt.x + bx0, glyphs.front().origin_pt.y + by0,
			glyphs.front().origin_pt.x + bx1, glyphs.front().origin_pt.y + by1};
		for (const PositionedGlyph& glyph : glyphs) {
			bounds.x_min = std::min(bounds.x_min, glyph.origin_pt.x + bx0);
			bounds.y_min = std::min(bounds.y_min, glyph.origin_pt.y + by0);
			bounds.x_max = std::max(bounds.x_max, glyph.origin_pt.x + bx1);
			bounds.y_max = std::max(bounds.y_max, glyph.origin_pt.y + by1);
		}
		RECTO_PDF_RETURN_IF_ERROR(sync_state(mode_strokes(mode)));
		RECTO_PDF_TRY(const ColorRgba fill_color, rgba(state_.fill, false));
		RECTO_PDF_TRY(const ColorRgba stroke_color, rgba(state_.stroke, true));
		auto drawn = builder_.draw_glyph_run(glyphs, size, mode, fill_color, stroke_color, run_matrix, bounds);
		// The text matrix advances whether or not the builder kept the run (a run it refuses is skipped like any
		// malformed operator), so the text after it stays where the file put it.
		advance_text(vertical ? 0.0 : x * state_.horizontal_scaling, vertical ? y : 0.0);
		return drawn;
	}

	static constexpr size_t kMaxCachedFonts = 4'096u;
	static constexpr size_t kMaxTextFontBytes = size_t{64} << 20u;

	const Document& document_;
	const PdfDict& resources_;
	DisplayListBuilder builder_;
	Category ext_gstates_{};
	Category color_spaces_{};
	Category font_resources_{};
	std::vector<std::pair<std::string, std::shared_ptr<const TextFont>>> fonts_{};
	std::vector<std::pair<uint64_t, std::shared_ptr<const TextFont>>> font_objects_{};
	size_t fonts_resolved_{0};
	size_t font_bytes_{0};
	PdfMatrix text_matrix_{};
	PdfMatrix line_matrix_{};
	bool in_text_{false};
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

PdfResult<std::shared_ptr<const DisplayList>> interpret_content(
	const Document& document,
	std::span<const uint8_t> content,
	const PdfDict& resources,
	const PdfRect& page_box,
	const DisplayListLimits& limits) noexcept
{
	try {
		Interpreter interpreter(document, resources, page_box, limits);
		return interpreter.run(content);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory interpreting a content stream");
	}
}

PdfResult<std::shared_ptr<const DisplayList>> interpret_page(
	const Document& document,
	size_t page_index,
	const DisplayListLimits& limits) noexcept
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
	return interpret_content(document, content, resources, page.crop_box, limits);
}

} // namespace recto::pdf
