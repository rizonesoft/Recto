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

> **Goal:** Close Milestone M1 with the command-line harness (`recto-cli`: `info`, `render`, `bench`, `stress`), the libFuzzer targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`) and M1 1-hour fuzz acceptance run, and the foundation plan section 4.9 Thread-Safety Verification Battery (`ThreadSafety.Determinism` 1-worker vs 8-worker pixel equality, `ThreadSafety.CancellationStorm` 30-second ASan cancellation storm, and `ThreadSafety.MultiDocumentStress` 20-document concurrent stress).

> **Current state:** Measured 2026-10-14. `src/cli` and `fuzz/` have their M0 scaffold `main.cpp` and `fuzz_smoke.cpp`. `D01 T01` and `D01 T02` build the `recto-pdf` and `recto-core` engine this file drives and proves.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.1 (`recto-cli`), 4.9 (Thread-Safety Verification Battery), 7 (M1 done-when), 9 (fuzzing rules).
- -> XREF: D01 T02 §6 -- `recto-cli render` (`§2`), `fuzz_render` (`§4`) and the determinism test (`§6`) drive the `DisplayList`, `Rasterizer`, `DocumentActor`, and `RenderPool` pipeline built in `D01 T02`.
- -> XREF: D00 T03 §4 -- consumes `tests/corpus/` (`render/`, `broken/`, `seeds/`) from `D00 T03 §3-§4` and promotes any fuzz findings under `D00 T03 §5`'s provenance rule.
- -> XREF: D06 T01 §1 -- the golden render suite in `D06 T01 §1` and the performance harness in `D06 T01 §7` invoke `recto-cli` verbs defined in `§1-§3` here.
- -> XREF: D06 T02 §3 -- continuous fuzzing operations in `D06 T02 §3` and `§4` run the fuzz targets built in `§4` here.
- -> XREF: D04 T08 §2 -- verb_names and the dispatcher library.
- -> XREF: D04 T08 §8 -- env:NAME secret parsing.
- -> XREF: D02 T03 §18 -- bookmarks verb follows the registration rule.
- -> XREF: D03 T03 §25 -- annot verb follows the registration rule.
- -> XREF: D03 T04 §29 -- edit verbs follow the registration rule.
- -> XREF: D05 T01 §15 -- organize verbs follow the registration rule.
- -> XREF: D04 T05 §26 -- a11y verb follows the registration rule.
- -> XREF: D04 T08 §2 -- verbs --json agreement gate.

## Outcome

- `recto-cli` supports `info`, `render`, `bench` and `stress` (with `text`, `find`, `ocr`, and conversion/organize verbs plugging into the same command dispatch table in M3 to M5), exiting `0` on valid inputs and distinct non-zero codes (`1` usage, `2` file/parse error, `3` password required/wrong) with a clean diagnostic message on broken or password-locked files.
- `fuzz_open`, `fuzz_filters` and `fuzz_render` compile with `/fsanitize=fuzzer,address` and run 1 hour clean over `tests/corpus/seeds/` plus `broken/`.
- All three tests in the foundation plan section 4.9 Thread-Safety Verification Battery pass under AddressSanitizer.
- Parity and automation additions (D18, D21): the dispatcher lives in a linkable unit with a read-only `Dispatcher::verb_names()` (`§8`), passwords resolve from `env:NAME` and never appear in output (`§9`), every later verb follows one registration and exit-code rule checked by `Cli.DispatcherContract` (`§10`), and `recto-cli verbs --json` is gated against the dispatcher (`§11`).

**Adjacency:** all=not-applicable (command-line developer tool, fuzz harnesses and concurrency test battery; no GUI surfaces)

**Adjacency rationale:** `recto-cli` is the headless test and benchmark harness required by foundation plan section 3.1; it shares `recto-core` and `recto-pdf` with the app but owns no persistent user state or GUI chrome.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `recto-cli` command dispatcher, exit-code contract and `recto-cli info` (`--json`) | D01 T02 §10 | [ ] |
| 2 | §2 | `recto-cli render` (full-page and stitched `512 x 512` tile modes, WIC PNG encoder) | §1 | [ ] |
| 3 | §3 | `recto-cli bench` and `recto-cli stress` commands | §2 | [ ] |
| 4 | §4 | libFuzzer targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`) with AddressSanitizer | D01 T02 §7, D00 T03 §4, D00 T03 §6 | [ ] |
| 5 | §5 | M1 1-hour `fuzz_open` acceptance run, regression promotion and CI 60s fuzz smoke | §4, D00 T03 §5 | [ ] |
| 6 | §6 | Section 4.9 Thread-Safety Battery Part 1: `ThreadSafety.Determinism` (1 vs 8 workers) | §2, D00 T03 §6 | [ ] |
| 7 | §7 | Section 4.9 Battery Part 2: `CancellationStorm` (30 s) and `MultiDocumentStress` | §6 | [ ] |
| 8 | §8 | `recto::cli::Dispatcher` in its own unit and the read-only `Dispatcher::verb_names()` | §3 | [ ] |
| 9 | §9 | `--password env:NAME` in the shared flag parser, never echoed or logged | §8 | [ ] |
| 10 | §10 | Verb extension rule: later verb families register under the `§1` exit-code contract | §9 | [ ] |
| 11 | §11 | Dispatcher and `recto-cli verbs --json` agreement gate for every registered verb | §10, D04 T08 §2 | [ ] |

## 1. `recto-cli` command dispatcher, exit-code contract and `recto-cli info` (`--json`)

