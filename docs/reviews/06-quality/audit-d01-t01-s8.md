# Audit - D01 T01 §8: Bi-level and image stream filters (`CCITTFaxDecode`, `JBIG2Decode`, WIC `DCT` and `JPX`)

- **Audit section:** `D06 T05 §22` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §8` (`docs/reviews/d01-t01-s8.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `ab5ea35`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 17 tests.` (17 tests from 1 suite). The checkpoint holds.

## Round 1: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T13:49:14Z
- Effort: xhigh
- Lines: 46
- Digest: 2ccd7f83378e97cf

## adversarial: needs-attention

New reproduction cases below are source-derived; they were not executed.

1. **[P1] CCITT working memory exceeds the promised ceiling.** `src/pdf/filters.cpp:1875`, `:2188`: transition vectors grow outside the allocation budget. Reproduce with `K=0`, `Columns=67108864`, `Rows=1`, `EndOfBlock=false`, encoding alternating one-pixel white/black runs. The dimension check accepts exactly 256 MiB, but `cur_changes` and its copied reference require approximately 512 MiB together, plus output and vector growth.

2. **[P1] Small CCITT streams can cause quadratic decoding work.** `src/pdf/filters.cpp:1891`: every 2D operation searches the reference transitions from the beginning. Reproduce with two identical 524288-pixel alternating rows, encoding the second with repeated `V(0)` operations. The bitmap passes a 4 MiB dimension check, while reference searches require approximately 69 billion comparisons. JBIG2 MMR uses the same routine.

3. **[P1] JBIG2 accepts an incomplete EOFB as a successful bitmap.** `src/pdf/filters.cpp:2768`: the second EOL’s failure is discarded, and remaining rows retain their initial white pixels. Reproduce with an 8×2 page and immediate MMR generic region whose entire compressed payload is `00 10`, one EOL followed by padding. Decoding returns a white page instead of `DamagedFormat`.

## consistency: approve

## integration: needs-attention

4. **[P1] Indirect image `/Decode` arrays bypass the promised inversion.** `src/pdf/filters.cpp:6069`, `:9477`: the pipeline forwards or copies the reference, but `apply_pdf_decode_array` only accepts a direct array. Reproduce by replacing the direct array in the CMYK pipeline test at `tests/recto-tests/pdf_filters_tests.cpp:2706` with `42 0 R` and supplying a resolver returning that same array. The direct form produces magenta; the indirect form silently skips the mapping. The later indirect filter-parameter resolution does not cover this image entry.

## source-defect: needs-attention

5. **[P1] Unknown-height JBIG2 pages lose later stripes.** `src/pdf/filters.cpp:3606`, `:5748`: maximum stripe size becomes the permanent page height, and End-of-Stripe segments are ignored. Reproduce with an 8-pixel-wide page, height `0xFFFFFFFF`, striping `0x8001`, and two one-row regions separated by End-of-Stripe segments ending at rows 0 and 1. The second region is clipped and the decoder returns one row. The final stripe establishes the actual page height under [T.88 §7.4.9–10](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.88-200002-S%21%21PDF-E&lang=e&type=items).

6. **[P2] Consecutive JBIG2 MMR halftone planes lack byte alignment.** `src/pdf/filters.cpp:2794`: the next plane starts at the exact bit offset rather than the next byte. The fixture at `tests/recto-tests/pdf_filters_tests.cpp:1657` incorrectly concatenates unaligned planes and therefore conceals the defect. Reproduce with that fixture’s first plane padded from 38 to 40 bits before the second plane. The valid byte-aligned encoding is misread. [T.88 §6.2.6](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.88-200002-S%21%21PDF-E&lang=e&type=items) requires each MMR invocation to begin and end on byte boundaries.

7. **[P1] JBIG2 arithmetic halftones ignore `HENABLESKIP`.** `src/pdf/filters.cpp:5501`, `:5558`: flag bit `0x08` is never read, and generic decoding has no skip-mask input. Reproduce with an arithmetic halftone grid containing cells wholly outside the region and `HENABLESKIP=1`, encoded with those cells omitted. Recto consumes arithmetic symbols for skipped cells, corrupting subsequent pattern indices. This contradicts [T.88 §6.6.5 and Annex C](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.88-200002-S%21%21PDF-E&lang=e&type=items).

8. **[P1] DCT decoding ignores `/ColorTransform`.** `src/pdf/filters.cpp:6169`, `:6313`: the parameter is resolved by the pipeline but never affects decoding; WIC’s RGB interpretation is always used. Reproduce with a non-neutral three-component JPEG without an Adobe marker and `/DecodeParms << /ColorTransform 0 >>`. The required untransformed samples instead undergo colour conversion. [PDF Reference, Table 3.11](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf) explicitly distinguishes these modes.

9. **[P1] Native JPX decoding ignores per-component precision and sampling.** `src/pdf/filters.cpp:7516`, `:8354`, `:9060`: component zero supplies the bit depth and signedness for every component, and component bounds omit `XRsiz`/`YRsiz`. Reproduce with a valid codestream using different component precisions, or chroma sampled at 2×2 relative to luma. Reconstruction uses incorrect component geometry or sample normalization. These are per-component SIZ fields under [T.800 §A.5.1](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.800-200208-S%21%21PDF-E&lang=e&type=items).

10. **[P1] JPX applies image decode mappings when the specification requires ignoring them.** `src/pdf/filters.cpp:7441`, `:9073`: both paths apply `/Decode` unconditionally. Reproduce by decoding `seed-filter-09-jpx.bin` through an image dictionary with `/Decode [0 0]`, no `/ColorSpace`, and `ImageMask=false`. Its gray samples become zero instead of remaining 128. This violates the JPX condition in [ISO 32000-2, image dictionary `Decode` entry](https://developer.adobe.com/document-services/docs/assets/5b15559b96303194340b99820d3a70fa/PDF_ISO_32000-2.pdf).

11. **[P2] CCITT damaged-row recovery does not implement the specified behaviour.** `src/pdf/filters.cpp:2285`, `:2332`: consecutive damaged rows repeatedly copy the previous recovered row, and mixed Group 3 decoding provides no recovery. Reproduce with `EndOfLine=true`, `DamagedRowsBeforeError=2`, a non-white good row, then two damaged rows. The second substitution should be white. A corresponding `K>0` stream is rejected instead of recovered. See [PDF Reference, CCITT parameter table](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf).

## design: advisory

Not applicable: this section owns no user surface.

## record: needs-attention

12. **[P2] The original review records JPEG 2000 capabilities that never existed in its stamped candidate.** `docs/reviews/01-engine/D01-T01-s8.md:12`, `:15`, `:19` claim 32 decomposition levels and all six standard code-block style flags. Both `56de060` and current code reject levels above 10 and every nonzero style (`src/pdf/filters.cpp:7608`, `:7627`, `:7725`). Reproduce by changing byte 58 of an in-memory copy of `seed-filter-09-jpx.bin` from `0x00` to `0x02`. Its empty packet remains valid with context reset enabled, but Recto rejects the header. These are standard [T.800 coding styles](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.800-200208-S%21%21PDF-E&lang=e&type=items).

13. **[P3] The claimed Adobe CMYK/YCCK verification is missing.** `tests/recto-tests/pdf_filters_tests.cpp:2243`, `:2667`: the generated JPEG contains no Adobe APP14 marker, uses neutral samples, and supplies no YCCK case. Applying a separate PDF inversion array does not exercise Adobe marker interpretation or YCCK conversion, despite the checked promise at `todo/01-engine/TODO-01-bridge-runtime.md:319`.

Checkpoint verification used the existing x64 Release binary: all 17 `PdfFilters.*` tests and `Support.AsanActiveWhenEnabled` passed, reporting `ASan=enabled`. No rebuild was performed. The additional instrumentation death test could not create its temporary file under read-only permissions. Graph validation and plan checks passed; no files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

Thirteen findings. The hostile-input, malformed-input and integration defects (the failure modes `AGENTS.md` names) are fixed here. The codec conformance gaps are filed as four codec-scoped sections with owners, and `D06 T05 §22` carries their deferrals.

Fixed, each with a test that fails on the code before it:

- Finding 1 (P1): correct. CCITT's two transition buffers (an `int32_t` per change, up to one per column) were outside the ceiling, and each row copied one into the other. `/Columns` is now bounded so both buffers fit the same ceiling as the output, and the buffers are swapped between rows, not copied (also in JBIG2 MMR). `PdfFilters.CcittAndMmrBounds` refuses 200,000 columns at a 1 MiB ceiling with `DecompressionLimitExceeded`.
- Finding 2 (P1): correct. `find_b1_b2` searched the reference line from its start for every code, so a row cost quadratic time; `a0` never moves back within a row, so the search now resumes from a per-row cursor. The same test decodes two 131,072-pixel alternating rows (the second all `V(0)`) to the expected pattern in 55 ms; the old code takes 24 s and fails the 3 s bound.
- Finding 3 (P1): correct. In JBIG2 MMR data, a lone EOL where the EOFB should be ended the region as a finished white page. An EOFB is two EOLs, so one alone now refuses the region (`DamagedFormat`), and a whole EOFB still ends it.
- Finding 4 (P1): correct. An indirect image `/Decode` reached `apply_pdf_decode_array`, which reads only a direct array, so the mapping was silently skipped. The pipeline now builds DCT and JPX image parameters in one place and resolves `/Decode` one level through the caller's resolver. `PdfFilters.DctRgbAndCmyk` adds the magenta case with `/Decode 42 0 R`.
- Finding 10 (P1): correct. ISO 32000-2 Table 87 says a `JPXDecode` image that is not an `ImageMask` ignores `/Decode`. The pipeline now keeps `/Decode` for JPX only when `/ImageMask` (resolved one level) is true. `PdfFilters.CorpusImageSeedsDecode` decodes the JPX seed with `/Decode [0 0]` and keeps 128.

Filed:

- Findings 5, 6 and 7 (JBIG2 striped pages of unknown height, MMR halftone plane byte alignment and its concealing fixture, `HENABLESKIP`): `D06 T05 §31`.
- Findings 9 and 12 (JPX per-component precision and subsampling; decomposition levels above 10 and the code-block style flags the original review recorded as supported): `D06 T05 §32`, which also corrects that record.
- Findings 8 and 13 (DCT `/ColorTransform` ignored; the Adobe APP14 CMYK and YCCK verification that never existed): `D06 T05 §33`.
- Finding 11 (CCITT damaged-row substitution and mixed Group 3 recovery): `D06 T05 §34`.

Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 125 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 18 tests.`; `python scripts/todo-graph.py validate` -> `valid: 687 sections`.

