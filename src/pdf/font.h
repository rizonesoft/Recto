// Embedded font programs loaded in memory into immutable `LoadedFont` objects
// (ISO 32000-1 sections 9.6.3, 9.8 and 9.9; D01 T02 section 2).
//
// Thread-ownership and freeze contract (`frozen: true`):
// The DirectWrite factory is created once with `DWRITE_FACTORY_TYPE_SHARED`,
// and one in-memory font file loader is registered with it for the life of
// the process. A font's bytes outlive its `IDWriteFontFace` through the owner
// object given to `CreateInMemoryFontFileReference`. A `LoadedFont` has only
// `const` accessors and no lazy caches: outlines are computed per call from
// the immutable bytes, so `RenderPool` workers may use one font concurrently.
#pragma once

#include "display_list.h"
#include "error.h"
#include "font_encoding.h"
#include "object.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

struct IDWriteFontFace;

namespace recto::pdf {

// The embedded font program a PDF names (ISO 32000-1 Table 126).
enum class FontProgramFormat : uint8_t {
	TrueType = 0, // `/FontFile2`
	OpenType = 1, // `/FontFile3` with `/Subtype /OpenType`
};

// How a loaded font draws: through DirectWrite, or as paths from its own
// outlines when DirectWrite refused the program.
enum class LoadedFontKind : uint8_t {
	TrueType = 0,
	OpenType = 1,
	OutlineOnly = 2,
};

// A glyph outline in text space (glyph units divided by units per em, y up):
// the verbs and points of `display_list.h`, ready for `PathBuilder` or
// `DisplayListBuilder::add_path`. Contours are closed.
struct GlyphOutline {
	std::vector<PathVerb> verbs;
	std::vector<PdfPoint> points;
};

// A glyph's advance, ink box and outline in text space, from one expansion.
struct GlyphShape {
	double advance{0.0};
	std::optional<PdfRect> ink_box{};
	GlyphOutline outline{};
};

// Per-call ceilings on one glyph's outline: nesting of composite glyphs, the
// components visited while expanding, and the points the expansion holds.
inline constexpr size_t kMaxCompositeDepth = 16u;
inline constexpr size_t kMaxComponentVisits = 4'096u;
inline constexpr size_t kMaxExpandedGlyphPoints = 65'535u;

class LoadedFont;

[[nodiscard]] PdfResult<std::shared_ptr<const LoadedFont>> load_embedded_font(
	std::shared_ptr<const std::vector<uint8_t>> program,
	FontProgramFormat format) noexcept;

// Bare Type 1 and CFF programs, which DirectWrite does not load (ISO 32000-1 9.6.2 and 9.9; D01 T02 section 29): a
// `/FontFile` Type 1 program (cleartext, `eexec` part, trailing zeros, by its `/Length1` to `/Length3`), and a
// `/FontFile3` CFF program of `/Subtype /Type1C` or `/CIDFontType0C`.
enum class CharstringFormat : uint8_t {
	Type1 = 0,
	Type1C = 1,
	CIDFontType0C = 2,
};

// A Type 1 program's `/Length1` (cleartext), `/Length2` (`eexec` part) and `/Length3` (zeros); a length of 0 is found
// from the program.
struct Type1Lengths {
	size_t length1 = 0;
	size_t length2 = 0;
	size_t length3 = 0;
};

// Bounds on a charstring program and on the work one glyph may take, the work shared across every subroutine call and
// both `seac` components.
inline constexpr size_t kMaxCharstringProgramBytes = size_t{64} << 20u;
inline constexpr size_t kMaxCharstringBytes = 65'535u;
inline constexpr size_t kMaxGlyphPathOperators = 100'000u;
inline constexpr size_t kMaxGlyphCharstringWork = 1'000'000u;
inline constexpr size_t kMaxCharstringSubrDepth = 10u;
inline constexpr size_t kMaxType1Stack = 24u;
inline constexpr size_t kMaxType2Stack = 48u;
inline constexpr size_t kMaxType2Stems = 96u; // Technical Note 5177, Appendix B
// Entries in a CFF program's global and local subroutine INDEXes together, so FDs sharing one large table cannot
// multiply it.
inline constexpr size_t kMaxCffSubrEntries = size_t{1} << 20u;

// Parses the program once into immutable data; each glyph is computed per call from it, so the font draws as an
// `OutlineOnly` one through its outlines, in text space through the program's font matrix. A malformed program is
// `DamagedFormat`, a bound passed `ResourceLimit`.
[[nodiscard]] PdfResult<std::shared_ptr<const LoadedFont>> load_charstring_font(
	std::shared_ptr<const std::vector<uint8_t>> program,
	CharstringFormat format,
	Type1Lengths lengths = {}) noexcept;

// An immutable embedded font. Metrics are in text space: glyph units divided
// by `units_per_em()`, so the interpreter multiplies them by the font size,
// and an ink box becomes `PositionedGlyph::ink_bounds_pt` (D01 T02 section 1).
class LoadedFont {
public:
	LoadedFont(const LoadedFont&) = delete;
	LoadedFont& operator=(const LoadedFont&) = delete;
	LoadedFont(LoadedFont&&) = delete;
	LoadedFont& operator=(LoadedFont&&) = delete;
	~LoadedFont() noexcept;