Implement the extensible command dispatcher and `info` command in `src/cli/main.cpp` (using `wmain(int argc, wchar_t* argv[])` for full Unicode path support on Windows):
- Command table architecture supporting `--help`, `--version`, and subcommands (`info`, `render`, `bench`, `stress`, with slots for M2-M5 subcommands).
- Deterministic exit codes: `0` success, `1` invalid CLI arguments, `2` `NotAPdf` / `DamagedFormat` / `IoError`, `3` `NeedsPassword` / `WrongPassword`.
- `recto-cli info <file.pdf> [--password <pw>] [--json]`: opens the file via `recto::core::Runtime` and `DocumentActor`, printing PDF version, page count, per-page `[width_pt, height_pt, rotation]`, encryption status (`method`, `r_revision`, permissions), metadata (`title`, `author`, `subject`, `keywords`, `creator`, `producer`, `creation_date`, `mod_date`), outline entry count, link count, and embedded font inventory in human-readable text or structured JSON (`--json`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `recto-cli` drives `recto-core` (`Runtime`, `DocumentHandle` / `DocumentActor`) through its public C++20 headers rather than calling `recto-pdf` single-threaded internals directly.

**Build order:**

1. Implement `wmain` UTF-8 argument conversion, subcommand dispatch, exit-code mapping, and `recto-cli info` (plain text and `--json`) in `src/cli/main.cpp`. Done when: `recto-cli.vcxproj` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/cli_tests.cpp` (`Cli.InfoPlainAndJson`, `Cli.InfoEncryptedPasswordHandling`, `Cli.InfoBrokenFilesExitCode`). Done when: `Cli.Info*` passes under ASan across `render/`, `text/`, and `broken/` corpus files.

- [ ] Use `wmain(int argc, wchar_t* argv[])` and set console UTF-8 output (`SetConsoleOutputCP(CP_UTF8)`) so non-ASCII file paths and PDF metadata strings round-trip without mojibake. Done when: `Cli.UnicodePathInfo` opens a corpus PDF copied to a path with non-ASCII characters (`TempDir/résumé-日本語.pdf`).
- [ ] Implement the subcommand dispatcher (`info`, `render`, `bench`, `stress`, `--help`, `--version`) and shared flag parser (`--password <pw>`, `--pages <spec>`, `--json`, `--out <path>`). Done when: unknown flags or missing arguments exit with code `1` and print usage to `stderr`.
- [ ] Implement `recto-cli info <file.pdf>` opening the document through `recto::core::Runtime` + `DocumentActor` and printing version, page count, page dimensions/rotations, metadata, security summary, outline count, and font list. Done when: `recto-cli info` succeeds (exit `0`) on every unencrypted file in `tests/corpus/render/` and `tests/corpus/text/`. Cheaper substitute: calling `recto-pdf` directly from `main.cpp` and leaving `Runtime`/`DocumentActor` unexercised.
- [ ] Implement `--json` output mode in `recto-cli info` with proper JSON string escaping (`\"`, `\\`, `\n`, `\uXXXX`). Done when: `Cli.InfoPlainAndJson` parses the `--json` stdout and verifies exact field values for `text/two-column-links-outline.pdf`.
- [ ] Map `ErrorCode::NeedsPassword` and `ErrorCode::WrongPassword` to exit code `3` with a clear diagnostic message on `stderr`, and unlock when `--password recto-test` is passed. Done when: `Cli.InfoEncryptedPasswordHandling` verifies exit `3` without `--password`, exit `3` with `--password wrong`, and exit `0` with `--password recto-test` on `render/encrypted-aes-256.pdf`.
- [ ] Map `NotAPdf` and `DamagedFormat` to exit code `2` and print the `PdfError` message plus `diagnostics_tail()` summary to `stderr`. Done when: every unrecoverable file in `tests/corpus/broken/` exits with code `2` and zero crashes under ASan.
- [ ] Commit: `cli: implement recto-cli command dispatcher and info (--json) subcommand`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.Info*:Cli.Unicode*` passes under ASan.

## 2. `recto-cli render` (full-page and stitched `512 x 512` tile modes, WIC PNG encoder)

Implement `recto-cli render <file.pdf> --out <dir_or_png>` in `src/cli/`:
- Options: `--pages <all|1-3,5>` (default `all`), `--dpi <72..1200>` (default `150`), `--rotation <0|90|180|270>` (default `0`), `--workers <1..16>` (default hardware pool), `--mode <tiles|full>` (default `tiles` using `TileScheduler` + `RenderPool` `512 x 512` tiles stitched into the output page bitmap; `full` rendering the page in one pass), and `--password <pw>`.
- Encode each rendered page bitmap (`GUID_WICPixelFormat32bppPBGRA` / `32bppBGRA`) to PNG via WIC (`GUID_ContainerFormatPng`) using atomic temp-then-replace writes.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `--mode tiles` schedules `512 x 512` tiles through `TileScheduler` and `RenderPool` over `std::shared_ptr<const DisplayList>` from `DocumentActor`, exercising the exact viewer tile path.

**Build order:**

1. Implement `recto-cli render` (page-range parser, `--mode tiles` stitching via `TileScheduler` + `RenderPool`, `--mode full`, and WIC PNG writer) in `src/cli/`. Done when: `recto-cli render` writes PNGs for single-page and multi-page corpus files.
2. Add `Cli.RenderFullVsTiledIdentity`, `Cli.RenderRotationsAndDpi`, and `Cli.RenderEncrypted` in `tests/recto-tests/cli_tests.cpp`. Done when: all `Cli.Render*` tests pass under ASan.

- [ ] Implement `--mode tiles` (default) dividing each target page at `--dpi` and `--rotation` into a grid of `512 x 512` `TileKey`s, dispatching them through `TileScheduler` and `RenderPool` (`--workers N`), and stitching completed tiles into the page buffer. Done when: multi-worker tiled rendering exercises `DocumentActor -> TileScheduler -> RenderPool`.
- [ ] Implement `--mode full` rendering each page in a single `Rasterizer` buffer so tests can compare `--mode tiles` against `--mode full` directly from the CLI. Done when: `Cli.RenderFullVsTiledIdentity` verifies that `--mode tiles --workers 4` and `--mode full` produce pixel-identical PNGs.
- [ ] Encode output page buffers to valid PNG files via WIC `GUID_ContainerFormatPng` (writing `<out_dir>/page-0001.png` when `--out` is a directory or multi-page, or `<out.png>` directly when rendering a single page to a `.png` path). Done when: WIC decodes the written PNG back to the exact expected `(width, height)` pixel dimensions.
- [ ] Honor `--dpi <72..1200>` and `--rotation <0|90|180|270>`, computing output pixel dimensions `round(width_pt * dpi / 72.0)` x `round(height_pt * dpi / 72.0)` (swapped for `90`/`270` effective rotation). Done when: `Cli.RenderRotationsAndDpi` verifies a `612 x 792 pt` page at `144 DPI` and `--rotation 90` produces a `1584 x 1224 px` PNG.
- [ ] Honor `--pages <spec>` (`1`, `1-3`, `2,4`) and return exit code `1` on out-of-range page numbers (`0` or `> page_count`) before creating any output file. Done when: invalid `--pages` specs exit `1` with zero files written.
- [ ] Commit: `cli: add recto-cli render with tiled RenderPool execution and WIC PNG output`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.Render*` passes under ASan.

## 3. `recto-cli bench` and `recto-cli stress` commands

Implement the benchmarking and stress-testing CLI subcommands in `src/cli/`:
- `recto-cli bench <file.pdf> [--iterations <n>] [--dpi <n>] [--json]`: measures and reports high-resolution (`QueryPerformanceCounter` / `std::chrono::steady_clock`) timings for (a) cold document open (`DocumentActor` open + XRef/page tree), (b) page-1 `DisplayList` compilation, (c) 1-worker vs N-worker `512 x 512` tile rasterization throughput (`ms` and `tiles/sec`), and (d) process working-set memory (`GetProcessMemoryInfo` `WorkingSetSize` and `PeakWorkingSetSize`), in plain text or `--json` (consumed by `D00 T01 §5` and `D06 T01 §7`).
- `recto-cli stress <file.pdf> [--seconds <n>] [--workers <n>]`: runs a command-line viewport-jump cancellation storm and multi-actor open/close stress pass, asserting all Debug live-object counters return to `0` at exit.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `bench` and `stress` shut down `Runtime` cleanly and assert `debug_live_documents() == 0 && debug_live_display_lists() == 0 && debug_live_workers() == 0` before returning exit code `0`.

**Build order:**

1. Implement `recto-cli bench` and `recto-cli stress` in `src/cli/`. Done when: `recto-cli bench tests/corpus/render/simple-1p.pdf --json` and `recto-cli stress tests/corpus/render/simple-1p.pdf --seconds 2` exit `0`.
2. Add `Cli.BenchJsonOutput` and `Cli.StressShortRun` in `tests/recto-tests/cli_tests.cpp`. Done when: `Cli.*` passes in full under ASan.

- [ ] Measure cold `DocumentActor` open latency, first-page `DisplayList` record latency, and full-page tile rasterization latency for `1` worker vs default `N` workers in `recto-cli bench`. Done when: `recto-cli bench` reports median and p95 timings across `--iterations N`.
- [ ] Query `GetProcessMemoryInfo` (`PROCESS_MEMORY_COUNTERS_EX`) in `recto-cli bench` to report `working_set_mb` and `private_usage_mb`. Done when: `--json` output includes `open_ms`, `display_list_ms`, `render_1w_ms`, `render_nw_ms`, and `working_set_mb`.
- [ ] Implement `recto-cli stress <file.pdf> [--seconds <n>] [--workers <n>]` driving random viewport jumps, `CancelToken` cancellations, and concurrent `DocumentActor` open/close cycles from the command line. Done when: `Cli.StressShortRun` runs a 2-second stress pass and exits `0` with zeroed live-object counters.
- [ ] Verify `recto-cli --help` lists all M1 subcommands (`info`, `render`, `bench`, `stress`) and their flags clearly. Done when: `Cli.HelpAndVersion` passes.
- [ ] Commit: `cli: implement recto-cli bench and stress commands`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.*` exits 0 under ASan.

## 4. libFuzzer targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`) with AddressSanitizer

