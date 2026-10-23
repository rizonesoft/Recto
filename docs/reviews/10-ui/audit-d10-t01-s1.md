# Audit - D10 T01 §1: `recto-ui` static library, module boundary and headless render harness

- **Audited section:** `D10 T01 §1` (`docs/reviews/d10-t01-s1.json`, stamped in `994a4e0a56c0265c7b783aa0ed11f0c8a6514c70` on implementation `b5ba9aa70715d5dc423d08e13ee00d3bbc6a2a6c`, 6 Sol rounds under the earlier Sonnet writer).
- **Auditor:** Opus 5.5 (the sole writer from 2026-10-23), unattended run.
- **Date:** 2026-10-23
- **Audit candidate:** `41ce2e31209960727e7e4d776f2e202a6f543ffc..798d08715471fab8df00d266ffef339a4bac26db` restricted to the files the section owns (`src/ui/`, `tests/recto-tests/ui_*`, `tests/recto-tests/recto-tests.vcxproj`, `Recto.sln`, `build/build.ps1`, `scripts/release-notes.py`, `tests/scripts/test_release_notes.py`). `git log 994a4e0..798d087` over those paths is empty, so the audited code is exactly the stamped implementation `b5ba9aa`.

## Self-review (fresh read of the full diff `41ce2e3..994a4e0`)

Every ticked checklist item maps to code and a test:

- `src/ui/recto-ui.vcxproj` static library, x64 and ARM64, in `Recto.sln`, `ProjectReference` from `recto-tests`: present; `/W4 /WX` and C++23 come from `Directory.Build.targets` (the dated correction in the section).
- `src/ui/pch.h`, `pch.cpp`, `include/recto/ui/ui.h` (`kVersion`, `version()`): `Ui.Project.LibraryLinks` calls the library function, so a missing link fails the build.
- `Ui.Project.IncludeBoundary`: scans the real tree and a scratch copy with 17 planted forbidden shapes (path, bare engine header name, comment inside the directive, splice, BOM, raw string, computed include, multi-line comment) and asserts the exact set of findings, so it fails both on a miss and on a false positive.
- `Ui.Project.NoWinRtUiTypes`: whitespace- and comment-insensitive token scan of every file under `src/ui`, with four planted shapes.
- `render_to_bitmap` (`tests/recto-tests/ui_render_harness.cpp`): WIC `32bppPBGRA` bitmap, `CreateWicBitmapRenderTarget` at `96 * dpi_scale`, explicit stride, `min_stride` padding filled with a sentinel, absurd stride refused before allocation.
- `Ui.Project.HarnessStride`, `HarnessSolid`, `HarnessScalesEffectivePixels`, `HarnessRejectsAbsurdStride`: each carries its own falsifier (the width-times-4 reading must fail, a different fill must differ at (16,16)).
- Wildcard `ClCompile Include="ui_*_tests.cpp"`: present in `tests/recto-tests/recto-tests.vcxproj`; `ui_test.h` `RECTO_UI_TEST` gives three-part `Ui.<Area>.<Name>` names, with `DISABLED_` kept in front (`Ui.Project.DisabledMarkerStaysAtTheFront`).
- `scripts/release-notes.py` `GTEST_ID` accepts `Ui.<Area>.<Name>` before the Python id pattern is tried, with `tests/scripts/test_release_notes.py` `UiTestIdTest` covering listed, unlisted and true Python ids.

Failure modes for this area (`AGENTS.md`) checked: engine/UI include boundary (both directions, by path and by bare engine name); a bitmap stride assumed to be `width * 4` (falsified in `HarnessStride`); a test that cannot fail (every test has a planted or altered counterpart). Threading: the harness uses its own single-threaded D2D factory and a balanced per-call COM scope that keeps a caller's existing apartment; no engine thread is touched. Architecture check: `recto-ui` is a leaf static library on the Windows SDK only, the D13 boundary holds, and no foundation plan decision is revised.

