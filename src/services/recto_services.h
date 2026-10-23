// Recto services: presentation, conversion jobs, Office conversion,
// settings (foundation plan 3.1). No UI types below this line. Scaffold state
// (D00 T01 section 3): the ABI version entry point only.
#pragma once

namespace recto::services {

// Services ABI version, backed by the core ABI so the chain
// services -> core -> pdf is exercised from the first build.
[[nodiscard]] int abi_version();

} // namespace recto::services