Replace the M0 scaffold in `fuzz/` with three real libFuzzer targets over `recto-pdf`:
- `fuzz/fuzz_open.cpp`: feeds `std::span<const uint8_t>(data, size)` into `recto::pdf::Document::open_memory`, and when open succeeds (or unlocks with `""` or `"recto-test"`), queries `description()`, `outline()`, `fonts()`, and `page_links(0)` (capped at the first 8 pages) to fuzz the lexer, parser, XRef tables/streams, damaged-file recovery, CNG crypto, and document model.
- `fuzz/fuzz_filters.cpp`: uses the first byte of `data` to select a filter or 2-filter chain (`Flate`, `LZW`, `Predictors`, `ASCII85`, `ASCIIHex`, `RunLength`, `CCITTFax`, `JBIG2`, `DCT`, `JPX`) and feeds the remaining bytes into `decode_stream` with a tight 16 MB fuzz decompression cap.
- `fuzz/fuzz_render.cpp`: opens `data` via `Document::open_memory`, compiles page 0 into a `DisplayList` (capped at 4,096 commands), and rasterizes one `256 x 256` tile at 72 DPI via a thread-local `Rasterizer`.
Wire `-BuildFuzz` into `build/build.ps1` compiling all three targets with `/fsanitize=fuzzer,address`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that feeding arbitrary fuzzer-generated bytes to `fuzz_open`, `fuzz_filters`, and `fuzz_render` never triggers undefined behavior, unhandled C++ exceptions, stack overflow, or unbounded memory growth.

**Build order:**

