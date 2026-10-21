## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T01:56:58Z
- Effort: medium
- Lines: 35
- Digest: a7a3021af64518d7

Review scope: the supplied candidate diff. Local file reads failed before PowerShell started, so repository-only context could not be verified. No files were modified; git and validation were not run.

**source-defect: needs-attention**

1. **Type 3 outline extraction is not implementable through the named API.** `todo/03-text/TODO-04-edit-pdf.md` §21 requires Type 3 glyph procedures to pass through `IDWriteFontFace::GetGlyphRunOutline`. PDF Type 3 procedures are not DirectWrite font faces, and arbitrary procedures need not represent monochrome outlines. Specify a bounded shared-interpreter conversion for supported procedures and explicit refusals for the remainder. [DirectWrite font formats](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ne-dwrite-dwrite_font_face_type), [GetGlyphRunOutline](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritefontface-getglyphrunoutline).

2. **Keeping `/Mask` unchanged can corrupt filtered images.** `TODO-04-edit-pdf.md` §25 treats `/Mask` as independent mask bytes, but it can be a colour-key array tied to source sample values. Inverting those samples changes which pixels are transparent; RGB-to-gray conversion also changes the required array dimensionality. Resolve colour-key transparency before filtering and preserve its resulting coverage. Add colour-key fixtures, including grayscale conversion. [ISO 32000-1, §8.9.6.4](https://developer.adobe.com/document-services/docs/assets/35e4369068f86065372c18787171a17e/PDF_ISO_32000-1.pdf).

3. **Existing axial/radial shadings do not necessarily have recoverable gradient stops.** `TODO-04-edit-pdf.md` §26 promises to read and edit their “stops, angle and radius” without restricting their function representation. Sampled and calculator functions, and general stitching functions, need not map losslessly to the proposed stop editor. Define the supported function subset and refuse unsupported representations without mutation. [PDF Association function and shading reference](https://pdfa.org/download-area/cheat-sheets/Color.pdf).

**consistency: needs-attention**

4. **The OCR writer can delete unrelated invisible text.** `todo/03-text/TODO-01-selection-and-search.md` §19 identifies an entire page’s OCR layer by `3 Tr` plus image coverage or `ScannedRaster` classification. Neither establishes which runs belong to the reviewed OCR result. The section also conflicts with §15’s explicit support for zones on digital pages. Require run-level provenance, preserve unrelated invisible text, and define replacement versus insertion on mixed pages. Pixel identity cannot detect this semantic loss.

5. **Stitching requests values its renderer rejects.** `TODO-01-selection-and-search.md` §13 lowers DPI as far as `36`, but §11 accepts only `72..1200`, and every strip must use §11. Align the contracts and test a stitched image that fits only below `72` DPI.

6. **Join cannot satisfy the prescribed z-order guarantees.** `TODO-04-edit-pdf.md` §23 relocates all joined runs to the first paragraph’s range while requiring an interleaved shape to retain its paint relationship. For paint order `paragraph A → overlapping shape → paragraph B`, moving B into A’s range changes that relationship. Preserve distributed ranges or refuse the join before mutation, consistently with §5.

**integration: needs-attention**

7. **The revised paragraph checkpoint requires its own later consumer.** `TODO-04-edit-pdf.md` §2 now requires vertical runs to receive §18’s supported/refused classification, while §18 consumes §2. The declared graph can remain valid while the implementation contract is circular. Keep §2’s initial refusal until §18 extends it, or move foundational classification before §2’s checkpoint.

8. **Vertical tests consume fixtures scheduled after those tests.** `TODO-04-edit-pdf.md` §18 explicitly uses fixtures from §30, but §30 depends on §§18 and 19 being completed. Put the required fixtures in the producing section or an earlier fixture section so the executor can pass the checkpoint before advancing.

**adversarial: needs-attention**

9. **Raw-byte absence does not prove removal of PDF text.** `TODO-01-selection-and-search.md` §19 makes absence of the old word in file bytes the decisive anti-duplication check. Compressed streams or font-encoded character codes can hide that byte sequence while leaving the original text extractable. Require decoded semantic extraction to exclude the old word and detect duplicate layers, including compressed and custom-encoded fixtures.

10. **OCR save has no binding to the document version reviewed.** `TODO-01-selection-and-search.md` §19 reopens the saved pathname but does not require its contents to match the source of the in-memory corrections. An external replacement can make valid edits target a different document. Bind edits to an immutable source snapshot or verified source identity and reject stale saves before writing. Test replacement of the input after review begins.

11. **Reflow silently removes legitimate repeated content.** `todo/03-text/TODO-02-accessibility.md` §10 drops untagged text repeated at the same position on three pages without restricting detection to header/footer regions. Repeated body instructions or table labels satisfy that rule. Constrain artifact inference and test repeated body content that must remain accessible.

12. **Suspect identities become unsafe after edits change word counts.** `TODO-01-selection-and-search.md` §§16 and 18 identify suspects and actions by `word_index`, while supporting deletion, insertion and asynchronous suspect calculation. No requirement rejects an action computed against an older snapshot. A preceding deletion can redirect a correction to another word. Use stable word identities or version-bound actions, and test stale results after insert/delete/undo.

OVERALL: needs-attention

## Response to round 1

All 12 findings were verified and fixed in the next commit:

1. D03 T04 §21: Type 3 glyphs convert only through the shared interpreter when they hold paths and fills only; otherwise refused; `Outlines.Type3Refusal`.
2. D03 T04 §25: colour-key `/Mask` is resolved to an explicit coverage mask before filtering; `ImageFilter.ColourKey`.
3. D03 T04 §26: gradient editing is limited to Type 2 functions (or Type 3 stitching of Type 2) on `/Domain [0 1]`; others refused; `ObjectAppearance.FunctionRefusal`.
4. D03 T01 §19: only `3 Tr` runs matching a reviewed word by text and quad are replaced; `OcrWrite.UnrelatedInvisibleText`.
5. D03 T01 §11, §13: 36 to 1200 DPI with bound tests, and a stitch case that fits only at 40 DPI.
6. D03 T04 §23: join is refused when a non-text object is painted between the paragraphs; `ParagraphJoinSplit.Interleaved`.
7. D03 T04 §2, §18: §2 keeps vertical runs `NotEditable`; §18 narrows that later.
8. D03 T04 §18, §30: §18 produces the vertical fixtures; §30 only uses them.
9. D03 T01 §19: proof is decoded text extraction with OCR off on compressed and custom-encoded fixtures; the pixel check is labelled look-only.
10. D03 T01 §19: a `SourceIdentity` recorded at first OCR publish rejects a changed file with `StaleSource`.
11. D03 T02 §10: repeated text is dropped only in the top or bottom 10% of the page; body and table repeats are kept.
12. D03 T01 §16, §18: words carry a never-reused `word_id`; actions carry `(word_id, snapshot_version)` and a mismatch is `StaleSuspect`.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T04:59:13Z
- Effort: medium
- Lines: 21
- Digest: 942aafbd14ae25e5

Review is limited to the supplied candidate diff. Repository reads failed before PowerShell started (`helper_unknown_error`), so governing context could not be verified. No files were modified; git and validation were not run.

**record: needs-attention**

Round 1 findings **3, 4 and 10 remain incompletely resolved**, as detailed below.

**consistency: needs-attention**

1. **Text-and-quad matching does not establish OCR provenance.** `todo/03-text/TODO-01-selection-and-search.md` §19, Treatment and layer-detection checklist. This leaves finding **4** unresolved. A foreign invisible run can have the same text and geometry, while the original run will no longer match a corrected word’s text or a deleted word. The contract does not retain an immutable mapping from original words to source operator ranges, resolve ambiguous matches, or distinguish an unmatched replacement from a genuinely new word. Its fallback can therefore preserve the wrong layer and append another, or replace foreign content. Require original source-range provenance, explicit replacement/deletion/insertion semantics, and refusal of ambiguous matches. Test identical overlapping foreign text and replacement/deletion after the reviewed snapshot changes.

2. **Join still permits changing the paint order of unrelated text.** `todo/03-text/TODO-04-edit-pdf.md` §23, Build order 1 and per-run-state checklist. The refusal checks only intervening **non-text** objects, but the checklist requires preserving every other object’s paint order. For selected paragraphs A and B with an unselected overlapping text object C painted between them, moving B into A’s ranges changes B’s relationship to C. Refuse intervening unselected painted text too, or preserve distributed ranges. Add an A → C → B fixture with overlapping text.

**source-defect: needs-attention**

3. **The gradient subset still includes representations the stop model does not account for.** `todo/03-text/TODO-04-edit-pdf.md` §26, existing-gradient checklist. Finding **3** remains unresolved: `/Domain [0 1]` does not constrain a Type 2 function to linear interpolation. `/N 2`, for example, produces a nonlinear ramp; Type 3 stitching also carries `/Encode` mappings and potentially discontinuous boundaries. The candidate neither preserves these parameters nor precisely excludes unsupported cases. “Any other stitching layout” does not define that boundary. Require faithful round-trip preservation or an explicit narrower subset, with fixtures for nonlinear exponents, reversed encoding and discontinuities. Refusing sampled functions alone does not test these cases. [ISO 32000-1, §§7.10.3 and 7.10.4](https://developer.adobe.com/document-services/docs/assets/35e4369068f86065372c18787171a17e/PDF_ISO_32000-1.pdf).

**adversarial: needs-attention**

4. **Source identity is captured too late and checked separately from the bytes consumed.** `todo/03-text/TODO-01-selection-and-search.md` §19, `SourceIdentity` checklist. Finding **10** remains unresolved in two windows. If the pathname is replaced after the viewer opens the document but before first OCR publication, the recorded hash can identify the replacement while corrections describe the original. A replacement after the comparison but before the isolated document opens also passes the prescribed check. The single replacement-after-review test covers neither case. Bind identity to the actual immutable input used by OCR, then make the writer consume that same snapshot or a verified, protected handle. Test replacement before first publication and between verification and writer open.

OVERALL: needs-attention

## Response to round 2

1. Fixed: `D03 T01 §19` records an immutable `OcrLayerMap` from each original word id to its single matching `3 Tr` run when the OCR result is first published; edits address words by id (`replace`, `delete`, `add`), ambiguous or mixed pages refuse with `AmbiguousTextLayer`, and `OcrWrite.AmbiguousLayer` and `OcrWrite.ReplaceAndDeleteByProvenance` test both cases.
2. Fixed: `D03 T04 §23` refuses a join when any other painted object, including unselected text, lies between the ranges, and `ParagraphJoinSplit.Interleaved` adds the A, unselected text C, B fixture.
3. Fixed: `D03 T04 §26` narrows the editable subset to linear type 2 functions (`/N 1`) and one-level stitching of them with unit `/Encode` and continuous boundaries, refusing everything else; `ObjectAppearance.FunctionRefusal` adds `/N 2`, reversed `/Encode` and discontinuous fixtures.
4. Fixed: `D03 T01 §19` binds OCR, the layer map and the writer to the one immutable byte snapshot the actor opened, and the writer never reopens the path; `OcrWrite.SnapshotIdentity` replaces the file before first publication, between review and save, and during the write. The Freeze check names the snapshot.
