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
- `docs/research/2026-10-20-pdfcraft-issue-lessons.md` (editing text without destroying neighbours, font swaps, merged paragraphs) and the gates `D06 T03 §4`, `§5` and `§7`.
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

## 1. Editable object model (`EditModel`) built from the shared interpreter

Editing needs a list of what is on the page: text runs, images, paths and forms with their geometry, graphics state and the exact operator range that drew them. pdfcraft's editor had its own content parser that disagreed with its renderer, so edits landed on content the user never saw (pdfcraft #3080, #3107, #3177). `EditModel` must be a consumer of the single interpreter (`D06 T03 §4`) and never lex a stream itself. It matches the object layer of Acrobat's Edit PDF and keeps Recto's rule that hostile input is bounded and returns `PdfResult<T>`.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/edit_model.h/.cpp` defining `EditObject` (kind `Text`, `Image`, `Path`, `Form`, `Shading`), its bounding box in page points, the source operator range (stream part index and operator ordinals), the occurrence path (the chain of `Do` invocations, with the invoking operator range of each, that reaches the object through nested Form XObjects), the full graphics state at the point of drawing, the marked-content stack with `/MCID`, the optional-content membership and the paint order index. Done when: `EditModel.Build` lists every object of a three-object fixture page with the correct kind, box and operator range.
2. Build the model by subscribing to the interpreter's operator trace (`D01 T02 §3`, `§4`) instead of re-parsing. Done when: `EditModel.SingleInterpreter` fails when the model builder is pointed at a private tokenizer and passes on the shared one.
3. Give every object an `EditId` that is stable within an edit session and carried by patch provenance, not recomputed from the drawing. Done when: after any sequence of committed edits, every object untouched by them has the `EditId` it had before, including identical objects at one position.

