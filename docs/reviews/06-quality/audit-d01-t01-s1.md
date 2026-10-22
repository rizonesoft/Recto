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
- Finding 3 (P2): production registration no longer fakes an enabled session; `TraceLoggingWrite` now builds and writes events only while a consumer listens (D16). `etw_provider_enabled()` reports the real state, and the counting tests opt in through `force_enable_etw_provider_for_testing()`. The regression guard is in the source (`tests/scripts/test_etw_registration.py`): production registration calls neither `_tlgEnableCallback` nor the seam, and the one `_tlgEnableCallback` call lives in the seam. **Corrected 2026-10-22 (after round 2):** a runtime check was written first and dropped on a wrong diagnosis. It "failed consistently" only because the mutation restores used `Copy-Item`, which keeps the source file's older timestamp, so MSBuild kept the mutant objects and every later run tested the forced-enable mutant. There was no machine state. The runtime check is restored in round 2's fixes.
- Finding 4 (P3): a dated audit note in `D01 T01 §1` records that ASan does not establish race freedom, and the changes above.
- Finding 5 (P3), recorded rather than changed: `docs/reviews/d01-t01-s1.json` is one of the 25 frozen legacy receipts (`docs/reviews/legacy-receipts.json`), so its round 3 `clean`, against a report whose round 3 lists fixes, stays as written; this audit's own record supersedes it.
Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 104 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfError.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 5 tests.`; `plan-gates` now runs `tests.scripts.test_etw_registration`.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T05:06:20Z
- Effort: medium
- Lines: 28
- Digest: eec4480974ebc3df

### adversarial: needs-attention

1. **P2** `src/pdf/error.cpp:983`, `src/pdf/error.cpp:1001`, `src/pdf/error.cpp:622`: Diagnostic access can terminate under allocation failure in MSVC Debug builds. `diagnostics_tail()` constructs `std::vector`, and `PdfError::format()` constructs `std::string`. Their default constructors are `noexcept` but allocate iterator proxies with Debug iterator checking enabled. Allocation failure therefore terminates before the surrounding catch can handle it. The empty-return paths have the same problem. Reproduce in Debug by installing a CRT allocation hook that rejects allocations, then calling `diagnostics_tail(0)` or formatting a preconstructed `PdfError`. This is source-verified against the installed MSVC headers, not an executed fault-injection test.

### consistency: approve

No findings.

### integration: approve

No additional findings.

### source-defect: approve

No additional findings.

### design: advisory

Not applicable: this section owns no user surface.

### record: needs-attention

2. **P3** `src/pdf/error.cpp:607`, `tests/recto-tests/pdf_error_tests.cpp:636`: ETW counters measure attempted writes before `EventWriteTransfer` returns. The tests assert those counters and intercepted payloads, but never check whether Windows accepted the writes. Consequently, they do not establish the ticked promise that events emit “without error.” Reproduce the verification gap by making `EventWriteTransfer` return a failure in a test harness: the counters and captured-payload assertions still pass. This is a coverage defect, not evidence that production writes currently fail.

Checkpoint verification: the existing x64 Release executable passed five `PdfError.*` tests and `Support.AsanActiveWhenEnabled`, reporting `ASan=enabled`. Both ETW source checks passed. A warning-free rebuild and the binary’s correspondence to `bd78922` were not independently established; rebuilding would modify files. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P2), recorded without a code change: confirmed against the installed headers (`MSVC/14.44.35207/include/vector:670`, `vector() noexcept(...)` calling `_Alloc_proxy`). With `_ITERATOR_DEBUG_LEVEL=2`, MSVC's STL allocates an iterator-debugging proxy inside its `noexcept` container constructors, so in a Debug build an allocation failure while constructing any `std::vector` or `std::string` in a `noexcept` function terminates. That is a property of the Debug STL, shared by every such function in the engine, not something `diagnostics_tail` or `PdfError::format` can avoid locally (even an empty return constructs a container). Release builds (what ships, and the ASan checkpoint) have no proxy and the catch paths hold. Debug builds are a developer configuration where terminating on allocation failure is acceptable.
- Finding 2 (P3): the hook now counts what Windows answers. `RectoTlgEventWriteTransfer` adds every non-zero `EventWriteTransfer` status to `etw_write_failures()`, and the counting tests (`ResultValueAndError`, `DiagnosticRingWraparound`) assert no write was refused. `set_etw_write_status_for_testing` lets `PdfError.EtwRefusedWritesAreCounted` inject a refusal and prove the counter sees it (one refused write counted, a following accepted write not). With the count removed from the hook, that test fails (`[  FAILED  ] PdfError.EtwRefusedWritesAreCounted`).
- Runtime ETW check restored: `PdfError.EtwProviderEnabledOnlyByASession` registers in a fresh process (a gtest death-test child) and requires `etw_provider_enabled()` to equal what the OS records for this process (`EnumerateTraceGuidsEx`, `TraceGuidQueryInfo`, `EnableCount` of this process's instance), so a real session listening on the machine cannot fail it. With the forced enablement put back into production registration it fails (`[  FAILED  ] PdfError.EtwProviderEnabledOnlyByASession`). `tests/scripts/test_etw_registration.py` stays as the CI-side source guard, since `plan-gates` builds no C++.
- Mutations now restore with `cp` and `touch`, and both mutants were re-run on rebuilt objects: x64 Debug `PdfError.*` -> `0 Warning(s)`, `[  PASSED  ] 7 tests.`

Gates after round 2: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 106 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfError.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 7 tests.`; `python -m unittest tests.scripts.test_etw_registration` -> `OK`.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T05:31:01Z
- Effort: medium
- Lines: 28
- Digest: 4a0fcdb726b4362a