1. Author `fuzz/fuzz_open.cpp`, `fuzz/fuzz_filters.cpp`, and `fuzz/fuzz_render.cpp`, configure `fuzz/recto-fuzz.vcxproj` (or per-target configurations / dispatcher) with `/fsanitize=fuzzer,address`, and add `-BuildFuzz` to `build/build.ps1`. Done when: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -BuildFuzz` builds all three fuzz executables with 0 warnings.
2. Run each target over `tests/corpus/seeds/` and `tests/corpus/broken/` (`-runs=1000`) to verify harness startup and corpus execution. Done when: all three targets complete 1,000 runs with 0 crashes and 0 leaks.

- [ ] Implement `fuzz/fuzz_open.cpp` calling `Document::open_memory({data, size})`, attempting `authenticate("recto-test")` if `NeedsPassword`, and walking `description()`, `outline()`, `fonts()`, and the first 8 pages' `page_links` and `decoded_page_contents`. Done when: `fuzz_open.exe` executes over `tests/corpus/seeds/` and `tests/corpus/broken/`. Cheaper substitute: a dummy `LLVMFuzzerTestOneInput` that returns `0` without parsing or resolving objects.
- [ ] Implement `fuzz/fuzz_filters.cpp` dispatching across all 10 stream filters (`Flate`, `LZW`, TIFF/PNG predictors, `ASCII85`, `ASCIIHex`, `RunLength`, `CCITTFax`, `JBIG2`, `DCT`, `JPX`) and 2-stage filter chains from the input buffer. Done when: `fuzz_filters.exe` exercises every filter branch without leaks.
- [ ] Implement `fuzz/fuzz_render.cpp` compiling page 0's `DisplayList` and rasterizing a `256 x 256` tile via a persistent per-process `Rasterizer` instance. Done when: `fuzz_render.exe` executes over `tests/corpus/seeds/` and `tests/corpus/render/` with constant memory usage across 1,000 iterations.
- [ ] Wire `-BuildFuzz` in `build/build.ps1` to build `fuzz_open.exe`, `fuzz_filters.exe`, and `fuzz_render.exe` under `build/bin/x64/Release/fuzz/` with `/fsanitize=fuzzer,address`. Done when: `-BuildFuzz` exits 0 with 0 warnings.
- [ ] Verify that `debug_live_display_lists()` is `0` at the end of every `LLVMFuzzerTestOneInput` iteration in `fuzz_render` so no `DisplayList` leaks across fuzzer iterations. Done when: `-rss_limit_mb=512` holds steady across 5,000 runs.
- [ ] Commit: `fuzz: implement fuzz_open, fuzz_filters and fuzz_render libFuzzer targets`

**Test checkpoint:** `fuzz_open.exe`, `fuzz_filters.exe`, and `fuzz_render.exe` each run `-runs=2000` over `tests/corpus/seeds/` and `tests/corpus/broken/` under `/fsanitize=fuzzer,address` with 0 crashes and 0 leaks.

## 5. M1 1-hour `fuzz_open` acceptance run, regression promotion and CI 60s fuzz smoke

Execute the foundation plan section 7 M1 fuzz gate:
- Run `fuzz_open` for **1 full hour** (`-max_total_time=3600 -rss_limit_mb=2048`) seeded with `tests/corpus/seeds/`, `tests/corpus/render/`, and `tests/corpus/broken/`.
- If any crash, hang, timeout, or ASan finding occurs during the run, fix the defect at the owning `src/pdf` layer, minimize the input (`-minimize_crash=1`), add the minimized file to `tests/corpus/broken/` with a `tests/corpus/PROVENANCE.md` entry (`D00 T03 §5`), and re-run until a continuous 1-hour pass completes with 0 findings.
- Add a 60-second fuzz smoke step (`-max_total_time=20` per target across the 3 targets) to `.github/workflows/build.yml`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Any parser or filter bug uncovered by the 1-hour fuzz pass is fixed inside `src/pdf` while preserving `PdfResult<T>` error returns and bounded memory rules.

**Build order:**

1. Run `fuzz_open.exe` for 3,600 seconds (`-max_total_time=3600`) against `tests/corpus/seeds/` + `broken/` (logging progress to `build/fuzz-open-1h.log`), fixing any discovered `src/pdf` defects and registering minimized regression inputs in `tests/corpus/broken/` and `PROVENANCE.md`. Done when: a continuous 3,600-second run exits `0` with `0` crashes and `0` leaks.
2. Add the 60-second fuzz smoke step (`fuzz_open`, `fuzz_filters`, `fuzz_render` over `tests/corpus/seeds/` and `broken/`) to `.github/workflows/build.yml`. Done when: `.github/workflows/build.yml` passes the fuzz smoke step and `Corpus.Provenance` is green.

- [ ] Execute `fuzz_open.exe -max_total_time=3600 -rss_limit_mb=2048 -timeout=10` over the seed and broken corpus, saving the run summary (`cov:`, `ft:`, `exec/s:`, total executions, peak RSS) to `build/fuzz-open-1h.log`. Done when: the 3,600-second run finishes with exit code `0`. Cheaper substitute: running for 30 seconds and claiming the 1-hour M1 gate is done.
- [ ] Triage and fix any crash, assertion, OOM, or >10s timeout discovered during the run at the owning `src/pdf` parser/xref/filter layer. Done when: every discovered input returns a clean `PdfResult` error in `< 50 ms` under ASan.
- [ ] Minimize any crash/timeout inputs found (`-minimize_crash=1`) and commit them to `tests/corpus/broken/` with SHA-256 and `CC0-1.0` synthetic provenance in `tests/corpus/PROVENANCE.md`. Done when: `recto-tests.exe --gtest_filter=Corpus.Provenance:PdfDocument.BrokenCorpusSweep` passes.
- [ ] Run `fuzz_filters.exe` and `fuzz_render.exe` for `-max_total_time=300` each to verify filter and rasterizer stability before closing M1. Done when: both complete 300 seconds with 0 crashes and 0 leaks.
- [ ] Add a `fuzz-smoke` step to `.github/workflows/build.yml` compiling the fuzz targets (`-BuildFuzz`) and running each target over `tests/corpus/seeds/` and `tests/corpus/broken/` (`-max_total_time=20`). Done when: the CI workflow syntax and local smoke run both succeed.
- [ ] Commit: `fuzz: complete M1 1-hour fuzz_open pass and add CI fuzz smoke step`

**Test checkpoint:** `build/fuzz-open-1h.log` quotes `>= 3600` seconds elapsed with `0` crashes and `0` leaks under `/fsanitize=fuzzer,address`, and `recto-tests.exe --gtest_filter=Corpus.Provenance` exits 0.

## 6. Section 4.9 Thread-Safety Battery Part 1: `ThreadSafety.Determinism` (1 vs 8 workers)

Implement the first mandatory test of the foundation plan section 4.9 Thread-Safety Verification Battery in `tests/recto-tests/thread_safety_tests.cpp`:
- **`ThreadSafety.Determinism`:** Across **10 corpus PDFs** (converting/generating additional deterministic render fixtures in `scripts/build-corpus.py` and `PROVENANCE.md` if `tests/corpus/` has fewer than 10 distinct unencrypted/unlocked PDFs), render every page at `150 DPI` as stitched `512 x 512` tiles first with **1 worker thread in sequential tile order** and then with **8 worker threads with a randomly shuffled tile submission order**.
- Assert **byte-for-byte RGBA equality** (`expect_rgba_equal` with `max_channel_delta = 0`) between the 1-worker output and the 8-worker shuffled output on every single page.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** The determinism assertion must be strict byte-for-byte equality (`0` tolerance) across all 10 corpus PDFs between 1 worker and 8 shuffled workers under AddressSanitizer.

**Build order:**

1. Ensure `tests/corpus/` contains at least 10 distinct multi-feature PDF fixtures registered in `PROVENANCE.md` and implement `ThreadSafety.Determinism` in `tests/recto-tests/thread_safety_tests.cpp`. Done when: `ThreadSafety.Determinism` compiles and runs across all 10 PDFs.
2. Verify failure capability by confirming that a planted 1-bit pixel perturbation fails `ThreadSafety.Determinism` naming the document, page, and `(x, y)` coordinate, then remove the perturbation and run under Release + ASan. Done when: `ThreadSafety.Determinism` passes under ASan.

- [ ] Ensure at least 10 distinct PDF fixtures in `tests/corpus/` (covering text, vector curves, clipping, gradients, transparency, CMYK, rotated pages, JPEG/PNG/CCITT images, Form XObjects, and decrypted AES-256 pages) are exercised by `ThreadSafety.Determinism`. Done when: the test iterates `>= 10` corpus files and logs each file name and page count.
- [ ] Render each page of all 10 PDFs at `150 DPI` (producing multiple `512 x 512` tiles per page) using a 1-worker `RenderPool` in row-major tile order to capture the baseline RGBA buffer. Done when: baseline buffers are non-blank and complete for every page.
- [ ] Render the same pages using an 8-worker `RenderPool` (`RenderPool(8)`) with tile submission order shuffled via a seeded `std::mt19937_64` PRNG and concurrent `DisplayList` reads across all 8 workers. Done when: all 8 workers actively participate in tile rendering.
- [ ] Assert strict byte-for-byte RGBA equality (`expect_rgba_equal(..., /*max_channel_delta=*/0)`) between the 1-worker and 8-worker renders for every page of all 10 PDFs. Done when: `0` mismatched pixels exist across the entire 10-document run. Cheaper substitute: comparing only image dimensions or allowing a perceptual pixel tolerance.
- [ ] Assert `debug_live_documents() == 0`, `debug_live_display_lists() == 0`, and `debug_live_workers() == 0` after the 1-worker and 8-worker `Runtime` instances shut down. Done when: all three counters equal `0`.
- [ ] Run `ThreadSafety.Determinism` under AddressSanitizer (`-EnableASAN`). Done when: the ASan run exits 0 with 0 errors.
- [ ] Commit: `test: implement ThreadSafety.Determinism (1 worker vs 8 shuffled workers)`

**Test checkpoint:** `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter "ThreadSafety.Determinism"` exits 0 and quotes the 10 verified corpus PDFs and total pages/bytes matched.

## 7. Section 4.9 Battery Part 2: `CancellationStorm` (30 s) and `MultiDocumentStress`

Implement the remaining two mandatory tests of the foundation plan section 4.9 Thread-Safety Verification Battery in `tests/recto-tests/thread_safety_tests.cpp` and run the complete battery under AddressSanitizer:
- **`ThreadSafety.CancellationStorm`:** Generate a deterministic **200-page mixed text/vector/image PDF** in `TempDir`, open it on a `Runtime` with **8 `RenderPool` workers**, and drive **1,000 random viewport jumps and zoom changes** over **30 seconds**, tripping `CancelToken`s every **5 ms** under AddressSanitizer (`0` use-after-free, `0` data races, `0` leaks, `0` deadlocks).
- **`ThreadSafety.MultiDocumentStress`:** Open **20 documents concurrently** on 20 `DocumentActor` threads while simultaneously requesting `DisplayList`s, rendering tiles across the shared `RenderPool`, querying outlines/links/descriptions, and closing documents in staggered order, asserting `debug_live_documents() == 0`, `debug_live_display_lists() == 0`, and `debug_live_workers() == 0` on shutdown.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Every number in foundation plan section 4.9 must be literal (`200` pages, `8` workers, `1,000` viewport/zoom jumps, `5 ms` cancellation cadence, `30` seconds duration for `CancellationStorm`, and `20` concurrent documents for `MultiDocumentStress`) and executed under AddressSanitizer.

**Build order:**

1. Implement the 200-page PDF generator helper, `ThreadSafety.CancellationStorm`, and `ThreadSafety.MultiDocumentStress` in `tests/recto-tests/thread_safety_tests.cpp`. Done when: `recto-tests.vcxproj` compiles cleanly on `x64` and `ARM64`.
2. Run `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter "ThreadSafety.*"` and verify all 3 battery tests (`Determinism`, `CancellationStorm`, `MultiDocumentStress`) pass under ASan. Done when: all 3 tests exit 0 with 0 ASan errors and 0 live objects remaining.

- [ ] Build a deterministic 200-page PDF fixture generator in `thread_safety_tests.cpp` (each page containing vector paths, clipped shapes, and multi-line text so `DisplayList` compilation and tile replay perform substantial work). Done when: the generated 200-page PDF opens cleanly with `page_count == 200`.
- [ ] Implement `ThreadSafety.CancellationStorm` opening the 200-page PDF on an 8-worker `Runtime` and firing `>= 1,000` random `(page_index 0..199, zoom 50%..400%, rotation 0/90/180/270)` `TileScheduler::update_viewport` jumps over a full `30-second` window with `5 ms` `CancelToken` trips. Done when: the test runs for `>= 30.0 s`, executes `>= 1,000` viewport jumps, and records both cancelled and completed tile counts. Cheaper substitute: running 10 jumps in 100 ms and calling it a cancellation storm.
- [ ] Destroy `Runtime` while tiles are still in flight at the end of `ThreadSafety.CancellationStorm` to exercise the section 4.8 6-step shutdown sequence under maximum contention. Done when: `Runtime` joins all threads without deadlock and all Debug live-object counters equal `0`.
- [ ] Implement `ThreadSafety.MultiDocumentStress` launching **20 concurrent `DocumentActor` instances** across corpus PDFs while 8 `RenderPool` threads and 4 client requester threads simultaneously request descriptions, outlines, links, `DisplayList`s, and tile renders, then close actors in randomized waves. Done when: all 20 actors open, serve concurrent requests, and close cleanly.
- [ ] Assert `debug_live_documents() == 0`, `debug_live_display_lists() == 0`, and `debug_live_workers() == 0` at the end of `ThreadSafety.MultiDocumentStress`. Done when: zero leaked documents, display lists, or workers remain.
- [ ] Execute the full `ThreadSafety.*` suite (`Determinism`, `CancellationStorm`, `MultiDocumentStress`) under AddressSanitizer (`-EnableASAN`). Done when: `recto-tests.exe --gtest_filter=ThreadSafety.*` passes with 0 ASan errors.
- [ ] Commit: `test: complete section 4.9 Thread-Safety Verification Battery under ASan`

**Test checkpoint:** `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter "ThreadSafety.*"` exits 0 with all 3 battery tests (`Determinism`, `CancellationStorm`, `MultiDocumentStress`) passing.

## 8. `recto::cli::Dispatcher` in its own unit and the read-only `Dispatcher::verb_names()`

The verb registry of `D04 T08 §1` and `§2` (`recto-cli verbs --json` and its `Cli.VerbsCoverage` gate) must compare the dispatcher's verbs with their specifications from inside `recto-tests`, but `§1` puts the command table in `src/cli/main.cpp`, which no test can link. This section moves the table, the shared flag parser and the exit-code mapping into `src/cli/dispatcher.h/.cpp` behind a `recto::cli::Dispatcher` class built as a static library that both `recto-cli` and `recto-tests` link, and adds read-only accessors: the verb names, each verb's subverbs and verdict exit codes, a registration call that refuses duplicates, and one version constant. Acrobat Pro has no command line; `recto-cli` is a Recto improvement that D18 keeps and D21 makes the automation surface. It must not change any behaviour of `§1` to `§3`: the same verbs, flags, exit codes and output bytes.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the move is a restructuring only: every verb still drives `recto-core` (`Runtime`, `DocumentHandle` / `DocumentActor`) through its public headers exactly as `§1` requires, and the new accessors read the static command table only, open no document, start no `Runtime` and touch no thread.

**Build order:**

1. Move the command table, flag parser and exit-code mapping from `src/cli/main.cpp` into `src/cli/dispatcher.h/.cpp` and a static library `src/cli/recto-cli-lib.vcxproj` referenced by `src/cli/recto-cli.vcxproj` and `tests/recto-tests/recto-tests.vcxproj`, leaving `main.cpp` as `wmain`, UTF-8 conversion and `Dispatcher::run`. Done when: `.\build.cmd` builds both projects on `x64` and `ARM64` and every existing `Cli.*` test passes unchanged.
2. Add the accessors and `tests/recto-tests/cli_dispatcher_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Cli.Dispatcher*` passes under ASan.

- [ ] Define `VerbEntry { std::string_view name; std::span<const std::string_view> subverbs; std::string_view usage; std::span<const int> verdict_exit_codes; std::span<const std::string_view> secret_flags; bool takes_input_pdf; ExitCode (*run)(VerbContext&); }` and `enum class ExitCode : int { Success = 0, Usage = 1, FileError = 2, Password = 3 }` in `src/cli/dispatcher.h`. Done when: `info`, `render`, `bench` and `stress` are registered through `VerbEntry` and `recto-cli --help` prints the same bytes as the golden captured before the move.
- [ ] Register every verb in one function `register_builtin_verbs(Dispatcher&)` in `src/cli/verb_table.cpp`, with `Dispatcher::register_verb(VerbEntry)` refusing a duplicate name or a name outside `[a-z][a-z0-9-]{0,31}` and returning an error that names the verb. Done when: `Cli.DispatcherRegistration` refuses a second `info`, `Info` and `bad_name`.
- [ ] Implement `Dispatcher::verb_names() const -> std::vector<std::string>` returning the table's top-level verbs sorted and unique; `--help` and `--version` are global flags and are not listed. Done when: `Cli.DispatcherVerbNames` reads exactly `bench`, `info`, `render`, `stress` from the M1 table and a test dispatcher with two registrations made in reverse order returns them sorted.
- [ ] Implement `Dispatcher::subverb_names(std::string_view verb) const` and `Dispatcher::verdict_exit_codes(std::string_view verb) const`, each returning `std::nullopt` for an unknown verb. Done when: `Cli.DispatcherVerbNames` reads the subverbs and verdict codes of a test verb and `std::nullopt` for `no-such-verb`.
- [ ] Define one `recto::cli::version_string()` in `src/cli/dispatcher.cpp`, used by `--version` and available to `D04 T08 §2` as `recto_version`. Done when: `Cli.DispatcherVersionMatches` finds `recto-cli --version` output equal to `version_string()`.
- [ ] Commit: `cli: move the dispatcher into its own unit and add read-only verb accessors`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.Dispatcher*:Cli.HelpAndVersion:Cli.Info*` passes under ASan, and letting `register_verb` accept a duplicate name fails `Cli.DispatcherRegistration`.

