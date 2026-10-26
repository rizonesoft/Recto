---
schema_version: 1
id: selection-and-search
domain: 03-text
status: active
title: "TODO-01 -- Text Extraction, Built-in OCR, Selection and Search"
frozen: true
track: M3
---

# TODO-01 -- Text Extraction, Built-in OCR, Selection, Search & Replace, and Comparison

> **Goal:** Give every page (digital, scanned, or vector-curve-outlined) one shared `PageText` model in `recto-pdf` and `recto-core`: native structured text extraction from content streams and `/ToUnicode` maps, built-in High-Accuracy In-Process C++23 Neural OCR + `Windows.Media.Ocr` recognition (with deskew, Sauvola adaptive binarization, morphological line removal, recursive XY-cut/table layout segmentation, and per-character CTC quad alignment) for scanned pages and vector-outlined text curves, quad-precise pointer and keyboard selection with the SPEC floating toolbar and context menu (built from `recto-ui` controls), non-blocking `Ctrl+F` / `Ctrl+H` Find & Replace (with `Regex`), and the `Ctrl+Shift+D` Compare PDFs view (side-by-side synchronized scroll and visual diff overlay).

> **Current state:** Measured 2026-10-23. No text or OCR code exists yet: `src/pdf` holds the parser, filters, crypto and display list (no `page_text.*`, no `annotations.*`), `src/core` and `src/services` are scaffold files, `src/ui` does not exist yet (`D10 T01 §1` creates it) and `src/app` still holds the retired WinUI scaffold until `D10 T01 §2` replaces it. `tests/corpus/text/` holds only `scanned.pdf` and `two-column-links-outline.pdf` (`D00 T03 §3`); every other fixture named below is built by `§23`. `D01 T02` provides the content-stream interpreter, DirectWrite font loader and `DocumentActor`; `D02 T02` provides the page coordinate mapping (`§5`); `D02 T03` provides the panel hosting contract (`§3`). The UI boards are `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png` (Reading room inspector search tab and filmstrip hit markers), governed by `design/SPEC.md` sections 9.3 and 9.3.1.

> 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); sections rewritten against `todo/10-ui/`. The floating selection toolbar, `ui::Menu` context menu, `FindCard`, `CompareView` and the area pickers are `recto-ui` elements under `src/app/`, drawn in Direct2D from `src/ui/generated/recto_tokens.g.h`, and the clipboard goes through `ui::Clipboard`; the extraction, OCR, search and comparison engines are unchanged.

> **Groomed 2026-10-23:** (1) `Current state` above re-measured. (2) The `§4`, `§6` and `§10` checkpoints said "display-session screenshots verify/confirm" the boards, which no machine can judge; they now compare headless renders with crops of the SPEC boards through the `D10 T01 §20` golden suite (`recto::ui::test::golden::expect_matches`) and those rows depend on `D10 T01 §20`. (3) `§8` was an umbrella (recognizer, model weights, CTC alignment, Windows OCR, fusion, actor wiring, CLI): the recognizer, the trained weights and the alignment moved to the new `§20`, `§21` and `§22`, which `§8` now depends on. Nothing owned the trained model behind the `> 99%` accuracy claim. (4) Tests named fixtures (tagged, rotated, ligature, hyphenated, tilted scan, vector-outlined, revision pairs, flat form) that nothing built; new `§23` builds them and `§1`, `§5`, `§7`, `§9`, `§10` depend on it. (5) `§6` promised 60 fps while searching with no measurement; a frame-time step was added.

## Inputs

- `docs/specs/2026-10-22-recto-ui-toolkit-design.md` sections 4.10, 4.11, 5 and 6 (the `recto-ui` toolkit that replaces WinUI 3, the WinUI-to-`recto-ui` mapping and the `todo/10-ui/` sections this file consumes).
- `design/SPEC.md` sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`, `RectoTextSelectionBrush`), 9.3 (Search, Find & Replace, and selection), 9.3.1 (Compare PDFs), 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR engine and language), 10 (`Ctrl+F`, `Ctrl+H`, `F3`, `Shift+F3`, `Ctrl+Shift+D`), 11 (selection announced to screen readers).
- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`PageText`), 5 (High-Accuracy Neural OCR + `Windows.Media.Ocr`, Vector-Outlined Text OCR, Find & Replace, Document Comparison), 6.3 (`PageText` shared across selection, search, OCR, comparison and UIA), 7 (M3 done-when).
- -> XREF: D02 T02 §5 -- selection, search and comparison highlights composite over the page geometry and tile canvas built in `D02 T02`.
- -> XREF: D02 T03 §3 -- the Reading room Search inspector tab and filmstrip hit badges plug into the panel host established in `D02 T03`.
- -> XREF: D02 T04 §5 -- the `Automatic OCR on scanned pages`, `OCR engine`, and `OCR language` rows in Settings are consumed by §8 here.
- -> XREF: D03 T02 §1 -- `DocumentViewProvider` and its `ui::TextRangeAdapter` text ranges (`D03 T02 §1`, `§2`) consume `PageText`'s character quads, `Span` heading/style attributes, and immutable `StructNode` (`/StructTreeRoot` `/MCID`) structure snapshot built in §1 and §2 here.
- -> XREF: D03 T03 §3 -- the floating selection toolbar's `Highlight`, `Strikeout`, and `Add Note` actions invoke `D03 T03`'s annotation engine.
- -> XREF: D04 T02 §6 -- `PDF -> Plain Text` and `Searchable PDF (OCR)` consume `OcrPipeline` from §8 here.
- -> XREF: D04 T02 §8 -- `TableExtractor` consumes `PageText` character and span quads from §1 here.
- -> XREF: D04 T03 §2 -- `DocxWriter` (PDF to Word) consumes `PageText` (including OCR-populated `PageText` on scanned and vector-outlined pages) from §1 and §8 here.
- -> XREF: D04 T05 §7 -- `PDF/UA-1` auto-tagger consumes `PageText` blocks and reading order from §1 here.
- -> XREF: D05 T01 §10 -- `Redactor` consumes `PageText` character quads from §1 here.
- -> XREF: D05 T02 §9 -- `Find & Redact` in the Redaction Bar reuses the find engine from §5 and §6 here.
- -> XREF: D06 T01 §4 -- `TextTables.*` (`D06 T01 §4`) verifies `PageText` normalization, regex search, `Replace Text` (`§9`), and `Compare PDFs` (`§10`) built here.
- -> XREF: D06 T02 §3 -- `fuzz_text` (`D06 T02 §3`) fuzzes the `PageText` extractor, `SearchEngine`, and content-stream text replacer (`§1`, `§5`, `§9`) built here.
- -> XREF: D06 T03 §11 -- user-reported extraction, reading-order and cross-page selection fixtures, and the single-interpreter differential suite (§4 there) over `PageText`, find and replace.
- -> XREF: D03 T04 §5 -- content-stream replace reuses Edit PDF's rewrite engine (§5 there) and font resolver (§4 there).
- -> XREF: D03 T05 §3 -- search across files (D03 T05) reuses this engine and its options.
- -> XREF: D03 T02 §7 -- Read Out Loud (D03 T02 §7) consumes this text model.
- -> XREF: D03 T03 §4 -- spell service in comment editors.
- -> XREF: D03 T03 §8 -- spell service in form fields.
- -> XREF: D02 T04 §5 -- settings rows.
- -> XREF: D04 T02 §3 -- stitch_page_areas reuse.
- -> XREF: D07 T01 §10 -- render_page_area for print area.
- -> XREF: D04 T02 §7 -- searchable word writer.
- -> XREF: D05 T02 §9 -- zone OCR to redaction.
- -> XREF: D07 T01 §14 -- print area marquee.
- -> XREF: D07 T01 §11 -- render_page_area.
- -> XREF: D04 T02 §19 -- OcrPipeline layout.
- -> XREF: D04 T02 §19 -- suspects review input.
- -> XREF: D04 T02 §22 -- snapshot tool owns clipboard copy.
- -> XREF: D04 T02 §3 -- stitch_page_areas.
- -> XREF: D03 T03 §36 -- ruling-line removal feeds detection.
- -> XREF: D05 T01 §28 -- compare engine.
- -> XREF: D02 T03 §13 -- PageText counts.
- -> XREF: D02 T03 §14 -- find engine for bookmark proposals.
- -> XREF: D04 T05 §18 -- page text for math detection.
- -> XREF: D06 T04 §8 -- the `D-ReadAloud.png` board and `design/SPEC.md` section 9.13 that §12, §13 here build against.
- -> XREF: D06 T04 §9 -- the `D-SearchPanels.png` board and `design/SPEC.md` section 9.3.2 that §9, §17, §18, §19 here build against.
- -> XREF: D06 T04 §28 -- the `D-Compare.png` board and `design/SPEC.md` section 9.3.1 that §10 here builds against.
- -> XREF: D10 T01 §12 -- pointer, keyboard and accelerator routing that selection, `Ctrl+F`, `Ctrl+H` and the area pickers are built on.
- -> XREF: D10 T02 §14 -- `ui::Menu` is the selection context menu and the spell suggestion menu built here.
- -> XREF: D07 T01 §23 -- the release packaging ships the `modelsecto-ocr-latin-v1.rocr` file §21 builds.

## Outcome

- `recto-pdf` extracts `PageText` (`Blocks -> Lines -> Spans -> Chars` with quad bounding boxes in page points, font metadata, reading-order clustering, and bounded `/StructTreeRoot` `/MCID` structure nodes `StructNode` for tagged headings `/H1`..`/H6`, `/Table`/`/TR`/`/TH`/`/TD`, `/L`/`/LI`, and `/Figure` `/Alt` descriptions) on the `DocumentActor` thread, and `recto-cli text`, `recto-cli find`, `recto-cli replace`, and `recto-cli compare` expose it from the command line.
- Selecting text on a page paints `RectoTextSelectionBrush` over character quads, shows the dark floating toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`) above the selection and the full context menu on right-click (`Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find`, `Select all on page`, `Copy page link`, `Export Selection as Table`), and announces selection changes to screen readers.
- `Ctrl+F` and `Ctrl+H` open the Find & Replace card (top-right in Focus/Classic, Search inspector tab with filmstrip markers in Reading room), search all pages off the UI thread with `Match case`, `Whole word`, and `Regex` (regular expression) options, highlight matches in `RectoSearchHitBrush` / `RectoSearchHitCurrentBrush`, and replace matched text in content streams with glyph width reflow.
- Scanned pages and **vector-curve-outlined text pages** (where fonts were converted to vector paths) are recognized on the background `RenderPool` via Recto's C++23 image preprocessor (deskew, Sauvola adaptive binarization, morphological ruling-line removal, and recursive XY-cut/table layout segmentation) + High-Accuracy In-Process C++23 Neural OCR (`NeuralOcrRecognizer` with per-character CTC quad alignment) alongside `Windows.Media.Ocr` (`OcrEngine`), populating `PageText` so selection, copy, `Ctrl+F`, UIA screen readers, and downstream converters work seamlessly.
- `Ctrl+Shift+D` opens the **Compare PDFs View** (`SPEC 9.3.1`), comparing Document A and Document B in Side-by-Side Synchronized View or Visual Overlay Diff View with a Changes side panel listing every text and graphic addition, deletion, and replacement.
- The selection toolbar, context menu, `FindCard`, `CompareView`, area pickers and OCR review panel are `recto-ui` elements (`ui::Menu`, `ui::Card`, `ui::TextBox`, `ui::ItemsView`, `ui::SegmentedControl`) in C++ under `src/app/`, coloured only from `recto_tokens.g.h`.

**Adjacency:** list=applicable @ D03 T01 §6; document=applicable @ D03 T01 §9; settings=applicable @ D02 T04 §5; reporting=applicable @ D03 T01 §10; notifications=applicable @ D03 T02 §5; permissions=not-applicable (copy permission flags are reported in Info, D02 T03 §9); audit=not-applicable (search history is not persisted); exchange=applicable @ D03 T01 §4; reverse=applicable @ D03 T01 §4

**Adjacency rationale:** The list surface is the find results list (`§6`) and the comparison changes list (`§10`), document is saving a Find-and-Replace updated PDF copy (`§9`), reporting is the comparison diff summary (`§10`), exchange is copying selected text and page links to the Windows clipboard (`§4`), and reverse is clearing selection and closing/clearing find (`§4`, `§6`). Screen-reader announcements for selection and find counts are wired through `D03 T02 §5`, and the automatic-OCR, OCR-engine, and OCR-language toggles live in Settings (`D02 T04 §5`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Native `PageText` glyph-to-Unicode extraction and column reading-order clustering | D01 T02 §4, §23 | [ ] |
| 2 | §2 | `DocumentActor::get_page_text` LRU cache and `recto-cli text` command | §1, D01 T02 §8, D01 T03 §1 | [ ] |
| 3 | §3 | Quad-precise pointer and keyboard text selection (`SelectionModel`) | §2, D02 T02 §5, D10 T01 §9, D10 T01 §12 | [ ] |
| 4 | §4 | Dark floating selection toolbar, right-click context menu and clipboard copy | §3, D10 T02 §2, D10 T02 §14, D10 T02 §16, D10 T01 §17, D10 T01 §20 | [ ] |
| 5 | §5 | Background find engine (`Match case`, `Whole word`, `Regex`) and `recto-cli find` | §2, §23 | [ ] |
| 6 | §6 | `Ctrl+F` floating `FindCard`, Reading room `Search` inspector tab and hit highlights | §4, §5, D02 T03 §3, D10 T02 §4, D10 T02 §11, D10 T02 §16, D10 T01 §10, D10 T01 §20 | [ ] |
| 7 | §7 | Scanned/outlined detector, C++23 OCR preprocessor, line removal & XY-cut layout | §2, D01 T02 §9, §23 | [ ] |
| 8 | §8 | Hybrid Neural OCR + `Windows.Media.Ocr` pipeline, CTC quad alignment & CLI | §6, §7, §20, §21, §22, D02 T04 §5 | [ ] |
| 9 | §9 | Content-stream Find & Replace (`text_replace.cpp`), `recto-cli replace` and `Ctrl+H` | §6, D01 T02 §5, D06 T04 §9, D10 T02 §4, §23 | [ ] |
| 10 | §10 | Two-document comparison (`pdf_compare.cpp`), `recto-cli compare` and `Ctrl+Shift+D` | §6, §8, D06 T04 §28, D10 T02 §11, D10 T02 §18, §23, D10 T01 §20 | [ ] |
| 11 | §11 | Page-area render with chosen DPI, clipboard image and `recto-cli snapshot` | §2, D01 T02 §6, D01 T03 §1, D10 T01 §17 | [ ] |
| 12 | §12 | Snapshot tool: drag an area, choose DPI, copy the image to the clipboard | §4, §11, D02 T02 §5, D02 T03 §11, D02 T04 §5, D03 T02 §5, D06 T04 §8, D10 T01 §12, D10 T02 §6, D10 T02 §9, D10 T02 §19 | [ ] |
| 13 | §13 | Scrolling capture: stitch an area spanning pages into one tall image | §11, §12, D06 T04 §8, D10 T02 §6 | [ ] |
| 14 | §14 | Offline spell check service over `ISpellChecker` and platform text boxes | D02 T04 §5, D10 T02 §4, D10 T02 §14, D10 T02 §24 | [ ] |
| 15 | §15 | Zone OCR engine: recognize a drawn area and `recto-cli ocr --zone` | §8, §11 | [ ] |
| 16 | §16 | OCR word confidence, suspect list and undoable in-memory corrections | §8, §14 | [ ] |
| 17 | §17 | Zone OCR tool: draw an area, review the text, copy or add it to the text layer | §12, §15, §16, D02 T03 §11, D06 T04 §9, D10 T01 §12, D10 T02 §4 | [ ] |
| 18 | §18 | OCR review panel: word crop beside recognized text, accept or correct | §11, §16, D02 T03 §3, D02 T03 §11, D03 T02 §5, D06 T04 §9, D10 T01 §11, D10 T02 §4, D10 T02 §11 | [ ] |
| 19 | §19 | Save OCR corrections and zone text into the text layer of a copy | §16, §18, §17, D04 T02 §7, D06 T03 §5, D02 T01 §8, D06 T04 §9, D10 T02 §17 | [ ] |
| 20 | §20 | `NeuralOcrRecognizer`: model loader, SIMD kernels and CTC decoding | §7 | [ ] |
| 21 | §21 | Neural OCR weights: synthetic line renderer, trainer, export and accuracy gate | §20, §23 | [ ] |
| 22 | §22 | Per-character CTC quad alignment into tight `CharQuad`s | §7, §20 | [ ] |
| 23 | §23 | Text fixtures for M3 in `scripts/build-corpus.py` | D00 T03 §3 | [ ] |

## 1. Native `PageText` glyph-to-Unicode extraction and column reading-order clustering

Implement `src/pdf/page_text.h` and `src/pdf/page_text.cpp` in `recto-pdf`: during or alongside content-stream interpretation, map every glyph run (`Tj`, `TJ`, `'`, `"`) through the font's `/ToUnicode` CMap or encoding table and text matrix (`Tm * CTM`) to Unicode codepoints with exact quad bounding boxes in page points (`CharQuad`), font family/size/weight/italic flags, baseline angle, and marked-content `/MCID` tags (`BDC`/`EMC`), then resolve any PDF `/StructTreeRoot` structure tree and `/ParentTree` into immutable per-page `StructNode` trees (`/H1`..`/H6` heading levels on `Span`, `/Table`/`/TR`/`/TH`/`/TD` grid cells, `/L`/`/LI` lists, `/Figure` `/Alt` descriptions and bounding boxes, and tagged reading order, falling back to geometric column/block clustering when untagged).

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `PageText` (`Block`, `Line`, `Span`, `CharQuad`, `StructNode`) is deeply immutable once constructed, holds zero pointers or references to `recto::pdf::Document`, `/StructTreeRoot` dictionary objects, or mutable font objects, and crosses threads solely as `std::shared_ptr<const PageText>`.

**Build order:**

1. Define immutable `PageText` (`Block`, `Line`, `Span`, `CharQuad`, `StructNode`) in `src/pdf/page_text.h` and map positioned glyphs through `/ToUnicode`, font encoding vectors, `Tm * CTM`, and `BDC`/`EMC` `/MCID` markers into `CharQuad` items with ligature expansion (`fi`, `fl`, `ff`, `ffi`, `ffl` -> component codepoints with subdivided quads) and bounded `/StructTreeRoot` + `/ParentTree` resolution (`64`-level recursion cap and cycle guard). Done when: glyph-to-Unicode and `/StructTreeRoot` `/MCID` extraction produces exact codepoints, oriented quads, heading levels, table cells, and `/Figure` `/Alt` nodes across TrueType, Type 1, Type 3, and CIDFontType0/2 runs.
2. Implement `TJ` kerning-threshold space insertion, line baseline grouping (`0`/`90`/`180`/`270` and arbitrary rotation angles), soft-hyphen (`U+00AD`) line-break folding, and reading-order block clustering (ordering by `/StructTreeRoot` `/K` `/MCID` sequence when tagged and falling back to `XY-cut` / gutter projection when untagged). Done when: `PdfText.*` unit tests extract both tagged `/StructTreeRoot` fixtures and untagged two-column corpus pages in reading order under ASan.

- [ ] Define `CharQuad` (`uint32_t codepoint`, 4-corner quad in page points, `span_index`, `char_offset`, `int32_t mcid`), `Span` (font family, size pt, bold/italic/monospace flags, fill colour, baseline angle, `uint8_t heading_level` `0..6`, `int32_t struct_node_index`), `StructNode` (`role` `Heading`/`Paragraph`/`Table`/`TableRow`/`TableHeaderCell`/`TableDataCell`/`List`/`ListItem`/`Figure`, `heading_level`, `row_index`, `col_index`, `row_span`, `col_span`, `alt_text`, `bbox_pt`, `char_start`, `char_end`, `children`), `Line`, `Block`, and immutable `std::shared_ptr<const PageText>` in `src/pdf/page_text.h`. Done when: `sizeof(CharQuad)` and `PageText` compile cleanly with zero mutable public members and zero pointers into `pdf::Document`.
- [ ] Map `Tj`, `TJ`, `'`, and `"` glyph runs through `D01 T02 §28`'s `simple_font_unicode` (the `/ToUnicode` CMap, then the encoding's glyph name through the Adobe Glyph List, then the TrueType `cmap`, never a guessed character) into Unicode codepoints and page-point quads transformed by `Tm * CTM` and page `/Rotate`, recording the active `BDC`/`EMC` `/MCID` integer when present. Done when: `PdfText.GlyphQuads` verifies character bounding quads and `/MCID` capture on unrotated and `90`/`180`/`270` rotated pages. -> XREF: D01 T02 §28

