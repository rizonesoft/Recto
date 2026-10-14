---
schema_version: 1
id: selection-and-search
domain: 03-text
status: active
title: "TODO-01 -- Text Extraction, Built-in OCR, Selection and Search"
track: M3
---

# TODO-01 -- Text Extraction, Built-in OCR, Selection, Search & Replace, and Comparison

> **Goal:** Give every page (digital, scanned, or vector-curve-outlined) one shared `PageText` model in `recto-pdf` and `recto-core`: native structured text extraction from content streams and `/ToUnicode` maps, built-in `Windows.Media.Ocr` recognition for scanned pages and vector-outlined text curves, quad-precise pointer and keyboard selection with the SPEC floating toolbar and context menu, non-blocking `Ctrl+F` / `Ctrl+H` Find & Replace (with `Regex`), and the `Ctrl+Shift+D` Compare PDFs view (side-by-side synchronized scroll and visual diff overlay).

> **Current state:** Measured 2026-10-14. No text or OCR code exists yet. `D01 T02` provides the content-stream interpreter, DirectWrite font loader and `DocumentActor`; `D02 T02` provides the page coordinate mapping (`§3`); `D02 T03` provides the panel hosting contract (`§2`). The UI boards are `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png` (Reading room inspector search tab and filmstrip hit markers), governed by `design/SPEC.md` sections 9.3 and 9.3.1.

## Inputs

- `design/SPEC.md` sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`, `RectoTextSelectionBrush`), 9.3 (Search, Find & Replace, and selection), 9.3.1 (Compare PDFs), 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR language), 10 (`Ctrl+F`, `Ctrl+H`, `F3`, `Shift+F3`, `Ctrl+Shift+D`), 11 (selection announced to screen readers).
- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`PageText`), 5 (Searchable PDF OCR, Vector-Outlined Text OCR, Find & Replace, Document Comparison), 6.3 (`PageText` shared across selection, search, OCR, comparison and UIA), 7 (M3 done-when).
- -> XREF: D02 T02 §2 -- selection, search and comparison highlights composite over the page geometry and tile canvas built in `D02 T02`.
- -> XREF: D02 T03 §3 -- the Reading room Search inspector tab and filmstrip hit badges plug into the panel host established in `D02 T03`.
- -> XREF: D03 T03 §2 -- the floating selection toolbar's `Highlight`, `Strikeout`, and `Add Note` actions invoke `D03 T03`'s annotation engine.
- -> XREF: D04 T03 §1 -- `DocxWriter` (PDF to Word) consumes `PageText` (including OCR-populated `PageText` on scanned and vector-outlined pages) from §1 and §4 here.

## Outcome

- `recto-pdf` extracts `PageText` (`Blocks -> Lines -> Spans -> Chars` with quad bounding boxes in page points, font metadata, and reading-order clustering) on the `DocumentActor` thread, and `recto-cli text`, `recto-cli find`, `recto-cli replace`, and `recto-cli compare` expose it from the command line.
- Selecting text on a page paints `RectoTextSelectionBrush` over character quads, shows the dark floating toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`) above the selection and the full context menu on right-click (`Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find`, `Select all on page`, `Copy page link`, `Export Selection as Table`), and announces selection changes to screen readers.
- `Ctrl+F` and `Ctrl+H` open the Find & Replace card (top-right in Focus/Classic, Search inspector tab with filmstrip markers in Reading room), search all pages off the UI thread with `Match case`, `Whole word`, and `Regex` (regular expression) options, highlight matches in `RectoSearchHitBrush` / `RectoSearchHitCurrentBrush`, and replace matched text in content streams with glyph width reflow.
- Scanned pages and **vector-curve-outlined text pages** (where fonts were converted to vector paths) are recognized on the background `RenderPool` via Recto's C++20 image preprocessor + `Windows.Media.Ocr` (`OcrEngine`), populating `PageText` so selection, copy, `Ctrl+F`, UIA screen readers, and downstream converters work seamlessly.
- `Ctrl+Shift+D` opens the **Compare PDFs View** (`SPEC 9.3.1`), comparing Document A and Document B in Side-by-Side Synchronized View or Visual Overlay Diff View with a Changes side panel listing every text and graphic addition, deletion, and replacement.