## Round 2: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T14:15:24Z
- Effort: xhigh
- Lines: 30
- Digest: 396b5eaac591e68e

## adversarial: needs-attention

Findings below are source-derived. The new reproduction cases were not executed.

1. **[P1] JBIG2’s memory budget excludes bitmap metadata.** `src/pdf/filters.cpp:3772`, `:4564` charge only `s.data.size()` before copying whole `Jbig2Bitmap` objects into retained dictionaries. Reproduce with a 1 MiB limit: create a dictionary containing 1,024 one-pixel symbols, then 32 retained dictionaries that each import and export those symbols without adding new ones. The retained objects alone occupy about 1.25 MiB on x64 Release, while the counter charges roughly 66 KiB. Per-symbol heap allocations increase the discrepancy. This scales beyond the default 256 MiB ceiling without triggering its guard.

2. **[P1] Empty JBIG2 arithmetic payloads produce successful images.** `src/pdf/filters.cpp:2618`, `:2623` synthesize `0xFF` indefinitely after exhaustion; generic-region decoding never validates termination before returning success at `:2933`. Reproduce by removing `gr.insert(gr.end(), 16u, 0xFFu)` from the fixture at `tests/recto-tests/pdf_filters_tests.cpp:1528`, leaving its segment header, dimensions and adaptive-template bytes intact. The empty payload behaves identically to the existing all-`0xFF` payload. Missing arithmetic data is therefore reported as a decoded bitmap, contrary to the malformed-input contract and [T.88 Annex E’s termination procedure](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.88-200002-S%21%21PDF-E&lang=e&type=items).

