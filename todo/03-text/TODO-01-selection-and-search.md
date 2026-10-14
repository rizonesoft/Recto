---
schema_version: 1
id: selection-and-search
domain: 03-text
status: active
title: "TODO-01 -- Text Extraction, Built-in OCR, Selection and Search"
track: M3
---

# TODO-01 -- Text Extraction, Built-in OCR, Selection and Search

> **Goal:** Give every page (digital or scanned) one shared `PageText` model in `recto-pdf` and `recto-core`: native structured text extraction from content streams and `/ToUnicode` maps, built-in `Windows.Media.Ocr` recognition for scanned pages, quad-precise pointer and keyboard selection with the SPEC floating toolbar and context menu, and non-blocking `Ctrl+F` search with true counts across multi-hundred-page documents.

> **Current state:** Measured 2026-10-14. No text or OCR code exists yet. `D01 T02` provides the content-stream interpreter, DirectWrite font loader and `DocumentActor`; `D02 T02` provides the page coordinate mapping (`§3`); `D02 T03` provides the panel hosting contract (`§2`). The UI boards are `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png` (Reading room inspector search tab and filmstrip hit markers), governed by `design/SPEC.md` section 9.3.

## Inputs

- `design/SPEC.md` sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`, `RectoTextSelectionBrush`), 9.3 (Search and selection), 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR language), 10 (`Ctrl+F`, `F3`, `Shift+F3`), 11 (selection announced to screen readers).
- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`PageText`), 5 (Searchable PDF OCR and PDF to text), 6.3 (`PageText` shared across selection, search, OCR and UIA), 7 (M3 done-when).
- -> XREF: D02 T02 §2 -- selection and search highlights composite over the page geometry and tile canvas built in `D02 T02`.
- -> XREF: D02 T03 §3 -- the Reading room Search inspector tab and filmstrip hit badges plug into the panel host established in `D02 T03`.
- -> XREF: D04 T03 §1 -- `DocxWriter` (PDF to Word) consumes `PageText` (including OCR-populated `PageText` on scanned pages) from §1 and §4 here.

## Outcome

- `recto-pdf` extracts `PageText` (`Blocks -> Lines -> Spans -> Chars` with quad bounding boxes in page points, font metadata, and reading-order clustering) on the `DocumentActor` thread, and `recto-cli text` and `recto-cli find` expose it from the command line.
- Selecting text on a page paints `RectoTextSelectionBrush` over character quads, shows the dark floating toolbar (`Copy`, `Find`, `Link`) above the selection and the full context menu on right-click (`Copy`, `Find`, `Select all on page`, `Copy page link`), and announces selection changes to screen readers.
- `Ctrl+F` opens the find card (top-right in Focus/Classic, Search inspector tab with filmstrip markers in Reading room), searches all pages off the UI thread without stalling scroll, highlights matches in `RectoSearchHitBrush` and the current match in `RectoSearchHitCurrentBrush`, and supports `Match case` and `Whole word`.
- Scanned pages (pages with zero extractable text and page-covering images) are recognized on the background `RenderPool` via Recto's C++20 image preprocessor + `Windows.Media.Ocr` (`OcrEngine`), populating `PageText` so selection, copy, `Ctrl+F`, UIA screen readers, and downstream converters work seamlessly on scanned PDFs.

**Adjacency:** list=applicable @ D03 T01 §3; document=not-applicable (text export and searchable PDF save are conversion jobs in D04 T02 §4); settings=applicable @ D02 T04 §2; reporting=not-applicable (no reports); notifications=applicable @ D03 T02 §3; permissions=not-applicable (copy permission flags are reported in Info, D02 T03 §3); audit=not-applicable (search history is not persisted); exchange=applicable @ D03 T01 §2; reverse=applicable @ D03 T01 §2

**Adjacency rationale:** The list surface is the find results list (`§3`), exchange is copying selected text and page links to the Windows clipboard (`§2`), and reverse is clearing selection and closing/clearing find (`§2`, `§3`). Screen-reader announcements for selection and find counts are wired through `D03 T02 §3`, and the automatic-OCR and OCR-language toggles live in Settings (`D02 T04 §2`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Native `PageText` extraction model and `recto-cli text` | D01 T02 §3, D01 T03 §1 | [ ] |
| 2 | §2 | Quad-precise text selection, floating toolbar, context menu and clipboard | §1, D02 T02 §3 | [ ] |
| 3 | §3 | Non-blocking find engine and `Ctrl+F` search card | §2, D02 T03 §2 | [ ] |
| 4 | §4 | Built-in OCR pipeline (`Windows.Media.Ocr`) for scanned pages | §1, §3 | [ ] |

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

Implement interactive text selection over `DocumentView` (`src/app` and `src/services`): pointer drag (character-by-character, double-click word, triple-click line/paragraph) and keyboard `Shift+Arrow` selection hit-test against `PageText` quads through `D02 T02 §3`'s page-to-viewport transform, paint `RectoTextSelectionBrush` (accent at 18% light / 22% dark) over the selected quads, and display the SPEC 9.3 dark floating selection toolbar (`Copy`, `Find`, `Link` to copy a `#page=N` reference) above the selection and the full right-click context menu (`Copy`, `Find`, `Select all on page`, `Copy page link`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4 (`RectoTextSelectionBrush`), 9.3 (floating selection toolbar and context menu), 10 (`Ctrl+C`, `Ctrl+A`, `Esc`), 11 (36 epx hit targets in floating toolbar; selection status announced), with `design/screenshots/D-Search.png`.
**Job:** The user can select text on any page, copy it cleanly to the clipboard, search for the selected phrase, or copy a page link. Consumer: the Windows clipboard, the find card (§3), and the UIA text provider (`D03 T02 §1`).
**Treatment:** Hit-testing and painting over `PageText` character quads in page space so zoom and rotation never shift selection bounds. Cheaper substitute that fails the checkpoint: rectangular rubber-band selection that ignores line flow, or losing the selection when zooming.
**Chrome:** consume `RectoTextSelectionBrush`, the dark floating bar tokens (`#1B1A19` / `#2A2826`), and Segoe Fluent Icons (`Copy`, `Find`, `Link`). Do not invent a second selection store separate from `D03 T02 §1`.

