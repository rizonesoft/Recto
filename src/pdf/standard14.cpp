// The Standard 14 fonts' metrics and names (ISO 32000-1 section 9.6.2.2; D01 T02 section 27): Adobe's Core14 AFM
// files as constexpr tables, glyph lookup by name and by built-in code, and the `/BaseFont` names that mean each font.
// The faces drawn with are loaded in font.cpp, beside the DirectWrite factory they share.
#include "font.h"

#include <algorithm>

namespace recto::pdf {

namespace {

#include "standard14_metrics.inc"

static_assert(kStandard14Metrics.size() == kStandard14FontCount);

// A subset font's six capital letters and `+` (ISO 32000-1 section 9.6.4), which name the subset, not the font.
[[nodiscard]] constexpr std::string_view without_subset_prefix(std::string_view name) noexcept
{
	if (name.size() > 7u && name[6] == '+' &&
		std::all_of(name.begin(), name.begin() + 6, [](char c) { return c >= 'A' && c <= 'Z'; })) {
		name.remove_prefix(7u);
	}
	return name;
}

[[nodiscard]] constexpr std::string_view without_suffix(std::string_view text, std::string_view suffix) noexcept
{
	return text.ends_with(suffix) ? text.substr(0, text.size() - suffix.size()) : text;
}

enum class Family : uint8_t { Courier, Helvetica, Times };

// The family names producers write for the three Latin families (Windows' metric-compatible faces included), after any
// `PS` and `MT` suffix.
[[nodiscard]] constexpr std::optional<Family> family_of(std::string_view name) noexcept
{
	name = without_suffix(without_suffix(name, "MT"), "PS");
	if (name == "Courier" || name == "CourierNew") {
		return Family::Courier;
	}
	if (name == "Helvetica" || name == "Arial") {
		return Family::Helvetica;
	}
	if (name == "Times" || name == "Times-Roman" || name == "TimesNewRoman") {
		return Family::Times;
	}
	return std::nullopt;
}

struct Style {
	bool bold = false;
	bool italic = false;
};

// A style suffix (after any `MT`): plain, `Bold`, `Italic` or `Oblique`, or both; anything else (`Narrow`, `Black`)
// is another font.
[[nodiscard]] constexpr std::optional<Style> style_of(std::string_view name) noexcept
{
	name = without_suffix(name, "MT");
	if (name.empty() || name == "Regular" || name == "Roman") {
		return Style{};
	}
	if (name == "Bold") {
		return Style{true, false};
	}
	if (name == "Italic" || name == "Oblique") {
		return Style{false, true};
	}
	if (name == "BoldItalic" || name == "BoldOblique") {
		return Style{true, true};
	}
	return std::nullopt;
}

[[nodiscard]] constexpr Standard14Font font_of(Family family, Style style) noexcept
{
	const uint8_t base = family == Family::Courier ? 0u : family == Family::Helvetica ? 4u : 8u;
	const uint8_t offset = style.bold ? (style.italic ? 3u : 1u) : (style.italic ? 2u : 0u);
	return static_cast<Standard14Font>(base + offset);
}

} // namespace

std::optional<Standard14Font> standard14_from_base_font(std::string_view base_font) noexcept
{
	const std::string_view name = without_subset_prefix(base_font);
	for (size_t i = 0; i < kStandard14FontCount; ++i) {
		if (kStandard14Metrics[i].base_font == name) {
			return static_cast<Standard14Font>(i);
		}
	}
	// `Family,Style` (Windows producers; the family may hold a hyphen, as `Times-Roman,Bold`) or `Family-Style`
	// (PostScript names such as `Arial-BoldMT`).
	size_t split = name.find(',');
	if (split == std::string_view::npos) {
		split = name.find('-');
	}
	const std::string_view family_name = name.substr(0, split);
	const std::optional<Style> style = split == std::string_view::npos ? Style{} : style_of(name.substr(split + 1u));
	if (!style.has_value()) {
		return std::nullopt;
	}
	// Symbol has one face; a style a producer asks of it (`Symbol,Bold`) or Monotype's name (`SymbolMT`) is still it.
	if (without_suffix(family_name, "MT") == "Symbol") {
		return Standard14Font::Symbol;
	}
	const std::optional<Family> family = family_of(family_name);
	if (!family.has_value()) {
		return std::nullopt;
	}
	return font_of(*family, *style);
}

const Standard14Metrics& standard14_metrics(Standard14Font font) noexcept
{
	return kStandard14Metrics[static_cast<size_t>(font)];
}

const Standard14Glyph* standard14_glyph_by_name(Standard14Font font, std::string_view name) noexcept
{
	const std::span<const Standard14Glyph> glyphs = standard14_metrics(font).glyphs;
	const auto it = std::lower_bound(glyphs.begin(), glyphs.end(), name,
		[](const Standard14Glyph& glyph, std::string_view key) { return glyph.name < key; });
	return it != glyphs.end() && it->name == name ? &*it : nullptr;
}

const Standard14Glyph* standard14_glyph_by_code(Standard14Font font, uint8_t code) noexcept
{
	const Standard14Metrics& metrics = standard14_metrics(font);
	const int16_t index = metrics.code_glyph[code];
	return index < 0 ? nullptr : &metrics.glyphs[static_cast<size_t>(index)];
}

Standard14Face standard14_face(Standard14Font font) noexcept
{
	const auto value = static_cast<uint8_t>(font);
	if (font == Standard14Font::Symbol) {
		return Standard14Face{L"Symbol", false, false};
	}
	if (font == Standard14Font::ZapfDingbats) {
		return Standard14Face{L"Segoe UI Symbol", false, false};
	}
	const uint8_t style = value % 4u; // plain, bold, italic or oblique, both
	const std::wstring_view family = value < 4u ? L"Courier New" : value < 8u ? L"Arial" : L"Times New Roman";
	return Standard14Face{family, style == 1u || style == 3u, style == 2u || style == 3u};
}

} // namespace recto::pdf
