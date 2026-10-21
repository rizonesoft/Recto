## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T02:06:00Z
- Effort: medium
- Lines: 47
- Digest: 52790a23ca767067

Review scope: the supplied candidate diff and supplied AGENTS.md. Local reads failed before PowerShell started, so surrounding repository context remains unverified. No files were modified and no git commands ran.

References below use **T01** for `todo/05-organize/TODO-01-page-operations.md` and **T02** for `todo/05-organize/TODO-02-organize-ui.md`.

**source-defect: needs-attention**

1. **T01 §19: additive Roman numerals contradict the required labels.** “Roman numerals in additive form” produces `iiii` for 4, whereas this section explicitly requires preserving `iv`. Specify subtractive notation for ordinary values, separately defining the extension above 3999. Pin literal expected strings rather than tests that reproduce the formatter. [Adobe’s page-label documentation](https://opensource.adobe.com/dc-acrobat-sdk-docs/library/pdfmark/pdfmark_Basic.html) explicitly uses `iv`.

2. **T02 §22: a data provider is not a drop-commit notification.** `SetDataProvider` runs when the target requests data, which can happen while inspecting an offered drag. It cannot guarantee “only when the drop happens.” The proposed implementation can create files during a subsequently cancelled drag. Define a supported deferred-transfer lifecycle, including cancellation and transfer completion before cleanup, and test a target requesting data before drop. [Microsoft API contract](https://learn.microsoft.com/ms-my/uwp/api/windows.applicationmodel.datatransfer.datapackage.setdataprovider?view=winrt-17134).

3. **T01 §25: the phone-number rejection checkpoint is impossible as written.** Changing one subscriber digit can produce another structurally valid E.164 number. The requirement to reject a one-digit change “in each” international detector incorrectly treats phone numbers as checksummed identifiers. Separate checksum corruption tests from numbering-format tests. [ITU E.164](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-E.164-201011-S%21%21PDF-E&lang=e&type=items) defines numbering structure, not the proposed error-detection property.

**consistency: needs-attention**

4. **T01 §23: byte-identical JPEG embedding conflicts with removing GPS metadata.** An unrotated, uncropped JPEG can contain EXIF GPS data. Requiring its original bytes in the PDF necessarily preserves that data, contradicting `NoMetadataLeak`. Preserve compressed image data while stripping sensitive metadata segments, and test the pass-through path with a GPS-bearing synthetic JPEG.

5. **T01 §16: same-path output conflicts with source preservation.** The section supports `dst_path == src_path` while requiring the source to remain unchanged after successful splitting. Both cannot hold. Separate copy creation from an explicitly requested replacement operation, and give each distinct postconditions and tests.

6. **T01 §6: removing managed headers cannot restore content that stamping permanently shrank.** The existing shrink option transforms content and annotation rectangles. The added removal step removes marks and demands original pixels, but supplies no restoration mechanism for those transformations. Define reversible storage and removal of the layout transformation, including repeated stamps and intervening edits. Test removal after stamping with shrink enabled.

**integration: needs-attention**

7. **T01 §30 and T02 §§19, 24: binder serialization cannot reliably preserve the Combine grid.** The UI permits arbitrary page ordering and interleaving, while the binder schema records per-file selections and `build_binder` concatenates items. No contract represents the global page sequence, per-page edits, or how repeated source segments map to one TOC entry. Saving and rebuilding can silently change the displayed document. Specify a lossless mapping and test an interleaved, reordered project through save, reopen and rebuild.

8. **T01 §30: nested TOC entries link to the wrong destination.** Layout creates entries for bookmarks down to `depth`, but building makes *every* entry link to its item’s first page. A chapter entry targeting page 20 therefore opens page 1. Carry each entry’s remapped destination through layout and test nested bookmarks, selected page ranges and the prepended TOC offset.

9. **T01 §21 and T02’s new page-tool coverage: bookmark-order reordering has no desktop consumer.** T01 adds `order_pages_by_bookmarks`, but T02 supplies neither an Organize action nor a command-box entry for it. This contradicts the stated UI coverage of T01 §§15 through 23. Add the command, preview/undo behavior and an explicit checkpoint, or explicitly scope this operation to CLI.

10. **T02 §32: the appearance-library ownership reference is wrong within the candidate.** It repeatedly locates the appearance library in §10, but the supplied diff adds that library under §11. Its prerequisite and storage integration therefore point executors at the wrong section. Reference the actual owner and require reopening a pressure-sensitive appearance after restart, not just rendering it immediately.

**adversarial: needs-attention**

11. **T01 §15: overwrite plus cancellation rollback can destroy existing outputs.** Existing names are permitted when overwrite is requested, but rollback only removes already-written parts. After replacing an existing file, deletion cannot recover its previous contents. Define transactional backups/restoration or forbid overwrite for this batch mode. Test cancellation and write failure after replacing the first of several pre-existing files, asserting their original bytes survive.

12. **T01 §16: retained annotations and widgets undermine the “only its own content” guarantee.** Crossing annotations are copied into multiple cells with unchanged geometry; widgets retain their field data and appearances. Those objects can carry text or image content from outside the cell even if page content streams are scrubbed. Specify clipping/sanitization of annotation appearances and field data, plus isolation of shared objects before mutation. Test extraction from the entire output object graph, including a widget and stamp crossing the cut.

13. **T01 §24: whole-page redaction does not remove all remaining references to sensitive data.** Removing page `/Annots` does not remove a widget’s field value from catalog `/AcroForm /Fields`. Likewise, page resources can keep old content reachable even after replacing `/Contents`; a reachability-only rewrite will retain them. Explicitly rebuild resources and prune affected field trees. The blanket requirement that former fonts/images disappear is also impossible when unredacted pages legitimately share them. Test unique sensitive objects disappearing while shared resources and unaffected fields survive.

14. **T01 §26: saved marks are not bound to the document they were reviewed against.** `--marks-out` and `--marks` define no document fingerprint, revision identity or geometry validation. Marks reviewed on one PDF can be applied to a different or modified PDF, leaving the intended sensitive text untouched while returning success. Define a bounded marks schema tied to the exact input revision and reject mismatches. Test changed bytes, reordered pages and changed page boxes.

15. **T01 §29 and T02 §31: downloaded trust updates have no defined authorization before activation.** Manual import requires fingerprint confirmation, but the downloader and `trust update` do not specify whether changed lists remain pending or immediately replace enabled anchors. This is especially consequential because XML signatures are deliberately unverified. Define pending download state and confirmation for every changed list before it can affect `ValidTrusted`; test that an unconfirmed update cannot change verification results. Merely displaying the downloaded file’s own hash does not independently authenticate its source.

16. **T01 §19: tree bounds do not bound generated label allocations.** A small hostile tree can contain a huge `/St` or prefix. Repeated-letter labels and repeated `M` output can allocate enormous strings; preview multiplies that across up to 100,000 pages. Add checked arithmetic, per-label byte limits and an aggregate preview budget. Test extreme `/St` values and long prefixes, not only tree depth and entry count.

17. **T01 §30: the CLI explicitly puts passwords in process arguments.** `--password <file>=<pw>` conflicts with AGENTS.md’s prohibition on credentials in arguments and exposes passwords through command history and process inspection. Specify interactive secret input or another protected input channel, and test that diagnostics and invocation metadata contain no password.

OVERALL: needs-attention

## Response to round 1

16 findings were verified and fixed in the next commit, and one was partly rejected:

1. D05 T01 §19: subtractive Roman numerals, tested against literal strings.
2. D05 T02 §22: the drag data provider creates its temp file on first request and removes it on cancel or after transfer; `AppTests.DragCancelled`.
3. D05 T01 §25: one-digit rejection applies only to checksummed detectors; E.164 is format-only.
4. D05 T01 §23: JPEG scan data is kept and EXIF and other metadata segments are dropped (ICC and Adobe kept).
5. D05 T01 §16: `split_page` writes a new file and refuses the source path; `PageSplit.SamePathRefused`.
6. D05 T01 §6: the shrink is stored reversibly in `/PieceInfo` and restored on removal; `StampWatermark.RemoveRestoresShrink`.
7. D05 T01 §30, D05 T02 §24: binder projects carry an exact `page_sequence`; interleaved round-trip tests.
8. D05 T01 §30: nested contents entries carry their own remapped destinations.
9. D05 T02 §19: `Order pages by bookmarks` with preview, undo and `Ctrl+K`.
10. Partly rejected: D05 T02 §32 correctly cites the appearance library in D05 T02 §10; the restart test for pressure appearances was added.
11. D05 T01 §15: parts go to temp files and existing outputs to `.recto-bak` backups restored on cancel or failure.
12. D05 T01 §16: annotations crossing a cut are flattened and crossing widgets' fields dropped; the test walks the reachable object graph.
13. D05 T01 §24: whole-page redaction empties resources and prunes widgets and fields, keeping shared objects used elsewhere.
14. D05 T01 §26: marks files are bound to the input hash, page count and page geometry; `MarksDocumentMismatch`.
15. D05 T01 §29, D05 T02 §31: downloaded trust lists stay `PendingConfirmation` until the person confirms.
16. D05 T01 §19: `/St`, prefix, label and preview budgets are bounded.
17. D05 T01 §30: the CLI takes passwords only by prompt or stdin; `Cli.BinderNoPasswordLeak`.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:05:04Z
- Effort: medium
- Lines: 27
- Digest: b4e797714766aab1

Review limited to the supplied diff and instructions: both repository-read attempts failed before PowerShell started. No files were modified and no git commands ran. `T01` and `T02` below refer to the two candidate files under `todo/05-organize/`.

**record: needs-attention**

1. **Finding 2 remains incomplete: cancellation can race deferred file creation.** **T02 §22** orders immediate folder deletion when a drag is cancelled, but does not require cancelling and awaiting an in-flight `extract_pages` request first. Cancellation during extraction can therefore delete the folder before the producer finishes, leaving a subsequently created file behind. `DragCancelled` can pass by cancelling only after data production completes. Specify producer cancellation, completion and cleanup ordering, and test cancellation during extraction as well as after data delivery.

2. **Finding 6 remains incomplete: removing nested shrink marks can strand the transformation.** **T01 §6** retains an inner shrink when an outer shrink exists, but does not say how its restoration record survives removal of its owning mark or when that retained shrink is unwound. Removing inner mark A and then outer mark B can leave A’s transformation permanently applied. Require persistent ownership of retained transformations and explicit final geometry assertions for both removal orders. Merely “covers a double stamp” does not establish restoration.

3. **Finding 13 remains incomplete for fields shared across pages.** **T01 §24** removes each redacted widget “together with its field,” while requiring an unredacted field and unredacted page pixels to survive. A single field can own widgets on both redacted and retained pages. Removing that field can break the retained widget; keeping it can retain the sensitive value. Define this case explicitly, including any refusal or reported destructive treatment, and test a single field with widgets on both pages. The current checkpoint can pass using two unrelated fields.

**consistency: needs-attention**

4. **Crossing links lose their function.** **T01 §16** retains a crossing link with a clamped `/Rect` and “no other data.” That removes its destination or action, producing an inert annotation and conflicting with the next step’s promise to remap link destinations. Preserve or reconstruct the permitted destination and required annotation entries while removing unrelated payloads. Test activating both resulting links, not just their presence.

5. **Whole-page redaction’s empty resources conflict with overlay text.** **T01 §24** requires optional overlay text but assigns a “new empty `/Resources`.” A text overlay needs its font resources. Require a fresh resource dictionary containing only resources used by the replacement content, and test a nonempty overlay after reopening the output. Otherwise an implementation can satisfy the resource-removal checkpoint while breaking `--label`.

**source-defect: needs-attention**

6. **The new literal page-label expectations encode the wrong alphabetic sequence.** **T01 §19** pairs values `26, 27, 52, 53, 703` with `Z, AA, AZ, ZZ, AAA`. PDF alphabetic labels repeat the same letter: `26 → Z`, `27 → AA`, `52 → ZZ`, `53 → AAA`; `703` produces 28 `A` characters. `AZ` is not part of this sequence. These assertions would reject a correct formatter or encourage an incorrect one. Use explicit `(style, value, expected)` cases, also separating uppercase and lowercase Roman expectations. See the numbering-style definition in the [PDF Reference, page-label dictionary](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf).

**adversarial: needs-attention**

7. **The rollback backup namespace is not protected.** **T01 §15** introduces `.recto-bak` siblings without specifying collision handling or exclusive reservation. A pre-existing backup can cause a commit-time rename failure or be overwritten by an implementation using replacement semantics. The rollback test only covers the three destination files, so it cannot detect destruction of an unrelated backup. Reserve unique backup paths without replacement before committing any output, retain backups if restoration fails, and test pre-existing backup names plus a failure during restoration.

**record advisory:** Finding 10’s disputed §10/§11 ownership cannot be adjudicated from this diff because the appearance-library definition is absent and repository reads failed. Also, **T02 §32** names `SignatureUi.PressureAppearanceRestart` in its new requirement but does not add it to the displayed test command. That command can pass without executing the new persistence check.

OVERALL: needs-attention

## Response to round 2

1. Fixed: D05 T02 §22 now orders drag cleanup as mark the session ended, cancel the session `CancelToken`, await the in-flight `extract_pages` producer, then delete the folder, with late requests creating nothing; added `AppTests.DragCancelledDuringExtraction` (cancel while a test gate holds the producer mid-write) and `AppTests.DragLateRequest` to the step, the test list and the checkpoint.
2. Fixed: D05 T01 §6 moves a retained inner shrink record into `/PieceInfo /Recto /Private /RetainedShrinks` tagged with its blocking outer mark and unwinds it (`shrink_unwound`) once no outer shrink remains; added `StampWatermark.RemoveNestedShrinkBothOrders` asserting the original `/Contents` list, link `/Rect` and pixels after both removal orders, quoted in the checkpoint.
3. Fixed: D05 T01 §24 defines a field with widgets on redacted and unredacted pages: it keeps its value because the unredacted page still shows it (the shared-font rule), loses only the redacted pages' widgets and is reported in `shared_fields_kept`, while a field whose visible widgets are all on redacted pages is removed with its value; added `RedactPages.SharedFieldAcrossPages` with one shared and one page-only field, `PageRedactionReport`, and the `shared_fields_kept` list in D05 T02 §28's apply result.
4. Fixed: D05 T01 §16 keeps a crossing link's `/Dest` or `GoTo`, `GoToR` or `URI` action, `/Border` or `/BS`, `/H`, `/F` and clipped `/QuadPoints`, drops `/Contents`, `/AP`, `/NM`, `/PA`, `/StructParent` and other action types; added `PageSplit.CrossingLinksActivate` hit-testing both halves of a `GoTo` and a `URI` link through `PdfLinkAnnotation` (`D01 T01 §4`) after reopen, quoted in the checkpoint.
5. Fixed: D05 T01 §24 gives a redacted page a fresh `/Resources` holding only what the replacement uses (one new font subset `/F0` for overlay text, none otherwise); added `RedactPages.OverlayTextAfterReopen` and a `--label REDACTED` readback in `Cli.WhiteoutAndRedactPages`.
6. Fixed: D05 T01 §19 now states the ISO 32000 Table 159 repeated-letter rule (value `n` is letter `(n - 1) % 26` repeated `(n - 1) / 26 + 1` times) and tests a literal `(style, value, expected)` table with separate upper and lower Roman cases, `52 -> ZZ`, `53 -> AAA`, `703 ->` 28 `A`s, and `PageLabels.LabelTooLong` returning `LabelOutOfBounds` instead of truncating.
7. Fixed: D05 T01 §15 moves each existing destination aside to a unique `<name>.recto-bak-<32 hex>` with `MoveFileExW` without replacement before any part commits, restores without replacement, keeps a backup whose restore fails and returns the new `PdfErrorCode::RollbackIncomplete`; added `SplitBySize.BackupNameCollision` and `SplitBySize.RestoreFailureKeepsBackups`, quoted in the checkpoint.
8. Partly fixed (advisory): the §10 versus §11 ownership is not changed because D05 T02 §10 holds the step "Keep a signature appearance library in `AppPaths::data_dir()`" (§11 begins after it); the §32 test step and checkpoint now run `SignatureUi.PressureAppearanceRestart` and `SignatureUi.AppearanceLibrary`.
