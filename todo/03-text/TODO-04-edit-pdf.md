---
schema_version: 1
id: edit-pdf
domain: 03-text
status: active
title: "TODO-04 -- Edit PDF: Text, Image and Vector Object Editing"
---

# TODO-04 -- Edit PDF: Text, Image and Vector Object Editing

> **Goal:** Recto matches Acrobat Pro's Edit PDF tool: the user opens Edit PDF mode, clicks a paragraph and retypes it on the page with the text reflowing inside its original box in the original embedded font, adds new text as real page content, and selects, moves, resizes, rotates, flips, crops, replaces, recolours, reorders and deletes image and vector objects, with undo and redo per committed edit. Nothing is written to the open viewer document; edits apply to an isolated edit copy, saved through the atomic writers, and removed or replaced content, including rewritten text and destructively cropped pixels, is physically gone from the saved file. Text that cannot be drawn is refused before commit, never written as `?`.

> **Current state:** Measured 2026-10-21. `src/pdf` holds the lexer, object model, filters, crypto, `display_list.cpp` and `document.cpp`; there is no content-stream editor, no object listing, no writer and no `src/app` document view yet. Recto's only text change today is content-stream Find and Replace (`D03 T01 §9`, planned), which rewrites matched `Tj` / `TJ` strings and cannot move, add, retype a paragraph or touch an image or path. `D06 T03 §4` makes the shared interpreter the only content reader, `D06 T03 §7` fixes the Unicode authoring rules and `D06 T03 §5` the save-integrity rules this file must satisfy. The corpus (`tests/corpus/text`, `scripts/build-corpus.py`) has no producer-idiom fixtures for editing.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` decisions D3, D9, D12 (native engine, zero PDF JavaScript, hostile input), D17 (one shell) and D18 (Acrobat Pro parity without weakening a Recto improvement).
- The gates (editing text without destroying neighbours, font swaps, merged paragraphs): `D06 T03 §4`, `§5` and `§7`.
- `design/SPEC.md` sections 8.2 (contextual mode bars and on-canvas selection), 9.9 (dialogs) and 10 (keyboard).
- `AGENTS.md`: authoring never modifies the open viewer document; jobs open their own `recto::pdf::Document`; every write is temp-file-then-replace; true removal strips content-stream objects.
- Existing sections consumed: `D01 T02 §3` and `§4` (interpreter), `D06 T03 §4` (single-interpreter rule), `D03 T01 §9` (Find and Replace shares the font resolver and rewrite engine built here).
- -> XREF: D01 T02 §4 -- the shared content interpreter `EditModel` is built from; no second parser.
- -> XREF: D06 T03 §7 -- the Unicode authoring gate the edit font resolver and Add Text must pass.
- -> XREF: D06 T03 §5 -- removal edits force a full rewrite verified by the save fault-injection suite.
- -> XREF: D03 T01 §9 -- Find and Replace shares the paragraph rewrite engine and edit font resolver.
- -> XREF: D02 T03 §11 -- every `edit.*` command is registered for `Ctrl+K`.
- -> XREF: D02 T01 §8 -- edit sessions save through the shell's Save, Save As and Replace-Original paths.
- -> XREF: D03 T03 §4 -- on-canvas handles and selection chrome reused for image and vector objects.
- -> XREF: D04 T02 §11 -- `font_subsetter.cpp` embeds fallback glyph subsets.
- -> XREF: D08 T02 §6 -- RewriteParagraph.
- -> XREF: D08 T02 §6 -- EditSession apply.
- -> XREF: D08 T02 §7 -- rewrite review.
- -> XREF: D08 T02 §8 -- paragraph model.
- -> XREF: D08 T02 §8 -- font fallback.
- -> XREF: D08 T02 §8 -- full-rewrite save.
- -> XREF: D08 T02 §29 -- AddImage.
- -> XREF: D03 T03 §15 -- flows as threads.
- -> XREF: D04 T02 §19 -- EditModel accepts OCR text.
- -> XREF: D04 T02 §19 -- paragraph model.
- -> XREF: D04 T02 §19 -- EditFontResolver.
- -> XREF: D02 T02 §21 -- ViewSnapProvider.
- -> XREF: D02 T03 §21 -- font resolver for contents page.
- -> XREF: D02 T05 §16 -- staged edit session.
- -> XREF: D01 T03 §10 -- edit verbs follow the registration rule.

## Outcome

- A page opened in Edit PDF mode lists every text paragraph, image and path as an object with an identity that survives edits to other objects, and a retyped paragraph re-reads as exactly the typed text with unchanged neighbours.
- Each text edit keeps the original embedded font for every glyph it contains and embeds a Unicode subset only for glyphs it lacks; an undrawable character is refused with its code point before anything changes.
- Image and vector edits save into a copy in which the deleted or replaced occurrence is absent from the page content, and every resource with no remaining use (including its XObject, when no other occurrence or page uses it) is absent from the file bytes.

**Adjacency:** list=applicable @ D03 T04 §15; document=applicable @ D03 T04 §14; settings=not-applicable (no persistent edit setting exists; fonts, sizes and colours are chosen per edit); reporting=not-applicable (each edit reports its result inline in §11 and §15, with no cross-document report); notifications=applicable @ D03 T04 §11; permissions=applicable @ D03 T04 §11; audit=not-applicable (the undo journal is session-only and no edit history is stored in the PDF); exchange=applicable @ D03 T04 §15; reverse=applicable @ D03 T04 §3

**Adjacency rationale:** The object and paragraph listing is the list surface (`recto-cli edit list`, `§15`, and the hit-test list in `§11`), saving through `SavePipeline` is the document surface (`§14`), the not-editable messages and the `/P` modify-permission refusal are the notification and permission surfaces (`§11`), the CLI verbs are the exchange surface (`§15`), and the per-edit undo journal is the reverse surface (`§3`). No setting is added because every choice is part of the edit itself, and no audit trail is written because the PDF is the record.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Editable object model (`EditModel`) built from the shared interpreter | D01 T02 §3, D01 T02 §4, D03 T01 §1, D06 T03 §4 | [ ] |
| 2 | §2 | Stable paragraph model: lines, paragraphs, columns, table cells and rotation | §1 | [ ] |
| 3 | §3 | Edit session on an isolated copy with a content patch journal and undo/redo | §1, D01 T02 §5, D04 T02 §1, D04 T02 §2 | [ ] |
| 4 | §4 | Edit font resolver: original glyph coverage, installed fonts and Unicode fallback | D01 T02 §2, D04 T02 §11, D06 T03 §7 | [ ] |
| 5 | §5 | Paragraph rewrite: reflow in the original box with per-run state and z-order kept | §2, §3, §4 | [ ] |
| 6 | §6 | Structure tree, marked content and layers kept consistent across text edits | §5, D02 T03 §7 | [ ] |
| 7 | §7 | Add Text box as real page content (not an annotation) | §3, §4 | [ ] |
| 8 | §8 | Image objects: add, replace, move, resize, rotate, flip, crop and export | §3, D01 T01 §8, D04 T02 §5, D04 T02 §9 | [ ] |
| 9 | §9 | Object removal, atomic marked content and full-rewrite save with orphan pruning | §3, §8, D06 T03 §5 | [ ] |
| 10 | §10 | Vector objects: select, move, resize, rotate, flip, recolour, width and delete | §3, §9 | [ ] |
| 11 | §11 | Edit PDF mode bar, object hit-testing and not-editable messages | §1, §2, D02 T02 §4, D03 T03 §3, D03 T01 §7 | [ ] |
| 12 | §12 | Inline on-canvas text editing with caret, selection and per-run formatting | §5, §6, §7, §11, D03 T01 §3 | [ ] |
| 13 | §13 | On-canvas image and vector handles, alignment guides, nudges and z-order | §8, §9, §10, §11, D03 T03 §4 | [ ] |
| 14 | §14 | Edit session commit, save, dirty state and Ctrl+K commands | §11, §12, §13, D02 T01 §8, D02 T01 §12, D02 T03 §11 | [ ] |
| 15 | §15 | `recto-cli edit`: list, text, image and vector verbs | §5, §7, §8, §10, D01 T03 §1 | [ ] |
| 16 | §16 | Producer fixture matrix and Edit PDF round-trip suite | §5, §6, §7, §9, §15, D06 T03 §4, D00 T03 §3 | [ ] |
| 17 | §17 | Spell check in the inline Edit PDF text editor | §12, D03 T01 §14 | [ ] |
| 18 | §18 | Vertical writing (`/WMode 1`) model: detection, metrics and editability classes | §1, §2 | [ ] |
| 19 | §19 | Vertical paragraph rewrite with vertical metrics and embedded fallback fonts | §4, §5, §6, §18 | [ ] |
| 20 | §20 | Vertical text in the inline editor: caret, hit testing and IME | §12, §18, §19 | [ ] |
| 21 | §21 | Convert text to outlines (paths) for a selection or the page, on a copy | §3, §4, §9 | [ ] |
| 22 | §22 | Linked text flows across boxes and pages, stored as article threads | §5, §6, D03 T03 §15 | [ ] |
| 23 | §23 | Join and split text objects: paragraphs, lines and boxes | §5, §6 | [ ] |
| 24 | §24 | Move objects to another page: text, images and paths | §3, §9 | [ ] |
| 25 | §25 | Image filters: brightness, contrast, greyscale and invert on a copy | §3, §8, §9, D04 T02 §9 | [ ] |
| 26 | §26 | Blend modes, opacity and shading objects for text, images and paths | §3, §10 | [ ] |
| 27 | §27 | Arrange and convert commands: outlines, link flow, join, split and move to page | §11, §14, §21, §22, §23, §24, D02 T03 §11 | [ ] |
| 28 | §28 | Appearance panel: image filters, blend mode, opacity and gradient fill | §13, §25, §26, D03 T03 §3, D02 T03 §3 | [ ] |
| 29 | §29 | `recto-cli edit` verbs for outlines, flows, join, split, move, filters and appearance | §15, §21, §22, §23, §24, §25, §26 | [ ] |
| 30 | §30 | Fixtures and round-trip suite for the parity edits | §16, §18, §19, §21, §22, §25, §26 | [ ] |

## 1. Editable object model (`EditModel`) built from the shared interpreter

Editing needs a list of what is on the page: text runs, images, paths and forms with their geometry, graphics state and the exact operator range that drew them. A private content parser that disagrees with the renderer makes edits land on content the user never saw. `EditModel` must be a consumer of the single interpreter (`D06 T03 §4`) and never lex a stream itself. It matches the object layer of Acrobat's Edit PDF and keeps Recto's rule that hostile input is bounded and returns `PdfResult<T>`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/edit_model.h/.cpp` defining `EditObject` (kind `Text`, `Image`, `Path`, `Form`, `Shading`), its bounding box in page points, the source operator range (stream part index and operator ordinals), the occurrence path (the chain of `Do` invocations, with the invoking operator range of each, that reaches the object through nested Form XObjects), the full graphics state at the point of drawing, the marked-content stack with `/MCID`, the optional-content membership and the paint order index. Done when: `EditModel.Build` lists every object of a three-object fixture page with the correct kind, box and operator range.
2. Build the model by subscribing to the interpreter's operator trace (`D01 T02 §3`, `§4`) instead of re-parsing. Done when: `EditModel.SingleInterpreter` fails when the model builder is pointed at a private tokenizer and passes on the shared one.
3. Give every object an `EditId` that is stable within an edit session and carried by patch provenance, not recomputed from the drawing. Done when: after any sequence of committed edits, every object untouched by them has the `EditId` it had before, including identical objects at one position.

- [ ] Define `EditObject` and `EditModel::build(Document&, page_index, CancelToken)` returning `PdfResult<EditModel>` in `src/pdf/edit_model.h/.cpp`. Done when: a split-`/Contents` fixture yields the same objects as the same page in one stream.
- [ ] Record the occurrence path for every object reached through a Form XObject, so two invocations of one form give two distinct `EditObject`s with distinct paths. Done when: `EditModel.FormOccurrences` lists an image inside a form invoked twice as two objects with different occurrence paths.
- [ ] Record per text run the font resource name, size, `Tc`, `Tw`, `Tz`, `TL`, `Ts`, `Tr`, fill and stroke colour with colour space, text matrix, clip stack and alpha. Done when: a fixture with every state value distinct reads each back exactly.
- [ ] Record the marked-content stack (`BDC` / `BMC` tags, property dictionaries, `/MCID`) and the optional-content group per object, resolving `/OC` through the page's `/Properties`. Done when: an object inside an OFF group reports `visible=false` and its group id.
- [ ] Assign `EditId` values once, when a page's `EditModel` is first built in a session, as never-reused 64-bit ids in paint order, and afterwards update the model only by applying each committed `ContentPatch`'s provenance map (`kept`, `moved`, `replaced`, `created`, `deleted`, `copied-on-write`, each naming old and new ids) instead of re-deriving ids from a rebuilt drawing. Identical objects at one position cannot be told apart by any fingerprint, so this is the only rule that keeps them distinct: ids are guaranteed stable within a session and are not stable across sessions or after an outside change to the file, where a fresh model assigns fresh ids and the journal is cleared. Done when: with three identical strings at one origin, an `AddText` inserting a fourth identical string before them keeps the original three ids on the original three objects, a rewrite of the middle one keeps the outer two ids and gives the middle one a `replaced` mapping, and reopening the session assigns new ids and rejects a stored command that names an old id with `StaleObject`.
- [ ] Bound recursion and allocation: form XObject depth, object count per page and operator count come from the interpreter's shared caps (`D06 T03 §4`). Done when: the cap fixtures give a `Truncated` model, not a crash or a private cap.
- [ ] Add `EditModel` as a seventh consumer in `UserClasses.Differential`. Done when: its glyph boxes match the other consumers within 0.01 pt on every differential fixture.
- [ ] Commit: `pdf: add EditModel object listing over the shared interpreter`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditModel.*` passes, and pointing the builder at a private tokenizer makes `EditModel.SingleInterpreter` fail.

