// The content-stream interpreter (ISO 32000-1 sections 7.8 and 8; D01 T02
// section 3): operators over a page's decoded content, recorded into an
// immutable `DisplayList`. This section reads the graphics state, paths,
// painting, clipping and the device colour spaces; text, images and Form
// XObjects are `D01 T02 section 4`, other colour spaces sections 32 and 33,
// and the ceilings on hostile streams section 19.
//
// Thread-ownership contract (`frozen: true`): an interpretation runs on the
// `DocumentActor` thread that owns the `Document`, reads the document and its
// resource dictionaries without changing them, and mutates only its own
// graphics state stack and `DisplayListBuilder`.
#pragma once

#include "display_list.h"
#include "document.h"
#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace recto::pdf {

// The deepest `q` nesting the interpreter keeps; a `q` past it is ignored
// together with the `Q` that matches it, so the state stays balanced.
inline constexpr size_t kMaxGraphicsStateDepth = 64u;

// The most operands kept before an operator; past it the stack is dropped and
// the operator that follows is skipped (section 19 sets the real ceilings).
inline constexpr size_t kMaxContentOperands = 8'192u;

// Interprets page `page_index` (0-based) of `document` into a display list for
// its crop box in default user space. A page with no or empty `/Contents`
// gives an empty list; an index past the last page is `InvalidArgument`
// (`RefusalReason::PageIndex`); a content stream that fails to decode and a
// recording refusal return their errors, never a partial page.
[[nodiscard]] PdfResult<std::shared_ptr<const DisplayList>> interpret_page(
	const Document& document,
	size_t page_index,
	const DisplayListLimits& limits = {}) noexcept;

// Interprets `content` with `resources` (a resolved resource dictionary, its
// entries resolved through `document`) into a display list for `page_box`.
[[nodiscard]] PdfResult<std::shared_ptr<const DisplayList>> interpret_content(
	const Document& document,
	std::span<const uint8_t> content,
	const PdfDict& resources,
	const PdfRect& page_box,
	const DisplayListLimits& limits = {}) noexcept;

} // namespace recto::pdf
