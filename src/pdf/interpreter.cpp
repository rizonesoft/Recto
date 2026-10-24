// The content-stream interpreter: graphics state, paths, painting, clipping
// and device colour (ISO 32000-1 sections 7.8 and 8; D01 T02 section 3).
#include "interpreter.h"

#include "lexer.h"
#include "parser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {

namespace {

// The colour spaces this section reads; `Other` is any space sections 32 and 33 read, which paints black until then.
enum class ColorFamily : uint8_t {
	Gray,
	Rgb,
	Cmyk,
	Other,
};

[[nodiscard]] constexpr size_t component_count(ColorFamily family) noexcept
{
	switch (family) {
	case ColorFamily::Gray:
		return 1u;
	case ColorFamily::Rgb:
		return 3u;
	case ColorFamily::Cmyk:
		return 4u;
	case ColorFamily::Other:
		return 0u;
	}
	return 0u;
}

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

// Device components to the recorded sRGB colour. DeviceCMYK uses the conversion ISO 32000-1 gives for DeviceCMYK to
// DeviceRGB when no profile applies (each channel 1 - min(1, colourant + black)); section 32 calibrates it.
[[nodiscard]] ColorRgba to_rgba(ColorFamily family, std::span<const double> c, float alpha) noexcept
{
	switch (family) {
	case ColorFamily::Gray:
		return ColorRgba{unit(c[0]), unit(c[0]), unit(c[0]), alpha};
	case ColorFamily::Rgb:
		return ColorRgba{unit(c[0]), unit(c[1]), unit(c[2]), alpha};
	case ColorFamily::Cmyk: {
		const double k = std::clamp(c[3], 0.0, 1.0);
		return ColorRgba{unit(1.0 - std::min(1.0, std::clamp(c[0], 0.0, 1.0) + k)),
			unit(1.0 - std::min(1.0, std::clamp(c[1], 0.0, 1.0) + k)),
			unit(1.0 - std::min(1.0, std::clamp(c[2], 0.0, 1.0) + k)), alpha};
	}
	case ColorFamily::Other:
		break;
	}
	return ColorRgba{0.0f, 0.0f, 0.0f, alpha};
}

// One colour: its space and components, black in the space's initial colour.
struct Color {
	ColorFamily family{ColorFamily::Gray};
	std::array<double, 4> components{0.0, 0.0, 0.0, 0.0};

	[[nodiscard]] static Color initial(ColorFamily family) noexcept
	{
		Color c{};
		c.family = family;
		if (family == ColorFamily::Cmyk) {
			c.components = {0.0, 0.0, 0.0, 1.0}; // ISO 32000-1 Table 74: CMYK starts at black
		}
		return c;
	}

