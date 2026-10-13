---
schema_version: 1
id: selection-and-search
domain: 03-text
status: active
title: "TODO-01 -- Text Extraction, Selection and Search"
track: M3
---

# TODO-01 -- Text Extraction, Selection and Search

> **Goal:** Page text becomes usable: extracted once per page on the actor, selectable with mouse and keyboard over the rendered page, copyable faithfully, and searchable with the SPEC find card, hit highlighting and next and previous that never miss a hit in the corpus text set (M3's done-when).

> **Current state:** Measured 2026-10-13. No text code exists. D01 provides the actor that will build stext beside display lists; D02 T02 §3 defines the page-geometry model selection and highlights anchor to; D02 T03 §2 provides the panel host find lives in. The behavior is fixed by SPEC 9.3 (find card, result list, hit colours, selection toolbar and context menu), the search brushes in SPEC section 4, F3 keys in section 10, and the plan's text writers (section 5, `fz_new_stext_page_from_page`) whose extraction this file shares with D04 T02 §4.

## Inputs

- `design/SPEC.md` sections 4 (`RectoSearchHitBrush`, `RectoSearchHitCurrentBrush`, `RectoTextSelectionBrush`), 9.3, 10, 11 (selection announced to screen readers); board `design/screenshots/D-Search.png`, with `explorations/B-Search.png` for the Reading room inspector form.
- `docs/plans/2026-10-13-recto-foundation.md` sections 4.2 rule 5 (stext pages move between threads, never shared), 5 (text extraction), 7 (M3).
- -> XREF: D02 T02 §3 -- the geometry model whose page mapping §2 here renders selection over; mapping corrections flow back there, never a second mapping here.
- -> XREF: D02 T03 §2 -- the panel host whose card and inspector forms §3's find surface fills per preset.

## Outcome

- Selecting text on any corpus page highlights exactly the glyphs chosen, copies the same characters MuPDF extracts, and offers Copy, Find and page-link actions where the SPEC puts them.
- Find reports true counts, lists results with context, walks hits in order across pages, and highlights current versus other hits in the SPEC brushes.
- Scanned (image-only) pages behave honestly: no selectable text, find reports zero, OCR stays a named after-v1 item (plan section 2).

**Adjacency:** list=applicable @ D03 T01 §3; document=not-applicable (no printed output); settings=not-applicable (match case and whole word are find-session state on the card itself, owned in section 3, not app settings); reporting=not-applicable (hit counts live on the find card); notifications=not-applicable (none originate here); permissions=not-applicable (copy respects no role model in a single-user viewer); audit=not-applicable (selection is transient view state); exchange=applicable @ D03 T01 §2; reverse=not-applicable (clearing a selection or closing find is view state, built into sections 2 and 3)

**Adjacency rationale:** The list surface is the find results list and the exchange surface is the clipboard (copy out, with find-from-selection as its inward twin), both owned inside this file. PDF-to-text export as a file is a conversion, owned by D04 T02 §4 over the same extraction built in section 1, and is deliberately not claimed here.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Stext extraction and the text model | D01 T02 §1 | [ ] |
| 2 | §2 | Selection rendering, copy and actions | §1, D02 T02 §3 | [ ] |
| 3 | §3 | Find engine and the find card | §1, D02 T03 §2 | [ ] |

## 1. Stext extraction and the text model

The actor builds an `fz_stext_page` per page on demand, cached beside its display list, and hands the core an immutable page-text model: blocks, lines and character quads in page space, reading order as MuPDF reports it, plus the plain-text serialization other consumers share (clipboard in §2, D04's text writer, D03 T02's UIA provider). Bridge additions follow D01 T01's status pattern; the model crosses to the UI side as Recto types, built once and read many times (plan rule 5: built on the actor, then moved, never shared mutably).

**Phase:** 3
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/bridge/stext.c`: build stext for a page on the owner's clone, walk it into flat core structs (quad, unicode codepoints, block and line breaks), with the dehyphenation and spacing choices MuPDF's options expose recorded as constants. Done when: extracted text for the `tests/corpus/text/` set matches the expected strings in `recto-tests` character for character.
2. Cache extracted pages on the actor with the display-list cache's lifetime rules, and expose `PageText` lookup by page index through `DocumentHandle`. Done when: repeated selection and find over one page extract once (actor telemetry in tests), and closing the document frees the cache (ASan clean).

- [ ] Bridge stext walk into `PageText` core types. Done when: multi-column and rotated corpus pages extract in the order their fixtures state. Cheaper substitute: OCR-style line guessing or a text dump with no geometry, which selection cannot use.
- [ ] Actor-side extraction cache. Done when: a 500-page document searched end to end holds text memory within a stated bound (cache eviction quoted in tests).
- [ ] Commit: `text: stext extraction into a cached PageText model`

**Test checkpoint:** `recto-tests --gtest_filter=Text.*` passes under ASan over the full text corpus with exact-match fixtures (quote files and character counts).

## 2. Selection rendering, copy and actions

Selection lives on the presentation layer as an overlay in page space: pointer down, drag and keyboard extension select by character quad (with word and line snapping on double and triple click as stock readers do), rendered with `RectoTextSelectionBrush` under the page's colour mode in a way that stays visible in all three modes. SPEC 9.3's floating selection toolbar (Copy, Find, Link) and the full context menu (copy, find, select all on page, copy page link) act on it; copy puts the extracted characters on the clipboard with newlines where lines break; selection changes announce through UI Automation (full provider in D03 T02 §1, the announcement hooks land here).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.3 with `design/screenshots/D-Search.png` (selection and its toolbar on the page).
**Job:** The user can take text out of a document accurately and act on what they selected. Consumer: the clipboard's next paste, find (§3 seeded from selection), and page links pasted elsewhere that Recto reopens at the page.
**Treatment:** Quad-precise overlay selection over §1's model, rendered by the presentation thread with the page. Cheaper substitute that fails the checkpoint: line-rectangle selection that copies whole lines the user did not choose, or pixel-space selection that breaks at zoom.
**Chrome:** consume §1's `PageText`, D02 T02 §3's geometry, the SPEC dark selection toolbar shape and stock context menu styling. Do not invent a second text-geometry mapping for hits (§3 reuses this one).

**Build order:**

1. Hit-testing and selection state: point to character through quads at any zoom, rotation and layout (including two-page), drag autoscroll at edges, keyboard selection with the caret anchored for UIA. Done when: scripted selections on corpus pages at three zooms and two rotations select exactly the fixture substrings.
2. Rendering and actions: selection quads composited per colour mode, the floating toolbar positioned above the selection (flipping below at the top edge), context menu, clipboard write with readback, copy page link producing a link Recto itself resolves on paste-open (file plus page), Find seeding §3. Done when: paste into a plain editor yields the fixture string byte for byte, and the announcements fire (observed through an automation listener in tests).

- [ ] Quad-precise selection across zoom, rotation and layouts. Done when: rotated-page selection copies in reading order, not visual order. Cheaper substitute that fails the checkpoint: selecting by rendered pixels with no reading-order model.
- [ ] Selection toolbar, context menu, clipboard and page-link round-trip. Done when: a copied page link pasted into the command box (D02 T03 §4) opens the file at that page.
- [ ] Commit: `text: selectable pages with copy, find-from-selection and page links`

**Test checkpoint:** screenshots against `D-Search.png` in findings, scripted substring selections exact at three zooms, and clipboard readback quoted byte-identical for five corpus selections.

## 3. Find engine and the find card

Find runs over §1's text model for the whole document (actor-extracted page by page with progress), honoring Match case and Whole word, reporting the true total and per-page distribution the SPEC card shows ("2 of 14", "4 pages", up to six context results plus Show all). Hits render with `RectoSearchHitBrush`, the current with `RectoSearchHitCurrentBrush` and on-accent text; next and previous (Enter, F3, Shift+F3) walk in reading order, scrolling and switching pages as needed. The card hosts at top-right of the canvas in Focus and Classic, and as the inspector Search tab in Reading room with filmstrip hit marks (SPEC 9.3, `B-Search.png`).

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.3 with `D-Search.png`, and `explorations/B-Search.png` for the inspector form and filmstrip marks.
**Job:** The user can find every occurrence of a string in the document and walk them in order. Consumer: navigation only; find changes no document state, and closing it clears highlights.
**Treatment:** Search over the shared `PageText` model with incremental per-page extraction, cancellable when the query changes. Cheaper substitute that fails the checkpoint: searching only loaded pages, or a find that blocks the UI extracting a 500-page document up front.
**Chrome:** consume the panel hosting from D02 T03 §2, the search brushes from tokens, and §2's quad rendering for highlights. Do not invent a second results list or highlight path.

**Build order:**

1. Find engine in `recto-core`: query over `PageText` with case and whole-word rules over Unicode text, normalization choices recorded (what counts as a word boundary, how ligatures and soft hyphens match), results as (page, quad range) in reading order. Done when: the text corpus verification (D06 T01 §2's expected hit tables) matches exactly, including zero-hit and whole-word edge fixtures.
2. The card and inspector forms with result contexts (match bolded), counts, per-page summary, walk keys, filmstrip marks in Reading room, and Esc restoring focus to the page. Done when: a driven session finds, walks and closes exactly as `D-Search.png`, keyboard only.

- [ ] Engine with recorded normalization rules and exact corpus counts. Done when: changing the query mid-extraction cancels cleanly with no stale hits painted. Cheaper substitute that fails the checkpoint: byte search over extracted dumps that cannot map back to quads.
- [ ] Find card plus inspector form and filmstrip marks. Done when: the surface-completeness account covers every SPEC 9.3 element in both forms.
- [ ] Commit: `text: find with true counts, highlights and the SPEC find card`

**Test checkpoint:** `recto-tests --gtest_filter=Find.*` green over the text corpus with exact hit tables (quote queries and counts), and driven screenshots against both search boards in findings. M3's search done-when ("finds every hit in the corpus text set") closes on this evidence.

## Verification

- Selection exactness, clipboard fidelity and find counts proven on the text corpus, not sampled impressions.
- Image-only pages: honest no-text behavior in selection and find, with OCR remaining the named after-v1 item.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
