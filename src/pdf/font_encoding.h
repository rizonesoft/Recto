// Simple-font encodings, glyph names to Unicode, TrueType glyph selection and `/ToUnicode` CMaps (ISO 32000-1
// sections 9.6.6, 9.10 and Annex D; D01 T02 section 28).
//
// Thread-ownership and freeze contract: every map here is built once by a factory function and then only read through
// `const` members with no lazy state, so a font's maps are shared by every thread that holds the font without locks.
#pragma once

#include "error.h"
#include "object.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace recto::pdf {

// ---------------------------------------------------------------------------------------------------------------------
// Annex D encodings and glyph names
// ---------------------------------------------------------------------------------------------------------------------

// The base encodings of Annex D: the three a PDF names, `StandardEncoding`, and the built-in encodings of the Symbol
// and ZapfDingbats standard fonts.
enum class BaseEncoding : uint8_t {
	Standard = 0,
	MacRoman,
	WinAnsi,
	MacExpert,
	Symbol,
	ZapfDingbats,
};

// The glyph name `code` has in `encoding`, or empty.
[[nodiscard]] std::string_view base_encoding_name(BaseEncoding encoding, uint8_t code) noexcept;

// The encoding a PDF `/Encoding` or `/BaseEncoding` name names: `MacRomanEncoding`, `MacExpertEncoding`,
// `WinAnsiEncoding`, and `StandardEncoding`, which producers write though Table 112 does not list it.
[[nodiscard]] std::optional<BaseEncoding> base_encoding_from_name(std::string_view name) noexcept;

// The Mac OS standard Roman character set ISO 32000-1 9.6.6.4 uses for a TrueType `(1,0)` subtable: Annex D's
// `MacRomanEncoding` and fifteen Mac OS characters it lacks. The code of a glyph name, or none.
[[nodiscard]] std::optional<uint8_t> mac_os_roman_code(std::string_view name) noexcept;

// The characters a glyph name stands for, by the Adobe Glyph List Specification's algorithm: the part after the first
// `.` dropped, `_` separating components, each component the scalars of zapfdingbats.txt (for a ZapfDingbats font)
// or else the AGL, a `uniXXXX...` run of four-digit groups, or `uXXXX` to `uXXXXXX`; a surrogate or a scalar past U+10FFFF
// makes its component nothing. Empty when the name stands for nothing.
[[nodiscard]] PdfResult<std::u32string> glyph_name_unicode(std::string_view name, bool zapf_dingbats = false) noexcept;

// The one Unicode scalar a Windows face draws a glyph name with, for a standard font drawn through a system face
// (D01 T02 section 27): the name's single AGL scalar, a character in the standard place of a name the AGL maps to
// Adobe's private-use area (`commaaccent`, U+F6C3 in the AGL, is U+0326), or none (Symbol's `apple`, U+F8FF).
[[nodiscard]] std::optional<char32_t> drawable_unicode(std::string_view name, bool zapf_dingbats = false) noexcept;

// ---------------------------------------------------------------------------------------------------------------------
// The effective encoding of a simple font
// ---------------------------------------------------------------------------------------------------------------------

// A font's built-in encoding: the glyph name of each code, empty for none.
using GlyphNames = std::array<std::string, 256>;

// Bounds on a `/Differences` array: entries read, and a glyph name's length.
inline constexpr size_t kMaxDifferencesEntries = 65'536u;
inline constexpr size_t kMaxGlyphNameBytes = 255u;

// The code-to-glyph-name map of a simple font (ISO 32000-1 9.6.6.1 and 9.6.6.2), immutable once built.
class SimpleEncoding {
public:
	// `encoding` is the font's `/Encoding` value, resolved (a name, a dictionary, or null when absent); `symbolic` the
	// font descriptor's Symbolic flag; `built_in` the font's own encoding (the standard fonts' AFM codes, an embedded
	// Type 1 program's, or null for none, as for a TrueType font). A named encoding is its base; a dictionary's
	// `/BaseEncoding` is the base its `/Differences` apply over, and without one the base is `StandardEncoding` for a
	// non-symbolic font and the built-in encoding for a symbolic one (Table 114); no `/Encoding` is the built-in
	// encoding, or `StandardEncoding` for a non-symbolic font without one. An unknown name or a malformed
	// `/Differences` entry is skipped, as readers do; more than `kMaxDifferencesEntries` entries is `ResourceLimit`.
	[[nodiscard]] static PdfResult<SimpleEncoding> build(
		const PdfValue* encoding, bool symbolic, const GlyphNames* built_in) noexcept;

	[[nodiscard]] std::string_view name(uint8_t code) const noexcept
	{
		return names_[code];
	}

	// The encoding the font names, when it names `MacRomanEncoding` or `WinAnsiEncoding` (directly or as its
	// `/BaseEncoding`), and whether it has `/Differences`: both select TrueType's name path (9.6.6.4).
	[[nodiscard]] std::optional<BaseEncoding> named_base() const noexcept
	{
		return named_base_;
	}
	[[nodiscard]] bool has_differences() const noexcept
	{
		return has_differences_;
	}

private:
	GlyphNames names_{};
	std::optional<BaseEncoding> named_base_{};
	bool has_differences_ = false;
};

// ---------------------------------------------------------------------------------------------------------------------
// TrueType glyph selection (ISO 32000-1 9.6.6.4)
// ---------------------------------------------------------------------------------------------------------------------

