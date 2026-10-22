# Audit - D01 T01 §4: Bounded recursive-descent indirect object parser (src/pdf/parser.cpp)

- **Audit section:** `D06 T05 §18` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §4` (`docs/reviews/d01-t01-s4.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `2caf1d9`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfParser.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 8 tests.` (8 tests from 1 suite). The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T06:58:54Z
- Effort: medium
- Lines: 28
- Digest: 541043a20990ab41

## adversarial: needs-attention

1. **P1: Container limits do not bound total parser allocation.** `src/pdf/parser.cpp:564`, `src/pdf/parser.cpp:609`: limits apply separately to each container; there is no shared object-size or allocation budget. Reproduce by passing `"[" + ("[" + "0 " * 1000000 + "]") * 100 + "]"` to `parse_direct_object()`. This approximately 200 MB input creates 100 million `PdfValue` instances while staying at depth two and within every container limit. Parsing continues until completion or allocator failure, rather than rejecting at an explicit byte ceiling. This violates foundation plan section 9’s bounded-parsing requirement and permits document-driven memory exhaustion. **Static finding; the memory-exhaustion probe was not executed.**

## consistency: needs-attention

2. **P3: Parser instrumentation violates the stated local-state freeze contract.** `src/pdf/parser.cpp:33`: `g_last_terminator_scan_steps` is mutable thread-local state shared by parser instances on the same thread. Reproduce by parsing a fallback stream with parser A, then another with parser B: A’s observable scan counter is overwritten. This is not a cross-thread race, but the contract that all parser state is local does not hold.

## integration: needs-attention

Finding 1 reaches document loading through `src/pdf/xref.cpp:1862`: `XrefTable::read_indirect_object_with_visited()` constructs the parser over document bytes without supplying an aggregate allocation budget. A referenced indirect array can therefore trigger the same exhaustion path.

## source-defect: approve

No additional specification or platform defect established.

## design: advisory

Not applicable: this section owns no user surface.

## record: needs-attention

3. **P3: The review records a false freeze-check result.** `docs/reviews/01-engine/D01-T01-s4.md:34` claims “zero static or thread-shared mutable state,” despite the thread-local counter described in finding 2. Reproduce by comparing that assertion with `src/pdf/parser.cpp:33`.

Checkpoint verification: the existing x64 Release binary passed all eight `PdfParser.*` tests and `Support.AsanActiveWhenEnabled`, which reported `ASan=enabled`. The parser and its tests have no diff between the recorded `2caf1d9` checkpoint and `ecf1a43`. A fresh build, its zero-warning claim, and ARM64 compilation were not independently rerun because this audit permits no file modifications.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P1): correct. The ceilings applied per container, so `[` followed by 100 arrays of 1,000,000 integers stayed within every limit while creating about 100 million values. `Parser` now counts the values one top-level parse creates (`parse_direct_object`, `parse_array`, `parse_dictionary`, `parse_indirect_object` each restart the count) and refuses past `kMaxParsedValues` (equal to `kMaxContainerElements`, so no object costs more than the largest single container already could) with `ErrorCode::DamagedFormat`, the code the container ceilings use. `xref.cpp`'s parser picks up the default. `PdfParser.TotalValueBudgetSpansContainers` refuses 16 values at a budget of 15 across nested arrays, a dictionary and an indirect object, accepts exactly 16, and restarts per parse. With the check disabled it fails.
- Finding 2 (P3): correct. The terminator scan count was a `thread_local` shared by every parser on a thread. It is now a `Parser` member read through `Parser::last_stream_terminator_scan_steps()`; `detail::last_stream_terminator_scan_steps` and the free function's write are gone. The parser test checks that a second parser's different scan leaves the first parser's count unchanged.
- Finding 3 (P3): the legacy report `docs/reviews/01-engine/D01-T01-s4.md` is a record of that review and stays as written; its false freeze claim is corrected where the contract lives, in a dated note in `D01 T01 §4`, which also records the aggregate budget.

Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 109 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfParser.*:PdfXref.*:PdfDocument.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 27 tests.`; `python scripts/todo-graph.py validate` -> `valid: 681 sections`.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T07:10:40Z
- Effort: medium
- Lines: 30
- Digest: de4eff7163e0506f

## adversarial: needs-attention

1. **P1: The new value-count budget still does not enforce an aggregate byte ceiling.** `src/pdf/parser.cpp:472` counts values, while `src/pdf/parser.cpp:509` copies each string’s bytes into independently owned storage. The 256 MiB limit in `src/pdf/lexer.h:54` applies per token. Consequently, very few values can retain gigabytes before the parser returns success or encounters allocator failure. This does not satisfy foundation plan section 9’s explicit byte-cap requirement.

   Reproduce with `parse_direct_object()` over `"[" + ("(" + "A" * (16 * 1024 * 1024) + ") ") * 64 + "]"`. It contains only 65 counted values at depth one, but retains 1 GiB of string payload, additional to the input and temporary allocations. No aggregate byte check rejects it. **Static reproduction; the memory-exhaustion probe was not executed.** The owning parser needs a shared byte budget covering strings, names, dictionary keys and container storage.

## consistency: approve

No additional finding.

## integration: needs-attention

Finding 1 reaches document loading through `src/pdf/xref.cpp:1862`: `XrefTable::read_indirect_object_with_visited()` constructs the parser over document bytes without an aggregate byte budget. Wrapping the reproduction in an indirect object preserves the allocation path.

## source-defect: approve

No additional finding.

## design: advisory

Not applicable: this section owns no user surface.

## record: advisory

The available x64 Release binary passed all nine `PdfParser.*` tests and `Support.AsanActiveWhenEnabled`, which reported `ASan=enabled`. The 18 `PdfXref.*` and `PdfDocument.*` tests also passed.

This verifies the available binary’s checkpoint behavior. A fresh build, ARM64 compilation and the historical zero-warning checkpoint were not independently reproduced because rebuilding would modify files. The existing build log records zero warnings and ASan compilation, but remains recorded evidence rather than a fresh build verification.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P1, rated by the reviewer): the value budget left payload unbounded except per token. Decoded payload never exceeds the input bytes it came from, so this was linear in file size, not an amplification, but plan section 9 asks for explicit byte caps and the fix is cheap. One top-level parse now charges every decoded string, name and dictionary key against `kMaxParsedBytes` (equal to `kMaxLexerTokenBytes`, so no object holds more payload than the largest single token already could), refusing with `ErrorCode::DamagedFormat`; the budget restarts with each top-level parse, like the value budget. `PdfParser.TotalByteBudgetSpansValues` refuses 11 bytes of literal and hex strings at a budget of 10, and 11 bytes of keys and a name in a dictionary, accepts exactly 11, and restarts per parse. With the check disabled it fails. The dated correction in `D01 T01 §4` names both budgets.

