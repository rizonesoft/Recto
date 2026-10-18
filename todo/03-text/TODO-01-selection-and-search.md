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

> **Goal:** Give every page (digital, scanned, or vector-curve-outlined) one shared `PageText` model in `recto-pdf` and `recto-core`: native structured text extraction from content streams and `/ToUnicode` maps, built-in `Windows.Media.Ocr` recognition for scanned pages and vector-outlined text curves, quad-precise pointer and keyboard selection with the SPEC floating toolbar and context menu, non-blocking `Ctrl+F` / `Ctrl+H` Find & Replace (with `Regex`), and the `Ctrl+Shift+D` Compare PDFs view (side-by-side synchronized scroll and visual diff overlay).

> **Current state:** Measured 2026-10-14. No text or OCR code exists yet. `D01 T02` provides the content-stream interpreter, DirectWrite font loader and `DocumentActor`; `D02 T02` provides the page coordinate mapping (`§5`); `D02 T03` provides the panel hosting contract (`§3`). The UI boards are `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png` (Reading room inspector search tab and filmstrip hit markers), governed by `design/SPEC.md` sections 9.3 and 9.3.1.

## Inputs

- `design/SPEC.md` sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`, `RectoTextSelectionBrush`), 9.3 (Search, Find & Replace, and selection), 9.3.1 (Compare PDFs), 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR language), 10 (`Ctrl+F`, `Ctrl+H`, `F3`, `Shift+F3`, `Ctrl+Shift+D`), 11 (selection announced to screen readers).
- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`PageText`), 5 (Searchable PDF OCR, Vector-Outlined Text OCR, Find & Replace, Document Comparison), 6.3 (`PageText` shared across selection, search, OCR, comparison and UIA), 7 (M3 done-when).
- -> XREF: D02 T02 §5 -- selection, search and comparison highlights composite over the page geometry and tile canvas built in `D02 T02`.
- -> XREF: D02 T03 §3 -- the Reading room Search inspector tab and filmstrip hit badges plug into the panel host established in `D02 T03`.
- -> XREF: D02 T04 §5 -- the `Automatic OCR on scanned pages` and `OCR language` rows in Settings are consumed by §8 here.
- -> XREF: D03 T02 §1 -- `DocumentViewAutomationPeer` and `UiaTextRange` (`D03 T02 §1`, `§2`) consume `PageText`'s character quads, `Span` heading/style attributes, and immutable `StructNode` (`/StructTreeRoot` `/MCID`) structure snapshot built in §1 and §2 here.
- -> XREF: D03 T03 §3 -- the floating selection toolbar's `Highlight`, `Strikeout`, and `Add Note` actions invoke `D03 T03`'s annotation engine.
- -> XREF: D04 T02 §6 -- `PDF -> Plain Text` and `Searchable PDF (OCR)` consume `OcrPipeline` from §8 here.
- -> XREF: D04 T02 §8 -- `TableExtractor` consumes `PageText` character and span quads from §1 here.
- -> XREF: D04 T03 §2 -- `DocxWriter` (PDF to Word) consumes `PageText` (including OCR-populated `PageText` on scanned and vector-outlined pages) from §1 and §8 here.
- -> XREF: D04 T05 §7 -- `PDF/UA-1` auto-tagger consumes `PageText` blocks and reading order from §1 here.
- -> XREF: D05 T01 §10 -- `Redactor` consumes `PageText` character quads from §1 here.
- -> XREF: D05 T02 §9 -- `Find & Redact` in the Redaction Bar reuses the find engine from §5 and §6 here.
- -> XREF: D06 T01 §4 -- `TextTables.*` (`D06 T01 §4`) verifies `PageText` normalization, regex search, `Replace Text` (`§9`), and `Compare PDFs` (`§10`) built here.
- -> XREF: D06 T02 §3 -- `fuzz_text` (`D06 T02 §3`) fuzzes the `PageText` extractor, `SearchEngine`, and content-stream text replacer (`§1`, `§5`, `§9`) built here.

## Outcome

- `recto-pdf` extracts `PageText` (`Blocks -> Lines -> Spans -> Chars` with quad bounding boxes in page points, font metadata, reading-order clustering, and bounded `/StructTreeRoot` `/MCID` structure nodes `StructNode` for tagged headings `/H1`..`/H6`, `/Table`/`/TR`/`/TH`/`/TD`, `/L`/`/LI`, and `/Figure` `/Alt` descriptions) on the `DocumentActor` thread, and `recto-cli text`, `recto-cli find`, `recto-cli replace`, and `recto-cli compare` expose it from the command line.
- Selecting text on a page paints `RectoTextSelectionBrush` over character quads, shows the dark floating toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`) above the selection and the full context menu on right-click (`Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find`, `Select all on page`, `Copy page link`, `Export Selection as Table`), and announces selection changes to screen readers.
- `Ctrl+F` and `Ctrl+H` open the Find & Replace card (top-right in Focus/Classic, Search inspector tab with filmstrip markers in Reading room), search all pages off the UI thread with `Match case`, `Whole word`, and `Regex` (regular expression) options, highlight matches in `RectoSearchHitBrush` / `RectoSearchHitCurrentBrush`, and replace matched text in content streams with glyph width reflow.
- Scanned pages and **vector-curve-outlined text pages** (where fonts were converted to vector paths) are recognized on the background `RenderPool` via Recto's C++20 image preprocessor + `Windows.Media.Ocr` (`OcrEngine`), populating `PageText` so selection, copy, `Ctrl+F`, UIA screen readers, and downstream converters work seamlessly.
- `Ctrl+Shift+D` opens the **Compare PDFs View** (`SPEC 9.3.1`), comparing Document A and Document B in Side-by-Side Synchronized View or Visual Overlay Diff View with a Changes side panel listing every text and graphic addition, deletion, and replacement.

