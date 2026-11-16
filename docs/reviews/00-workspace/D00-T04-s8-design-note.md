## Plan review: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-11-16T04:41:39Z
- Candidate: ba97969c09c68a759ee2c20a0f74bed512c969b2
- Effort: medium
- Lenses: plan
- Seconds: 97
- Lines: 12
- Digest: 97cf19c9c1d220c0

1. **P1: ASan Debug retains incompatible CMake defaults.** Section line 290; design note lines 11, 19 and 26. Adding `/fsanitize=address` does not disable CMake’s default Debug runtime checks or incremental linking, both incompatible with MSVC ASan. Specify their removal through the appropriate CMake settings and require real Debug and Release ASan builds and test runs. The current CI ASan job exercises Release only, so it cannot catch the Debug failure. See [CMake runtime-check defaults](https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_CHECKS.html) and [Microsoft’s ASan restrictions](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues?view=msvc-170).

2. **P1: Boundary-check execution is not wired into the advertised direct build.** Section lines 294 and 301; design note line 42. The design generates manifests and extends the checker, but never specifies a build dependency that executes it and propagates failure. Today, `build/build.ps1` invokes the check separately. A direct `cmake --build --preset x64-debug` or Visual Studio build could therefore succeed with a forbidden dependency. Add automatic enforcement to these entry points and prove that the build command itself fails after introducing the forbidden include, rather than proving only that a separately invoked checker rejects it.

3. **P2: Visual Studio bypasses required initialization.** Section lines 290–292; design note lines 25, 31–32 and 50. Pool sizing and refusal live exclusively in `build.ps1`, while Open Folder invokes CMake directly. The note supplies no equivalent initialization for that supported path, including the externally selected target architecture for ARM64. Define how direct presets obtain valid memory limits, prerequisite tools and the correct compiler environment; test Open Folder configuration and builds, including ARM64 selection and low-memory refusal. [Microsoft documents the preset architecture and environment requirements.](https://learn.microsoft.com/en-us/cpp/build/cmake-presets-vs?view=msvc-170)

4. **P2: The switch leaves the governing build decision contradictory.** Section line 298; design note lines 14 and 54. Foundation decision D8 still declares MSBuild the primary build system. No step updates it when CMake becomes the default. Add a dated D8 amendment identifying CMake/Ninja as primary, MSBuild’s temporary parity role and the reversal cost.

LENS plan: findings
HIGHEST: P1
PLAN: gaps
OVERALL: needs-attention

- Implemented: ASan Debug defaults (#1), build-graph boundary check (#2), direct preset entry (#3), D8 amendment (#4) in the design note and D00 T04 §8, re-reviewed in D00-T04-s8-design-note-r2.md
