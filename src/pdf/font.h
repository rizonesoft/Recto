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
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
	// DirectWrite. Always succeeds for a font without `glyf` outlines.
	[[nodiscard]] PdfResult<void> check_glyph(uint16_t glyph) const noexcept;

	// The DirectWrite face and the validated glyf tables, defined in font.cpp.
	struct DWriteState;
	struct GlyfTables;

private:

	friend PdfResult<std::shared_ptr<const LoadedFont>> load_embedded_font(
		std::shared_ptr<const std::vector<uint8_t>> program,
		FontProgramFormat format) noexcept;

	LoadedFont(
		LoadedFontKind kind,
		std::shared_ptr<const std::vector<uint8_t>> program,
		uint16_t units_per_em,
		uint16_t glyph_count,
		std::unique_ptr<const DWriteState> dwrite,
		std::unique_ptr<const GlyfTables> glyf) noexcept;

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
};

// Loads an embedded font program in memory. DirectWrite loads it when it can;
// a TrueType program DirectWrite rejects as a font is read through its own
// `glyf` outlines (`OutlineOnly`). A DirectWrite resource failure (memory,
// or any status that is not a font rejection) returns its typed error with
// the step and `HRESULT` kept, and tries no fallback.
[[nodiscard]] PdfResult<std::shared_ptr<const LoadedFont>> load_embedded_font(
	std::shared_ptr<const std::vector<uint8_t>> program,
	FontProgramFormat format) noexcept;

namespace detail {

// The DirectWrite step a test makes fail (`ScopedFontLoaderFailure`).
enum class FontLoaderStage : uint8_t {
	Factory = 0,
	Loader = 1,
	Reference = 2,
	Face = 3,
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

} // namespace detail

} // namespace recto::pdf