## consistency: approve

## integration: needs-attention

3. **[P1] Indirect numbers inside image `/Decode` arrays still disable the mapping.** `src/pdf/filters.cpp:9502` resolves the array object only; `:6103` reads its elements directly and silently abandons the entire mapping when an element is a reference. Reproduce using the existing CMYK magenta pipeline case: replace its first `0` with `42 0 R`, with the resolver returning integer `0`. The equivalent direct array produces magenta; the referenced element causes the mapping to be skipped. PDF permits indirect references as array elements. [Adobe PDF Reference, §4.11](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.2.pdf).

## source-defect: needs-attention

4. **[P1] JBIG2 symbol dictionaries ignore retained arithmetic contexts.** `src/pdf/filters.cpp:3727` never reads flag bits 8 and 9; `:3783` instead initializes fresh generic and refinement contexts for every dictionary. Reproduce with dictionary A retaining its learned bitmap contexts (`0x0200`) and dictionary B referencing A and using those contexts (`0x0100`), with matching templates and adaptive pixels. Encode B using A’s learned contexts. Recto resets them, corrupting glyphs or rejecting valid data. [T.88 §7.4.2.2](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.88-200002-S%21%21PDF-E&lang=e&type=items) requires restoring and preserving these statistics.

5. **[P1] Native JPX decoding silently ignores region-of-interest scaling.** `src/pdf/filters.cpp:7968` and `:8038` skip `RGN` (`0xFF5E`) markers; coefficient reconstruction at `:8925` has no ROI scaling state. Reproduce on the native fallback with a lossless, unsigned grayscale image containing non-neutral samples, encoded using OpenJPEG’s `-n 1 -r 1 -ROI c=0,U=4`. Compare Recto’s samples with the original and reference decoder. Recto interprets the shifted coefficients without the required realignment, producing incorrect pixels. This is separate from the filed component-geometry and coding-style gaps. [T.800 §A.6.3 and Annex H](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.800-200208-S%21%21PDF-E&lang=e&type=items).

