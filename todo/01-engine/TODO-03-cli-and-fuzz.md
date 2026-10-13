---
schema_version: 1
id: cli-and-fuzz
domain: 01-engine
status: active
title: "TODO-03 -- recto-cli, Fuzz Targets and the Thread-Safety Battery"
frozen: true
track: M1
---

# TODO-03 -- recto-cli, Fuzz Targets and the Thread-Safety Battery

> **Goal:** The engine is provable without the UI: `recto-cli` renders, converts and describes PDFs from the command line for testing, profiling and fuzz reproduction; libFuzzer targets cover open and render from M1; and the foundation plan's thread-safety battery (section 4.11) runs as one named suite, green under AddressSanitizer. This file carries M1's done-when.

> **Current state:** Measured 2026-10-13. No code exists. D01 T01 and T02 will provide the bridge, actor, display lists and scheduler; D00 T01 §3 creates the empty `recto-cli` and `recto-fuzz` projects; D00 T03 provides the corpus and ASan configuration. The requirements are plan sections 3.1 (`recto-cli` purpose), 4.11 (the six battery rows) and 7 (M1 done-when: all 4.11 tests pass under ASan, the CLI renders the corpus with no crashes, the fuzzer runs one hour clean). Conversion verbs beyond render and describe grow with D04 and D05, which own those writers. This file is `frozen: true` because the battery is the frozen contract's evidence.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.1, 4.11, 7 (M1), 9 (fuzz from M1, fuzz job in CI).
- `tests/corpus/` as built by D00 T03 §2 (render, text, broken and seeds folders).
- -> XREF: D06 T01 §1 -- the golden render suite drives this file's CLI as its harness; the CLI's render output format agreed here is what goldens compare.
- -> XREF: D06 T02 §2 -- the continuous fuzz operation runs and triages the targets built in §2 here; crash inputs it files come back through this file's CLI for reproduction.

## Outcome

- `recto-cli render in.pdf --page 3 --zoom 2 out.png`, `recto-cli info in.pdf` and `recto-cli text in.pdf out.txt` work over the whole corpus with script-friendly exit codes and no UI dependency.
- Fuzz targets for document open and tile render run locally and as a bounded CI smoke, with crashes reproducing through the CLI.
- The 4.11 battery is one suite name a stamp can quote, green under ASan.