## 2. Stable paragraph model: lines, paragraphs, columns, table cells and rotation

A paragraph is not a stream feature, so the editor must infer it from runs, and it must do so the same way every time or an edit lands on the wrong block. Addressing paragraphs by list index merges neighbouring paragraphs when one changes. This section groups `EditObject` text runs into lines and paragraphs with identity that survives edits, detects alignment, rotation, columns and table cells, and refuses to merge across them. It matches Acrobat's paragraph box and keeps Recto's single `PageText` reading order (`D03 T01 §1`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/paragraph_model.h/.cpp` with `Paragraph` (id, lines, bounding quad, alignment, line spacing, indent, rotation, column index, optional table-cell address) built from `EditModel` text objects. Done when: `ParagraphModel.Basic` groups a two-paragraph fixture into exactly two paragraphs.
2. Give a paragraph an id that is stable under edits elsewhere and carried through its own edits by the session. Done when: editing paragraph A leaves paragraph B's id unchanged and A keeps its id after its rewrite.
3. Declare unsupported layouts instead of guessing. Done when: a vertical (`Identity-V`) or `Tr 7` run is reported as `NotEditable` with a reason code; `§18` later narrows the vertical case by extending this classification for its supported subset.

- [ ] Cluster runs into lines by baseline in the run's rotated frame, then lines into paragraphs by gap, indent and alignment, in `src/pdf/paragraph_model.cpp`. Done when: a 90 degree and a 33 degree rotated fixture each give one paragraph per source paragraph.
- [ ] Detect left, right, centre and justified alignment from line start and end offsets, and record the first-line indent and the line pitch. Done when: the four alignment fixtures report the right class and pitch within 0.05 pt.
- [ ] Detect columns using the `PageText` XY-cut result (`D03 T01 §1`) so a paragraph never spans a column gutter. Done when: a two-column fixture yields paragraphs wholly inside one column with distinct column indexes.
- [ ] Detect table cells from ruled lines and aligned text and tag each paragraph with row and column, never merging text across cells. Done when: a 3 by 3 ruled table fixture yields nine paragraphs with correct addresses.
- [ ] Record on each `Paragraph` the operator range of every line (`line_ranges`), which need not be contiguous, and the objects painted between them. Done when: a fixture with paragraph A line 1, an overlapping shape, then A line 2 reports two line ranges and the shape between them.
- [ ] Define `ParagraphId` as the `EditId` of its first run plus a hash of its line count and box, and keep a remap table the session updates on commit. Done when: after a rewrite the session's provenance map resolves the old paragraph id to the new paragraph without re-grouping.
- [ ] Establish the logical text of bidi and complex-script runs (Arabic, Hebrew, Indic) only from evidence that states it: the `/ActualText` of the enclosing marked content when present, otherwise the `ToUnicode` text of the show operators taken in content-stream order, never recovered from glyph positions. Then verify each candidate by shaping, not by character order: shape the candidate logical string with DirectWrite using the run's own embedded font program (bidi levels from UAX #9, clusters from the shaper), map each cluster to its glyphs, and accept the candidate only when the shaped glyph sequence and advances reproduce the drawn glyph ids and positions of the original run within 0.05 pt. Because the comparison is per cluster, a reordered Indic vowel sign and an Arabic ligature each count as one cluster. When the embedded font lacks the layout tables to shape the script, or shaping cannot be run for any reason, the comparison cannot be made and the paragraph is `NotEditable("cannot verify order")`. A single-direction RTL run also tries the reversed stream order as the one other candidate, accepted only if it alone passes. The paragraph is `NotEditable("ambiguous order")` when no candidate or more than one passes, and always when the run mixes directions with positions as the only evidence. Done when: `ParagraphModel.BidiEvidence` accepts a logical-order Arabic fixture, a mixed Arabic, digit and punctuation fixture with an independently specified `/ActualText`, an Arabic ligature (lam-alef) fixture and a Devanagari `कि` fixture (vowel sign drawn left of the consonant) whose expected logical strings are written in the test source, accepts a visual-order pure-Arabic fixture through the reversed candidate, refuses a visual-order mixed-direction fixture with no `/ActualText`, and refuses a fixture whose font lacks the layout tables, with no mutation in either refused case.
- [ ] Commit: `pdf: group text runs into stable paragraphs with alignment, columns and cells`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ParagraphModel.*` passes, and merging two paragraph fixtures by gap threshold change makes `ParagraphModel.NeverMergeAcrossCells` fail.

## 3. Edit session on an isolated copy with a content patch journal and undo/redo

AGENTS.md forbids modifying the open viewer document, and `D03 T01 §9` already runs on an isolated instance. This section builds the shared transaction layer all edit kinds use: an `EditSession` that owns its own `recto::pdf::Document` opened on the source file, applies edits as reversible content patches, supports undo and redo per committed edit, and hands the result to a writer. It matches the edit transaction of Acrobat's Edit PDF and keeps Recto's isolation rule and `PdfResult<T>` contract.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/edit_session.h/.cpp`: `EditSession::open(path)` loads its own `Document`, `apply(EditCommand)` returns `PdfResult<EditResult>`, `undo()` and `redo()` restore exact prior bytes of the touched streams. Done when: `EditSession.UndoRedo` restores byte-identical content streams after each undo.
2. Express every edit as a `ContentPatch` (stream part, operator range, replacement bytes, new resources, and the provenance map of `EditId` changes defined in `§1`). Done when: `EditSession.PatchOnly` shows a text edit changes only the patched range and the resource dictionary.
3. Isolate shared forms: an edit to an object reached through a Form XObject patches a private copy (copy-on-write) of that form and of every enclosing form on its occurrence path, and repoints only the selected invocation's `Do` to the copy. Done when: `EditSession.FormCopyOnWrite` edits one of two invocations of a form (and one nested two levels deep) and the other invocation renders and reads back unchanged.
4. Define commit semantics: an edit is committed when the engine returns success, and a failed edit changes nothing. Done when: an injected failure mid-apply leaves the session identical to before.

- [ ] Author `EditCommand` variants (`RewriteParagraph`, `AddTextBox`, `MoveObject`, `ResizeObject`, `RotateObject`, `FlipObject`, `CropImage`, `ReplaceImage`, `Recolour`, `SetLineWidth`, `Reorder`, `Delete`) as plain data in `src/pdf/edit_command.h`. Done when: each variant serialises to and from a stable JSON form for `§15`.
- [ ] Keep the undo journal as inverse patches with the structure-tree and resource changes they made, bounded by a configurable entry count and a byte budget. Done when: 200 edits of a 5 MB page undo and redo in reverse order within the budget.
- [ ] Require a clean document before entry: if the tab holds unsaved changes (annotations, form values, bookmarks, metadata), the entry flow in `§14` asks the user to save them or discard them, and the session opens only from the file on disk afterward, so no earlier unsaved work can be silently lost on the later swap. Done when: `EditSession.OpensCleanFile` shows an unsaved annotation is either in the opened file or explicitly discarded, never dropped without a choice.
- [ ] Open the session from a snapshot of the saved file, never the viewer's `DocumentActor` instance, and run all work on a job thread. Done when: ASan with `EditSession.IsolatedFromViewer` shows no shared mutable state and the viewer's `DisplayList` for the page is untouched until swap.
- [ ] Make edits idempotent: applying a rewrite whose result equals the current text produces no patch. Done when: three identical rewrites leave the content stream byte-identical to one.
- [ ] Reject an edit when the file's `/Encrypt` permissions forbid modification and report `PermissionDenied` naming the flag. Done when: a permission-restricted fixture refuses the edit and the file is unchanged.
- [ ] Commit: `pdf: add EditSession with reversible content patches`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditSession.*` passes under ASan, and skipping the inverse-patch capture makes `EditSession.UndoRedo` fail.

## 4. Edit font resolver: original glyph coverage, installed fonts and Unicode fallback

Typing into an embedded subset font fails in three ways: swapping the whole run's font when one glyph was missing, inferring coverage from `/Widths` instead of the font program, and writing `?` for missing characters. This section decides, per glyph, whether the original font can draw it and otherwise picks a fallback, using the font file's own `cmap`. It also supplies the installed-font list for the font picker and enforces embedding rights. It matches Acrobat's font handling and keeps Recto's rule that no path writes `?`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/edit_fonts.h/.cpp` with `EditFontResolver::cover(run_font, text)` returning per-character `Original`, `FallbackFont(name)` or `Unsupported(U+XXXX)`. Done when: a subset-font fixture reports the present characters as `Original` and only the missing ones as fallback.
2. Read coverage from the embedded font program (`glyf` / `cmap` for TrueType, CFF charset and the `/Encoding` / `CIDToGIDMap` for others), never from `/Widths`. Done when: a font with a wide `/Widths` table but no glyph for `Z` reports `Z` as missing.
3. Enumerate installed fonts through DirectWrite and apply embedding rights from `OS/2 fsType`. Done when: a restricted-licence fixture font is listed as disabled with the reason and refused if forced.

- [ ] Implement `EditFontResolver::list_installed()` with DirectWrite (`IDWriteFontCollection`), including each face of `.ttc` collections, reporting family, style, whether the outlines are TrueType (`glyf`), and `fsType`. Done when: the list on the reference machine contains Segoe UI and each face of a `.ttc` with its face index.
- [ ] Allow embedding only for fonts whose `fsType` permits it (installable, editable, or preview-and-print for a non-editable use is refused for edits) and list the others disabled with the reason. Done when: `EditFonts.FsType` refuses the bits that forbid embedding and allows installable embedding.
- [ ] Offer only TrueType-outline fonts to the picker for embedding and show OpenType-CFF fonts disabled with "Embedding this font type is not supported yet", since `font_subsetter.cpp` (`D04 T02 §11`) subsets `glyf` fonts. Done when: a CFF-flavoured font is disabled with that text and a `glyf` font is enabled.
- [ ] Subset and embed a fallback font as a Type 0 / `CIDFontType2` with `Identity-H`, a `/CIDToGIDMap` and a `/ToUnicode` CMap using `font_subsetter.cpp`, adding it once per page resource set. Done when: `EditFonts.FallbackEmbed` extracts the typed string exactly and the file has one new font object per used fallback.
- [ ] Choose the fallback by this order: the user-picked font, a font from the same family that covers the character, then the DirectWrite system fallback for that code point. Done when: a Cyrillic character in a Latin subset resolves to a Cyrillic-capable face, named in the result.
- [ ] Refuse a character no installed embeddable font can draw before commit, returning `UnsupportedGlyph` with `U+XXXX`. Done when: a private-use code point returns the error with the code point and nothing is written.
- [ ] Commit: `pdf: resolve edit fonts per glyph with embedding rights and Unicode fallback`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditFonts.*` passes, and reading coverage from `/Widths` instead of the font program makes `EditFonts.CoverageFromProgram` fail.

## 5. Paragraph rewrite: reflow in the original box with per-run state and z-order kept

This is the core of Edit PDF: the user changes a paragraph's text and it reflows inside the box it occupied, in the same font, size, colour and stacking position. Reflowing with the first run's state for every run loses clips, changes stacking and makes edits drift on repeat. The rewrite replaces only the paragraph's operator range, emits graphics and text state per run, and is idempotent. It matches Acrobat's paragraph edit and keeps Recto's per-glyph font retention (`§4`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/paragraph_rewrite.h/.cpp`: `rewrite(session, paragraph_id, new_text_runs)` producing a `ContentPatch`. Done when: `ParagraphRewrite.Basic` retypes one paragraph and `PageText` readback returns the new text with neighbours unchanged.
2. Lay out the new text with the resolved fonts' advances, honouring the original box width, alignment, line pitch, indent, `Tc`, `Tw` and `Tz`, with word-wrap and a hyphenation-free break rule. Done when: shortened text keeps the left edge and the original first-baseline position within 0.05 pt.
3. Place the patch at the original operator ranges so z-order is unchanged: write each new line into the original range of the line it replaces, in order, put surplus lines into the last range, and leave an emptied range as `q Q`, so objects painted between lines stay between them. Done when: the interleaved fixture (line 1, overlapping shape, line 2) keeps the shape's paint position relative to every line after a rewrite that adds a line and a rewrite that removes one, and a layout where this cannot be kept returns `NotEditable("interleaved content")` before any mutation.

- [ ] Emit per run its own `Tf`, `Tc`, `Tw`, `Tz`, `Ts`, `Tr`, colour and clip, restoring the state before leaving the range, never inheriting the first run's state. Done when: a four-run fixture with distinct state per run rewrites with each run's state intact.
- [ ] Preserve a run's formatting when text is added inside it and give inserted text the state of the run at the caret. Done when: typing inside a bold run yields bold glyphs with the same font resource.
- [ ] When the new text needs more lines than the box, grow downward without overlapping the next object, and stop with `NoRoom` naming the blocking object when growth would overlap. Done when: a tight-layout fixture returns `NoRoom` and writes nothing.
- [ ] Lay out right-to-left runs by shaping the logical string with DirectWrite for the resolved font, emitting the shaped glyph ids in visual order, and refusing before mutation when the font lacks a required joining form or the paragraph was marked ambiguous in `§2`. Done when: an Arabic paragraph round-trips in logical order through save and extraction.
- [ ] Reflow within the original rotation frame so a rotated paragraph stays at its angle. Done when: the 90 degree and 33 degree fixtures keep their baseline angle within 0.01 degrees.
- [ ] Prove idempotence: rewriting a paragraph with its own current text is a no-op and ten rewrites of the same edit equal one. Done when: the content streams of both runs are byte-identical.
- [ ] Commit: `pdf: rewrite paragraphs with reflow, per-run state and stable z-order`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ParagraphRewrite.*` passes under ASan, and reusing the first run's state for all runs makes `ParagraphRewrite.PerRunState` fail.

## 6. Structure tree, marked content and layers kept consistent across text edits

A text edit that drops its `BDC` / `EMC` wrapper, reuses an `/MCID` or leaves `/ActualText` stale breaks accessibility and copy-paste, and an edit inside a hidden layer must stay in that layer. Writing text outside the original marked content loses the Unicode mapping. This section keeps the structure tree, marked content, `/ToUnicode` and optional-content membership consistent for every rewrite and add. It matches Acrobat's tagged-aware editing and keeps Recto's layer awareness (`D02 T03 §7`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Make `paragraph_rewrite.cpp` keep the original `BDC` / `EMC` and `/MCID` of the rewritten range. Done when: `ParagraphRewrite.MarkedContent` shows the same tag, property list and `/MCID` after the edit.
2. Update the structure tree when line counts or text change. Done when: a tagged fixture's `/StructTreeRoot` leaf for the paragraph still references its `/MCID` on the page and its `/ActualText` (if any) equals the new text.
3. Keep optional-content membership. Done when: an edit to a paragraph in an OFF layer stays in that layer and renders hidden.

- [ ] Preserve and update `/ActualText` on the enclosing marked-content property dictionary or structure element, and drop it if it now disagrees with the text. Done when: extraction through structure and through `ToUnicode` return the same string.
- [ ] Allocate a fresh `/MCID` for each additional marked-content sequence created by a split, unique per page, and register it in the structure tree and `/ParentTree`. Done when: the saved file passes the `/ParentTree` consistency check with no duplicate `/MCID`.
- [ ] Never move text in or out of a `/Artifact` span unless the user edits it, and keep `/Artifact` text as an artifact. Done when: a header/footer artifact fixture is edited with its `/Artifact` tag intact.
- [ ] Keep the `/ToUnicode` CMap of any font whose glyph usage changed complete for all glyphs used, so copy returns the visible text. Done when: `PageText` of the saved page equals the typed text for every fixture.
- [ ] Commit: `pdf: keep structure tree, marked content and layers consistent across text edits`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ParagraphRewrite.MarkedContent:ParagraphRewrite.StructTree:ParagraphRewrite.Layers` passes, and dropping the `/MCID` carry-over makes `ParagraphRewrite.StructTree` fail.

## 7. Add Text box as real page content (not an annotation)

Acrobat's Add Text creates page content, not a FreeText annotation, so the text prints, extracts and survives flattening. Recto's `FreeText` annotations (`D03 T03 §2`) stay available for commenting; this section creates true content with a chosen font, size, colour and alignment. An add-text path that writes `?` for non-Latin text or uses an annotation is not acceptable. It must use the resolver in `§4` and the idempotent session in `§3`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/add_text.h/.cpp`: `add_text_box(session, page, rect, runs, style)` writes a `BT ... ET` block appended to the page's top content stream part in its own `q ... Q`. Done when: `AddText.Basic` adds a box and `PageText` returns it at the rect.
2. Reuse `EditFontResolver` for every character and embed subsets once per page. Done when: a mixed Latin, Greek and CJK string adds with one embedded font per script and extracts exactly.
3. Wrap the box in a marked-content sequence with a fresh `/MCID` and, on tagged files, a `/P` structure element. Done when: a tagged fixture gains one `/P` leaf.

- [ ] Implement alignment (left, centre, right, justified), line pitch and word wrap inside the rect, and fixed-box versus auto-grow mode. Done when: the four alignments lay out identically to the paragraph rewrite engine on the same text.
- [ ] Apply font family, size, fill colour, and bold and italic through the chosen face (never synthetic emboldening). Done when: choosing a bold face selects the bold file and `fsType` is respected.
- [ ] Choose the content stream insertion so the box sits above existing content but inside the page's clip and below no `q`/`Q` imbalance. Done when: the `Q` balance of the page is unchanged after the add.
- [ ] Refuse an undrawable character before commit with its code point. Done when: a private-use code point returns `UnsupportedGlyph` and the page is unchanged.
- [ ] Commit: `pdf: add text boxes as real page content`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AddText.*` passes, and making the writer emit a `FreeText` annotation makes `AddText.IsPageContent` fail.

## 8. Image objects: add, replace, move, resize, rotate, flip, crop and export

Image editing in Acrobat covers every direct manipulation of a placed image. Placement lives in the `cm` that precedes `Do`, the pixels live in the XObject, and crop is a clip, so most operations are matrix or clip patches and never re-encode pixels. Re-encoding images on move loses masks. This section keeps `/SMask`, `/Mask` and `/Decode` intact and never recompresses on a geometry change.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/image_edit.h/.cpp` with `move`, `resize(aspect_lock)`, `rotate`, `flip_h`, `flip_v` and `crop` implemented as a replacement of the object's `cm` and, for crop, an added clip `re W n` inside its `q ... Q`. Done when: `ImageEdit.Geometry` changes only the `cm` and clip bytes and the XObject object number and bytes are untouched.
2. Add `replace_image(session, edit_id, source_file)` creating a new XObject (reusing `DCTDecode` pass-through for JPEG per `D04 T02 §5`) at the same placement. Done when: the replaced image renders at the same box with the new pixels.
3. Add `add_image(session, page, rect, source_file)` using the same encoder. Done when: a PNG with alpha is added with an `/SMask` and extracts as the same pixels.

- [ ] Preserve `/SMask`, `/Mask`, `/Decode`, `/ColorSpace`, `/Interpolate` and the image's `/OC` on geometry edits. Done when: a masked-image fixture renders pixel-identically after move, resize and flip, compared with the source XObject unchanged.
- [ ] Implement aspect-lock resize from any of the eight handles, and non-uniform resize when the lock is off, computing the new `cm` in page space from the old one. Done when: a rotated image resized by a corner keeps its angle and ratio within 0.01 degrees.
- [ ] Crop as a clip rectangle in the image's own frame, with a reversible crop (no pixel discarded) and a destructive "Apply crop" that rewrites the image pixels. Done when: the reversible crop is undoable to the original box and the destructive crop reduces the XObject pixel count.
- [ ] Export through the existing lossless extractor (`D04 T02 §9`), writing the original `DCTDecode` bytes as `.jpg` and other images as `.png`, with the `/SMask` as alpha. Done when: `ImageEdit.Export` writes a JPEG byte-identical to the stream data.
- [ ] Handle inline images (`BI ... EI`) by listing them, supporting move, resize and delete, and promoting to an XObject only for replace. Done when: an inline-image fixture moves and deletes correctly with `EI` detection from the shared interpreter.
- [ ] Keep the marked-content wrapper of the image atomic: a `BDC ... EMC` containing the `Do` moves with it. Done when: the structure-tree leaf of the image still resolves after a move.
- [ ] Commit: `pdf: add image object operations without re-encoding`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ImageEdit.*` passes, and re-encoding the pixels on move makes `ImageEdit.Geometry` fail.

## 9. Object removal, atomic marked content and full-rewrite save with orphan pruning

Deleting images in an incremental update leaves the old bytes in the file, and deleting a `Do` can orphan its marked content. A delete or a replace must remove the content and prune what nothing references any more, and `D06 T03 §5` requires a full rewrite for removal. This section implements deletion of any object, atomic removal of its marked-content wrapper, reference-count pruning, and the full-rewrite save. It matches Acrobat's delete and keeps Recto's true-removal rule (the redaction rule in `AGENTS.md`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `delete` to `EditSession` that removes the object's operator range including its enclosing `q ... Q` and `BDC ... EMC` when that wrapper holds only the object. Done when: `EditDelete.AtomicWrapper` leaves no empty `BDC`/`EMC` pair and the `q`/`Q` count balances.
2. Add a resource reference counter that marks an XObject, font, ExtGState, pattern or shading unreferenced after the edit. Done when: deleting the only use of an image marks it for pruning.
3. Make a save containing any delete, replace, text rewrite or destructive crop use the full-rewrite path of `PdfWriter` (`D04 T02 §2`) and write only reachable objects. Done when: the saved file contains no deleted object bytes (scan of the file).

- [ ] Remove the deleted object from the structure tree (`/K` references, `/ParentTree`) and from `/OCProperties` use lists when the object was the only member of a group. Done when: the saved file's structure tree has no dangling `/MCID`.
- [ ] Prune unreferenced objects including `/SMask` children and the thumbnail `/Thumb` for the page, and keep an XObject shared with another page. Done when: a shared-image fixture keeps the image after one page's use is deleted.
- [ ] Choose between incremental and full rewrite per session: only edits that remove nothing (move, resize, rotate, flip, recolour, line width, reversible crop, add text, add image, reorder) may save incrementally; a paragraph rewrite that removes or replaces existing text, delete, replace image and destructive `Apply crop` always rewrite in full, so no earlier revision keeps the old text or pixels. Done when: `EditSave.WriterChoice` asserts the writer for every edit kind, and `EditSave.NoPriorRevisionLeak` saves a text rewrite and a destructive crop and finds the old text and the discarded pixels absent from every byte of the saved file.
- [ ] Run the save-integrity verification of `D06 T03 §5` on the written file: page count, outlines, `/PageLabels`, `/Info`, `/AcroForm`, signatures noted. Done when: a planted writer that drops `/PageLabels` aborts the save.
- [ ] Warn before rewriting a signed document, since a rewrite invalidates its signatures, via the dialog in `§14`. Done when: a signed fixture returns `InvalidatesSignature` and writes nothing until confirmed.
- [ ] Commit: `pdf: remove objects atomically with orphan pruning and full-rewrite save`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditDelete.*:EditSave.*` passes, and skipping the prune pass makes `EditDelete.NoOrphanBytes` fail on a raw-byte scan.

## 10. Vector objects: select, move, resize, rotate, flip, recolour, width and delete

Path and shading objects are the third class Edit PDF exposes. They are edited by changing the colour operators, line width and `cm` that apply to a path; the one exception is geometry normalization, a numeric transform of the path's own coordinates used only when a non-uniform transform is applied to a stroked path, because a single `w` cannot keep an even stroke width under it. Paths are never traced or rebuilt. This section selects a path object as a unit, moves it, changes fill and stroke colour and line width, and deletes it, over `EditModel` and the removal rules in `§9`. It matches Acrobat's path editing and does not edit individual path points (recorded as out of scope).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/vector_edit.h/.cpp` with `move`, `resize`, `rotate`, `flip_h`, `flip_v`, `recolour_fill`, `recolour_stroke`, `set_line_width` and `delete` over a path object's operator range. Done when: `VectorEdit.Basic` changes a fixture rectangle's fill and width and re-renders it with the new values.
2. Recolour by rewriting the colour operator or inserting one at the object start inside its `q ... Q`, honouring the colour space (`DeviceGray`, `DeviceRGB`, `DeviceCMYK`, `ICCBased`, `Separation`). Done when: an ICCBased path recolours to a `DeviceRGB` value without altering the neighbouring path.
3. Handle clip paths, patterns and shadings explicitly. Done when: a clip-only path is `NotEditable` with a reason and a shading fill can be moved and deleted but not recoloured.

- [ ] Group the operators of a compound path (many `m` / `l` / `c` with one paint operator) as one object, and a multi-paint sequence as separate objects. Done when: `VectorEdit.Grouping` selects a compound path as one.
- [ ] Preserve transparency (`/ca`, `/CA`, blend mode through `gs`) when recolouring. Done when: a 50 percent alpha path keeps 50 percent alpha after a recolour.
- [ ] Implement `resize`, `rotate`, `flip_h` and `flip_v` as one decision in `src/pdf/vector_edit.cpp`, taken on the object's complete effective transform `E` (its own `cm` operators composed with the transform inherited from enclosing `cm` operators and Form XObject `/Matrix` values, read from the `EditModel` graphics state) multiplied by the requested transform `T`: when `E*T` is a similarity (uniform scale, rotation, flip) or the path has no stroke, `T` is composed into the object's `cm` (or wrapped in `q T cm ... Q`) with path coordinates and paint operators byte-identical; when `E*T` is non-uniform or skewed and the path is stroked, geometry normalization applies. Normalization emits the object inside its own `q <inverse of the inherited transform> cm ... Q` so the effective transform inside is the identity, with the path coordinates rewritten as final page-space coordinates and `w` set directly, leaving sibling objects and the clip already in force untouched (a `cm` never changes a clip); when the inherited transform is not invertible the edit returns `NotEditable("singular transform")`. Done when: `VectorEdit.TransformMatrix` rotates 33 degrees, flips and uniformly resizes a stroked fixture path and non-uniformly resizes an unstroked one, with the rendered box within 0.05 pt of the expected transform and path operand bytes unchanged, and `VectorEdit.TransformNormalize` resizes a stroked path non-uniformly with the operand bytes changed, the box within 0.05 pt and 2 pt strokes, including a variant whose stroked rectangle sits inside a Form invoked under `2 0 0 1 0 0 cm` with no local `cm` and a variant on a page whose content is already scaled non-uniformly (branch chosen from `E*T`, not from `T` alone), each with page-space expected geometry written in the test source and a neighbouring sibling that must render unchanged.
- [ ] Keep the displayed stroke width when a transform is applied: in the matrix branch divide the requested width by the uniform scale of `E*T` when setting `w`; in the normalization branch set `w` directly (the effective transform inside is the identity), and `set_line_width` on an object whose effective transform `E` is non-uniform first normalizes the geometry the same way and then sets `w`. Done when: a 2 pt stroked rectangle resized non-uniformly, and one inside the non-uniform Form variant of the previous item, show 2 pt horizontal and vertical strokes within 0.05 pt, and a uniformly scaled one shows 2 pt.
- [ ] Reject a recolour of a pattern or shading fill with a clear reason, never silently ignoring it. Done when: `VectorEdit.PatternFill` returns `NotEditable("pattern fill")`.
- [ ] Commit: `pdf: edit vector objects with colour, width, move and delete`

**Test checkpoint:** `recto-tests.exe --gtest_filter=VectorEdit.*` passes, applying the recolour to the whole `q ... Q` group makes `VectorEdit.OnlySelectedPath` fail, and normalizing geometry in the matrix branch makes `VectorEdit.TransformMatrix` fail on its unchanged-bytes assertion.

## 11. Edit PDF mode bar, object hit-testing and not-editable messages

Edit PDF is a mode of the one viewer shell (D17), not a new window. This section adds the contextual `Edit PDF Bar` following the Redaction bar chrome in SPEC 8.2, hit-testing over `EditModel`, the selected-object overlay, and the messages for content that cannot be edited: a scanned page (suggest OCR), Type 3 fonts, clip-only text, vertical text, an OFF layer and a document whose permissions forbid changes. It matches Acrobat's Edit PDF toolbar and keeps Recto's rule that an unsupported edit says why.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.2 contextual mode bars (the Redaction & Sanitize Bar and the Markup & Annotation Bar, `D03 T03 §3`) in layout, `40 epx` height, tokens and floating-bar elevation.
**Job:** The user clicks `Edit PDF`, sees every editable object outlined on hover, clicks one to select it, and gets a plain-language message when the thing they click cannot be edited. Consumer: `§12` and `§13` interactions.
**Treatment:** A contextual bar of icon buttons (`Text`, `Image`, `Path`, `Add Text`, `Add Image`, `Fonts`, `Colour`, `Done`) over a hit-test layer reading `EditModel`. Cheaper substitute that fails the checkpoint: a toolbar that always shows tools but does nothing on a scanned page, or a popup dialog for editing text.
**Chrome:** consume `RectoFloatingBarBackgroundBrush`, `RectoAccentBrush` for selection outlines, Segoe Fluent Icons, `RectoDialogStyle`; no hex values.

**Build order:**

1. Add `src/app/controls/EditPdfBar.xaml/.h/.cpp` and register the mode with the layout presets so it occupies the same slot as the other contextual bars. Done when: entering and leaving Edit PDF mode swaps the bar without moving the page.
2. Add `src/services/edit_hit_test.h/.cpp` mapping a pointer in page space to the topmost `EditObject` or paragraph, with hover outline and selection. Done when: `AppTests.EditHitTest` returns the right object for text, image and path fixtures at several zoom and rotation settings.
3. Add the not-editable classification and its messages. Done when: each class shows its own message once per page without blocking the viewer.

- [ ] Detect scanned pages with the scanned/outlined detector (`D03 T01 §7`) and show "This page is a scanned image. Run OCR to make its text editable." with an `OCR this page` action. Done when: the scanned corpus page shows that message and the action starts OCR.
- [ ] Show distinct messages for Type 3 fonts, text with `7 Tr` or only a clip, vertical writing, text inside an OFF layer and an encrypted file with a modify restriction, each naming the cause. Done when: `AppTests.EditNotEditableMessages` shows the five messages for the five fixtures.
- [ ] Give every icon-only bar button an `AutomationProperties.Name` and a tooltip. Done when: the accessibility inventory (`D03 T02 §3`) finds none missing. Cheaper substitute: relying on tooltips only.
- [ ] Show selection outlines with the accent colour and eight `8 x 8 epx` handles for the selected object (shared with `D03 T03 §4` visuals). Done when: the selected object matches the handle geometry in SPEC 8.2.
- [ ] Keep the viewer responsive: hit-testing reads the `EditModel` snapshot, never calling `Document` on the UI thread. Done when: a 300-page document keeps frame time under 16 ms while hovering. Cheaper substitute: building the model on the UI thread.
- [ ] Commit: `app: add Edit PDF mode bar with hit-testing and not-editable messages`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.EditHitTest:AppTests.EditNotEditableMessages:A11y.EditPdfBar` passes, and routing the scanned page into the text tool makes `AppTests.EditNotEditableMessages` fail.

## 12. Inline on-canvas text editing with caret, selection and per-run formatting

Acrobat edits text in place on the page, not in a popup. This section places a live `TextBox`-style editor over the paragraph with the page's own font metrics, caret, selection, IME composition and a small format flyout (font, size, colour, alignment), committing one undoable edit on blur, `Enter` on a new box or `Ctrl+Enter`. It matches Acrobat's inline text edit and keeps Recto's per-glyph font retention and refusal of undrawable characters (`§4`).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.2 (on-canvas selection, floating popover) and the `FreeText` inline editor of `D03 T03 §4`.
**Job:** The user clicks a paragraph, types, changes font, size, colour or alignment from a flyout, and sees the text reflow on the page as they type. Consumer: `ParagraphRewrite` (`§5`), `AddText` (`§7`).
**Treatment:** A transparent text editing layer over the page using the document's real glyph advances, committing through the `EditSession` per edit. Cheaper substitute that fails the checkpoint: a modal "Edit text" dialog, or live editing with a different font than the file's.
**Chrome:** consume `RectoFloatingBarBackgroundBrush` for the format flyout, `RectoAccentBrush` for the caret box, the font picker list from `§4`.

**Build order:**

1. Add `src/app/controls/InlineTextEditor.xaml/.h/.cpp` bound to a `ParagraphId`, rendering the paragraph's lines at their page positions and the caret from `PageText` quads. Done when: entering a paragraph shows the original text exactly in place.
2. Wire typing to a debounced preview using the layout from `§5` without committing, then commit one `RewriteParagraph` on exit. Done when: `AppTests.InlineTextEdit` commits a single undo entry for a typing burst.
3. Add the format flyout with the font picker (`§4` installed list, disabled entries with reasons), size, colour, alignment and line spacing. Done when: choosing a font applies to the selection runs only.

- [ ] Support IME composition, selection with mouse and keyboard, `Home`/`End`, word and line movement, clipboard paste as plain text, and right-to-left caret movement. Done when: composing Japanese text inserts correctly and arrow keys move logically in an Arabic line.
- [ ] Show a character the font cannot draw immediately: when typing or pasting `U+XXXX` with no embeddable font, reject the character and show an inline note naming the code point. Done when: `AppTests.InlineTextUnsupported` rejects it and the buffer is unchanged.
- [ ] Show a font-substitution notice when a typed character falls back to another font, naming that font, and keep the rest of the run in the original font. Done when: typing one Cyrillic letter shows "Cyrillic uses Segoe UI" and the other letters keep the original face.
- [ ] Commit the edit when focus leaves, `Ctrl+Enter` is pressed or another object is selected, and cancel on `Esc` restoring the original. Done when: `Esc` leaves the document unchanged and the dirty flag unset.
- [ ] Add an automation name and tooltip to every flyout control and expose the editor to UIA as an editable text element. Done when: `A11y.InlineTextEditor` finds a name on every control. Cheaper substitute: tooltips only.
- [ ] Commit: `app: add inline on-canvas text editing with per-run formatting`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.InlineTextEdit:AppTests.InlineTextUnsupported:A11y.InlineTextEditor` passes, and opening a popup editor instead makes `AppTests.InlineTextEdit` fail.

## 13. On-canvas image and vector handles, alignment guides, nudges and z-order

Direct manipulation of image and path objects needs the same handle behaviour users know from Acrobat: drag to move, eight handles to resize (`Shift` for aspect lock), a rotation handle, alignment guides to page edges and other objects, keyboard nudges and arrange commands. This section builds the interaction over `§8` to `§10` and the shared handle visuals of `D03 T03 §4`. It keeps Recto's rule that every gesture is one undo entry.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.2 selection box and handles as built in `D03 T03 §4`, and the alignment-guide treatment of the Form Designer (`D03 T03 §9`).
**Job:** The user drags, resizes, rotates, flips, crops, reorders and deletes an image or path object, with guides snapping to edges and neighbours, and nudges by keyboard. Consumer: `ImageEdit` (`§8`), `VectorEdit` (`§10`), `EditDelete` (`§9`).
**Treatment:** A live transform preview on the overlay during the gesture and one committed `EditCommand` on release. Cheaper substitute that fails the checkpoint: committing on every pointer move (hundreds of undo entries), or handles only on images.
**Chrome:** consume `RectoAccentBrush` for the selection box, handles and guides, the floating action popover of `D03 T03 §4`, Segoe Fluent Icons for flip, crop and arrange.

**Build order:**

1. Reuse the `D03 T03 §4` selection overlay for `EditObject` image and path selections with move, 8-handle resize (`Shift` locks aspect) and a rotation handle. Done when: `AppTests.EditObjectGestures` moves, resizes and rotates fixtures and commits one undo entry each.
2. Add alignment guides to page edges, page centre and neighbouring objects with snapping at 4 epx, toggled by `Alt`. Done when: dragging near an edge snaps to it and shows the guide.
3. Add keyboard nudges and arrange commands. Done when: arrow keys move 1 pt, `Shift` 10 pt, `Ctrl+Shift+]` brings to front and the z-order in the file changes accordingly.

- [ ] Implement `Flip horizontal`, `Flip vertical`, `Crop` (drag the crop box, reversible by default) and `Apply crop` in a floating popover. Done when: each action appears as one undo entry and renders as `§8` defines.
- [ ] Implement z-order commands `Bring to front`, `Forward`, `Backward`, `Send to back` by moving the object's operator range among siblings within the same `q ... Q` and optional-content scope, never across a clip group. Done when: a fixture reorders and the render order matches the new order.
- [ ] Implement `Delete` and `Replace image...` (`FileOpenPicker`) with a confirmation-free undoable delete. Done when: `Delete` removes the object and `Ctrl+Z` brings it back identically.
- [ ] Show the vector controls: fill colour, stroke colour and line width in the floating popover using the colour picker of the annotation bar (`D03 T03 §3`). Done when: changing fill updates the page live and commits on close.
- [ ] Give each handle, the rotation handle and every popover icon an automation name and tooltip and make the selection operable with the keyboard alone. Done when: `A11y.EditObjectSelection` finds all names and a keyboard-only test moves, resizes and deletes an object. Cheaper substitute: mouse-only handles.
- [ ] Commit: `app: add on-canvas image and vector manipulation with guides and nudges`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.EditObjectGestures:AppTests.EditGuides:A11y.EditObjectSelection` passes, and committing on every pointer move makes `AppTests.EditObjectGestures` fail on the undo count.

## 14. Edit session commit, save, dirty state and Ctrl+K commands

Edits are unsaved work, so they must light the tab's dirty state, be committed before any save, survive a crash through the recovery snapshots, and swap the viewer onto the saved result only after success. This section also registers every Edit PDF command in the `CommandRegistry` so `Ctrl+K` reaches each one. It builds on `Save` (`D02 T01 §8`) and keeps `AGENTS.md`: the open viewer document is never modified in place.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 9.9 (the Unsaved changes and Replace original dialogs, `D-Dialogs.png`), 8.2 (mode bars) and 10 (keyboard).
**Job:** The user finishes editing, presses `Ctrl+S`, and the saved file contains every edit and no removed content while the original is unchanged on failure. Consumer: `SavePipeline` (`D02 T01 §8`), the tab dirty indicator, and the `Ctrl+K` command box.
**Treatment:** The edit session feeds `SavePipeline`, which writes through `AtomicFileWriter` and swaps the tab onto a fresh `DocumentActor` after verification. Cheaper substitute that fails the checkpoint: saving a still-open text buffer without committing it, or modifying the open viewer document in place.
**Chrome:** consume `RectoDialogStyle`, `DocumentTabSession` dirty indicator and the command box of `D02 T03 §12`.

**Build order:**

1. Make an uncommitted inline text buffer commit (or the save be refused with a reason) at the start of every save, close and print. Done when: typing in a paragraph and pressing `Ctrl+S` saves the typed text (the `D06 T03 §5` rule).
2. Connect `EditSession` to `SavePipeline` so a save runs the chosen writer from `§9` on a job thread, verifies, replaces atomically, then swaps the viewer onto a fresh `DocumentActor`. Done when: `AppTests.EditSave` shows the viewer re-rendering the saved file and the original untouched after a forced failure.
3. Register the commands `edit.enter`, `edit.exit`, `edit.addText`, `edit.addImage`, `edit.replaceImage`, `edit.delete`, `edit.undo`, `edit.redo`, `edit.flipH`, `edit.flipV`, `edit.crop`, `edit.front`, `edit.back`, `edit.font`, `edit.exportImage` with the accelerator `Ctrl+Shift+E` for `edit.enter`. Done when: `Keyboard.Audit` and the command box find all fifteen.

- [ ] On `edit.enter` with a dirty tab, show the SPEC 9.9 unsaved-changes dialog (`Save`, `Discard`, `Cancel`) before the session opens, and open the session from the resulting file on disk. Done when: `AppTests.EditEntryDirtyTab` adds an annotation, enters Edit PDF, chooses `Save`, edits text, saves, and finds both the annotation and the edit in the result; `Cancel` stays out of Edit PDF. Cheaper substitute: opening the session from the stale file and swapping over the unsaved work.
- [ ] Snapshot the edit session into the crash recovery store of `D02 T01 §12` at each committed edit and restore it on relaunch. Done when: killing the app mid-session and relaunching offers the edits back. Cheaper substitute: snapshotting only on close.
- [ ] Show the signed-document confirmation of `§9` and the unsaved-changes dialog when leaving Edit PDF with committed edits. Done when: `Keep editing`, `Discard` and `Save` behave as the SPEC 9.9 dialog defines.
- [ ] Mark the tab dirty on the first committed edit and clean it only after a verified save, never on partial success. Done when: a forced writer failure leaves the tab dirty.
- [ ] Add `Ctrl+Shift+E` to `docs/keyboard-shortcuts.md` generation and the SPEC section 10 binding table through `D02 T04 §9`'s registry. Done when: `Keyboard.Audit` reports the accelerator bound exactly once. Cheaper substitute: a hidden handler outside the registry.
- [ ] Commit: `app: save Edit PDF sessions safely and register Edit PDF commands`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.EditSave:AppTests.EditCrashRecovery:Keyboard.Audit` passes, and unregistering `edit.crop` makes `Keyboard.Audit` fail.

## 15. `recto-cli edit`: list, text, image and vector verbs

Automation parity: anything the Edit PDF mode does to text, images and paths can be scripted without a UI, for batch fixes and for tests. The verbs call the same `EditSession` as the app, so they obey the same refusals and write through `AtomicFileWriter`. It matches Acrobat's scripted editing without PDF JavaScript, keeping D9.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `recto-cli edit list <in.pdf> [--page N] [--json]` printing object ids, kinds, boxes and paragraph text. Done when: `Cli.EditList` prints the fixture's objects with stable ids that equal `EditModel` ids.
2. Add `recto-cli edit text <in.pdf> --page N --paragraph <id> --text <str> [--font <name>] [--size <pt>] [--color <hex>] --out <out.pdf>` and `edit add-text` with `--rect`. Done when: the output extracts the new text and the unrelated paragraphs are unchanged.
3. Add `edit image` (`--replace`, `--add`, `--move`, `--resize`, `--rotate`, `--flip`, `--crop`, `--export`, `--delete`) and `edit path` (`--move`, `--fill`, `--stroke`, `--width`, `--delete`). Done when: each verb has a passing CLI test and a refusal test.

- [ ] Return the shared exit codes (`D01 T03 §1`): usage error, refused edit (`UnsupportedGlyph`, `NotEditable`, `NoRoom`, `PermissionDenied`) with the code point or reason in the message and `--json`. Done when: each refusal exits non-zero and writes no output file.
- [ ] Accept a JSON edit script (`--script edits.json`) of the `EditCommand` forms from `§3` applied in order, all-or-nothing. Done when: a script with one failing step leaves no output file.
- [ ] Write the output only through `AtomicFileWriter` and refuse `--out` equal to the input unless `--in-place` is given. Done when: without the flag the input hash is unchanged.
- [ ] Commit: `cli: add recto-cli edit verbs over the Edit PDF engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.Edit*` passes, and writing the output before verification makes `Cli.EditAtomic` fail.

## 16. Producer fixture matrix and Edit PDF round-trip suite

Editing breaks differently per producer: Word writes one `Tj` per word with `TJ` kerning, Chrome and Skia write one glyph per positioned op with `Identity-H` subsets, InDesign writes tagged marked content and CFF fonts, LibreOffice writes per-line `Tj` with `Tc`, and Google Docs writes subsetted TrueType with `ToUnicode` quirks. This section builds deterministic fixtures with those idioms and the suite that proves an edit of each stays correct, so a producer-specific regression cannot ship unseen. Real producer exports are not redistributed; the generator reproduces their content-stream patterns.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Extend `scripts/build-corpus.py` to emit `tests/corpus/edit/` fixtures per producer idiom (word, chrome-skia, indesign, libreoffice, google-docs), each with a text paragraph, a two-column page, a table, a rotated paragraph, an image and a path, with rows in `tests/corpus/PROVENANCE.md`. Done when: `Corpus.Provenance` passes with the new rows.
2. Add `tests/recto-tests/edit_pdf_tests.cpp` with `EditMatrix.RoundTrip` that retypes, adds, moves and deletes in every fixture, saves, reopens and compares. Done when: the suite passes on all producers.
3. Add the cross-checks. Done when: each is listed below and passes.

- [ ] Assert unrelated paragraphs, images and paths are byte-identical in their content-stream ranges after an edit in each fixture. Done when: the neighbour byte-compare passes for all fixtures.
- [ ] Assert the original embedded font resource of unchanged runs is unchanged and only missing glyphs use a new font (`D06 T03 §7`). Done when: the font resource names of unchanged runs match before and after.
- [ ] Assert rotated, two-column, table-cell and right-to-left fixtures edit without merging blocks and stay in logical order. Done when: the paragraph counts and extraction order are unchanged except the edited block.
- [ ] Assert idempotence across repeated edits and across save and reopen, and that an edit followed by undo restores byte-identical streams. Done when: ten repeated edits equal one and undo restores the original hashes.
- [ ] Assert structure tree, `/ParentTree`, `/ToUnicode` and optional-content membership after each edit on the tagged and layered fixtures. Done when: the consistency checks of `§6` pass on the saved files.
- [ ] Assert the differential suite of `D06 T03 §4` still passes with `EditModel` included, and that a fuzz pass over the edit entry points (`fuzz/fuzz_edit.cpp`, started from the corpus) finds no crash in one minute under ASan. Done when: both pass in CI.
- [ ] Assert a hostile fixture (deep `q` nesting, a 100000-operator page, an oversized image dictionary) returns a bounded `PdfError` or `Truncated` model without crash. Done when: ASan reports clean.
- [ ] Commit: `test(edit): add producer fixture matrix and Edit PDF round-trip suite`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditMatrix.*:Corpus.Provenance` passes, and having the rewrite engine inherit the first run's state makes `EditMatrix.RoundTrip` fail on the chrome-skia and word fixtures.

## 17. Spell check in the inline Edit PDF text editor

Add spelling to the editor of `§12`, which draws its own text and so cannot use a stock text control's spell check. The squiggle, the suggestion menu and `Add to dictionary` come from `SpellService` (`D03 T01 §14`). This matches Acrobat Pro's `Check Spelling` while editing and the spell check in Foxit PDF Editor, PDFelement and UPDF. It must not break offline-by-default (only the Windows spelling API is used), and it keeps the rule that spelling never changes text on its own and never blocks a commit.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.2 (on-canvas editing, floating popover) and the context menu treatment of `D03 T01 §4`.
**Job:** While retyping a paragraph the user sees misspelled words underlined, right-clicks one, picks a suggestion, or adds the word to the dictionary, and the page text updates as one undoable edit. Consumer: `InlineTextEditor` (`§12`).
**Treatment:** Debounced (`300 ms`) background checks of the editor buffer, a wavy underline in `RectoDangerBrush` paired with an `N spelling issues` text chip in the editor footer, and a context menu with up to `5` suggestions, `Ignore once`, `Ignore all` and `Add to dictionary`. Cheaper substitute that fails the checkpoint: a colour-only underline with no menu, or checking on the UI thread.
**Chrome:** consume `RectoDangerBrush`, `RectoRaisedBrush`, `RectoTextBrush`; no hex values.

**Build order:**

1. Add `SpellOverlay` to `InlineTextEditor` mapping `SpellIssue` ranges to editor quads and drawing the underline. Done when: `AppTests.InlineSpell` underlines a planted misspelling at the right quad and nothing in a correct sentence.
2. Add the menu, the footer chip and `F7` as `Next misspelling`. Done when: `AppTests.InlineSpellMenu` replaces a word with a suggestion as one undo entry and `Add to dictionary` clears the underline.

- [ ] Use the language of the run being edited (`/Lang` through `SpellService` language pick) and recheck only the changed paragraph range. Done when: `AppTests.InlineSpellLanguage` checks a French run with the French dictionary and an English run with the English one.
- [ ] Skip text that is not being edited and text the editor holds as unsupported glyphs, and never underline inside a refused character note. Done when: `AppTests.InlineSpell` finds no issue outside the buffer.
- [ ] Honour `Check spelling while typing` (`D03 T01 §14`): off removes the overlay and the chip. Done when: `AppTests.InlineSpellSetting` toggles it live.
- [ ] Register `Next misspelling` (`edit.nextMisspelling`, `F7` only while an Edit PDF text editor has focus) in `CommandRegistry`. Done when: `Keyboard.Audit` reports `F7` bound once and the command box finds it.
- [ ] Announce `<n> spelling issues` when the count changes after a pause, without moving focus, and give the menu items automation names. Done when: `A11y.InlineSpell` records the announcement and finds the names.
- [ ] Add the tests named above. Done when: `recto-tests.exe --gtest_filter=AppTests.InlineSpell*:A11y.InlineSpell` passes on the display-session host.
- [ ] Commit: `feat(app): add spell check to the inline Edit PDF text editor`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.InlineSpell*:A11y.InlineSpell` passes, and running the check on the UI thread makes `AppTests.InlineSpell` fail its frame-time assertion.

## 18. Vertical writing (`/WMode 1`) model: detection, metrics and editability classes

Add the model that understands vertical text so §19 can edit it instead of refusing it. Vertical CJK text uses a Type 0 font with a vertical CMap (`Identity-V` or a predefined `-V` CMap, `/WMode 1`), advances downward using `/DW2` and `/W2`, and flows in columns from right to left. This matches PDFelement's vertical text editing and extends Acrobat Pro's Edit PDF, which handles it unevenly. It must not break `§2`'s rule to declare unsupported layouts instead of guessing, so this section classifies each vertical paragraph as editable or `NotEditable` with a precise reason.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Extend `EditModel` text records (`§1`) with the writing mode, the vertical metrics (`/DW2` default `[880 -1000]`, per-CID `/W2` entries) and the origin shift of each glyph, taken from the shared interpreter. Done when: `VerticalModel.Metrics` reads `/DW2` and `/W2` from a fixture and places three glyphs at the expected vertical positions within `0.05 pt`.
2. Group vertical runs into `VerticalParagraph` (columns right to left, lines top to bottom) in `src/pdf/paragraph_model.cpp` and classify them. Done when: `VerticalModel.Grouping` groups a two-column vertical fixture into one paragraph with two columns.

- [ ] Detect vertical fonts from `/Encoding` (`Identity-V`, predefined `-V` CMaps) and embedded CMap `/WMode 1`, and record the `vert` substitution state of the font program through DirectWrite. Done when: `VerticalModel.Detect` identifies `Identity-V` and `UniJIS-UCS2-V` fixtures and a horizontal font in a rotated frame as horizontal.
- [ ] Classify a vertical paragraph `Editable` only when it is upright CJK (and sideways Latin runs drawn with a `90` degree text matrix) in uniform columns using one font and size per run. Done when: `VerticalModel.Classes` returns `Editable` for the plain fixture.
- [ ] Return `NotEditable` with a reason code for each layout Recto does not rebuild: `tate-chu-yoko` (horizontal digits inside a column), ruby annotation text, overlapping columns, per-glyph position adjustments that do not follow the advance (`TJ` kerning other than the font's metrics) and a font with no vertical metrics and no `vert` data for a glyph present in the run. Done when: `VerticalModel.Refusals` returns the five reason codes for the five fixtures and no mutation occurs.
- [ ] Surface the class in `ParagraphModel` so `§11`'s messages show `This vertical text uses <reason> and cannot be edited yet` for refusals. Done when: `AppTests.EditNotEditableMessages` shows the vertical message with the reason for the tate-chu-yoko fixture.
- [ ] Bound the work with the interpreter's shared caps and `PdfResult<T>`. Done when: a hostile vertical fixture with `100000` glyphs returns `Truncated` under ASan.
- [ ] Add `VerticalModel.*` tests in `tests/recto-tests/vertical_model_tests.cpp` using `tests/corpus/edit/vertical-ja.pdf` (`Identity-V`, two columns, plus tate-chu-yoko, ruby, overlapping-column and no-vertical-data variants) that this section adds to `scripts/build-corpus.py` with its `PROVENANCE.md` rows. Done when: `recto-tests.exe --gtest_filter=VerticalModel.*` passes under ASan.
- [ ] Commit: `pdf: model vertical writing with metrics and editability classes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=VerticalModel.*` passes under ASan, and reading advances from the horizontal `/Widths` makes `VerticalModel.Metrics` fail.

## 19. Vertical paragraph rewrite with vertical metrics and embedded fallback fonts

Make the editable vertical paragraphs of §18 rewritable: retype the text and it reflows down the columns inside the original box, in the original font for every glyph the font program holds. This matches PDFelement's vertical text editing and keeps Recto's per-glyph font retention and refusal of undrawable characters (`§4`). If a typed character needs a glyph the font lacks, a fallback font is embedded as `Identity-V` with computed vertical metrics; if no safe vertical form exists, the character is refused before commit with `NoVerticalForm(U+XXXX)`. The layouts §18 refuses (tate-chu-yoko, ruby, overlapping columns, custom position adjustments, fonts with no vertical data) stay refused with their reason code and are the recorded gap of this feature.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add vertical layout to `src/pdf/paragraph_rewrite.cpp`: place glyphs down a column by `w1` advances, wrap to the next column on the left when the box height is reached, apply the line-start and line-end prohibition rules (kinsoku, a default set for Japanese per JIS X 4051, none for other languages), and align top, centre or bottom. Done when: `VerticalRewrite.Basic` retypes a column and `PageText` readback returns the new text in column order.
2. Map characters to vertical glyphs with the font's `vert` substitution through DirectWrite shaping with vertical orientation, and embed a fallback as `CIDFontType2` with `Identity-V`, `/DW2`, `/W2`, `/CIDToGIDMap` and `/ToUnicode` for missing glyphs. Done when: `VerticalRewrite.Fallback` adds a missing kanji with one embedded font and extracts it exactly.

- [ ] Use the original font for every glyph it holds, including its vertical variants of punctuation (for example the vertical forms of `U+3001`, `U+3002` and `U+30FC`), and refuse `NoVerticalForm` when only a horizontal glyph exists. Done when: `VerticalRewrite.Punctuation` keeps the vertical forms and refuses a character with no vertical glyph, writing nothing.
- [ ] Compute `/W2` and `/DW2` for an embedded fallback from the font's `vhea` and `vmtx` tables when present, and from the ascender and descender otherwise, recording the derivation. Done when: `VerticalRewrite.FallbackMetrics` places fallback glyphs on the same column centre line as the original glyphs within `0.1 pt`.
- [ ] Keep per-run state, marked content and `/MCID` as `§5` and `§6` do, and keep the column progression right to left. Done when: `VerticalRewrite.PerRunState` and `VerticalRewrite.MarkedContent` pass.
- [ ] Reflow inside the original box and stop with `NoRoom` naming the blocking object when growth would overlap. Done when: `VerticalRewrite.NoRoom` returns the error and writes nothing.
- [ ] Prove idempotence and undo as `§5` does. Done when: ten identical vertical rewrites equal one and undo restores byte-identical streams.
- [ ] Add `VerticalRewrite.*` tests in `tests/recto-tests/vertical_rewrite_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=VerticalRewrite.*` passes under ASan.
- [ ] Commit: `pdf: rewrite vertical paragraphs with vertical metrics and fallback fonts`

**Test checkpoint:** `recto-tests.exe --gtest_filter=VerticalRewrite.*` passes under ASan, and laying the text out horizontally and rotating it makes `VerticalRewrite.Basic` fail on column-order extraction.

## 20. Vertical text in the inline editor: caret, hit testing and IME

Make the editor of `§12` work on vertical paragraphs: the caret moves down a column, the arrow keys follow vertical reading order, pointer hits map to vertical glyph quads, and Japanese, Chinese and Korean IME composition appears in place. This matches PDFelement's vertical text editing and keeps Recto's refusal of unsupported layouts (the editor never opens a paragraph §18 refused).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.2 and the `§12` editor chrome.
**Job:** The user clicks a vertical Japanese paragraph, types with an IME, moves the caret with the arrow keys, and sees the text reflow down the columns. Consumer: `InlineTextEditor`.
**Treatment:** A vertical mode of `InlineTextEditor` that rotates its text layout with DirectWrite vertical reading direction, draws the caret as a horizontal bar, and maps `Down`/`Up` to next/previous character and `Left`/`Right` to next/previous column (left is the next column). Cheaper substitute that fails the checkpoint: editing vertical text in a horizontal popup, or a vertical caret drawn as a vertical bar.
**Chrome:** consume `RectoAccentBrush` for the caret box, the `§12` flyout and `RectoFloatingBarBackgroundBrush`; no hex values.

**Build order:**

1. Add vertical layout and caret geometry to `InlineTextEditor` from `ParagraphId` and the vertical class of §18. Done when: `AppTests.InlineVerticalEdit` shows the original text exactly in place and a horizontal caret bar at the clicked character.
2. Wire IME composition, selection and the arrow keys. Done when: composing Japanese text inserts it in place and `Left` moves to the next column.

- [ ] Open the vertical editor only for `Editable` vertical paragraphs and show the §18 refusal message otherwise. Done when: `AppTests.InlineVerticalRefusal` shows the message for a tate-chu-yoko fixture and no editor.
- [ ] Show an inline note with the code point when a typed character has no vertical form (§19) and leave the buffer unchanged. Done when: `AppTests.InlineVerticalUnsupported` rejects it.
- [ ] Expose the editor to UI Automation with the vertical reading direction in its text attributes. Done when: `A11y.InlineVerticalEditor` finds the names and the text pattern.
- [ ] Add the tests named above. Done when: `recto-tests.exe --gtest_filter=AppTests.InlineVertical*:A11y.InlineVerticalEditor` passes on the display-session host.
- [ ] Commit: `feat(app): edit vertical text in the inline editor with caret and IME`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.InlineVertical*:A11y.InlineVerticalEditor` passes, and drawing a vertical caret bar makes `AppTests.InlineVerticalEdit` fail its caret geometry assertion.

## 21. Convert text to outlines (paths) for a selection or the page, on a copy

Add the engine that replaces text with filled vector paths so the result prints and looks identical on any machine without the font. This matches Foxit PDF Editor's and PDFelement's `Convert text to outlines` and what print production needs. It must not break the isolation rule (it runs on an `EditSession` copy and never on the open viewer document) and it keeps Recto's true removal: the text objects are gone from the saved file, found by a raw-byte scan, while the converted object keeps `/ActualText` so the words remain extractable and readable by screen readers.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement glyph outline extraction in `src/pdf/glyph_outline.h/.cpp` from the embedded font program (TrueType `glyf` and CFF charstrings) through DirectWrite `IDWriteFontFace::GetGlyphRunOutline` with a Recto path sink, and from Type 3 glyph procedures through the shared interpreter (`D06 T03 §4`, never DirectWrite): a procedure is converted only when it contains path construction, fill and `d1` uncoloured-glyph operators and no images, shadings or nested text, within the interpreter's caps, and any other procedure is refused with `NotEditable("type 3 glyph procedure not convertible")`, scaled by the run's size, `Tz`, `Tc`, `Tw`, `Ts` and text matrix. Done when: `Outlines.GlyphPaths` converts a TrueType, a CFF and a path-only Type 3 fixture, `Outlines.Type3Refusal` refuses a Type 3 fixture whose procedure draws an image with no mutation, and the rendered paths match the original text within `0.5` pixels at `300` DPI.
2. Implement `text_to_outlines(session, scope)` as an `EditCommand` replacing the text operators of the scope with `m`, `l`, `c`, `h` and a fill (or fill and stroke for `Tr 2`) in the run's colour, clip and alpha, inside the original `q ... Q` and marked-content wrapper. Done when: `Outlines.Command` converts one paragraph and the render before and after differs by `0` pixels beyond anti-aliasing noise at `300` DPI.

- [ ] Support the scopes `Selection` (the selected paragraphs or runs), `Page` and `Page range`, and report the count of converted runs. Done when: `Outlines.Scopes` converts each scope and leaves the other text untouched.
- [ ] Keep the original text recoverable for accessibility: wrap each converted run in `/Span << /ActualText (...) >> BDC ... EMC` (or update the existing structure element's `/ActualText`) holding the original Unicode, and keep the structure tree leaf. Done when: `Outlines.ActualText` extracts the original words from the saved file and a structure walk still reaches the leaf.
- [ ] Handle invisible and clipping text modes: leave `Tr 3` (invisible OCR layers) and `Tr 7` runs unchanged unless asked, and refuse a run whose font program is missing or unreadable with `NotEditable("font program unavailable")`. Done when: `Outlines.Modes` leaves an OCR layer untouched and refuses the missing-font fixture with no mutation.
- [ ] Remove the now-unused fonts, `/ToUnicode` maps and descendant font objects when nothing else references them, and force the full-rewrite save path (`§9`). Done when: `EditSave.NoPriorRevisionLeak` finds the converted words' font name absent from the saved bytes when no other text uses it.
- [ ] Bound the work: at most `2000000` path segments per page, `CancelToken` per run, `PdfResult<T>` on failure. Done when: `Outlines.Bounds` ends a hostile page inside the cap under ASan.
- [ ] Add `Outlines.*` tests in `tests/recto-tests/text_outline_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Outlines.*` passes under ASan.
- [ ] Commit: `pdf: convert text to outlines on an edit copy`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Outlines.*` passes under ASan, and leaving the text operators beside the paths makes the raw-byte scan in `Outlines.Command` fail.

## 22. Linked text flows across boxes and pages, stored as article threads

Add text that flows from one box into the next, on one page or across pages, so editing the first box reflows the rest. This matches Foxit PDF Editor's linked text boxes and UPDF's text-box linking, which Acrobat Pro lacks. It must not break `§5`'s per-box rewrite or the article model of `D03 T03 §15`: a flow is stored as a standard article thread (ISO 32000-2 `/Threads`, one bead per box) so any reader follows the same reading order, and the reflow itself happens only inside Recto when the user edits.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `TextFlow` (ordered `FlowBox` entries of page, rect, paragraph id, alignment) in `src/pdf/text_flow.h/.cpp`, build it from an existing `/Threads` article whose beads hold text paragraphs (`D03 T03 §15`'s `list_articles`), and create one with `link_boxes(session, box_a, box_b)`. Done when: `TextFlow.Build` reads a two-box thread and `TextFlow.Link` links two boxes on different pages and writes a valid bead ring.
2. Implement `reflow_flow(session, flow_id, new_text)` distributing the text over the boxes in order with the `§5` layout, so overflow from box 1 starts box 2. Done when: `TextFlow.Reflow` retypes a longer text and the surplus appears at the top of box 2 with box 1 full.

- [ ] Keep every box's own font, size, colour and alignment, and flow the text with the run state of the position it lands in. Done when: `TextFlow.RunState` keeps bold runs bold across a box boundary.
- [ ] Break only at line ends (never inside a word unless a word is wider than the box), and report `FlowOverflow` naming the last box when text no longer fits, writing nothing. Done when: `TextFlow.Overflow` returns the error and the file is unchanged.
- [ ] Unlink: `unlink_box(session, box)` removes one bead and keeps its text where it is, closing the ring. Done when: `TextFlow.Unlink` leaves a valid ring and unchanged page text.
- [ ] Keep `/MCID`, structure leaves and `/ActualText` consistent across boxes as `§6` does, with one structure element per box. Done when: `TextFlow.Structure` passes the `/ParentTree` consistency check.
- [ ] Treat a flow whose beads cover non-text or interleaved content as `NotEditable("interleaved flow")`. Done when: `TextFlow.Refusals` refuses it with no mutation.
- [ ] Add `TextFlow.*` tests in `tests/recto-tests/text_flow_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=TextFlow.*` passes under ASan.
- [ ] Commit: `pdf: link text boxes into flows stored as article threads`

**Test checkpoint:** `recto-tests.exe --gtest_filter=TextFlow.*` passes under ASan, and storing the link in a private dictionary instead of `/Threads` makes `TextFlow.Build` fail on a thread authored by another reader.

## 23. Join and split text objects: paragraphs, lines and boxes

Add explicit join and split of text objects. The paragraph model (`§2`) groups text automatically and the rewrite (`§5`) never merges across a gap, so the user needs a way to say that two paragraphs are one, or one is two. This matches Foxit PDF Editor's and UPDF's merge and split of text blocks. It must not break stable identity (`§1`, `§2`): both operations produce provenance maps so unrelated objects keep their ids.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `join_paragraphs(session, ids)` and `split_paragraph(session, id, caret_char)` as `EditCommand`s in `src/pdf/paragraph_rewrite.cpp`, joining by writing the combined runs (each keeping its font and state) into the first paragraph's line ranges and emptying the others' ranges only when no other painted object (an unselected text object, a path, an image, a shading or an XObject) lies between the ranges involved in paint order, and otherwise refusing the join with `NotEditable("interleaved content")` before any mutation (the `§5` rule), and splitting by writing two ranges from one. Done when: `ParagraphJoinSplit.Join` merges two paragraphs and `PageText` returns one block, and `ParagraphJoinSplit.Split` yields two.
2. Add `split_lines` (every line becomes its own object) and `merge_lines`. Done when: `ParagraphJoinSplit.Lines` round-trips a paragraph through split and merge with equal text.

- [ ] Allow a join only for paragraphs with the same rotation and writing mode and in reading order, and refuse otherwise with `NotEditable("cannot join <reason>")`. Done when: `ParagraphJoinSplit.Refusals` refuses a rotated pair and a table-cell pair.
- [ ] Keep marked content and structure: a join merges the structure leaves into the first (`/ActualText` updated) and a split allocates a fresh `/MCID` per new sequence. Done when: `ParagraphJoinSplit.Structure` passes the `/ParentTree` check.
- [ ] Preserve per-run state in both directions and the paint order of every other object. Done when: `ParagraphJoinSplit.RunState` keeps a bold run bold, `ParagraphJoinSplit.Interleaved` refuses a join over the order paragraph A, overlapping shape, paragraph B and over the order paragraph A, overlapping unselected text object C, paragraph B, each with no mutation, and a split keeps an interleaved shape between the same lines.
- [ ] Make both idempotent and undoable. Done when: ten identical joins equal one and undo restores byte-identical streams.
- [ ] Add `ParagraphJoinSplit.*` tests in `tests/recto-tests/paragraph_join_split_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ParagraphJoinSplit.*` passes under ASan.
- [ ] Commit: `pdf: join and split text objects`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ParagraphJoinSplit.*` passes under ASan, and re-grouping with the automatic gap threshold instead of the command makes `ParagraphJoinSplit.Join` fail.

## 24. Move objects to another page: text, images and paths

Add moving an object (a paragraph, image or path) from one page to another, which Edit PDF's single-page move cannot do. This matches Foxit PDF Editor's and UPDF's move to another page. It must not break the copy-on-write rule (`§3`) or true removal (`§9`): the object is written to the target page with its resources copied, and removed from the source page with unused resources pruned.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `move_to_page(session, edit_id, target_page, placement)` in `src/pdf/object_move.h/.cpp`: copy the object's operator range with its graphics state into a `q ... Q` at the end of the target page, copy the font, XObject, ExtGState, pattern and shading resources it names (reusing identical ones), and delete the source range through `§9`. Done when: `ObjectMove.Basic` moves an image and a paragraph to page 3 and both render at the chosen position there and are gone from the source.
2. Handle objects inside Form XObjects, marked content and optional content. Done when: `ObjectMove.FormAndLayers` moves an object from inside a form and keeps its layer membership.

- [ ] Resolve the target position as `Same position` (default), `Top left` or an explicit point, and scale nothing. Done when: `ObjectMove.Placement` checks the three placements within `0.05 pt` and different page sizes and rotations.
- [ ] Carry the structure element across pages when the file is tagged (new `/MCID` on the target, `/ParentTree` updated on both pages). Done when: `ObjectMove.Structure` passes the consistency check.
- [ ] Prune resources left unused on the source and keep those shared with other objects. Done when: `ObjectMove.Prune` removes an image used only by the moved object and keeps a shared one.
- [ ] Force the full-rewrite save and refuse a move on a page whose `/Contents` is unreadable. Done when: `EditSave.NoPriorRevisionLeak` covers a move and the refusal fixture writes nothing.
- [ ] Add `ObjectMove.*` tests in `tests/recto-tests/object_move_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ObjectMove.*` passes under ASan.
- [ ] Commit: `pdf: move objects to another page with resources and structure`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ObjectMove.*` passes under ASan, and moving without copying the font resource makes `ObjectMove.Basic` fail on text extraction.

## 25. Image filters: brightness, contrast, greyscale and invert on a copy

Add pixel filters for placed images: brightness, contrast, greyscale and invert. This matches Foxit PDF Editor's image adjustments. It must not break the image rules of `§8` (masks, `/Decode` and colour space stay correct) and it keeps true removal: the filtered image replaces the old XObject, which is pruned, so the original pixels are not left in the file. The filter is destructive by design and is undoable in the session; flip, rotate and crop stay in `§8`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `apply_image_filter(session, edit_id, FilterSpec)` in `src/pdf/image_filter.h/.cpp`: decode the image (`D04 T02 §9`'s decoder), apply the filter in linear 8-bit or 16-bit samples per channel, re-encode losslessly (`FlateDecode`) or, for a `DCTDecode` source with an unchanged colour model, re-encode as JPEG at quality `92`, and replace the XObject keeping the placement. Done when: `ImageFilter.Brightness` and `ImageFilter.Contrast` match a reference computation within `1` level per channel.
2. Add greyscale (Rec. 709 luma into `DeviceGray`) and invert. Done when: `ImageFilter.Greyscale` halves or better the stream size of an RGB fixture and `ImageFilter.Invert` is its own inverse.

- [ ] Define `FilterSpec` (`Brightness` `-100..100`, `Contrast` `-100..100`, `Greyscale`, `Invert`) with clamping and an identity check that writes nothing for neutral values. Done when: `ImageFilter.Identity` produces no patch.
- [ ] Resolve a colour-key `/Mask` array (ISO 32000-1 section 8.9.6.4) into an explicit coverage mask from the source samples before filtering, then write that coverage as a soft mask or stencil `/Mask` on the filtered image so the same pixels stay transparent after invert, brightness, contrast and the RGB-to-gray change in array dimensionality; keep a stencil `/Mask` and `/SMask` bytes untouched, apply the filter to colour channels only, and honour `/Decode` and indexed palettes (filter the palette entries). Done when: `ImageFilter.Masks` keeps stencil and soft mask bytes identical, `ImageFilter.ColourKey` keeps the transparent pixel set identical after invert and after greyscale on RGB and indexed colour-key fixtures, and `ImageFilter.Indexed` filters the palette.
- [ ] Refuse image kinds the filter cannot handle with a reason (`ImageMask` stencils for brightness and contrast, JPX with unsupported components): `NotEditable`. Done when: `ImageFilter.Refusals` refuses each with no mutation.
- [ ] Bound the work: at most `268435456` pixels, `CancelToken` per tile, `PdfResult<T>`. Done when: `ImageFilter.Bounds` returns `AreaTooLarge`-style errors for an oversize image.
- [ ] Prune the replaced XObject through `§9` and force the full-rewrite save. Done when: `EditSave.NoPriorRevisionLeak` covers a filter and finds the original image bytes absent.
- [ ] Add `ImageFilter.*` tests in `tests/recto-tests/image_filter_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ImageFilter.*` passes under ASan.
- [ ] Commit: `pdf: add brightness, contrast, greyscale and invert image filters`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ImageFilter.*` passes under ASan, and filtering the mask as well makes `ImageFilter.Masks` fail.

## 26. Blend modes, opacity and shading objects for text, images and paths

Add control of how an object composites and fills: a blend mode and opacity for any object, and creating and editing axial and radial gradient fills on paths. This matches Foxit PDF Editor's blend modes, opacity and shading objects. It must not break `§10`'s rule that a pattern or shading fill is never silently ignored: shadings here are created and edited explicitly through Recto's own writer, and no PDF function type that needs scripting exists, since only sampled, exponential and stitching functions are written.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `set_blend_and_opacity(session, edit_id, BlendSpec)` in `src/pdf/object_appearance.h/.cpp`, adding or replacing an `ExtGState` (`/BM`, `/ca`, `/CA`) and a `gs` operator at the start of the object's `q ... Q` without touching neighbours. Done when: `ObjectAppearance.Blend` sets `Multiply` and `0.5` opacity on a path, an image and a text run and renders them as the reference.
2. Implement `set_gradient_fill(session, edit_id, GradientSpec)` for paths, writing a type 2 (axial) or type 3 (radial) shading with a type 2 or type 3 function (exponential or stitching) in the path's colour space, referenced through a shading pattern. Done when: `ObjectAppearance.Gradient` fills a rectangle with a two-stop axial gradient and reads the stops and angle back.

- [ ] Support all PDF blend modes of ISO 32000-2 table 134 (`Normal`, `Multiply`, `Screen`, `Overlay`, `Darken`, `Lighten`, `ColorDodge`, `ColorBurn`, `HardLight`, `SoftLight`, `Difference`, `Exclusion`, `Hue`, `Saturation`, `Color`, `Luminosity`). Done when: `ObjectAppearance.AllBlendModes` renders each against a reference image within `2` levels.
- [ ] Preserve existing alpha and soft masks and compose opacity with them rather than replacing them. Done when: `ObjectAppearance.KeepsSoftMask` keeps a fixture's `/SMask`.
- [ ] Edit existing gradients only when the function is in the exact subset Recto's stop model represents (ISO 32000-1 7.10.3 and 7.10.4), in `DeviceGray`, `DeviceRGB` or `DeviceCMYK`: a type 2 function with `/Domain [0 1]` and `/N 1` (linear), or a type 3 stitching function with `/Domain [0 1]` whose sub-functions are all such linear type 2 functions, whose `/Encode` pairs are all `[0 1]`, and whose adjacent sub-functions meet continuously (each `C1` equals the next `C0`); read stops, angle and radius and change them, and refuse with `NotEditable("shading function not editable")` and no mutation sampled (type 0) and calculator (type 4) functions, any `/N` other than 1, any other `/Encode` (reversed or partial), a discontinuous boundary, and any nesting deeper than one stitching level, and report other shading types (function-based, free-form, lattice, Coons and tensor patch meshes) as movable and deletable but not editable. Done when: `ObjectAppearance.ShadingTypes` edits types 2 and 3 with supported functions, refuses types 2 and 3 with a sampled function, an `/N 2` exponential, a stitching function with a reversed `/Encode [1 0]` and one with a discontinuous boundary (`ObjectAppearance.FunctionRefusal`, each file byte-identical after the refusal), and reports types 1, 4, 5, 6 and 7.
- [ ] Limit a gradient to `32` stops and clamp offsets to `0..1` in ascending order. Done when: `ObjectAppearance.GradientBounds` rejects a `33`-stop request.
- [ ] Add a transparency-group warning when a blend mode is set inside a page that has no `/Group`: add the page `/Group << /S /Transparency /CS ... >>` only when absent. Done when: `ObjectAppearance.PageGroup` adds the group once and leaves an existing one untouched.
- [ ] Make every operation idempotent and undoable. Done when: ten identical calls equal one and undo restores byte-identical streams.
- [ ] Add `ObjectAppearance.*` tests in `tests/recto-tests/object_appearance_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=ObjectAppearance.*` passes under ASan.
- [ ] Commit: `pdf: add blend modes, opacity and gradient fills for edit objects`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ObjectAppearance.*` passes under ASan, and writing the blend mode into the fill colour operator instead of an `ExtGState` makes `ObjectAppearance.Blend` fail.

## 27. Arrange and convert commands: outlines, link flow, join, split and move to page

Add the user commands for §21 to §24 to the Edit PDF mode: `Convert to outlines`, `Link text boxes`, `Unlink`, `Join paragraphs`, `Split paragraph`, and `Move to page...`. This matches the same commands in Foxit PDF Editor and UPDF. It must not break the shell rules (every command in `Ctrl+K`, `Esc` peels back, controls named and tooltipped) and it keeps Recto's refusal messages: a command that cannot run says why.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** Genuinely new surface. It adds a `Text` overflow menu to the `EditPdfBar` (`§11`) and context-menu entries on the selected object, following `design/SPEC.md` section 8.2.
**Job:** The user selects text, chooses `Convert to outlines` for the selection, the page or a range; drags a link handle from one box to the next to flow text; joins two paragraphs; splits one at the caret; or moves an object to page 7 from a dialog. Consumer: §21 to §24, `CommandRegistry`.
**Treatment:** Buttons and menu items bound to commands, an overflow chevron on each text box that is full (drag to another box to link), a `Move to page` dialog with a page number box and a thumbnail picker, and a confirm step for `Convert to outlines` that names what will no longer be editable. Cheaper substitute that fails the checkpoint: commands reachable only from a context menu, or an outline conversion with no confirmation.
**Chrome:** consume `RectoFloatingBarBackgroundBrush`, `RectoAccentBrush` for the link handle, `RectoDialogStyle`, Segoe Fluent Icons; no hex values.

**Build order:**

1. Register `edit.outlines`, `edit.linkBoxes`, `edit.unlinkBox`, `edit.join`, `edit.split`, `edit.moveToPage` in `CommandRegistry` and add them to the bar overflow and the object context menu. Done when: `Keyboard.Audit` and the command box find all six and each is disabled with a reason tooltip when not applicable.
2. Build the link handle with an overflow marker and the `Move to page` dialog. Done when: `AppTests.EditArrange` links two boxes by drag, joins, splits and moves an object to another page, each as one undo entry.

- [ ] Show the `Convert to outlines` confirmation (`Selection`, `Page`, `Page range`, and the sentence `The text becomes shapes and can no longer be edited as text. It stays searchable.`) with `Convert` and `Cancel`. Done when: `AppTests.EditOutlinesDialog` cancels without a change and converts on confirm.
- [ ] Show an overflow marker (a `+` glyph with the text `Text continues`) at the end of a full box and animate nothing when animations are off. Done when: `AppTests.EditFlowHandle` shows the marker only on overflow.
- [ ] Give each command, handle and dialog control an automation name and tooltip. Done when: the inventory (`D03 T02 §3`) finds none missing. Cheaper substitute: tooltips only.
- [ ] Mark the tab dirty on the first committed operation and clean it only after a verified save (`§14`). Done when: `AppTests.EditArrange` leaves the tab dirty after a forced save failure.
- [ ] Add the tests named above. Done when: `recto-tests.exe --gtest_filter=AppTests.EditArrange:AppTests.EditOutlinesDialog:AppTests.EditFlowHandle:A11y.EditArrange` passes on the display-session host.
- [ ] Commit: `app: add outline, link, join, split and move-to-page commands to Edit PDF`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.EditArrange:AppTests.EditOutlinesDialog:AppTests.EditFlowHandle:A11y.EditArrange` passes, and unregistering `edit.join` makes `Keyboard.Audit` fail.

## 28. Appearance panel: image filters, blend mode, opacity and gradient fill

Add one panel for the appearance of the selected object: image filter sliders, blend mode, opacity and gradient editing. This matches Foxit PDF Editor's properties panel. It must not break `§13`'s one-undo-per-gesture rule: slider drags preview live and commit once on release.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes
**Fidelity:** Genuinely new surface. It follows `design/SPEC.md` section 8.3 (side-panel cards through `SidePanelHost`, `D02 T03 §3`) and the style popover of `D03 T03 §4`.
**Job:** The user selects an image and drags brightness and contrast with a live preview, picks `Multiply` and `60%` opacity, or edits a gradient's stops on a path and sees the page change. Consumer: §25 and §26.
**Treatment:** An `Appearance` card in `SidePanelHost` with sliders (`Brightness`, `Contrast`), toggles (`Greyscale`, `Invert`), a blend mode combo, an opacity slider with a number box, and a gradient editor with stop markers, a type switch (`Linear`, `Radial`), an angle box and a colour picker (the annotation bar picker, `D03 T03 §3`). Cheaper substitute that fails the checkpoint: a dialog that applies only on OK with no preview, or committing every slider tick.
**Chrome:** consume `SidePanelHost`, `RectoSurfaceBrush`, `RectoAccentBrush`, `RectoTextBrush`, Segoe Fluent Icons; no hex values.

**Build order:**

1. Build `AppearanceViewModel` and the card with live preview rendered from a worker-built preview bitmap and one `EditCommand` on release. Done when: `AppTests.AppearancePanel` drags brightness through twenty ticks and finds one undo entry.
2. Add the blend, opacity and gradient controls and the `Ctrl+K` command `Appearance`. Done when: `AppTests.AppearanceBlend` and `AppTests.AppearanceGradient` change a path's blend mode and gradient and read the saved file back.

- [ ] Show only the controls that apply to the selection (filters for images, gradient for paths, blend and opacity for all) and a reason line for the rest. Done when: `AppTests.AppearanceApplicability` checks image, path and text selections.
- [ ] Preview filters on a downscaled copy of the image (at most `1024` pixels on the long edge) off the UI thread, and apply the full-resolution filter on release with progress text. Done when: `AppTests.AppearancePerf` keeps frame time under `16.7 ms` while dragging.
- [ ] Keep numeric entry for every slider for keyboard use, with names, tooltips and live-region announcements of the new value. Done when: `A11y.AppearancePanel` finds all names and a keyboard-only run sets a blend mode and opacity.
- [ ] Add the tests named above. Done when: `recto-tests.exe --gtest_filter=AppTests.Appearance*:A11y.AppearancePanel` passes on the display-session host.
- [ ] Commit: `app: add the Appearance panel for filters, blend, opacity and gradients`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AppTests.Appearance*:A11y.AppearancePanel` passes, and committing on every slider tick makes `AppTests.AppearancePanel` fail on the undo count.

## 29. `recto-cli edit` verbs for outlines, flows, join, split, move, filters and appearance

Extend the scripted editing of `§15` to the new edits: the same `EditSession`, the same refusals, written through `AtomicFileWriter`. This keeps Recto's rule that every capability is scriptable without PDF JavaScript (D9). It must not break `§15`'s exit-code contract (`D01 T03 §1`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `edit outlines`, `edit link`, `edit unlink`, `edit join`, `edit split`, `edit move-to-page`. Done when: each verb has a passing CLI test and a refusal test.
2. Add `edit filter` and `edit appearance`, and the `EditCommand` forms for all of them in the `--script` format. Done when: a script mixing the verbs applies all-or-nothing.

- [ ] `recto-cli edit outlines <in.pdf> (--selection <ids> | --page N | --pages <range>) --out <out.pdf>`. Done when: `Cli.EditOutlines` verifies the paths replace the text and the words stay extractable.
- [ ] `recto-cli edit link <in.pdf> --from <paragraph-id> --to <paragraph-id> --out <out.pdf>` and `edit unlink`. Done when: `Cli.EditLink` creates and removes a thread.
- [ ] `recto-cli edit join <in.pdf> --paragraphs <id,id> --out <out.pdf>` and `edit split <in.pdf> --paragraph <id> --at <char> --out <out.pdf>`. Done when: `Cli.EditJoinSplit` verifies counts and refusals.
- [ ] `recto-cli edit move-to-page <in.pdf> --id <edit-id> --page <N> [--at same|top-left|x,y] --out <out.pdf>`. Done when: `Cli.EditMovePage` moves an object and prunes the source.
- [ ] `recto-cli edit filter <in.pdf> --id <edit-id> [--brightness n] [--contrast n] [--gray] [--invert] --out <out.pdf>` and `edit appearance <in.pdf> --id <edit-id> [--blend <mode>] [--opacity 0..1] [--gradient <json>] --out <out.pdf>`. Done when: `Cli.EditFilter` and `Cli.EditAppearance` verify the saved values.
- [ ] Return the shared refusal exit codes with the reason in the message and `--json`, write no output on refusal, and refuse `--out` equal to the input without `--in-place`. Done when: each refusal exits non-zero and the input hash is unchanged.
- [ ] Commit: `cli: add recto-cli edit verbs for the competitor-parity edits`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.EditOutlines:Cli.EditLink:Cli.EditJoinSplit:Cli.EditMovePage:Cli.EditFilter:Cli.EditAppearance` passes, and writing the output before verification makes `Cli.EditAtomic` fail.

## 30. Fixtures and round-trip suite for the parity edits

Prove the new edits on deterministic fixtures: vertical text, linked boxes, outlines, filters and blends. Real documents are not redistributed; the generator writes the content-stream patterns. It keeps `§16`'s rule that each edit is checked for neighbour integrity, idempotence and structure.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Extend `scripts/build-corpus.py` to emit `tests/corpus/edit/` fixtures `vertical-ja.pdf` (added by `§18`), `linked-boxes.pdf` (two pages, `/Threads`), `outline-fonts.pdf` (TrueType, CFF, Type 3), `image-filter.pdf` (RGB, indexed, masked), `blend-gradient.pdf` (shadings of types 2, 3 and 4), with rows in `tests/corpus/PROVENANCE.md`. Done when: `Corpus.Provenance` passes with the new rows.
2. Add `EditParity.RoundTrip` in `tests/recto-tests/edit_parity_tests.cpp` applying each edit, saving, reopening and comparing. Done when: it passes for every fixture.

- [ ] Assert unrelated objects stay byte-identical in their content-stream ranges after each parity edit. Done when: the neighbour byte-compare passes for all fixtures.
- [ ] Assert idempotence and undo for each edit, and the raw-byte absence of removed content (text fonts after outlines, original image bytes after a filter, moved objects on the source page). Done when: the scans pass.
- [ ] Assert structure tree, `/ParentTree`, `/ActualText` and optional-content membership after each edit. Done when: the consistency checks of `§6` pass on the saved files.
- [ ] Add the new entry points to the fuzz target (`fuzz/fuzz_edit.cpp`) and run it for one minute from the new corpus under ASan. Done when: no crash is found.
- [ ] Assert a hostile fixture (a `100000`-glyph vertical page, a `33`-stop gradient request, a `20000`-by-`20000` pixel image filter) returns a bounded error without crash. Done when: ASan reports clean.
- [ ] Commit: `test(edit): add fixtures and round-trip suite for the competitor-parity edits`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditParity.*:Corpus.Provenance` passes, and having outline conversion leave the text operators in place makes `EditParity.RoundTrip` fail on the raw-byte scan.

## Verification

- `recto-tests.exe --gtest_filter=EditModel.*:ParagraphModel.*:EditSession.*:EditFonts.*:ParagraphRewrite.*:AddText.*:ImageEdit.*:EditDelete.*:EditSave.*:VectorEdit.*:AppTests.Edit*:AppTests.Inline*:A11y.Edit*:A11y.Inline*:Cli.Edit*:EditMatrix.*:VerticalModel.*:VerticalRewrite.*:Outlines.*:TextFlow.*:ParagraphJoinSplit.*:ObjectMove.*:ImageFilter.*:ObjectAppearance.*:AppTests.Appearance*:EditParity.*` all pass (engine suites under ASan).
- Retyping a paragraph in each producer fixture keeps neighbours byte-identical, keeps the original font for present glyphs, refuses an undrawable character with its code point, and a deleted image's bytes are absent from the saved file when nothing else uses it, and a shared image is kept while any other use remains.
- The open viewer document is unchanged after any failed or cancelled edit, save or export.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