## 9. `--password env:NAME` in the shared flag parser, never echoed or logged

A password on a command line is visible to every process that can list command lines, and lands in shell history and agent logs. `recto-mcp` (`D04 T08 §8`) passes each secret as `--flag env:RECTO_MCP_SECRET_<n>`, and the action wizard (`D04 T07`) takes `--password env:NAME` too, so the shared flag parser must resolve a value of the form `env:NAME` from the named environment variable. Acrobat Pro's batch tools ask for passwords in dialogs; this keeps Recto's scriptable `recto-cli` without putting secrets in arguments or logs (AGENTS.md credentials rule). It must not change `§1`: the literal `--password <pw>` form, exit `3` for a missing or wrong password, and exit `1` for usage errors keep their meaning.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that secret resolution happens in the CLI argument layer before any `Runtime` call, that the resolved password reaches the `DocumentActor` through the same `authenticate` message as the literal form (moved into the message, never shared between threads), and that nothing else in the actor contract changes.

**Build order:**

1. Implement `resolve_secret_arg(std::string_view value) -> SecretResult` and `SecretString` in `src/cli/secret_arg.h/.cpp`, and call it from the shared parser in `src/cli/dispatcher.cpp` for `--password` and for every flag listed in a verb's `VerbEntry::secret_flags`. Done when: `recto-cli-lib` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/cli_secret_tests.cpp` running `recto-cli.exe` as a child process with a controlled environment. Done when: `recto-tests.exe --gtest_filter=Cli.PasswordEnv*:Cli.SecretFlagsShared` passes under ASan.

