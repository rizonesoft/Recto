---
schema_version: 1
id: corpus-and-suites
domain: 06-quality
status: active
title: "TODO-01 -- Golden, Text, Round-Trip and Performance Suites"
track: M1
---

# TODO-01 -- Golden, Text, Round-Trip and Performance Suites

> **Goal:** Build the verification suites that close Milestones M1 through M6: pixel-exact golden render comparisons (`Goldens.*`) with a disciplined `--refresh-goldens` workflow and cross-architecture (`x64` vs `ARM64`) pixel-identity gate, per-file text extraction and search expectation tables (`TextTables.*`) plus Unicode normalization and regex adversarial rows, a services-level conversion and document-operations round-trip matrix (`RoundTrips.*`) covering every M4 and M5 format and security engine, and the automated performance harness (`tests/perf/`) with dated reference-machine baselines (`docs/perf/`).

> **Current state:** Measured 2026-10-14 (re-measured 2026-10-23: `tests/recto-tests` holds the M0 and `D01 T01` unit suites, `tests/corpus/render/` holds 8 PDFs and no `goldens/` folder, and none of `Goldens.*`, `TextTables.*`, `RoundTrips.*`, `PerfSmoke.*`, `tests/perf/` or `docs/perf/` exists). No suites of this file exist yet. `D00 T03` provides the test runner helpers (`test_support.h/.cpp`), corpus folders (`render/`, `text/`, `convert/`, `broken/`, `seeds/`), `PROVENANCE.md` enforcement, and the MSVC AddressSanitizer build configuration. Individual feature sections prove their own slices; this file turns those slices into maintained regression suites with explicit refresh and baseline rules so later optimizations or `recto-pdf` changes are judged against committed evidence, never memory. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI references rewritten against `todo/10-ui/`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.11, 7 (each milestone's done-when verification gate), and 8 (the five performance targets and the reference-machine rule).
- `tests/corpus/README.md` and `tests/corpus/PROVENANCE.md` built by `D00 T03 §5` (the golden refresh rules extend, never bypass, corpus provenance).
- -> XREF: D00 T03 §2 -- the RGBA pixel comparison (`compare_rgba`, `expect_rgba_equal`, `compute_ssim`) and SHA-256 helpers (`sha256_hex`) consumed by `Goldens.*` (`§1`, `§2`) and `RoundTrips.*` (`§5`, `§6`).
- -> XREF: D00 T03 §5 -- the corpus folders and `PROVENANCE.md` integrity suite that `§1` through `§7` consume and extend.
- -> XREF: D01 T03 §2 -- `recto-cli render` shares the deterministic rendering pipeline exercised by `Goldens.*` in `§1` and `§2`.
- -> XREF: D01 T03 §3 -- `recto-cli bench` provides the engine cold-open, first-page render, and 500-page RSS telemetry consumed by `§7` and `§8`.
- -> XREF: D01 T03 §7 -- the M1 exit checklist (`D01 T03 §7`) and `Goldens.*` refresh gate (`§2`) precede the M2 performance harness (`§7`).
- -> XREF: D02 T01 §7 -- `PerfSmoke` and `run_perf.ps1` (`§7`) exercise the full viewer shell (`D02 T01 §7`, `§11`), side panels (`D02 T03 §6`, `§7`, `§8`, `§10`), and theme/motion settings (`D02 T04 §7`, `§8`).
- -> XREF: D03 T01 §9 -- `TextTables.*` (`§4`) verifies `Replace Text` (`D03 T01 §9`) and `Compare PDFs` (`D03 T01 §10`) text-diff expectations alongside normalization and regex rules.
- -> XREF: D04 T02 §4 -- `RoundTrips.Formats` (`§5`) exercises `PDF -> SVG/XPS` (`D04 T02 §4`), `PDF -> Plain Text` (`D04 T02 §5`), `Searchable PDF (OCR)` (`D04 T02 §7`), `PdfOptimizer`/`Linearizer` (`D04 T02 §11`), and the Convert UI (`D04 T02 §12`).
- -> XREF: D04 T03 §4 -- `RoundTrips.DocumentsAndSecurity` (`§6`) exercises the queue frame (`D04 T01 §7`), `DocxWriter`/`RtfWriter` (`D04 T03 §4`), `XlsxWriter`/`WebDocWriter`/`ComplianceEngine` (`D04 T05 §2`, `§4`, `§8`, `§9`), and all Organize & Security engines (`D05 T01 §10`, `D05 T02 §6`, `§8`, `§9`, `§11`).
- -> XREF: D04 T04 §6 -- `RoundTrips.DocumentsAndSecurity` (`§6`) exercises the 100% native `Office/Legacy Binary Office/HTML/MD -> PDF` engines (`D04 T04 §1` through `§7`).
- -> XREF: D07 T02 §6 -- the M6 Store & Release Compliance verification (`D07 T02 §6`) gates on `RoundTrips.*` (`§5`, `§6`) and the reference-machine performance baseline (`§8`) built here.
- -> XREF: D06 T03 §10 -- user-reported render classes (area-filtered minification, blend modes, soft masks, open paths) added to the golden harness built here, and the release-over-release diff in §12 there.
- -> XREF: D10 T01 §5 -- the compositor commit that §7's cold-start gate waits for before recording `t1`.
- -> XREF: D99 T01 §4 -- the operator confirms the first green `goldens-arm64` run of §2 and then drops `continue-on-error`.

## Outcome

- Every `recto-pdf` rendering change produces either silence (meaning pixel-identical output across `x64` and `ARM64`) or a named, reviewable golden diff with side-by-side actual and difference PNGs written to the test output directory.
- Text extraction, search hit counts, hit contexts, OCR pre/post text layers, and Unicode normalization rules (`NFKC` ligatures, soft-hyphen elision, non-ASCII case folding, whole-word boundaries, and ECMAScript regex) are data-driven tables under `tests/corpus/text/expectations/` verified by `TextTables.*`.
- Every M4 and M5 converter and document engine (`PDF <-> Images/TIFF/GIF/SVG/XPS`, Plain Text, Searchable OCR PDF, `TableExtractor`, `ImageExtractor`, `PdfOptimizer`, `Linearizer`, `DocxWriter`, `RtfWriter`, `XlsxWriter`, `WebDocWriter`, `ComplianceEngine` PDF/A & PDF/UA & ZUGFeRD/Factur-X, 100% native `Office/Legacy Binary Office (.doc/.xls/.ppt)/HTML/Markdown -> PDF`, graft `merge`/`split`/`edit`, watermarks/Bates, AES-256 encryption, PKCS#7/PAdES signatures, and physical redaction/sanitization) runs in `RoundTrips.*` against `tests/corpus/convert/` with format-specific structural and visual oracles.
- Foundation plan section 8's five performance targets are measured by an automated scriptable harness (`tests/perf/`) and recorded with median and IQR spread on the `D00 T01 §5` primary reference machine under `docs/perf/`.

**Adjacency:** all=not-applicable (automated test suites, data expectation tables, and benchmark harnesses over engine and services code; no user-facing product UI surfaces)

**Adjacency rationale:** The surfaces of these suites are `recto-tests` console/XML output, diff PNG artifacts in the ignored build output folder, and committed expectation/baseline files under `tests/corpus/` and `docs/perf/`. The golden refresh discipline (`§2`) and baseline recording discipline (`§8`) are engineering process gates documented in `tests/corpus/README.md` and `docs/perf/README.md`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Pixel-exact golden render comparison harness (`Goldens.*` in `recto-tests`) | D01 T03 §2, D00 T03 §5, D01 T02 §15, D01 T02 §16, D01 T02 §17 | [ ] |
| 2 | §2 | Disciplined golden refresh tool (`--refresh-goldens`) and `x64` vs `ARM64` pixel gate | §1 | [ ] |
| 3 | §3 | Per-file text extraction and find expectation tables (`expectations/` & `TextTables.*`) | D03 T01 §5, D03 T01 §8, D03 T01 §23 | [ ] |
| 4 | §4 | Normalization and regex adversarial verification rows (ligatures, soft hyphens, bidi) | §3, D03 T01 §9, D03 T01 §10, D01 T02 §18 | [ ] |
| 5 | §5 | Conversion round-trip matrix Part 1 (`RoundTrips.Formats`: Images/SVG/XPS, OCR, Tables) | D04 T02 §4, D04 T02 §5, D04 T02 §7, D04 T02 §11, D04 T02 §12 | [ ] |
| 6 | §6 | Conversion round-trip matrix Part 2 (`RoundTrips.DocumentsAndSecurity`: Word, Sign) | §5, D04 T01 §7, D04 T03 §4, D04 T04 §5, D04 T04 §6, D04 T04 §7, D04 T05 §2, D04 T05 §4, D04 T05 §8, D04 T05 §9, D05 T01 §10, D05 T02 §6, D05 T02 §8, D05 T02 §9, D05 T02 §11 | [ ] |
| 7 | §7 | Automated performance targets harness (`tests/perf/` and `recto-cli bench`) | §2, D01 T03 §3, D01 T03 §7, D02 T01 §7, D02 T01 §11, D02 T02 §6, D02 T03 §6, D02 T03 §7, D02 T03 §8, D02 T03 §10, D02 T04 §7, D02 T04 §8, D00 T01 §5, D10 T01 §2, D10 T01 §5 | [ ] |
| 8 | §8 | Primary reference-machine M2/M6 performance baseline record (`docs/perf/`) | §7 | [ ] |
| 9 | §9 | Damaged-fax corpus compared with Acrobat Pro, Foxit, PDFelement and UPDF | D06 T05 §34 | [ ] |
| 10 | §10 | Save time and file size compared with Acrobat Pro, Foxit, PDFelement and UPDF | §7, D01 T02 §5, D04 T02 §2 | [ ] |

## 1. Pixel-exact golden render comparison harness (`Goldens.*` in `recto-tests`)

> **Groomed 2026-10-23:** tiles are `512 x 512` (the text said `256 x 256`, which no engine section uses), and `Depends On` now includes `D01 T02 §15`, `§16` and `§17`: those sections add render fixtures (`transparency-groups.pdf`, `shadings-1-to-7.pdf`, `non-embedded-fonts.pdf` and others) to `tests/corpus/render/`, and `Goldens.AllRenderCorpusFilesHaveGoldens` would otherwise fail on every fixture added after the first golden set.

Turn deterministic rendering into a permanent per-commit contract: every file in `tests/corpus/render/` renders across a fixed matrix of DPIs (`72`, `144`, `300`) and rotations (`0`, `90`, `180`, `270` degrees) through the exact same `DocumentActor` + `RenderPool` tile-compositing pipeline used by `recto-cli render`, and compares pixel-for-pixel (`max_channel_delta == 0`, `mismatched_pixel_count == 0`) against committed golden PNGs and SHA-256 manifests under `tests/corpus/render/goldens/`. On any mismatch, the suite writes `<stem>-actual.png` and a `4x`-amplified `<stem>-diff.png` into `build/test-failures/goldens/` and fails naming the corpus file, page index, DPI, rotation, first mismatched `(x, y)` coordinate, and channel delta.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `tests/recto-tests/golden_render_tests.cpp` (`Goldens.*`) enumerating every `.pdf` in `tests/corpus/render/`, rendering each page across the DPI/rotation matrix via `Runtime` (`1` worker and `4` workers), and comparing against `tests/corpus/render/goldens/<stem>_p<page>_dpi<dpi>_rot<rot>.png` using `recto::test::compare_rgba`. Done when: `recto-tests --gtest_filter=Goldens.*` runs every render-corpus file and fails if any `.pdf` in `tests/corpus/render/` lacks its golden set.
2. Add diagnostic failure artifact emission (`build/test-failures/goldens/<stem>_p<page>_dpi<dpi>_rot<rot>_actual.png` and `_diff.png`) and commit the initial baseline goldens under `tests/corpus/render/goldens/` with their SHA-256 entries registered in `tests/corpus/PROVENANCE.md`. Done when: a planted 1-LSB pixel modification in a golden PNG fails `Goldens.*` with exact coordinates and writes both `_actual.png` and `_diff.png`.

- [ ] Enumerate `tests/corpus/render/*.pdf` dynamically in `Goldens.AllRenderCorpusFilesHaveGoldens` and fail immediately if a newly added corpus PDF has no matching golden PNG entries in `tests/corpus/render/goldens/`. Done when: dropping an untracked `new.pdf` into `tests/corpus/render/` fails `Goldens.AllRenderCorpusFilesHaveGoldens` naming `new.pdf`. Cheaper substitute: hard-coding a static list of filenames in C++ so new corpus PDFs silently skip golden comparison.
- [ ] Render each corpus page at `72 DPI` (`0`, `90`, `180`, `270` deg), `144 DPI` (`0` deg), and `300 DPI` (`0` deg) through `Runtime` + `DocumentActor` + `RenderPool` tile composition (`512 x 512` tiles, the engine tile size of `D01 T02 §6`). Done when: `Goldens.RenderMatrixMatchesCommittedGoldens` exercises all 6 render configurations per page on every file in `tests/corpus/render/`.
- [ ] Enforce zero pixel tolerance (`max_channel_delta == 0` and `mismatched_pixel_count == 0` via `recto::test::expect_rgba_equal`) on every golden comparison. Done when: a 1-LSB change (`R=200 -> R=201`) in a single pixel of `vector-shapes_p1_dpi144_rot0.png` fails the test with the exact `(x, y)` coordinate and channel values.
- [ ] Emit `<name>_actual.png`, `<name>_expected.png`, and a `4x`-amplified red-highlighted `<name>_diff.png` into `build/test-failures/goldens/` whenever `compare_rgba` reports `mismatched_pixel_count > 0`. Done when: the planted 1-pixel failure test verifies that all three diagnostic PNGs exist on disk and are readable via WIC.
- [ ] Verify that `1`-worker and `4`-worker `RenderPool` runs produce byte-identical RGBA buffers matching the committed golden SHA-256 for every page in `tests/corpus/render/`. Done when: `Goldens.SingleAndMultiWorkerMatchGoldens` passes under ASan.
- [ ] Register all golden PNGs and `tests/corpus/render/goldens/manifest.json` in `tests/corpus/PROVENANCE.md` so `Corpus.Provenance` (`D00 T03 §5`) verifies their SHA-256 checksums. Done when: `recto-tests --gtest_filter=Corpus.Provenance` passes with the golden files included.
- [ ] Add reviewed glyph-sheet goldens of the fourteen standard fonts drawn through their Windows faces (every AFM glyph of each, in the four styles of each Latin family, Symbol and ZapfDingbats), so a wrong glyph or style shows as a pixel change (filed by the `D01 T02 §27` completion review, `docs/reviews/01-engine/D01-T02-s27.md`). Done when: `Goldens.Standard14Sheets` compares each sheet against its committed golden and the goldens are in the manifest. -> XREF: D01 T02 §27
- [ ] Commit: `test: add pixel-exact Goldens.* render comparison suite and baseline goldens`

**Test checkpoint:** `recto-tests --gtest_filter=Goldens.*:Corpus.Provenance` passes on `x64` Debug, Release, and ASan, and the planted 1-pixel failure output naming page, DPI, rotation, `(x, y)` coordinate, and `_diff.png` path is quoted.

## 2. Disciplined golden refresh tool (`--refresh-goldens`) and `x64` vs `ARM64` pixel gate

Provide a single deterministic command (`python scripts/refresh-goldens.py` driving `recto-tests --refresh-goldens` or `recto-cli render`) that regenerates `tests/corpus/render/goldens/` and updates `manifest.json` and `PROVENANCE.md`, document the golden refresh governance rule in `tests/corpus/README.md` (a golden refresh must be an isolated commit whose message states the `recto-pdf` rendering change that caused the pixel delta), and build `Goldens.*` for both `x64` and `ARM64` (ARM64 is compile-and-link verified on the build host, foundation plan section 8) with a hosted `windows-11-arm` CI job that runs it natively, so any cross-architecture Direct2D/DirectWrite rasterization divergence fails in CI.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `--refresh-goldens` mode in `tests/recto-tests/golden_render_tests.cpp` and wrapper script `scripts/refresh-goldens.py` that re-renders all `tests/corpus/render/*.pdf` goldens, updates `tests/corpus/render/goldens/manifest.json` and `tests/corpus/PROVENANCE.md`, and prints a per-file summary (`unchanged`, `updated`, `added`, `removed`). Done when: running `python scripts/refresh-goldens.py` on an unchanged build prints `0 updated` and leaves `git diff` completely empty.
2. Document the golden refresh policy in `tests/corpus/README.md` and add a cross-architecture manifest verification check in `Goldens.CrossArchManifestParity`. Done when: `tests/corpus/README.md` defines the refresh commit rules and `Goldens.*` passes on `x64` (and on the `windows-11-arm` job of the last item below once it has run).

- [ ] Implement `RECTO_REFRESH_GOLDENS=1` / `--refresh-goldens` support in `golden_render_tests.cpp` that writes deterministic WIC PNGs (stripped of timestamps or host metadata chunks) to `tests/corpus/render/goldens/`. Done when: two consecutive `--refresh-goldens` runs produce byte-identical PNG files (`git status --porcelain` is empty). Cheaper substitute: PNG encoders that write creation timestamps (`tIME` chunks) causing every refresh to dirty all golden files in git.
- [ ] Write `scripts/refresh-goldens.py` to run `--refresh-goldens`, recompute SHA-256 hashes in `tests/corpus/render/goldens/manifest.json` and `tests/corpus/PROVENANCE.md`, and report exact pixel-delta counts for each changed golden. Done when: `python scripts/refresh-goldens.py --check` exits `0` when goldens are current and exits `2` listing out-of-date files when a golden is modified.
- [ ] Verify that the Direct2D WIC bitmap render target of `D01 T02 §6` (software rasterization, no GPU device) produces identical golden pixels regardless of host GPU vendor. Done when: `Goldens.CrossArchManifestParity` checks that every `manifest.json` entry carries one SHA-256 with no per-architecture variant and, on an `ARM64` process, that the rendered digests equal it (on `x64` the `ARM64` half is `GTEST_SKIP` with the reason printed).
- [ ] Add a `goldens-arm64` job to `.github/workflows/build.yml` on the hosted `windows-11-arm` runner that builds `Release|ARM64` and runs `recto-tests --gtest_filter=Goldens.*`, marked `continue-on-error: true` until the runner image is confirmed to carry the pinned MSVC toolset. Done when: the job exists in the workflow and `python scripts/check-actions-pins.py check` exits `0`. Cheaper substitute: asserting ARM64 parity on the x64 host, which proves nothing about ARM64 code generation. (The first green run is recorded by the operator in `D99 T01`; no section depends on it.)
- [ ] Document the golden refresh discipline in `tests/corpus/README.md`: (1) never refresh goldens to silence an unexplained failure, (2) inspect `_diff.png` for every changed page, (3) commit golden refreshes in a dedicated commit citing the owning `recto-pdf` section and cause. Done when: `tests/corpus/README.md` contains the `## Golden Refresh Discipline` section with exact command examples.
- [ ] Add unit test `Goldens.RefreshRoundTripInTempDir` that copies a subset of `render/` into a `TempDir`, corrupts one golden PNG, runs the refresh routine against the `TempDir`, and verifies the restored PNG matches the canonical SHA-256. Done when: `Goldens.RefreshRoundTripInTempDir` passes in `< 200 ms`.
- [ ] **Corrected 2026-10-19:** (was: `.github/workflows/ci.yml`) Wire `Goldens.*` into `.github/workflows/build.yml` so every PR runs pixel-exact golden verification in both Release and ASan jobs. Done when: CI workflow YAML includes `Goldens.*` in the test step without exclusion filters.
- [ ] Commit: `test: add deterministic --refresh-goldens tool and cross-arch golden parity gate`

**Test checkpoint:** `python scripts/refresh-goldens.py` on a clean tree produces an empty `git diff`, `python scripts/refresh-goldens.py --check` exits `0`, and `recto-tests --gtest_filter=Goldens.*` passes.

## 3. Per-file text extraction and find expectation tables (`expectations/` & `TextTables.*`)

> **Groomed 2026-10-23:** now depends on `D03 T01 §23` (same phase 3), which builds the M3 text fixtures and `tiny.rocr` under the exact file names listed below; this section writes the `expectations/<stem>.json` tables for them and builds no PDF itself.

Turn Milestone M3's text and search accuracy into a data-driven verification suite (`TextTables.*` in `tests/recto-tests/text_table_tests.cpp`): every PDF in `tests/corpus/text/` has a companion JSON expectation file in `tests/corpus/text/expectations/<stem>.json` specifying per-page extracted UTF-8 text (`PageText` lines, words, reading-order sequence, and bounding-box sanity bounds), pre-OCR vs post-OCR state (`needs_ocr`, post-OCR expected tokens for `scanned-english.pdf` and `vector-outlined-text.pdf`), and a table of `SearchEngine` queries (`query`, `match_case`, `whole_word`, `use_regex`, `expected_total_hits`, `expected_hits_per_page`, `first_hit_page`, `first_hit_context_snippet`, and `first_hit_quad_count`).

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the JSON expectation schema and author `tests/corpus/text/expectations/<stem>.json` for every document in `tests/corpus/text/` (`two-column-article.pdf`, `ligatures-and-hyphens.pdf`, `rotated-4way.pdf`, `cjk-rtl-bidi.pdf`, `scanned-english.pdf`, `vector-outlined-text.pdf`, and any additional M3 fixtures). Done when: every `.pdf` in `tests/corpus/text/` has a corresponding `.json` expectation file registered in `tests/corpus/PROVENANCE.md`.
2. Implement `tests/recto-tests/text_table_tests.cpp` (`TextTables.*`) that loads every corpus PDF and expectation JSON, verifies `PageText` extraction, runs `OcrPipeline` on `needs_ocr` fixtures, and executes every find-table row through both `recto::core::SearchEngine` and `recto-cli find --json`. Done when: `recto-tests --gtest_filter=TextTables.*` passes under ASan and fails if any `tests/corpus/text/*.pdf` lacks an expectation file.

- [ ] Enforce completeness in `TextTables.EveryTextCorpusFileHasExpectations`: enumerate `tests/corpus/text/*.pdf` and fail if any file is missing `tests/corpus/text/expectations/<stem>.json` or has an empty `find_cases` array. Done when: adding an unmapped PDF to `tests/corpus/text/` fails `TextTables.EveryTextCorpusFileHasExpectations` naming the file. Cheaper substitute: scattering ad-hoc `EXPECT_EQ` string checks across C++ test files with no check that every corpus PDF is covered.
- [ ] Verify per-page `PageText` extraction in `TextTables.PageTextMatchesExpectations`: compare extracted UTF-8 text, line count, word count, and non-empty monotonically ordered character `RectF` bounds against `<stem>.json`. Done when: all native-text corpus PDFs match their expected UTF-8 text and structural counts.
- [ ] Verify two-column and rotated reading order in `TextTables.ReadingOrderAndRotation`: confirm `two-column-article.pdf` extracts all of Column 1 before Column 2, and `rotated-4way.pdf` (`0`, `90`, `180`, `270` deg) produces identical reading order and upright selection quads on all 4 pages. Done when: `TextTables.ReadingOrderAndRotation` passes with 0 out-of-order words.
- [ ] Verify pre-OCR and post-OCR expectations in `TextTables.ScannedAndOutlinedOcr`: confirm `scanned-english.pdf` and `vector-outlined-text.pdf` return `0` characters and `needs_ocr == true` before OCR, and after running `OcrPipeline` (`D03 T01 §8`) match all required expected words and find-table hits. Done when: `TextTables.ScannedAndOutlinedOcr` passes on Windows 11 with the `en-US` recognizer and cleanly reports language-pack status.
- [ ] Execute every `find_cases` row in `<stem>.json` against `SearchEngine::search_document` and verify `expected_total_hits`, `expected_hits_per_page`, `first_hit_page`, `first_hit_context_snippet`, and non-degenerate highlight quads (`width > 0`, `height > 0`). Done when: all find rows across all corpus files pass in `TextTables.FindTableExpectations`.
- [ ] Verify CLI parity in `TextTables.CliTextAndFindParity`: run `recto-cli text` and `recto-cli find --json` on each corpus file and confirm output matches the in-process `PageText` and `SearchEngine` results byte-for-byte. Done when: `TextTables.CliTextAndFindParity` passes across the entire `tests/corpus/text/` folder.
- [ ] Commit: `test: add data-driven TextTables.* extraction and find expectation suite`

**Test checkpoint:** `recto-tests --gtest_filter=TextTables.*` passes under ASan with per-file expectation row counts quoted.

## 4. Normalization and regex adversarial verification rows (ligatures, soft hyphens, bidi)

Extend `tests/corpus/text/expectations/` and `TextTables.*` with adversarial rows that specifically test and prove each normalization and matching rule defined in `D03 T01 §5`: Unicode `NFKC` compatibility decomposition (`ﬁ`, `ﬂ`, `ﬀ`, `ﬃ`, `ﬄ` matched by ASCII `fi`, `fl`, `ff`, `ffi`, `ffl` and vice versa with accurate single-glyph highlight quad mapping), end-of-line soft-hyphen (`U+00AD`) elision across line wraps without stripping genuine intra-word hard hyphens (`self-contained`), Unicode case folding beyond ASCII (`Straße` <-> `STRASSE`, `Éclair` <-> `éclair`), Unicode-aware whole-word boundary rules at punctuation and CJK/Latin transitions, and ECMAScript regular expression queries with bounded execution time against catastrophic backtracking patterns.

> **Groomed 2026-10-23:** depends on `D01 T02 §18`, which adds `ErrorCode::ResourceLimit` and `ErrorCode::InvalidArgument` used here (cross-domain groom follow-up).

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add adversarial expectation rows to `tests/corpus/text/expectations/ligatures-and-hyphens.json` and `tests/corpus/text/expectations/cjk-rtl-bidi.json` covering every normalization rule and edge case. Done when: each normalization rule has both positive-match and negative-non-match rows in the JSON tables.
2. Add rule-inversion mutation unit tests in `tests/recto-tests/text_table_tests.cpp` (`TextTables.NormalizationRuleSensitivity` and `TextTables.RegexSafetyBounds`) proving that disabling any normalization flag in `SearchOptions` causes the corresponding expectation row to fail as predicted. Done when: all adversarial and rule-sensitivity tests pass under ASan.

- [ ] Add `NFKC` ligature decomposition rows in `ligatures-and-hyphens.json` testing ASCII queries (`"finance"`, `"flow"`, `"office"`, `"baffle"`) against `U+FB00..U+FB04` ligature glyphs and ligature queries (`"ﬁnance"`) against ASCII text, verifying that returned highlight quads tightly bound the ligature glyph without shifting subsequent character offsets on the line. Done when: `TextTables.LigatureDecompositionAndQuadAlignment` passes with character offset drift `== 0`. Cheaper substitute: normalizing the page string without maintaining a reverse index map, causing every highlight quad after a ligature on the same line to shift by 1 to 2 characters.
- [ ] Add soft-hyphen (`U+00AD`) line-wrap rows versus explicit hard-hyphen (`U+002D`) rows in `ligatures-and-hyphens.json` verifying that `"algorithm"` matches `"algo-\u00AD\nrithm"` (returning 2 highlight quads across the wrapped lines) while `"state-of-the-art"` still requires hyphens when queried literally. Done when: `TextTables.SoftVsHardHyphenAcrossLineWrap` passes and returns exactly 2 rects for the wrapped match.
- [ ] Add non-ASCII Unicode case-folding rows (`"straße"` matching `"STRASSE"` and `"Straße"`, `"éclair"` matching `"ÉCLAIR"`, Greek `"σ"`/`"ς"`/`"Σ"`) under `match_case: false` and negative rows under `match_case: true`. Done when: `TextTables.UnicodeCaseFolding` passes for 1-to-1 and 1-to-2 case expansions (`ß -> SS`).
- [ ] Add whole-word boundary (`whole_word: true`) adversarial rows testing words adjacent to quotes, parentheses, em/en dashes, periods, and CJK characters, plus negative rows verifying `"cat"` does not match `"scatter"` or `"cat_1"`. Done when: `TextTables.WholeWordBoundaries` passes all positive and negative rows.
- [ ] Add CJK, Arabic RTL, and mixed bidirectional (`cjk-rtl-bidi.json`) search rows verifying logical character order matching and accurate visual glyph quad ordering on RTL runs. Done when: `TextTables.CjkAndBidiSearchQuads` passes with visual quads strictly inside the target glyph boxes.
- [ ] Add regex rows (`use_regex: true`) testing capture groups, character classes, quantifiers (`\b[A-Z]{2,4}-\d{3}\b`), invalid regex syntax refusal (`ErrorCode::InvalidArgument` with zero crash), and pathological backtracking patterns (`(a+)+$`) capped by `SearchEngine`'s step limit (`< 100 ms`). Done when: `TextTables.RegexAndBacktrackingGuard` completes in `< 200 ms` under ASan. **Corrected 2026-10-23:** (`D01 T02 §22`: the scope is `ErrorCode`).
- [ ] Prove rule sensitivity in `TextTables.NormalizationRuleSensitivity` by toggling internal normalization flags (`disable_nfkc`, `disable_soft_hyphen_join`, `ascii_only_case_fold`) in test hooks and asserting that the corresponding expectation rows fail. Done when: each of the 3 inverted rules triggers a deterministic expectation failure.
- [ ] Commit: `test: add Unicode normalization, bidi, and regex adversarial rows to TextTables.*`

**Test checkpoint:** `recto-tests --gtest_filter=TextTables.*` passes 100% of expectation and rule-sensitivity tests under ASan with execution time `< 2 s` quoted.

## 5. Conversion round-trip matrix Part 1 (`RoundTrips.Formats`: Images/SVG/XPS, OCR, Tables)

Build the first half of Milestone M4's services-level conversion round-trip harness in `tests/recto-tests/round_trip_tests.cpp` (`RoundTrips.Formats.*`), driven by an applicability manifest (`tests/corpus/convert/round_trip_matrix.json`) over `tests/corpus/convert/`: verify `PDF -> PNG/JPEG/BMP/Multipage TIFF/Animated GIF` and back (`Images -> PDF`), `PDF -> Vector SVG / OpenXPS` and back (`SVG / XPS -> PDF`), `PDF -> Plain Text (.txt)`, `Searchable PDF (OCR)` invisible `3 Tr` text-layer synthesis, `TableExtractor` (`CSV`/`TSV`/Markdown), `ImageExtractor` (`PNG`/`JPEG`), and `PdfOptimizer` + Fast Web View `Linearizer` using format-specific structural and visual (`compute_ssim >= 0.98`) oracles.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `tests/corpus/convert/round_trip_matrix.json` declaring which converter legs apply to each file in `tests/corpus/convert/` and their expected oracle thresholds, and implement the matrix loader in `tests/recto-tests/round_trip_tests.cpp`. Done when: adding a file to `tests/corpus/convert/` without an entry in `round_trip_matrix.json` fails `RoundTrips.EveryConvertCorpusFileMapped`.
2. Implement `RoundTrips.Formats.*` covering raster images (`PNG`, `JPEG`, `BMP`, multipage `TIFF`, `GIF`), vector (`SVG`, `OpenXPS`), `Plain Text`, `Searchable PDF (OCR)`, `TableExtractor`, `ImageExtractor`, `PdfOptimizer`, and `Linearizer`. Done when: all `RoundTrips.Formats.*` tests pass under ASan.

- [ ] Enforce corpus coverage in `RoundTrips.EveryConvertCorpusFileMapped`: enumerate `tests/corpus/convert/*` and fail if any file lacks an entry in `tests/corpus/convert/round_trip_matrix.json`. Done when: an unmapped file in `tests/corpus/convert/` fails the check naming the file. Cheaper substitute: testing only a single hard-coded sample PDF while the rest of `tests/corpus/convert/` goes unexercised.
- [ ] Verify `PDF -> PNG/JPEG/BMP/Multipage TIFF/Animated GIF -> PDF` in `RoundTrips.Formats.RasterImageRoundTrip`: export all pages of `forms-and-annots.pdf` and `vector-shapes.pdf`, verify WIC frame counts and dimensions (including multipage `.tiff` IFD count and `.gif` frame count), rebuild a PDF via `images_to_pdf`, and verify `compute_ssim >= 0.98` per page against the source render. Done when: all 5 raster image formats round-trip with `SSIM >= 0.98`.
- [ ] Verify `PDF -> SVG / OpenXPS -> PDF` in `RoundTrips.Formats.VectorSvgAndXpsRoundTrip`: export pages to SVG 1.1 and OpenXPS `.oxps`, verify well-formed XML and `<path>`/`<text>`/`FixedPage` elements, convert back to PDF via `svg_to_pdf` / `xps_to_pdf`, and verify vector path and text retention plus `compute_ssim >= 0.98`. Done when: vector round-trips preserve selectable text and pass `SSIM >= 0.98`.
- [ ] Verify `PDF -> Plain Text (.txt)` and `Searchable PDF (OCR)` in `RoundTrips.Formats.TextAndSearchableOcr`: run `pdf_to_text` (confirming UTF-8 output matches `PageText`) and run `ocr_to_searchable_pdf` on `scanned-english.pdf`, verifying visual appearance is unchanged (`SSIM >= 0.999` vs original scan) while `SearchEngine` finds target English words on the output PDF. Done when: `RoundTrips.Formats.TextAndSearchableOcr` passes both checks.
- [ ] Verify `TableExtractor` and `ImageExtractor` in `RoundTrips.Formats.TableAndImageExtraction`: extract tables from `table-invoice.pdf` to CSV/TSV/Markdown (verifying exact row/column cell strings and numeric totals) and extract embedded `/Image` XObjects from `embedded-images.pdf` (verifying extracted image count, pixel dimensions, and `SSIM >= 0.99`). Done when: both extractors match expected fixtures in `round_trip_matrix.json`.
- [ ] Verify `PdfOptimizer` and Fast Web View `Linearizer` in `RoundTrips.Formats.OptimizeAndLinearize`: run all 3 compression presets (`High quality`, `Balanced`, `Smallest file`) and linearization (`linearize_pdf`), verifying output byte size reduction on `unoptimized-heavy.pdf`, valid `/Linearized 1` header dictionary and hint stream on linearized output, and `compute_ssim >= 0.97` across all pages. Done when: `RoundTrips.Formats.OptimizeAndLinearize` passes under ASan.
- [ ] Commit: `test: add RoundTrips.Formats.* conversion verification suite over convert corpus`

**Test checkpoint:** `recto-tests --gtest_filter=RoundTrips.EveryConvertCorpusFileMapped:RoundTrips.Formats.*` passes under ASan with per-format SSIM and structural metrics quoted.

## 6. Conversion round-trip matrix Part 2 (`RoundTrips.DocumentsAndSecurity`: Word, Sign)

Complete the services-level round-trip suite (`RoundTrips.DocumentsAndSecurity.*` in `tests/recto-tests/round_trip_tests.cpp`) covering Milestone M4 and M5 document converters, page organization operations, and security/compliance engines: `PDF -> Word (.docx Flowing/Exact/Continuous and .rtf)` round-tripped back through built-in `OpenXML/RTF -> PDF` (`Office/HTML/MD -> PDF`), `PDF -> Excel (.xlsx, .csv, .xml)` round-tripped through built-in `.xlsx/.csv -> PDF`, `PDF -> HTML5/Markdown/EPUB 3/LaTeX` round-tripped through built-in `HTML/Markdown -> PDF`, 100% native legacy binary Office (`.doc`, `.xls`, `.ppt -> PDF`) round-trips, `ComplianceEngine` (`PDF/A-1b..3u`, `PDF/UA-1`, `ZUGFeRD 2.x / Factur-X`), graft `merge`/`split`/`reorder`/`rotate`/`delete`/`extract`/`insert_blank`/`crop`, `Watermark & Stamp`, `Headers/Footers & Bates`, `AES-256` encrypt/decrypt, `PKCS#7`/`PAdES` sign/verify, and physical `Redactor`/`sanitize_document`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `RoundTrips.Documents.*` in `tests/recto-tests/round_trip_tests.cpp` verifying `DocxWriter` (3 modes), `RtfWriter`, `XlsxWriter` (`.xlsx`/`.csv`/`.xml`), `WebDocWriter` (`HTML5`/`Markdown`/`EPUB 3`/`LaTeX`), built-in `OpenXML/RTF/HTML/Markdown -> PDF`, and 100% native legacy binary Office (`.doc`, `.xls`, `.ppt -> PDF`) legs. Done when: every document converter leg runs and validates via structural XML/ZIP/OLE2 inspection and re-import to PDF.
2. Implement `RoundTrips.OrganizeAndSecurity.*` verifying graft `merge`/`split`/`edit`, watermarks, Bates numbering, `AES-256` encryption/decryption, `PKCS#7`/`PAdES` signature signing/verification/tamper detection, `ComplianceEngine` (`PDF/A`, `PDF/UA-1`, `ZUGFeRD/Factur-X`), and physical redaction/sanitization. Done when: a planted page-order bug or unredacted glyph fails its exact leg and the full `RoundTrips.*` suite passes under ASan.

- [ ] Verify `PDF -> Word (.docx Flowing, Exact, Continuous & .rtf) -> PDF` in `RoundTrips.Documents.WordRoundTrip`: convert corpus PDFs via `DocxWriter` (all 3 modes) and `RtfWriter`, validate ZIP structure and well-formed `word/document.xml` (zero unclosed tags, valid `w:tbl` grids), convert the generated `.docx` and `.rtf` back to PDF via the built-in `office_to_pdf` engine (`D04 T04 §1`), and verify non-empty page count and preserved paragraph/table text strings. Done when: all 4 Word legs round-trip without error and retain >= 98% of source words. Cheaper substitute: checking only that `.docx` output byte size is `> 0`.
- [ ] Verify `PDF -> Excel (.xlsx, .csv, .xml) -> PDF` and `PDF -> HTML5/Markdown/EPUB 3/LaTeX -> PDF` in `RoundTrips.Documents.SpreadsheetAndWebRoundTrip`: convert `table-invoice.pdf` and `two-column-article.pdf`, validate `.xlsx` OpenXML sheets and `.epub` OPF/XHTML package structure, convert `.xlsx`, `.csv`, `.html`, and `.md` back to PDF via built-in `D04 T04 §2`/`§3` engines, and verify extracted text parity. Done when: all spreadsheet and web legs pass under ASan.
- [ ] Verify 100% native legacy binary Office (`[MS-CFB]` / `[MS-DOC]` `.doc`, `[MS-XLS]` BIFF8 `.xls`, and `[MS-PPT]` `.ppt -> PDF`) conversion in `RoundTrips.Documents.LegacyBinaryOfficeRoundTrip`: convert `sample-legacy.doc`, `sample-legacy.xls`, and `sample-legacy.ppt` (`D04 T04 §5, §6`) to PDF, open the generated PDFs via `recto::pdf::Document::open`, and verify page counts, extracted `PageText` paragraphs/table cells/slide titles, and `compute_ssim >= 0.98` against golden renders. Done when: all three legacy binary Office formats convert 100% in-process with zero external tools and pass under ASan.
- [ ] Verify graft `merge`, `split`, `reorder`, `rotate`, `delete`, `extract`, `insert_blank`, `crop`, `Watermark`, and `Header/Footer/Bates` in `RoundTrips.OrganizeAndSecurity.PageOperations`: split a multi-page corpus PDF into single-page PDFs, merge them back in original order, verify page count and `expect_rgba_equal` / `SSIM == 1.0` per page, and confirm a planted wrong page order in `split` fails the test immediately. Done when: `RoundTrips.OrganizeAndSecurity.PageOperations` passes and proves sensitivity to page-order mutation.
- [ ] Verify `AES-256` (`V=5, R=6`) encryption/decryption and `PKCS#7`/`PAdES` signing/verification in `RoundTrips.OrganizeAndSecurity.CryptoAndSignatures`: encrypt with user and owner passwords, verify wrong password returns `ErrorCode::WrongPassword`, unlock with `decrypt_pdf`, sign with the test X.509 certificate, verify `SignatureStatus::Valid`, and confirm flipping 1 byte in a signed stream yields `SignatureStatus::InvalidHashMismatch`. Done when: `RoundTrips.OrganizeAndSecurity.CryptoAndSignatures` passes under ASan. **Corrected 2026-10-23:** (`D01 T02 §22`: was `PasswordRequired`; a wrong password is `WrongPassword`).
- [ ] Verify `ComplianceEngine` (`PDF/A-1b`, `PDF/A-2b`, `PDF/A-3b`, `PDF/UA-1`, `ZUGFeRD 2.x / Factur-X`) and physical `Redactor` + `sanitize_document` in `RoundTrips.OrganizeAndSecurity.ComplianceAndRedaction`: convert corpus PDFs to each standard and re-validate (`0` violations), and apply redaction rects confirming zero target characters or raw image pixels survive in the uncompressed output streams. Done when: `RoundTrips.OrganizeAndSecurity.ComplianceAndRedaction` passes all compliance and redaction checks.
- [ ] Commit: `test: complete RoundTrips.DocumentsAndSecurity.* suite over all M4/M5 engines`

**Test checkpoint:** `recto-tests --gtest_filter=RoundTrips.*` passes 100% under ASan with the converter applicability summary across all OpenXML, legacy binary OLE2, RTF, HTML, Markdown, and PDF compliance legs quoted.

## 7. Automated performance targets harness (`tests/perf/` and `recto-cli bench`)

> **Groomed 2026-10-23:** WinUI terms (`HomePage::Loaded`, `CompositionTarget::Rendering`, `DocumentViewCanvas`) replaced with the `recto-ui` equivalents (`D10 T01 §5`, `§18`); the cold-start and scroll gates are numeric and judged by `tests/perf/run_perf.ps1`, and `display-session` stays because the metrics need a presented window.

Build the repeatable benchmark harness (`tests/perf/run_perf.ps1` and `tests/recto-tests/perf_harness_tests.cpp`) that measures all five foundation plan section 8 targets against deterministic benchmark documents generated by `scripts/build-corpus.py --perf` (`perf-100p-text.pdf` and `perf-500p-mixed.pdf`):
1. **Cold start to interactive Home window:** target `< 400 ms` measured by the parent harness from `CreateProcessW` invocation to Home interactive readiness (the first `ui::Window` frame presented after the Home draw, via the DirectComposition commit of `D10 T01 §5`, plus the `ui::Application` message loop idle, reported via the `--perf-cold-start` QPC timestamp).
2. **Open 100-page PDF to first page visible:** target `< 300 ms` (`recto-cli bench` engine cold-open + first-page tile render and app presentation trace).
3. **Scroll frame time with cached tiles (60 fps target / 120 Hz capable):** target `<= 16.6 ms` p95 frame time, `0` UI-thread frames blocked on PDF work, and `0` blank frames (100% of every visible page viewport region in the presented frame is covered by composed sharp tiles or a stretched preview bitmap, not merely a resident or partially intersecting tile) during a scripted 50-page normal-reading-speed continuous scroll trace.
4. **Zoom step (`Ctrl+wheel`):** target `< 16 ms` scaled bitmap preview in the same frame and `< 150 ms` sharp viewport tile swap.
5. **Working set on 500-page mixed PDF:** target `< 600 MB` working set (`GetProcessMemoryInfo` `WorkingSetSize` under the shipping default `512 MB` tile cache cap after scrolling 20 pages).

`D01 T02 §39` (Flate compressor at zlib-ng parity) waits on this harness. -> XREF: D01 T02 §39

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Extend `scripts/build-corpus.py` (`--perf` flag) to deterministically generate `perf-100p-text.pdf` (100 pages of multi-column dense text) and `perf-500p-mixed.pdf` (500 pages of mixed text, vector graphics, and images), and add `--perf-trace` instrumentation hooks to `Recto.exe` and `recto-cli bench`. Done when: both benchmark PDFs generate deterministically and `recto-cli bench` + `Recto.exe --perf-trace` emit structured JSON measurements for all 5 metrics.
2. Author `tests/perf/run_perf.ps1` running `N=9` iterations per metric, computing median, interquartile range (IQR), min, and max, stamping host CPU/RAM/GPU/OS metadata from `D00 T01 §5`, and writing `build/perf-latest.json` plus a Markdown table; add a CI smoke test (`PerfSmoke.*` in `recto-tests`) with relaxed CI runner bounds and failure-injection tests for delayed startup and blank scroll frames. Done when: `pwsh -NoProfile -File tests/perf/run_perf.ps1` produces all 5 numbers with median and IQR in one command and `PerfSmoke.*` passes.

- [ ] Extend `scripts/build-corpus.py` to generate `perf-100p-text.pdf` (100 pages, ~3,000 characters/page) and `perf-500p-mixed.pdf` (500 pages with text, vector paths, and raster images) deterministically with fixed seeds and register their SHA-256 digests. Done when: two runs of `python scripts/build-corpus.py --perf` produce byte-identical 100-page and 500-page PDFs. Cheaper substitute: benchmarking on tiny 1-page unit-test PDFs that cannot reveal `O(N)` page-tree or LRU memory regressions.
- [ ] Measure Metric 2 (`First page visible < 300 ms` on `perf-100p-text.pdf`) and Metric 5 (`WorkingSetSize < 600 MB` on `perf-500p-mixed.pdf` with the shipping default `512 MB` tile cache cap after scrolling 20 pages) via `recto-cli bench --iterations 9 --json` and `Recto.exe --perf-trace`. Done when: JSON output reports `first_page_visible_ms`, `tile_cache_cap_mb: 512`, and `working_set_mb` with median and IQR across 9 runs.
- [ ] Implement `--perf-cold-start` in `tests/perf/run_perf.ps1` and `Recto.exe` (the `ui::Application` entry point in `src/app/`): the parent harness records `QueryPerformanceCounter` (`t0`) immediately before `CreateProcessW` and `Recto.exe` records `QueryPerformanceCounter` (`t1`) only after `HomePage` is loaded, its first DirectComposition commit after the first Home draw has presented, and the `ui::Window` message loop is interactive, emitting `(t1 - t0)` in milliseconds and verifying via `PerfSmoke.ColdStartDetectsDelayedHomeReady` that an injected `450 ms` `HomePage` initialization delay fails the `< 400 ms` gate. Done when: `tests/perf/run_perf.ps1` spawns `Recto.exe --perf-cold-start` 9 times, reports full `CreateProcessW`-to-interactive-Home latency, and `PerfSmoke.ColdStartDetectsDelayedHomeReady` passes.
- [ ] Implement `--perf-scroll-zoom <pdf>` in `Recto.exe` driving a scripted 50-page normal-reading-speed scroll trace and a `100% -> 200%` zoom step on the `DocumentView` page canvas (`ui::SwapChainHost`, `D10 T01 §18`), recording UI-thread frame durations (`Metric 3: Scroll frame time <= 16.6 ms p95`, `0` frames blocked on engine work, and `blank_frame_count == 0` verified by checking that the union of drawn/composed sharp-tile and stretched-preview quads in each presented frame covers 100% of every visible page region) and time from zoom command to scaled preview and sharp viewport tile completion (`Metric 4: < 16 ms preview, < 150 ms sharp tiles`), plus `PerfSmoke.ScrollBlankFrameDetector` proving that both (a) a missing visible tile while another tile on the same page remains present and (b) a resident tile texture omitted from the presented composition pass increment `blank_frame_count` and fail the gate. Done when: `--perf-scroll-zoom perf-100p-text.pdf` emits JSON with p50/p95/max UI-thread frame times, `blank_frame_count: 0`, preview latency, and sharp-tile completion latency, and `PerfSmoke.ScrollBlankFrameDetector` passes both negative sub-cases.
- [ ] Author `tests/perf/run_perf.ps1` aggregating all 5 metrics, stamping host hardware (`Get-CimInstance Win32_Processor`, RAM, GPU, OS build, git SHA, build configuration, `512 MB` default tile cache cap), comparing against foundation plan section 8's targets, and outputting both JSON and a Markdown report. Done when: `pwsh -NoProfile -File tests/perf/run_perf.ps1` completes end-to-end and exits `0` when targets are met.
- [ ] Add `PerfSmoke.*` in `tests/recto-tests/perf_harness_tests.cpp` verifying the harness measurement code paths on CI with relaxed shared-runner ceilings (`first_page < 500 ms`, `500p_rss < 600 MB`, `blank_frame_count == 0`) plus the negative tests `PerfSmoke.ColdStartDetectsDelayedHomeReady` and `PerfSmoke.ScrollBlankFrameDetector` (covering both partial-page tile omission and resident-but-uncomposed textures) so regressions that blow up complexity, memory, or blank frames fail on every PR. Done when: `recto-tests --gtest_filter=PerfSmoke.*` passes in CI.
- [ ] Time DCTDecode image decoding on the `D06 T05 §33` fixtures scaled to page-size images (baseline, progressive, 4:2:0, Adobe CMYK and YCCK) in `recto-cli bench`, recording the median and 95th percentile and peak memory, and time the same images to first display in Acrobat Pro on the same machine (filed by the `D06 T05 §33` completion review, `docs/reviews/06-quality/D06-T05-s33.md`). Done when: `docs/perf/` holds both rows, and any image type where Recto is slower is filed through `add-todo` with the measured gap as its target. -> XREF: D06 T05 §33
- [ ] Time text extraction on the same PDFs in Recto (`D01 T02 §28` mapping through `D03 T01 §1`) and in Acrobat Pro, recording the median and 95th percentile and peak memory, and compare the extracted strings for exact agreement (filed by the `D01 T02 §28` completion review). Done when: `docs/perf/` holds the rows, and any document where Recto is slower or differs is filed through `add-todo`. -> XREF: D01 T02 §28
- [ ] Time loading and drawing embedded Type 1, name-keyed CFF and large CID-keyed CFF fonts (`D01 T02 §29`) in Recto and in Acrobat Pro: cold load, per-glyph outline p50 and p95, and peak memory, with the hostile fixtures at their bounds (filed by the `D01 T02 §29` completion review). Done when: `docs/perf/` holds the rows, and any font where Recto is slower is filed through `add-todo` with its numbers. -> XREF: D01 T02 §29
- [ ] Time CJK and Office PDFs whose text is in Type 0 fonts (`D01 T02 §30`) in Recto and in Acrobat Pro on the same hardware: time to the first correctly drawn page cold and warm, median and 95th percentile, and peak memory, with the damaged-file fixtures timed to their refusal (filed by the `D01 T02 §30` completion review). Done when: `docs/perf/` holds the rows, and any document where Recto is slower or draws differently is filed through `add-todo` with its numbers. -> XREF: D01 T02 §30
- [ ] Commit: `test: add automated performance harness for foundation plan section 8 targets`

**Test checkpoint:** `pwsh -NoProfile -File tests/perf/run_perf.ps1` output and `recto-tests --gtest_filter=PerfSmoke.*` output quoted with median and IQR across 9 iterations.

## 8. Primary reference-machine M2/M6 performance baseline record (`docs/perf/`)

> **Groomed 2026-10-23:** the baseline file name carries the date of the run (`<YYYY-MM-DD>-reference-baseline.md`, not the fixed `2026-10-14`, a day that has passed), and `PerfSmoke.BaselineRecordExistsAndIsComplete` checks the newest file matching `docs/perf/*-reference-baseline.md`.

Execute `tests/perf/run_perf.ps1` on the `D00 T01 §5` primary reference host machine (`x64` Release build with the shipping default `512 MB` tile cache cap), commit the dated baseline record under `docs/perf/<YYYY-MM-DD>-reference-baseline.md` and `docs/perf/README.md`, and verify every one of foundation plan section 8's five targets passes (`PASS`) with margin.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Run `pwsh -NoProfile -File tests/perf/run_perf.ps1 -Output docs/perf/<YYYY-MM-DD>-reference-baseline.md` on the primary reference host machine recorded in `D00 T01 §5` (`docs/plans/2026-10-13-recto-foundation.md` section 8). Done when: `docs/perf/<YYYY-MM-DD>-reference-baseline.md` contains the machine spec, git commit SHA, `512 MB` default tile cache configuration, and 9-run median/IQR/min/max numbers for all 5 targets.
2. Author `docs/perf/README.md` documenting how baselines are recorded and compared across milestones (M2 viewer baseline and M6 candidate-bound release verification in `D07 T02 §7`), and optimize any missed target before stamping so all 5 targets pass (`PASS`). Done when: every target row in `docs/perf/<YYYY-MM-DD>-reference-baseline.md` carries a `PASS` status (`0` blank frames, `< 400 ms` cold launch, `< 300 ms` 100p open, `<= 16.6 ms` p95 scroll, `< 16 ms` / `< 150 ms` zoom, `< 600 MB` working set at `512 MB` tile cache cap) and `python scripts/todo-graph.py validate` exits `0`.

- [ ] Verify before running that the host machine matches the primary reference machine recorded in `docs/plans/2026-10-13-recto-foundation.md` section 8 (`D00 T01 §5`: CPU model, core count, RAM, OS build) and that the build is `Release | x64` with ASan disabled and the default `512 MB` tile cache cap active. Done when: the hardware and configuration header in `docs/perf/<YYYY-MM-DD>-reference-baseline.md` matches section 8's reference machine record. Cheaper substitute: recording numbers from a Debug or ASan build, a reduced 32 MB cache, or an undocumented machine without stating the configuration.
- [ ] Record the 9-iteration median, IQR, min, and max for all 5 targets (`Cold launch to interactive Home < 400 ms`, `Open 100-page PDF to first page visible < 300 ms`, `Continuous scroll frame time <= 16.6 ms p95 with 0 blank frames`, `Zoom preview < 16 ms / sharp tiles < 150 ms`, `500-page memory < 600 MB with 512 MB tile cache cap`) in `docs/perf/<YYYY-MM-DD>-reference-baseline.md`. Done when: all 5 rows are populated with measured numbers and spread (< 10% IQR/median noise).
- [ ] Verify each measured median meets foundation plan section 8's target threshold with `PASS` and positive headroom percentage (filing and completing any needed optimization before stamping `§8`). Done when: all 5 targets record `PASS` with zero unresolved target failures.
- [ ] Author `docs/perf/README.md` explaining the invocation command (`pwsh -NoProfile -File tests/perf/run_perf.ps1`), the repeatability rule (median of 9 runs after 1 warm-up discovery check, cold-open cache flush instructions), and the M2 baseline vs M6 release-candidate verification gate (`D07 T02 §7`). Done when: `docs/perf/README.md` is committed and links to `2026-10-14-reference-baseline.md`.
- [ ] Add a schema/completeness test `PerfSmoke.BaselineRecordExistsAndIsComplete` in `tests/recto-tests/perf_harness_tests.cpp` verifying that `docs/perf/<YYYY-MM-DD>-reference-baseline.md` exists and contains all 5 target rows with numeric medians and `PASS` verdicts. Done when: `recto-tests --gtest_filter=PerfSmoke.BaselineRecordExistsAndIsComplete` passes.
- [ ] Commit: `docs: record primary reference-machine performance baseline in docs/perf/`

**Test checkpoint:** the 5-row summary table from `docs/perf/<YYYY-MM-DD>-reference-baseline.md` (with all 5 rows `PASS`) is quoted verbatim alongside `recto-tests --gtest_filter=PerfSmoke.*` passing.

## 9. Damaged-fax corpus compared with Acrobat Pro, Foxit, PDFelement and UPDF

The completion review of `D06 T05 §34` (`docs/reviews/06-quality/D06-T05-s34.md`, excellence 4) found no comparative evidence behind Recto's damaged-fax recovery: Table 11 says what must happen, but nothing shows what users of the competitors get for the same file. Product ambition (`AGENTS.md`) asks for measured claims; this section measures them.

**Phase:** 6
**Needs:** windows-host, attended, third-party
**Surface:** no

**Build order:**

1. Build a damaged-fax corpus and record each reader's result. Done when: the table in `docs/benchmarks/damaged-fax.md` has a row per file and reader.

- [ ] Add CCITT Group 3 (`K = 0` and `K > 0`) and Group 4 fixtures with damaged rows, missing EOLs and exhausted allowances to `tests/corpus/fax/` through `scripts/build-corpus.py`, with provenance rows. Done when: `python scripts/build-corpus.py --check` passes with them.
- [ ] Third party: open each fixture in recorded versions of Acrobat Pro, Foxit PDF Editor, PDFelement and UPDF and record the rows recovered, the content lost, whether it refused and what it said, in `docs/benchmarks/damaged-fax.md`, beside Recto's result. Done when: every file has a row per reader with the version and date.
- [ ] Add `Corpus.DamagedFaxMatchesTable11` asserting Recto's recovered rows for every fixture. Done when: it passes and fails with recovery disabled.
- [ ] Commit: `corpus: damaged-fax fixtures compared with four readers`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Corpus.DamagedFaxMatchesTable11` passes and `docs/benchmarks/damaged-fax.md` holds the comparison table.

## 10. Save time and file size compared with Acrobat Pro, Foxit, PDFelement and UPDF

The completion review of `D01 T02 §5` added the large-base revision gate (`docs/reviews/01-engine/D01-T02-s5.md`, excellence 3). -> XREF: D01 T02 §5

The completion review of `D01 T02 §37` (`docs/reviews/01-engine/D01-T02-s37.md`, excellence 3) found that Recto's compressor is measured only against zlib-ng on stream samples: nothing shows what a user saving the same document gets from the competitors, in time or in bytes. Product ambition (`AGENTS.md`) asks for measured claims; this section measures them end to end, through the save paths. -> XREF: D01 T02 §37

**Phase:** 6
**Needs:** windows-host, attended, third-party
**Surface:** no

**Build order:**

1. Measure Recto's saves on the `§7` benchmark documents and record each competitor's beside them. Done when: the table in `docs/benchmarks/save.md` has a row per document, operation and product.

- [ ] Add `recto-cli bench save` timing an incremental save (`D01 T02 §5`) after one annotation edit and a full rewrite (`D04 T02 §2`) of `perf-100p-text.pdf`, `perf-500p-mixed.pdf` and three real-world corpus files, reporting the median and slowest of seven runs, the output size, and a strict read-back (the saved file reopens and renders the same pages). Done when: it prints one JSON row per document and operation, and a save that fails to read back exits non-zero.
- [ ] Third party: perform the same edit and save, and a full "Save As" with lossless settings (no image recompression, no font subsetting change), in recorded versions of Acrobat Pro, Foxit PDF Editor, PDFelement and UPDF, and record time (median of five, timed from the command to the file closing), output size and whether the file reopens, in `docs/benchmarks/save.md` beside Recto's rows. Done when: every document has a row per product with the version and date.
- [ ] Time an incremental save of one annotation edit on 10 MB, 100 MB and 1 GB bases, unencrypted and AES-256 (built by `scripts/build-corpus.py --perf`), recording the revision time and peak working set beside the base size; the revision time must not grow with the base (filed by the `D01 T02 §5` completion review, excellence 3). Done when: `recto-cli bench save` prints the rows and fails when the 1 GB revision takes more than twice the 10 MB one.
- [ ] Time edit, undo, redo and save of one annotation on the same bases in Recto (through `D01 T02 §13`'s chain) and in each competitor, recording the 50th and 95th percentile and peak memory (filed by the `D01 T02 §13` completion review). Done when: every base has a row per product and operation.
- [ ] Where Recto is slower or larger than the best competitor on any row, file the owning section through `add-todo` with the measured gap as its target. Done when: every such row names its owner in the table.
- [ ] Commit: `bench: save time and size compared with four editors`

**Test checkpoint:** `recto-cli bench save --json` passes on the five documents with every read-back true, and `docs/benchmarks/save.md` holds the comparison table.

## Verification

- Every milestone done-when gate that cites a verification suite (M1 golden render determinism, M2 performance targets, M3 text and search expectation tables, M4 and M5 conversion and security round-trips) quotes the corresponding suite in this file.
- Every golden refresh in git history is an isolated commit with a stated rendering cause, and `python scripts/refresh-goldens.py --check` exits `0`.
- `python scripts/todo-graph.py validate` passes.