### adversarial: needs-attention

1. **P3** `src/pdf/error.cpp:927`: Timestamps are sampled before acquiring the ring lock, so the promised chronological ordering can disagree with the stored timestamps. Reproduce by suspending thread A after timestamp acquisition, allowing thread B to record a later timestamp, then resuming A. The tail returns B before A, with decreasing timestamps. The concurrent test checks sequence numbers only. This is a static interleaving, not an executed reproduction.

### consistency: approve

No additional findings.

### integration: approve

No additional findings.

### source-defect: needs-attention

2. **P2** `src/pdf/error.cpp:961`: Every diagnostic, including malformed-document and resource-limit errors, implicitly uses ETW’s default **Verbose** level. An ordinary Error- or Warning-level collection therefore excludes these errors. Reproduce by enabling `Recto.Diagnostics` at `WINEVENT_LEVEL_ERROR`, then calling `make_pdf_error(ErrorCode::DamagedFormat, "bad token")`: the ring records it, but ETW filters out the diagnostic. The tests force Verbose enablement and miss this. Microsoft documents the omitted-level default as 5. [TraceLoggingWrite documentation](https://learn.microsoft.com/en-us/windows/win32/api/traceloggingprovider/nf-traceloggingprovider-traceloggingwrite)

### design: advisory

Not applicable: no user surface.

### record: needs-attention

3. **P3** `tests/recto-tests/pdf_error_tests.cpp:704`: The span/concurrency test still verifies attempted writes without asserting that `etw_write_failures()` stayed unchanged. The round-2 correction covers sequential diagnostics, but leaves span rejection undetected. Reproduce in a test harness by injecting `ERROR_INVALID_PARAMETER` during `EtwTraceLoggingProviderAndSpans`: captured packets and attempted-write counts still satisfy its assertions. This is a static coverage finding; no mutation was performed.

Checkpoint verification at `a20d27c`: the full filter aborted because Google Test’s death-test helper could not create a temporary file, reporting access denied. Excluding `EtwProviderEnabledOnlyByASession`, six `PdfError` tests and `Support.AsanActiveWhenEnabled` passed, reporting `ASan=enabled`. Both Python registration checks passed. A warning-free rebuild, binary/source correspondence, and the complete checkpoint were not independently established. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 3

- Finding 2 (P2): `Diagnostic` events are written at `WINEVENT_LEVEL_ERROR` (each one is a `PdfError` handed to a caller), so an Error-level session collects them; `SpanStart` and `SpanEnd` say `WINEVENT_LEVEL_VERBOSE` explicitly (tracing detail). The capture seam records the descriptor's level (`CapturedEtwEvent::level`), and `EtwTraceLoggingProviderAndSpans` asserts 2 for the diagnostic and 5 for the span. With the Error level removed from the diagnostic it fails (`Which is: '\x5' (5)` against 2).
- Finding 1 (P3): the timestamp is taken under the ring lock with the sequence number, so time follows sequence order. `ConcurrentDiagnosticRecording` asserts non-decreasing timestamps across the tail. **Corrected after round 4:** that assertion and the claim that time follows sequence order were wrong, since system time can be set backwards; see round 4's fixes.
- Finding 3 (P3): both the sequential and the concurrent parts of `EtwTraceLoggingProviderAndSpans` assert `etw_write_failures()` is unchanged, so a refused span write fails them (the counter's own sensitivity is proven by `EtwRefusedWritesAreCounted`).
- The reviewer's sandbox could not run `EtwProviderEnabledOnlyByASession` (gtest's death-test helper needs a temporary file and the read-only sandbox refused it); it runs in the gates below.

Gates after round 3: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 106 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfError.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 7 tests.`

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T05:43:25Z
- Effort: medium
- Lines: 28
- Digest: 1b49a5c44872ba28

### adversarial: needs-attention

1. **P2** `src/pdf/error.cpp:1009`, `src/pdf/error.cpp:1028`, `src/pdf/error.cpp:638`: The round-2 Debug allocation-failure defect remains. MSVC’s Debug `std::vector` constructor allocates an iterator proxy inside a `noexcept` constructor, so allocation failure terminates before the surrounding catch can recover. Even `diagnostics_tail(0)` constructs that vector. Reproduce in Debug with a CRT allocation hook rejecting allocations, then call `diagnostics_tail(0)` or format a preconstructed `PdfError`. Rechecked against the installed MSVC headers: `vector:672-673` and `xmemory:1231`. This is source-verified, not an executed fault-injection test. The acceptance statement at `docs/reviews/06-quality/audit-d01-t01-s1.md:106` leaves the defect in place; the governing contract declares no Debug exception to failure handling.

### consistency: approve

No additional findings.

### integration: approve

No additional findings.

### source-defect: needs-attention

2. **P3** `src/pdf/error.cpp:940`, `tests/recto-tests/pdf_error_tests.cpp:567`: Taking UTC timestamps under the lock does not guarantee non-decreasing timestamps. `GetSystemTimePreciseAsFileTime` follows system time, which can be corrected backwards. Reproduce in an isolated test environment by recording an entry, moving system time backwards, then recording another: sequence order remains correct, but the new timestamp assertion fails. The claim that timestamp ordering now holds “by construction” at `docs/reviews/06-quality/audit-d01-t01-s1.md:153` is therefore false. Preserve sequence-based ordering and distinguish UTC timestamps from monotonic elapsed time. This is a static reproduction, not an executed clock change. [Microsoft timing guidance](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)

### design: advisory

Not applicable: this section owns no user surface.

### record: advisory

The complete checkpoint was not independently established. The existing x64 Release executable passed six `PdfError` tests, excluding `EtwProviderEnabledOnlyByASession`; `Support.AsanActiveWhenEnabled` passed and reported `ASan=enabled`. Both Python ETW registration checks passed.

The death test requires temporary-file writes, and rebuilding modifies files, so neither was performed under the read-only instruction. These results do not establish binary correspondence to `fdd66bd` or a warning-free rebuild. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 4

- Finding 1 (P2): the reviewer is right that a findings-file note does not change the contract. The scope is now in the contract: `D01 T01 §1` carries a dated correction saying the allocation-failure contract holds in Release (shipped, and the ASan checkpoint) and that Debug's iterator-debugging proxies terminate on allocation failure inside `noexcept` STL constructors, with the cost of reversing it (`_ITERATOR_DEBUG_LEVEL=0` across Recto and every vcpkg dependency, or no standard container returned from a `noexcept` function). No code changes, since the behaviour belongs to MSVC's Debug STL, not to `error.cpp`.
- Finding 2 (P3): correct. The non-decreasing timestamp assertion is removed from `ConcurrentDiagnosticRecording`: entries are ordered by sequence number (still asserted), and `timestamp_utc_ms` is wall-clock time for people reading the log, which can move backwards when system time is corrected. The timestamp stays under the lock (it narrows the gap between the two), with a comment saying what orders the ring.

Gates after round 4: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 106 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfError.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 7 tests.`; `python scripts/todo-graph.py validate` -> `valid: 681 sections`.