	[[nodiscard]] LoadedFontKind kind() const noexcept
	{
		return kind_;
	}

	[[nodiscard]] uint16_t units_per_em() const noexcept
	{
		return units_per_em_;
	}

	// The heap this font holds: its program and every table parsed from it, for a caller's memory budget.
	[[nodiscard]] size_t memory_bytes() const noexcept;

	[[nodiscard]] uint16_t glyph_count() const noexcept
	{
		return glyph_count_;
	}

	// The font program, shared with DirectWrite when it loaded the font.
	[[nodiscard]] const std::shared_ptr<const std::vector<uint8_t>>& program() const noexcept
	{
		return program_;
	}

	// The DirectWrite face (not reference-counted for the caller; it lives as
	// long as this font), or null for `OutlineOnly`.
	[[nodiscard]] IDWriteFontFace* dwrite_face() const noexcept;

	// The glyph's horizontal advance in text space. `DamagedFormat` past
	// `glyph_count()`.
	[[nodiscard]] PdfResult<double> advance(uint16_t glyph) const noexcept;

	// The glyph's ink box in text space, or none for a glyph that paints
	// nothing.
	[[nodiscard]] PdfResult<std::optional<PdfRect>> ink_box(uint16_t glyph) const noexcept;

	// The glyph's outline in text space, computed from the font program on each
	// call within the per-call ceilings above (`ResourceLimit` past them).
	[[nodiscard]] PdfResult<GlyphOutline> outline(uint16_t glyph) const noexcept;

	// The advance, ink box and outline together, from one expansion of the
	// glyph where the font is read through its `glyf` outlines.
	[[nodiscard]] PdfResult<GlyphShape> shape(uint16_t glyph) const noexcept;

	// Whether the glyph stays within the per-call ceilings, checked through the
	// font's own `glyf` outlines before DirectWrite expands it: the rasterizer
	// (`D01 T02 section 6`) calls it before drawing a TrueType glyph through
	// DirectWrite. A Type 1 or CFF glyph is checked by running its charstring
	// under its bounds (D01 T02 section 29). Always succeeds for a font with
	// neither.
	[[nodiscard]] PdfResult<void> check_glyph(uint16_t glyph) const noexcept;

	// The glyph a name names (a Type 1 program's `/CharStrings` key, a name-keyed CFF charset), or none, as for a
	// font without glyph names.
	[[nodiscard]] std::optional<uint16_t> glyph_for_name(std::string_view name) const noexcept;
	// The glyph a CID-keyed CFF charset gives a CID, or none.
	[[nodiscard]] std::optional<uint16_t> glyph_for_cid(uint32_t cid) const noexcept;
	// Whether the program is a CID-keyed CFF, whose charset maps CIDs to glyphs (D01 T02 section 30).
	[[nodiscard]] bool cid_keyed() const noexcept;
	// The program's built-in encoding (a Type 1 `/Encoding`, a CFF encoding) as 256 glyph names, for `SimpleEncoding`
	// (D01 T02 section 28), or null for a font without one.
	[[nodiscard]] const GlyphNames* built_in_encoding() const noexcept;

	// The DirectWrite face and the validated glyf tables, defined in font.cpp; the parsed Type 1 or CFF program, in
	// font_charstrings.h.
	struct DWriteState;
	struct GlyfTables;
	struct CharstringProgram;

private:

	friend PdfResult<std::shared_ptr<const LoadedFont>> load_embedded_font(
		std::shared_ptr<const std::vector<uint8_t>> program,
		FontProgramFormat format) noexcept;
	friend PdfResult<std::shared_ptr<const LoadedFont>> load_charstring_font(
		std::shared_ptr<const std::vector<uint8_t>> program,
		CharstringFormat format,
		Type1Lengths lengths) noexcept;

