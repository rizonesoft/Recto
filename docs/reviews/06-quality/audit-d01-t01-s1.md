# Audit - D01 T01 §1: PdfResult<T>, PdfError, ErrorCode, D16 diagnostic ring and ETW

- **Audit section:** `D06 T05 §15` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §1` (`docs/reviews/d01-t01-s1.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `9cec0bd`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfError.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 4 tests.` (4 tests from 1 suite). The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T04:43:41Z
- Effort: medium
- Lines: 36
- Digest: bb1ee4e86506af44

### adversarial: needs-attention

1. **P2** `src/pdf/error.h:1252`: `RECTO_PDF_TRY` performs its final move outside exception handling. `prepare_extract()` only tests earlier moves; it cannot guarantee the next succeeds. Reproduce with a payload whose move constructor throws on the fourth move after resetting its counter: materialization and both preparation moves succeed, then extraction throws. Inside a `noexcept` caller this terminates instead of returning `OutOfMemory`. This is a static reproduction path, not an executed test.

2. **P2** `src/pdf/error.h:887`: `value_or(const T&)` returns a borrowed reference, creating a lifetime trap inconsistent with the promised expected-shaped interface. Reproduce:
   ```cpp
   PdfResult<std::string> r{PdfError{ErrorCode::IoError}};
   const auto& s = r.value_or(std::string(1024, 'x'));
   // s now refers to a destroyed string.
   ```
   Reading `s` on the following statement accesses a dead object. Existing tests use persistent fallback objects and miss this case.

### consistency: approve

No findings.

### integration: needs-attention

3. **P2** `src/pdf/error.cpp:786`: Provider initialization manually invokes the SDK’s internal `_tlgEnableCallback` to simulate an enabled consumer. Consequently, production code packs and submits every event even when nobody is listening, defeating D16’s low-overhead logging intent. Reproduce by recording diagnostics with no ETW session: `etw_diagnostic_events_emitted()` still increments through the write hook. The existing tests depend on this artificial enablement. Microsoft documents that normal `TraceLoggingWrite` skips event construction and submission without a listening consumer. [TraceLoggingWrite documentation](https://learn.microsoft.com/en-us/windows/win32/api/traceloggingprovider/nf-traceloggingprovider-traceloggingwrite)

### source-defect: needs-attention

4. **P3** `todo/01-engine/TODO-01-bridge-runtime.md:77`: ASan execution is presented as evidence of “zero data races.” ASan does not detect data races; Microsoft lists ThreadSanitizer separately. Reproduce the verification gap by inspecting the checkpoint and concurrent tests: they check counts and sequence ordering, without race instrumentation. This is an evidence defect, not proof of an actual race. [Microsoft sanitizer documentation](https://learn.microsoft.com/en-us/cpp/sanitizers/asan)

### design: advisory

Not applicable: the section owns no user surface.

### record: needs-attention

5. **P3** `docs/reviews/d01-t01-s1.json:21`: Round 3 is recorded as `clean`, while the corresponding report’s round 3 says `fixed` and describes two major fixes. Reproduce by comparing the receipt with `docs/reviews/01-engine/D01-T01-s1.md` under “Round 3.” The final blanket clean verdict does not identify a separate post-fix review round, and finding 1 contradicts its claimed extraction guarantee.

Checkpoint verification: the existing x64 Release executable passed all four `PdfError.*` tests plus `Support.AsanActiveWhenEnabled`, reporting `ASan=enabled`. This confirms the available binary’s results. It does not independently establish a warning-free rebuild from `944bf6b`; rebuilding was excluded by the no-file-changes constraint. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 1

`build/build.ps1 -Platform x64 -Configuration Debug -TestFilter PdfError.*` -> exit 0, `0 Warning(s)`, `PdfError.*` passing with `PdfError.ValueOrReturnsAValue` new; `python -m unittest tests.scripts.test_etw_registration` -> `OK`, and both its tests fail with the forced enablement put back into `ensure_etw_provider_registered`.

- Finding 1 (P2): a payload whose move may throw cannot be moved out of a result safely in a `noexcept` function, and the engine has four (types holding MSVC's `std::unordered_map`, whose move allocates). `RECTO_PDF_TRY` now requires a nothrow-movable payload at compile time, and its pre-move (`prepare_extract`) is gone; the new `RECTO_PDF_TRY_REF(name, expr)` binds a reference to the value where it already is, with no move. The four call sites (`crypto.cpp` twice, `document.cpp` twice) and the two test helpers use it; materialization stays `PdfResult`'s own move or copy, which returns a failure as `OutOfMemory` or `InternalError`, so the existing out-of-memory tests hold unchanged.
- Finding 2 (P2): `value_or` returns a value, as `std::expected` does, so a temporary fallback cannot dangle. On a const result it copies, so it exists only for payloads whose copy cannot throw; others use `try_value_or` (failures as `PdfError`) or the rvalue form (which moves). The old tests that took `value_or` results by reference on throwing payloads, the pattern the finding describes, now assert through `try_value_or`.
- Finding 3 (P2): production registration no longer fakes an enabled session; `TraceLoggingWrite` now builds and writes events only while a consumer listens (D16). `etw_provider_enabled()` reports the real state, and the counting tests opt in through `force_enable_etw_provider_for_testing()`. The regression guard is in the source (`tests/scripts/test_etw_registration.py`): production registration calls neither `_tlgEnableCallback` nor the seam, and the one `_tlgEnableCallback` call lives in the seam. A runtime check was tried first, a gtest death-test child asserting the provider stays disabled after registration; it passed twice, then failed consistently: in a clean process this machine reports the provider enabled at registration while `EnumerateTraceGuidsEx` lists no session enabling it (`EnableCount = 0`), so enablement here comes from machine state an unprivileged test cannot see, and a runtime assertion would be flaky. It was replaced by the source guard.
- Finding 4 (P3): a dated audit note in `D01 T01 §1` records that ASan does not establish race freedom, and the changes above.
- Finding 5 (P3), recorded rather than changed: `docs/reviews/d01-t01-s1.json` is one of the 25 frozen legacy receipts (`docs/reviews/legacy-receipts.json`), so its round 3 `clean`, against a report whose round 3 lists fixes, stays as written; this audit's own record supersedes it.
Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 104 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfError.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 5 tests.`; `plan-gates` now runs `tests.scripts.test_etw_registration`.