**Build order:**

1. Implement selection state and hit-testing (`SelectionModel` in `src/services`) supporting single-click clear, drag char range, double-click word, triple-click line, `Ctrl+A` (select all on current page), and multi-page drag selection, rendered as quad overlays on the presentation surface. Done when: zooming or rotating with active selection preserves the exact selected character range and quads.
2. Build the dark floating toolbar (`Copy`, `Find`, `Link` with 36 epx hit targets) and right-click `MenuFlyout` (`Copy`, `Find`, `Select all on page`, `Copy page link`), wiring `Ctrl+C` and `Copy` to place normalized UTF-8 text on the clipboard and `Find` to open §3's find card seeded with the selection. Done when: selecting across two lines and pressing `Ctrl+C` places the exact text on the clipboard and `Esc` clears selection.

- [ ] Quad-precise pointer and keyboard selection preserved across zoom and rotation. Done when: double-click, triple-click, drag and `Ctrl+A` match expected character ranges in `TextSelection.*`. Cheaper substitute: single-line-only selection or clearing selection on every scroll/zoom tick.
- [ ] Floating selection toolbar (36 epx targets) and context menu with working `Copy`, `Find` and `Link`. Done when: rendered screenshot matches `D-Search.png` selection treatment and clipboard readback matches the fixture string.
- [ ] Commit: `text: add quad-precise text selection, floating toolbar and context menu`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextSelection.*` passes, and display-session screenshots verify the floating selection toolbar and quad highlight against `D-Search.png`.

## 3. Non-blocking find engine and `Ctrl+F` search card

Implement the background find engine in `src/core/search.cpp` (plus `recto-cli find <file.pdf> <query> [--case] [--whole-word]`) and the SPEC 9.3 find surfaces:
- In Focus and Classic presets: the top-right floating card (`D-Search.png`) with query box, `"2 of 14"`, previous/next (`F3` / `Shift+F3`), close (`Esc`), `Match case` and `Whole word` toggles, `"4 pages"` summary, up to 6 context result rows with bolded match text and page number, and `"Show all N results"`.
- In Reading room preset: the same search view hosted in the right inspector's `Search` tab (`explorations/B-Search.png`) with hit markers on the bottom filmstrip thumbnails.
- On the page canvas: all matches highlighted in `RectoSearchHitBrush` and the active match in `RectoSearchHitCurrentBrush` with `RectoOnAccentBrush` text.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`), 9.3, 10 (`Ctrl+F`, `F3`, `Shift+F3`, `Esc`), with `design/screenshots/D-Search.png` and `design/screenshots/explorations/B-Search.png`.
**Job:** The user can find every occurrence of a word or phrase across a large PDF immediately without stalling scrolling, see each match in context, and jump between hits. Consumer: the document view scrolls to the current match and paints hit quads; `D06 T01 §2` verifies the find tables.
**Treatment:** Incremental background search across pages starting from the current visible page outward, cancelling stale queries via `CancelToken` as the user types, with recorded Unicode case-folding, ligature (`fi` matching `"fi"`), and soft-hyphen normalization rules. Cheaper substitute that fails the checkpoint: searching only already-rendered pages or blocking the UI thread while scanning a 500-page PDF.
**Chrome:** consume `RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`, and the shared search view-model across both the floating card and the Reading room inspector tab. Do not duplicate search state between presets.

