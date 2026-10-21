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

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:00:20Z
- Effort: medium
- Lines: 25
- Digest: 5dd12eb6ff08ff9c

Reviewed the supplied diff. Repository reads failed twice because the command runner could not initialize, so cross-file verification remains incomplete. No files were modified; git and validation were not run.

All section references below are to `todo/03-text/TODO-03-annotations-and-forms.md`.

**record: needs-attention**

1. **Earlier finding 4 remains partially unresolved (§47, transform checklist).** The replacement explicitly updates an arc’s centre, radii and rotation, but non-uniform scaling of a rotated ellipse also requires remapping its angular parameters. Transforming the old `/InkList` without regenerating it using §46’s sampling policy can also fail the reopening comparison. Require preservation of transformed endpoints and sweep, regeneration of the sample from the transformed spec, and a test using a rotated, partial ellipse with unequal scale factors. The current “moves and scales” test can pass using an axis-aligned arc and uniform scale.

2. **Earlier finding 7 remains partially unresolved (§25 CSV checklist; §40 CSV checklist).** The added goldens test serialized characters, not interpretation in the target spreadsheet applications requested by the finding. The seven-character rule also omits locale-sensitive full-width formula prefixes. Specify supported application/import paths and verify that hostile cells remain text there. OWASP explicitly identifies these prefixes and warns that quote-based protection can fail after Excel saves and reopens a CSV. [OWASP CSV Injection](https://community.owasp.org/attacks/CSV_Injection)

**consistency: needs-attention**

3. **§40’s `HostileHeaders` assertion contradicts §25’s shared escaping contract.** §40 requires every listed header to be “quoted and prefixed.” Under §25, `@SUM(1)` receives a prefix but requires no CSV quotes; a name merely *containing* a tab, comma and quote requires quoting but receives no prefix unless its first character triggers one. A conforming implementation therefore fails the prescribed test. Give exact fixture strings and expected serialized cells, or change the shared policy consistently.

4. **§52’s new containment rule rejects built-in stickers.** The preceding requirement installs read-only packs beside the executable, but the new rule requires loader and apply paths to resolve beneath `AppPaths::data_dir()\stickers\`. Those requirements conflict for an installed application whose data directory differs from its installation directory. Define separate trusted built-in and user-pack roots, constrain each operation to the appropriate root, and test applying a built-in sticker in that configuration.

**source-defect: advisory**

5. **§46’s numerical justification is incorrect.** For a circle, the maximum segment angle is `2 * acos(1 - tolerance / radius)`. At `5000 pt` and `0.05 pt`, a full circle requires 703 segments, or 704 points with a repeated closing endpoint, not 717 points. With 719 segments, the limiting radius is approximately `5237.92 pt`, so `5000 pt` is a conservative cap, not the largest possible radius claimed by the text. State the full-angle formula and point-count convention explicitly.

**adversarial: needs-attention**

6. **§47 does not define atomic rejection when scaling violates §46’s limits.** A valid `5000 pt` arc enlarged by a factor of two exceeds the new radius cap. The transform requirement nevertheless says to rewrite member geometry and regenerate appearances without specifying preflight or rollback. One actor operation alone does not guarantee all-or-nothing mutation. Require validation of every transformed member before committing changes, and test that an over-limit resize leaves the entire group unchanged through save and reopen.

OVERALL: needs-attention

## Response to round 2

1. Fixed: D03 T03 §47 transform item now remaps an arc exactly (SVD of `M_lin * R(rotation) * diag(rx, ry)` gives the new rotation, radii and `start_deg - beta`, sweep kept, positive scale only) and regenerates `/InkList` and `/AP` from the new spec with the §46 rules; `PdfAnnotGroup.ArcSurvivesTransform` now uses a rotated partial ellipse scaled by `(2, 0.5)` and checks start, middle and end points, sweep, the fresh sample and no demotion after reopen.
2. Partly fixed: D03 T03 §25 `csv_escape_cell` now also prefixes the full-width `＝`, `＋`, `－`, `＠` (OWASP lists them), states the contract (first open or import as CSV; re-saved CSV is outside it, `.xlsx` string cells of §41 named for spreadsheet work) in `csv_writer.h` and both verbs' `--help`, and `CsvEscaping` parses the output back with an RFC 4180 reader to check row and column counts and that no parsed cell starts with a trigger; tests do not launch a spreadsheet application because Recto has zero Office dependencies (D7, D10), and the §41 `XlsxOutput` test already re-reads `=1+1` as text.
3. Fixed: D03 T03 §40 `HostileHeaders` now gives four exact fixtures and their serialized cells under the §25 two-step rule (prefix first, then quote), including a prefixed-and-quoted full-width case; §25 states the two steps and their order explicitly.
4. Fixed: D03 T03 §52 `StickerLibrary` takes `StickerRoots` (`builtin` at `<exe-dir>\stickers\` read-only, `user` at `AppPaths::data_dir()\stickers\`), each operation is confined to its root, and `PdfStickers.BuiltinRoot` applies a built-in sticker with the exe and data directories in unrelated folders.
5. Fixed: D03 T03 §46 sampling now states `theta_max = 2 * acos(1 - tolerance / r)`, `n` segments and `n + 1` points with a repeated closing point, 703 segments (704 points) for `5000 pt`, and the `5000 pt` cap as conservative below the `5237.92 pt` limit; `PdfArc.Sampling` checks the exact point counts 24, 72, 224 and 704.
6. Fixed: D03 T03 §47 adds an all-or-nothing item (stage and validate every member against §46 and §48 limits before any write) and `PdfAnnotGroup.AtomicReject`, which scales a group holding a `5000 pt` arc by 2 and finds every member byte for byte unchanged through save and reopen; the Test checkpoint quotes that error.