**Adjacency:** list=applicable @ D03 T01 §6; document=applicable @ D03 T01 §9; settings=applicable @ D02 T04 §5; reporting=applicable @ D03 T01 §10; notifications=applicable @ D03 T02 §5; permissions=not-applicable (copy permission flags are reported in Info, D02 T03 §9); audit=not-applicable (search history is not persisted); exchange=applicable @ D03 T01 §4; reverse=applicable @ D03 T01 §4

**Adjacency rationale:** The list surface is the find results list (`§6`) and the comparison changes list (`§10`), document is saving a Find-and-Replace updated PDF copy (`§9`), reporting is the comparison diff summary (`§10`), exchange is copying selected text and page links to the Windows clipboard (`§4`), and reverse is clearing selection and closing/clearing find (`§4`, `§6`). Screen-reader announcements for selection and find counts are wired through `D03 T02 §5`, and the automatic-OCR and OCR-language toggles live in Settings (`D02 T04 §5`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Native `PageText` glyph-to-Unicode extraction and column reading-order clustering | D01 T02 §4 | [ ] |
| 2 | §2 | `DocumentActor::get_page_text` LRU cache and `recto-cli text` command | §1, D01 T02 §8, D01 T03 §1 | [ ] |
| 3 | §3 | Quad-precise pointer and keyboard text selection (`SelectionModel`) | §2, D02 T02 §5 | [ ] |
| 4 | §4 | Dark floating selection toolbar, right-click context menu and clipboard copy | §3 | [ ] |
| 5 | §5 | Background find engine (`Match case`, `Whole word`, `Regex`) and `recto-cli find` | §2 | [ ] |
| 6 | §6 | `Ctrl+F` floating `FindCard`, Reading room `Search` inspector tab and hit highlights | §4, §5, D02 T03 §3 | [ ] |
| 7 | §7 | Scanned-page and vector-curve-outlined text detector plus C++20 OCR image preprocessor | §2, D01 T02 §9 | [ ] |
| 8 | §8 | `Windows.Media.Ocr` (`OcrPipeline`), `PageText` quad mapping and `recto-cli ocr --text` | §6, §7, D02 T04 §5 | [ ] |
| 9 | §9 | Content-stream Find & Replace (`text_replace.cpp`), `recto-cli replace` and `Ctrl+H` | §6, D01 T02 §5 | [ ] |
| 10 | §10 | Two-document comparison (`pdf_compare.cpp`), `recto-cli compare` and `Ctrl+Shift+D` | §6, §8 | [ ] |

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
- [ ] Map `Tj`, `TJ`, `'`, and `"` glyph runs through `/ToUnicode` CMap (`D01 T02 §2`), standard encoding vectors, or glyph-name tables into Unicode codepoints and page-point quads transformed by `Tm * CTM` and page `/Rotate`, recording the active `BDC`/`EMC` `/MCID` integer when present. Done when: `PdfText.GlyphQuads` verifies character bounding quads and `/MCID` capture on unrotated and `90`/`180`/`270` rotated pages.
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
**Chrome:** consume `RectoTextSelectionBrush` from `design/tokens/RectoColors.xaml` and the page-to-viewport transform from `D02 T02 §5`. Do not invent a second selection store separate from `D03 T02 §2`.

**Build order:**

1. Implement `SelectionModel` (`src/services/selection_model.cpp`) storing anchor and active `(page_index, char_index)` endpoints over `PageText`, with hit-testing (`hit_test_point`), word/line boundary expansion, `Shift+Left/Right/Up/Down` caret extension, `select_all_on_page`, and coalesced line-quad geometry computation. Done when: `TextSelection.*` unit tests verify char drag, double-click word, triple-click line, and multi-page selection ranges.
2. Wire pointer press/move/release, double-click, triple-click, `Ctrl+A`, and `Esc` on `DocumentView` (`src/app/views/DocumentView.xaml.cpp`) and render the coalesced selection quads using `RectoTextSelectionBrush`. Done when: zooming or rotating with an active selection preserves the exact selected character range and quads on screen.

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

Build the SPEC 9.3 dark floating selection toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link` with `36 epx` hit targets positioned above the selection) and the full right-click `MenuFlyout` context menu (`Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find`, `Select all on page`, `Copy page link`, `Export Selection as Table`), plus plain UTF-8 and RTF clipboard copy (`Ctrl+C`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that clipboard serialization (`selected_text_utf8`, `selected_text_rtf`) reads only immutable `std::shared_ptr<const PageText>` snapshots without touching `recto::pdf::Document`.

**Fidelity:** SPEC sections 4 (dark floating bar tokens `#1B1A19` / `#2A2826`), 9.3 (floating selection toolbar and right-click context menu), 10 (`Ctrl+C`, `Esc`), and 11 (`36 epx` hit targets in floating toolbar), with `design/screenshots/D-Search.png`.
**Job:** Immediately after selecting text, the user can copy plain or formatted text, search for the selected phrase, copy a `#page=N` link, or trigger highlight/strikeout/note/redaction/table-export actions from either the floating toolbar or right-click menu. Consumer: the Windows clipboard, `FindCard` (`§6`), `D03 T03 §3` annotations, `D05 T02 §9` redaction, and `D04 T02 §10` table export.
**Treatment:** Dark floating pill toolbar anchored `8 epx` above the selection bounding box (flipping below when near the top viewport edge) with `36 x 36 epx` icon buttons and full right-click `MenuFlyout`. Cheaper substitute that fails the checkpoint: only supporting `Ctrl+C` with no floating selection toolbar or right-click menu, or buttons smaller than `36 epx`.
**Chrome:** consume the dark floating bar tokens (`RectoFloatingBarBackgroundBrush` `#1B1A19`, `RectoFloatingBarBorderBrush` `#2A2826`) from `design/tokens/RectoColors.xaml` and Segoe Fluent Icons.

**Build order:**

1. Implement plain UTF-8 and Rich Text Format (`CF_RTF` preserving font family, size, bold, italic, and fill colour from `PageText::Span`) clipboard writers in `src/services/selection_clipboard.cpp`, plus `#page=N` page-link copy. Done when: `Ctrl+C` copies both plain text and RTF to the clipboard and `Copy page link` copies `<path>#page=<N>`.
2. Build `SelectionFloatingToolbar` (`36 epx` buttons for `Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`) and the right-click `MenuFlyout` on `DocumentView`, wiring `Copy`, `Copy with Formatting`, `Find`, `Select all on page`, and `Copy page link` now and routing markup/redact/table hooks to `D03 T03 §3`, `D05 T02 §9`, and `D04 T02 §10`. Done when: rendered screenshot matches `D-Search.png` selection treatment and clipboard readback matches the fixture string.

- [ ] Implement `SelectionModel::selected_text_utf8()` and `selected_text_rtf()` in `src/services/selection_clipboard.cpp`, preserving paragraph breaks, font names, point sizes, bold/italic flags, and RGB colours in RTF. Done when: `TextSelection.ClipboardPlainAndRtf` verifies both UTF-8 and RTF payloads from a multi-span selection.
- [ ] Wire `Ctrl+C`, toolbar `Copy`, and context-menu `Copy` / `Copy with Formatting` to `Windows::ApplicationModel::DataTransfer::Clipboard`. Done when: selecting across two lines and pressing `Ctrl+C` places the exact text on the Windows clipboard.
- [ ] Build `SelectionFloatingToolbar` (`src/app/controls/SelectionFloatingToolbar.xaml/.cpp`) using `#1B1A19` fill, `#2A2826` border, `8 epx` corner radius, and 6 buttons (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`) each >= `36 x 36 epx` with `AutomationProperties.Name` and tooltip. Done when: `TextSelection.FloatingToolbarBounds` confirms all 6 buttons have >= `36 epx` hit targets and names. Cheaper substitute that fails the checkpoint: `28 epx` buttons or missing `Find`/`Link`/`Redact` actions.
- [ ] Position `SelectionFloatingToolbar` centered `8 epx` above the top-most selected quad onpointer release, automatically flipping `8 epx` below the bottom-most quad when the selection is within `48 epx` of the top of the viewport. Done when: selections at the very top of a page never clip the floating toolbar under the app toolbar.
- [ ] Build the right-click `MenuFlyout` context menu (`SPEC 9.3`) with `Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find "<selection>"`, `Select all on page`, `Copy page link`, and `Export Selection as Table`. Done when: right-clicking an active selection opens the complete 11-item context menu.
- [ ] Wire `Link` / `Copy page link` to copy `<filename>#page=<1-based-page>` to the clipboard and `Find` to open `FindCard` (`§6`) seeded with the selected string; record explicit command hooks for `Highlight`/`Underline`/`Strikeout`/`Add Note` (`-> XREF: D03 T03 §3`), `Redact` (`-> XREF: D05 T02 §9`), and `Export Selection as Table` (`-> XREF: D04 T02 §10`). Done when: clicking `Find` on the floating toolbar seeds the search query and clicking `Link` copies the page reference.
- [ ] Dismiss `SelectionFloatingToolbar` and clear selection when pressing `Esc` or clicking outside any text quad. Done when: `Esc` closes the floating toolbar and clears `SelectionModel`.
- [ ] Commit: `text: add quad-precise text selection, floating toolbar and context menu`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextSelection.*` passes, and display-session screenshots verify the floating selection toolbar and quad highlight against `D-Search.png`.

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
**Freeze check:** Confirm that `SearchViewModel` dispatches all multi-page search work off the UI thread with cooperative `CancelToken` cancellation and marshals progressive `SearchHit` results back via `DispatcherQueue::TryEnqueue`.

**Fidelity:** SPEC sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`), 9.3, 10 (`Ctrl+F`, `F3`, `Shift+F3`, `Esc`), with `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png`.
**Job:** The user can find every literal or regex occurrence of a word or pattern across a large PDF immediately without stalling scrolling, see each match in context, and jump between hits. Consumer: `DocumentView` scrolls to the current match and paints hit quads; `D03 T02 §5` announces result counts.
**Treatment:** Shared `SearchViewModel` driving both the top-right `FindCard` (Focus/Classic) and the right-inspector `Search` tab + filmstrip badges (Reading room), cancelling stale queries via `CancelToken` as the user types. Cheaper substitute that fails the checkpoint: searching only already-rendered pages, blocking the UI thread while scanning a 500-page PDF, or omitting the context result list / Reading room inspector adaptation.
**Chrome:** consume `RectoSearchHitBrush` and `RectoSearchHitCurrentBrush` from `design/tokens/RectoColors.xaml` and the shared `SearchViewModel`. Do not duplicate search state between layout presets.

**Build order:**

1. Implement `SearchViewModel` (`src/services/search_view_model.cpp`) managing the query string, `Match case` / `Whole word` / `Regex` toggles, active `CancelToken`, progressive hit aggregation, current hit index (`1..N`), `"N pages"` count, 6-row preview / `"Show all N results"` expansion, and `next_hit()` / `prev_hit()` / `select_hit(index)`. Done when: rapid query typing cancels in-flight searches and updates `SearchViewModel` deterministically.
2. Build `FindCard` (`src/app/controls/FindCard.xaml/.cpp` for Focus/Classic) and the Reading room `Search` inspector tab + filmstrip hit-marker badges (`D02 T03 §3` / `D02 T03 §6`), and paint `RectoSearchHitBrush` / `RectoSearchHitCurrentBrush` quads on `DocumentView`. Done when: both Focus (`D-Search.png`) and Reading room (`explorations/B-Search.png`) surfaces match their boards and scrolling stays at 60 fps during search.

- [ ] Implement `SearchViewModel` in `src/services/search_view_model.h/.cpp` with debounced progressive query execution, `CancelToken` replacement on keystroke, `"k of N"` and `"M pages"` counters, and `next_hit` / `prev_hit` wrap-around navigation. Done when: `SearchUi.ViewModel` verifies progressive hit updates, wrap-around `F3`/`Shift+F3`, and cancellation.
- [ ] Build the floating `FindCard` (`src/app/controls/FindCard.xaml/.cpp`) anchored top-right (`16 epx` inset) in Focus and Classic presets (`D-Search.png`), containing the query `TextBox`, `"2 of 14"` status text, previous/next/close icon buttons, `Match case` / `Whole word` / `Regex` toggle pills, `"N pages"` header, and the context results list (6 rows by default, expandable via `"Show all N results"`). Done when: `FindCard` matches `D-Search.png`.
- [ ] Render each result row with prefix text, bolded match span, suffix text, and right-aligned page number badge; clicking any row selects that hit and scrolls `DocumentView` so the hit quad is centered in the viewport. Done when: clicking a result row jumps to the target page and highlights that match as current.
- [ ] Host the same `SearchViewModel` inside the Reading room right inspector's `Search` tab (`D02 T03 §3`) and render accent hit-indicator dots/badges on matching page thumbnails in the bottom filmstrip (`D02 T03 §6`) when in the `Reading room` preset (`explorations/B-Search.png`). Done when: switching between `Focus` and `Reading room` preserves the active search query, results list, and current hit. Cheaper substitute that fails the checkpoint: showing the floating `FindCard` on top of the Reading room inspector instead of using the inspector `Search` tab and filmstrip markers.
- [ ] Paint all page hits in `RectoSearchHitBrush` and the active hit in `RectoSearchHitCurrentBrush` over `PageText` character quads on `DocumentView`. Done when: `SearchUi.HitQuads` confirms distinct brush tokens for passive vs active hits.
- [ ] Wire `Ctrl+F` (focus query box and select query text), `F3` / `Enter` (next hit), `Shift+F3` / `Shift+Enter` (previous hit), and `Esc` (close find and clear hit highlights when focus is in `FindCard`). Done when: all keyboard shortcuts operate end-to-end.
- [ ] Commit: `text: implement non-blocking find engine with regex and Ctrl+F search surfaces`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Search.*:SearchUi.*` passes, and display-session screenshots confirm both `D-Search.png` and `explorations/B-Search.png`.

## 7. Scanned-page and vector-curve-outlined text detector plus C++20 OCR image preprocessor

Implement `src/core/ocr_preprocess.h/.cpp` in `recto-core`: detect **scanned pages** (0 extractable `PageText` characters with raster image XObjects covering `>= 50%` of the page box) and **vector-curve-outlined text pages** (0 text operators with high-density glyph-sized Bezier/line subpaths `m`/`l`/`c`/`h`/`f`), rasterize target pages or regions at `300 DPI` on a `RenderPool` worker, and run Recto's native C++20 image preprocessor (auto-deskew for small page tilt, contrast normalization, and adaptive Sauvola/Otsu thresholding) ready for `Windows.Media.Ocr`.

**Phase:** 3
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `classify_page_for_ocr` and `preprocess_for_ocr` operate solely on immutable `const DisplayList&`, `const PageText&`, and worker-local `RgbaBitmap` buffers on `RenderPool` threads without locking `DocumentActor`.

**Build order:**

1. Implement `classify_page_for_ocr(const DisplayList&, const PageText&)` in `src/core/ocr_preprocess.cpp` distinguishing `DigitalText`, `ScannedRaster`, and `VectorOutlinedText` pages. Done when: `OcrPreprocess.PageClassification` classifies digital corpus pages, `scanned.pdf`, and vector-outlined text fixtures with 100% accuracy.
2. Implement `preprocess_for_ocr(const RgbaBitmap& src_300dpi, CancelToken)` performing grayscale conversion, projection-profile / Hough skew estimation (`±15°`), bilinear deskew rotation (recording the inverse affine matrix so OCR boxes map back to original page points), histogram contrast stretch, and adaptive Sauvola local thresholding. Done when: tilted and low-contrast synthetic test bitmaps deskew within `0.2°` of horizontal under ASan.

- [ ] Define `OcrPageKind` (`DigitalText`, `ScannedRaster`, `VectorOutlinedText`) and `classify_page_for_ocr` in `src/core/ocr_preprocess.h/.cpp`. Done when: pages with `< 3` characters and `>= 50%` image coverage classify as `ScannedRaster`.
- [ ] Detect `VectorOutlinedText` pages by counting closed subpaths (`m`/`l`/`c`/`v`/`y`/`h`) in the `DisplayList` whose bounding boxes fall within glyph dimensions (`4..72 pt` height) arranged along horizontal rows when `PageText` has `0` text operators. Done when: a PDF where text was converted to outlines classifies as `VectorOutlinedText` while a geometric line-art diagram does not. Cheaper substitute: checking only for raster images and ignoring vector-curve-outlined text pages.
- [ ] Render `ScannedRaster` and `VectorOutlinedText` pages at `300 DPI` on a background worker using `D01 T02 §6`'s tile/page rasterizer with `CancelToken` propagation. Done when: `300 DPI` rasterization never runs on the UI thread or holds the `DocumentActor` lock.
- [ ] Implement skew angle estimation (`-15°..+15°` in `0.1°` steps via horizontal projection variance maximization) and bilinear deskew rotation, storing the `2x3` inverse transform matrix `deskew_to_page_matrix`. Done when: `OcrPreprocess.Deskew` straightens a `3.5°`-tilted page bitmap and maps deskewed pixel coordinates back to original page points within `0.5 pt`.
- [ ] Implement contrast normalization (1st-to-99th percentile luminance stretch) and integral-image-accelerated Sauvola adaptive binarization (`window = 31 px`, `k = 0.2`) producing a clean high-contrast BGRA8 buffer for `SoftwareBitmap`. Done when: uneven page shading and gray scan backgrounds binarize cleanly without erasing thin glyph strokes.
- [ ] Respect `CancelToken` between preprocessing stages so closing a tab or cancelling a job aborts preprocessing immediately. Done when: `OcrPreprocess.Cancellation` aborts within `< 10 ms`.
- [ ] Add `OcrPreprocess.*` unit tests in `tests/recto-tests/ocr_preprocess_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=OcrPreprocess.*` passes under ASan.
- [ ] Commit: `feat(ocr): add scanned/vector-outlined page detector and C++20 OCR image preprocessor`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrPreprocess.*` passes under ASan across digital, scanned, tilted, and vector-outlined fixtures.

## 8. `Windows.Media.Ocr` (`OcrPipeline`), `PageText` quad mapping and `recto-cli ocr --text`

Implement `src/core/ocr_pipeline.h/.cpp` (`OcrPipeline`) using Windows 11's native `Windows.Media.Ocr` (`OcrEngine`): feed §7's preprocessed `300 DPI` `SoftwareBitmap` into `OcrEngine::RecognizeAsync` for the configured language (`TryCreateFromUserProfileLanguages` or the language chosen in `D02 T04 §5`), map `OcrLine` and `OcrWord` bounding rects back to PDF page points (`OcrResult -> PageText` quad mapping: `CharQuad`, `Span`, `Line`, `Block`), populate the page's `PageText` cache in `DocumentActor`, add `recto-cli ocr <file.pdf> --text [--vector-outlines] [--lang <tag>]` (`recto-cli ocr --text`), and wire automatic/on-demand viewer OCR.

**Phase:** 3
**Needs:** windows-host
**Surface:** yes
**Freeze check:** Confirm that `OcrPipeline` runs `OcrEngine::RecognizeAsync` on a background worker thread and publishes the recognized `std::shared_ptr<const PageText>` snapshot into `DocumentActor` via an actor message without mutating any existing `PageText` in place.

**Fidelity:** SPEC sections 9.3 (selection and search work identically once OCR populates `PageText`), 9.5 (vector-outlined text OCR option), and 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR language).
**Job:** A user opening a scanned PDF or a PDF where text was converted to vector curves can select text, copy it, search it with `Ctrl+F`, and read it with a screen reader without installing third-party OCR software. Consumer: `DocumentActor`'s `PageText` cache, `D03 T02 §1` UIA provider, `D04 T02 §6` plain-text and `§7` searchable-PDF writers, and `D04 T03 §2` `DocxWriter`.
**Treatment:** Background `OcrPipeline` invoking `Windows.Media.Ocr::OcrEngine` off the UI thread, mapping recognized word rects through §7's inverse deskew matrix into `PageText` character quads, and caching the resulting `PageText` on `DocumentActor`. Cheaper substitute that fails the checkpoint: running OCR on the UI thread, ignoring vector-outlined curve text, or storing OCR text in a separate side-channel that selection and UIA cannot read.
**Chrome:** consume the existing `PageText` selection/search overlays (`§3`, `§6`), the Settings preferences (`D02 T04 §5`), and polite status announcement (`D03 T02 §5`); no modal dialog blocks reading while background OCR completes.

