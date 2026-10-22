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