**Adjacency:** list=not-applicable (the corpus is listed by the filesystem, not a Recto surface); document=applicable @ D01 T03 §1; settings=not-applicable (CLI flags are per-invocation, nothing persists); reporting=applicable @ D01 T03 §3; notifications=not-applicable (a console tool's exit code is its notice); permissions=not-applicable (no roles at a command line); audit=not-applicable (fuzz corpora and crash files are working artifacts under fuzz/, curated by D06 T02 §2); exchange=not-applicable (no user data import beyond the input file a verb names); reverse=not-applicable (verbs write new files through the core save rule and never mutate inputs)

**Adjacency rationale:** The CLI is a document surface (it produces the PNGs and text files other sections compare) and the battery is a reporting surface (its quoted results are what M1's done-when rests on), so both are declared with their owning sections. Its inputs are files the operator names on the command line; the hostile-input rules for them live in D06 T02 §1, not here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | recto-cli render, info and text verbs | D01 T02 §2 | [ ] |
| 2 | §2 | Fuzz targets and CI smoke | §1 | [ ] |
| 3 | §3 | Thread-safety battery green under ASan | D01 T02 §4, §2 | [ ] |

## 1. recto-cli render, info and text verbs

Plan 3.1: the CLI exists so the engine can be tested, profiled and fuzz-reproduced without the UI. It links `recto-core` only (never WinRT, never the bridge directly), runs the full runtime lifecycle per invocation (create, work, ordered shutdown from D01 T02 §4), and treats every input as hostile: damaged input is a clean non-zero exit with the `PdfError` message, never a crash.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/cli/main.cpp` with `info` (description through D01 T01 §3), `render` (one page at a zoom or DPI to PNG through the actor, display list and a single pool worker path) and `text` (stext extraction if D03 T01 §1 has landed, otherwise the bridge-level page text the plan's writer pattern uses; record which). Done when: every `tests/corpus/render/` and `text/` file processes with the expected exit code and output shape.
2. Scripting contract: stable stdout summary lines (one per result, key-colon-value), errors on stderr, exit codes 0 (ok), 2 (usage), 3 (input refused or damaged), 4 (password needed or wrong). Done when: `recto-cli` over `tests/corpus/broken/` exits 3 for every file and the batch test asserts it.

- [ ] `info`, `render` and `text` verbs over the corpus. Done when: the render output for the reference page matches D01 T02 §2's single-threaded reference pixels. Cheaper substitute: a test-only harness inside `recto-tests`, which cannot reproduce fuzz crashes or be profiled standalone.
- [ ] Exit-code and summary contract asserted by a batch test. Done when: the broken folder yields exit 3 across the board with no crash dialog and no output file.
- [ ] Commit: `cli: recto-cli render, info and text over the corpus`

**Freeze check:** The CLI exercises the frozen paths unmodified: it opens documents only through `DocumentHandle`, renders only through display lists, and shuts down only through `Runtime`. `Cli.CorpusNoCrash` runs the whole starter corpus under ASan; any shortcut around the actor would show as a special case in `src/cli`, which the section's include audit forbids.

**Test checkpoint:** `recto-cli` batch over `tests/corpus/` under ASan: every good file renders or describes, every broken file exits 3 (quote file counts per folder).

## 2. Fuzz targets and CI smoke

Plan section 9: fuzz the bridge from M1 and keep a fuzz job in CI. Targets call the bridge the way hostile input reaches it (open from memory, then describe and render page 0), built with libFuzzer and ASan, seeded from `tests/corpus/seeds/`. A finding is only real when it reproduces through `recto-cli` on the minimized input.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `fuzz/fuzz_open.cpp` and `fuzz/fuzz_render.cpp` (`LLVMFuzzerTestOneInput` over the bridge open and open-then-render paths, runtime created once, per-input clone dropped each iteration). Done when: both targets run the seed corpus with zero findings and bounded memory.
2. Add the CI smoke: fixed short runs (seed corpus plus a time-boxed fuzz burst) in `.github/workflows/build.yml`, crashing inputs uploaded as artifacts. Done when: a planted crash input (kept out of the seed corpus, injected for the proof) fails the job and names the artifact.
3. Record the one-hour clean local run M1 requires, with its command, corpus size and date, in this section at stamp time. Done when: the run's summary (executions, coverage, zero crashes) is quoted in the review findings.

- [ ] `fuzz_open` and `fuzz_render` targets with seed corpus. Done when: both complete the seeds clean under ASan. Cheaper substitute: fuzzing only the parser entry and never the render path hostile files actually reach.
- [ ] CI fuzz smoke with crash artifacts. Done when: the planted-crash proof goes red and the clean tree is green.
- [ ] Commit: `fuzz: libFuzzer open and render targets with CI smoke`

**Freeze check:** Fuzz inputs reach MuPDF only through the frozen lifecycle (base context, per-iteration clone, status-only errors): a fuzzer crash inside MuPDF is a finding against the pin or the config, triaged through D06 T02 §2, never worked around by catching signals in the harness. The harness contains no signal handlers; that absence is the check.

**Test checkpoint:** both targets over `tests/corpus/seeds/` exit 0 under ASan, and the recorded one-hour run summary shows zero crashes (quote executions).

## 3. Thread-safety battery green under ASan

Plan 4.11 names six tests; D01 T01 and T02 landed most of them beside their sections. This section assembles the remainder (conversion-while-viewing isolation at engine level using a prototype job thread, and the debug lock-order recorder), names the whole set as one suite filter, and runs it as M1's done-when evidence. The battery is the frozen contract's regression net: weakening a row is a frozen-contract change.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Assemble `ThreadSafety.*` in `recto-tests` mapping each 4.11 row to its test (shared display list determinism, 20-document isolation, random cancellation, job-while-viewing isolation via two independent document instances of one file, all under ASan, lock-order recording in Debug). Done when: `--gtest_filter=ThreadSafety.*` runs exactly the six rows and each row's plan reference is in its test name or comment.
2. Add the Debug lock wrapper: Recto's own locks (scheduler, caches, actor queues) record acquisition order per thread and assert on inversion. Done when: a planted inversion in a scratch test trips the assert, and the battery runs clean with the recorder on.
3. Run the full battery plus the whole test suite under ASan and record M1's evidence (counts, ASan findings zero, fuzz hour from §2). Done when: the M1 done-when column can be quoted row by row from this run.

- [ ] `ThreadSafety.*` suite covering all six 4.11 rows. Done when: each row fails against a deliberately broken variant in a scratch branch (spot-check at least determinism and cancellation), proving the tests can fail. Cheaper substitute: running the sections' individual tests and calling the sum a battery.
- [ ] Debug lock-order recorder over Recto's own locks. Done when: the planted inversion asserts and the real battery does not.
- [ ] Commit: `test: thread-safety battery (plan 4.11) green under ASan`

**Freeze check:** The battery is the freeze check for the whole engine domain: any later change to locks, actors, display lists, scheduler or shutdown re-runs `ThreadSafety.*` under ASan, and a red row blocks the stamp of the section that reddened it. The suite itself is the committed fixture.

**Test checkpoint:** `recto-tests --gtest_filter=ThreadSafety.*` under ASan: all rows pass, zero sanitizer findings, with the full `recto-tests` suite green in the same run (quote both counts).

## Verification

- M1 done-when (foundation plan section 7) is quotable: battery green under ASan, CLI renders the corpus with no crashes, fuzzer one hour clean.
- `python scripts/todo-graph.py validate` passes; fuzz crash artifacts, if any ever appear, reproduce through `recto-cli` before they are filed.