> **Corrected 2026-10-26:** (found by the `D01 T02 §28` completion review) the item named `D01 T02 §2` for `/ToUnicode` CMaps; `D01 T02 §28` owns them, with the encodings and the fallback order, through `simple_font_unicode`. Its acceptance gains real-PDF fixtures asserting exact extracted strings for a ToUnicode font, an AGL-named Type 1 and a TrueType font found only through its `cmap`.
- [ ] Parse the document's `/StructTreeRoot`, `/RoleMap`, and `/ParentTree` with a 64-level depth cap and cycle set, linking page `/MCID` runs and `/Figure` XObject/path bounding boxes into `PageText::struct_nodes` (`heading_level` `1..6` on `/H1`..`/H6` spans, row/column indices on `/Table`/`/TR`/`/TH`/`/TD`, and `/Alt` or `/ActualText` strings on `/Figure` and spans). Done when: `PdfText.TaggedStructTree` extracts heading levels, table cell bounds, and `/Figure` `/Alt` strings on a tagged PDF and rejects cyclic `/K` trees cleanly.
- [ ] Expand typographic ligatures (`U+FB00..U+FB06`: `ff`, `fi`, `fl`, `ffi`, `ffl`) into constituent ASCII codepoints with proportionally split horizontal character quads. Done when: `PdfText.Ligatures` extracts `"office"` and `"flower"` with individual character quads for each letter.
- [ ] Insert synthetic space characters when `TJ` horizontal kerning adjustments or inter-glyph gaps exceed `0.22 * em_size`, while keeping tight kerning pairs inside the same word. Done when: `PdfText.TjKerning` preserves word boundaries without splitting kerned words like `"To"` or `"AV"`.
- [ ] Group characters into `Line`s by baseline orientation and vertical projection, and fold line-ending soft hyphens (`U+00AD`) or explicit hyphenations across wrapped lines while retaining the visible hyphen quad for hit-testing. Done when: `PdfText.LineGrouping` joins wrapped hyphenated words cleanly.
- [ ] Order `Block`s and `Line`s by `/StructTreeRoot` `/MCID` traversal when the page carries tagged structure, and fall back to vertical whitespace gutter detection (`XY-cut`) on untagged pages so two-column and three-column academic pages read column-by-column after full-width headers. Done when: `PdfText.TwoColumnReadingOrder` and `PdfText.TaggedStructTree` read in logical sequence. Cheaper substitute: dumping raw `Tj` strings in stream order with no `/StructTreeRoot` or coordinate clustering.
- [ ] Add `PdfText.*` unit tests in `tests/recto-tests/pdf_text_tests.cpp` covering single-column, two-column, tagged `/StructTreeRoot`, rotated, ligature, and `TJ`-kerned fixtures under ASan. Done when: `recto-tests.exe --gtest_filter=PdfText.*` passes 100%.
- [ ] Commit: `feat(pdf): implement PageText glyph-to-Unicode extraction and column reading-order clustering`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfText.*` passes under ASan across single-column, two-column, tagged `/StructTreeRoot`, rotated and ligature fixtures.

## 2. `DocumentActor::get_page_text` LRU cache and `recto-cli text` command

Wire `PageText` extraction (including its immutable `struct_nodes` snapshot) into `DocumentActor::get_page_text` (`src/core/document_actor.cpp`) with a per-actor LRU cache of immutable `std::shared_ptr<const PageText>` snapshots alongside the `DisplayList` cache, and implement `recto-cli text <file.pdf> [--pages <range>] [--json]` in `src/cli/`.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `PageText` and `/StructTreeRoot` `struct_nodes` extraction executes exclusively on the owning `DocumentActor` thread (or swaps an immutable OCR `std::shared_ptr<const PageText>` snapshot on the actor thread), and callers (including UIA in `D03 T02 §1`-`§2`) receive only `std::shared_ptr<const PageText>`.

**Build order:**

1. Add `get_page_text(uint32_t page_index)` to `DocumentActor` with a 64-page LRU cache of `std::shared_ptr<const PageText>` snapshots and cache invalidation on page mutation (`ocr`). Done when: repeated `get_page_text` calls return the cached snapshot (including `struct_nodes`) in `< 0.1 ms` without re-parsing the content stream or `/StructTreeRoot`.
2. Implement `recto-cli text <file.pdf> [--pages <range>] [--json]` in `src/cli/`. Done when: `recto-cli text` outputs match the expected UTF-8 text and JSON structure fixtures for `tests/corpus/text/` under ASan.

- [ ] Add `GetPageTextCommand` and `get_page_text(uint32_t page_index)` to `DocumentActor` (`src/core/document_actor.h/.cpp`) returning `PdfResult<std::shared_ptr<const PageText>>`. Done when: calling `get_page_text` from any thread marshals onto the actor thread and returns an immutable snapshot containing `blocks`, `lines`, `spans`, `quads`, and `struct_nodes`.
- [ ] Maintain a 64-entry LRU cache of `std::shared_ptr<const PageText>` inside `DocumentActor` and invalidate affected page entries when background OCR (`§8`) publishes a recognized `PageText` snapshot. Done when: `ActorText.LruCacheAndInvalidate` verifies cache hits and invalidation.
- [ ] Implement `PageText::to_utf8_string()` formatting blocks with blank-line separators and lines with newline separators. Done when: `PageText::to_utf8_string()` produces deterministic UTF-8 output with `\n` line endings.
- [ ] Implement `recto-cli text <file.pdf> [--pages <range>] [--password <pw>] [--json]` in `src/cli/cmd_text.cpp`, printing plain UTF-8 text by default or structured JSON (`blocks`, `lines`, `spans`, `quads`, `struct_nodes`) with `--json`. Done when: `recto-cli text tests/corpus/text/two-column.pdf` exits `0` and prints the expected reading-order text.
- [ ] Verify out-of-range page numbers and encrypted PDFs without a valid password return deterministic CLI exit codes (`2` and `3`). Done when: `Cli.TextCommand` tests pass for valid, encrypted, and out-of-range inputs.
- [ ] Add `ActorText.*` and `Cli.TextCommand` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=ActorText.*:Cli.TextCommand` passes under ASan.
- [ ] Commit: `text: implement native PageText extraction model and recto-cli text`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfText.*:ActorText.*:Cli.TextCommand` passes under ASan across `tests/corpus/text/`.

## 3. Quad-precise pointer and keyboard text selection (`SelectionModel`)

Implement `SelectionModel` in `src/services/selection_model.h/.cpp` and interactive pointer/keyboard selection on `DocumentView` (`src/app/`): hit-test pointer coordinates against `PageText` character quads through `D02 T02 §5`'s page-to-viewport transform, support character drag, double-click word, triple-click line/paragraph, `Shift+Arrow` keyboard extension, `Ctrl+A` (select all on page), and multi-page drag selection, and paint `RectoTextSelectionBrush` (accent at 18% light / 22% dark) over the selected quads.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that `SelectionModel` reads only cached `std::shared_ptr<const PageText>` snapshots without blocking the UI thread on `DocumentActor` parsing.

**Fidelity:** SPEC sections 4 (`RectoTextSelectionBrush`), 9.3 (text selection), and 10 (`Ctrl+A`, `Esc`, `Shift+Arrow`), with `design/screenshots/D-Search.png`.
**Job:** The user can select exact character, word, line, or multi-page ranges on any page with mouse or keyboard and see crisp quad highlights that stay anchored across zoom and rotation. Consumer: the floating selection toolbar and clipboard (`§4`), the `FindCard` (`§6`), and the UIA text provider (`D03 T02 §2`).
**Treatment:** Hit-testing and painting over `PageText` character quads in page space (`(page_index, char_index)` endpoints) so zoom and rotation never shift selection bounds. Cheaper substitute that fails the checkpoint: rectangular rubber-band selection that ignores line flow, or losing the selection when zooming.
**Chrome:** consume `RectoTextSelectionBrush` from `src/ui/generated/recto_tokens.g.h` and the page-to-viewport transform from `D02 T02 §5`. Do not invent a second selection store separate from `D03 T02 §2`.

**Build order:**

1. Implement `SelectionModel` (`src/services/selection_model.cpp`) storing anchor and active `(page_index, char_index)` endpoints over `PageText`, with hit-testing (`hit_test_point`), word/line boundary expansion, `Shift+Left/Right/Up/Down` caret extension, `select_all_on_page`, and coalesced line-quad geometry computation. Done when: `TextSelection.*` unit tests verify char drag, double-click word, triple-click line, and multi-page selection ranges.
2. Wire pointer press/move/release (`ui::Input`, `D10 T01 §12`), double-click, triple-click, `Ctrl+A`, and `Esc` on `DocumentView` (`src/app/views/document_view.cpp`) and render the coalesced selection quads using `RectoTextSelectionBrush`. Done when: zooming or rotating with an active selection preserves the exact selected character range and quads on screen.

- [ ] Implement `SelectionEndpoint` (`page_index`, `char_index`) and `SelectionModel` in `src/services/selection_model.h/.cpp` with `SelectionChanged` notifications. Done when: `SelectionModel` tracks empty, single-page, and cross-page selections deterministically.
- [ ] Implement point-in-quad and nearest-character-on-line hit-testing (`SelectionModel::hit_test_point`) in PDF page points using `D02 T02 §5`'s `viewport_to_page` transform. Done when: dragging across wrapped lines selects characters in reading order.
- [ ] Implement double-click word selection (expanding to Unicode word boundaries) and triple-click line selection (expanding to the enclosing `PageText::Line`). Done when: `TextSelection.WordAndLineExpand` matches expected character spans.
- [ ] Implement keyboard selection extension (`Shift+Left`, `Shift+Right`, `Shift+Up`, `Shift+Down`, `Ctrl+A` for current page, and `Esc` or single-click on empty margin to clear). Done when: `TextSelection.KeyboardExtend` verifies caret movement across words and lines.
- [ ] Coalesce adjacent `CharQuad`s on the same baseline into tight per-line selection quads so `RectoTextSelectionBrush` paints without seams or double-alpha overlap between letters. Done when: a selected word produces a single merged quad per line segment. Cheaper substitute that fails the checkpoint: painting overlapping per-character alpha rects with visible dark seams between glyphs.
- [ ] Preserve selection `(page_index, char_index)` endpoints across zoom (`10%-800%`) and rotation (`R` / `Shift+R`) so selection quads recompute smoothly in the new transform. Done when: `TextSelection.ZoomAndRotatePersistence` passes.
- [ ] Switch the cursor to the I-beam (`CoreCursorType::IBeam`) when hovering over `PageText` character quads and restore the arrow/pan cursor over empty margins. Done when: hovering over page text shows the I-beam cursor.
- [ ] Add `TextSelection.ModelAndQuads` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=TextSelection.ModelAndQuads` passes.
- [ ] Commit: `feat(text): implement SelectionModel quad-precise pointer and keyboard selection`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextSelection.*` passes, verifying character, word, line, multi-page, and zoom/rotation-preserved selection quads.

## 4. Dark floating selection toolbar, right-click context menu and clipboard copy

Build the SPEC 9.3 dark floating selection toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link` with `36 epx` hit targets positioned above the selection) and the full right-click `ui::Menu` context menu (`Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find`, `Select all on page`, `Copy page link`, `Export Selection as Table`), plus plain UTF-8 and RTF clipboard copy (`Ctrl+C`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that clipboard serialization (`selected_text_utf8`, `selected_text_rtf`) reads only immutable `std::shared_ptr<const PageText>` snapshots without touching `recto::pdf::Document`.

**Fidelity:** SPEC sections 4 (dark floating bar tokens `#1B1A19` / `#2A2826`), 9.3 (floating selection toolbar and right-click context menu), 10 (`Ctrl+C`, `Esc`), and 11 (`36 epx` hit targets in floating toolbar), with `design/screenshots/D-Search.png`.
**Job:** Immediately after selecting text, the user can copy plain or formatted text, search for the selected phrase, copy a `#page=N` link, or trigger highlight/strikeout/note/redaction/table-export actions from either the floating toolbar or right-click menu. Consumer: the Windows clipboard, `FindCard` (`§6`), `D03 T03 §3` annotations, `D05 T02 §9` redaction, and `D04 T02 §10` table export.
**Treatment:** Dark floating toolbar (`OverlayCornerRadius` token, 8 epx) anchored `8 epx` above the selection bounding box (flipping below when near the top viewport edge) with `36 x 36 epx` icon buttons and full right-click `ui::Menu` (`D10 T02 §14`). Cheaper substitute that fails the checkpoint: only supporting `Ctrl+C` with no floating selection toolbar or right-click menu, or buttons smaller than `36 epx`.
**Chrome:** consume the dark floating bar tokens (`RectoFloatingBarBackgroundBrush` `#1B1A19`, `RectoFloatingBarBorderBrush` `#2A2826`) from `src/ui/generated/recto_tokens.g.h` and Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Implement plain UTF-8 and Rich Text Format (`CF_RTF` preserving font family, size, bold, italic, and fill colour from `PageText::Span`) clipboard writers in `src/services/selection_clipboard.cpp`, plus `#page=N` page-link copy. Done when: `Ctrl+C` copies both plain text and RTF to the clipboard and `Copy page link` copies `<path>#page=<N>`.
2. Build `SelectionFloatingToolbar` (`36 epx` buttons for `Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`) and the right-click `ui::Menu` on `DocumentView`, wiring `Copy`, `Copy with Formatting`, `Find`, `Select all on page`, and `Copy page link` now and routing markup/redact/table hooks to `D03 T03 §3`, `D05 T02 §9`, and `D04 T02 §10`. Done when: rendered screenshot matches `D-Search.png` selection treatment and clipboard readback matches the fixture string.

- [ ] Implement `SelectionModel::selected_text_utf8()` and `selected_text_rtf()` in `src/services/selection_clipboard.cpp`, preserving paragraph breaks, font names, point sizes, bold/italic flags, and RGB colours in RTF. Done when: `TextSelection.ClipboardPlainAndRtf` verifies both UTF-8 and RTF payloads from a multi-span selection.
- [ ] Wire `Ctrl+C`, toolbar `Copy`, and context-menu `Copy` / `Copy with Formatting` to `Windows::ApplicationModel::DataTransfer::Clipboard`. Done when: selecting across two lines and pressing `Ctrl+C` places the exact text on the Windows clipboard.
- [ ] Build `SelectionFloatingToolbar` (`src/app/controls/selection_floating_toolbar.h/.cpp`) using `RectoFloatingBarBackgroundBrush` fill, `RectoFloatingBarBorderBrush` border, the `OverlayCornerRadius` token (8 epx) with no hex or radius literal, and 6 buttons (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`) each a `ui::Button` (`D10 T02 §2`) >= `36 x 36 epx` with a `ui::Element` automation name and a `ui::ToolTip` (`D10 T02 §3`). Done when: `TextSelection.FloatingToolbarBounds` confirms all 6 buttons have >= `36 epx` hit targets and names. Cheaper substitute that fails the checkpoint: `28 epx` buttons or missing `Find`/`Link`/`Redact` actions.
- [ ] Position `SelectionFloatingToolbar` centered `8 epx` above the top-most selected quad onpointer release, automatically flipping `8 epx` below the bottom-most quad when the selection is within `48 epx` of the top of the viewport. Done when: selections at the very top of a page never clip the floating toolbar under the app toolbar.
- [ ] Build the right-click `ui::Menu` context menu (`SPEC 9.3`) with `Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find "<selection>"`, `Select all on page`, `Copy page link`, and `Export Selection as Table`. Done when: right-clicking an active selection opens the complete 11-item context menu.
- [ ] Wire `Link` / `Copy page link` to copy `<filename>#page=<1-based-page>` to the clipboard and `Find` to open `FindCard` (`§6`) seeded with the selected string; record explicit command hooks for `Highlight`/`Underline`/`Strikeout`/`Add Note` (`-> XREF: D03 T03 §3`), `Redact` (`-> XREF: D05 T02 §9`), and `Export Selection as Table` (`-> XREF: D04 T02 §10`). Done when: clicking `Find` on the floating toolbar seeds the search query and clicking `Link` copies the page reference.
- [ ] Dismiss `SelectionFloatingToolbar` and clear selection when pressing `Esc` or clicking outside any text quad. Done when: `Esc` closes the floating toolbar and clears `SelectionModel`.
- [ ] Add the headless golden entry `selection_toolbar` to `tests/recto-tests/golden/ui/manifest.json` (the `D-Search.png` toolbar crop, Light and Dark, scales 100, 150, 200) and `Golden.TextSelectionToolbar`. Done when: the test passes and fails when `RectoFloatingBarBackgroundBrush` is replaced by `RectoRaisedBrush`. Cheaper substitute: a person comparing screenshots.
- [ ] Commit: `text: add quad-precise text selection, floating toolbar and context menu`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextSelection.*:Golden.Text*` passes, where `Golden.TextSelectionToolbar` renders the floating toolbar and quad highlight headless at 100, 150 and 200 percent and must reach the `ssim_min` of its `tests/recto-tests/golden/ui/manifest.json` entry against the `D-Search.png` crop (a toolbar drawn in the wrong colour token or at the wrong offset drops the SSIM below it and fails).

## 5. Background find engine (`Match case`, `Whole word`, `Regex`) and `recto-cli find`

Implement the background find engine in `src/core/search.h/.cpp` and `recto-cli find <file.pdf> <query> [--case] [--whole-word] [--regex] [--json]`: scan pages outward from the current visible page on background workers with cooperative `CancelToken` cancellation, Unicode case-folding, ligature (`fi` matching `"fi"`), soft-hyphen line-wrap normalization, `Match case`, `Whole word`, and ECMAScript regular expression (`Regex`) matching, producing hit quads and context snippets with bold match spans.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `SearchEngine` workers obtain `std::shared_ptr<const PageText>` snapshots via `DocumentActor::get_page_text`, poll `CancelToken` between pages, and never access `recto::pdf::Document` directly from worker threads.

**Build order:**

1. Implement `SearchEngine` (`src/core/search.cpp`) with Unicode case-folding, ligature/soft-hyphen normalization, `Match case`, `Whole word`, and `Regex` (`std::regex` / UTF-8 codepoint regex with syntax-error guard), mapping match offsets back to per-character `CharQuad` ranges and context snippets (`±40` chars around the match). Done when: `Search.*` unit tests verify exact hit counts, regex matches (dates, emails, invoice IDs), whole-word boundaries, and cross-line hyphenated matches under ASan.
2. Add progressive multi-page search scheduling (starting from `start_page` outward, checking `CancelToken` between pages) and `recto-cli find <file.pdf> <query> [--case] [--whole-word] [--regex] [--json]`. Done when: `recto-cli find` outputs exact page numbers, quad bounds, and snippets on `tests/corpus/text/`.

- [ ] Define `SearchOptions` (`match_case`, `whole_word`, `use_regex`, `start_page`), `SearchHit` (`page_index`, `char_start`, `char_count`, merged line quads, context snippet prefix/match/suffix), and `SearchSummary` (`total_hits`, `pages_with_hits`, `invalid_regex_error`) in `src/core/search.h`. Done when: `src/core/search.h` compiles cleanly across `recto-core`, `recto-services`, and `recto-cli`.
- [ ] Build the normalized page-search buffer that maps folded search characters (Unicode case-folding when `!match_case`, expanded `fi`/`fl`/`ff`/`ffi`/`ffl` ligatures, and stripped soft-hyphens across wrapped lines) 1-to-1 back to `PageText` `CharQuad` indices. Done when: searching `"office"` matches a `U+FB03` `ffi` ligature and highlights the exact ligature quad, and searching a word split across lines by a soft hyphen highlights both line segments. Cheaper substitute: `std::string::find` on raw ASCII bytes that misses `fi` ligatures, non-ASCII case folding, and line-break hyphens.
- [ ] Implement `Whole word` boundary checking using Unicode word-character classification (`std::isalnum` + Unicode letters/marks) at the start and end of every candidate match. Done when: searching `"cat"` with `Whole word` matches `"cat"` and `"cat."` but not `"catalog"` or `"scatter"`.
- [ ] Implement `Regex` mode with invalid-pattern error reporting (returning `SearchSummary::invalid_regex_error` without throwing uncaught exceptions or hanging on empty matches). Done when: `Search.RegexPatterns` matches `\b\d{4}-\d{2}-\d{2}\b` dates and gracefully reports malformed `[unclosed` patterns.
- [ ] Extract context snippets (`prefix`, `match_text`, `suffix` trimmed at word boundaries within `~80` characters) for each `SearchHit` to populate the SPEC 9.3 results list. Done when: `Search.ContextSnippets` produces clean single-line snippets with newline characters normalized to spaces.
- [ ] Schedule multi-page search outward from `start_page` (`start_page, start_page+1, ..., N-1, 0, ..., start_page-1`), invoking a progressive per-page callback and aborting within `< 5 ms` when `CancelToken::is_cancelled()` flips. Done when: `Search.ProgressiveAndCancel` cancels a 200-page search mid-flight cleanly.
- [ ] Implement `recto-cli find <file.pdf> <query> [--case] [--whole-word] [--regex] [--json]` in `src/cli/cmd_find.cpp`. Done when: `recto-tests.exe --gtest_filter=Search.*:Cli.FindCommand` passes under ASan.
- [ ] Add `SearchOptions::match_diacritics` (default `false`) folding the search buffer and query through Unicode NFD with combining marks removed, so `resume` finds `résumé` unless the user turns on exact accents. Done when: `Search.DiacriticInsensitive` finds `résumé`, `Résumé` and `resume` with the default and only `résumé` with `match_diacritics`.
- [ ] Keep hit offsets mapped to the original characters when folding changes string length (`İ`, `ß`, decomposed accents), so highlights never slice out of range. Done when: `Search.FoldingKeepsOffsets` highlights the exact original glyph quads for each folded match.
- [ ] Commit: `feat(text): implement background SearchEngine with regex and recto-cli find`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Search.*:Cli.FindCommand` passes under ASan across multi-page, ligature, hyphenated, and regex fixtures.

## 6. `Ctrl+F` floating `FindCard`, Reading room `Search` inspector tab and hit highlights

Build the SPEC 9.3 find surfaces and wire them to §5's background `SearchEngine`:
- In **Focus** and **Classic** presets: the top-right floating `FindCard` (`design/screenshots/D-Search.png`) with query box, `"2 of 14"` counter, previous/next (`Shift+F3` / `F3`), close (`Esc`), `Match case`, `Whole word`, and `Regex` toggles, `"4 pages"` summary, up to 6 context result rows with bolded match text and page number, and `"Show all N results"`.
- In **Reading room** preset: the same `SearchViewModel` hosted in the right inspector's `Search` tab (`design/screenshots/explorations/B-Search.png`) with hit markers on the bottom filmstrip thumbnails.
- On the page canvas: all matches highlighted in `RectoSearchHitBrush` and the active match in `RectoSearchHitCurrentBrush`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that `SearchViewModel` dispatches all multi-page search work off the UI thread with cooperative `CancelToken` cancellation and marshals progressive `SearchHit` results back via `ui::Dispatcher::post`.

**Fidelity:** SPEC sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`), 9.3, 10 (`Ctrl+F`, `F3`, `Shift+F3`, `Esc`), with `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png`.
**Job:** The user can find every literal or regex occurrence of a word or pattern across a large PDF immediately without stalling scrolling, see each match in context, and jump between hits. Consumer: `DocumentView` scrolls to the current match and paints hit quads; `D03 T02 §5` announces result counts.
**Treatment:** Shared `SearchViewModel` driving both the top-right `FindCard` (Focus/Classic) and the right-inspector `Search` tab + filmstrip badges (Reading room), cancelling stale queries via `CancelToken` as the user types. Cheaper substitute that fails the checkpoint: searching only already-rendered pages, blocking the UI thread while scanning a 500-page PDF, or omitting the context result list / Reading room inspector adaptation.
**Chrome:** consume `RectoSearchHitBrush` and `RectoSearchHitCurrentBrush` from `src/ui/generated/recto_tokens.g.h` and the shared `SearchViewModel`. Do not duplicate search state between layout presets.

**Build order:**

1. Implement `SearchViewModel` (`src/services/search_view_model.cpp`) managing the query string, `Match case` / `Whole word` / `Regex` toggles, active `CancelToken`, progressive hit aggregation, current hit index (`1..N`), `"N pages"` count, 6-row preview / `"Show all N results"` expansion, and `next_hit()` / `prev_hit()` / `select_hit(index)`. Done when: rapid query typing cancels in-flight searches and updates `SearchViewModel` deterministically.
2. Build `FindCard` (`src/app/controls/find_card.h/.cpp` for Focus/Classic) and the Reading room `Search` inspector tab + filmstrip hit-marker badges (`D02 T03 §3` / `D02 T03 §6`), and paint `RectoSearchHitBrush` / `RectoSearchHitCurrentBrush` quads on `DocumentView`. Done when: both Focus (`D-Search.png`) and Reading room (`explorations/B-Search.png`) surfaces match their boards and scrolling stays at 60 fps during search.

- [ ] Implement `SearchViewModel` in `src/services/search_view_model.h/.cpp` with debounced progressive query execution, `CancelToken` replacement on keystroke, `"k of N"` and `"M pages"` counters, and `next_hit` / `prev_hit` wrap-around navigation. Done when: `SearchUi.ViewModel` verifies progressive hit updates, wrap-around `F3`/`Shift+F3`, and cancellation.
- [ ] Build the floating `FindCard` (`src/app/controls/find_card.h/.cpp`) anchored top-right (`16 epx` inset) in Focus and Classic presets (`D-Search.png`), containing the query `ui::TextBox` (`D10 T02 §4`), `"2 of 14"` status text, previous/next/close icon buttons, `Match case` / `Whole word` / `Regex` `ui::Button` toggles (`ControlCornerRadius` token), `"N pages"` header, and the context results list (6 rows by default, expandable via `"Show all N results"`). Done when: `FindCard` matches `D-Search.png`.
- [ ] Render each result row with prefix text, bolded match span, suffix text, and right-aligned page number badge; clicking any row selects that hit and scrolls `DocumentView` so the hit quad is centered in the viewport. Done when: clicking a result row jumps to the target page and highlights that match as current.
- [ ] Host the same `SearchViewModel` inside the Reading room right inspector's `Search` tab (`D02 T03 §3`) and render accent hit-indicator dots/badges on matching page thumbnails in the bottom filmstrip (`D02 T03 §6`) when in the `Reading room` preset (`explorations/B-Search.png`). Done when: switching between `Focus` and `Reading room` preserves the active search query, results list, and current hit. Cheaper substitute that fails the checkpoint: showing the floating `FindCard` on top of the Reading room inspector instead of using the inspector `Search` tab and filmstrip markers.
- [ ] Paint all page hits in `RectoSearchHitBrush` and the active hit in `RectoSearchHitCurrentBrush` over `PageText` character quads on `DocumentView`. Done when: `SearchUi.HitQuads` confirms distinct brush tokens for passive vs active hits.
- [ ] Wire `Ctrl+F` (focus query box and select query text), `F3` / `Enter` (next hit), `Shift+F3` / `Shift+Enter` (previous hit), and `Esc` (close find and clear hit highlights when focus is in `FindCard`). Done when: all keyboard shortcuts operate end-to-end.
- [ ] Add `SearchUi.ScrollFrameTime` in `tests/recto-tests/search_ui_tests.cpp`: open `tests/corpus/text/long-500.pdf` (`§23`), start a whole-document query for a word on every page, scroll the canvas at a fixed rate through the headless `ui::Window` harness while the search runs, and record per-frame times from the `D10 T01 §14` frame clock. Done when: the test fails if the p95 frame time exceeds `16.7 ms` and prints the measured p95 and maximum. Cheaper substitute: asserting only that the search runs on a worker thread. Source: foundation plan section 7 (M3 done-when: find without blocking scrolling).
- [ ] Add the headless golden entries `find_card_focus` and `find_inspector_reading_room` to `tests/recto-tests/golden/ui/manifest.json` (`D-Search.png` and `explorations/B-Search.png` crops, Light theme, scales 100, 150, 200) and the `Golden.FindCard` and `Golden.FindInspectorTab` tests that call `recto::ui::test::golden::expect_matches`. Done when: both tests pass and fail when the current-hit fill is changed to `RectoSearchHitBrush`. Cheaper substitute: a person comparing screenshots.
- [ ] Commit: `text: implement non-blocking find engine with regex and Ctrl+F search surfaces`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Search.*:SearchUi.*:Golden.Find*` passes, where `Golden.FindCard` and `Golden.FindInspectorTab` compare headless renders with the `D-Search.png` and `explorations/B-Search.png` crops at 100, 150 and 200 percent against the manifest `ssim_min`, and `SearchUi.ScrollFrameTime` quotes a measured p95 frame time of at most `16.7 ms` while a 500-page search runs.

## 7. Scanned/outlined detector, C++23 OCR preprocessor, line removal & XY-cut layout

Implement `src/core/ocr_preprocess.h/.cpp` in `recto-core`: detect **scanned pages** (0 extractable `PageText` characters with raster image XObjects covering `>= 50%` of the page box) and **vector-curve-outlined text pages** (0 text operators with high-density glyph-sized Bezier/line subpaths `m`/`l`/`c`/`h`/`f`), rasterize target pages or regions at `300 DPI` on a `RenderPool` worker, and run Recto's native C++23 image preprocessor: auto-deskew (`-15°..+15°`), contrast normalization, integral-image Sauvola adaptive binarization, **Morphological Line Removal** (detecting and recording horizontal/vertical table and form ruling lines `>= 25 px` for `TableExtractor` and erasing them from the OCR bitmap without severing character strokes), and **Recursive XY-Cut & Table Layout Segmentation** (partitioning headers, multi-column body blocks, and 2D table cells in reading order before line recognition).

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `classify_page_for_ocr` and `preprocess_for_ocr` operate solely on immutable `const DisplayList&`, `const PageText&`, and worker-local `RgbaBitmap` buffers on `RenderPool` threads without locking `DocumentActor`.

**Build order:**

1. Implement `classify_page_for_ocr(const DisplayList&, const PageText&)` in `src/core/ocr_preprocess.cpp` distinguishing `DigitalText`, `ScannedRaster`, and `VectorOutlinedText` pages, plus `preprocess_for_ocr(const RgbaBitmap& src_300dpi, CancelToken)` performing grayscale conversion, projection-profile / Hough skew estimation (`-15°..+15°`), bilinear deskew rotation (recording `deskew_to_page_matrix`), histogram contrast stretch, and integral-image Sauvola local thresholding. Done when: `OcrPreprocess.PageClassification` classifies digital, scanned, and vector-outlined fixtures with 100% accuracy and tilted bitmaps deskew within `0.2°` of horizontal under ASan.
2. Implement `remove_ruling_lines_for_ocr` (morphological horizontal/vertical opening to extract `OcrDetectedRuleLine` segments and erase table/underline strokes while preserving vertical/horizontal character crossings) and `segment_ocr_layout` (recursive XY-cut whitespace projection + ruling-line grid cell detection producing ordered `OcrLayoutRegion` blocks, columns, and table cells). Done when: `OcrPreprocess.LineRemovalAndXyCut` cleanly separates a scanned two-column page with a bordered table into reading-order blocks and individual table cells without fusing adjacent columns under ASan.

- [ ] Define `OcrPageKind` (`DigitalText`, `ScannedRaster`, `VectorOutlinedText`), `OcrDetectedRuleLine`, `OcrLayoutRegion` (`Heading`, `BodyColumn`, `TableCell`, `Caption` with `row_index`/`col_index` and bounding box), and `classify_page_for_ocr` in `src/core/ocr_preprocess.h/.cpp`. Done when: pages with `< 3` characters and `>= 50%` image coverage classify as `ScannedRaster`.
- [ ] Detect `VectorOutlinedText` pages by counting closed subpaths (`m`/`l`/`c`/`v`/`y`/`h`) in the `DisplayList` whose bounding boxes fall within glyph dimensions (`4..72 pt` height) arranged along horizontal rows when `PageText` has `0` text operators. Done when: a PDF where text was converted to outlines classifies as `VectorOutlinedText` while a geometric line-art diagram does not. Cheaper substitute: checking only for raster images and ignoring vector-curve-outlined text pages.
- [ ] Render `ScannedRaster` and `VectorOutlinedText` pages at `300 DPI` on a background worker using `D01 T02 §6`'s tile/page rasterizer with `CancelToken` propagation. Done when: `300 DPI` rasterization never runs on the UI thread or holds the `DocumentActor` lock.
- [ ] Implement skew angle estimation (`-15°..+15°` in `0.1°` steps via horizontal projection variance maximization) and bilinear deskew rotation, storing the `2x3` inverse transform matrix `deskew_to_page_matrix`. Done when: `OcrPreprocess.Deskew` straightens a `3.5°`-tilted page bitmap and maps deskewed pixel coordinates back to original page points within `0.5 pt`.
- [ ] Implement contrast normalization (1st-to-99th percentile luminance stretch) and integral-image-accelerated Sauvola adaptive binarization (`window = 31 px`, `k = 0.2`) producing a clean high-contrast binary/BGRA8 buffer. Done when: uneven page shading and gray scan backgrounds binarize cleanly without erasing thin glyph strokes.
- [ ] Implement **Morphological Line Removal** (`remove_ruling_lines_for_ocr`): run directional horizontal and vertical run-length / morphological opening filters (`>= 25 px` at `300 DPI`) to record `OcrDetectedRuleLine` segments for table grid reconstruction and erase those ruling lines from the binarized image only at pixels where orthogonal stroke thickness confirms no character stem crosses the line. Done when: `OcrPreprocess.MorphologicalLineRemoval` removes table borders and underlines while keeping `"g"`, `"p"`, `"y"`, and `"H"` strokes intact.
- [ ] Implement **Recursive XY-Cut & Table Layout Segmentation** (`segment_ocr_layout`): partition the deskewed page into `OcrLayoutRegion`s by recursively splitting horizontal and vertical whitespace projection valleys (multi-column gutters and section headers) and intersecting `OcrDetectedRuleLine` grids into 2D `TableCell` regions ordered top-to-bottom, column-by-column. Done when: `OcrPreprocess.XyCutAndTableSegmentation` never merges text lines across a two-column gutter or across adjacent table cells. Cheaper substitute: feeding the entire unsegmented page bitmap into a line recognizer so two-column lines concatenate horizontally across the gutter.
- [ ] Respect `CancelToken` between preprocessing stages so closing a tab or cancelling a job aborts preprocessing immediately. Done when: `OcrPreprocess.Cancellation` aborts within `< 10 ms`.
- [ ] Add `OcrPreprocess.*` unit tests in `tests/recto-tests/ocr_preprocess_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OcrPreprocess.*` passes under ASan.
- [ ] Commit: `feat(ocr): add scanned/vector-outlined detector, Sauvola binarizer, line removal, and XY-cut layout`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrPreprocess.*` passes under ASan across digital, scanned, tilted, bordered-table, two-column, and vector-outlined fixtures.

## 8. Hybrid Neural OCR + `Windows.Media.Ocr` pipeline, CTC quad alignment & CLI

Implement `src/core/ocr_pipeline.h/.cpp` (`OcrPipeline`) combining Recto's **High-Accuracy In-Process C++23 Neural OCR Engine** (`NeuralOcrRecognizer` from `§20` running the weights of `§21`) alongside Windows 11's native **`Windows.Media.Ocr` (`OcrEngine`)** (`OcrEngineMode`: `HybridAuto` default, `NeuralHighAccuracy`, `WindowsMediaOcr`): recognize each segmented `OcrLayoutRegion` from §7, run **Per-Character CTC Quad Alignment** (`align_ctc_character_quads` from `§22`, mapped through `deskew_to_page_matrix` into tight page-point `CharQuad`s, `Span`s, `Line`s, `Block`s, and table `StructNode`s), populate `DocumentActor`'s `PageText` cache, add `recto-cli ocr <file.pdf> --text [--engine hybrid|neural|windows] [--vector-outlines] [--lang <tag>]`, and wire automatic/on-demand viewer OCR.

**Phase:** 3
**Needs:** windows-host
**Surface:** yes
**Freeze check:** Confirm that `OcrPipeline` runs `NeuralOcrRecognizer` and `OcrEngine::RecognizeAsync` on background worker threads and publishes the recognized `std::shared_ptr<const PageText>` snapshot into `DocumentActor` via an actor message without mutating any existing `PageText` in place.

**Fidelity:** SPEC sections 9.3 (selection and search work identically once OCR populates `PageText`), 9.5 (OCR engine and vector-outlined text OCR options), and 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR engine and OCR language).
**Job:** A user opening a scanned PDF, a multi-column academic scan, a scanned table, or a PDF where text was converted to vector curves can select individual characters/words, copy them, search with `Ctrl+F`, and read with a screen reader with state-of-the-art accuracy and zero external dependencies. Consumer: `DocumentActor`'s `PageText` cache, `D03 T02 §1` UIA provider, `D04 T02 §6` plain-text and `§7` searchable-PDF writers, and `D04 T03 §2` `DocxWriter`.
**Treatment:** Hybrid background `OcrPipeline` running `NeuralOcrRecognizer` + `Windows.Media.Ocr::OcrEngine` over §7's XY-cut layout regions and table cells, computing tight per-character `CharQuad`s via CTC timestep alignment and connected-component vertical ink bounds mapped through `deskew_to_page_matrix`, and caching the resulting `PageText` on `DocumentActor`. Cheaper substitute that fails the checkpoint: running OCR on the UI thread, dividing word boxes into equal-width character slices without CTC/ink alignment, ignoring vector-outlined curve text, or storing OCR text in a separate side-channel that selection and UIA cannot read.
**Chrome:** consume the existing `PageText` selection/search overlays (`§3`, `§6`), the Settings preferences (`D02 T04 §5`), and polite status announcement (`D03 T02 §5`); no modal dialog blocks reading while background OCR completes.

**Build order:**

1. Implement `Windows.Media.Ocr::OcrEngine` integration and the `HybridAuto` fusion in `src/core/ocr_pipeline.cpp` around the `NeuralOcrRecognizer` (`§20`, `§21`) and `align_ctc_character_quads` (`§22`), assembling `PageText` (`Block -> Line -> Span -> CharQuad` plus `StructNode` table cells from §7's `OcrLayoutRegion`s) and `recto-cli ocr <file.pdf> --text [--engine hybrid|neural|windows] [--vector-outlines] [--lang <bcp47>]`. Done when: `Ocr.*` tests run on `tests/corpus/text/scanned.pdf`, a two-column scan, and a vector-outlined-text fixture and extract expected text and tight per-character quads with `> 99%` character accuracy under ASan.
2. Wire automatic and on-demand OCR into `DocumentActor` and `SearchViewModel` (honoring `Automatic OCR on scanned pages`, `OCR engine`, and `OCR language` from `D02 T04 §5`). Done when: opening `scanned.pdf` or a vector-outlined PDF in the viewer makes its words selectable by mouse and searchable via `Ctrl+F` with tight character highlight quads.

- [ ] Implement `OcrEngineMode` (`HybridAuto`, `NeuralHighAccuracy`, `WindowsMediaOcr`) and engine initialization in `src/core/ocr_pipeline.h/.cpp`, loading the `NeuralOcrRecognizer` (`§20`) with the `§21` weights alongside `Windows::Media::Ocr::OcrEngine` (`TryCreateFromLanguage(Language(bcp47))` or `TryCreateFromUserProfileLanguages()`). Done when: `Ocr.EngineInit` initializes both the neural recognizer and Windows OCR cleanly across configured languages.
- [ ] Run recognition per `OcrLayoutRegion` (`Heading`, `BodyColumn`, `TableCell`, `Caption`) from §7 on a background worker thread with `CancelToken` cancellation, using `HybridAuto` confidence fusion (combining `NeuralOcrRecognizer` sequence recognition with `Windows.Media.Ocr` and selecting the higher-confidence decoding on dense/degraded or numeric/tabular lines). Done when: `Ocr.HybridRecognition` achieves `> 99%` character accuracy on scanned body text, tabular numbers, and vector-outlined text without touching the UI thread.
- [ ] Call `align_ctc_character_quads` (`§22`) for every recognized line and assemble the resulting `CharQuad`s, `Span`s, `Line`s, `Block`s and table `StructNode` cells into one immutable `PageText` per page. Done when: `Ocr.PageTextAssembly` shows a recognized two-column scan and a bordered table yielding the same block order and cell structure as the digital twin fixtures. Cheaper substitute that fails the checkpoint: dividing word boxes into equal-width slices.
- [ ] Populate `DocumentActor`'s `PageText` cache with the OCR-synthesized `std::shared_ptr<const PageText>` so `get_page_text` serves it to selection (`§3`), search (`§5`), UIA (`D03 T02 §1`), and converters (`D04`). Done when: calling `get_page_text` after OCR returns the recognized `PageText` snapshot. Cheaper substitute that fails the checkpoint: storing OCR output outside `PageText` so mouse selection and `Ctrl+F` still see an empty page.
- [ ] Implement `recto-cli ocr <file.pdf> --text [--engine hybrid|neural|windows] [--vector-outlines] [--lang <tag>] [--pages <range>]` in `src/cli/cmd_ocr.cpp`. Done when: `recto-cli ocr tests/corpus/text/scanned.pdf --text` prints the recognized text with `> 99%` character accuracy.
- [ ] Refuse and report instead of guessing: a missing or invalid `§21` weights file with `NeuralHighAccuracy` selected returns `OcrError::ModelUnavailable` with a plain message and falls back to Windows OCR only for `HybridAuto`; a missing Windows OCR language pack falls back to the neural engine and announces the language gap through `D03 T02 §5`. Done when: `Ocr.ModelUnavailable` and `Ocr.LanguagePackMissing` cover both paths and neither returns an empty page as success. Cheaper substitute: logging and returning empty text.
- [ ] Trigger background OCR automatically when viewing or searching a `ScannedRaster` or `VectorOutlinedText` page if `Automatic OCR on scanned pages` (`D02 T04 §5`) is enabled, or via the command box (`Ctrl+K` -> `Recognize Text (OCR)`). Done when: `Ctrl+F` on `scanned.pdf` highlights the recognized words at their exact image coordinates.
- [ ] Commit: `text: add hybrid C++23 Neural OCR + Windows.Media.Ocr pipeline and CTC quad alignment`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrPreprocess.*:Ocr.*` passes under ASan, and `recto-cli ocr tests/corpus/text/scanned.pdf --text` prints the expected recognized text.

## 9. Content-stream Find & Replace (`text_replace.cpp`), `recto-cli replace` and `Ctrl+H`

Implement `src/pdf/text_replace.h/.cpp` in `recto-pdf`: locate literal or regular-expression text matches in page content streams on an isolated authoring `recto::pdf::Document` instance (never sharing or mutating the open viewer `DocumentActor`'s `pdf::Document` in place), replace matched glyph-run strings (`Tj` / `TJ`), recalculate horizontal glyph advances using the font's width metrics (embedding or selecting a Standard-14 / TrueType fallback font resource when the replacement introduces characters absent from a subset font), write the updated PDF copy via `IncrementalWriter` (`D01 T02 §5`) and temp-file-then-replace, expose `recto-cli replace <in.pdf> --find <pat> --replace <str> [--regex] [--case] [--whole-word] --out <out.pdf>`, and wire the `Ctrl+H` Replace expansion row in `FindCard` (`SPEC 9.3`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that content-stream replacement always opens its own isolated `recto::pdf::Document` copy on a background job thread, never shares or mutates the open viewer `DocumentActor`'s `pdf::Document` in place, leaves the open viewer `DocumentActor` and its cached `std::shared_ptr<const DisplayList>` / `std::shared_ptr<const PageText>` snapshots untouched on cancellation or failure, writes output via temp-file-then-replace, and reloads/swaps the viewer tab onto a fresh `DocumentActor` only after the replacement copy succeeds.

**Fidelity:** SPEC sections 9.3 (`Ctrl+H` Find & Replace expansion in `FindCard`) and 10 (`Ctrl+H` shortcut) with `D-Search.png`. Design board: `design/SPEC.md` section 9.3.2 with `design/screenshots/D-SearchPanels.png` (drawn by `D06 T04 §9`).
**Job:** The user can replace literal or regex text matches one-by-one (`Replace`) or across the entire document (`Replace All`) and save the updated PDF copy with real content-stream text modifications without risking in-place corruption of the open viewer document. Consumer: saved PDF copies and reloaded `DocumentActor` viewer tab.
**Treatment:** Isolated-copy content-stream glyph-run replacement with font width reflow in `src/pdf/text_replace.cpp` and `Ctrl+H` expansion in `FindCard`. Cheaper substitute that fails the checkpoint: mutating the live viewer `DocumentActor` in place, painting a white rectangle with an annotation overlay instead of updating the content stream, or corrupting glyph spacing after replacement.
**Chrome:** consume §6's `FindCard` and Reading room `Search` inspector tab, expanding the `Replace with` row when `Ctrl+H` or the chevron toggle is invoked.

**Build order:**

1. Implement `replace_text_in_document` (`src/pdf/text_replace.cpp`) operating on an isolated `recto::pdf::Document` instance, rewriting matched `Tj` / `TJ` content-stream operators, recalculating horizontal glyph advances for following runs on the same line, handling subset-font fallback when replacement characters are missing from the original font's `/Widths` or CMap, and saving via `IncrementalWriter`, plus `recto-cli replace`. Done when: `TextReplace.*` replaces literal strings and regex dates in a corpus PDF copy and `PageText` readback of the saved output confirms the replaced text at valid coordinates while the source viewer `DocumentActor` remains unmodified under ASan.
2. Add the `Ctrl+H` `Replace with` input row (`Replace` and `Replace All` buttons) to `FindCard` and the Reading room `Search` tab, running replacement on an isolated working copy, prompting temp-file-then-replace `Save` / `Save a copy` (`D02 T01 §8`), and reloading the viewer tab onto the saved copy on completion. Done when: pressing `Ctrl+H`, entering replacement text, and clicking `Replace All` writes the updated copy and reloads the canvas cleanly while leaving the open viewer document untouched on cancel or failure.

- [ ] Implement content-stream token span mapping in `src/pdf/text_replace.cpp` linking each `SearchHit` (`§5`) back to the originating `BT ... ET` block and `Tj` / `TJ` operator indices in the page `/Contents` stream on an isolated `pdf::Document` instance. Done when: `TextReplace.LocateOperators` maps single-operator and split-`TJ` matches accurately.
- [ ] Rewrite matched `Tj` / `TJ` strings with the replacement string (including regex capture-group substitutions `$1`, `$2` when `use_regex` is active) and adjust subsequent horizontal positioning (`TJ` offset or `Td`/`Tm` x-translation) on the same line by the width delta `width(new_text) - width(old_text)`. Done when: replacing a short word with a longer word shifts trailing text on the same line without overlapping glyphs. Cheaper substitute that fails the checkpoint: replacing bytes in-place without updating glyph advances or failing whenever replacement length differs.
- [ ] Detect when a replacement string contains Unicode codepoints missing from the original subset font's encoding/`/ToUnicode` map and automatically switch the replaced run to an embedded TrueType/Standard-14 fallback font resource in the page `/Resources /Font` dictionary. Done when: `TextReplace.SubsetFontFallback` replaces text using characters not originally present on the page and extracts cleanly via `PageText`.
- [ ] Recompress the modified `/Contents` stream (`FlateDecode`) and write the updated PDF copy via `IncrementalWriter` (`D01 T02 §5`) and temp-file-then-replace (`D02 T01 §8`), then reload the viewer tab onto a fresh `DocumentActor` opened from the committed output copy while proving in `TextReplace.ViewerIsolationOnSuccessFailureAndCancel` that the original viewer `DocumentActor` and source file remain completely unchanged on success (prior to explicit replace-original), failure, or `CancelToken` abort. Done when: `TextReplace.ViewerIsolationOnSuccessFailureAndCancel` passes under ASan.
- [ ] Implement `recto-cli replace <in.pdf> --find <pat> --replace <str> [--regex] [--case] [--whole-word] --out <out.pdf>` in `src/cli/cmd_replace.cpp`. Done when: `Cli.ReplaceCommand` verifies exit codes, replacement counts, and output text readback.
- [ ] Add the `Ctrl+H` `Replace with` row (`ReplaceTextBox`, `Replace` single-hit button, `Replace All` button) to `FindCard` and the Reading room `Search` inspector tab (`SPEC 9.3`). Done when: pressing `Ctrl+H` toggles the replace row open and focuses `ReplaceTextBox`.
- [ ] Add `TextReplace.*` and `SearchUi.FindAndReplace` tests in `tests/recto-tests/`. Done when: `recto-tests.exe --gtest_filter=TextReplace.*:SearchUi.FindAndReplace` passes under ASan.
- [ ] Commit: `feat(text): implement content-stream Find & Replace engine, recto-cli replace, and Ctrl+H UI`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextReplace.*:Cli.ReplaceCommand:SearchUi.FindAndReplace` passes under ASan.

## 10. Two-document comparison (`pdf_compare.cpp`), `recto-cli compare` and `Ctrl+Shift+D`

Implement `src/core/pdf_compare.h/.cpp` in `recto-core` (word/line Myers diff over `PageText` quads plus normalized page bitmap/display-list visual diff), add `recto-cli compare <a.pdf> <b.pdf> [--json]`, and build the `Ctrl+Shift+D` **Compare PDFs View** (`SPEC 9.3.1`): **Side-by-Side Synchronized View** (locked scroll and zoom across Document A and Document B), **Visual Overlay Diff View** (`RectoSuccessBrush` tint for insertions, `RectoDangerBrush` tint for deletions), and the right **Changes panel** listing every text and graphic addition, deletion, and replacement by page.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Verify that Document A and Document B each run on their own isolated `DocumentActor` instance, and `compare_documents` consumes only immutable `std::shared_ptr<const PageText>` and `std::shared_ptr<const DisplayList>` snapshots on background workers.

**Fidelity:** SPEC sections 4 (`RectoSuccessBrush`, `RectoDangerBrush`), 9.3.1 (`Ctrl+Shift+D` Compare PDFs View: Side-by-Side Synchronized View, Visual Overlay Diff View, and Changes panel), and 10 (`Ctrl+Shift+D` shortcut). Design board: `design/SPEC.md` section 9.3.1 with `design/screenshots/D-Compare.png` (drawn by `D06 T04 §28`).
**Job:** The user can compare two versions of a PDF document to inspect every text insertion, deletion, replacement, and graphic change side-by-side or overlaid, and jump to any change from the summary panel. Consumer: `CompareView` canvases and Changes panel, plus `recto-cli compare` JSON reports.
**Treatment:** Dual text-quad Myers diff + visual raster diff on background workers, rendered in a dedicated `CompareView` with synchronized two-canvas scrolling/zooming or alpha-blended visual overlay. Cheaper substitute that fails the checkpoint: plain-text diff without page quad coordinates, or unsynchronized side-by-side panes with no visual overlay mode.
**Chrome:** consume `RectoSuccessBrush` (insertions), `RectoDangerBrush` (deletions), `RectoWarningBrush` (replacements/graphic diffs) from `src/ui/generated/recto_tokens.g.h`, and reuse `DocumentView` tile presentation for both panes.

**Build order:**

1. Implement `compare_documents(doc_a, doc_b, CancelToken)` in `src/core/pdf_compare.cpp` computing a word-and-line Myers diff over `PageText` (with exact page-point `CharQuad` bounds in both documents) plus a `150 DPI` visual tile diff for non-text graphic changes, and expose `recto-cli compare <a.pdf> <b.pdf> [--json]`. Done when: `PdfCompare.*` identifies exact inserted, deleted, replaced, and graphic-modified regions between two corpus document revisions under ASan.
2. Build `CompareView` (`src/app/views/compare_view.h/.cpp`, `Ctrl+Shift+D`, `SPEC 9.3.1`) with Document A / Document B file selectors, `Side-by-Side` vs `Visual Overlay` mode segmented toggle, locked scroll/zoom synchronization, diff quad overlays, and the right `Changes` panel grouped by page. Done when: clicking any entry in the `Changes` panel scrolls both canvases to the diff quad and toggles cleanly between Side-by-Side and Visual Overlay modes.

- [ ] Define `DiffKind` (`Inserted`, `Deleted`, `Replaced`, `GraphicChanged`), `DiffEntry` (`id`, `kind`, `page_a`, `page_b`, `quads_a`, `quads_b`, `old_text`, `new_text`), and `ComparisonReport` (`total_insertions`, `total_deletions`, `total_replacements`, `total_graphic_changes`, `entries`) in `src/core/pdf_compare.h`. Done when: `ComparisonReport` serializes to deterministic JSON.
- [ ] Implement the Myers O(ND) word-sequence diff over `PageText` tokens across Document A and Document B in `src/core/pdf_compare.cpp`, coalescing adjacent inserted/deleted words into phrase-level `DiffEntry` records with exact `CharQuad` bounding boxes in page points. Done when: `PdfCompare.TextDiff` detects exact word additions, deletions, and replacements across multi-page revisions. Cheaper substitute that fails the checkpoint: line-only plain-text diff without `CharQuad` coordinates.
- [ ] Implement the visual raster diff pass (`150 DPI` page renders compared in `8 x 8 pt` cells after masking out already-reported text diff quads) to detect modified diagrams, signatures, stamps, or layout shifts as `GraphicChanged` bounding boxes. Done when: `PdfCompare.GraphicDiff` detects a modified vector figure on a page whose text did not change.
- [ ] Implement `recto-cli compare <a.pdf> <b.pdf> [--json]` in `src/cli/cmd_compare.cpp`, printing a human-readable summary or structured JSON diff report. Done when: `Cli.CompareCommand` passes under ASan.
- [ ] Build `CompareView` (`src/app/views/compare_view.h/.cpp`, reachable via `Ctrl+Shift+D`, `Tools -> Compare PDFs`, and `Ctrl+K`) with header pickers for Document A (defaulting to the active viewer tab) and Document B, a `Side-by-Side` / `Visual Overlay` segmented switch, and a diff-count summary badge. Done when: `Ctrl+Shift+D` opens `CompareView` with the current document preloaded as Document A.
- [ ] Implement **Side-by-Side Synchronized View** (two `DocumentView` canvases with locked scroll offset and zoom level, painting `RectoDangerBrush` over deletions on A and `RectoSuccessBrush` over insertions on B) and **Visual Overlay Diff View** (single composite canvas blending Document A and B with colour-coded diff highlights). Done when: scrolling or zooming Pane A updates Pane B in lockstep within the same frame.
- [ ] Build the right **Changes panel** (`SPEC 9.3.1`) listing all `DiffEntry` cards grouped by page with filter chips (`All`, `Inserted`, `Deleted`, `Replaced`, `Graphics`) and click-to-jump navigation. Done when: `AppTests.ComparePdfsUi` drives opening two fixtures in `CompareView`, filtering changes, clicking a diff card to jump both panes, and toggling `Visual Overlay`.
- [ ] Add the headless golden entries `compare_side_by_side` and `compare_overlay` to `tests/recto-tests/golden/ui/manifest.json` (`D-Compare.png` crops, Light theme, scales 100, 150, 200) and `Golden.CompareSideBySide` and `Golden.CompareOverlay` driving `CompareView` over `tests/corpus/text/revision-a.pdf` and `revision-b.pdf` (`§23`). Done when: both pass and fail when insertions are tinted with `RectoDangerBrush`. Cheaper substitute: a person comparing screenshots.
- [ ] Commit: `text: add content-stream Find & Replace (Ctrl+H) and Compare PDFs view (Ctrl+Shift+D)`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCompare.*:Cli.CompareCommand:AppTests.ComparePdfsUi:Golden.Compare*` passes under ASan, where `Golden.CompareSideBySide` and `Golden.CompareOverlay` compare headless renders of `CompareView` with the `D-Compare.png` crops at 100, 150 and 200 percent against the manifest `ssim_min`.

## 11. Page-area render with chosen DPI, clipboard image and `recto-cli snapshot`

Add the engine behind a snapshot tool: render any rectangle of a page to a bitmap at a chosen DPI on a `RenderPool` worker, put it on the clipboard as an image, save it to a file, and expose it as `recto-cli snapshot`. This is Acrobat Pro's `Snapshot` tool and matches Foxit PDF Editor's Snapshot, UPDF's Screenshot and PDFelement's Screenshot. It must not break the thread contract (the render reads only immutable `DisplayList` snapshots and never the live `recto::pdf::Document`) and it keeps Recto's improvement that the image is re-rendered from the page at the requested DPI, so it is crisp at any zoom and never a capture of window pixels. Every file write is temp-file-then-replace.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `render_page_area` obtains `std::shared_ptr<const DisplayList>` snapshots from `DocumentActor`, runs on `RenderPool` workers with a `CancelToken`, never calls `recto::pdf::Document` from the caller thread, and caches no tile after an abort.

**Build order:**

1. Define `AreaRenderRequest` and implement `render_page_area` in `src/services/area_render.h/.cpp`, rasterizing the rectangle tile by tile through the shared rasterizer (`D01 T02 §6`) into one `RgbaBitmap`. Done when: `AreaRender.PixelSize` renders a `200 x 100 pt` area at `72`, `150` and `300` DPI and gets `200 x 100`, `417 x 208` and `834 x 417` pixels, with stride read from the bitmap and never assumed to be `width * 4`.
2. Add the image sinks: the clipboard payload (PNG stream plus device-independent bitmap), file encoding through WIC (`PNG`, `JPEG`, `BMP`, `TIFF`), and `recto-cli snapshot`. Done when: `Cli.SnapshotCommand` writes a PNG whose size and pixels match `render_page_area` for the same request.

- [ ] Define `AreaRenderRequest` (`page_index`, `rect_pt` in unrotated user space with the origin at the bottom-left of the crop box, `dpi` `36..1200`, `include_annotations`, `transparent_background`, `grayscale`) and `AreaRenderResult` in `src/services/area_render.h`. Done when: the header compiles in `recto-services` with no `src/ui` include.
- [ ] Convert the displayed rectangle to user space for pages with `/Rotate` `90`, `180` and `270` and a crop box not at the origin, with `area_from_viewport_rect` using `D02 T02 §5`'s transform. Done when: `AreaRender.RotatedPages` renders the same page content for a rectangle drawn on each rotation of one fixture.
- [ ] Bound the work: refuse a request over `268435456` pixels with a typed `AreaTooLarge` error naming the largest DPI that fits, refuse a rectangle that is empty, non-finite or outside the crop box, and poll the `CancelToken` between tiles. Done when: `AreaRender.Bounds` accepts `36` DPI and rejects `35` and `1201`, returns each error and a cancelled render ends within `50 ms` leaving no cached tile.
- [ ] Honour `include_annotations` and an optional transparent background (alpha `0` where the page paints nothing). Done when: `AreaRender.AnnotationsAndAlpha` shows a highlight present only when requested and a transparent corner pixel with alpha `0`.
- [ ] Encode with WIC on the calling worker and write files through `AtomicFileWriter` (temp file in the destination folder, then replace). Done when: `AreaRender.FileSinks` writes the four formats, reads each back at the same size, and leaves no temp file after a forced failure.
- [ ] Build the clipboard payload (`SnapshotClipboardPayload`: PNG stream plus a `CF_DIBV5`-compatible bitmap) off the UI thread, and hand it to the UI thread only for `ui::Clipboard` (`D10 T01 §17`). Done when: `AreaRender.ClipboardPayload` decodes both formats to the same pixels and a source search shows no `ui::Clipboard` call outside the UI hand-off.
- [ ] Implement `recto-cli snapshot <file.pdf> --page <N> --rect <x,y,w,h> [--dpi <n>] [--format png|jpg|bmp|tiff] [--no-annotations] [--transparent] [--gray] --out <file>` in `src/cli/cmd_snapshot.cpp`, reading `--rect` in user-space points, with the shared exit codes (`D01 T03 §1`) and a refusal to overwrite the input. Done when: `Cli.SnapshotCommand` verifies success, an out-of-range page, an oversize request (`AreaTooLarge` message) and the input hash unchanged.
- [ ] Add `AreaRender.*` and `Cli.SnapshotCommand` tests in `tests/recto-tests/area_render_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=AreaRender.*:Cli.SnapshotCommand` passes under ASan.
- [ ] Commit: `feat(text): add page-area render, clipboard image and recto-cli snapshot`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AreaRender.*:Cli.SnapshotCommand` passes under ASan, and scaling a `72` DPI render up instead of re-rendering makes `AreaRender.PixelSize` fail on edge sharpness.

## 12. Snapshot tool: drag an area, choose DPI, copy the image to the clipboard

Build the `Snapshot` tool on top of §11: the user chooses `Snapshot` from the command box or the Tools menu, drags a rectangle on a page, picks a DPI, and copies the area to the clipboard as an image or saves it to a file. This matches Acrobat Pro's `Snapshot` tool and Foxit PDF Editor's, UPDF's and PDFelement's screenshot tools. It must not break the shell rules (`Esc` cancels, every command is in `Ctrl+K`, motion respects "Show animations in Windows") and it keeps Recto's improvement that a keyboard-only user can set the area numerically and the result is announced.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that the tool reads page geometry from the cached transform of `D02 T02 §5`, hands the render to §11 on a worker, and shows progress without blocking the UI thread.

**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.2 (contextual popover chrome) and section 9.3 (the dark floating selection toolbar, `D03 T01 §4`), with `design/screenshots/D-Search.png` for the toolbar treatment. Design board: `design/SPEC.md` section 9.13 with `design/screenshots/D-ReadAloud.png` (drawn by `D06 T04 §8`).
**Job:** The user drags a rectangle over a figure, a table or a paragraph, sees its pixel size, chooses `150` or `300` DPI, presses `Copy` and pastes a sharp image into another app. Consumer: the Windows clipboard, §13, and `Save as...`.
**Treatment:** A transparent `AreaPickerLayer` over `DocumentView` with a dashed `RectoAccentBrush` outline, eight `8 x 8 epx` handles and a text label (`1240 x 880 px at 150 DPI`), then a dark floating popover with `36 x 36 epx` buttons (`Copy`, `Save as...`, `Cancel`) and a DPI combo. Cheaper substitute that fails the checkpoint: capturing the window pixels at the current zoom, or a tool with no keyboard path.
**Chrome:** consume `RectoAccentBrush`, the dark floating-bar tokens used by `D03 T01 §4`, `RectoTextBrush` for the size label and Fluent UI System Icons (`RectoIcons`: `camera`); no hex values.

**Build order:**

1. Implement `AreaPickerLayer` (`src/app/controls/area_picker_layer.h/.cpp`, reusable by §17) with drag, resize handles, move, `Esc` cancel and a numeric `Precise area` expander (`X`, `Y`, `Width`, `Height` in points and a `Page` box). Done when: `AppTests.AreaPicker` draws, moves and resizes a rectangle by pointer and sets the same rectangle by keyboard only.
2. Add the `Snapshot` command and popover wired to §11, with DPI choices `72`, `96`, `150`, `300`, `600` and `Custom`, the `Snapshot default DPI` setting, and an announcement of the result. Done when: `AppTests.SnapshotCopy` copies a rectangle at `300` DPI and the clipboard image has the announced size.

- [ ] Register `Snapshot` (`tools.snapshot`, no default accelerator) in `CommandRegistry` (`D02 T03 §11`), enabled when a document is open, and list it under the Tools menu. Done when: the command box finds `Snapshot` and `Keyboard.Audit` finds no accelerator clash.
- [ ] Change the cursor to a crosshair while the tool is active, restore it on exit, and keep text selection (`§3`) disabled only while the tool is active. Done when: `AppTests.AreaPicker` verifies the cursor and that a drag selects no text.
- [ ] Show the live pixel size label and a `File size estimate unavailable until saved` note only for `Save as...`; keep the label text, not colour, as the size signal. Done when: the label reads `<w> x <h> px at <dpi> DPI` and updates while resizing.
- [ ] Add `Snapshot default DPI` (`SnapshotDefaultDpi`, default `150`, choices as above) to Settings (`D02 T04 §5`), searchable in `Find a setting`, read by the popover. Done when: `AppTests.SnapshotSettings` changes it and the next popover opens with the new value.
- [ ] Copy on `Enter` or `Copy` through the §11 payload, save through `IFileSaveDialog` and the atomic writer, and announce `Snapshot copied, <w> by <h> pixels` through `AnnouncementService` (`D03 T02 §5`) without moving focus. Done when: `A11y.SnapshotAnnouncements` records the announcement and `FocusManager::GetFocusedElement` is unchanged.
- [ ] Give the handles, DPI combo and every popover button an automation name and tooltip, and keep every target at least `36 x 36 epx` in the popover. Done when: the inventory (`D03 T02 §3`) reports none missing or undersized. Cheaper substitute: tooltips only.
- [ ] Add `AppTests.AreaPicker`, `AppTests.SnapshotCopy`, `AppTests.SnapshotSettings` and `A11y.SnapshotAnnouncements`. Done when: `recto-tests.exe --gtest_filter=AppTests.AreaPicker:AppTests.Snapshot*:A11y.SnapshotAnnouncements` passes on the display-session host.
- [ ] Commit: `feat(app): add the Snapshot tool with DPI choice and clipboard copy`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.AreaPicker:AppTests.Snapshot*:A11y.SnapshotAnnouncements` passes on the display-session host, and replacing the §11 render with a window-pixel grab makes `AppTests.SnapshotCopy` fail on the size assertion.

## 13. Scrolling capture: stitch an area spanning pages into one tall image

Extend the Snapshot tool so a rectangle dragged across a page boundary, or a page range chosen in the popover, becomes one tall image with the pages stitched top to bottom. This matches PDFelement's scrolling screenshot and covers the case where Acrobat's Snapshot is limited to one page. It must not break §11's bounds (the stitched result obeys the same pixel cap and lowers the DPI with a visible message instead of failing) and it keeps Recto's rule that the render is re-rendered per page, never a screenshot of a scrolled window.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that `stitch_page_areas` renders each page strip through §11 on `RenderPool`, joins strips on the worker, and never reads `recto::pdf::Document` from the UI thread.

**Fidelity:** Genuinely new surface. It adds a `Spans pages <a> to <b>` row and a `Gap between pages` control to the §12 popover, following `design/SPEC.md` section 9.3 popover chrome. Design board: `design/SPEC.md` section 9.13 with `design/screenshots/D-ReadAloud.png` (drawn by `D06 T04 §8`), which draws the span row and the gap control in the snapshot popover.
**Job:** The user drags from the middle of page 3 into page 5, sees `Spans pages 3 to 5`, chooses to remove or keep the gaps, and copies one continuous image. Consumer: the clipboard and file sinks of §11, and `recto-cli snapshot --stitch`.
**Treatment:** Per-page strips rendered at one common DPI and joined vertically, narrower strips centred on the widest with page-white padding, with a `Gap between pages` choice of `None` (default), `Page gap` (`8 pt` in the `RectoCanvasBrush` colour) or `Page break line`. Cheaper substitute that fails the checkpoint: scrolling the viewport and grabbing window pixels, or scaling strips to different DPIs so text sizes differ.
**Chrome:** consume the §12 popover, `RectoCanvasBrush` for the gap colour and `RectoTextBrush` for labels.

**Build order:**

1. Implement `stitch_page_areas` in `src/services/area_render.cpp`: split a request that spans pages into per-page strips, render each with `render_page_area`, and join them with the chosen gap. Done when: `AreaRender.Stitch` joins a three-page fixture into one image whose height equals the strip heights plus gaps and whose seams show no duplicated or missing pixel row.
2. Add the cross-page drag, the span label and the gap control to the popover, and `recto-cli snapshot --pages <range> --stitch`. Done when: `AppTests.SnapshotSpan` drags across a page boundary and copies a stitched image, and `Cli.SnapshotStitch` writes the same pixels.

- [ ] Allow `AreaPickerLayer` (§12) to extend a drag across page boundaries in the vertical layout, clipping the rectangle to each page's visible crop box, and show `Spans pages <a> to <b>`. Done when: `AppTests.SnapshotSpan` shows the label for a drag from page 3 to page 5.
- [ ] Choose the common DPI as the requested DPI, lowering it with the visible message `Reduced to <n> DPI to fit` when the stitched size would pass the `268435456` pixel cap, and refuse only when even `36` DPI cannot fit. Done when: `AreaRender.StitchBounds` shows the lowered DPI, the message text, a stitched fixture that fits only at `40` DPI (below `72`) rendering through §11, and the refusal.
- [ ] Join strips of different widths by centring on the widest strip with white padding, and keep the same scale for every strip. Done when: `AreaRender.StitchWidths` stitches a letter page and an A3 page and finds both text heights in the ratio of their sources.
- [ ] Implement the three `Gap between pages` modes. Done when: `AreaRender.StitchGap` finds `0` gap rows, `8 pt` of gap rows and a gap with a line for the three modes.
- [ ] Extend the CLI with `--pages <range> --stitch [--gap none|gap|line]`, selecting the whole page boxes of the range. Done when: `Cli.SnapshotStitch` verifies the output height and an invalid range exit code.
- [ ] Make the render cancellable from the popover's `Cancel` and show progress (`Rendering page 4 of 12`) as text. Done when: `AppTests.SnapshotSpan` cancels mid-render and finds no image on the clipboard.
- [ ] Add `AreaRender.Stitch*`, `AppTests.SnapshotSpan` and `Cli.SnapshotStitch`. Done when: `recto-tests.exe --gtest_filter=AreaRender.Stitch*:AppTests.SnapshotSpan:Cli.SnapshotStitch` passes (engine under ASan).
- [ ] Commit: `feat(text): add scrolling capture that stitches an area across pages`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AreaRender.Stitch*:AppTests.SnapshotSpan:Cli.SnapshotStitch` passes, and rendering each strip at a different DPI makes `AreaRender.StitchWidths` fail.

## 14. Offline spell check service over `ISpellChecker` and platform text boxes

Add one spelling service for every place Recto lets the user type text into a document: Edit PDF text (`D03 T04 §17`), comments and free text (`D03 T03 §4`) and form fields (`D03 T03 §8`). This matches Acrobat Pro's `Check Spelling` and the spell check in Foxit PDF Editor, PDFelement and UPDF. It must not break offline-by-default (D16): the service calls only the Windows spelling API, which uses dictionaries installed on the machine, and Recto makes no network call and adds no dictionary download. It keeps Recto's rule that spelling never alters text on its own: the service reports ranges and suggestions and the user decides.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `SpellService` runs `ISpellChecker` calls on a worker, returns plain value types, holds no pointer into `PageText` or `recto::pdf::Document`, and is never called from the UI thread for more than one word.

**Build order:**

1. Implement `ISpellEngine` (`check(text, language)`, `suggest(word, language)`, `add_word`, `ignore_word`, `supported(language)`) and `WindowsSpellEngine` in `src/services/spell_service.h/.cpp` over `ISpellCheckerFactory` (`IsSupported`, `CreateSpellChecker`), `ISpellChecker::Check`, `Suggest`, `Add` and `Ignore`, with a `FakeSpellEngine` for tests. Done when: `Spell.EngineFake` drives check, suggest, add and ignore through the fake and `Spell.EngineWindows` flags `recieve` in an English sentence on the Windows host.
2. Add `SpellService` on top: per-language checker cache, a document-local ignore list, skip rules, fed into the `ui::SpellService` checkers and the `TextBox` spell checking of `D10 T02 §24`, which draws the squiggles and the suggestion `ui::Menu`. Done when: `Spell.SkipRules` skips URLs, e-mail addresses, all-digit tokens and words with digits, and `Spell.PlatformControls` finds spell checking (`D10 T02 §24`) turned on for every `ui::TextBox` the app builds for comments, free text and form fields.

- [ ] Define `SpellIssue` (`start`, `length`, `word`, `suggestions` up to `8`, `action` among `Suggest`, `Delete`, `Replace`) and `SpellLanguage` (BCP 47 tag, `supported`) in `src/services/spell_service.h`. Done when: the header compiles in `recto-services` with no `src/ui` include.
- [ ] Pick the language per text span from the nearest `/Lang` (marked content, then structure element, then the document catalog), then the `SpellingLanguage` setting, then the Windows display language, and return a typed `SpellError::NoDictionary(tag)` with the plain message `No spelling dictionary for <tag>. Add the language in Windows Settings.` when `IsSupported` is false. Done when: `Spell.LanguagePick` selects by span, by document, by setting and by fallback, and reports the missing dictionary without throwing.
- [ ] Implement `Add to dictionary` through `ISpellChecker::Add` (the Windows user dictionary), `Ignore once` as a range in the caller, and `Ignore all` through `ISpellChecker::Ignore` for the session. Done when: `Spell.AddIgnore` adds a word and sees it accepted, and an ignored word is flagged again in a new service instance.
- [ ] Skip URLs, e-mail addresses, file paths, words containing digits, and all-capital tokens of up to `5` letters, and bound the input at `20000` characters per call by splitting at sentence ends. Done when: `Spell.SkipRules` and `Spell.Bounds` pass, and a `5` million character string returns after the cap without allocating a second copy.
- [ ] Turn on `TextBox::set_spell_check` (`D10 T02 §24`: toolkit-drawn squiggles and suggestion `ui::Menu`) for every `ui::TextBox` (`D10 T02 §4`) that edits comments, free text and form-field values, feed this section's document ignore list and skip rules into its checker, enable it from the `Check spelling while typing` setting and set the field language tag from the span language. Done when: `Spell.PlatformControls` walks those controls and finds spell checking on with the language set, and turning the setting off turns it off. Cheaper substitute: an app-side adorner that duplicates the toolkit's squiggle drawing.
- [ ] Add `Check spelling while typing` (`CheckSpellingWhileTyping`, default `true`) and `Spelling language` (`SpellingLanguage`, default `Automatic`, choices from the supported list) to Settings (`D02 T04 §5`), searchable in `Find a setting`. Done when: `Spell.SettingsRows` changes each and sees `SpellService` read it.
- [ ] Confirm no network use: no `HttpClient`, `WinHttp` or `WebSocket` symbol appears in `spell_service.cpp`, and the service starts with the network disabled. Done when: `Spell.Offline` passes.
- [ ] Add `Spell.*` tests in `tests/recto-tests/spell_service_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Spell.*` passes on the Windows host.
- [ ] Commit: `feat(text): add the offline spell check service over ISpellChecker`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Spell.*` passes on the Windows host, and removing the `/Lang` lookup makes `Spell.LanguagePick` fail.

## 15. Zone OCR engine: recognize a drawn area and `recto-cli ocr --zone`

Add recognition of a user-chosen area. The whole-page OCR of §8 segments a page automatically and has no way to read one figure caption, one table or one block of a mixed page, and it skips a digital page that carries an image of text. This matches PDFelement's area OCR and UPDF's and Foxit PDF Editor's selective recognition, and adds what Acrobat Pro's whole-page `Recognize Text` lacks. It must not break the thread contract (the zone is rasterized and recognized on `RenderPool` workers from immutable snapshots and never mutates a `PageText`) and it keeps Recto's offline in-process engines.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `recognize_zone` rasterizes through §11, runs `preprocess_for_ocr` and `NeuralOcrRecognizer` / `OcrEngine` on `RenderPool` workers with a `CancelToken`, and returns a new `std::shared_ptr<const PageText>` without mutating any cached snapshot.

**Build order:**

1. Implement `recognize_zone(DocumentActor&, ZoneRequest, CancelToken)` in `src/core/ocr_pipeline.cpp`: render only the zone at `300` DPI, run §7's deskew, binarization and (for `Table`) ruling-line removal and cell segmentation restricted to the zone, recognize with the selected engine and language, align characters with §8's CTC alignment, and map every quad back to page points. Done when: `OcrZone.TextZone` recognizes a paragraph of `scanned.pdf` inside a zone with `> 99%` character accuracy and no word outside the zone.
2. Add `recto-cli ocr` zone flags. Done when: `Cli.OcrZone` prints the zone text and refuses an empty zone.

- [ ] Define `ZoneRequest` (`page_index`, `rect_pt` in user space, `kind` among `Auto`, `Text`, `Table`, `SingleLine`, `engine`, `language`) and `ZoneOcrResult` (`std::shared_ptr<const PageText> text`, `words`, `mean_confidence`) in `src/core/ocr_pipeline.h`. Done when: the header compiles in `recto-core`.
- [ ] Run on any page, including a digital page whose zone covers an image of text, without consulting `classify_page_for_ocr`, and leave the page's existing `PageText` untouched. Done when: `OcrZone.OnDigitalPage` recognizes a raster caption on a page that already has `500` text characters and leaves `get_page_text` unchanged.
- [ ] Implement `Table` kind: detect ruling lines and cells inside the zone and return `StructNode` table cells with row and column indices. Done when: `OcrZone.TableZone` returns a `3 x 3` grid with the cell texts in row order.
- [ ] Implement `SingleLine` kind: skip layout segmentation and recognize the zone as one line strip. Done when: `OcrZone.SingleLine` reads a one-line zone and returns one `Line`.
- [ ] Reject an empty, non-finite or out-of-page zone and a zone larger than the page with a typed `ZoneError`, and bound the work at one page of pixels at `300` DPI. Done when: `OcrZone.Bounds` returns each error.
- [ ] Extend `recto-cli ocr <file.pdf> --page <N> --zone <x,y,w,h> [--zone-kind auto|text|table|line] [--engine hybrid|neural|windows] [--lang <tag>] --text` and `--zones <zones.json>` for several zones, printing `TSV` for `table` and plain text otherwise. Done when: `Cli.OcrZone` verifies text, table and multi-zone output and the exit codes.
- [ ] Add `OcrZone.*` and `Cli.OcrZone` tests in `tests/recto-tests/ocr_zone_tests.cpp` on `tests/corpus/text/scanned.pdf` and a new `text/ocr-zones.pdf` fixture (a digital page with a raster caption and a bordered scanned table) added to `scripts/build-corpus.py` with its `PROVENANCE.md` row. Done when: `python scripts/build-corpus.py --check` and `recto-tests.exe --gtest_filter=OcrZone.*:Cli.OcrZone` pass under ASan.
- [ ] Commit: `feat(ocr): recognize a user-drawn zone and add recto-cli ocr --zone`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrZone.*:Cli.OcrZone` passes under ASan, and running whole-page OCR then cropping makes `OcrZone.OnDigitalPage` fail.

## 16. OCR word confidence, suspect list and undoable in-memory corrections

Give OCR results the data a reviewer needs: a confidence for each recognized word, a list of suspect words with the reason each is suspect, and an undoable store of corrections applied to the in-memory text. This matches Foxit PDF Editor's OCR suspects review and has no Acrobat equivalent (Acrobat's `Recognize Text` marks suspects only in a separate dialog). It must not break the immutability rule (a correction publishes a new `std::shared_ptr<const PageText>` through an actor message and never edits a snapshot in place) and it keeps Recto's honesty about engines: `Windows.Media.Ocr` exposes no confidence, so a Windows-only page reports none and is reviewed by the other reasons, never by a made-up number.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `OcrPageResult` and every `OcrTextEdit` application produce new immutable snapshots, that the actor swaps them by message, and that no reviewer or UI code writes into a published `PageText`.

**Build order:**

1. Extend `OcrPipeline` (§8) to publish `OcrPageResult` (the `PageText` snapshot plus `OcrWordInfo` per word: `char_start`, `char_count`, `quad`, `confidence` as `std::optional<float>`, `engine`, `alternate_text`), taking confidence from the CTC non-blank posteriors and `alternate_text` from the other engine in `HybridAuto` when the two readings differ. Done when: `OcrReview.WordInfo` shows a confidence in `0..1` for every neural word and an empty confidence for Windows-only words.
2. Implement `find_suspects` and the `OcrTextEdits` store. Done when: `OcrReview.Suspects` flags a planted `l0ve` and a low-confidence word, and `OcrReview.EditsUndoRedo` applies, undoes and redoes a replacement with `get_page_text` following each step.

- [ ] Implement `find_suspects(const OcrPageResult&, SuspectOptions, SpellService*)` in `src/core/ocr_review.h/.cpp` returning `Suspect` (`page_index`, `word_id`, `snapshot_version`, `reasons`) ordered by page and reading order, with reasons `LowConfidence` (below the `OcrSuspectThreshold` setting), `EnginesDisagree`, `NotInDictionary` (only when `SpellService` (§14) has a dictionary for the language) and `ImplausibleMix` (digits inside letters, or two scripts in one word). Done when: `OcrReview.Suspects` returns each reason on its own fixture word and none on clean text.
- [ ] Define `OcrTextEdit` (`ReplaceWord`, `DeleteWord`, `AddWords` for zone text from §15) and `OcrTextEdits` with `apply`, `undo`, `redo`, a journal bounded at `10000` entries, and a replacement that keeps the word's quad and re-splits character quads proportionally across it. Done when: `OcrReview.EditsUndoRedo` and `OcrReview.QuadResplit` pass.
- [ ] Publish each applied edit as a new `PageText` snapshot to `DocumentActor` by actor message, invalidating that page's LRU entry (`§2`) so selection, `Ctrl+F` and UIA see the corrected word at once. Done when: `OcrReview.ActorPublish` searches for the corrected word and finds it after the edit and not after the undo.
- [ ] Give every `OcrWordInfo` a never-reused 64-bit `word_id` assigned when the page result is first published, keep ids across edits that do not replace the word, and stamp every published snapshot with a `snapshot_version`; every action (`ReplaceWord`, `DeleteWord`, accept) names `(word_id, snapshot_version)` and is rejected with `StaleSuspect` when the version differs or the id is gone, and a suspect list computed asynchronously against an older version is discarded. Done when: `OcrReview.StaleActions` computes suspects, deletes a preceding word, inserts a zone word, undoes, and finds each stale action rejected and a fresh action applied to the right word.
- [ ] Mark corrected and accepted words `reviewed` in `OcrTextEdits` so the suspect list can hide them, without changing confidence values. Done when: `OcrReview.ReviewedState` hides an accepted suspect and shows it again after undo.
- [ ] Add `OCR suspect threshold` (`OcrSuspectThreshold`, `0.50` to `0.99`, default `0.85`) to Settings (`D02 T04 §5`), searchable in `Find a setting`. Done when: `OcrReview.ThresholdSetting` changes it and the suspect count changes.
- [ ] Bound the work: at most `100000` suspects per document, `CancelToken` between pages, and a correction text of at most `200` characters. Done when: `OcrReview.Bounds` returns the typed error for an oversize correction and stops a hostile `500000`-word page inside the cap.
- [ ] Add `OcrReview.*` tests in `tests/recto-tests/ocr_review_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OcrReview.*` passes under ASan.
- [ ] Commit: `feat(ocr): add word confidence, suspect detection and undoable corrections`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrReview.*` passes under ASan, and reporting a fixed confidence for Windows-only words makes `OcrReview.WordInfo` fail.

## 17. Zone OCR tool: draw an area, review the text, copy or add it to the text layer

Build the `Recognize Text in Area` tool on §15 and the shared `AreaPickerLayer` of §12: the user drags a zone, chooses the kind and language, reads the recognized text in a card, and copies it, copies a table as tab-separated text, or adds it to the page's text layer so it becomes selectable and searchable. This matches PDFelement's area OCR and the zone recognition of UPDF and Foxit PDF Editor. It must not break the shell rules (`Esc` cancels, `Ctrl+K` reachable, nothing blocks the UI thread, no PDF write until the user saves through §19) and it keeps Recto's offline recognition.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that the tool starts `recognize_zone` on a worker, marshals the result back with `ui::Dispatcher::post`, and adds words only through `OcrTextEdits` (§16).

**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.2 (contextual cards) and section 9.7 (OCR engine and language rows), and reuses the §12 area outline and popover chrome. Design board: `design/SPEC.md` section 9.3.2 with `design/screenshots/D-SearchPanels.png` (drawn by `D06 T04 §9`), which draws the zone outline and the result card.
**Job:** The user drags over a scanned table on a mixed page, chooses `Table`, reads the recognized grid, presses `Copy as table`, and pastes it into a spreadsheet; or presses `Add to text layer` so the zone's words can be selected and found with `Ctrl+F`. Consumer: the clipboard, `OcrTextEdits` (§16) and the save path (§19).
**Treatment:** The `AreaPickerLayer` of §12 followed by a result card with a kind selector (`Auto`, `Text`, `Table`, `Single line`), a language combo, an editable multi-line text box with the recognized text, the mean confidence as text, and `Copy text`, `Copy as table`, `Add to text layer` and `Cancel` buttons. Cheaper substitute that fails the checkpoint: running OCR on the whole page and cropping the text, or a card that cannot be edited before copying.
**Chrome:** consume `RectoSurfaceBrush`, `RectoBorderBrush`, `RectoAccentBrush` for the zone outline, `RectoWarningBrush` for low-confidence words (paired with a `!` glyph and the confidence text), Fluent UI System Icons (`RectoIcons`: `scan_text`); no hex values.

**Build order:**

1. Register `Recognize Text in Area` (`ocr.zone`, no default accelerator) and add the result card with a progress state and a `Cancel` that aborts the `CancelToken`. Done when: `AppTests.OcrZoneTool` draws a zone on `scanned.pdf`, sees recognized text in the card, and cancels a second run with no result applied.
2. Wire `Copy text`, `Copy as table` (tab-separated, rows by newline) and `Add to text layer` to the clipboard and to `OcrTextEdits::AddWords`. Done when: after `Add to text layer`, `Ctrl+F` finds a zone word at its image coordinates and `Undo` removes it.

- [ ] Reuse `AreaPickerLayer` (§12) with the OCR cursor and the numeric `Precise area` expander for keyboard-only use. Done when: `AppTests.OcrZoneTool` creates the same zone by keyboard only.
- [ ] Show low-confidence words in the card with a `!` marker and the confidence value, never colour alone. Done when: `A11y.OcrZoneCard` finds the marker and the text for each word under the threshold.
- [ ] Offer the engine and language from `D02 T04 §5` as defaults and let the card override the language for this zone only, without changing the setting. Done when: `AppTests.OcrZoneTool` recognizes a French zone with the override and leaves `OcrLanguage` unchanged.
- [ ] Mark the page `text layer edited` in the tab (a dirty indicator on the tab, not a file write) after `Add to text layer`, and warn on close that the added text is saved only through `Save corrected copy` (§19). Done when: `AppTests.OcrZoneDirty` closes the tab and finds the warning.
- [ ] Give the card controls and the zone handles automation names and tooltips, and announce `Recognized <n> words` through `AnnouncementService`. Done when: the inventory (`D03 T02 §3`) finds none missing and `A11y.OcrZoneCard` records the announcement.
- [ ] Add `AppTests.OcrZoneTool`, `AppTests.OcrZoneDirty` and `A11y.OcrZoneCard`. Done when: `recto-tests.exe --gtest_filter=AppTests.OcrZone*:A11y.OcrZoneCard` passes on the display-session host.
- [ ] Commit: `feat(app): add the Recognize Text in Area tool with copy and add-to-layer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.OcrZone*:A11y.OcrZoneCard` passes on the display-session host, and recognizing the whole page then cropping makes `AppTests.OcrZoneTool` fail on the no-text-outside-zone assertion.

## 18. OCR review panel: word crop beside recognized text, accept or correct

Build the review surface for §16: a side panel that lists suspect words, each with an image crop of the scanned word beside the recognized text, so the user accepts it or types the correct text. This matches Foxit PDF Editor's OCR suspects review and has no Acrobat equivalent. It must not break the panel contract (`D02 T03 §3`), the thread rule (crops are rendered by §11 on workers) or the immutable text rule (a correction goes through `OcrTextEdits`), and it keeps Recto's rule that no correction is applied without the user's action.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that the panel reads `Suspect` values and crops only from worker-built results, applies corrections only through `OcrTextEdits` on the actor, and never touches `recto::pdf::Document`.

**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.3 (side-panel cards through `SidePanelHost`, `D02 T03 §3`), with the Reading room inspector tab variant of `D-Search.png` and `explorations/B-Search.png`. Design board: `design/SPEC.md` section 9.3.2 with `design/screenshots/D-SearchPanels.png` (drawn by `D06 T04 §9`).
**Job:** After OCR the user opens `OCR review`, sees `12 of 48 reviewed`, steps through suspects with the crop and the recognized text side by side, presses `Enter` to correct or `Accept` to keep, and watches the page highlight move. Consumer: `OcrTextEdits` (§16) and `DocumentView`.
**Treatment:** A virtualized list in `SidePanelHost` (Focus and Classic) or an inspector tab (Reading room); each row holds a `160 x 48 epx` crop rendered at `300` DPI with `8 px` padding, an editable text box with the recognized word, reason chips with text (`Low confidence 62%`, `Not in dictionary`), and `Accept`, `Skip` and `Correct` actions; the canvas marks the active word with a `RectoWarningBrush` box and a marker glyph. Cheaper substitute that fails the checkpoint: a text-only list with no image crop, or highlighting the word on the canvas only.
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), `RectoSurfaceBrush`, `RectoWarningBrush`, `RectoAccentBrush`, `RectoTextBrush`, Fluent UI System Icons (`RectoIcons`: `checkmark`, `chevron_down`); no hex values.

**Build order:**

1. Add `OcrReviewViewModel` (`src/services/ocr_review_view_model.h/.cpp`) and the virtualized panel with crops rendered on workers via `render_page_area` and cached by `(word_id, snapshot_version)`. Done when: `AppTests.OcrReviewPanel` lists suspects of `scanned.pdf` with a crop for each and the counter `0 of N reviewed`.
2. Wire `Accept`, `Correct`, `Skip`, `Next` and `Previous`, the canvas highlight, filters by reason, and the `Ctrl+K` command `OCR review`. Done when: `AppTests.OcrReviewCorrect` corrects three words, sees the page text update, undoes one with `Ctrl+Z`, and the counter reads `2 of N reviewed`.

- [ ] Register `OCR review` (`ocr.review`, no default accelerator) and the actions `Accept suspect`, `Correct suspect`, `Next suspect` (`Alt+Down`) and `Previous suspect` (`Alt+Up`) in `CommandRegistry` (`D02 T03 §11`), active only while the panel has focus. Done when: the command box finds all four and `Keyboard.Audit` finds no clash.
- [ ] Show the active suspect on the canvas with a box and a marker glyph, scroll it into view without changing zoom, and keep the highlight after a preset switch. Done when: `AppTests.OcrReviewPanel` verifies the box quad equals the word quad.
- [ ] Filter by reason (`All`, `Low confidence`, `Engines disagree`, `Not in dictionary`, `Mixed`) and show `No suspects` when the page set is clean. Done when: `AppTests.OcrReviewFilters` shows the counts per reason.
- [ ] Offer `Accept all above <n>%` as a confirmed bulk action that marks words reviewed without changing text, with an undo. Done when: `AppTests.OcrReviewBulk` accepts a batch and undoes it as one step.
- [ ] Keep the panel responsive: crop rendering and `find_suspects` run off the UI thread, and a `200`-page OCR result with `5000` suspects scrolls at under `16.7 ms` per frame. Done when: `AppTests.OcrReviewPerf` records the frame times.
- [ ] Give every row control an automation name and tooltip, announce `Corrected to <text>` and `Accepted` through `AnnouncementService`, and make the list operable by keyboard alone. Done when: `A11y.OcrReviewPanel` finds all names and a keyboard-only run corrects and accepts a word.
- [ ] Add `AppTests.OcrReview*` and `A11y.OcrReviewPanel`. Done when: `recto-tests.exe --gtest_filter=AppTests.OcrReview*:A11y.OcrReviewPanel` passes on the display-session host.
- [ ] Commit: `feat(app): add the OCR review panel with word crops and corrections`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.OcrReview*:A11y.OcrReviewPanel` passes on the display-session host, and applying a correction without an explicit action makes `AppTests.OcrReviewCorrect` fail.

## 19. Save OCR corrections and zone text into the text layer of a copy

Make the corrections of §16 and the zone text of §17 permanent: write them into the invisible text layer of a copy of the PDF, never the open document, so the saved file selects, searches and extracts the corrected text. This matches Foxit PDF Editor's save of corrected OCR text and Acrobat Pro's rewritten `Recognize Text` layer. It must not break the write rules (temp-file-then-replace, the open viewer document is never modified in place, authoring works on an isolated `recto::pdf::Document`) and it keeps Recto's true removal: the old wrong invisible text is removed from the file by a full rewrite, proved by decoded extraction, not hidden under new text, and the visible pixels are unchanged.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that the writer opens its own `recto::pdf::Document` on a job thread from the immutable `std::shared_ptr<const std::vector<uint8_t>>` byte snapshot the viewer's document was opened from (never by reopening the path), takes only that snapshot and the immutable `OcrTextEdits`, `OcrPageResult` and `OcrLayerMap` values, and leaves the viewer's `DocumentActor` and snapshots untouched on success, failure or cancellation.

**Fidelity:** Genuinely new surface. It adds a `Save corrected copy` button to the §18 panel footer and the §17 card, using the `Save a copy` dialog of `D02 T01 §8` and the SPEC 9.9 dialog style. Design board: `design/SPEC.md` section 9.3.2 with `design/screenshots/D-SearchPanels.png` (drawn by `D06 T04 §9`), which draws the button in both places and the changed-on-disk notice.
**Job:** The user finishes reviewing, presses `Save corrected copy`, chooses a file, and opens the result in any reader to find the corrected words selectable and searchable while the page looks the same. Consumer: any PDF reader and `recto-cli ocr correct`.
**Treatment:** An isolated writer that, for each edited page, replaces only the invisible-text runs (`3 Tr`) the `OcrLayerMap` recorded for the edited words when the OCR result was first published (the original word's text and quad matched exactly one run in the snapshot), writes corrected words in place of those runs and added zone words as new runs with the `searchable_pdf` word writer (`D04 T02 §7`), leaves every other invisible run untouched, and saves with the full-rewrite path. Cheaper substitute that fails the checkpoint: appending a second invisible layer over the old one, which leaves the wrong text extractable, or deleting every `3 Tr` run on the page, which destroys unrelated invisible text.
**Chrome:** consume `RectoDialogStyle`, the §18 panel footer and `D02 T01 §8`'s save dialogs; no hex values.

**Build order:**

1. Implement `apply_ocr_text_edits_to_copy(snapshot, layer_map, edits, out, CancelToken)` in `src/core/ocr_text_writer.cpp`: open an isolated document from the byte snapshot, address each edited word's runs through `layer_map`, replace them, insert added words, and save by full rewrite through `PdfWriter` (`D04 T02 §2`) and `AtomicFileWriter`. Done when: `OcrWrite.CorrectedLayer` saves a corrected `scanned.pdf`, and decoded text extraction with OCR disabled (content streams decompressed, `/ToUnicode` and font encodings applied) returns the corrected word and not the old one, exactly once, on fixtures with compressed streams and a custom-encoded font.
2. Add `Save corrected copy` to the §18 panel and §17 card and `recto-cli ocr correct <in.pdf> --corrections <file.json> --out <out.pdf>`. Done when: `AppTests.OcrSaveCopy` saves from the panel and `Cli.OcrCorrect` applies a JSON edit list.

- [ ] Record run provenance once, against the snapshot, when the tab's first OCR result for a page is published: build an immutable `OcrLayerMap` from each original `OcrWordInfo` id to the content stream and operator range of the single `3 Tr` run whose decoded text and quad (within `0.5 pt`) match that original word; a word matching no run is `unmapped`, and a word matching more than one run, or a run matching more than one word, is `ambiguous`. Edits address words by id, never by re-matching their new text: `replace` rewrites the mapped range, `delete` removes it, and `add` (a new zone word) inserts a new run. A page whose edited words are all `unmapped` (OCR was in memory only) gets a new layer from the `searchable_pdf` word writer; a page where an edited word is `ambiguous`, or where mapped and unmapped edited words mix, refuses the save with `AmbiguousTextLayer` naming the page and writes no output. Unrelated invisible runs (another producer's layer, a digital page's hidden text) are never touched. Done when: `OcrWrite.LayerDetection` replaces mapped runs on one page and creates a layer on another; `OcrWrite.UnrelatedInvisibleText` keeps a foreign invisible run and a digital page's text byte-identical in their ranges; `OcrWrite.AmbiguousLayer` refuses a page where a foreign run duplicates an OCR word's text and quad; and `OcrWrite.ReplaceAndDeleteByProvenance` replaces one word with different text and deletes another, and decoded extraction finds the new text once and neither old word.
- [ ] Bind the edits to the exact bytes they were made against: the OCR pipeline, the `OcrLayerMap` and the writer all read the one immutable byte snapshot the tab's `DocumentActor` opened (`Document::file_bytes()`, shared as `std::shared_ptr<const std::vector<uint8_t>>`), with its SHA-256 recorded beside the edits; the writer never reopens the path, so replacing the file on disk at any time cannot change what is written. When the file on disk no longer matches the snapshot's hash, the save still writes the copy from the snapshot and says `The file changed on disk; this copy is based on the version you reviewed`. Done when: `OcrWrite.SnapshotIdentity` replaces the input on disk before the first OCR publication, between review and save, and while the writer runs, and in each case the output's unedited bytes derive from the snapshot (its pages render identical to the snapshot, not to the replacement) and the notice is shown.
- [ ] Prove the visible page is unchanged: render every edited page before and after and compare. Done when: `OcrWrite.VisualIdentity` finds `0` differing pixels (a check of the page look only; text preservation is covered by `OcrWrite.UnrelatedInvisibleText`).
- [ ] Run the save-integrity verification of `D06 T03 §5` (page count, outlines, `/PageLabels`, `/Info`, `/AcroForm`, signatures noted) and warn before rewriting a signed document. Done when: a planted writer that drops `/PageLabels` aborts the save and a signed fixture returns `InvalidatesSignature` until confirmed.
- [ ] Define the `corrections.json` schema (`page`, `bbox_pt`, `old_text`, `new_text`, `kind` among `replace`, `delete`, `add`) with a version field, and reject an edit whose `old_text` does not match the word at the box. Done when: `Cli.OcrCorrect` applies a valid file, refuses a mismatched one with a non-zero exit code and writes no output.
- [ ] Mark the tab clean of `text layer edited` only after a verified save of the copy, and keep the edits if the save fails. Done when: `AppTests.OcrSaveCopy` forces a failure and finds the edits and the indicator intact.
- [ ] Add `OcrWrite.*`, `AppTests.OcrSaveCopy` and `Cli.OcrCorrect`. Done when: `recto-tests.exe --gtest_filter=OcrWrite.*:AppTests.OcrSaveCopy:Cli.OcrCorrect` passes (engine under ASan).
- [ ] Commit: `feat(ocr): save OCR corrections and zone text into the text layer of a copy`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrWrite.*:AppTests.OcrSaveCopy:Cli.OcrCorrect` passes, appending a second layer instead of replacing the first makes `OcrWrite.CorrectedLayer` fail on the decoded-extraction duplicate check, and reopening the path in the writer instead of the snapshot fails `OcrWrite.SnapshotIdentity`.

## 20. `NeuralOcrRecognizer`: model loader, SIMD kernels and CTC decoding

> **Groomed 2026-10-23:** split out of `§8`, which bundled the recognizer with Windows OCR, fusion, alignment, actor wiring and the CLI. Default taken: the recognizer runs on the CPU with AVX2 on x64 and NEON on ARM64 plus a scalar reference; DirectML is not used in v1 (a GPU path would make results differ between machines, and reversing it costs one extra backend behind `NeuralBackend`).

Implement `src/core/neural_ocr.h/.cpp`: a typed `.rocr` model format, a loader that refuses hostile or damaged files, inference kernels (convolution, bidirectional recurrent layers, linear, softmax) and a CTC decoder that returns the best-path text plus the per-timestep posteriors that `§22` aligns to ink. It reads normalized line strips cut from `§7`'s `OcrLayoutRegion`s and never touches a `Document`. The weights themselves come from `§21`; this section proves the engine with a tiny deterministic test model.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `NeuralOcrRecognizer` is immutable after `load`, shares its weights across worker threads read-only, keeps all activation buffers worker-local, and polls `CancelToken` between layers and every 64 timesteps without locking `DocumentActor`.

**Build order:**

1. Define the `.rocr` format (magic, version, layer table, dtype `int8` or `fp16`, alphabet, SHA-256 of the payload) and `load_neural_model(span<const std::byte>)` returning `OcrResult<NeuralModel>`. Done when: `NeuralOcr.ModelRoundTrip` loads a generated model and `NeuralOcr.ModelRefusesDamage` refuses truncated, oversized and hash-mismatched files with typed errors.
2. Implement the scalar reference kernels and the AVX2 and NEON kernels behind one `NeuralBackend` interface. Done when: `NeuralOcr.KernelParity` shows every kernel within `1e-4` of the scalar reference on random inputs.
3. Implement strip normalization and greedy CTC decoding with posterior output. Done when: `NeuralOcr.DecodeTinyModel` reproduces the golden text and posteriors of the committed test model.

- [ ] Define `NeuralModel`, `NeuralLayer` and `OcrResult<T>` in `src/core/neural_ocr.h` and write the `.rocr` reader in `src/core/neural_ocr.cpp`, bounding layer count (`<= 64`), tensor elements (`<= 64 MiB` in total), alphabet size (`<= 512`) and every offset against the buffer length. Done when: `NeuralOcr.ModelRefusesDamage` feeds truncated, oversized-tensor, bad-offset and hash-mismatch inputs and gets a typed error each time under ASan. Cheaper substitute: trusting the header sizes. Source: foundation plan section 4 (hostile input bounds).
- [ ] Implement the scalar kernels `conv2d_3x3`, `bidirectional_gru`, `linear` and `log_softmax` in `src/core/neural_kernels_scalar.cpp`. Done when: `NeuralOcr.ScalarKernels` matches hand-computed outputs for a `3 x 3` convolution and a two-step GRU.
- [ ] Implement the AVX2 kernels in `src/core/neural_kernels_avx2.cpp` (x64 only, selected at runtime by `__cpuid`) and the NEON kernels in `src/core/neural_kernels_neon.cpp` (ARM64 only). Done when: `NeuralOcr.KernelParity` passes on x64 and the ARM64 build compiles with zero warnings.
- [ ] Implement `normalize_strip(const GrayBitmap&, int target_height)` scaling a line crop to height `32` with the aspect ratio kept and the stride respected. Done when: `NeuralOcr.StripNormalize` passes on a strip whose stride is wider than its width times one. Cheaper substitute: assuming stride equals width.
- [ ] Implement `ctc_greedy_decode` returning text, per-character confidence and the per-timestep posterior matrix. Done when: `NeuralOcr.CtcDecode` collapses repeats and blanks correctly on a hand-made posterior matrix.
- [ ] Cap work: refuse a strip wider than `8192` px, allocate activations once per worker, and poll `CancelToken` as the freeze check states. Done when: `NeuralOcr.Cancellation` aborts a long strip within `10 ms` and `NeuralOcr.OversizedStrip` returns a typed error.
- [ ] Add `tests/recto-tests/neural_ocr_tests.cpp` with a deterministic `4 KiB` test model generated by `scripts/build-corpus.py` as `tests/corpus/text/models/tiny.rocr`. Done when: `recto-tests.exe --gtest_filter=NeuralOcr.*` passes under ASan.
- [ ] Commit: `ocr: add NeuralOcrRecognizer model loader, SIMD kernels and CTC decoding`

**Test checkpoint:** `recto-tests.exe --gtest_filter=NeuralOcr.*` passes under ASan, and swapping the AVX2 `linear` kernel for one with an off-by-one tail makes `NeuralOcr.KernelParity` fail.

## 21. Neural OCR weights: synthetic line renderer, trainer, export and accuracy gate

> **Groomed 2026-10-23:** new. The plan claims `> 99%` character accuracy from an in-process neural recognizer, but no section produced the trained weights. Defaults taken: Latin-script weights only (other scripts use `Windows.Media.Ocr` in `HybridAuto`; adding a script means rerunning this section with another alphabet); training data is synthetic text drawn with the fonts installed on every Windows 11 host, so nothing is downloaded and no third-party dataset licence applies; the trainer is a development-time Python tool outside the shipped product. Reversal cost: replacing the data source means editing `tools/ocr-train/synth` only.

Build the pipeline that produces `models/recto-ocr-latin-v1.rocr`: a C++ line renderer that writes labelled training shards, a pinned PyTorch trainer that fits a small CRNN with CTC loss on the CPU, an exporter to the `§20` format, and an accuracy gate measured on text the trainer never saw. The shipped product contains only the `.rocr` file; the trainer and its Python packages never ship. The file is copied next to `Recto.exe`, `recto-cli.exe` and `recto-tests.exe` by their projects (packaging the `models` folder into the installer is owned by `D07 T01`).

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that the weights file is read-only data loaded once by `§20`, that nothing in the training tools is linked into `recto-core`, and that no code path downloads a model or contacts a network.

**Build order:**

1. Build `tools/ocr-train/synth/synth.vcxproj` (`recto-ocr-synth.exe`): deterministic line renderer using DirectWrite and WIC, with seeded augmentation. Done when: two runs with seed `1` produce shard files with identical SHA-256.
2. Write `tools/ocr-train/train.py` (pinned in `tools/ocr-train/requirements.txt`, run in a virtual environment under `build/ocr-venv`) and `tools/ocr-train/export_rocr.py`. Done when: a one-epoch smoke run writes a loadable `.rocr`.
3. Train, commit `models/recto-ocr-latin-v1.rocr` with its provenance, and add the accuracy gate. Done when: `NeuralOcrAccuracy.*` passes.

- [ ] Implement `recto-ocr-synth.exe --count <n> --seed <s> --out <dir>` drawing lines from a public-domain word list in `tools/ocr-train/wordlist.txt` with installed fonts (sizes `6` to `24` pt, regular, bold, italic), then applying seeded blur, noise, skew within `3` degrees, contrast loss and JPEG artefacts, and writing image strips plus label files. Done when: `NeuralOcrSynth.Deterministic` regenerates `1000` lines twice with identical hashes. Cheaper substitute: using the corpus fixtures as training data.
- [ ] Write `tools/ocr-train/train.py` defining the CRNN (three convolution blocks, a bidirectional GRU, a linear layer over the `§20` alphabet), CTC loss, a fixed seed and a CPU-only run. Done when: a `--epochs 1 --count 2000` smoke run finishes and prints a falling loss.
- [ ] Write `tools/ocr-train/export_rocr.py` quantizing to `int8`, writing the `§20` layout and the payload SHA-256. Done when: `NeuralOcr.ModelRoundTrip` loads the exported smoke file.
- [ ] Run the full training recipe recorded in `tools/ocr-train/RECIPE.md` (data count, epochs, seed, package versions) and commit the result as `models/recto-ocr-latin-v1.rocr` (size at most `8 MiB`) with a row in `tests/corpus/PROVENANCE.md` naming the recipe commit, the licence (`GPL-3.0-or-later`) and the SHA-256. Done when: `Corpus.Provenance` lists the file and passes.
- [ ] Measure accuracy on held-out synthetic lines (seed `999`, `5000` lines) with `NeuralOcrAccuracy.HeldOutSynthetic` and on `tests/corpus/text/scanned.pdf` through `recto-cli ocr --engine neural`. Done when: character accuracy is at least `99.0%` on both and each test prints the measured value; if training misses the bar the section stays open and the recipe is adjusted, the bar is never lowered. Cheaper substitute: measuring only on training data.
- [ ] Add `Ocr.WeightsPresent` asserting that `recto-core` finds the model next to the executable and that `OcrEngineMode::NeuralHighAccuracy` works from the `recto-cli` and `Recto.exe` output folders. Done when: the test passes in both and fails when the file is removed.
- [ ] Keep the venv out of the tree: add `build/ocr-venv/` handling to `tools/ocr-train/RECIPE.md` and confirm `build/` is ignored. Done when: `git status --short` after a training run lists no untracked files outside `models/` and `tools/ocr-train/`.
- [ ] Commit: `ocr: add synthetic training pipeline, Latin weights and the 99 percent accuracy gate`

**Test checkpoint:** `recto-tests.exe --gtest_filter=NeuralOcrSynth.*:NeuralOcrAccuracy.*:Ocr.WeightsPresent` passes, printing the measured held-out accuracy of at least `99.0%`, and a model file with one flipped weight byte makes `NeuralOcr.ModelRefusesDamage` fail the hash check.

## 22. Per-character CTC quad alignment into tight `CharQuad`s

> **Groomed 2026-10-23:** split out of `§8`.

Implement `align_ctc_character_quads` in `src/core/ocr_align.h/.cpp`: turn the CTC posteriors of `§20` (or, in `WindowsMediaOcr`-only mode, the word boxes of `Windows.Media.Ocr`) into one tight page-point quad per character by combining the horizontal frame boundaries of the non-blank emission peaks with vertical connected-component ink bounds, then mapping the four corners through `deskew_to_page_matrix` from `§7`.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `align_ctc_character_quads` is a pure function of immutable posteriors, an immutable binarized strip and the matrix, and holds no reference to `Document` or `DocumentActor`.

**Build order:**

1. Implement the frame-to-column mapping and ink-bound intersection. Done when: `OcrAlign.ProportionalWords` aligns `"iii"` and `"mmm"` quads within `1.0 pt` of the rendered ink.
2. Implement the projection-based fallback for word-box-only input and the corner transform. Done when: `OcrAlign.WordBoxFallback` and `OcrAlign.DeskewMapping` pass.

- [ ] Extract each character's start and end frames from the non-blank posterior peaks in `src/core/ocr_align.cpp`. Done when: `OcrAlign.FrameBounds` matches hand-marked frames on the tiny test model.
- [ ] Intersect each horizontal span with vertical connected-component ink bounds (`top`, `bottom`) of the binarized strip. Done when: `OcrAlign.InkBounds` gives distinct heights for `l` and `x`.
- [ ] Implement the proportional ink-projection sub-segmentation used when only word boxes exist. Done when: `OcrAlign.WordBoxFallback` places narrow glyphs (`i`, `l`, `.`) narrower than wide glyphs (`m`, `w`). Cheaper substitute that fails the checkpoint: dividing the word box into `width / N` slices.
- [ ] Map the four corners of every quad through `deskew_to_page_matrix` into page points and clamp to the page box. Done when: `OcrAlign.DeskewMapping` places quads of a `3.5` degree tilted scan within `0.5 pt` of the true glyph positions.
- [ ] Handle empty, single-character and degenerate-ink lines without dividing by zero or emitting NaN. Done when: `OcrAlign.Degenerate` passes under ASan with a blank strip and a one-pixel strip.
- [ ] Add `tests/recto-tests/ocr_align_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OcrAlign.*` passes under ASan.
- [ ] Commit: `ocr: add per-character CTC quad alignment`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrAlign.*` passes under ASan, and using uniform `width / N` slices makes `OcrAlign.ProportionalWords` fail.

## 23. Text fixtures for M3 in `scripts/build-corpus.py`

> **Groomed 2026-10-23:** new. `D00 T03 §3` built only `scanned.pdf` and `two-column-links-outline.pdf`. The tests of `§1`, `§5`, `§7`, `§9`, `§10` and `D06 T01 §3` name fixtures that nothing built. The file names below match the list in `D06 T01 §3` so its expectation tables attach to them.

Extend the deterministic generator `scripts/build-corpus.py` (stdlib only, fixed timestamps and `/ID` bytes) with the text fixtures M3 needs and register each in `tests/corpus/PROVENANCE.md`: `two-column-article.pdf`, `ligatures-and-hyphens.pdf`, `rotated-4way.pdf`, `cjk-rtl-bidi.pdf`, `tagged-structure.pdf`, `scanned-english.pdf`, `scanned-tilted.pdf`, `scanned-two-column.pdf`, `scanned-bordered-table.pdf`, `vector-outlined-text.pdf`, `flat-form.pdf`, `revision-a.pdf`, `revision-b.pdf`, `regex-cases.pdf` and `long-500.pdf`, plus the model `tiny.rocr` that `§20` tests load.

**Phase:** 3
**Needs:** none
**Surface:** no
**Freeze check:** Confirm that the generator only writes files under `tests/corpus/text/` and that re-running it produces byte-identical output.

**Build order:**

1. Add the born-digital fixtures (`two-column-article`, `ligatures-and-hyphens`, `rotated-4way`, `cjk-rtl-bidi`, `tagged-structure`, `regex-cases`, `revision-a`, `revision-b`, `long-500`). Done when: `python scripts/build-corpus.py --check` exits 0 and the files exist.
2. Add the raster and outline fixtures (`scanned-english`, `scanned-tilted`, `scanned-two-column`, `scanned-bordered-table`, `vector-outlined-text`, `flat-form`) and `models/tiny.rocr`. Done when: each is listed in `PROVENANCE.md` with its SHA-256.

- [ ] Generate `tagged-structure.pdf` with `/StructTreeRoot`, `/ParentTree`, `/H1`, `/Table` with `/TR`/`/TH`/`/TD`, `/L`/`/LI` and a `/Figure` with `/Alt`. Done when: `PdfText.TaggedFixture` reads every one of those node kinds from it.
- [ ] Generate `rotated-4way.pdf` (`0`, `90`, `180`, `270` degree text runs) and `ligatures-and-hyphens.pdf` (`fi`, `fl`, `ffi` glyphs through `/ToUnicode`, soft hyphen line breaks, a hyphenated word across a line). Done when: `tests/scripts/test_build_corpus.py` decodes the files with a stdlib parser and finds the described runs.
- [ ] Generate `cjk-rtl-bidi.pdf` with Latin, Arabic and Han runs and `/ToUnicode` maps, and `two-column-article.pdf` with a heading, two columns and a footnote. Done when: both validate through `scripts/build-corpus.py --check`.
- [ ] Generate `regex-cases.pdf` and `long-500.pdf` (`500` pages, one fixed word on every page, under `1 MiB`). Done when: `tests/scripts/test_build_corpus.py` asserts the page counts.
- [ ] Generate `revision-a.pdf` and `revision-b.pdf` differing by one inserted word, one deleted word, one replaced word and one moved vector figure. Done when: `PdfCompare.TextDiff` and `PdfCompare.GraphicDiff` find exactly those four changes.
- [ ] Generate `scanned-english.pdf`, `scanned-tilted.pdf` (`3.5` degree tilt), `scanned-two-column.pdf` and `scanned-bordered-table.pdf` from rendered bitmap text (the `render_bitmap_banner` helper), `vector-outlined-text.pdf` (the same text as filled Bezier paths with no text operators) and `flat-form.pdf` (labelled boxes and underlines, no `/AcroForm`). Done when: `OcrPreprocess.Classify` reports `ScannedRaster` for the four scans, `VectorOutlinedText` for the outlines and `DigitalText` for the flat form.
- [ ] Generate `models/tiny.rocr` (`4 KiB`, fixed weights, documented alphabet and a recorded golden decode of one hand-made strip). Done when: `NeuralOcr.DecodeTinyModel` reproduces the golden decode.
- [ ] Register every new file in `tests/corpus/PROVENANCE.md` with SHA-256 and `CC0-1.0`. Done when: `Corpus.Provenance` passes with the new files listed.
- [ ] Commit: `test: add M3 text fixtures to the deterministic corpus generator`

**Test checkpoint:** `python scripts/build-corpus.py --check` exits 0, `python -m unittest tests.scripts.test_build_corpus` passes, and `recto-tests.exe --gtest_filter=Corpus.Provenance` passes with the sixteen new files listed; deleting one file makes the provenance test fail.

## Verification

- M3 text and OCR obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `Ctrl+F` and `Ctrl+H` find and replace across multi-hundred-page PDFs without blocking scrolling, scanned and vector-outlined PDFs become selectable and searchable via built-in OCR, and `Compare PDFs` highlights every text and graphic change.
- `recto-tests` (`PdfText.*`, `ActorText.*`, `TextSelection.*`, `Search.*`, `SearchUi.*`, `OcrPreprocess.*`, `Ocr.*`, `TextReplace.*`, `PdfCompare.*`, `AppTests.ComparePdfsUi`, `AreaRender.*`, `Spell.*`, `OcrZone.*`, `OcrReview.*`, `OcrWrite.*`, `NeuralOcr.*`, `NeuralOcrAccuracy.*`, `OcrAlign.*`) green under ASan.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