	LoadedFont(
		LoadedFontKind kind,
		std::shared_ptr<const std::vector<uint8_t>> program,
		uint16_t units_per_em,
		uint16_t glyph_count,
		std::unique_ptr<const DWriteState> dwrite,
		std::unique_ptr<const GlyfTables> glyf,
		std::unique_ptr<const CharstringProgram> charstrings) noexcept;

	// The glyph whose metrics a DirectWrite TrueType glyph takes (USE_MY_METRICS), from the glyf tables.
	[[nodiscard]] PdfResult<uint16_t> dwrite_metrics_glyph(uint16_t glyph) const noexcept;
	// The glyf budget check without recording (the public functions record).
	[[nodiscard]] PdfResult<void> glyf_check(uint16_t glyph) const noexcept;

	const LoadedFontKind kind_;
	const std::shared_ptr<const std::vector<uint8_t>> program_;
	const uint16_t units_per_em_;
	const uint16_t glyph_count_;
	const std::unique_ptr<const DWriteState> dwrite_;
	const std::unique_ptr<const GlyfTables> glyf_;
	const std::unique_ptr<const CharstringProgram> charstrings_;
};

// Loads an embedded font program in memory. DirectWrite loads it when it can;
// a TrueType program DirectWrite rejects as a font is read through its own
// `glyf` outlines (`OutlineOnly`). A DirectWrite resource failure (memory,
// or any status that is not a font rejection) returns its typed error with
// the step and `HRESULT` kept, and tries no fallback.
[[nodiscard]] PdfResult<std::shared_ptr<const LoadedFont>> load_embedded_font(
	std::shared_ptr<const std::vector<uint8_t>> program,
	FontProgramFormat format) noexcept;

// The fourteen standard fonts (ISO 32000-1 section 9.6.2.2), whose metrics a reader knows without an embedded program
// (D01 T02 section 27). The metrics are Adobe's Core14 AFM files (`src/pdf/adobe-core14-afm`), generated into
// `constexpr` tables by `scripts/build-font-metrics.py`; the face drawn with is a Windows face, loaded once into an
// immutable `LoadedFont` that every thread shares. Layout takes the AFM width, so text drawn through the face never
// reflows; which glyph name and Unicode a PDF code means is the encoding's (D01 T02 section 28).
enum class Standard14Font : uint8_t {
	Courier = 0,
	CourierBold,
	CourierOblique,
	CourierBoldOblique,
	Helvetica,
	HelveticaBold,
	HelveticaOblique,
	HelveticaBoldOblique,
	TimesRoman,
	TimesBold,
	TimesItalic,
	TimesBoldItalic,
	Symbol,
	ZapfDingbats,
};
inline constexpr size_t kStandard14FontCount = 14u;

// One glyph of an AFM file, in AFM units of 1/1000 em (text space times 1000).
struct Standard14Glyph {
	std::string_view name;
	int16_t code; // in the font's built-in encoding, or -1 for a glyph it does not encode
	int16_t width;
	std::array<int16_t, 4> box; // llx, lly, urx, ury
};

// One font's AFM metrics. A field the AFM does not give (`Symbol` and `ZapfDingbats` give no ascender, descender or
// cap height) is none, never zero.
struct Standard14Metrics {
	std::string_view base_font;
	std::array<int16_t, 4> font_box; // llx, lly, urx, ury
	std::optional<int16_t> ascender;
	std::optional<int16_t> descender;
	std::optional<int16_t> cap_height;
	double italic_angle;
	bool fixed_pitch;
	std::span<const Standard14Glyph> glyphs; // sorted by name in byte order
	std::span<const int16_t, 256> code_glyph; // each code's index into `glyphs`, or -1
};

// The standard font a `/BaseFont` names: the fourteen names, after a subset prefix (`ABCDEF+`), and the aliases
// producers write for them (`Arial`, `ArialMT`, `Arial,Bold`, `Arial-BoldItalicMT`, `TimesNewRoman`,
// `TimesNewRomanPS-BoldMT`, `CourierNew`, `CourierNewPSMT` and their style suffixes). None for any other name.
[[nodiscard]] std::optional<Standard14Font> standard14_from_base_font(std::string_view base_font) noexcept;

// The `CFF ` table of an OpenType program, or none when it has none or its directory runs outside the program: the
// charset and encoding a CFF-based OpenType font's glyph names come from (D01 T02 sections 30 and 43).
[[nodiscard]] std::optional<std::span<const uint8_t>> opentype_cff_table(std::span<const uint8_t> sfnt) noexcept;

[[nodiscard]] const Standard14Metrics& standard14_metrics(Standard14Font font) noexcept;

// The glyph of that name, or of that built-in code, or null.
[[nodiscard]] const Standard14Glyph* standard14_glyph_by_name(Standard14Font font, std::string_view name) noexcept;
[[nodiscard]] const Standard14Glyph* standard14_glyph_by_code(Standard14Font font, uint8_t code) noexcept;

// The Windows face a standard font is drawn with: `Courier New`, `Arial` or `Times New Roman` at the font's weight and
// style, `Symbol` for Symbol, `Segoe UI Symbol` for ZapfDingbats.
struct Standard14Face {
	std::wstring_view family;
	bool bold;
	bool italic;
};
[[nodiscard]] Standard14Face standard14_face(Standard14Font font) noexcept;

// The face as a `LoadedFont`, read from the system font file once per process and shared (a failure is not kept, so a
// later call tries again). A family or face Windows lacks is `UnsupportedFeature` naming it, never another font; a
// DirectWrite failure keeps its step and `HRESULT`; allocation failure is `OutOfMemory`.
[[nodiscard]] PdfResult<std::shared_ptr<const LoadedFont>> load_standard14_face(Standard14Font font) noexcept;

// A glyph to draw and the face it is in.
struct Standard14FaceGlyph {
	std::shared_ptr<const LoadedFont> face;
	uint16_t glyph{0};
};

// The glyph a standard font draws for a Unicode scalar, through its face's `cmap`; for `Symbol`, whose face has only a
// symbol `cmap`, a code point below 256 is the built-in code (looked up at `0xF000 + code`), and the one code the
// Windows face lacks (the Euro at 0xA0, which Adobe added to Symbol in 1997) is drawn from `Segoe UI Symbol` at
// U+20AC. None when no face has a glyph for it.
[[nodiscard]] PdfResult<std::optional<Standard14FaceGlyph>> standard14_face_glyph(
	Standard14Font font, char32_t code_point) noexcept;

namespace detail {

// The DirectWrite step a test makes fail (`ScopedFontLoaderFailure`).
enum class FontLoaderStage : uint8_t {
	Factory = 0,
	Loader = 1,
	Reference = 2,
	Face = 3,
	SystemFont = 4, // a Standard 14 face: the system collection, family, font and face
	SystemFontFile = 5, // a Standard 14 face: its file's reference key, loader and stream
};

// Makes the named DirectWrite step on the calling thread return `hresult`
// instead of running, for as long as the object lives. Test use only.
class ScopedFontLoaderFailure {
public:
	ScopedFontLoaderFailure(FontLoaderStage stage, int32_t hresult) noexcept;
	~ScopedFontLoaderFailure() noexcept;
	ScopedFontLoaderFailure(const ScopedFontLoaderFailure&) = delete;
	ScopedFontLoaderFailure& operator=(const ScopedFontLoaderFailure&) = delete;

private:
	FontLoaderStage previous_stage_;
	int32_t previous_hresult_;
};

// The number of font program owner objects alive in the process (the bytes
// DirectWrite still holds), for lifetime tests.
[[nodiscard]] int64_t live_font_program_owners() noexcept;

// The font programs the loader's registry names (one per live DirectWrite font), for lifetime tests.
[[nodiscard]] size_t registered_font_programs() noexcept;

// Forgets the Standard 14 faces loaded so far (the fonts callers hold stay valid), so a test can load one afresh.
void reset_standard14_faces() noexcept;

// How many Standard 14 face files this process has read, for tests of once-per-process loading.
[[nodiscard]] uint64_t standard14_face_reads() noexcept;

// While the object lives, `font`'s face is looked up under `family` on this thread, bypassing the shared faces: a
// family Windows lacks can be tried on any machine. Test use only.
class ScopedStandard14Family {
public:
	ScopedStandard14Family(Standard14Font font, std::wstring_view family) noexcept;
	~ScopedStandard14Family() noexcept;
	ScopedStandard14Family(const ScopedStandard14Family&) = delete;
	ScopedStandard14Family& operator=(const ScopedStandard14Family&) = delete;

private:
	std::optional<Standard14Font> previous_font_;
	std::wstring_view previous_family_;
};

} // namespace detail

} // namespace recto::pdf