**Build order:**

1. Implement `src/core/search.cpp` (Unicode case-folding, ligature and soft-hyphen normalization, `Match case`, `Whole word`, snippet extraction with bold spans, and `CancelToken` cancellation) and `recto-cli find`. Done when: `Search.*` tests verify exact hit counts, page counts, and snippets across multi-page, ligature, and hyphenated fixtures under ASan.
2. Build the `FindCard` / Reading room `Search` inspector tab and filmstrip hit markers, wire `Ctrl+F`, `F3`, `Shift+F3` and `Esc`, and render hit quads on the presentation canvas. Done when: searching a 200-page PDF updates results progressively while scrolling stays at 60 fps, and both Focus (`D-Search.png`) and Reading room (`B-Search.png`) surfaces match their boards.

- [ ] Background find engine with ligature/hyphen normalization, `Match case`, `Whole word`, and `recto-cli find`. Done when: `recto-tests.exe --gtest_filter=Search.*` passes under ASan. Cheaper substitute: `std::string::find` on ASCII bytes that misses `fi` ligatures, non-ASCII case folding, and line-break hyphens.
- [ ] `Ctrl+F` floating card, Reading room Search inspector tab, filmstrip hit markers, and page hit highlights (`RectoSearchHitBrush` / `RectoSearchHitCurrentBrush`). Done when: screenshots match `D-Search.png` and `B-Search.png` and rapid typing cancels prior passes cleanly.
- [ ] Commit: `text: implement non-blocking find engine and Ctrl+F search surfaces`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Search.*` passes under ASan, and display-session screenshots confirm both `D-Search.png` and `explorations/B-Search.png`.

## 4. Built-in OCR pipeline (`Windows.Media.Ocr`) for scanned pages

Implement `src/core/ocr_pipeline.cpp` (`OcrPipeline`) using Windows 11's native `Windows.Media.Ocr` (`OcrEngine`):
1. Detect scanned pages automatically (pages where `PageText` has 0 extractable characters and raster image XObjects cover >= 50% of the page box) or on explicit user command.
2. Render the scanned page at 300 DPI on a `RenderPool` worker, run Recto's C++20 image preprocessor (auto-deskew for small page tilt, contrast normalization, and adaptive Sauvola/Otsu thresholding into an `IBuffer` / `SoftwareBitmap`), and invoke `OcrEngine::RecognizeAsync` for the configured Windows OCR language (`TryCreateFromUserProfileLanguages` or the language selected in Settings).
3. Map recognized `OcrLine` and `OcrWord` bounding rects back to PDF page points (`CharQuad`, `Span`, `Line`, `Block`) and populate the page's `PageText` in `DocumentActor` so pointer selection, `Ctrl+C` copy, `Ctrl+F` search, UIA screen readers (`D03 T02 §1`), `recto-cli ocr <file.pdf>`, `PDF -> Text` (`D04 T02 §4`), and `PDF -> Word` (`D04 T03 §1`) all work on scanned PDFs.

**Phase:** 3
**Needs:** windows-host
**Surface:** yes

**Fidelity:** SPEC sections 9.3 (selection and search work identically once OCR populates `PageText`) and 9.7 (Reading: automatic OCR on scanned pages; Converting: OCR language).
**Job:** A user opening a scanned PDF can select text, copy it, search it with `Ctrl+F`, and read it with a screen reader without installing third-party OCR software. Consumer: `DocumentActor`'s `PageText` cache, `D03 T02 §1` UIA provider, `D04 T02 §4` searchable-PDF writer, and `D04 T03 §1` `DocxWriter`.
**Treatment:** Background `OcrPipeline` with C++20 deskew/contrast preprocessing feeding `Windows.Media.Ocr`, cancellable via `CancelToken`, caching recognized `PageText` per page on the actor. Cheaper substitute that fails the checkpoint: running OCR on the UI thread, skipping image preprocessing on low-contrast scans, or storing OCR text in a separate side-channel that selection and UIA cannot read.
**Chrome:** consume the existing `PageText` overlays and polite status announcement (`D03 T02 §3`); no extra modal chrome blocks reading while background OCR completes.

**Build order:**

1. Implement `src/core/ocr_pipeline.cpp`: scanned-page detector, 300 DPI page rasterization + C++20 deskew and contrast preprocessor, `Windows.Media.Ocr::OcrEngine` invocation with `CancelToken` support, and `OcrResult -> PageText` coordinate mapping. Add `recto-cli ocr <file.pdf> --text` for headless verification. Done when: `Ocr.*` tests run on the scanned raster corpus PDF (`tests/corpus/text/scanned.pdf`) and extract the expected text and word bounding boxes with > 98% character accuracy.
2. Wire automatic and on-demand OCR into `DocumentActor` and `Search` (honoring the `Automatic OCR on scanned pages` and `OCR language` settings from `D02 T04 §2`). Done when: opening `scanned.pdf` in the viewer makes its words selectable by mouse and searchable via `Ctrl+F` with accurate highlight quads over the scanned image.

- [ ] `OcrPipeline` (`src/core/ocr_pipeline.cpp`) with C++20 image preprocessing, `Windows.Media.Ocr` integration, `PageText` quad mapping, and `recto-cli ocr`. Done when: `recto-tests.exe --gtest_filter=Ocr.*` extracts the expected text and word quads from `tests/corpus/text/scanned.pdf` under ASan. Cheaper substitute: returning empty `PageText` on scanned pages and deferring OCR past v1.
- [ ] Seamless viewer selection and `Ctrl+F` search over OCR-recognized scanned pages. Done when: `Ctrl+F` on `scanned.pdf` highlights the recognized words at their exact image coordinates.
- [ ] Commit: `text: add built-in Windows.Media.Ocr pipeline for scanned PDFs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Ocr.*` passes under ASan, and `recto-cli ocr tests/corpus/text/scanned.pdf --text` prints the expected recognized text.

## Verification

- M3 text and OCR obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `Ctrl+F` finds every occurrence across a multi-hundred-page PDF without blocking scrolling, scanned PDFs become selectable and searchable via built-in OCR, and selection/copy/search match `design/SPEC.md` section 9.3.
- `recto-tests` (`PdfText.*`, `TextSelection.*`, `Search.*`, `Ocr.*`) green under ASan.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