// A font program's `cmap` subtables Recto reads (`(3,1)`, `(3,10)`, `(3,0)` and `(1,0)`, formats 0, 4, 6 and 12) and
// its `post` glyph names (formats 1 and 2), immutable once parsed. A table that is absent is empty; one that is
// malformed is `DamagedFormat` from `parse`, every read bounds-checked and the work linear in the table.
class TrueTypeTables {
public:
	[[nodiscard]] static PdfResult<TrueTypeTables> parse(std::span<const uint8_t> program) noexcept;

	// The glyph a subtable maps `code` to, or none (also when the font has no such subtable).
	[[nodiscard]] std::optional<uint16_t> lookup(uint16_t platform, uint16_t encoding, uint32_t code) const noexcept;
	[[nodiscard]] bool has(uint16_t platform, uint16_t encoding) const noexcept;
	// The glyph a `post` name names, or none.
	[[nodiscard]] std::optional<uint16_t> glyph_for_name(std::string_view name) const noexcept;
	// The lowest Unicode scalar the font's Unicode subtable (`(3,10)`, else `(3,1)`) maps to `glyph`, or none.
	[[nodiscard]] std::optional<char32_t> unicode_for_glyph(uint16_t glyph) const noexcept;

	// Every format read as codes `start` to `end` mapping to `glyph` onward (format 4's arithmetic wraps at 65,536),
	// sorted by `start` and not overlapping.
	struct Group {
		uint32_t start;
		uint32_t end;
		uint32_t glyph;
	};
	struct Subtable {
		uint16_t platform;
		uint16_t encoding;
		bool wraps16;
		std::vector<Group> groups;
	};

private:
	std::vector<Subtable> subtables_;
	std::vector<std::pair<std::string, uint16_t>> post_names_; // sorted by name, the lowest glyph of a repeated name
	std::vector<std::pair<uint16_t, char32_t>> unicode_of_glyph_; // sorted by glyph
};

// The glyph a code of a simple TrueType font draws (ISO 32000-1 9.6.6.4): when the font names `MacRomanEncoding` or
// `WinAnsiEncoding`, has `/Differences`, or is flagged non-symbolic and not symbolic, the code's glyph name (its
// encoding, then `StandardEncoding` for a code it leaves empty) through a `(3,1)` subtable by the name's Unicode, a
// `(1,0)` subtable by the name's Mac OS Roman code, or the `post` names; otherwise, or when none of those finds it, a
// `(3,0)` subtable at the code and at `0xF000`, `0xF100` and `0xF200` plus it, a `(1,0)` subtable at the code, and the
// code as a glyph index. Glyph 0 when nothing maps it.
[[nodiscard]] uint16_t simple_truetype_glyph(const TrueTypeTables& tables, const SimpleEncoding& encoding, bool symbolic,
	bool nonsymbolic, uint8_t code) noexcept;

// ---------------------------------------------------------------------------------------------------------------------
// `/ToUnicode` CMaps (ISO 32000-1 9.10.3, Adobe Technical Notes 5014 and 5411)
// ---------------------------------------------------------------------------------------------------------------------

inline constexpr size_t kMaxToUnicodeStreamBytes = size_t{16} << 20u;
inline constexpr size_t kMaxCodespaceRanges = 100u;
inline constexpr size_t kMaxToUnicodeCodes = 65'536u;
inline constexpr size_t kMaxToUnicodeTargetBytes = 512u;

// A parsed `/ToUnicode` CMap, immutable once parsed: its codespace ranges, and each mapped code's characters. A
// `bfrange` with a string target is kept as a range (its last character incremented across it), one with an array is
// expanded; both count every code they map toward `kMaxToUnicodeCodes`.
class ToUnicodeCMap {
public:
	// `usecmap` is `UnsupportedFeature`; a malformed operator or operand is `DamagedFormat`; a bound passed is
	// `ResourceLimit`. A target that is not valid UTF-16 is skipped, as readers do.
	[[nodiscard]] static PdfResult<ToUnicodeCMap> parse(std::span<const uint8_t> stream) noexcept;

	// The characters `code` (of `length` bytes, 1 to 4) maps to, or none.
	[[nodiscard]] std::optional<std::u32string> lookup(uint32_t code, uint8_t length) const noexcept;

	// How many bytes the codespace gives a code starting with these bytes (1 for a CMap without codespace ranges).
	[[nodiscard]] uint8_t code_length(std::span<const uint8_t> bytes) const noexcept;

	struct Range {
		uint32_t low;
		uint32_t high;
		uint8_t length;
	};
	struct Mapping {
		uint32_t low;
		uint32_t high;
		uint8_t length;
		std::u32string target; // for a range, the first code's characters, its last one incremented across it
	};

private:
	std::vector<Range> codespace_;
	std::vector<Mapping> mappings_; // sorted by (length, low), not overlapping
};

// ---------------------------------------------------------------------------------------------------------------------
// Unicode for a code, in the order readers use (ISO 32000-1 9.10.2)
// ---------------------------------------------------------------------------------------------------------------------

// The characters a code of a simple font stands for: its `/ToUnicode` mapping, else its glyph name through the AGL,
// else, for a TrueType font, the Unicode scalar its Unicode subtable gives the glyph the code selects; else empty, never
// a guessed character.
struct SimpleFontText {
	const ToUnicodeCMap* to_unicode = nullptr;
	const SimpleEncoding* encoding = nullptr;
	bool zapf_dingbats = false;
	const TrueTypeTables* truetype = nullptr;
	bool symbolic = false;
	bool nonsymbolic = false;
};
[[nodiscard]] PdfResult<std::u32string> simple_font_unicode(const SimpleFontText& font, uint8_t code) noexcept;

} // namespace recto::pdf
