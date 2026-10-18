// Recto PDF engine (recto-pdf): native C++20 PDF 1.7 / 2.0 parser, filter
// pipeline, decryptor, content-stream interpreter, display-list builder,
// structured text extractor, and writer/organizer (foundation plan 3.1,
// section 4).
//
// Scaffold state (D00 T01 section 3): ABI version entry point only; the
// parser, filter pipeline and object model land in D01 T01.
#pragma once

#include "error.h"
#include "object.h"
#include "lexer.h"

namespace recto::pdf {

// PDF engine ABI version. Bumped whenever this header changes incompatibly.
[[nodiscard]] int abi_version() noexcept;

} // namespace recto::pdf