- [ ] Resolve `env:NAME`, with `NAME` matching `[A-Za-z_][A-Za-z0-9_]{0,127}`, through `GetEnvironmentVariableW` (size query first, then read) and convert UTF-16 to UTF-8 with the same routine `wmain` uses for literal arguments, so both forms hand identical bytes to `authenticate`. Done when: `Cli.PasswordEnvUnlocks` runs `recto-cli info render/encrypted-aes-256.pdf --password env:RECTO_TEST_PW` with `RECTO_TEST_PW=recto-test` in the child environment and exits `0`.
- [ ] Exit `1` with a `stderr` message naming the variable (never a value) when the variable is not set or `NAME` is invalid, before any file is opened or written; a variable that is set but empty is an empty password. Done when: `Cli.PasswordEnvMissing` exits `1` for `env:RECTO_UNSET_VAR` and `env:1BAD`, writes no file for `render --out`, and an empty `RECTO_TEST_PW` exits `3` on `encrypted-aes-256.pdf`.
- [ ] Map a wrong password read from the environment to exit `3` with the same diagnostic as the literal form. Done when: `Cli.PasswordEnvWrong` exits `3` with `RECTO_TEST_PW=wrong` and its `stderr` equals the literal-form `stderr` with the value replaced by `env:RECTO_TEST_PW`.
- [ ] Hold the resolved value in `SecretString` (`src/cli/secret_arg.h`): no stream operator or formatter, zeroed with `SecureZeroMemory` on destruction and after a move, and printed in diagnostics only as `env:NAME`. Done when: `Cli.PasswordEnvNeverEchoed` runs `info`, `info --json` and `render` with the right, a wrong (a canary string) and a missing variable and finds neither `recto-test` nor the canary in any `stdout`, `stderr` or output file. Cheaper substitute: masking the value with a regex over finished output.
- [ ] Treat every `--password` value that begins with `env:` as a variable reference (a literal password with that prefix is passed through a variable), and state it with the `env:NAME` form in `recto-cli --help`. Done when: `Cli.HelpAndVersion` finds `env:NAME` in the help text and `Cli.PasswordEnvPrefixIsReference` exits `1` for `--password env:`.
- [ ] Apply the same resolution to every flag a verb lists in `VerbEntry::secret_flags`. Done when: `Cli.SecretFlagsShared` registers a test verb with a secret `--owner-password` and receives the variable's value through `env:NAME`.
- [ ] Keep `§1`'s literal form unchanged. Done when: `Cli.InfoEncryptedPasswordHandling` passes unmodified.
- [ ] Commit: `cli: accept env:NAME for passwords and keep secrets out of output`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.PasswordEnv*:Cli.SecretFlagsShared:Cli.InfoEncryptedPasswordHandling` passes under ASan, and printing the resolved value in the wrong-password diagnostic fails `Cli.PasswordEnvNeverEchoed`.

## 10. Verb extension rule: later verb families register under the `§1` exit-code contract

Sections in D02 to D08 add verbs to `recto-cli`: `annot`, `forms`, `bookmarks`, `count`, `toc`, `media`, `snapshot`, `math`, `a11y`, `rasterize-pdf`, `export-area`, `policy`, `edit` and others, plus new flags on existing verbs such as `ocr --zone`. Acrobat Pro automates through Action Wizard and scripts; Recto's equivalent is one command line with one predictable contract (D21). This section writes the rule every later verb follows and a contract test that iterates `Dispatcher::verb_names()` (`§8`), so every verb registered later is checked automatically without editing this frozen file: register through `register_builtin_verbs`, parse flags with the shared parser (`§8`, `§9`), keep exit codes `0` to `3` with their `§1` meanings, use codes `4` and above only as declared verdicts (as `preflight` does in `D04 T06 §15`), print `--json` only to `stdout`, and write files only by temp-file-then-replace. It must not change `§1`'s existing behaviour for `info`, `render`, `bench` or `stress`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the rule requires every verb to reach a document only through `Runtime` and `DocumentActor`, or through a job that opens its own `recto::pdf::Document` (foundation plan section 4), never through `recto-pdf` calls on an instance an actor owns, and that the contract test opens only corpus files through the `recto-cli.exe` child process.

**Build order:**

1. Write the rule as the header comment of `src/cli/verb_table.cpp` and enforce its registration half in `Dispatcher::register_verb` (`src/cli/dispatcher.cpp`). Done when: `Cli.DispatcherRegistration` refuses a verb registered with verdict code `2`.
2. Add `tests/recto-tests/cli_contract_tests.cpp` with `Cli.DispatcherContract` iterating `verb_names()`. Done when: `recto-tests.exe --gtest_filter=Cli.DispatcherContract` passes under ASan for the M1 verbs.

- [ ] Write the rule in the header comment of `src/cli/verb_table.cpp`: one `VerbEntry` per verb in `register_builtin_verbs` with at least one valid contract invocation per subverb that takes a PDF, the shared flag parser, exit codes `0` success, `1` usage, `2` unreadable or damaged file, `3` password (`§1`), declared verdict codes `4` and above, `--json` on `stdout` only, temp-file-then-replace writes, and the verb families named above. Done when: the comment names each obligation and cites `D01 T03 §1`.
- [ ] Refuse in `Dispatcher::register_verb` a verdict code outside `4` to `63` and a verb whose `secret_flags` names a flag missing from its `usage`. Done when: `Cli.DispatcherRegistration` refuses verdict codes `2` and `64` and an undeclared secret flag.
- [ ] Add per-verb help: `recto-cli <verb> --help` prints the verb's `usage` to `stdout` and exits `0`, without changing the global `--help`. Done when: `Cli.DispatcherContract` passes the help probe for every verb in `verb_names()`.
- [ ] Probe every verb in `verb_names()` in `Cli.DispatcherContract`: `<verb> --no-such-flag` exits `1` with usage on `stderr` and creates no file in the child's working folder, and the verb appears in `recto-cli --help`. Done when: a test verb whose unknown-flag path exits `2` fails the probe, and removing a verb's line from the global help fails it.
- [ ] Add `struct ContractInvocation { std::span<const std::string_view> args; std::string_view valid_input; int valid_exit; }` and `VerbEntry::contract_invocations` in `src/cli/dispatcher.h`: `args` is a complete valid argument list (subverb, required flags, extra inputs as corpus paths, outputs) using the placeholders `{in}` for the input under test, `{corpus}` for `tests/corpus/` and `{out_dir}` for a fresh empty folder; `valid_input` is a corpus path that makes the invocation succeed; `valid_exit` is `0` or one of the verb's declared verdict codes. `Dispatcher::register_verb` refuses a verb with `takes_input_pdf` and no invocation, a verb with subverbs whose invocations do not cover each subverb, and an invocation without exactly one `{in}`. Done when: `Cli.DispatcherRegistration` refuses all three, and the M1 table registers `info {in}`, `render {in} --pages 1 --out {out_dir}`, `bench {in} --iterations 1` and `stress {in} --seconds 1` on `render/simple-1p.pdf`.
- [ ] Probe each contract invocation of every verb with `takes_input_pdf` set in `Cli.DispatcherContract`: first run it with `valid_input` and require `valid_exit`, then replace only `{in}` with `{corpus}broken/non-pdf-header.pdf` and require exit `2`, then with `{corpus}render/encrypted-aes-256.pdf` and no password and require exit `3`, each later run leaving `{out_dir}` empty. Done when: `Cli.DispatcherContract` passes for `info`, `render`, `bench` and `stress`, and a test verb whose registered invocation lacks a required flag fails the valid-run step instead of passing on a usage exit `1`.
- [ ] Commit: `cli: add the verb extension rule and the dispatcher contract test`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.DispatcherContract:Cli.DispatcherRegistration` passes under ASan, and registering a test verb that exits `2` on an unknown flag fails `Cli.DispatcherContract`.