**Build order:**

1. Implement `OcrPipeline::recognize_page` in `src/core/ocr_pipeline.cpp` wrapping `Windows::Media::Ocr::OcrEngine`, converting `OcrResult` (`Lines -> Words`) into `PageText` (`Block -> Line -> Span -> CharQuad` with proportionally subdivided per-character quads mapped through `deskew_to_page_matrix`), and add `recto-cli ocr <file.pdf> --text [--vector-outlines] [--lang <bcp47>]`. Done when: `Ocr.*` tests run on `tests/corpus/text/scanned.pdf` and a vector-outlined-text fixture and extract expected text and word bounding boxes with `> 98%` character accuracy under ASan.
2. Wire automatic and on-demand OCR into `DocumentActor` and `SearchViewModel` (honoring `Automatic OCR on scanned pages` and `OCR language` from `D02 T04 §5`). Done when: opening `scanned.pdf` or a vector-outlined PDF in the viewer makes its words selectable by mouse and searchable via `Ctrl+F` with accurate highlight quads.

- [ ] Implement `OcrEngine` initialization in `src/core/ocr_pipeline.cpp` supporting `OcrEngine::TryCreateFromLanguage(Language(bcp47))` when a specific language is configured in Settings (`D02 T04 §5`) and falling back to `OcrEngine::TryCreateFromUserProfileLanguages()`. Done when: `Ocr.EngineInit` enumerates installed Windows OCR languages and initializes cleanly.
- [ ] Wrap the preprocessed `300 DPI` BGRA8 buffer in a `Windows::Graphics::Imaging::SoftwareBitmap` and invoke `OcrEngine::RecognizeAsync` on a background worker thread with `CancelToken` cancellation. Done when: `OcrPipeline` never touches the UI thread and aborts cleanly when cancelled.
- [ ] Map each `OcrWord` bounding rect and `OcrLine::Text` through §7's `deskew_to_page_matrix` into `PageText` (`Block`, `Line`, `Span`, and per-codepoint `CharQuad`s subdivided across the word width, plus space `CharQuad`s between words). Done when: `Ocr.QuadMapping` produces `CharQuad` coordinates in PDF page points that align within `1.5 pt` of the rendered glyph bitmaps.
- [ ] Populate `DocumentActor`'s `PageText` cache with the OCR-synthesized `std::shared_ptr<const PageText>` so `get_page_text` serves it to selection (`§3`), search (`§5`), UIA (`D03 T02 §1`), and converters (`D04`). Done when: calling `get_page_text` after OCR returns the recognized `PageText` snapshot. Cheaper substitute that fails the checkpoint: storing OCR output outside `PageText` so mouse selection and `Ctrl+F` still see an empty page.
- [ ] Implement `recto-cli ocr <file.pdf> --text [--vector-outlines] [--lang <tag>] [--pages <range>]` in `src/cli/cmd_ocr.cpp`. Done when: `recto-cli ocr tests/corpus/text/scanned.pdf --text` prints the recognized text with `> 98%` character accuracy.
- [ ] Trigger background OCR automatically when viewing or searching a `ScannedRaster` or `VectorOutlinedText` page if `Automatic OCR on scanned pages` (`D02 T04 §5`) is enabled, or via the command box (`Ctrl+K` -> `Recognize Text (OCR)`). Done when: `Ctrl+F` on `scanned.pdf` highlights the recognized words at their exact image coordinates.
- [ ] Commit: `text: add built-in Windows.Media.Ocr pipeline for scanned and vector-outlined PDFs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OcrPreprocess.*:Ocr.*` passes under ASan, and `recto-cli ocr tests/corpus/text/scanned.pdf --text` prints the expected recognized text.

