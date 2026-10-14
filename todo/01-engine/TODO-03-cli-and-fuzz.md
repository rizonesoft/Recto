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

> **Goal:** Close Milestone M1 with the command-line harness (`recto-cli`), the libFuzzer targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`), and the foundation plan section 4.9 Thread-Safety Verification Battery (1-worker vs 8-worker pixel determinism, 30-second ASan cancellation storm, 20-document concurrent stress).

> **Current state:** Measured 2026-10-14. `src/cli` and `fuzz/` have their M0 scaffold `main.cpp` and `fuzz_smoke.cpp`. `D01 T01` and `D01 T02` build the `recto-pdf` and `recto-core` engine this file drives and proves.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.1 (`recto-cli`), 4.9 (Thread-Safety Verification Battery), 7 (M1 done-when), 9 (fuzzing rules).
- -> XREF: D01 T02 §2 -- `recto-cli render`, `fuzz_render` and the determinism test drive the `DisplayList` and `Rasterizer` pipeline built there.
- -> XREF: D00 T03 §2 -- consumes `tests/corpus/` (`render/`, `broken/`, `seeds/`) and promotes any fuzz findings under its provenance rule.
- -> XREF: D06 T01 §1 -- the golden render suite in `D06 T01 §1` and the performance harness in `D06 T01 §4` invoke `recto-cli` verbs defined in §1 here.
- -> XREF: D06 T02 §2 -- continuous fuzzing operations in `D06 T02 §2` run the fuzz targets built in §2 here.

## Outcome

- `recto-cli` supports `info`, `render`, `bench` and `stress` (with `text`, `find`, `ocr` and conversion verbs plugging into the same command dispatch in M3 to M5), exiting 0 on valid inputs and a distinct non-zero code with a clean diagnostic message on broken or password-locked files.
- `fuzz_open`, `fuzz_filters` and `fuzz_render` compile with `/fsanitize=fuzzer,address` and run 1 hour clean over `tests/corpus/seeds/` plus `broken/`.
- All three tests in the foundation plan section 4.9 Thread-Safety Verification Battery pass under AddressSanitizer.

**Adjacency:** all=not-applicable (command-line developer tool, fuzz harnesses and concurrency test battery; no GUI surfaces)

**Adjacency rationale:** `recto-cli` is the headless test and benchmark harness required by foundation plan section 3.1; it shares `recto-core` and `recto-pdf` with the app but owns no persistent user state or GUI chrome.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `recto-cli` (`info`, `render`, `bench`, `stress`) | D01 T02 §4 | [ ] |
| 2 | §2 | libFuzzer targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`) and CI smoke | §1, D00 T03 §2, D00 T03 §3 | [ ] |
| 3 | §3 | Section 4.9 Thread-Safety Verification Battery under ASan | §1, D01 T02 §4, D00 T03 §3 | [ ] |

## 1. `recto-cli` (`info`, `render`, `bench`, `stress`)

Implement `src/cli/main.cpp` command dispatch and argument parsing (`--password <pw>`, `--pages <range>`, `--dpi <n>`, `--rotation <0|90|180|270>`, `--workers <n>`, `--out <path>`, `--json`):
- `recto-cli info <file.pdf>`: prints page count, page dimensions/rotations, PDF version, encryption status/method/permissions, metadata, outline count, and font inventory (plain text or `--json`).
- `recto-cli render <file.pdf> --out <dir_or_png>`: renders selected pages at the requested DPI, rotation and worker count via `Runtime` + `DocumentActor` + `RenderPool` (both full-page and stitched `512 x 512` tile modes) and writes PNG files through WIC.
- `recto-cli bench <file.pdf>`: measures cold open time, per-page `DisplayList` record time, and 1-worker vs N-worker tile rasterization throughput.
- `recto-cli stress <file.pdf>`: runs a configurable cancellation-storm or multi-document stress pass from the command line.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `recto-cli` drives `recto-core` (`Runtime`, `DocumentActor`, `RenderPool`, `TileScheduler`) through its public C++20 headers rather than reaching around the actor model, so CLI runs exercise the exact same threading contract as the viewer.

**Build order:**

1. Implement `info` and `render` (including PNG encoding via WIC `GUID_ContainerFormatPng` and `--password` support). Done when: `recto-cli info` and `recto-cli render` succeed on every `tests/corpus/render/` file (and on the encrypted corpus file when `--password recto-test` is passed) and exit non-zero with a clean `PdfError` line on every `tests/corpus/broken/` file.
2. Implement `bench` and `stress` plus CLI integration tests in `recto-tests` (`Cli.*`). Done when: `recto-tests.exe --gtest_filter=Cli.*` verifies `info --json`, `render` output dimensions, wrong-password refusal, and broken-file refusal.

- [ ] `recto-cli` verbs `info`, `render`, `bench`, and `stress` implemented over `recto-core`. Done when: `recto-cli render` writes valid PNGs for the render corpus and `recto-cli info` reports accurate metadata and security details. Cheaper substitute: calling `recto-pdf` single-threaded functions directly from `main.cpp` and leaving `Runtime`/`RenderPool` unexercised by the CLI.
- [ ] Clean non-zero exit codes and diagnostic messages on `tests/corpus/broken/` and wrong passwords. Done when: every file in `broken/` exits non-zero without crashing under ASan.
- [ ] Commit: `cli: implement recto-cli info, render, bench and stress commands`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.*` exits 0 under ASan, and running `recto-cli info` and `recto-cli render` across `tests/corpus/render/` and `tests/corpus/broken/` matches all expected exit codes.

## 2. libFuzzer targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`) and CI smoke

