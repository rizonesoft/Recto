## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T02:00:35Z
- Effort: medium
- Lines: 41
- Digest: c4b73a211df7029f

Review scope: supplied diff and supplied AGENTS.md instructions. Direct repository reads failed because the execution helper could not start, so surrounding repository contracts remain unverified. No files were modified and git was not used.

References below use `T02` for `todo/04-convert/TODO-02-pdf-conversions-a.md` and `T04` for `todo/04-convert/TODO-04-office-to-pdf.md`.

**source-defect: needs-attention**

1. **T04 §14: OpenType fonts cannot universally be embedded as `CIDFontType2`.** OpenType can contain CFF outlines, whereas `CIDFontType2` requires TrueType outlines. Dispatch embedding and subsetting by outline technology, with a CFF fixture as well as TrueType. Otherwise valid OFD fonts produce invalid PDF font dictionaries or incorrect rendering. See [PDF specification, §9.7.4 and §9.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf).

2. **T04 §18: `application/pkcs7-mime` does not mean encrypted.** It also carries opaque signed messages. The prescribed blanket `EncryptedMessage` refusal rejects readable signed mail and misreports its condition. Inspect the CMS content type; distinguish signed data from encrypted data, and test both. See [RFC 8551, §§3.2 and 3.5.2](https://www.rfc-editor.org/rfc/rfc8551.html).

**consistency: needs-attention**

3. **T02 §17: the page geometry requirements contradict each other.** `PageLoop` must preserve source `/MediaBox` and `/Rotate`, but the implementation renders with CropBox and rotation already applied, then writes a CropBox-sized `/MediaBox`. A cropped, rotated page cannot satisfy both contracts; retaining rotation also risks rotating the rendered image twice. Specify one coordinate model and test a nonzero CropBox origin with unequal MediaBox/CropBox sizes and rotations of 90 and 270 degrees.

4. **T02 §19: JPEG re-encoding contradicts preservation of figure pixels.** Background cleanup re-encodes colour photographs at JPEG quality 90, while `BackgroundCleanup` requires a figure region to remain byte-identical to the scan. Lossy re-encoding cannot guarantee that. Require lossless output for the preservation claim, or explicitly define and test an accepted visual difference.

5. **T04 §20: the binary detector rejects the UTF-16 input it must accept.** Counting raw NUL bytes against a 5% threshold rejects ordinary UTF-16 English text, where approximately half the bytes are NUL. Make the binary check encoding-aware and specify its position after BOM recognition/decoding. Test both UTF-16 endiannesses with predominantly ASCII characters.

6. **T04 §18: nested-message depth and its acceptance test disagree.** The implementation allows four levels, but `NestedMessages` requires rendering two levels and embedding the third. Define whether the root counts toward the limit and test the actual boundary and one level beyond it.

**integration: needs-attention**

7. **T04 §21: OFD dispatch can ship before OFD text and completion features.** Its dependency row requires §13, but not §14 or §15, although its contract includes text, attachments and seal counts from those sections. A ready executor can therefore expose graphics-only OFD conversion that silently loses invoice text. Depend on the completed OFD pipeline and require selectable text and seal reporting in the dispatch fixture.

8. **T04 §§11 and 18: explicitly consumed implementations are not prerequisites.** §11 consumes §9’s paragraph/list parsing, but depends only on §§8 and 2. §18 consumes §20’s plain-text path, but does not depend on §20. Declare these semantic dependencies or move the shared implementations into an earlier prerequisite. These are missing producer dependencies, not graph-format errors.

**adversarial: needs-attention**

9. **T04 §§9 and 10: independent row/column caps still permit roughly 17.2 billion expanded cells per table or sheet.** A tiny document can repeat a nonempty cell across 16,384 columns and 1,048,576 rows while respecting both limits. Add aggregate expanded-cell, allocation and work budgets before expansion, plus checked multiplication. The attack fixture must repeat nonempty rows and cells together, not merely exceed one dimension.

10. **T02 §24: paper-style generation has no bounds on its loop-driving parameters.** `spacing_pt` accepts no stated positive minimum or finite-number check. Zero spacing can prevent progress; tiny spacing can produce enormous dot grids within the permitted page dimensions. Bound spacing, staff counts and generated primitives, reject non-finite values, and test cancellation during style generation rather than only between pages.

11. **T02 §17: “one page bitmap” is not a memory bound.** DPI is capped, but rendered dimensions, total pixels and allocation bytes are not. A hostile large page can exhaust memory with a single bitmap. Validate checked dimensions and a concrete resource budget before rendering, or require bounded tiles/bands. The 50-page allocator-count test does not detect this failure.

12. **T02 §19: flat-border detection does not establish that erasing a text region is safe.** A region can have a uniform border while containing a diagram, watermark or coloured interior. Painting the entire region with its border median destroys those pixels despite the promise to preserve non-text content. Require an interior/background safety check or conservative fallback, and test text overlapping a figure whose surrounding border is uniform.

13. **T02 §3: long-image splitting has no defined outcome when one page exceeds the height limit.** “Close at the last page that fits” has no progress case when the first page cannot fit. This can cause empty parts, repeated retries or cap violations. Specify a typed refusal or an explicit within-page splitting policy, and test a single oversized page.

14. **T02 §§17 and 25: the stated mutation checkpoints cannot prove their claims.** Removing a `PageText` assertion does not itself make `KeptAndDropped` fail. Likewise, §25 expressly creates a private temporary PDF before Save, so “creating the file before the user saves” cannot inherently fail the close-prompt assertion. Replace these with observable failures: seeded forbidden content must be detected; an unsaved tab must retain dirty state; and no user-destination file may appear before Save.

OVERALL: needs-attention

## Response to round 1

All 14 findings were verified and fixed in the next commit:

1. D04 T04 §14: fonts embed by outline type (TrueType as `CIDFontType2` with `/FontFile2`; CFF OpenType as `CIDFontType0` with `/FontFile3 /Subtype /OpenType`); `OfdText.CffFont`.
2. D04 T04 §18: the CMS content type is read with `CryptMsgGetParam`; opaque signed, enveloped and corrupt CMS each get their own result.
3. D04 T02 §17: one geometry model (displayed CropBox size with rotation applied, origin `0 0`, `/Rotate 0`); `ImageOnlyPdf.Geometry`.
4. D04 T02 §19: cleaned images are lossless `FlateDecode` and the claim is pixel equality of figure regions.
5. D04 T04 §20: the binary check runs on decoded characters after encoding detection; BOM-less UTF-16 is detected first.
6. D04 T04 §18: nesting levels are defined from 0 with levels 1 to 4 rendered and 5 or deeper embedded; tests at 4, 5 and 6.
7. D04 T04 §21: depends on §15 and uses an OFD invoice fixture.
8. D04 T04 §11 depends on §9, and §18 on §20.
9. D04 T04 §9, §10: an aggregate budget of 4,000,000 expanded cells checked with overflow-safe arithmetic.
10. D04 T02 §24: finite, bounded parameters, a primitive cap per page and cancel polling; `BlankPdf.CancelDuringStyle`.
11. D04 T02 §17: a checked pre-check refuses pages over 16384 pixels per side or 268 megapixels with `PageTooLarge`.
12. D04 T02 §19: regions with non-text interior pixels stay image; `EditableOcr.FigureUnderText`.
13. D04 T02 §3: a page taller than the cap is refused with `PageTooTallForLongImage` and nothing is written.
14. D04 T02 §17, §25: checkpoints fail on retained fonts, text, annotations or forms, and on a clean tab or files outside the private temp folder.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:01:10Z
- Effort: medium
- Lines: 21
- Digest: 221d9c98e7cd228f

Review scope: supplied candidate diff. Direct repository reads failed twice because the execution helper could not start (`helper_unknown_error`). Surrounding contracts remain unverified. No files were modified; git was not used.

**record: needs-attention**

Round 1 findings **3 and 9 remain partially unresolved**. Finding **5’s encoding-order requirements remain contradictory**, despite the added acceptance tests. Details follow.

**consistency: needs-attention**

1. **`todo/04-convert/TODO-02-pdf-conversions-a.md`, §17: the writer still specifies unrotated page dimensions.** The revised Build order requires a rotation-applied CropBox size with `/Rotate 0`, but the unchanged writing step requires `/MediaBox` equal to the source CropBox size. For a nonsquare page rotated 90 or 270 degrees, these prescribe different dimensions. Round 1 finding **3** therefore remains. Make the writing step use the same displayed dimensions and image transform as `ImageOnlyPdf.Geometry`.

2. **`todo/04-convert/TODO-04-office-to-pdf.md`, §20: encoding detection still has two incompatible orders.** The first step explicitly specifies BOM, strict UTF-8, MLang, then Windows-1252. The next step says to detect BOM-less UTF-16 “first.” Mostly-ASCII UTF-16 bytes, including their NUL bytes, can pass strict UTF-8 validation, so following the first step still produces the rejection identified in Round 1 finding **5**. Specify one ordered detection algorithm, placing BOM-less UTF-16 recognition before UTF-8 validation while preserving explicit overrides.

**adversarial: needs-attention**

3. **`todo/04-convert/TODO-04-office-to-pdf.md`, §10: the new budget excludes empty cells that still drive expansion.** Only *non-empty* expanded cells count toward the four-million limit; only *trailing* empty repeats are ignored. A row containing 16,383 repeated empty cells followed by one nonempty cell, repeated 1,048,576 times, stays below the nonempty-cell budget while permitting approximately 17.2 billion expanded positions. The new all-nonempty attack fixture does not catch this. Round 1 finding **9** remains for ODS. Bound all materialized cells and traversal work, or require sparse range handling with a bounded pagination budget; test repeated leading/interior empty cells followed by nonempty content.

**source-defect: advisory**

Repository context and external standards/API verification remain incomplete. This lens does not constitute approval of the new CFF or CMS prescriptions.

OVERALL: needs-attention

## Response to round 2

1. Fixed: T02 §17 context paragraph and writing step now use the displayed size (`/MediaBox [0 0 W H]` with the CropBox swapped for `90` and `270`, `cm` operands equal to it, `/Rotate 0`), split into its own checklist item asserted by `ImageOnlyPdf.Geometry`; the Test checkpoint now fails on the unrotated CropBox size or a copied `/Rotate`.
2. Fixed: T04 §20 context paragraph and detection step now give one ordered algorithm (override, BOM, BOM-less UTF-16 by NUL pattern, strict UTF-8, MLang, Windows-1252) in `detect_text_encoding`, the binary check no longer restates a competing order, and the new `TextToPdf.DetectionOrder` plus the Test checkpoint fail if UTF-8 validation runs before UTF-16 recognition.
3. Fixed: T04 §10 repeat step now counts every materialized cell position, empty or not, against the `4,000,000` budget (row repeat times the larger of `1` and the positions up to the last non-empty cell, overflow-safe), keeps only trailing empty runs sparse, and `OdfToPdf.OdsRepeatCaps` adds leading-empty, interior-empty and interior-empty-row cases; the Test checkpoint fails when only non-empty cells are counted.