Gates after round 2: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 110 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfParser.*:PdfXref.*:PdfDocument.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 28 tests.`

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T07:21:27Z
- Effort: medium
- Lines: 30
- Digest: bbe7254361226417

## adversarial: approve

No new actionable defect established at `318b02d`.

## consistency: approve

No new violation established in the audited files.

## integration: approve

No new integration defect established.

## source-defect: approve

No specification defect established. Full ISO source verification was limited by the official PDF URL returning HTTP 404.

## design: advisory

Not applicable: this section owns no user surface.

## record: advisory

Independently ran the existing x64 Release binary with `--gtest_filter=PdfParser.*:Support.AsanActiveWhenEnabled:PdfXref.*:PdfDocument.*`: all 29 tests passed, including 10 parser tests; the activation check reported `ASan=enabled`.

This verifies the available binary’s checkpoint behavior. A fresh build, ARM64 compilation, and the historical zero-warning claim were not independently reproduced because rebuilding would modify files. The existing build log records ASan compilation and zero warnings, but remains recorded evidence.

No files were modified; the working tree remains clean.

HIGHEST: none
OVERALL: approve

## Audit outcome

The stamp on `D01 T01 §4` is confirmed. The audit found gaps present since that stamp, not regressions: the parser's ceilings applied only per container and per token, so one object could create about one value per two input bytes and hold payload bounded only by the file (fixed in `bd5c5c8` and `318b02d` with `kMaxParsedValues` and `kMaxParsedBytes` per top-level parse), and the stream terminator scan count was a `thread_local` contrary to the Freeze check (now a `Parser` member). The contract carries a dated correction. Dependants of `D01 T01 §4` are not suspect: the budgets sit at the existing per-container and per-token ceilings, so nothing the parser accepted under those limits as a single container or token is refused, and all 110 tests pass in x64 Debug and Release.

Test checkpoint at `318b02d`: `python scripts/todo-graph.py resolve "D01 T01 §4"` -> exit 3 (still verified); round 3 approves (`OVERALL: approve`). `D01 T01 §4`'s own checkpoint: `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfParser.*:PdfXref.*:PdfDocument.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 28 tests.`