	[[nodiscard]] ColorRgba rgba() const noexcept
	{
		return to_rgba(family, components, 1.0f);
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
	std::string rendering_intent{"RelativeColorimetric"};
	double flatness{1.0};
	bool style_recorded{true};
	bool alpha_recorded{true};
};

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
};

// Sorted by name (byte order) for the binary search in `op_named`.
inline constexpr std::array<std::pair<std::string_view, Op>, 43> kOperators = {{
	{"B", Op::FillStroke},
	{"B*", Op::FillStrokeEvenOdd},
	{"BI", Op::BeginInlineImage},
	{"CS", Op::StrokeSpace},
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
	Interpreter(const Document& document, const PdfDict& resources, const PdfRect& page_box, const DisplayListLimits& limits) noexcept
		: document_(document)
		, resources_(resources)
		, builder_(page_box, limits)
		, max_depth_(std::min(kMaxGraphicsStateDepth, limits.max_save_depth))
	{
	}

	[[nodiscard]] PdfResult<std::shared_ptr<const DisplayList>> run(std::span<const uint8_t> content) noexcept
	{
		Parser parser(content);
		Lexer& lexer = parser.lexer();
		std::vector<PdfValue> operands;
		for (;;) {
			const size_t before = lexer.position();
			auto peeked = lexer.peek_token();
			if (!peeked.has_value()) {
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
					drop_operands(operands);
					resync(lexer, before);
					continue;
				}
				const Op code = op_named(op->text.view());
				if (code == Op::BeginInlineImage) {
					skip_inline_image(lexer);
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
		return builder_.finish();
	}

private:
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

	// Inline images belong to section 4; until then their data is skipped to the `EI` that ends it, so its bytes are
	// never read as operators.
	static void skip_inline_image(Lexer& lexer) noexcept
	{
		const std::span<const uint8_t> input = lexer.input();
		size_t at = lexer.position();
		const auto is_space = [](uint8_t b) noexcept {
			return b == 0x20u || b == 0x0Au || b == 0x0Du || b == 0x09u || b == 0x0Cu || b == 0x00u;
		};
		// Past "ID" and its single whitespace byte.
		while (at + 2u <= input.size() && !(input[at] == 'I' && input[at + 1u] == 'D' &&
				   (at + 2u == input.size() || is_space(input[at + 2u])))) {
			++at;
		}
		at += 3u;
		while (at + 2u <= input.size()) {
			if (input[at] == 'E' && input[at + 1u] == 'I' && (at == 0u || is_space(input[at - 1u])) &&
				(at + 2u == input.size() || is_space(input[at + 2u]))) {
				lexer.seek(at + 2u);
				return;
			}
			++at;
		}
		lexer.seek(input.size());
	}

	[[nodiscard]] static std::optional<double> number(const PdfValue& v) noexcept
	{
		const auto n = v.as_number();
		if (!n.has_value() || !std::isfinite(*n)) {
			return std::nullopt;
		}
		return n;
	}

	// The operands as `count` finite numbers, or none when they are not exactly that.
	[[nodiscard]] static std::optional<std::array<double, 6>> numbers(const std::vector<PdfValue>& operands, size_t count) noexcept
	{
		if (operands.size() != count) {
			return std::nullopt;
		}
		std::array<double, 6> out{};
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

	[[nodiscard]] PdfResult<void> dispatch_operator(Op op, const std::vector<PdfValue>& operands)
	{
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
					state_.rendering_intent = std::string(*name);
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
				after_rect_ = false;
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
				after_rect_ = true;
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
			set_color(false, ColorFamily::Gray, operands);
			return {};
		case Op::StrokeGray:
			set_color(true, ColorFamily::Gray, operands);
			return {};
		case Op::FillRgb:
			set_color(false, ColorFamily::Rgb, operands);
			return {};
		case Op::StrokeRgb:
			set_color(true, ColorFamily::Rgb, operands);
			return {};
		case Op::FillCmyk:
			set_color(false, ColorFamily::Cmyk, operands);
			return {};
		case Op::StrokeCmyk:
			set_color(true, ColorFamily::Cmyk, operands);
			return {};
		case Op::FillSpace:
		case Op::StrokeSpace: {
			if (operands.size() == 1u) {
				if (const auto name = operands[0].as_name()) {
					Color& target = op == Op::StrokeSpace ? state_.stroke : state_.fill;
					target = Color::initial(color_space_named(*name));
				}
			}
			return {};
		}
		case Op::FillColor:
		case Op::StrokeColor: {
			const bool stroking = op == Op::StrokeColor;
			Color& target = stroking ? state_.stroke : state_.fill;
			if (target.family != ColorFamily::Other) {
				set_color(stroking, target.family, operands);
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
				if (auto resolved = document_.resolve_value(*value); resolved.has_value() && resolved->as_dict() != nullptr) {
					slot.dict = *resolved->as_dict();
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
		auto resolved = document_.resolve_value(*entry);
		if (!resolved.has_value()) {
			return std::nullopt;
		}
		return std::move(*resolved);
	}

	// A named value inside an `/ExtGState` dictionary, resolved.
	[[nodiscard]] std::optional<PdfValue> entry(const PdfDict& dict, std::string_view key) const
	{
		const PdfValue* v = dict.get(key);
		if (v == nullptr) {
			return std::nullopt;
		}
		auto resolved = document_.resolve_value(*v);
		if (!resolved.has_value()) {
			return std::nullopt;
		}
		return std::move(*resolved);
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
			if (const PdfArray* pair = d->as_array(); pair != nullptr && pair->size() == 2u) {
				set_dash((*pair)[0], (*pair)[1]);
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
					if (const auto n = item.as_name(); n.has_value() && (mode = blend_mode_named(*n)).has_value()) {
						break;
					}
				}
			}
			state_.blend_mode = mode.value_or(BlendMode::Normal);
			state_.alpha_recorded = false;
		}
	}

	// A colour space name: a device space, or a `/ColorSpace` resource naming one (through inherited resources and
	// indirect references); anything else is a space sections 32 and 33 read.
	[[nodiscard]] ColorFamily color_space_named(std::string_view name)
	{
		const auto device = [](std::string_view n) -> std::optional<ColorFamily> {
			if (n == "DeviceGray") {
				return ColorFamily::Gray;
			}
			if (n == "DeviceRGB") {
				return ColorFamily::Rgb;
			}
			if (n == "DeviceCMYK") {
				return ColorFamily::Cmyk;
			}
			return std::nullopt;
		};
		if (const auto family = device(name)) {
			return *family;
		}
		const auto value = resource("ColorSpace", color_spaces_, name);
		if (!value.has_value()) {
			return ColorFamily::Other;
		}
		if (const auto n = value->as_name()) {
			return device(*n).value_or(ColorFamily::Other);
		}
		if (const PdfArray* array = value->as_array(); array != nullptr && array->size() == 1u) {
			if (const auto n = (*array)[0].as_name()) {
				return device(*n).value_or(ColorFamily::Other);
			}
		}
		return ColorFamily::Other;
	}

	void set_color(bool stroking, ColorFamily family, const std::vector<PdfValue>& operands) noexcept
	{
		const size_t count = component_count(family);
		const auto n = numbers(operands, count);
		if (!n.has_value()) {
			return;
		}
		Color& target = stroking ? state_.stroke : state_.fill;
		target.family = family;
		for (size_t i = 0; i < count; ++i) {
			target.components[i] = (*n)[i];
		}
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
	// A segment after `re` starts a new subpath at the rectangle's origin, so the recorded path says where it starts.
	void begin_segment()
	{
		if (after_rect_) {
			path_.move_to(*current_);
			after_rect_ = false;
		}
	}

	// `h` and the closing painting operators; a rectangle is already closed.
	void close_subpath()
	{
		if (current_.has_value() && !after_rect_) {
			path_.close_subpath();
			current_ = subpath_start_;
		}
	}

	[[nodiscard]] PdfResult<void> paint(bool fill, bool stroke, FillRule rule, bool close)
	{
		if (close) {
			close_subpath();
		}
		const std::optional<FillRule> clip = pending_clip_;
		pending_clip_.reset();
		auto done = path_.empty() ? PdfResult<void>{} : record_paint(fill, stroke, rule, clip);
		end_path(); // the path ends with its painting operator, recorded or refused
		return done;
	}

	[[nodiscard]] PdfResult<void> record_paint(bool fill, bool stroke, FillRule rule, std::optional<FillRule> clip)
	{
		RECTO_PDF_TRY(const uint32_t id, builder_.add_path(path_));
		if (fill || stroke) {
			RECTO_PDF_RETURN_IF_ERROR(sync_state(stroke));
			RECTO_PDF_RETURN_IF_ERROR(builder_.draw_path(id, state_.stroke.rgba(), state_.fill.rgba(), stroke, fill, rule));
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
		after_rect_ = false;
	}

	const Document& document_;
	const PdfDict& resources_;
	DisplayListBuilder builder_;
	Category ext_gstates_{};
	Category color_spaces_{};
	GraphicsState state_{};
	size_t max_depth_;
	std::vector<GraphicsState> state_stack_{};
	size_t ignored_saves_{0};
	PathBuilder path_{};
	std::optional<PdfPoint> current_{};
	std::optional<PdfPoint> subpath_start_{};
	std::optional<FillRule> pending_clip_{};
	bool after_rect_{false};
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