## design: advisory

Not applicable: no user surface.

## record: advisory

The existing x64 Release binary passed all 18 `PdfFilters.*` tests and `Support.AsanActiveWhenEnabled`, reporting `ASan=enabled`. No rebuild was performed, so current x64/ARM64 compilation was not independently reverified. Graph validation and the generated-plan check passed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P1): correct. JBIG2's decode budget charged a retained symbol only its pixel bytes, while each copy also costs its `Jbig2Bitmap` object and allocation, so many tiny symbols retained across dictionaries outgrew the budget. Every one of the 19 charges now uses `jbig2_footprint` (the object plus its pixel storage's capacity). `PdfFilters.Jbig2Decode` decodes its symbol-and-halftone page at a 256-byte ceiling, which pixel bytes alone fit but the footprints do not, and refuses with `DecompressionLimitExceeded`; at 4 KiB it decodes. The old accounting passes at 256, so the test fails on it.
- Finding 2 (P1): partly agreed. The MQ decoder reading `0xFF` past the end is T.88 Annex E's specified marker behaviour, normal near the end of real coded data and what other readers do, so an underrun is not refused. A region with no coded data at all cannot be a coded region: all five JBIG2 arithmetic starts (generic, refinement, symbol dictionary, text and halftone) now refuse an empty payload with `DamagedFormat`. `PdfFilters.Jbig2Decode` checks the reviewer's case (the arithmetic generic fixture with its payload removed) for every template and TPGDON setting.
- Finding 3 (P1): correct. Round 1 resolved the `/Decode` array but not its elements. Indirect numbers inside it now resolve one level into a copy (bounded at 64 elements, 32 components). `PdfFilters.DctRgbAndCmyk` adds the magenta case with the first element `43 0 R`; it fails on the round 1 code.
- Findings 4 and 5 (P1): conformance features filed with their codecs. Retained JBIG2 symbol-dictionary contexts (flag bits 8 and 9, T.88 7.4.2.2) join `D06 T05 §31`, and JPX region-of-interest scaling (`RGN`, T.800 A.6.3, Annex H) joins `D06 T05 §32`, each with its own checklist item and test.

Gates after round 2: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 125 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 18 tests.` (the 256-byte budget case refuses in Release as in Debug).