Advisory observations (not regressions; weighed against the section's contract, which names `#include` scanning only):

1. The engine-to-UI direction matches paths with a `ui` component but not a bare recto-ui header name, while the UI-to-engine direction matches both. Today a bare name could not resolve, because no engine project has `src\ui\include` on its include path; nothing asserts that project-level fact.
2. `NoWinRtUiTypes` covers `Microsoft.UI` only, not the system `Windows.UI.Xaml` namespace. The section's contract names `Microsoft.UI`; the wider XAML residue guard is `D10 T01 §2`'s `Ui.App.NoWinUiResidue`.
3. A finding's line number after a comment that spans lines counts lines of the comment-free text (documented in `logical_source`).

## Completion review: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T01:14:16Z
- Candidate: b5ba9aa70715d5dc423d08e13ee00d3bbc6a2a6c
- Effort: xhigh
- Lines: 103
- Digest: 954b779bc4b145f3

Incomplete for its consumers, and it does not yet demonstrate an advantage over mature toolkit infrastructure.

All source anchors below refer to `b5ba9aa`. No files were modified. Six file-free C++ checks and three UI release-ID tests passed using existing binaries. Builds and ASan were not rerun; their wiring was inspected statically.

**Part 1: completeness**

1. **Project matrix: `present`.** x64 and ARM64, Debug and Release, are declared as static-library configurations in `src/ui/recto-ui.vcxproj:9`, with solution mappings at `Recto.sln:54`. CI builds all four configurations at `.github/workflows/build.yml:114`.

2. **Compiler settings and PCH: `present`.** v145 comes from `Directory.Build.props:7`; C++23 and `/W4 /WX` from `Directory.Build.targets:12`. PCH wiring is at `src/ui/recto-ui.vcxproj:43`. `test_every_module_compiles_as_cpp23_with_warnings_as_errors` evaluates every project/configuration.

3. **Public include and actual library linkage: `present`.** `src/ui/include/recto/ui/ui.h:8` declares the version API; `Ui.Project.LibraryLinks` calls the compiled implementation. The test passed.

4. **Normal build and ASan wiring: `present`.** `build/build.ps1:89` includes `recto-ui` in the instrumentation-state cleanup list, and line 119 includes it in ASan build targets. `Directory.Build.targets:26` supplies x64 instrumentation. The tests project references the library at `tests/recto-tests/recto-tests.vcxproj:93`.

5. **Regression protection for UI instrumentation: `missing-in-scope`.** No check proves that `recto-ui` objects themselves remain instrumented or that its ASan rebuild-state wiring remains intact. `Support.AsanActiveWhenEnabled`, at `tests/recto-tests/support_tests.cpp:700`, verifies the test executable's compilation state. Add evaluated UI compile-flag checks and a regression covering instrumentation-state transitions.

6. **UI-to-engine includes in supported source files: `present`.** `ui_includes_engine` checks path components and engine header basenames at `tests/recto-tests/ui_project_tests.cpp:230`. `Ui.Project.IncludeBoundary` plants forbidden includes, including comments, continuations, BOMs and computed includes.

7. **Engine-to-UI includes by bare header name: `missing-in-scope`.** `engine_includes_ui`, at `tests/recto-tests/ui_project_tests.cpp:245`, rejects `ui` path components and `recto_ui.h`, but misses the actual public header name `ui.h`. It also cannot recognize future bare toolkit header names. Build a toolkit header inventory and add reverse-direction fixtures.

8. **Complete, reliable source scanning: `missing-in-scope`.** The extension allowlist at `tests/recto-tests/ui_project_tests.cpp:44` omits includable files such as `.ipp`, `.inc` and `.hxx`. Multiline raw strings are copied intact at line 142, then scanned line by line, allowing harmless embedded `#include` text to become a finding. Failed reads become empty text at line 71. Add fixtures for these cases and fail explicitly when inspection fails.

9. **Project include-path boundary, both directions: `missing-in-scope`.** The scanner at `tests/recto-tests/ui_project_tests.cpp:195` never evaluates `AdditionalIncludeDirectories`, inherited property sheets or per-file settings. Adding an engine include directory to UI, or a UI directory to an engine project, is unguarded. Test effective paths in all four configurations.

10. **Project-reference boundary, both directions: `missing-in-scope`.** `boundary_findings`, at `tests/recto-tests/ui_project_tests.cpp:258`, never examines project references. Forbidden direct or transitive dependencies can be added without failing it. Add configured dependency-graph checks with planted references.

11. **WinRT UI token guard: `present`.** `NoWinRtUiTypes`, at `tests/recto-tests/ui_project_tests.cpp:390`, checks dotted and C++ namespace spellings, including whitespace-separated forms, and contains negative fixtures.

12. **Headless callback API and bitmap ownership: `present`.** `tests/recto-tests/ui_render_harness.h:27` defines owned bytes, dimensions and stride; line 34 defines the borrowed draw context. The implementation uses software WIC rendering without an HWND or D3D device at `ui_render_harness.cpp:84`.

13. **Basic DPI semantics: `present`.** Target DPI is `96 * dpi_scale`, and context dimensions are expressed in epx at `tests/recto-tests/ui_render_harness.cpp:99`. `HarnessScalesEffectivePixels` verifies a 200-percent render and passed.

14. **Transparent clear, explicit stride and PBGRA output: `present`.** Clear occurs at `tests/recto-tests/ui_render_harness.cpp:116`; WIC stride and buffer size are checked at line 126; row copies use WIC stride at line 150. `HarnessSolid` checks transparent surrounding pixels; `HarnessStride` proves consumers cannot assume packed rows. Both passed.

15. **Interoperation with existing comparison helpers: `missing-in-scope`.** The harness returns premultiplied BGRA, but `HarnessSolid` passes it directly to `expect_rgba_equal` at `tests/recto-tests/ui_project_tests.cpp:441`. Equality remains valid, but diagnostics label BGRA bytes as RGBA (`test_support.cpp:575`). Direct use with `compute_ssim` would apply red luminance weight to blue (`test_support.cpp:625`). Provide an explicit format adapter or format-aware comparison API, tested with unequal red/blue values and partial transparency.

16. **Complete input preflight: `missing-in-scope`.** `tests/recto-tests/ui_render_harness.cpp:78` accepts positive infinity and does not check whether calculated DPI or epx extents remain finite. Invalid `min_stride` is rejected only at line 142, after allocation and callback execution. Validate all options before drawing; test that rejected requests never invoke the callback.

17. **Basic COM/render error reporting: `present`.** `check`, at `tests/recto-tests/ui_render_harness.cpp:31`, throws with the failing operation and hexadecimal HRESULT. `EndDraw` is checked at line 120.

18. **Failure-path regression coverage: `missing-in-scope`.** The only failure test is `HarnessRejectsAbsurdStride`, at `tests/recto-tests/ui_project_tests.cpp:508`. Nothing exercises an actual deferred drawing failure, checks operation/HRESULT reporting, or verifies recovery after a callback throws. Add those cases.

19. **Apartment-preserving implementation: `present`.** `ComScope`, at `tests/recto-tests/ui_render_harness.cpp:42`, balances successful initialization and leaves an existing STA unchanged when initialization returns `RPC_E_CHANGED_MODE`.

20. **Apartment and thread regression coverage: `missing-in-scope`.** No test verifies fresh threads, existing STA/MTA callers, nested initialization counts, exception cleanup or concurrent independent renders. These are necessary before Dispatcher and Window tests rely on this behavior.

21. **Automatic compilation and normal test registration: `present`.** The wildcard at `tests/recto-tests/recto-tests.vcxproj:80` includes future `ui_*_tests.cpp` files. `RECTO_UI_TEST`, at `ui_test.h:47`, registers three-part IDs. Listing and filtering the existing binary selected the expected tests.

22. **Disabled-test interaction with area filters: `missing-in-scope`.** `registered_name`, at `tests/recto-tests/ui_test.h:36`, changes `Project.DISABLED_Probe` into `DISABLED_Project.Probe`. Consequently, `Ui.Project.* --gtest_also_run_disabled_tests` still excludes it. The existing test checks only the constructed string. Define and document supported area filtering, then test actual default execution, explicit disabled execution, listing and release-ID exclusion.

23. **Unique registered IDs across translation units: `missing-in-scope`.** The macro's functions and registration variables are file-local (`tests/recto-tests/ui_test.h:48`). Two files can register the same ID without a linker collision; no uniqueness assertion exists. Reject duplicate IDs and test that rejection.

24. **Nonempty direct checkpoint enforcement: `missing-in-scope`.** Direct checkpoint commands lack a zero-selection failure mechanism. An unmatched `Ui.Project.NoSuchAuditTest` filter returned exit 0 during this audit. `build/build.ps1:162` protects its wrapper, but the section's direct executable checkpoint does not. Require nonzero exit for empty selections and assert expected registrations.

25. **Release-note recognition of normal UI IDs: `present`.** `scripts/release-notes.py:48` recognizes three-part UI IDs; its list parser preserves dotted cases at line 206. All three `tests.scripts.test_release_notes.UiTestIdTest` tests passed.

26. **Pixel-contract regression matrix: `missing-in-scope`.** Current fixtures are square, opaque and principally 100/200 percent (`tests/recto-tests/ui_project_tests.cpp:411`). Add nonsquare renders, 125/150/175-percent scales, both epx dimensions, partial-alpha expectations, and a source-readback fixture whose input stride exceeds packed width. The current padded-output test does not exercise padded WIC input readback.

27. **Production graphics integration and Element overload: `missing-out-of-scope`.** Explicitly deferred by `todo/10-ui/TODO-01-ui-foundation.md:93`: `GraphicsDevice::create_headless` belongs to §5, and the Element overload belongs to §6. These remain required consumer integration work.

28. **Golden references and design-board comparison: `missing-out-of-scope`.** Manifest loading, scaling, themes, SSIM thresholds and reviewed updates belong to D10 T01 §20 (`todo/10-ui/TODO-01-ui-foundation.md:644`), with board coverage in D06 T04 §1.

**Part 2: excellence**

Comparison: Chromium's [DEPS/checkdeps layering rules](https://chromium.googlesource.com/chromium/src/+/55ad8f69544f3ce40e852da9b997aa164dcaddbb/buildtools/checkdeps/), [GN transitive dependency assertions](https://gn.googlesource.com/gn/+/main/docs/reference.md#var_assert_no_deps), [Skia Gold failure diagnostics](https://chromium.googlesource.com/chromium/src/+/main/docs/testing/pixel_tests.md), and Microsoft's [Direct2D resource-reuse guidance](https://learn.microsoft.com/en-us/windows/win32/direct2d/improving-direct2d-performance). Targets below are proposed acceptance targets, not claimed competitor measurements.

1. **Per-render setup cost: `in-scope`.** Every call initializes COM and creates WIC/D2D factories and a render target (`ui_render_harness.cpp:82`). In 31 existing Release-binary iterations containing one 64×64 render each, cold execution took 21 ms, warm median 5 ms and warm p95 approximately 9 ms, using GoogleTest's rounded timings. Target: reusable per-thread factory lifetime, preserved caller apartment, and warm median ≤1 ms/p95 ≤2 ms on a documented reference machine, with a repeatable benchmark.

2. **Layering enforcement is too late and filter-dependent: `in-scope`.** Unlike independently runnable dependency checks, enforcement requires building and selecting a native test. Target: reject forbidden source, effective include-path and dependency-graph changes before native compilation in CI, independently of GoogleTest filters, with negative fixtures for both directions.

3. **Source diagnostics do not reliably identify physical locations: `in-scope`.** Comment removal and splicing change line numbering (`ui_project_tests.cpp:138`); token findings deliberately report line zero (`:293`). Target: every finding names the original file, physical line, dependency/token and violated rule, verified with multiline-comment and continuation fixtures.

4. **Render failures discard available context: `in-scope`.** `EndDraw()` omits its error-tag outputs (`ui_render_harness.cpp:120`), although [Direct2D exposes tags identifying failed drawing operations](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1rendertarget-enddraw). Target: failures include operation, HRESULT, tags, dimensions, scale and requested stride; an injected drawing error verifies every field.

5. **Cross-machine text determinism is unproven: `out-of-scope`, D10 T01 §10/§20.** Software rendering removes GPU variability, and the current alpha mode already defaults text to grayscale, according to [Direct2D's alpha-mode rules](https://learn.microsoft.com/en-us/windows/win32/direct2d/supported-pixel-formats-and-alpha-modes). It does not control font versions or text rendering parameters. Target: a declared rendering profile, recorded OS/font versions, and identical text-fixture hashes across repeated runs on two controlled machines at 100/150/200 percent.

6. **Consumers lack one-call visual assertions and inspectable failure artifacts: `out-of-scope`, D10 T01 §20 and D06 T04.** Mature pixel infrastructure exposes expected and actual images directly on failure. Here consumers must assemble drawing, comparison and diagnostics. Target: one assertion produces actual, expected and difference PNGs plus a report containing scale, format, stride, threshold and rendering profile, without overwriting approved references.

**Consolidated gap list**

1. `missing-in-scope` P1.5: Verify UI-object ASan instrumentation and instrumentation-state transitions.
2. `missing-in-scope` P1.7: Detect reverse includes by actual toolkit header basenames.
3. `missing-in-scope` P1.8: Cover additional source extensions, raw strings and failed reads correctly.
4. `missing-in-scope` P1.9: Enforce effective project include paths in both directions.
5. `missing-in-scope` P1.10: Enforce direct and transitive project-reference boundaries.
6. `missing-in-scope` P1.15: Provide correct PBGRA comparison and diagnostic interoperability.
7. `missing-in-scope` P1.16: Validate finite geometry and all options before drawing.
8. `missing-in-scope` P1.18: Test deferred render failures, callback exceptions and recovery.
9. `missing-in-scope` P1.20: Test apartment preservation, balancing and independent concurrent renders.
10. `missing-in-scope` P1.22: Resolve and integration-test disabled-test area filtering.
11. `missing-in-scope` P1.23: Reject duplicate registered test IDs.
12. `missing-in-scope` P1.24: Fail direct checkpoints when no tests are selected.
13. `missing-in-scope` P1.26: Expand DPI, dimensions, alpha and source-stride regression coverage.
14. `missing-out-of-scope` P1.27: Complete GraphicsDevice and Element integration in §5/§6.
15. `missing-out-of-scope` P1.28: Complete golden and board comparisons in §20/D06 T04.
16. `in-scope` P2.1: Reduce and benchmark per-render setup cost.
17. `in-scope` P2.2: Run comprehensive layering enforcement before native builds.
18. `in-scope` P2.3: Report accurate physical source locations and violated rules.
19. `in-scope` P2.4: Include drawing tags and render parameters in failures.
20. `out-of-scope` P2.5: Establish controlled cross-machine text determinism in §10/§20.
21. `out-of-scope` P2.6: Deliver one-call visual assertions and failure artifacts in §20/D06 T04.

COMPLETION: gaps

## Checkpoint re-run

At `798d087`, in a clean worktree (`git worktree add --detach <scratchpad>/audit-tip HEAD`): `.\build.cmd -Platform x64 -Configuration Debug -SkipTests` -> `0 Warning(s)`, `0 Error(s)`; `recto-tests.exe --gtest_filter=Ui.Project.*` -> `[  PASSED  ] 8 tests.`; `msbuild Recto.sln /p:Configuration=Debug /p:Platform=ARM64 /t:recto-ui` -> exit 0, `recto-ui.lib` built. The stamp's checkpoint evidence holds.

## Dispatch stopped (no round recorded)

2026-10-23: the first round dispatch was stopped by the session host because the machine ran critically low on memory, together with a concurrent ASan build; the dispatcher recorded nothing. The audit verdict waits for a Sol round. Owner: the next session on this audit, as soon as memory allows; due 2026-10-24. The completion review above lists 13 `missing-in-scope` and 4 `in-scope` gaps; none is a regression of the stamped contract, so the working view is that the stamp holds and the gaps go to a new hardening section through `add-todo`, to be confirmed by the round.

A second dispatch at 2026-10-23T01:33:16Z returned a review, but `review-dispatch.py` refused to record it (exit 2) because the heading above was hand-written in the round format; the heading was renamed and the round dispatched again.

## Round 1: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T01:44:54Z
- Effort: xhigh
- Lines: 37
- Digest: 84afd4d78d7f4035

### adversarial: needs-attention

1. **P1**, `tests/recto-tests/ui_project_tests.cpp:73`: UTF-16 source bypasses both guards. The binary reader preserves interleaved NUL bytes, while `logical_source` handles only a UTF-8 BOM. Reproduce with BOM-marked UTF-16 files containing a forbidden include under `src/core`, or `Microsoft::UI` references under `src/ui`. Byte-level probes found zero include or token matches. MSVC supports BOM-marked UTF-16 source, including when `/utf-8` supplies the fallback encoding. [Microsoft documentation](https://learn.microsoft.com/en-us/cpp/build/reference/utf-8-set-source-and-executable-character-sets-to-utf-8?view=msvc-170).

### consistency: needs-attention

2. **P1**, `tests/recto-tests/ui_project_tests.cpp:197`: Both directive patterns require literal `#`, allowing `%:include` to bypass `IncludeBoundary`. Reproduce in an engine source with `%:include "../ui/include/recto/ui/ui.h"`. The lexer preserves it and neither regex matches. A Clang-cl `/Zs` probe accepted `%:include` targeting the actual toolkit header. `%:` is the standard alternative spelling of `#`. [C++ specification](https://eel.is/c++draft/lex.digraph).

### integration: advisory

Completion-review item 22 is confirmed: `tests/recto-tests/ui_test.h:36` moves disabled tests outside their normal `Ui.<Area>.*` filter, including when `--gtest_also_run_disabled_tests` is supplied. This is the previously recorded gap, not an additional finding.

### source-defect: needs-attention

3. **P1**, `tests/recto-tests/ui_project_tests.cpp:127`: The lexer treats `//` inside an angle-bracket header name as a comment, erasing the closing `>` and hiding the include from both patterns. Reproduce in `src/ui` with:

   ```cpp
   #include <../../pdf//error.h>
   ```

   Clang-cl `/Zs /W4 /WX /permissive- /EHsc`, using only the toolkit’s existing `src/ui/include` directory, accepted this engine header. Compile-time assertions against the extracted candidate lexer confirmed the truncation. Header-name tokens require distinct lexical handling. [C++ specification](https://eel.is/c++draft/lex.header).

### design: advisory

Not applicable, `Surface: no`.

### record: needs-attention

4. **P2**, `docs/reviews/10-ui/D10-T01-s1.md:147`: The justification that absent cross-module include directories prevent an architecture breach is false. Finding 3 reaches an engine header through the declared toolkit include directory. Quoted relative and absolute includes also do not require adding an engine include directory. Consequently, this paragraph does not justify treating scanner bypasses as non-breaches. [MSVC include resolution](https://learn.microsoft.com/en-us/cpp/preprocessor/hash-include-directive-c-cpp?view=msvc-160).

The supplied gate evidence is not contradicted. Read-only verification passed six existing-binary UI tests and three release-ID tests. Full builds and scratch-writing scanner tests were not rerun.

STAMP: does-not-hold. The stamped boundary and WinRT-token checks accept forbidden source, despite passing their existing fixtures.

HIGHEST: P1

OVERALL: needs-attention
