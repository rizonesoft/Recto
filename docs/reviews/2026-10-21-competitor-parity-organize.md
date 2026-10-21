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