**Adjacency:** list=applicable @ D03 T01 §3; document=applicable @ D03 T01 §5; settings=applicable @ D02 T04 §2; reporting=applicable @ D03 T01 §5; notifications=applicable @ D03 T02 §3; permissions=not-applicable (copy permission flags are reported in Info, D02 T03 §3); audit=not-applicable (search history is not persisted); exchange=applicable @ D03 T01 §2; reverse=applicable @ D03 T01 §2

**Adjacency rationale:** The list surface is the find results list (`§3`) and the comparison changes list (`§5`), document is saving a Find-and-Replace updated PDF copy (`§5`), reporting is the comparison diff summary (`§5`), exchange is copying selected text and page links to the Windows clipboard (`§2`), and reverse is clearing selection and closing/clearing find (`§2`, `§3`). Screen-reader announcements for selection and find counts are wired through `D03 T02 §3`, and the automatic-OCR and OCR-language toggles live in Settings (`D02 T04 §2`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Native `PageText` extraction model and `recto-cli text` | D01 T02 §3, D01 T03 §1 | [ ] |
| 2 | §2 | Quad-precise text selection, floating toolbar, context menu and clipboard | §1, D02 T02 §3 | [ ] |
| 3 | §3 | Non-blocking find engine (`Match case`, `Whole word`, `Regex`) and `Ctrl+F` search card | §2, D02 T03 §2 | [ ] |
| 4 | §4 | Built-in OCR pipeline (`Windows.Media.Ocr`) for scanned and vector-outlined text pages | §1, §3 | [ ] |
| 5 | §5 | Content-stream Find & Replace (`Ctrl+H`) and Compare PDFs View (`Ctrl+Shift+D`) | §3, §4 | [ ] |

## 1. Native `PageText` extraction model and `recto-cli text`

Implement `src/pdf/page_text.cpp` in `recto-pdf`: during or alongside content-stream interpretation, map every glyph run (`Tj`, `TJ`, `'`, `"`) through the font's `/ToUnicode` CMap or encoding table and text matrix (`Tm * CTM`) to Unicode codepoints with exact quad bounding boxes in page points, font name/size/weight/italic flags, and baseline angle. Coalesce characters into words, lines (de-hyphenating soft hyphens and joining kerning splits from `TJ` adjustments), columns, and reading-order blocks. Expose `get_page_text` on `DocumentActor` (cached in an LRU alongside `DisplayList`) and add `recto-cli text <file.pdf>` to print UTF-8 page text.

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PageText` (`Block`, `Line`, `Span`, `CharQuad`) and `src/pdf/page_text.cpp` coalescing positioned glyphs into reading-order blocks (handling two-column layouts, rotated text, `TJ` kerning offsets, and ligature expansion `fi`/`fl`/`ff`/`ffi`/`ffl`). Done when: `PdfText.*` unit tests extract exact UTF-8 strings and monotonically ordered character quads from single-column, two-column, and rotated corpus pages.
2. Wire `DocumentActor::get_page_text` and add `recto-cli text <file.pdf> [--pages <range>]`. Done when: `recto-cli text` outputs match the expected text fixtures for `tests/corpus/text/` under ASan.

- [ ] `PageText` builder in `src/pdf/page_text.cpp` with reading-order column/block clustering, `TJ` kerning threshold handling, and ligature normalization. Done when: a two-column corpus page extracts column 1 top-to-bottom before column 2, rather than interleaving lines across the gutter. Cheaper substitute: dumping raw `Tj` strings in stream order with no coordinate clustering.
- [ ] `DocumentActor` caching and `recto-cli text` verb. Done when: `recto-cli text` passes all `tests/corpus/text/` fixtures under ASan.
- [ ] Commit: `text: implement native PageText extraction model and recto-cli text`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfText.*` passes under ASan across single-column, two-column, rotated and ligature fixtures.

## 2. Quad-precise text selection, floating toolbar, context menu and clipboard

Implement interactive text selection over `DocumentView` (`src/app` and `src/services`): pointer drag (character-by-character, double-click word, triple-click line/paragraph) and keyboard `Shift+Arrow` selection hit-test against `PageText` quads through `D02 T02 §3`'s page-to-viewport transform, paint `RectoTextSelectionBrush` (accent at 18% light / 22% dark) over the selected quads, and display the SPEC 9.3 dark floating selection toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link` to copy a `#page=N` reference) above the selection and the full right-click context menu (`Copy`, `Copy with Formatting`, `Highlight`, `Underline`, `Strikeout`, `Add Note`, `Mark for Redaction`, `Find`, `Select all on page`, `Copy page link`, `Export Selection as Table`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4 (`RectoTextSelectionBrush`), 9.3 (floating selection toolbar and context menu), 10 (`Ctrl+C`, `Ctrl+A`, `Esc`), 11 (36 epx hit targets in floating toolbar; selection status announced), with `design/screenshots/D-Search.png`.
**Job:** The user can select text on any page, copy it cleanly to the clipboard, search for the selected phrase, mark it up, or copy a page link. Consumer: the Windows clipboard, the find card (§3), annotations (`D03 T03 §2`), redaction (`D05 T02 §6`), and the UIA text provider (`D03 T02 §1`).
**Treatment:** Hit-testing and painting over `PageText` character quads in page space so zoom and rotation never shift selection bounds. Cheaper substitute that fails the checkpoint: rectangular rubber-band selection that ignores line flow, or losing the selection when zooming.
**Chrome:** consume `RectoTextSelectionBrush`, the dark floating bar tokens (`#1B1A19` / `#2A2826`), and Segoe Fluent Icons (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link`). Do not invent a second selection store separate from `D03 T02 §1`.

**Build order:**

1. Implement selection state and hit-testing (`SelectionModel` in `src/services`) supporting single-click clear, drag char range, double-click word, triple-click line, `Ctrl+A` (select all on current page), and multi-page drag selection, rendered as quad overlays on the presentation surface. Done when: zooming or rotating with active selection preserves the exact selected character range and quads.
2. Build the dark floating toolbar (`Copy`, `Highlight`, `Strikeout`, `Redact`, `Find`, `Link` with 36 epx hit targets) and right-click `MenuFlyout`, wiring `Ctrl+C` and `Copy` (plain UTF-8 and RTF formatted copy) to the clipboard and `Find` to open §3's find card seeded with the selection. Done when: selecting across two lines and pressing `Ctrl+C` places the exact text on the clipboard and `Esc` clears selection.

- [ ] Quad-precise pointer and keyboard selection preserved across zoom and rotation. Done when: double-click, triple-click, drag and `Ctrl+A` match expected character ranges in `TextSelection.*`. Cheaper substitute: single-line-only selection or clearing selection on every scroll/zoom tick.
- [ ] Floating selection toolbar (36 epx targets) and context menu with working `Copy`, `Find`, `Link` and markup hooks. Done when: rendered screenshot matches `D-Search.png` selection treatment and clipboard readback matches the fixture string.
- [ ] Commit: `text: add quad-precise text selection, floating toolbar and context menu`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextSelection.*` passes, and display-session screenshots verify the floating selection toolbar and quad highlight against `D-Search.png`.

## 3. Non-blocking find engine (`Match case`, `Whole word`, `Regex`) and `Ctrl+F` search card

Implement the background find engine in `src/core/search.cpp` (plus `recto-cli find <file.pdf> <query> [--case] [--whole-word] [--regex]`) and the SPEC 9.3 find surfaces:
- In Focus and Classic presets: the top-right floating card (`D-Search.png`) with query box, `"2 of 14"`, previous/next (`F3` / `Shift+F3`), close (`Esc`), `Match case`, `Whole word`, and `Regex` (regular expression) toggles, `"4 pages"` summary, up to 6 context result rows with bolded match text and page number, and `"Show all N results"`.
- In Reading room preset: the same search view hosted in the right inspector's `Search` tab (`explorations/B-Search.png`) with hit markers on the bottom filmstrip thumbnails.
- On the page canvas: all matches highlighted in `RectoSearchHitBrush` and the active match in `RectoSearchHitCurrentBrush` with `RectoOnAccentBrush` text.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`), 9.3, 10 (`Ctrl+F`, `F3`, `Shift+F3`, `Esc`), with `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png`.
**Job:** The user can find every literal or regex occurrence of a word or pattern across a large PDF immediately without stalling scrolling, see each match in context, and jump between hits. Consumer: the document view scrolls to the current match and paints hit quads; `D06 T01 §2` verifies the find tables.
**Treatment:** Incremental background search across pages starting from the current visible page outward, cancelling stale queries via `CancelToken` as the user types, with recorded Unicode case-folding, ligature (`fi` matching `"fi"`), soft-hyphen normalization, and ECMAScript/POSIX regular expression matching rules. Cheaper substitute that fails the checkpoint: searching only already-rendered pages or blocking the UI thread while scanning a 500-page PDF.
**Chrome:** consume `RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`, and the shared search view-model across both the floating card and the Reading room inspector tab. Do not duplicate search state between presets.

**Build order:**

1. Implement `src/core/search.cpp` (Unicode case-folding, ligature and soft-hyphen normalization, `Match case`, `Whole word`, `Regex` pattern matching with invalid-regex guard, snippet extraction with bold spans, and `CancelToken` cancellation) and `recto-cli find`. Done when: `Search.*` tests verify exact hit counts, regex matches (e.g., dates/emails), page counts, and snippets across multi-page, ligature, and hyphenated fixtures under ASan.
2. Build the `FindCard` / Reading room `Search` inspector tab and filmstrip hit markers, wire `Ctrl+F`, `F3`, `Shift+F3` and `Esc`, and render hit quads on the presentation canvas. Done when: searching a 200-page PDF updates results progressively while scrolling stays at 60 fps, and both Focus (`D-Search.png`) and Reading room (`B-Search.png`) surfaces match their boards.

- [ ] Background find engine with ligature/hyphen normalization, `Match case`, `Whole word`, `Regex`, and `recto-cli find`. Done when: `recto-tests.exe --gtest_filter=Search.*` passes under ASan. Cheaper substitute: `std::string::find` on ASCII bytes that misses `fi` ligatures, non-ASCII case folding, regex patterns, and line-break hyphens.
- [ ] `Ctrl+F` floating card, Reading room Search inspector tab, filmstrip hit markers, and page hit highlights (`RectoSearchHitBrush` / `RectoSearchHitCurrentBrush`). Done when: screenshots match `D-Search.png` and `B-Search.png` and rapid typing cancels prior passes cleanly.
- [ ] Commit: `text: implement non-blocking find engine with regex and Ctrl+F search surfaces`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Search.*` passes under ASan, and display-session screenshots confirm both `D-Search.png` and `explorations/B-Search.png`.

## 4. Built-in OCR pipeline (`Windows.Media.Ocr`) for scanned and vector-outlined text pages

Implement `src/core/ocr_pipeline.cpp` (`OcrPipeline`) using Windows 11's native `Windows.Media.Ocr` (`OcrEngine`):
1. Detect **scanned pages** (pages where `PageText` has 0 extractable characters and raster image XObjects cover >= 50% of the page box) and **vector-curve-outlined text pages** (pages where fonts were converted to vector Bézier outlines `m`/`l`/`c`/`h`/`f` with 0 text operators and high-density glyph-sized subpaths) automatically or on explicit user command.
2. Render the scanned page or vector-outlined text regions at 300 DPI on a `RenderPool` worker, run Recto's C++20 image preprocessor (auto-deskew for small page tilt, contrast normalization, and adaptive Sauvola/Otsu thresholding into an `IBuffer` / `SoftwareBitmap`), and invoke `OcrEngine::RecognizeAsync` for the configured Windows OCR language (`TryCreateFromUserProfileLanguages` or the language selected in Settings).
3. Map recognized `OcrLine` and `OcrWord` bounding rects back to PDF page points (`CharQuad`, `Span`, `Line`, `Block`) and populate the page's `PageText` in `DocumentActor` so pointer selection, `Ctrl+C` copy, `Ctrl+F` search, UIA screen readers (`D03 T02 §1`), `recto-cli ocr <file.pdf>`, `PDF -> Text` (`D04 T02 §4`), and `PDF -> Word` (`D04 T03 §1`) all work on both scanned and vector-outlined PDFs.

**Phase:** 3
**Needs:** windows-host
**Surface:** yes

**Fidelity:** SPEC sections 9.3 (selection and search work identically once OCR populates `PageText`), 9.5 (vector-outlined text OCR option), and 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR language).
**Job:** A user opening a scanned PDF or a PDF where text was converted to vector curves can select text, copy it, search it with `Ctrl+F`, and read it with a screen reader without installing third-party OCR software. Consumer: `DocumentActor`'s `PageText` cache, `D03 T02 §1` UIA provider, `D04 T02 §4` searchable-PDF writer, and `D04 T03 §1` `DocxWriter`.
**Treatment:** Background `OcrPipeline` with C++20 deskew/contrast preprocessing and vector-curve-outlined text detection feeding `Windows.Media.Ocr`, cancellable via `CancelToken`, caching recognized `PageText` per page on the actor. Cheaper substitute that fails the checkpoint: running OCR on the UI thread, ignoring vector-outlined curve text, or storing OCR text in a separate side-channel that selection and UIA cannot read.
**Chrome:** consume the existing `PageText` overlays and polite status announcement (`D03 T02 §3`); no extra modal chrome blocks reading while background OCR completes.

**Build order:**

1. Implement `src/core/ocr_pipeline.cpp`: scanned-page and vector-outlined-text detector, 300 DPI page/region rasterization + C++20 deskew and contrast preprocessor, `Windows.Media.Ocr::OcrEngine` invocation with `CancelToken` support, and `OcrResult -> PageText` coordinate mapping. Add `recto-cli ocr <file.pdf> --text [--vector-outlines]` for headless verification. Done when: `Ocr.*` tests run on both the scanned raster corpus PDF (`tests/corpus/text/scanned.pdf`) and a vector-outlined-text fixture and extract the expected text and word bounding boxes with > 98% character accuracy.
2. Wire automatic and on-demand OCR into `DocumentActor` and `Search` (honoring the `Automatic OCR on scanned pages` and `OCR language` settings from `D02 T04 §2`). Done when: opening `scanned.pdf` or an outlined-curves PDF in the viewer makes its words selectable by mouse and searchable via `Ctrl+F` with accurate highlight quads.

- [ ] `OcrPipeline` (`src/core/ocr_pipeline.cpp`) with C++20 image preprocessing, vector-outlined-text detection, `Windows.Media.Ocr` integration, `PageText` quad mapping, and `recto-cli ocr`. Done when: `recto-tests.exe --gtest_filter=Ocr.*` extracts the expected text and word quads from both scanned and vector-outlined fixtures under ASan. Cheaper substitute: returning empty `PageText` on scanned or vector-outlined pages.
- [ ] Seamless viewer selection and `Ctrl+F` search over OCR-recognized pages. Done when: `Ctrl+F` on `scanned.pdf` highlights the recognized words at their exact image coordinates.
- [ ] Commit: `text: add built-in Windows.Media.Ocr pipeline for scanned and vector-outlined PDFs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ocr.*` passes under ASan, and `recto-cli ocr tests/corpus/text/scanned.pdf --text` prints the expected recognized text.

## 5. Content-stream Find & Replace (`Ctrl+H`) and Compare PDFs View (`Ctrl+Shift+D`)

Deliver content-stream text replacement and two-document visual/semantic comparison:
1. **Find & Replace (`src/pdf/text_replace.cpp` and `Ctrl+H` in `FindCard`):** Locate literal or regular-expression text matches in page content streams, replace the matched run strings (`Tj` / `TJ`), recalculate horizontal glyph advances using the page font's width metrics (embedding/updating a TrueType/Standard-14 fallback subset if the replacement introduces characters absent from the original subset), and write the updated PDF copy. In the UI (`SPEC 9.3`), pressing `Ctrl+H` expands the `Replace with` row (`Replace`, `Replace All`).
2. **Compare PDFs (`src/core/pdf_compare.cpp` and `CompareView` `Ctrl+Shift+D`, `SPEC 9.3.1`):** Compare Document A and Document B using a word/line Myers diff over `PageText` plus a normalized page bitmap/display-list visual diff; render either **Side-by-Side Synchronized View** (locked scroll and zoom across two `DocumentView` canvases) or **Visual Overlay Diff View** (`RectoSuccessBrush` tint for insertions, `RectoDangerBrush` tint for deletions), accompanied by the right **Changes panel** listing every addition, deletion, replacement, and graphic change by page. Expose `recto-cli replace` and `recto-cli compare`.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.3 (`Ctrl+H` Find & Replace expansion) and 9.3.1 (`Ctrl+Shift+D` Compare PDFs side-by-side and overlay diff views) with `D-Search.png`.
**Job:** The user can replace text across a PDF document (literal or regex) and compare two versions of a PDF to spot every text and visual change. Consumer: saved PDF copies (`Replace`) and the `CompareView` canvases and Changes panel.
**Treatment:** Content-stream glyph-run replacement with font width reflow in `src/pdf/text_replace.cpp`, and dual-mode (`Side-by-Side` + `Visual Overlay`) comparison in `src/core/pdf_compare.cpp`. Cheaper substitute that fails the checkpoint: painting a white box with overlay text instead of updating the content stream on Replace, or comparing plain text only with no quad highlights or visual diff.
**Chrome:** consume the §3 `FindCard` for `Ctrl+H`, `RectoSuccessBrush` and `RectoDangerBrush` tokens for diff quads, and the `DocumentView` presentation canvas for synchronized side-by-side scrolling.

**Build order:**

1. Implement `src/pdf/text_replace.cpp` (literal and regex content-stream text replacement with glyph advance reflow and font encoding update) and `src/core/pdf_compare.cpp` (word/quad Myers diff + visual pixel diff), plus `recto-cli replace <in.pdf> --find <pat> --replace <str> [--regex] --out <out.pdf>` and `recto-cli compare <a.pdf> <b.pdf> [--json]`. Done when: `TextReplace.*` replaces strings and regex dates in a corpus PDF and `PageText` readback of the output confirms the replaced text at valid coordinates, and `PdfCompare.*` identifies exact inserted, deleted, and modified spans between two document revisions under ASan.
2. Wire the `Ctrl+H` Replace row in `FindCard` and build the `CompareView` (`Ctrl+Shift+D`, Side-by-Side synchronized scroll/zoom toggle, Visual Overlay mode, and Changes side panel). Done when: clicking a change in the Changes panel jumps both canvases to the diff quad and `Ctrl+H` `Replace All` updates the document preview and saves cleanly.

- [ ] `src/pdf/text_replace.cpp` (literal + regex content-stream replacement) and `src/core/pdf_compare.cpp` (text + visual diff) with `recto-cli replace` and `recto-cli compare`. Done when: `recto-tests.exe --gtest_filter=TextReplace.*:PdfCompare.*` passes under ASan. Cheaper substitute: plain-text diff without page quad coordinates.
- [ ] `Ctrl+H` Find & Replace UI and `Ctrl+Shift+D` Compare PDFs view (Side-by-Side + Overlay + Changes panel). Done when: display-session tests and screenshots verify synchronized scrolling, diff quad highlights, and `Ctrl+K` reachability.
- [ ] Commit: `text: add content-stream Find & Replace (Ctrl+H) and Compare PDFs view (Ctrl+Shift+D)`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextReplace.*:PdfCompare.*` passes under ASan, and display-session screenshots verify the `Ctrl+H` Replace card and `Ctrl+Shift+D` Compare PDFs view.

## Verification

- M3 text and OCR obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `Ctrl+F` and `Ctrl+H` find and replace across multi-hundred-page PDFs without blocking scrolling, scanned and vector-outlined PDFs become selectable and searchable via built-in OCR, and `Compare PDFs` highlights every text and graphic change.
- `recto-tests` (`PdfText.*`, `TextSelection.*`, `Search.*`, `Ocr.*`, `TextReplace.*`, `PdfCompare.*`) green under ASan.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
