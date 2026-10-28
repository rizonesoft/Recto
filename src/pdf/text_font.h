// A font resource's codes and widths for positioning text (ISO 32000-1 sections 9.2.4, 9.6 and 9.7; D01 T02 section
// 42): how a shown string splits into codes, each code's displacement, and the font's bounding box for a run's
// conservative bounds. Glyphs and outlines are section 43's. A `TextFont` is immutable once resolved.
#pragma once

#include "error.h"
#include "font_cid.h"
#include "object.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace recto::pdf {

class Document;

enum class TextFontKind : uint8_t {
	Simple,    // Type1, MMType1, TrueType: one byte per code, /Widths or the Standard 14 AFM widths
	Type3,     // one byte per code, /Widths through /FontMatrix
	Composite, // Type0: the CMap's codes, the CIDFont's /W and /W2
	Fallback,  // a font that could not be used: Helvetica's AFM widths
};

// One code of a shown string, its displacements in text space per unit of font size.
struct TextCode {
	uint32_t code = 0;
	uint8_t length = 0; // bytes the code took; 0 at the end of the string
	uint32_t cid = 0;   // a composite font's CID; the code otherwise
	double w0 = 0.0;    // horizontal displacement (9.2.4)
	double w1 = 0.0;    // vertical displacement, for /WMode 1 (9.7.4.3)
	double vx = 0.0;    // position vector, for /WMode 1
	double vy = 0.0;
	bool word_space = false; // the single-byte code 32, to which word spacing applies (9.3.3)
};

class TextFont {
public:
	// The font a `/Font` resource value names (direct or indirect). A font that is missing, damaged, of a kind this
	// reader does not read, or refused by a bound gives the fallback with its diagnostic recorded; `OutOfMemory` and
	// any other failure are returned, and stop the page as the interpreter's other resolver failures do.
	[[nodiscard]] static PdfResult<std::shared_ptr<const TextFont>> resolve(
		const Document& document, const PdfValue& font) noexcept;

	// Helvetica's AFM widths and box (a recorded default for a font that cannot be used), or `OutOfMemory`
	// when memory runs out building it.
	[[nodiscard]] static PdfResult<std::shared_ptr<const TextFont>> fallback() noexcept;

	[[nodiscard]] TextFontKind kind() const noexcept
	{
		return kind_;
	}
	[[nodiscard]] bool vertical() const noexcept
	{
		return vertical_;
	}
	// The font's bounding box in text space per unit of font size.
	[[nodiscard]] const PdfRect& bbox() const noexcept
	{
		return bbox_;
	}
	// False when the font declares a box of all zeros, which promises nothing (Table 112, 9.8): a run then takes a
	// box wide enough for any glyph.
	[[nodiscard]] bool bbox_known() const noexcept
	{
		return bbox_known_;
	}
	// The most codespace ranges one code is checked against: 1 for a one-byte kind, the CMap's range count for a
	// composite font (at least 1), for the interpreter's work budget.
	[[nodiscard]] size_t code_cost() const noexcept
	{
		return type0_ != nullptr ? std::max<size_t>(1u, type0_->cmap().codespace_count()) : 1u;
	}
	// The heap this font holds, for the interpreter's per-page font budget.
	[[nodiscard]] size_t memory_bytes() const noexcept
	{
		return sizeof(*this) + (type0_ != nullptr ? type0_->memory_bytes() : 0u);
	}
	// The composite font, for section 43's glyphs; null for every other kind.
	[[nodiscard]] const std::shared_ptr<const Type0Font>& type0() const noexcept
	{
		return type0_;
	}

	// The first code of `bytes` (length 0 when `bytes` is empty).
	[[nodiscard]] TextCode next(std::span<const uint8_t> bytes) const noexcept;

	TextFont() noexcept = default;

private:
	friend struct TextFontBuilder;

	TextFontKind kind_ = TextFontKind::Fallback;
	bool vertical_ = false;
	PdfRect bbox_{0.0, -0.25, 1.0, 1.0};
	bool bbox_known_ = true;
	std::array<double, 256> widths_{}; // one-byte kinds: each code's w0
	std::shared_ptr<const Type0Font> type0_{};
};

} // namespace recto::pdf
