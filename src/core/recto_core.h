// Recto core: document actors, display lists, the tile scheduler, built-in
// OCR, and native conversions (foundation plan 3.3, section 4). Scaffold
// state (D00 T01 section 3): the ABI version entry point only; the engine
// lands in D01 T02.
#pragma once

namespace recto::core {

// Core ABI version. Bumped whenever this header changes incompatibly.
// Backed by the recto-pdf ABI so the link chain core -> pdf is exercised
// from the first build.
[[nodiscard]] int abi_version();

} // namespace recto::core
