// Type 1 and CFF programs read once into immutable data, and their glyphs computed per call from it (ISO 32000-1
// sections 9.6.2 and 9.9; the Adobe Type 1 Font Format; Adobe Technical Notes 5176 and 5177; D01 T02 section 29).
// Internal to recto-pdf: `LoadedFont` holds the parsed program and calls `charstrings::shape` for every outline.
#pragma once

#include "font.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace recto::pdf {

// A CFF INDEX: each object's absolute byte range in the program, object `i` from `offsets[i]` to `offsets[i + 1]`.
struct CffIndex {
	std::vector<uint32_t> offsets;

	[[nodiscard]] size_t count() const noexcept
	{
		return offsets.empty() ? 0u : offsets.size() - 1u;
	}
};

// A text-space transform `[a b c d e f]`: x' = a x + c y + e, y' = b x + d y + f.
using FontMatrix = std::array<double, 6>;
inline constexpr FontMatrix kDefaultFontMatrix = {0.001, 0.0, 0.0, 0.001, 0.0, 0.0};

struct LoadedFont::CharstringProgram {
	enum class Kind : uint8_t {
		Type1,
		Cff,
		CffCid,
	};
	Kind kind = Kind::Type1;
	uint16_t glyph_count = 0;
	// Type 1: each glyph's charstring and each subroutine, decrypted with `lenIV` bytes removed; one font matrix.
	std::vector<std::vector<uint8_t>> type1_glyphs;
	std::vector<std::vector<uint8_t>> type1_subrs;
	FontMatrix type1_matrix = kDefaultFontMatrix;
	// CFF: the charstrings and global subroutines in the program, and each FD's private data (one for a name-keyed
	// font), its matrix the FD's composed with the top one.
	CffIndex charstrings;
	CffIndex global_subrs;
	struct Fd {
		CffIndex local_subrs;
		double default_width = 0.0;
		double nominal_width = 0.0;
		FontMatrix matrix = kDefaultFontMatrix;
	};
	std::vector<Fd> fds;
	std::vector<uint8_t> fd_of_glyph; // a CID-keyed font's FD of each glyph
	// Lookup: names (Type 1 and name-keyed CFF) and CIDs to glyphs, sorted; the built-in encoding.
	std::vector<std::pair<std::string, uint16_t>> names;
	std::vector<std::pair<uint32_t, uint16_t>> cids;
	std::optional<GlyphNames> built_in;
};

namespace charstrings {

[[nodiscard]] PdfResult<std::unique_ptr<const LoadedFont::CharstringProgram>> parse(
	std::span<const uint8_t> bytes, CharstringFormat format, Type1Lengths lengths) noexcept;

// One glyph's advance, ink box and outline in text space, from the program bytes the parsed data points into.
[[nodiscard]] PdfResult<GlyphShape> shape(
	const LoadedFont::CharstringProgram& program, std::span<const uint8_t> bytes, uint16_t glyph) noexcept;

// The advance alone, running the charstring only as far as its width.
[[nodiscard]] PdfResult<double> advance(
	const LoadedFont::CharstringProgram& program, std::span<const uint8_t> bytes, uint16_t glyph) noexcept;

} // namespace charstrings

} // namespace recto::pdf