## 9. Content-stream Find & Replace (`text_replace.cpp`), `recto-cli replace` and `Ctrl+H`

Implement `src/pdf/text_replace.h/.cpp` in `recto-pdf`: locate literal or regular-expression text matches in page content streams on an isolated authoring `recto::pdf::Document` instance (never sharing or mutating the open viewer `DocumentActor`'s `pdf::Document` in place), replace matched glyph-run strings (`Tj` / `TJ`), recalculate horizontal glyph advances using the font's width metrics (embedding or selecting a Standard-14 / TrueType fallback font resource when the replacement introduces characters absent from a subset font), write the updated PDF copy via `IncrementalWriter` (`D01 T02 §5`) and temp-file-then-replace, expose `recto-cli replace <in.pdf> --find <pat> --replace <str> [--regex] [--case] [--whole-word] --out <out.pdf>`, and wire the `Ctrl+H` Replace expansion row in `FindCard` (`SPEC 9.3`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes
**Freeze check:** Confirm that content-stream replacement always opens its own isolated `recto::pdf::Document` copy on a background job thread, never shares or mutates the open viewer `DocumentActor`'s `pdf::Document` in place, leaves the open viewer `DocumentActor` and its cached `std::shared_ptr<const DisplayList>` / `std::shared_ptr<const PageText>` snapshots untouched on cancellation or failure, writes output via temp-file-then-replace, and reloads/swaps the viewer tab onto a fresh `DocumentActor` only after the replacement copy succeeds.

**Fidelity:** SPEC sections 9.3 (`Ctrl+H` Find & Replace expansion in `FindCard`) and 10 (`Ctrl+H` shortcut) with `D-Search.png`.
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

**Fidelity:** SPEC sections 4 (`RectoSuccessBrush`, `RectoDangerBrush`), 9.3.1 (`Ctrl+Shift+D` Compare PDFs View: Side-by-Side Synchronized View, Visual Overlay Diff View, and Changes panel), and 10 (`Ctrl+Shift+D` shortcut).
**Job:** The user can compare two versions of a PDF document to inspect every text insertion, deletion, replacement, and graphic change side-by-side or overlaid, and jump to any change from the summary panel. Consumer: `CompareView` canvases and Changes panel, plus `recto-cli compare` JSON reports.
**Treatment:** Dual text-quad Myers diff + visual raster diff on background workers, rendered in a dedicated `CompareView` with synchronized two-canvas scrolling/zooming or alpha-blended visual overlay. Cheaper substitute that fails the checkpoint: plain-text diff without page quad coordinates, or unsynchronized side-by-side panes with no visual overlay mode.
**Chrome:** consume `RectoSuccessBrush` (insertions), `RectoDangerBrush` (deletions), `RectoWarningBrush` (replacements/graphic diffs) from `design/tokens/RectoColors.xaml`, and reuse `DocumentView` tile presentation for both panes.

**Build order:**

1. Implement `compare_documents(doc_a, doc_b, CancelToken)` in `src/core/pdf_compare.cpp` computing a word-and-line Myers diff over `PageText` (with exact page-point `CharQuad` bounds in both documents) plus a `150 DPI` visual tile diff for non-text graphic changes, and expose `recto-cli compare <a.pdf> <b.pdf> [--json]`. Done when: `PdfCompare.*` identifies exact inserted, deleted, replaced, and graphic-modified regions between two corpus document revisions under ASan.
2. Build `CompareView` (`src/app/views/CompareView.xaml/.cpp`, `Ctrl+Shift+D`, `SPEC 9.3.1`) with Document A / Document B file selectors, `Side-by-Side` vs `Visual Overlay` mode segmented toggle, locked scroll/zoom synchronization, diff quad overlays, and the right `Changes` panel grouped by page. Done when: clicking any entry in the `Changes` panel scrolls both canvases to the diff quad and toggles cleanly between Side-by-Side and Visual Overlay modes.

- [ ] Define `DiffKind` (`Inserted`, `Deleted`, `Replaced`, `GraphicChanged`), `DiffEntry` (`id`, `kind`, `page_a`, `page_b`, `quads_a`, `quads_b`, `old_text`, `new_text`), and `ComparisonReport` (`total_insertions`, `total_deletions`, `total_replacements`, `total_graphic_changes`, `entries`) in `src/core/pdf_compare.h`. Done when: `ComparisonReport` serializes to deterministic JSON.
- [ ] Implement the Myers O(ND) word-sequence diff over `PageText` tokens across Document A and Document B in `src/core/pdf_compare.cpp`, coalescing adjacent inserted/deleted words into phrase-level `DiffEntry` records with exact `CharQuad` bounding boxes in page points. Done when: `PdfCompare.TextDiff` detects exact word additions, deletions, and replacements across multi-page revisions. Cheaper substitute that fails the checkpoint: line-only plain-text diff without `CharQuad` coordinates.
- [ ] Implement the visual raster diff pass (`150 DPI` page renders compared in `8 x 8 pt` cells after masking out already-reported text diff quads) to detect modified diagrams, signatures, stamps, or layout shifts as `GraphicChanged` bounding boxes. Done when: `PdfCompare.GraphicDiff` detects a modified vector figure on a page whose text did not change.
- [ ] Implement `recto-cli compare <a.pdf> <b.pdf> [--json]` in `src/cli/cmd_compare.cpp`, printing a human-readable summary or structured JSON diff report. Done when: `Cli.CompareCommand` passes under ASan.
- [ ] Build `CompareView` (`src/app/views/CompareView.xaml/.cpp`, reachable via `Ctrl+Shift+D`, `Tools -> Compare PDFs`, and `Ctrl+K`) with header pickers for Document A (defaulting to the active viewer tab) and Document B, a `Side-by-Side` / `Visual Overlay` segmented switch, and a diff-count summary badge. Done when: `Ctrl+Shift+D` opens `CompareView` with the current document preloaded as Document A.
- [ ] Implement **Side-by-Side Synchronized View** (two `DocumentView` canvases with locked scroll offset and zoom level, painting `RectoDangerBrush` over deletions on A and `RectoSuccessBrush` over insertions on B) and **Visual Overlay Diff View** (single composite canvas blending Document A and B with colour-coded diff highlights). Done when: scrolling or zooming Pane A updates Pane B in lockstep within the same frame.
- [ ] Build the right **Changes panel** (`SPEC 9.3.1`) listing all `DiffEntry` cards grouped by page with filter chips (`All`, `Inserted`, `Deleted`, `Replaced`, `Graphics`) and click-to-jump navigation. Done when: `AppTests.ComparePdfsUi` drives opening two fixtures in `CompareView`, filtering changes, clicking a diff card to jump both panes, and toggling `Visual Overlay`.
- [ ] Commit: `text: add content-stream Find & Replace (Ctrl+H) and Compare PDFs view (Ctrl+Shift+D)`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCompare.*:Cli.CompareCommand:AppTests.ComparePdfsUi` passes under ASan, and display-session screenshots verify the `Ctrl+Shift+D` Compare PDFs view.

## Verification

- M3 text and OCR obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `Ctrl+F` and `Ctrl+H` find and replace across multi-hundred-page PDFs without blocking scrolling, scanned and vector-outlined PDFs become selectable and searchable via built-in OCR, and `Compare PDFs` highlights every text and graphic change.
- `recto-tests` (`PdfText.*`, `ActorText.*`, `TextSelection.*`, `Search.*`, `SearchUi.*`, `OcrPreprocess.*`, `Ocr.*`, `TextReplace.*`, `PdfCompare.*`, `AppTests.ComparePdfsUi`) green under ASan.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
