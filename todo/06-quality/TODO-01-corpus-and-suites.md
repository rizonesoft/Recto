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

> **Current state:** Measured 2026-10-14. No suites exist yet. `D00 T03` provides the test runner helpers (`test_support.h/.cpp`), corpus folders (`render/`, `text/`, `convert/`, `broken/`, `seeds/`), `PROVENANCE.md` enforcement, and the MSVC AddressSanitizer build configuration. Individual feature sections prove their own slices; this file turns those slices into maintained regression suites with explicit refresh and baseline rules so later optimizations or `recto-pdf` changes are judged against committed evidence, never memory.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.11, 7 (each milestone's done-when verification gate), and 8 (the five performance targets and the reference-machine rule).
- `tests/corpus/README.md` and `tests/corpus/PROVENANCE.md` built by `D00 T03 §5` (the golden refresh rules extend, never bypass, corpus provenance).
- -> XREF: D00 T03 §2 -- the RGBA pixel comparison (`compare_rgba`, `expect_rgba_equal`, `compute_ssim`) and SHA-256 helpers (`sha256_hex`) consumed by `Goldens.*` (`§1`, `§2`) and `RoundTrips.*` (`§5`, `§6`).
- -> XREF: D00 T03 §5 -- the corpus folders and `PROVENANCE.md` integrity suite that `§1` through `§7` consume and extend.
- -> XREF: D01 T03 §2 -- `recto-cli render` shares the deterministic rendering pipeline exercised by `Goldens.*` in `§1` and `§2`.
- -> XREF: D01 T03 §3 -- `recto-cli bench` provides the engine cold-open, first-page render, and 500-page RSS telemetry consumed by `§7` and `§8`.

## Outcome

- Every `recto-pdf` rendering change produces either silence (meaning pixel-identical output across `x64` and `ARM64`) or a named, reviewable golden diff with side-by-side actual and difference PNGs written to the test output directory.
- Text extraction, search hit counts, hit contexts, OCR pre/post text layers, and Unicode normalization rules (`NFKC` ligatures, soft-hyphen elision, non-ASCII case folding, whole-word boundaries, and ECMAScript regex) are data-driven tables under `tests/corpus/text/expectations/` verified by `TextTables.*`.
- Every M4 and M5 converter and document engine (`PDF <-> Images/TIFF/GIF/SVG/XPS`, Plain Text, Searchable OCR PDF, `TableExtractor`, `ImageExtractor`, `PdfOptimizer`, `Linearizer`, `DocxWriter`, `RtfWriter`, `XlsxWriter`, `WebDocWriter`, `ComplianceEngine` PDF/A & PDF/UA & ZUGFeRD/Factur-X, built-in `Office/HTML/Markdown -> PDF`, optional Office COM/LibreOffice adapters, graft `merge`/`split`/`edit`, watermarks/Bates, AES-256 encryption, PKCS#7/PAdES signatures, and physical redaction/sanitization) runs in `RoundTrips.*` against `tests/corpus/convert/` with format-specific structural and visual oracles.
- Foundation plan section 8's five performance targets are measured by an automated scriptable harness (`tests/perf/`) and recorded with median and IQR spread on the `D00 T01 §5` primary reference machine under `docs/perf/`.

**Adjacency:** all=not-applicable (automated test suites, data expectation tables, and benchmark harnesses over engine and services code; no user-facing product UI surfaces)

**Adjacency rationale:** The surfaces of these suites are `recto-tests` console/XML output, diff PNG artifacts in the ignored build output folder, and committed expectation/baseline files under `tests/corpus/` and `docs/perf/`. The golden refresh discipline (`§2`) and baseline recording discipline (`§8`) are engineering process gates documented in `tests/corpus/README.md` and `docs/perf/README.md`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Pixel-exact golden render comparison harness (`Goldens.*` in `recto-tests`) | D01 T03 §2, D00 T03 §5 | [ ] |
| 2 | §2 | Disciplined golden refresh tool (`--refresh-goldens`) and `x64` vs `ARM64` pixel gate | §1 | [ ] |
| 3 | §3 | Per-file text extraction and find expectation tables (`expectations/` & `TextTables.*`) | D03 T01 §5, D03 T01 §8 | [ ] |
| 4 | §4 | Normalization and regex adversarial verification rows (ligatures, soft hyphens, bidi) | §3 | [ ] |
| 5 | §5 | Conversion round-trip matrix Part 1 (`RoundTrips.Formats`: Images/SVG/XPS, OCR, Tables) | D04 T02 §11 | [ ] |
| 6 | §6 | Conversion round-trip matrix Part 2 (`RoundTrips.DocumentsAndSecurity`: Word, Sign) | §5, D04 T03 §4, D04 T04 §6, D04 T05 §8, D05 T01 §10 | [ ] |
| 7 | §7 | Automated performance targets harness (`tests/perf/` and `recto-cli bench`) | D01 T03 §3, D02 T02 §6, D00 T01 §5 | [ ] |
| 8 | §8 | Primary reference-machine M2/M6 performance baseline record (`docs/perf/`) | §7 | [ ] |

## 1. Pixel-exact golden render comparison harness (`Goldens.*` in `recto-tests`)

Turn deterministic rendering into a permanent per-commit contract: every file in `tests/corpus/render/` renders across a fixed matrix of DPIs (`72`, `144`, `300`) and rotations (`0`, `90`, `180`, `270` degrees) through the exact same `DocumentActor` + `RenderPool` tile-compositing pipeline used by `recto-cli render`, and compares pixel-for-pixel (`max_channel_delta == 0`, `mismatched_pixel_count == 0`) against committed golden PNGs and SHA-256 manifests under `tests/corpus/render/goldens/`. On any mismatch, the suite writes `<stem>-actual.png` and a `4x`-amplified `<stem>-diff.png` into `build/test-failures/goldens/` and fails naming the corpus file, page index, DPI, rotation, first mismatched `(x, y)` coordinate, and channel delta.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `tests/recto-tests/golden_render_tests.cpp` (`Goldens.*`) enumerating every `.pdf` in `tests/corpus/render/`, rendering each page across the DPI/rotation matrix via `Runtime` (`1` worker and `4` workers), and comparing against `tests/corpus/render/goldens/<stem>_p<page>_dpi<dpi>_rot<rot>.png` using `recto::test::compare_rgba`. Done when: `recto-tests --gtest_filter=Goldens.*` runs every render-corpus file and fails if any `.pdf` in `tests/corpus/render/` lacks its golden set.
2. Add diagnostic failure artifact emission (`build/test-failures/goldens/<stem>_p<page>_dpi<dpi>_rot<rot>_actual.png` and `_diff.png`) and commit the initial baseline goldens under `tests/corpus/render/goldens/` with their SHA-256 entries registered in `tests/corpus/PROVENANCE.md`. Done when: a planted 1-LSB pixel modification in a golden PNG fails `Goldens.*` with exact coordinates and writes both `_actual.png` and `_diff.png`.

- [ ] Enumerate `tests/corpus/render/*.pdf` dynamically in `Goldens.AllRenderCorpusFilesHaveGoldens` and fail immediately if a newly added corpus PDF has no matching golden PNG entries in `tests/corpus/render/goldens/`. Done when: dropping an untracked `new.pdf` into `tests/corpus/render/` fails `Goldens.AllRenderCorpusFilesHaveGoldens` naming `new.pdf`. Cheaper substitute: hard-coding a static list of filenames in C++ so new corpus PDFs silently skip golden comparison.
- [ ] Render each corpus page at `72 DPI` (`0`, `90`, `180`, `270` deg), `144 DPI` (`0` deg), and `300 DPI` (`0` deg) through `Runtime` + `DocumentActor` + `RenderPool` tile composition (`256 x 256` tiles). Done when: `Goldens.RenderMatrixMatchesCommittedGoldens` exercises all 6 render configurations per page on every file in `tests/corpus/render/`.
- [ ] Enforce zero pixel tolerance (`max_channel_delta == 0` and `mismatched_pixel_count == 0` via `recto::test::expect_rgba_equal`) on every golden comparison. Done when: a 1-LSB change (`R=200 -> R=201`) in a single pixel of `vector-shapes_p1_dpi144_rot0.png` fails the test with the exact `(x, y)` coordinate and channel values.
- [ ] Emit `<name>_actual.png`, `<name>_expected.png`, and a `4x`-amplified red-highlighted `<name>_diff.png` into `build/test-failures/goldens/` whenever `compare_rgba` reports `mismatched_pixel_count > 0`. Done when: the planted 1-pixel failure test verifies that all three diagnostic PNGs exist on disk and are readable via WIC.
- [ ] Verify that `1`-worker and `4`-worker `RenderPool` runs produce byte-identical RGBA buffers matching the committed golden SHA-256 for every page in `tests/corpus/render/`. Done when: `Goldens.SingleAndMultiWorkerMatchGoldens` passes under ASan.
- [ ] Register all golden PNGs and `tests/corpus/render/goldens/manifest.json` in `tests/corpus/PROVENANCE.md` so `Corpus.Provenance` (`D00 T03 §5`) verifies their SHA-256 checksums. Done when: `recto-tests --gtest_filter=Corpus.Provenance` passes with the golden files included.
- [ ] Commit: `test: add pixel-exact Goldens.* render comparison suite and baseline goldens`

**Test checkpoint:** `recto-tests --gtest_filter=Goldens.*:Corpus.Provenance` passes on `x64` Debug, Release, and ASan, and the planted 1-pixel failure output naming page, DPI, rotation, `(x, y)` coordinate, and `_diff.png` path is quoted.

## 2. Disciplined golden refresh tool (`--refresh-goldens`) and `x64` vs `ARM64` pixel gate

Provide a single deterministic command (`python scripts/refresh-goldens.py` driving `recto-tests --refresh-goldens` or `recto-cli render`) that regenerates `tests/corpus/render/goldens/` and updates `manifest.json` and `PROVENANCE.md`, document the golden refresh governance rule in `tests/corpus/README.md` (a golden refresh must be an isolated commit whose message states the `recto-pdf` rendering change that caused the pixel delta), and wire `Goldens.*` into both `x64` and `ARM64` builds so any cross-architecture Direct2D/DirectWrite rasterization divergence fails immediately.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `--refresh-goldens` mode in `tests/recto-tests/golden_render_tests.cpp` and wrapper script `scripts/refresh-goldens.py` that re-renders all `tests/corpus/render/*.pdf` goldens, updates `tests/corpus/render/goldens/manifest.json` and `tests/corpus/PROVENANCE.md`, and prints a per-file summary (`unchanged`, `updated`, `added`, `removed`). Done when: running `python scripts/refresh-goldens.py` on an unchanged build prints `0 updated` and leaves `git diff` completely empty.
2. Document the golden refresh policy in `tests/corpus/README.md` and add a cross-architecture manifest verification check in `Goldens.CrossArchManifestParity`. Done when: `tests/corpus/README.md` defines the refresh commit rules and `Goldens.*` passes identically on `x64` and `ARM64` builds.

- [ ] Implement `RECTO_REFRESH_GOLDENS=1` / `--refresh-goldens` support in `golden_render_tests.cpp` that writes deterministic WIC PNGs (stripped of timestamps or host metadata chunks) to `tests/corpus/render/goldens/`. Done when: two consecutive `--refresh-goldens` runs produce byte-identical PNG files (`git status --porcelain` is empty). Cheaper substitute: PNG encoders that write creation timestamps (`tIME` chunks) causing every refresh to dirty all golden files in git.
- [ ] Write `scripts/refresh-goldens.py` to run `--refresh-goldens`, recompute SHA-256 hashes in `tests/corpus/render/goldens/manifest.json` and `tests/corpus/PROVENANCE.md`, and report exact pixel-delta counts for each changed golden. Done when: `python scripts/refresh-goldens.py --check` exits `0` when goldens are current and exits `2` listing out-of-date files when a golden is modified.
- [ ] Verify that `Direct2D` software WARP rasterization (`D3D10_DRIVER_TYPE_WARP` configured in `D01 T02 §6`) produces identical golden pixels regardless of host GPU vendor or `x64` vs `ARM64` compilation target. Done when: `Goldens.CrossArchManifestParity` verifies identical SHA-256 digests against `manifest.json`.
- [ ] Document the golden refresh discipline in `tests/corpus/README.md`: (1) never refresh goldens to silence an unexplained failure, (2) inspect `_diff.png` for every changed page, (3) commit golden refreshes in a dedicated commit citing the owning `recto-pdf` section and cause. Done when: `tests/corpus/README.md` contains the `## Golden Refresh Discipline` section with exact command examples.
- [ ] Add unit test `Goldens.RefreshRoundTripInTempDir` that copies a subset of `render/` into a `TempDir`, corrupts one golden PNG, runs the refresh routine against the `TempDir`, and verifies the restored PNG matches the canonical SHA-256. Done when: `Goldens.RefreshRoundTripInTempDir` passes in `< 200 ms`.
- [ ] Wire `Goldens.*` into `.github/workflows/ci.yml` so every PR runs pixel-exact golden verification in both Release and ASan jobs. Done when: CI workflow YAML includes `Goldens.*` in the test step without exclusion filters.
- [ ] Commit: `test: add deterministic --refresh-goldens tool and cross-arch golden parity gate`

**Test checkpoint:** `python scripts/refresh-goldens.py` on a clean tree produces an empty `git diff`, `python scripts/refresh-goldens.py --check` exits `0`, and `recto-tests --gtest_filter=Goldens.*` passes.

## 3. Per-file text extraction and find expectation tables (`expectations/` & `TextTables.*`)

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
- [ ] Add regex rows (`use_regex: true`) testing capture groups, character classes, quantifiers (`\b[A-Z]{2,4}-\d{3}\b`), invalid regex syntax refusal (`PdfErrorCode::InvalidArgument` with zero crash), and pathological backtracking patterns (`(a+)+$`) capped by `SearchEngine`'s step limit (`< 100 ms`). Done when: `TextTables.RegexAndBacktrackingGuard` completes in `< 200 ms` under ASan.
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

Complete the services-level round-trip suite (`RoundTrips.DocumentsAndSecurity.*` in `tests/recto-tests/round_trip_tests.cpp`) covering Milestone M4 and M5 document converters, page organization operations, and security/compliance engines: `PDF -> Word (.docx Flowing/Exact/Continuous and .rtf)` round-tripped back through built-in `OpenXML/RTF -> PDF` (`Office/HTML/MD -> PDF`), `PDF -> Excel (.xlsx, .csv, .xml)` round-tripped through built-in `.xlsx/.csv -> PDF`, `PDF -> HTML5/Markdown/EPUB 3/LaTeX` round-tripped through built-in `HTML/Markdown -> PDF`, `ComplianceEngine` (`PDF/A-1b..3u`, `PDF/UA-1`, `ZUGFeRD 2.x / Factur-X`), optional Microsoft Office COM and LibreOffice adapters (with explicit named skip lines when absent), graft `merge`/`split`/`reorder`/`rotate`/`delete`/`extract`/`insert_blank`/`crop`, `Watermark & Stamp`, `Headers/Footers & Bates`, `AES-256` encrypt/decrypt, `PKCS#7`/`PAdES` sign/verify, and physical `Redactor`/`sanitize_document`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `RoundTrips.Documents.*` in `tests/recto-tests/round_trip_tests.cpp` verifying `DocxWriter` (3 modes), `RtfWriter`, `XlsxWriter` (`.xlsx`/`.csv`/`.xml`), `WebDocWriter` (`HTML5`/`Markdown`/`EPUB 3`/`LaTeX`), built-in `Office/HTML/Markdown -> PDF`, and optional Office COM / LibreOffice legs with explicit skip reporting. Done when: every document converter leg runs and validates via structural XML/ZIP inspection and re-import to PDF.
2. Implement `RoundTrips.OrganizeAndSecurity.*` verifying graft `merge`/`split`/`edit`, watermarks, Bates numbering, `AES-256` encryption/decryption, `PKCS#7`/`PAdES` signature signing/verification/tamper detection, `ComplianceEngine` (`PDF/A`, `PDF/UA-1`, `ZUGFeRD/Factur-X`), and physical redaction/sanitization. Done when: a planted page-order bug or unredacted glyph fails its exact leg and the full `RoundTrips.*` suite passes under ASan.

- [ ] Verify `PDF -> Word (.docx Flowing, Exact, Continuous & .rtf) -> PDF` in `RoundTrips.Documents.WordRoundTrip`: convert corpus PDFs via `DocxWriter` (all 3 modes) and `RtfWriter`, validate ZIP structure and well-formed `word/document.xml` (zero unclosed tags, valid `w:tbl` grids), convert the generated `.docx` and `.rtf` back to PDF via the built-in `office_to_pdf` engine (`D04 T04 §1`), and verify non-empty page count and preserved paragraph/table text strings. Done when: all 4 Word legs round-trip without error and retain >= 98% of source words. Cheaper substitute: checking only that `.docx` output byte size is `> 0`.
- [ ] Verify `PDF -> Excel (.xlsx, .csv, .xml) -> PDF` and `PDF -> HTML5/Markdown/EPUB 3/LaTeX -> PDF` in `RoundTrips.Documents.SpreadsheetAndWebRoundTrip`: convert `table-invoice.pdf` and `two-column-article.pdf`, validate `.xlsx` OpenXML sheets and `.epub` OPF/XHTML package structure, convert `.xlsx`, `.csv`, `.html`, and `.md` back to PDF via built-in `D04 T04 §2`/`§3` engines, and verify extracted text parity. Done when: all spreadsheet and web legs pass under ASan.
- [ ] Wire optional external Office COM and LibreOffice converter legs in `RoundTrips.Documents.ExternalOfficeAdapters`: query `detect_office_converters()` (`D04 T04 §4`); when installed, run the `.docx`/`.xlsx`/`.pptx -> PDF` corpus through the external adapter and compare against the built-in engine; when absent, print an explicit structured line (`[ SKIPPED:external-converter ] Microsoft Office COM not installed on host; built-in OpenXML engine ran and passed`). Done when: CI logs explicitly print which external legs ran and which skipped while always executing the built-in OpenXML/RTF/HTML/Markdown legs.
- [ ] Verify graft `merge`, `split`, `reorder`, `rotate`, `delete`, `extract`, `insert_blank`, `crop`, `Watermark`, and `Header/Footer/Bates` in `RoundTrips.OrganizeAndSecurity.PageOperations`: split a multi-page corpus PDF into single-page PDFs, merge them back in original order, verify page count and `expect_rgba_equal` / `SSIM == 1.0` per page, and confirm a planted wrong page order in `split` fails the test immediately. Done when: `RoundTrips.OrganizeAndSecurity.PageOperations` passes and proves sensitivity to page-order mutation.
- [ ] Verify `AES-256` (`V=5, R=6`) encryption/decryption and `PKCS#7`/`PAdES` signing/verification in `RoundTrips.OrganizeAndSecurity.CryptoAndSignatures`: encrypt with user and owner passwords, verify wrong password returns `PdfErrorCode::PasswordRequired`, unlock with `decrypt_pdf`, sign with the test X.509 certificate, verify `SignatureStatus::Valid`, and confirm flipping 1 byte in a signed stream yields `SignatureStatus::InvalidHashMismatch`. Done when: `RoundTrips.OrganizeAndSecurity.CryptoAndSignatures` passes under ASan.
- [ ] Verify `ComplianceEngine` (`PDF/A-1b`, `PDF/A-2b`, `PDF/A-3b`, `PDF/UA-1`, `ZUGFeRD 2.x / Factur-X`) and physical `Redactor` + `sanitize_document` in `RoundTrips.OrganizeAndSecurity.ComplianceAndRedaction`: convert corpus PDFs to each standard and re-validate (`0` violations), and apply redaction rects confirming zero target characters or raw image pixels survive in the uncompressed output streams. Done when: `RoundTrips.OrganizeAndSecurity.ComplianceAndRedaction` passes all compliance and redaction checks.
- [ ] Commit: `test: complete RoundTrips.DocumentsAndSecurity.* suite over all M4/M5 engines`

**Test checkpoint:** `recto-tests --gtest_filter=RoundTrips.*` passes 100% under ASan with the converter applicability summary and any external-converter skip lines quoted.

## 7. Automated performance targets harness (`tests/perf/` and `recto-cli bench`)

Build the repeatable benchmark harness (`tests/perf/run_perf.ps1` and `tests/recto-tests/perf_harness_tests.cpp`) that measures all five foundation plan section 8 targets against deterministic benchmark documents generated by `scripts/build-corpus.py --perf` (`perf-100p-text.pdf` and `perf-500p-mixed.pdf`):
1. **First page visible (100-page text PDF, cold open):** target `< 120 ms` (`recto-cli bench` engine cold-open + first-page tile render and app presentation trace).
2. **Scroll frame time (60 fps target / 120 Hz capable):** target `< 8 ms` on UI thread (`0` dropped frames from PDF work during a scripted 50-page continuous scroll trace).
3. **Zoom preview to sharp tiles:** target `0 ms` preview (existing bitmap scaled immediately) and `< 100 ms` sharp viewport tile swap.
4. **Memory (500-page mixed PDF open, scrolled 20 pages):** target `< 250 MB` working set (`GetProcessMemoryInfo` `WorkingSetSize`).
5. **Cold start to empty Home window:** target `< 350 ms` from `CreateProcessW` to `MainWindow` first `CompositionTarget::Rendering` frame (`--perf-cold-start` telemetry flag).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Extend `scripts/build-corpus.py` (`--perf` flag) to deterministically generate `perf-100p-text.pdf` (100 pages of multi-column dense text) and `perf-500p-mixed.pdf` (500 pages of mixed text, vector graphics, and images), and add `--perf-trace` instrumentation hooks to `Recto.exe` and `recto-cli bench`. Done when: both benchmark PDFs generate deterministically and `recto-cli bench` + `Recto.exe --perf-trace` emit structured JSON measurements for all 5 metrics.
2. Author `tests/perf/run_perf.ps1` running `N=9` iterations per metric, computing median, interquartile range (IQR), min, and max, stamping host CPU/RAM/GPU/OS metadata from `D00 T01 §5`, and writing `build/perf-latest.json` plus a Markdown table; add a CI smoke test (`PerfSmoke.*` in `recto-tests`) with relaxed CI runner bounds. Done when: `pwsh -NoProfile -File tests/perf/run_perf.ps1` produces all 5 numbers with median and IQR in one command.

- [ ] Extend `scripts/build-corpus.py` to generate `perf-100p-text.pdf` (100 pages, ~3,000 characters/page) and `perf-500p-mixed.pdf` (500 pages with text, vector paths, and raster images) deterministically with fixed seeds and register their SHA-256 digests. Done when: two runs of `python scripts/build-corpus.py --perf` produce byte-identical 100-page and 500-page PDFs. Cheaper substitute: benchmarking on tiny 1-page unit-test PDFs that cannot reveal `O(N)` page-tree or LRU memory regressions.
- [ ] Measure Metric 1 (`First page visible < 120 ms` on `perf-100p-text.pdf`) and Metric 4 (`WorkingSetSize < 250 MB` on `perf-500p-mixed.pdf` after scrolling 20 pages) via `recto-cli bench --iterations 9 --json` and `Recto.exe --perf-trace`. Done when: JSON output reports `first_page_visible_ms` and `working_set_mb` with median and IQR across 9 runs.
- [ ] Implement `--perf-cold-start` in `Recto.exe` (`src/app/App.xaml.cpp`) recording `QueryPerformanceCounter` at `wWinMain` entry and at the first rendered frame of `MainWindow`, writing the elapsed milliseconds to stdout/JSON and exiting cleanly for Metric 5 (`Cold start to empty window < 350 ms`). Done when: `tests/perf/run_perf.ps1` spawns `Recto.exe --perf-cold-start` 9 times and reports median cold-start latency.
- [ ] Implement `--perf-scroll-zoom <pdf>` in `Recto.exe` driving a scripted 50-page scroll trace and a `100% -> 200%` zoom step on `DocumentViewCanvas`, recording UI-thread frame durations (`Metric 2: UI-thread frame time < 8 ms`, `0` frames blocked on engine work) and time from zoom command to sharp viewport tile completion (`Metric 3: 0 ms preview, < 100 ms sharp tiles`). Done when: `--perf-scroll-zoom perf-100p-text.pdf` emits JSON with p50/p95/max UI-thread frame times, preview latency, and sharp-tile completion latency.
- [ ] Author `tests/perf/run_perf.ps1` aggregating all 5 metrics, stamping host hardware (`Get-CimInstance Win32_Processor`, RAM, GPU, OS build, git SHA, build configuration), comparing against foundation plan section 8's targets, and outputting both JSON and a Markdown report. Done when: `pwsh -NoProfile -File tests/perf/run_perf.ps1` completes end-to-end and exits `0` when targets are met.
- [ ] Add `PerfSmoke.*` in `tests/recto-tests/perf_harness_tests.cpp` verifying the harness measurement code paths on CI with relaxed shared-runner ceilings (`first_page < 500 ms`, `500p_rss < 350 MB`) so regressions that blow up complexity or memory fail on every PR. Done when: `recto-tests --gtest_filter=PerfSmoke.*` passes in CI.
- [ ] Commit: `test: add automated performance harness for foundation plan section 8 targets`

**Test checkpoint:** `pwsh -NoProfile -File tests/perf/run_perf.ps1` output and `recto-tests --gtest_filter=PerfSmoke.*` output quoted with median and IQR across 9 iterations.

## 8. Primary reference-machine M2/M6 performance baseline record (`docs/perf/`)

Execute `tests/perf/run_perf.ps1` on the `D00 T01 §5` primary reference host machine (`x64` Release build), commit the dated baseline record under `docs/perf/2026-10-14-reference-baseline.md` and `docs/perf/README.md`, and verify every one of foundation plan section 8's five targets is either met with margin or has a filed optimization section in the owning domain.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Run `pwsh -NoProfile -File tests/perf/run_perf.ps1 -Output docs/perf/2026-10-14-reference-baseline.md` on the primary reference host machine recorded in `D00 T01 §5` (`docs/plans/2026-10-13-recto-foundation.md` section 8). Done when: `docs/perf/2026-10-14-reference-baseline.md` contains the machine spec, git commit SHA, and 9-run median/IQR/min/max numbers for all 5 targets.
2. Author `docs/perf/README.md` documenting how baselines are recorded and compared across milestones (M2 viewer baseline and M6 pre-release verification), and file any missed target via `add-todo` in `01-engine` or `02-viewer`. Done when: every target row in `docs/perf/2026-10-14-reference-baseline.md` carries a `PASS` status (or a named `DNN TNN §N` owner if missed) and `python scripts/todo-graph.py validate` exits `0`.

- [ ] Verify before running that the host machine matches the primary reference machine recorded in `docs/plans/2026-10-13-recto-foundation.md` section 8 (`D00 T01 §5`: CPU model, core count, RAM, OS build) and that the build is `Release | x64` with ASan disabled. Done when: the hardware header in `docs/perf/2026-10-14-reference-baseline.md` matches section 8's reference machine record. Cheaper substitute: recording numbers from a Debug or ASan build or an undocumented machine without stating the configuration.
- [ ] Record the 9-iteration median, IQR, min, and max for all 5 targets (`First page visible < 120 ms`, `Scroll UI-thread frame time < 8 ms`, `Zoom preview 0 ms / sharp tiles < 100 ms`, `500-page memory < 250 MB`, `Cold start < 350 ms`) in `docs/perf/2026-10-14-reference-baseline.md`. Done when: all 5 rows are populated with measured numbers and spread (< 10% IQR/median noise).
- [ ] Evaluate each measured median against foundation plan section 8's target threshold and record `PASS` (with headroom percentage) or file an owning remediation section via `add-todo` if any target exceeds its ceiling. Done when: zero targets are unmeasured or unowned.
- [ ] Author `docs/perf/README.md` explaining the invocation command (`pwsh -NoProfile -File tests/perf/run_perf.ps1`), the repeatability rule (median of 9 runs after 1 warm-up discovery check, cold-open cache flush instructions), and the M2 vs M6 comparison workflow. Done when: `docs/perf/README.md` is committed and links to `2026-10-14-reference-baseline.md`.
- [ ] Add a schema/completeness test `PerfSmoke.BaselineRecordExistsAndIsComplete` in `tests/recto-tests/perf_harness_tests.cpp` verifying that `docs/perf/2026-10-14-reference-baseline.md` exists and contains all 5 target rows with numeric medians. Done when: `recto-tests --gtest_filter=PerfSmoke.BaselineRecordExistsAndIsComplete` passes.
- [ ] Commit: `docs: record primary reference-machine performance baseline in docs/perf/`

**Test checkpoint:** the 5-row summary table from `docs/perf/2026-10-14-reference-baseline.md` is quoted verbatim alongside `recto-tests --gtest_filter=PerfSmoke.*` passing.

## Verification

- Every milestone done-when gate that cites a verification suite (M1 golden render determinism, M2 performance targets, M3 text and search expectation tables, M4 and M5 conversion and security round-trips) quotes the corresponding suite in this file.
- Every golden refresh in git history is an isolated commit with a stated rendering cause, and `python scripts/refresh-goldens.py --check` exits `0`.
- `python scripts/todo-graph.py validate` passes.
