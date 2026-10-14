// Recto core: document actors, display lists and the tile scheduler
// (foundation plan 3.3, section 4). Scaffold state (D00 T01 section 3):
// the ABI version entry point only; the engine lands in D01 T02.
#pragma once

namespace recto::core {

// Core ABI version. Bumped whenever this header changes incompatibly.
// Backed by the bridge ABI so the link chain core -> bridge is exercised
// from the first build.
[[nodiscard]] int abi_version();

} // namespace recto::core
