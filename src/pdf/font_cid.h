// Composite fonts (ISO 32000-1 section 9.7; Adobe Technical Note 5014; D01 T02 section 30): a `/Type0` font's CMap
// from codes to CIDs, its one CIDFont's map from CIDs to glyphs and its widths per CID, assembled from the font's
// dictionaries into an immutable `Type0Font` shared read-only across threads with its `LoadedFont`. The predefined CJK
// CMaps are `§17`'s.
#pragma once

#include "error.h"
#include "filters.h"
#include "font.h"
#include "object.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace recto::pdf {

// Bounds on what a hostile CMap or CIDFont may ask for.
inline constexpr size_t kMaxCidCMapStreamBytes = size_t{4} << 20u;
inline constexpr size_t kMaxCidCodespaceRanges = 100u; // as `kMaxCodespaceRanges`: each code is checked against them
inline constexpr size_t kMaxCidMappings = 100'000u; // cidrange, cidchar and notdef entries together
inline constexpr uint32_t kMaxCid = 65'535u;         // Annex C
inline constexpr size_t kMaxCidWidthElements = 65'536u; // in `/W`, and in `/W2`, nested arrays included
inline constexpr size_t kMaxUseCMapLevels = 8u;        // an encoding CMap and the CMaps it inherits

// One code of a string: its bytes read as a big-endian number, how many bytes it took (0 at the end of the string)
// and its CID.
struct CidCode {
	uint32_t code = 0;
	uint8_t length = 0;
	uint32_t cid = 0;
	uint32_t notdef_cid = 0; // the code's notdef mapping, else 0: the CID used when the font has no glyph for `cid`
};

// A CMap from codes to CIDs (9.7.5): `/Identity-H`, `/Identity-V` or an embedded CMap stream.
class CidCMap {
public:
	[[nodiscard]] static CidCMap identity(bool vertical) noexcept;

	// An embedded CMap stream, over `base` (the CMap it inherits through its dictionary's `/UseCMap`, or null): the
	// base's codespace ranges and mappings, this CMap's own mappings over them. `usecmap` in the stream may name
	// `/Identity-H` or `/Identity-V`; another name is `UnsupportedFeature`. A CMap with no codespace range is
	// `DamagedFormat`, a bound passed `ResourceLimit`.
	[[nodiscard]] static PdfResult<CidCMap> parse(std::span<const uint8_t> stream, const CidCMap* base = nullptr) noexcept;

	// The first code of `bytes` by the codespace ranges (9.7.6.2): a code no range matches takes the length of the
	// shortest range with the same first byte, else of the shortest range, and is CID 0; a mapped code its CID, else
	// its notdef mapping, else 0. A non-empty string always gives at least one byte and never more than it holds; an
	// incomplete code at its end takes the rest, as CID 0; the empty string gives length 0.
	[[nodiscard]] CidCode next(std::span<const uint8_t> bytes) const noexcept;

	[[nodiscard]] bool vertical() const noexcept
	{
		return vertical_;
	}
	void set_vertical(bool vertical) noexcept
	{
		vertical_ = vertical;
	}

	// A code space range, and a run of codes mapped to consecutive CIDs from `cid`; codes of different lengths are
	// different codes.
	struct Range {
		uint32_t low;
		uint32_t high;
		uint8_t length;
	};
	struct Mapping {
		uint32_t low;
		uint32_t high;
		uint8_t length;
		uint32_t cid;
	};

	// The codespace ranges a code may be checked against, for a caller's work budget.
	[[nodiscard]] size_t codespace_count() const noexcept
	{
		return codespace_.size();
	}

	// The heap this CMap holds, for a caller's memory budget.
	[[nodiscard]] size_t memory_bytes() const noexcept
	{
		return name_.capacity() + codespace_.capacity() * sizeof(Range) + (cids_.capacity() + notdefs_.capacity()) * sizeof(Mapping);
	}

private:
	bool identity_ = false;
	bool vertical_ = false;
	std::string name_; // its `/CMapName`, by which a derived CMap's `usecmap` names it
	std::vector<Range> codespace_;
	std::vector<Mapping> cids_;    // sorted by (length, low), not overlapping
	std::vector<Mapping> notdefs_; // likewise
};

// A `/CIDFontType2` font's `/CIDToGIDMap` (9.7.4.2).
class CidToGidMap {
public:
	[[nodiscard]] static CidToGidMap identity() noexcept;
	// A map from glyph indices by CID (a CID-keyed CFF's charset read out).
	[[nodiscard]] static CidToGidMap from_glyphs(std::vector<uint16_t> glyphs) noexcept;
	// Big-endian glyph indices by CID; an odd length is `DamagedFormat`. Entries past CID 65,535 are not kept.
	[[nodiscard]] static PdfResult<CidToGidMap> parse(std::span<const uint8_t> stream) noexcept;
	// The CID's glyph, 0 past the map.
	[[nodiscard]] uint32_t glyph(uint32_t cid) const noexcept;

	[[nodiscard]] size_t memory_bytes() const noexcept
	{
		return glyphs_.capacity() * sizeof(uint16_t);
	}

private:
	bool identity_ = true;
	std::vector<uint16_t> glyphs_;
};