- [ ] Define `EditObject` and `EditModel::build(Document&, page_index, CancelToken)` returning `PdfResult<EditModel>` in `src/pdf/edit_model.h/.cpp`. Done when: a split-`/Contents` fixture yields the same objects as the same page in one stream.
- [ ] Record the occurrence path for every object reached through a Form XObject, so two invocations of one form give two distinct `EditObject`s with distinct paths. Done when: `EditModel.FormOccurrences` lists an image inside a form invoked twice as two objects with different occurrence paths.
- [ ] Record per text run the font resource name, size, `Tc`, `Tw`, `Tz`, `TL`, `Ts`, `Tr`, fill and stroke colour with colour space, text matrix, clip stack and alpha. Done when: a fixture with every state value distinct reads each back exactly (pdfcraft #2517, #3215).
- [ ] Record the marked-content stack (`BDC` / `BMC` tags, property dictionaries, `/MCID`) and the optional-content group per object, resolving `/OC` through the page's `/Properties`. Done when: an object inside an OFF group reports `visible=false` and its group id.
- [ ] Assign `EditId` values once, when a page's `EditModel` is first built in a session, as never-reused 64-bit ids in paint order, and afterwards update the model only by applying each committed `ContentPatch`'s provenance map (`kept`, `moved`, `replaced`, `created`, `deleted`, `copied-on-write`, each naming old and new ids) instead of re-deriving ids from a rebuilt drawing. Identical objects at one position cannot be told apart by any fingerprint, so this is the only rule that keeps them distinct: ids are guaranteed stable within a session and are not stable across sessions or after an outside change to the file, where a fresh model assigns fresh ids and the journal is cleared. Done when: with three identical strings at one origin, an `AddText` inserting a fourth identical string before them keeps the original three ids on the original three objects, a rewrite of the middle one keeps the outer two ids and gives the middle one a `replaced` mapping, and reopening the session assigns new ids and rejects a stored command that names an old id with `StaleObject`.
- [ ] Bound recursion and allocation: form XObject depth, object count per page and operator count come from the interpreter's shared caps (`D06 T03 §4`). Done when: the cap fixtures give a `Truncated` model, not a crash or a private cap (pdfcraft #3120, #3504).
- [ ] Add `EditModel` as a seventh consumer in `UserClasses.Differential`. Done when: its glyph boxes match the other consumers within 0.01 pt on every differential fixture.
- [ ] Commit: `pdf: add EditModel object listing over the shared interpreter`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditModel.*` passes, and pointing the builder at a private tokenizer makes `EditModel.SingleInterpreter` fail.

## 2. Stable paragraph model: lines, paragraphs, columns, table cells and rotation

A paragraph is not a stream feature, so the editor must infer it from runs, and it must do so the same way every time or an edit lands on the wrong block. pdfcraft addressed paragraphs by list index and merged neighbouring paragraphs when one changed (pdfcraft #2516, #2564, #3429, #3744). This section groups `EditObject` text runs into lines and paragraphs with identity that survives edits, detects alignment, rotation, columns and table cells, and refuses to merge across them. It matches Acrobat's paragraph box and keeps Recto's single `PageText` reading order (`D03 T01 §1`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/paragraph_model.h/.cpp` with `Paragraph` (id, lines, bounding quad, alignment, line spacing, indent, rotation, column index, optional table-cell address) built from `EditModel` text objects. Done when: `ParagraphModel.Basic` groups a two-paragraph fixture into exactly two paragraphs.
2. Give a paragraph an id that is stable under edits elsewhere and carried through its own edits by the session. Done when: editing paragraph A leaves paragraph B's id unchanged and A keeps its id after its rewrite.
3. Declare unsupported layouts instead of guessing. Done when: a vertical (`Identity-V`) or `Tr 7` run is reported as `NotEditable` with a reason code.

- [ ] Cluster runs into lines by baseline in the run's rotated frame, then lines into paragraphs by gap, indent and alignment, in `src/pdf/paragraph_model.cpp`. Done when: a 90 degree and a 33 degree rotated fixture each give one paragraph per source paragraph (pdfcraft #1680, #3577).
- [ ] Detect left, right, centre and justified alignment from line start and end offsets, and record the first-line indent and the line pitch. Done when: the four alignment fixtures report the right class and pitch within 0.05 pt.
- [ ] Detect columns using the `PageText` XY-cut result (`D03 T01 §1`) so a paragraph never spans a column gutter. Done when: a two-column fixture yields paragraphs wholly inside one column with distinct column indexes.
- [ ] Detect table cells from ruled lines and aligned text and tag each paragraph with row and column, never merging text across cells. Done when: a 3 by 3 ruled table fixture yields nine paragraphs with correct addresses (pdfcraft #3429).
- [ ] Record on each `Paragraph` the operator range of every line (`line_ranges`), which need not be contiguous, and the objects painted between them. Done when: a fixture with paragraph A line 1, an overlapping shape, then A line 2 reports two line ranges and the shape between them.
- [ ] Define `ParagraphId` as the `EditId` of its first run plus a hash of its line count and box, and keep a remap table the session updates on commit. Done when: after a rewrite the session's provenance map resolves the old paragraph id to the new paragraph without re-grouping.
- [ ] Establish the logical text of bidi and complex-script runs (Arabic, Hebrew, Indic) only from evidence that states it: the `/ActualText` of the enclosing marked content when present, otherwise the `ToUnicode` text of the show operators taken in content-stream order, never recovered from glyph positions. Then apply UAX #9 forward to that logical string to compute the expected visual order and accept the paragraph only when that order reproduces the drawn glyph positions; when it does not, and for the single-direction RTL case try the reversed stream order as the one other candidate and accept it only if it alone passes the same forward check. Mark the paragraph `NotEditable("ambiguous order")` when no candidate or more than one candidate passes, and always when the run mixes directions and the positions are the only evidence. Done when: `ParagraphModel.BidiEvidence` accepts a logical-order Arabic fixture and a mixed Arabic, digit and punctuation fixture whose `/ActualText` is the independently specified logical string, accepts a visual-order pure-Arabic fixture through the reversed candidate, and refuses a visual-order mixed-direction fixture with no `/ActualText`, with no mutation in the refused case (pdfcraft #1039, #2878).
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

Typing into an embedded subset font fails in three ways pdfcraft shipped: it swapped the whole run's font when one glyph was missing, inferred coverage from `/Widths` instead of the font program, and wrote `?` for missing characters (pdfcraft #1026, #1388, #2750, #125, #506). This section decides, per glyph, whether the original font can draw it and otherwise picks a fallback, using the font file's own `cmap`. It also supplies the installed-font list for the font picker and enforces embedding rights. It matches Acrobat's font handling and keeps Recto's rule that no path writes `?`.

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
- [ ] Refuse a character no installed embeddable font can draw before commit, returning `UnsupportedGlyph` with `U+XXXX` (pdfcraft #3154, #3592). Done when: a private-use code point returns the error with the code point and nothing is written.
- [ ] Commit: `pdf: resolve edit fonts per glyph with embedding rights and Unicode fallback`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditFonts.*` passes, and reading coverage from `/Widths` instead of the font program makes `EditFonts.CoverageFromProgram` fail.

## 5. Paragraph rewrite: reflow in the original box with per-run state and z-order kept

This is the core of Edit PDF: the user changes a paragraph's text and it reflows inside the box it occupied, in the same font, size, colour and stacking position. pdfcraft reflowed with the first run's state for every run, lost clips and changed stacking, and edits drifted on repeat (pdfcraft #2516, #2517, #2564, #3215, #3577). The rewrite replaces only the paragraph's operator range, emits graphics and text state per run, and is idempotent. It matches Acrobat's paragraph edit and keeps Recto's per-glyph font retention (`§4`).

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/paragraph_rewrite.h/.cpp`: `rewrite(session, paragraph_id, new_text_runs)` producing a `ContentPatch`. Done when: `ParagraphRewrite.Basic` retypes one paragraph and `PageText` readback returns the new text with neighbours unchanged.
2. Lay out the new text with the resolved fonts' advances, honouring the original box width, alignment, line pitch, indent, `Tc`, `Tw` and `Tz`, with word-wrap and a hyphenation-free break rule. Done when: shortened text keeps the left edge and the original first-baseline position within 0.05 pt.
3. Place the patch at the original operator ranges so z-order is unchanged: write each new line into the original range of the line it replaces, in order, put surplus lines into the last range, and leave an emptied range as `q Q`, so objects painted between lines stay between them. Done when: the interleaved fixture (line 1, overlapping shape, line 2) keeps the shape's paint position relative to every line after a rewrite that adds a line and a rewrite that removes one, and a layout where this cannot be kept returns `NotEditable("interleaved content")` before any mutation.

- [ ] Emit per run its own `Tf`, `Tc`, `Tw`, `Tz`, `Ts`, `Tr`, colour and clip, restoring the state before leaving the range, never inheriting the first run's state (pdfcraft #2517, #3215). Done when: a four-run fixture with distinct state per run rewrites with each run's state intact.
- [ ] Preserve a run's formatting when text is added inside it and give inserted text the state of the run at the caret. Done when: typing inside a bold run yields bold glyphs with the same font resource.
- [ ] When the new text needs more lines than the box, grow downward without overlapping the next object, and stop with `NoRoom` naming the blocking object when growth would overlap. Done when: a tight-layout fixture returns `NoRoom` and writes nothing.
- [ ] Lay out right-to-left runs by shaping the logical string with DirectWrite for the resolved font, emitting the shaped glyph ids in visual order, and refusing before mutation when the font lacks a required joining form or the paragraph was marked ambiguous in `§2`. Done when: an Arabic paragraph round-trips in logical order through save and extraction (pdfcraft #1039, #3277).
- [ ] Reflow within the original rotation frame so a rotated paragraph stays at its angle. Done when: the 90 degree and 33 degree fixtures keep their baseline angle within 0.01 degrees.
- [ ] Prove idempotence: rewriting a paragraph with its own current text is a no-op and ten rewrites of the same edit equal one. Done when: the content streams of both runs are byte-identical.
- [ ] Commit: `pdf: rewrite paragraphs with reflow, per-run state and stable z-order`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ParagraphRewrite.*` passes under ASan, and reusing the first run's state for all runs makes `ParagraphRewrite.PerRunState` fail.

## 6. Structure tree, marked content and layers kept consistent across text edits

A text edit that drops its `BDC` / `EMC` wrapper, reuses an `/MCID` or leaves `/ActualText` stale breaks accessibility and copy-paste, and an edit inside a hidden layer must stay in that layer. pdfcraft wrote text outside the original marked content and lost the Unicode mapping (pdfcraft #1026, #3577). This section keeps the structure tree, marked content, `/ToUnicode` and optional-content membership consistent for every rewrite and add. It matches Acrobat's tagged-aware editing and keeps Recto's layer awareness (`D02 T03 §7`).

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

Acrobat's Add Text creates page content, not a FreeText annotation, so the text prints, extracts and survives flattening. Recto's `FreeText` annotations (`D03 T03 §2`) stay available for commenting; this section creates true content with a chosen font, size, colour and alignment. pdfcraft's add-text path wrote `?` for non-Latin text and used an annotation (pdfcraft #125, #347, #506). It must use the resolver in `§4` and the idempotent session in `§3`.

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
- [ ] Refuse an undrawable character before commit with its code point (pdfcraft #3154). Done when: a private-use code point returns `UnsupportedGlyph` and the page is unchanged.
- [ ] Commit: `pdf: add text boxes as real page content`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AddText.*` passes, and making the writer emit a `FreeText` annotation makes `AddText.IsPageContent` fail.

## 8. Image objects: add, replace, move, resize, rotate, flip, crop and export

Image editing in Acrobat covers every direct manipulation of a placed image. Placement lives in the `cm` that precedes `Do`, the pixels live in the XObject, and crop is a clip, so most operations are matrix or clip patches and never re-encode pixels. pdfcraft re-encoded images on move and lost masks (pdfcraft #2164, #3407). This section keeps `/SMask`, `/Mask` and `/Decode` intact and never recompresses on a geometry change.

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
- [ ] Handle inline images (`BI ... EI`) by listing them, supporting move, resize and delete, and promoting to an XObject only for replace. Done when: an inline-image fixture moves and deletes correctly with `EI` detection from the shared interpreter (pdfcraft #3216).
- [ ] Keep the marked-content wrapper of the image atomic: a `BDC ... EMC` containing the `Do` moves with it. Done when: the structure-tree leaf of the image still resolves after a move.
- [ ] Commit: `pdf: add image object operations without re-encoding`

**Test checkpoint:** `recto-tests.exe --gtest_filter=ImageEdit.*` passes, and re-encoding the pixels on move makes `ImageEdit.Geometry` fail.

## 9. Object removal, atomic marked content and full-rewrite save with orphan pruning

pdfcraft deleted images from an incremental update and left the old bytes in the file, or deleted a `Do` and orphaned its marked content (pdfcraft #2165, #3407, #3511). A delete or a replace must remove the content and prune what nothing references any more, and `D06 T03 §5` requires a full rewrite for removal. This section implements deletion of any object, atomic removal of its marked-content wrapper, reference-count pruning, and the full-rewrite save. It matches Acrobat's delete and keeps Recto's true-removal rule (the redaction rule in `AGENTS.md`).

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
- [ ] Implement `resize`, `rotate`, `flip_h` and `flip_v` as one decision in `src/pdf/vector_edit.cpp`: the transform is composed into the object's `cm` (or wrapped in `q <matrix> cm ... Q`) with path coordinates and paint operators left byte-identical whenever the matrix is a similarity (uniform scale, rotation, flip) or the path has no stroke; the transform is applied by geometry normalization (coordinates rewritten into page space, the object's own `cm` dropped) only when the matrix is non-uniform or skewed and the path is stroked. Done when: `VectorEdit.TransformMatrix` rotates 33 degrees, flips and uniformly resizes a stroked fixture path and an unstroked one resized non-uniformly, with the rendered box within 0.05 pt of the expected transform and the path operand bytes unchanged, and `VectorEdit.TransformNormalize` resizes a stroked path non-uniformly with the path operand bytes changed and the box within 0.05 pt.
- [ ] Keep the displayed stroke width when a transform is applied: in the matrix branch under a uniform scale `s`, set `w` to the requested width divided by `s`; in the normalization branch set `w` to the requested width directly, and `set_line_width` on an object under a non-uniform `cm` first normalizes the geometry and then sets `w`. Done when: a 2 pt stroked rectangle resized non-uniformly shows 2 pt horizontal and vertical strokes within 0.05 pt and a uniformly scaled one shows 2 pt.
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
- [ ] Show a character the font cannot draw immediately: when typing or pasting `U+XXXX` with no embeddable font, reject the character and show an inline note naming the code point. Done when: `AppTests.InlineTextUnsupported` rejects it and the buffer is unchanged (pdfcraft #125, #3154).
- [ ] Show a font-substitution notice when a typed character falls back to another font, naming that font, and keep the rest of the run in the original font. Done when: typing one Cyrillic letter shows "Cyrillic uses Segoe UI" and the other letters keep the original face (pdfcraft #1026).
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

- [ ] Assert unrelated paragraphs, images and paths are byte-identical in their content-stream ranges after an edit in each fixture (pdfcraft #2516, #3429). Done when: the neighbour byte-compare passes for all fixtures.
- [ ] Assert the original embedded font resource of unchanged runs is unchanged and only missing glyphs use a new font (`D06 T03 §7`, pdfcraft #1026). Done when: the font resource names of unchanged runs match before and after.
- [ ] Assert rotated, two-column, table-cell and right-to-left fixtures edit without merging blocks and stay in logical order. Done when: the paragraph counts and extraction order are unchanged except the edited block.
- [ ] Assert idempotence across repeated edits and across save and reopen, and that an edit followed by undo restores byte-identical streams. Done when: ten repeated edits equal one and undo restores the original hashes.
- [ ] Assert structure tree, `/ParentTree`, `/ToUnicode` and optional-content membership after each edit on the tagged and layered fixtures. Done when: the consistency checks of `§6` pass on the saved files.
- [ ] Assert the differential suite of `D06 T03 §4` still passes with `EditModel` included, and that a fuzz pass over the edit entry points (`fuzz/fuzz_edit.cpp`, started from the corpus) finds no crash in one minute under ASan. Done when: both pass in CI.
- [ ] Assert a hostile fixture (deep `q` nesting, a 100000-operator page, an oversized image dictionary) returns a bounded `PdfError` or `Truncated` model without crash. Done when: ASan reports clean.
- [ ] Commit: `test(edit): add producer fixture matrix and Edit PDF round-trip suite`

**Test checkpoint:** `recto-tests.exe --gtest_filter=EditMatrix.*:Corpus.Provenance` passes, and having the rewrite engine inherit the first run's state makes `EditMatrix.RoundTrip` fail on the chrome-skia and word fixtures.

## Verification

- `recto-tests.exe --gtest_filter=EditModel.*:ParagraphModel.*:EditSession.*:EditFonts.*:ParagraphRewrite.*:AddText.*:ImageEdit.*:EditDelete.*:EditSave.*:VectorEdit.*:AppTests.Edit*:AppTests.Inline*:A11y.Edit*:A11y.Inline*:Cli.Edit*:EditMatrix.*` all pass (engine suites under ASan).
- Retyping a paragraph in each producer fixture keeps neighbours byte-identical, keeps the original font for present glyphs, refuses an undrawable character with its code point, and a deleted image's bytes are absent from the saved file when nothing else uses it, and a shared image is kept while any other use remains.
- The open viewer document is unchanged after any failed or cancelled edit, save or export.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