## 11. Dispatcher and `recto-cli verbs --json` agreement gate for every registered verb

`D04 T08 §2` exports the verb registry (`recto-cli verbs --json`) that `recto-mcp` and agents read, and its `Cli.VerbsCoverage` checks the in-process registry against `Dispatcher::verb_names()`. This gate checks the exported document itself against the dispatcher, so a verb added to the table under the `§10` rule but missing from `verbs --json`, or described there with other subverbs, exit codes or secret flags than it registers, fails a test. It keeps Recto's automation surface (D21) truthful about the CLI, which Acrobat Pro does not offer. It must not change `§1`'s exit-code meanings or any verb's behaviour.

**Phase:** 5
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that the gate runs `recto-cli.exe verbs --json` as a child process, that `verbs` reads static tables only and opens no document, and that no verb's path through `Runtime` and `DocumentActor` changes.

**Build order:**

1. Add `tests/recto-tests/cli_verbs_agreement_tests.cpp` parsing `recto-cli verbs --json` with `BoundedJson` (`D04 T06 §2`) and comparing it with `Dispatcher`. Done when: `recto-tests.exe --gtest_filter=Cli.VerbsJson*` passes under ASan.

- [ ] Assert in `Cli.VerbsJsonListsEveryVerb` that every name from `Dispatcher::verb_names()` and every subverb from `subverb_names` appears in the exported `verbs[]`. Done when: removing the spec of `stress` from the registry fails the test naming `stress`.
- [ ] Assert in `Cli.VerbsJsonExitCodesAgree` that each exported verb lists `0` as `Success`, `1` and `2` with their `§1` meanings, `3` when it has a password or secret flag, and exactly the `verdict_exit_codes` the dispatcher registered as `Diagnostic`. Done when: a test verb registered with verdict code `5` whose exported spec omits `5` fails the test.
- [ ] Assert in `Cli.VerbsJsonSecretsAgree` that every flag in a verb's `secret_flags` is exported as a `Secret` argument accepted as `env:NAME`. Done when: exporting `--password` as a `String` fails the test.
- [ ] Commit: `test: gate recto-cli verbs --json against the dispatcher table`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.VerbsJson*` passes under ASan, and deleting one verb's spec fails `Cli.VerbsJsonListsEveryVerb`.

## Verification

- M1 definition of done (`docs/plans/2026-10-13-recto-foundation.md` section 7): all three tests in the section 4.9 Thread-Safety Verification Battery pass under ASan, and `fuzz_open` runs 1 hour clean on the seed corpus.
- `recto-tests.exe --gtest_filter=Cli.Dispatcher*:Cli.PasswordEnv*:Cli.SecretFlagsShared:Cli.DispatcherContract` passes under ASan once `§8` to `§10` ship, and `Cli.VerbsJson*` passes once `§11` (phase 5) ships.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