enum class CidFontKind : uint8_t {
	CIDFontType0 = 0,
	CIDFontType2 = 1,
};

// The glyph a CID selects (9.7.4.2): through the map for `/CIDFontType2`; for `/CIDFontType0`, by the CID-keyed
// program's charset, else through the map (an OpenType-wrapped CID-keyed CFF's charset, else the identity: the CID itself
// for a name-keyed program, as FreeType and pdf.js read it). A glyph at or past the font's glyph count is glyph 0; without
// a font the glyph is unchecked.
[[nodiscard]] uint16_t cid_glyph(uint32_t cid, CidFontKind kind, const CidToGidMap& map, const LoadedFont* font) noexcept;

// A CID's vertical metrics (9.7.4.3), in text space: the vertical advance `w1y` and the position vector (vx, vy) from
// the horizontal origin to the vertical one.
struct CidVertical {
	double w1y = 0.0;
	double vx = 0.0;
	double vy = 0.0;
};

// A CIDFont's widths (9.7.4.3): `/DW` (default 1000) and `/W`, `/DW2` (default [880 -1000]) and `/W2`, in text space.
class CidWidths {
public:
	// The descendant font's values, any of them null when absent; indirect references, the values' own and those
	// inside them, go through `resolver` (none without one). A malformed entry, an object reference included that the
	// resolver finds missing or damaged, is skipped, as readers do, a later entry winning over an earlier one; any
	// other resolver error is passed on; more than `kMaxCidWidthElements` elements, or a range reaching past CID
	// 65,535, is `ResourceLimit`.
	[[nodiscard]] static PdfResult<CidWidths> build(const PdfValue* dw, const PdfValue* w, const PdfValue* dw2,
		const PdfValue* w2, const IndirectObjectResolver* resolver) noexcept;

	[[nodiscard]] double advance(uint32_t cid) const noexcept;
	// A CID without a `/W2` entry: `w1y` and `vy` from `/DW2`, `vx` half its horizontal advance.
	[[nodiscard]] CidVertical vertical(uint32_t cid) const noexcept;

	[[nodiscard]] size_t memory_bytes() const noexcept
	{
		return widths_.capacity() * sizeof(Run<double>) + verticals_.capacity() * sizeof(Run<CidVertical>);
	}

	template <typename V>
	struct Run {
		uint32_t low;
		uint32_t high;
		V value;
	};

private:
	double default_width_ = 1.0;
	double default_vy_ = 0.88;
	double default_w1y_ = -1.0;
	std::vector<Run<double>> widths_;          // sorted, not overlapping
	std::vector<Run<CidVertical>> verticals_; // likewise; vx and vy as given
};

// One code of a string through the whole font: its CID, glyph and metrics.
struct Type0Glyph {
	uint32_t code = 0;
	uint8_t length = 0;
	uint32_t cid = 0;
	uint16_t glyph = 0;
	double advance = 0.0;
	CidVertical vertical{};
};

// A `/Type0` font, immutable once loaded.
class Type0Font {
public:
	Type0Font(CidCMap cmap, CidFontKind kind, CidToGidMap map, CidWidths widths,
		std::shared_ptr<const LoadedFont> font) noexcept;

	// The first code of `bytes` (length 0 at the end).
	[[nodiscard]] Type0Glyph next(std::span<const uint8_t> bytes) const noexcept;

	[[nodiscard]] const CidCMap& cmap() const noexcept
	{
		return cmap_;
	}
	[[nodiscard]] CidFontKind kind() const noexcept
	{
		return kind_;
	}
	[[nodiscard]] const CidWidths& widths() const noexcept
	{
		return widths_;
	}
	// The embedded program, or null when the descendant embeds none (`§17` substitutes it).
	[[nodiscard]] const std::shared_ptr<const LoadedFont>& font() const noexcept
	{
		return font_;
	}

	// The heap its CMap, maps, widths and retained embedded program hold, for a caller's memory budget.
	[[nodiscard]] size_t memory_bytes() const noexcept
	{
		const size_t program = font_ != nullptr && font_->program() != nullptr ? font_->program()->capacity() : 0u;
		return sizeof(*this) + cmap_.memory_bytes() + map_.memory_bytes() + widths_.memory_bytes() + program;
	}

private:
	const CidCMap cmap_;
	const CidFontKind kind_;
	const CidToGidMap map_;
	const CidWidths widths_;
	const std::shared_ptr<const LoadedFont> font_;
};

// A `/Type0` font from its dictionary (9.7.6): its `/Encoding` (an Identity name, or an embedded CMap stream inheriting
// through `/UseCMap`, at most `kMaxUseCMapLevels` CMaps, a cycle `DamagedFormat`; another name `UnsupportedFeature`),
// its one `/DescendantFonts` entry (`/CIDFontType0` or `/CIDFontType2`, else `DamagedFormat`) with its widths and
// `/CIDToGIDMap`, and its descriptor's `/FontFile2` or `/FontFile3` program, every indirect object through `resolver`
// and every stream through `decode_stream`.
[[nodiscard]] PdfResult<std::shared_ptr<const Type0Font>> load_type0_font(
	const PdfDict& type0, const IndirectObjectResolver& resolver) noexcept;

} // namespace recto::pdf
