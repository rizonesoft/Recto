## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T01:58:50Z
- Effort: medium
- Lines: 29
- Digest: 7df3f0ec63b9ec72

Reviewed the supplied diff. Repository reads failed because the local command runner could not initialize, so cross-file verification remains incomplete. No files were modified; git and validation were not run.

All section references below refer to `todo/03-text/TODO-03-annotations-and-forms.md`.

**source-defect: needs-attention**

1. **§32: PDF417 quiet zone is underspecified below its minimum.** The requirement permits one module and calls that the standard minimum. PDF417 requires two module widths around the symbol. Correct the minimum and test the actual output border, since successful decoding of generous test images does not prove compliant spacing. See the [PDF417 implementation documentation](https://barcode4j.sourceforge.net/trunk/symbol-pdf417.html).

2. **§38, build step 1: `/IF` is placed outside `/MK`.** The prescribed dictionary closes `/MK` before specifying `/IF`. For widget icons, `/IF` belongs inside the appearance characteristics dictionary: `/MK << /TP 1 /I ... /IF << ... >> >>`. Otherwise other viewers can ignore the fit settings while Recto’s generated `/AP` makes its own tests pass. Require inspection of the dictionary nesting and appearance regeneration in another viewer. See [Adobe PDF Reference, table 8.36](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=626).

**consistency: needs-attention**

3. **§46: accepted radii contradict the sampling accuracy and point cap.** A full circle of radius `1e6 pt` cannot meet `0.05 pt` chord error with at most 720 points. Even 720 segments produce approximately `9.52 pt` error. The allowed radius, error bound and cap need a consistent rejection or subdivision policy. Also require adaptive Bézier subdivision to meet the absolute error bound; merely limiting pieces to 90 degrees does not guarantee it. Test near the maximum accepted radius, not just 5, 50 and 500 pt.

4. **§47 together with §§46 and 49: group transforms omit exact arc geometry.** Group movement rewrites `/InkList` and `/Rect`, but not the private `ArcSpec` centre and radii. On reopening, §46’s mismatch detector therefore demotes a Recto-moved arc to ordinary ink, losing editability. Define subtype-aware transforms for move and resize, including private geometry, and test grouping, transforming, saving and reopening an arc without demotion.

**integration: needs-attention**

5. **§§25–29: the exporter interface cannot supply its consumers’ required inputs.** `write(summary, path)` has no explicit source-document context, password, layout options or cancellation token. Yet the PDF exporter must copy source page resources, the extraction exporter must reopen the source, the CLI selects layouts, and the dialog promises cancellation. Define the export request and document ownership contract in §25 before implementing consumers. Include the handoff of unsaved actor edits; reopening the original file alone would export stale comments.

6. **§36: field detection requires information absent from its input and result contracts.** `detect_form_fields(DisplayList, PageText, DetectOptions, CancelToken)` must avoid existing widgets, generate document-wide unique names and consume scanned ruling lines, but no contract supplies those inputs. `FieldProposal` also omits the `MaxLen` required for comb fields and the grouping/export-value information needed to create useful radio groups. Define these data explicitly and test accepted proposals through save, reopen and filling, rather than only checking detected rectangles.

**adversarial: needs-attention**

7. **§§25 and 40: CSV protection does not cover the stated threat.** Checking only a leading `=`, `+`, `-` or `@` misses dangerous control-character prefixes and spreadsheet-specific interpretation. In §40, hostile field names also become header cells and need the same protection as values. Specify protection for every untrusted cell, including headers, and test tabs, CR/LF, embedded separators and quotes in the target spreadsheet applications. The current single-formula golden can pass with these cases vulnerable. See [OWASP CSV Injection](https://community.owasp.org/attacks/CSV_Injection).

8. **§§52–53: sticker path confinement is incomplete.** Imported `pack.json` supplies a pack `id` and sticker `file` paths, while persistence constructs `stickers\<pack-id>\` and export writes pack contents. The checklist does not explicitly constrain manifest-derived paths during loading, persistence, removal and export. An import-folder check alone does not protect those later operations or a modified on-disk manifest. Define validated identifiers and relative asset paths, enforce containment at each filesystem operation, and test traversal and absolute paths in both manifest fields.

OVERALL: needs-attention

## Response to round 1

All 8 findings were verified and fixed in the next commit:

1. D03 T03 §32: PDF417 quiet zone is 2 modules (QR 4, Data Matrix 1), measured by `PdfBarcode.QuietZone`.
2. D03 T03 §38: `/IF` is nested inside `/MK` (ISO 32000-1 Table 189), asserted on saved bytes by `PdfImageField.MkNesting`.
3. D03 T03 §46: radius capped at 5000 pt (717 points within the 720 cap at 0.05 pt); Bezier pieces halve from 90 degrees until the measured error is at most 0.05 pt; tests at 5000 and 5000.01.
4. D03 T03 §47: group moves go through a subtype-aware `transform_annotation` that rewrites arc geometry; `PdfAnnotGroup.ArcSurvivesTransform`.
5. D03 T03 §25 to §29: an `ExportRequest` with a `SourceSnapshot` (staged revision or saved file) feeds every exporter; `PdfCommentSummary.UnsavedEdits`.
6. D03 T03 §36: `DetectInput` carries existing widgets, names and rule lines; proposals carry `max_len`, `radio_group` and `export_value`; accept, save, re-open and fill tests.
7. D03 T03 §25, §40: one shared `csv_escape_cell` for every untrusted cell including headers; `CsvEscaping` and `HostileHeaders`.
8. D03 T03 §52: pack ids and file names are restricted and re-validated at every use, with canonical containment and reparse points refused; `PdfStickers.PathConfinement`.
