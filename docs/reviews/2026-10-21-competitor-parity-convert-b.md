## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T02:02:16Z
- Effort: medium
- Lines: 27
- Digest: dbd32cfc9ce29d3f

Review is limited to the supplied candidate diff. Direct repository reads failed because the command runner could not start, so surrounding contracts remain unverified.

**source-defect: needs-attention**

1. **PDF/UA-2 is incorrectly allowed alongside PDF/A-3.** `todo/04-convert/TODO-05-excel-web-and-compliance.md`, §24 permits combining UA-2 with `3a`, `3b` or `3u`. PDF/A-3 is based on PDF 1.7; PDF/UA-2 requires PDF 2.0. Reject these combinations too. Supporting combined conformance requires PDF/A-4. Add rejection tests for all three PDF/A-3 levels. [PDF Association standards table](https://pdfa.org/pdf-standards/)

2. **Keeping `/Mask` unchanged corrupts colour-key transparency.** `todo/04-convert/TODO-06-print-production.md`, §18 converts multicomponent images to `DeviceGray` while explicitly preserving `/Mask`. A colour-key mask contains two integers per source colour component, evaluated against source samples. An RGB mask cannot remain unchanged on a one-component image; simply converting its endpoints also cannot preserve distinctions between colours that collapse to the same grey. Derive an explicit mask from the original samples before conversion and test transparent and opaque colours with equal grey values. [ISO 32000-1, §8.9.6.4](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf)

3. **The ICC acceptance rule rejects valid grey profiles.** `todo/04-convert/TODO-06-print-production.md`, §18 says to refuse a profile whose “header class or colour space is not grey.” `GRAY` is a data colour-space signature, not a profile class. Specify acceptable profile classes and supported transform direction separately from requiring `GRAY`. Test a valid output-class grey profile. [ICC profile specification](https://www.color.org/icc1-v41.pdf)

**consistency: needs-attention**

4. **Partial equation parses can replace complete source expressions.** `todo/04-convert/TODO-05-excel-web-and-compliance.md`, §§18–19 promises that incomplete trees remain `Uncertain`, but the operational rule accepts confidence at `0.6`, derived from glyph consumption and overlap. A non-overlapping tree consuming 80% of an equation can therefore become `Parsed`, allowing export to discard the remaining expression. Require complete accounting of semantic glyphs independently of confidence. Test an otherwise valid expression with an unparsed trailing term.

5. **Greyscale conversion leaves the sources of regenerated colour unchanged.** `todo/04-convert/TODO-06-print-production.md`, §18 rewrites annotation/widget appearances but does not require conversion of annotation colour properties or inherited field `/DA` strings. Existing appearances can pass `colour.only_gray`, then editing a field regenerates coloured text. Convert the stored appearance-generating properties too and test editing and regenerating a widget after conversion. This also conflicts with the governing owning-layer rule. [ISO 32000-2 interactive-feature requirements](https://pdf-issues.pdfa.org/32000-2-2020/clause12.html)

**integration: needs-attention**

6. **The formula-tagging API omits an option its implementation requires.** `todo/04-convert/TODO-05-excel-web-and-compliance.md`, §20 defines `TagFormulaOptions` without `retag_existing`, then requires callers to pass it to permit retagging. Specify the option, its default and forwarding to `ReadingOrderEditor`; test both refusal and explicit retagging while preserving existing MCIDs.

**adversarial: needs-attention**

7. **The contrast sampler can omit the failing background and report `Passed`.** `todo/04-convert/TODO-05-excel-web-and-compliance.md`, §22 discards colours occupying less than 2% of the text bounding box. A small patch behind a character can fail contrast while occupying less than that threshold. Quantised gradients can also leave no qualifying colour, with no specified outcome. Measure relevant backgrounds at glyph locations or report incomplete coverage; add fixtures for both cases. WCAG provides no 2% exemption. [WCAG 2.2](https://www.w3.org/TR/WCAG22/)

8. **Adding invisible OCR text bypasses the scanned-page safeguard.** `todo/04-convert/TODO-05-excel-web-and-compliance.md`, §22 skips invisible OCR runs but marks scanned content unevaluated only when the page has no text layer. A scan with invisible OCR can consequently have zero evaluated runs and zero unevaluated runs, reaching `Passed` while its visible text was never checked. Track image-based text independently of the presence of a text layer. Test the same low-contrast scan before and after OCR. [WCAG 2.2 includes images of text](https://www.w3.org/TR/WCAG22/)

OVERALL: needs-attention

## Response to round 1

All 8 findings were verified and fixed in the next commit:

1. D04 T05 §24: `--level ua2` refuses every PDF/A level, since PDF/A-1 to 3 are PDF 1.x based.
2. D04 T06 §18: a colour-key `/Mask` becomes an explicit mask from the original samples before conversion; `FixupsGrey.ColourKeyMask`.
3. D04 T06 §18: grey ICC profiles are accepted on data space `GRAY` with class `mntr`, `prtr` or `scnr`; `link`, `abst` and `nmcl` are refused.
4. D04 T05 §18: a region is `Parsed` only when every non-whitespace glyph is consumed.
5. D04 T06 §18: greyscale also converts annotation and widget colours, `/DA`, `/DS` and `/RC`; `FixupsGrey.RegeneratedAppearance`.
6. D04 T05 §20: `retag_existing` (default false) controls retagging, keeping MCIDs and `/ActualText`.
7. D04 T05 §22: every pixel in each glyph quad is read and the lowest ratio reported; unsampled runs are `NotEvaluated`.
8. D04 T05 §22: image-based text is tracked independently of any text layer; a low-contrast scan stays `Incomplete` before and after OCR.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:02:26Z
- Effort: medium
- Lines: 19
- Digest: 9f13db4760fdb17f

Review limited to the supplied diff: repository reads failed because the command runner could not start. Surrounding contracts remain unverified. No files were modified and no git commands ran.

**record: needs-attention**

1. **Earlier finding #5 remains partially unresolved.** `todo/04-convert/TODO-06-print-production.md`, §18 restricts `/DA` conversion to fields and `/AcroForm`, omitting the `/DA` on a FreeText annotation itself. A plain FreeText annotation can retain a red default appearance while its converted `/AP` passes `colour.only_gray`; regenerating the appearance restores red text. Include annotation-level `/DA` and add a FreeText regeneration test without `/DS` or `/RC`. The existing widget test cannot catch this. [PDF reference, Free Text Annotations, Table 8.17](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.4.pdf)

**consistency: needs-attention**

Finding 1 leaves the promised conversion of stored appearance-generating properties incomplete.

**source-defect: needs-attention**

Finding 1 overlooks a separately defined annotation property, distinct from field and AcroForm default appearances.

**adversarial: needs-attention**

2. **The exhaustive contrast sampler lacks a sampling-work budget and inner-loop cancellation.** `todo/04-convert/TODO-05-excel-web-and-compliance.md`, §22 now reads every pixel for every glyph quad, but limits only raster size and run count, polling cancellation every 256 runs. Those limits do not bound repeated pixel visits: one run containing many large glyph quads can repeatedly scan substantial portions of the 16-million-pixel plane before reaching another cancellation check. The oversized-page test can pass without exercising this case. Cap cumulative pixel visits and glyph processing, poll cancellation inside sampling loops, and test a single pathological run on a page below the raster cap. Budget exhaustion must produce an incomplete or truncated result.

OVERALL: needs-attention

## Response to round 2

1. Fixed: D04 T06 §18 now converts every stored `/DA` including the annotation-level `/DA` of `FreeText` (ISO 32000-1 Table 174) and of `Redact` with `/OverlayText` (Table 187), adds `FixupsGrey.RegeneratedFreeText` (red `/DA`, no `/DS` or `/RC`, regenerated through `D03 T03 §2`), adds both annotations to `greyscale-mixed.pdf`, and lists `D03 T03 §2` and `D03 T03 §6` as dependencies of the §18 row.
2. Fixed: D04 T05 §22 caps glyph quads per page at `1 million` and cumulative pixel visits at `64 million`, polls the `CancelToken` every `65536` pixel visits inside the sampling loop, marks the run in progress and later runs `NotEvaluated(sampling budget)` with a `Truncated` result and at most `Incomplete`, and adds `Contrast.SamplingBudget` (one pathological run below the raster cap) and `Contrast.CancelInsideRun`, named in the Test checkpoint.