Replace `fuzz/fuzz_smoke.cpp` with real libFuzzer targets over `recto-pdf`:
- `fuzz_open`: feeds arbitrary byte spans into `recto::pdf::Document::open_memory` (exercising lexer, XRef tables, compressed XRef streams, damaged-file scanner, page tree, outlines, and metadata).
- `fuzz_filters`: feeds arbitrary byte spans and filter chains into `recto-pdf`'s stream decoders (`Flate`, `LZW`, predictors, `ASCII85`, `ASCIIHex`, `RunLength`, `CCITTFax`).
- `fuzz_render`: opens the input buffer and interprets + rasterizes page 1 at 72 DPI with a tight per-input memory and command budget.
Add a short fuzz smoke step (e.g., 60 seconds per target over `tests/corpus/seeds/` and `broken/`) to CI and run the 1-hour M1 acceptance pass locally.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that feeding arbitrary bytes to `open_memory`, stream filters, and `Rasterizer` never triggers undefined behavior, unhandled exceptions, or unbounded allocations (the 256 MB stream ceiling and 64-level recursion limit hold on every fuzzer-generated input).

**Build order:**

1. Add `Document::open_memory(std::span<const uint8_t>)` in `src/pdf/document.cpp` (sharing the exact same parser path as file-backed documents) and implement `fuzz_open`, `fuzz_filters`, and `fuzz_render` in `fuzz/` with `/fsanitize=fuzzer,address` support in `build/build.ps1`. Done when: all three targets compile and run against `tests/corpus/seeds/` and `tests/corpus/broken/`.
2. Run `fuzz_open` for 1 hour (M1 definition of done), fix any parser/filter crash or leak discovered in `src/pdf` while recording minimized regression inputs in `tests/corpus/broken/` (and `PROVENANCE.md`), and wire a 60-second corpus smoke pass into `.github/workflows/build.yml`. Done when: the 1-hour run finishes with 0 crashes and 0 leaks.

- [ ] `fuzz_open`, `fuzz_filters`, and `fuzz_render` built with `/fsanitize=fuzzer,address` and wired into `build/build.ps1` and CI smoke. Done when: each target executes the seed and broken corpus with 0 findings. Cheaper substitute: a dummy `LLVMFuzzerTestOneInput` that returns 0 without parsing or rendering.
- [ ] 1-hour `fuzz_open` run completed clean (any discovered bugs fixed at the owning `src/pdf` layer and added to `tests/corpus/broken/`). Done when: the 1-hour execution log (executions/sec, corpus coverage, 0 crashes) is quoted in the review findings.
- [ ] Commit: `fuzz: add fuzz_open, fuzz_filters and fuzz_render targets and run M1 1-hour pass`

**Test checkpoint:** 1-hour `fuzz_open` log under `/fsanitize=fuzzer,address` shows 0 crashes and 0 leaks, and `recto-tests.exe --gtest_filter=Corpus.Provenance` passes with any new regression inputs registered.

## 3. Section 4.9 Thread-Safety Verification Battery under ASan

Implement the three mandatory M1 concurrency tests in `tests/recto-tests/thread_safety_tests.cpp` (`ThreadSafety.Determinism`, `ThreadSafety.CancellationStorm`, `ThreadSafety.MultiDocumentStress`) exactly as specified in foundation plan section 4.9, and run them under AddressSanitizer.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Every assertion in foundation plan section 4.9 must be literal and unweakened: byte-for-byte RGBA equality between 1 worker and 8 workers with shuffled tile order across 10 corpus PDFs, 1,000 random viewport jumps and zoom changes over 30 seconds on a 200-page PDF with 5 ms `CancelToken` trips under ASan, and 20 concurrent `DocumentActor` open/render/text/close cycles returning all Debug live-object counters to 0.

**Build order:**

1. Add a 200-page deterministic multi-content generator helper (or corpus fixture) and implement `ThreadSafety.Determinism` (1 worker vs 8 workers with randomized tile order across 10 corpus PDFs, asserting byte-for-byte RGBA equality on every page). Done when: `ThreadSafety.Determinism` passes 100% and a planted race/perturbation in a test seam fails it immediately.
2. Implement `ThreadSafety.CancellationStorm` (200-page PDF, 8 workers, 1,000 viewport/zoom jumps, 5 ms `CancelToken` trips for 30 seconds under ASan) and `ThreadSafety.MultiDocumentStress` (20 concurrent documents opening, rendering tiles, extracting text, and closing while asserting `live_documents == 0` and `live_display_lists == 0`). Done when: all three tests pass under Release + ASan with 0 errors and 0 leaks.

- [ ] `ThreadSafety.Determinism` proves byte-for-byte RGBA equality between 1 worker and 8 shuffled workers across 10 corpus PDFs. Done when: the test passes under ASan and quotes the page and byte counts verified. Cheaper substitute: comparing only image dimensions or allowing a pixel tolerance.
- [ ] `ThreadSafety.CancellationStorm` (30 s, 200-page PDF, 1,000 jumps, 5 ms cancellation) and `ThreadSafety.MultiDocumentStress` (20 concurrent actors) pass under ASan with zero leaks and zeroed live-object counters. Done when: the full ASan run log is quoted in the review findings.
- [ ] Commit: `test: implement section 4.9 Thread-Safety Verification Battery under ASan`

**Test checkpoint:** `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN` running `recto-tests.exe --gtest_filter=ThreadSafety.*` exits 0 with all 3 battery tests passing.

## Verification

- M1 definition of done (`docs/plans/2026-10-13-recto-foundation.md` section 7): all three tests in the section 4.9 Thread-Safety Verification Battery pass under ASan, and `fuzz_open` runs 1 hour clean on the seed corpus.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
