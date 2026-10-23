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

## Round 3: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T14:36:38Z
- Effort: xhigh
- Lines: 30
- Digest: 96d0009c25e50e15

## adversarial: needs-attention

The following reproductions are source-derived and were not executed against the native decoder.

1. **[P1] JBIG2 MMR transition buffers remain outside the memory budget.** `src/pdf/filters.cpp:2782`: unlike the CCITT entry point, this path never bounds transition storage. Reproduce with a 1 MiB limit, an 8×2 page, and a 131000×2 MMR region: encode alternating one-pixel runs in row one and repeated `V(0)` operations in row two. The dimension guard accepts 1,048,000 bytes, but the bitmap and two transition buffers require at least 1,080,742 bytes, before capacity growth and other allocations.

2. **[P1] Text-region symbol references can multiply unbudgeted memory.** `src/pdf/filters.cpp:4673`, `:5059`: every referred dictionary appends its symbol pointers without a cumulative size check. Reproduce with a dictionary exporting 1,024 one-pixel symbols and a text region listing that dictionary 1,024 times. At a 1 MiB ceiling, `syms` alone reaches 8 MiB on x64 before payload validation. With 65,536 references it exceeds the default 256 MiB ceiling. Huffman decoding additionally allocates `sbsymcodes` from this unchecked count.

3. **[P1] Tiny MMR halftones can trigger billions of unnecessary compositing iterations.** `src/pdf/filters.cpp:5663`, `:5681`, `:2517`: compositing traverses each entire pattern even when its horizontal extent is completely outside the destination. Reproduce with two 255×255 patterns, a 255×255 region, `HGW=1048576`, `HGH=1`, `HGX=HGY=0`, `HRX=1`, `HRY=0`, and an all-white MMR grid selecting pattern zero. Dimensions pass, but rendering performs 68,183,654,400 inner iterations. This requires neither arithmetic coding nor `HENABLESKIP`.

## consistency: approve

## integration: needs-attention

4. **[P1] Round 2’s `/Decode` resolution introduces memory amplification before type validation.** `src/pdf/filters.cpp:9551`, `:9552`, `:9569`: referenced values are retained and deeply cloned before checking that they are numbers. `src/pdf/object.cpp:248` copies string bytes during cloning. Reproduce with a small JPEG and a `/Decode` array containing 64 references to one 8 MiB string. The accepted 64-element bound permits at least 512 MiB of cloned strings, plus retained resolver results and another array clone, without charging the 256 MiB budget. The malformed mapping is only discarded afterwards.

## source-defect: advisory

5. **[P2, owned by D06 T05 §31] Halftone grid vectors are incorrectly signed.** `src/pdf/filters.cpp:5565`: `HRX` and `HRY` pass through `int16_t`, although [T.88 Table 20](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.88-200002-S%21%21PDF-E&lang=e&type=items) defines unsigned 16-bit values. Reproduce with two black 1×1 patterns, a 129×1 region, a 2×1 all-zero index grid, and `HRX=0x8000`, `HRY=0`. Expected placements are x=0 and x=128; Recto places the second at x=-128 and clips it.

## design: advisory

Not applicable: no user surface.

## record: advisory

The existing x64 Release binary passed all 18 `PdfFilters.*` tests and `Support.AsanActiveWhenEnabled`, reporting `ASan=enabled`. Graph validation reported `valid: 687 sections`; the plan check reported `plan current`. No rebuild was performed, so current x64/ARM64 compilation and warning claims were not independently reverified. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

- Finding 4 (P1): correct, and a regression from my round 2 fix: `/Decode` elements were resolved and deep-cloned before their type was checked, so 64 references to one 8 MiB string retained and copied 512 MiB. Each element now resolves one level and only its number is kept; the first element that is not a number drops the whole mapping, as a malformed direct array does. No resolved object is retained or copied. The only difference from round 2 is memory retained, which no output observes, so this rests on the construction (a number per element, nothing else stored), stated here rather than claimed as tested.
- Finding 1 (P1): correct. JBIG2 MMR regions lacked CCITT's bound on the two transition buffers. They now get the same bound. `PdfFilters.CcittAndMmrBounds` refuses a one-row MMR region 200,000 pixels wide at a 1 MiB ceiling (its bitmap fits, its transition buffers do not), and the round 2 code accepts it.
- Finding 2 (P1), finding 3 (P1) and finding 5 (P2): fixed at their sites. Each symbol a text region refers to is charged (its pointer and Huffman code entry), compositing visits only the part of a source that lands inside the destination, and `HRX`/`HRY` are read unsigned (T.88 7.4.5.1, Table 20). These three shipped without a test that fails before them, because each needs a hand-built JBIG2 fixture (a text region listing a dictionary many times, a halftone grid far outside its region, an `HRX=0x8000` grid). Those fixtures are owed, each required to fail with its fix reverted, and are filed with a systematic JBIG2 allocation-and-loop pass as `D06 T05 §35`. JBIG2 has yielded a new resource site in each of rounds 2 and 3, and §35 replaces site-by-site discovery with an inventory.

Gates after round 3: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 125 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 18 tests.`; `python scripts/todo-graph.py validate` -> `valid: 688 sections`.

## Round 4: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T14:52:07Z
- Effort: xhigh
- Lines: 28
- Digest: 3874a16a07a7580c

## adversarial: needs-attention

New reproductions below are source-derived and were not executed against the native decoder.

1. **[P1] JPX tag-tree allocations exceed the default memory ceiling.** `src/pdf/filters.cpp:8536`, `:8578`, `:6759`: metadata accounting omits most storage allocated by the two tag trees per precinct. Reproduce through the native fallback with a 512×512 image, four components, one decomposition level, precinct exponents `(0,0)` at resolution 0 and `(1,1)` at resolution 1, and 524,288 empty LRCP packets. This creates 1,048,576 precinct/subband entries. Using the [MSVC vector layout](https://github.com/microsoft/STL/blob/main/stl/inc/vector), the guard charges approximately 209 MiB overall, while metadata alone requires at least 280 MiB, excluding allocator overhead. The 256 MiB ceiling therefore does not hold.

2. **[P1] CCITT bounds transition storage separately from output and ignores capacity growth.** `src/pdf/filters.cpp:2148`, `:2194`: reproduce using the existing two-row, 131,072-column alternating fixture at `tests/recto-tests/pdf_filters_tests.cpp:4002`, but pass `1u << 20u` as the ceiling. Both guards accept it. The two transition vectors require at least 1,048,568 bytes and output requires another 32,768 bytes, exceeding 1 MiB before vector capacity overhead. The round-1 memory fix remains incomplete.

## consistency: approve

## integration: needs-attention

3. **[P1] Direct malformed `/Decode` elements still bypass numeric validation before copying.** `src/pdf/filters.cpp:9559`, `:9590`, `:9594`: round 3 sanitizes only arrays containing references when a resolver exists. Reproduce with a tiny JPEG, no `/DecodeParms`, a direct `/Decode` array containing an 8 MiB literal string, and a 1 MiB decode ceiling. Copying the image dictionary duplicates that string; `deep_clone()` copies it again after the temporary entry is erased. The mapping is discarded only later by `apply_pdf_decode_array`. These allocations bypass the ceiling. `src/pdf/object.cpp:248` confirms string bytes are copied.

## source-defect: advisory

Previously identified conformance gaps remain owned by D06 T05 §31 through §34. The findings above concern allocation enforcement outside the deferred JBIG2 inventory.

## design: advisory

Not applicable: no user surface.

## record: advisory

The existing x64 Release binary passed all 18 `PdfFilters.*` tests plus `Support.AsanActiveWhenEnabled`, reporting `ASan=enabled`. Graph validation returned `valid: 688 sections`; the plan check returned `plan current`. No rebuild was performed, so current compilation and warning claims were not independently verified. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 4

- Finding 2 (P1): correct. CCITT and JBIG2 MMR bounded the two transition buffers and the output each against the whole ceiling, so together they could reach twice it, and the buffers grew by doubling. Both buffers are now reserved once at their exact maximum (`columns` entries: `push_change_point` keeps the stored positions strictly increasing below `columns`), charged first, and the output (CCITT) or bitmap footprint (MMR) gets what remains (`ccitt_transition_bytes`, `reserve_ccitt_transitions`). `PdfFilters.CcittAndMmrBounds` refuses the reviewer's 131,072-column case at 1 MiB, decodes it at 2 MiB with a measured peak within 2 MiB, and refuses a 131,000-pixel MMR row at 1 MiB (its transitions and bitmap together exceed it) while an 8,192-pixel row decodes; the round 3 code accepts both refused cases.
- Finding 3 (P1): correct, and the copy was wider than `/Decode`: the image stage copied its whole source dictionary (the image dictionary when there is no `/DecodeParms`), so any large entry was duplicated, and then deep-cloned `/Decode`. The stage now carries only what the image decoders read: `/ColorTransform` (an integer), `/TargetFormat` (`/BGRA` or `/CMYK`) and `/Decode` reduced to plain numbers, the array and each element resolved one level, direct or not; an entry that is not an array of at most 64 numbers drops the mapping before anything is retained or copied. The test binary gains `test::ScopedHeapMeter` (`tests/recto-tests/heap_meter.cpp`, replacing the global operator new and delete to count each block's usable size per thread), so memory claims are measured: `PdfFilters.DctRgbAndCmyk` decodes a JPEG whose direct `/Decode` holds an 8 MiB string at a 1 MiB ceiling, gets the unmapped image, and peaks below 1 MiB; the round 3 code peaks at 16,778,510 bytes.
- Finding 1 (P1): correct. JPX charged code-block and precinct records but not the two tag trees per precinct subband, which for many one-block precincts cost more than the records. `J2kTagTree::footprint` computes what `init` allocates (the level array, now reserved exactly, and two cell arrays per level, each rounded to the heap's 16-byte granularity plus a 16-byte header), and each precinct's two trees are charged before they are built. `PdfFilters.JpxTagTreesCharged` builds a 256x256 codestream with 65,536 1x1 precincts and empty packets: refused at 24 MiB, decoded to the level-shifted zero image at 64 MiB; the round 3 code decodes it at 24 MiB.
- Proof that the tests can fail: with only `src/pdf/filters.cpp` reverted to `ddbd06b`, x64 Debug `PdfFilters.*` -> `[  FAILED  ] 3 tests` (`DctRgbAndCmyk`: `actual: 16778510 vs 1048576`; `CcittAndMmrBounds`: the 1 MiB CCITT and 131,000-pixel MMR cases succeeded; `JpxTagTreesCharged`: the 24 MiB case succeeded); with the fix, `[  PASSED  ] 19 tests.`

Gates after round 4: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 159 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 19 tests.`

## Completion review: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T07:09:10Z
- Candidate: 9f9b9e68ee5a4ade2ae72c64520ea07dadc6d268
- Effort: xhigh
- Lines: 99
- Digest: 7b8d67fae7373f67

The candidate is incomplete for users, and superiority over Acrobat Pro is unproven. The existing x64 binary passed all 19 `PdfFilters.*` tests plus `Support.AsanActiveWhenEnabled`, reporting ASan enabled. I did not rebuild, execute new reproductions or modify files. New defect findings below are source-derived.

**Part 1: completeness**

1. **present: Standard fax decoding and parameter readers.** Group 3 1D/2D, Group 4, polarity and alignment have coverage in `PdfFilters.CcittGroup4` and `PdfFilters.CcittGroup3AndBlackIs1`. Defaults live in `src/pdf/filters.h:81`; readers for all eight CCITT parameters are at `src/pdf/filters.cpp:2125`. No additional user setting is required.

2. **missing-out-of-scope: Correct damaged-fax recovery.** Consecutive damaged rows repeat the previous row instead of substituting white after the first recovery; mixed Group 3 refuses recoverable rows. Evidence: `src/pdf/filters.cpp:2308` and `:2393`. Existing owner: **D06 T05 §34**, including the missing regression tests.

3. **present: Basic JBIG2 archive decoding and globals resolution.** Generic, symbol, text and halftone cases, `/JBIG2Globals`, malformed segment lengths and empty arithmetic payloads are exercised by `PdfFilters.Jbig2Decode` at `tests/recto-tests/pdf_filters_tests.cpp:1451`. Indirect globals retain the caller’s resolver at `src/pdf/filters.cpp:5944`.

4. **missing-out-of-scope: Remaining JBIG2 conformance.** Striped pages, byte-aligned MMR halftone planes, `HENABLESKIP` and retained dictionary contexts remain incomplete. Existing owner: **D06 T05 §31**, detailed at `todo/06-quality/TODO-05-stamp-audits.md:852`.

5. **missing-out-of-scope: Complete JBIG2 resource enforcement and regression evidence.** The systematic allocation and loop inventory remains outstanding, as do tests for repeated dictionary references, off-page compositing and unsigned `HRX`/`HRY`. Existing owner: **D06 T05 §35**, explicitly recorded at `todo/06-quality/TODO-05-stamp-audits.md:935`.

6. **present: Basic camera JPEG decoding and `/Decode` mappings.** Grayscale/RGB and basic CMYK paths, direct and indirect `/Decode` values, and expected pixels are exercised by `PdfFilters.DctRgbAndCmyk` at `tests/recto-tests/pdf_filters_tests.cpp:2662`. This does not establish the Adobe CMYK/YCCK claims covered by the next item.

7. **missing-out-of-scope: JPEG colour-transform correctness.** `/ColorTransform` reaches the pipeline but does not control WIC decoding. Non-neutral Adobe APP14 CMYK and YCCK verification is missing. Existing owner: **D06 T05 §33**, with its precise cases at `todo/06-quality/TODO-05-stamp-audits.md:896`.

8. **present: Basic native JPEG 2000 fallback.** Native decoding follows unsuccessful WIC decoding at `src/pdf/filters.cpp:7599`. `PdfFilters.CorpusImageSeedsDecode` and the JPX cases inside `PdfFilters.DctRgbAndCmyk` exercise decoded samples; `PdfFilters.JpxTagTreesCharged` checks a native metadata budget case.

9. **missing-out-of-scope: Remaining JPX component and coding support.** Mixed precision/signedness, subsampling, the full decomposition/style set and ROI realignment remain incomplete. Existing owner: **D06 T05 §32**, including regression fixtures, at `todo/06-quality/TODO-05-stamp-audits.md:874`.

10. **missing-in-scope: Embedded JPX colour, palette and channel semantics.** The native container reader extracts `ihdr` but discards colour, palette and channel-definition information (`src/pdf/filters.cpp:6594`). Output assumes RGB and treats component four as alpha (`:9217`). A palette image can consequently become grayscale, and four colour components can be misinterpreted as RGBA. The WIC path also chooses grayscale/RGB from codestream component count (`:7540`). Tests must cover a coloured palette, embedded colour specifications, CMYK and declared opacity. This is separate from §32’s component geometry and coding styles. [Adobe’s PDF reference](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.5_v6.pdf) specifies these JPX colour requirements.

11. **present: Filter dispatch, wrappers and indirect parameters.** The eight-stage chain limit, filter abbreviations, matching parameters and indirect resolution have coverage in `PdfFilters.ChainedPipeline`, `IndirectFilterAndDecodeParmsResolve` and `IndirectValuesAndFullChainsResolve`. See `src/pdf/filters.cpp:9389`.

12. **missing-in-scope: Bounded parameter resolution before decoding.** `deref_array` deep-clones every element before checking whether resolution changed anything (`src/pdf/filters.cpp:9418`). Resolving one recognised dictionary parameter also deep-clones unrelated entries (`:9468`). A tiny JPEG with `/DecodeParms [<< /Unused (8 MiB string) >>]`, a resolver and a 1 MiB decode ceiling therefore copies well beyond that ceiling before decoding. The new image-dictionary protection does not cover these paths. Neither reproduction has a regression test.

13. **present: Image-size refusals and several working-memory guards.** Oversized headers return `DecompressionLimitExceeded`; malformed headers return `DamagedFormat`. Evidence includes `src/pdf/filters.cpp:2162`, `:6132` and `:6680`, plus the oversized cases in `PdfFilters.DctRgbAndCmyk`. `CcittAndMmrBounds` and `JpxTagTreesCharged` test combined transition/output and tag-tree accounting. The default 256 MiB ceiling and caller overrides are read by the decoders. These checks do not prove the unresolved paths above are bounded.

14. **missing-in-scope: A cumulative decoder work ceiling outside the deferred JBIG2 pass.** Native JPX iterates layers, resolutions, components and precincts without a cumulative work budget (`src/pdf/filters.cpp:8942`). The byte meter measures allocations rather than enforcing execution limits. Memory limits and one CCITT timing assertion do not bound a large or damaged decode. No typed work-exhaustion outcome or regression test exists.

15. **missing-out-of-scope: Cooperative cancellation through an active decode.** None of the public decoder signatures accepts cancellation (`src/pdf/filters.h:100`, `:110`, `:140`, `:153`), and the implementations never return `Cancelled`. Existing integration owner: **D01 T02 §7**, which must carry cancellation into decoder loops and WIC work, beyond checks between interpreter commands.

16. **present: Typed C++ failure boundaries.** JPEG and JPX entry points translate `std::bad_alloc` into `OutOfMemory` and unexpected exceptions into `InternalError` (`src/pdf/filters.cpp:6493`, `:9242`). Malformed-input and budget tests assert named errors. WIC HRESULT failures remain a separate deficiency.

17. **missing-in-scope: Accurate WIC failure classification and supporting tests.** Failures from WIC decoding and pixel copying become `DamagedFormat` without inspecting the HRESULT (`src/pdf/filters.cpp:6271`, `:6339`, `:6467`). Thus `E_OUTOFMEMORY` can be reported as file corruption; JPX can fall through to another decoding attempt after a resource failure (`:7527`). Preserve resource, unsupported-codec and corrupt-data distinctions, retain the HRESULT for support, and add injected-failure tests.

18. **present: Thread-local WIC ownership and concurrent decoding evidence.** Factory storage is thread-local at `src/pdf/filters.cpp:6037`. `PdfFilters.ConcurrentWicDecode` runs eight simultaneous threads with repeated pixel comparisons and distinct live factory identities (`tests/recto-tests/pdf_filters_tests.cpp:2207`). This does not prove application thread routing.

19. **missing-out-of-scope: Delivery into the viewer, OCR, conversion and redaction.** Decoded streams are not yet an implemented document-to-display pipeline. Existing owners include **D01 T02 §4, §8 and §9**, followed by domains **03, 04 and 05**. Integration evidence belongs to **D06 T01 §1, §3, §5 and §6**. Decoder unit tests cannot establish these user workflows.

20. **missing-out-of-scope: Opening images larger than the full-resolution budget.** Current entry points refuse oversized images. The decided reduced-scale decoding path is owned by **D01 T02 §19**, specifically `todo/01-engine/TODO-02-actor-and-render.md:605`. Its large-image regression must demonstrate useful rendering within the existing memory target.

21. **missing-out-of-scope: Partial-document recovery and user/support diagnostics.** Typed decoder messages alone do not establish that readable pages survive, skipped images are disclosed, or users can copy useful details. Existing owners: **D06 T03 §2** for recovery records, **§3** for page notices, **D02 T01 §11** for the error report, and **D02 T02 §25** for per-page failures. Relevant contracts include `todo/06-quality/TODO-03-user-failure-gates.md:98` and `:120`.

22. **missing-in-scope: Deterministic tests of both JPX implementations.** WIC is attempted first (`src/pdf/filters.cpp:7517`), but tests cannot force the native path. A registered JPEG 2000 codec can bypass the implementation that `PdfFilters.JpxTagTreesCharged` intends to verify (`tests/recto-tests/pdf_filters_tests.cpp:4109`). Add explicit path selection or dependency injection and verify both configurations.

**Part 2: excellence**

**Competitor: Adobe Acrobat Pro.** It is the strongest documented comparison baseline here. Adobe documents JPEG, JPEG 2000, CCITT and JBIG2 workflows, embedded JPX colour handling, partial opening after image errors, and support diagnostics. Its image-error recovery can require changing zoom or optimising the file, leaving an opportunity for Recto to improve recovery. [Image settings](https://developer.adobe.com/acrobat-pdfl-sdk/guides/pdfcreation/pdf-create-usingsettings), [JBIG2 scan settings](https://helpx.adobe.com/acrobat/desktop/create-documents/scan-documents-to-pdfs/scanned-pdf-settings.html), [JPX handling](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdflsdk/apireference/Samples/CodeSamples/JPXEncode/sources/JPXEncode.cpp.html), [image-error behaviour](https://helpx.adobe.com/acrobat/kb/insufficient-data-image.html), [diagnostics](https://helpx.adobe.com/acrobat/kb/acrobat-diagnostics.html).

Foxit also documents JPEG 2000/JBIG2 scan workflows; PDFelement and UPDF document scanned-document processing. Their cited documentation supplies no comparable decoder latency or hostile-input measurements, so a speed ranking would be unsupported. [Foxit](https://www.foxit.com/resource-hub/user-guide/foxit-pdf-editor-for-windows/scan-with-custom-settings/), [PDFelement](https://pdf.wondershare.com/pdfelement/PDFelement-v12-user-guide-win.pdf), [UPDF](https://updf.com/tech-spec/).

Targets below are proposed unless already specified by Recto’s plan.

1. **Default fidelity remains below the required baseline.** The fax, JBIG2, JPEG and JPX gaps in Part 1 can reject valid documents or change their pixels. **Target:** every fixture specified by §31 to §34 produces the required pixels without user intervention. **out-of-scope**, retaining those existing owners.

2. **Native JPX colour handling cannot match Acrobat’s documented embedded-colour behaviour.** **Target:** palette-red remains red, CMYK remains four colour channels, and opacity follows channel declarations; lossless reference fixtures match exactly. **in-scope**, Part 1 item 10.

3. **Duplicate buffers unnecessarily reject ordinary camera images and delay the first result.** RGB JPEG decoding reserves raw, RGBA and BGRA storage, totalling 11 bytes per pixel (`src/pdf/filters.cpp:6321`, `:6475`). A 6000 × 4500 image therefore requires 297,000,000 bytes and is refused under 256 MiB, despite one display buffer fitting comfortably. **Target:** decode that image within the unchanged ceiling, allocating only requested representations; verify peak memory and elapsed time. **in-scope**. Reduced-scale handling of genuinely oversized images remains with D01 T02 §19.

4. **Hostile-file responsiveness has no complete enforceable guarantee.** Unbudgeted parameter copies and uncapped decoder work prevent a defensible advantage over mature viewers. **Target:** the 1 MiB parameter-copy reproduction stays within budget or refuses before copying; deterministic work-budget tests stop expensive native JPX processing with a typed resource refusal. **in-scope**. JBIG2’s systematic pass remains with §35.

5. **Cancel cannot interrupt an image decode.** **Target:** propagate cancellation through active decoding, return `Cancelled`, release temporary buffers and publish no cancelled result. Preserve Recto’s decided sub-10 ms cooperative cancellation objective. **out-of-scope**, D01 T02 §7.

6. **Keeping the readable document open is unproven.** Acrobat documents opening a PDF despite damaged images. Recto currently has no delivered equivalent around these decoders. **Target:** one corrupt image in a multi-page fixture leaves every healthy page readable, identifies the affected page and permits retry without reopening the document. **out-of-scope**, D01 T02 and D06 T03 §2, plus D02 T02 §25.

7. **Error wording can misdiagnose resource exhaustion as corruption.** **Target:** injected WIC allocation failures consistently return `OutOfMemory`; unsupported codecs and damaged data remain distinguishable, with the HRESULT available in technical details. **in-scope**, Part 1 item 17.

8. **User diagnostics and recovery polish are not delivered.** **Target:** one nonblocking notice identifies the affected page/image and refusal reason; one Copy details action includes filter, typed error, relevant limit and version, with paths scrubbed and no automatic upload. **out-of-scope**, the diagnostic owners in Part 1 item 21.

9. **Installed-codec-dependent coverage prevents a reliable default experience.** **Target:** execute forced-native and WIC configurations, with lossless pixel equality and documented tolerances for lossy paths; native budget tests must execute native code. **in-scope**, Part 1 item 22.

10. **No measurement demonstrates faster decoding than Acrobat.** The 3-second CCITT assertion (`tests/recto-tests/pdf_filters_tests.cpp:4044`) detects quadratic behaviour, but supplies no competitive latency evidence. **Target:** publish reproducible cold/warm p50 and p95 measurements per codec on valid, large and damaged fixtures; a proposed winning threshold is at least 20% lower p95 with equal fidelity and enforced limits. **in-scope** for decoder measurements.

11. **First-page latency and zero UI-thread decoding remain unverified.** **Target:** prove zero decoding on the UI thread and retain the foundation’s existing frame, sharp-tile and working-set targets. Add an explicit scanned-first-page benchmark; the existing <300 ms requirement names a text PDF. **out-of-scope**, D01 T02 §8/§9 and D06 T01 §7/§8.

**Gap list**

1. **missing-out-of-scope; out-of-scope:** Correct damaged-fax recovery and regressions, D06 T05 §34.
2. **missing-out-of-scope; out-of-scope:** JBIG2 stripes, plane alignment, skip masks and retained contexts, D06 T05 §31.
3. **missing-out-of-scope:** Systematic JBIG2 resource enforcement and three owed regression fixtures, D06 T05 §35.
4. **missing-out-of-scope; out-of-scope:** JPEG `/ColorTransform`, Adobe CMYK and YCCK correctness, D06 T05 §33.
5. **missing-out-of-scope; out-of-scope:** JPX component geometry, coding styles, decomposition levels and ROI, D06 T05 §32.
6. **missing-in-scope; in-scope:** Preserve and interpret embedded JPX colour, palette and channel semantics.
7. **missing-in-scope; in-scope:** Eliminate unbudgeted deep copies during parameter resolution and add regressions.
8. **missing-in-scope; in-scope:** Enforce cumulative decoder work limits with typed exhaustion and deterministic tests.
9. **missing-out-of-scope; out-of-scope:** Carry cooperative cancellation through active decodes, D01 T02 §7.
10. **missing-in-scope; in-scope:** Classify WIC failures accurately, retain HRESULT details and test injected failures.
11. **missing-out-of-scope:** Deliver decoded images into rendering, OCR, conversion and redaction, D01 T02 and domains 03 to 05, with D06 T01 integration suites.
12. **missing-out-of-scope:** Deliver reduced-scale decoding for genuinely oversized images, D01 T02 §19.
13. **missing-out-of-scope; out-of-scope:** Preserve healthy document content and expose recovery notices/details, D06 T03 §2/§3, D02 T01 §11 and D02 T02 §25.
14. **missing-in-scope; in-scope:** Make native and WIC JPX paths explicitly testable and verify both.
15. **in-scope:** Avoid unconditional duplicate pixel buffers that reject otherwise manageable camera images.
16. **in-scope:** Establish reproducible decoder latency evidence against Acrobat.
17. **out-of-scope:** Verify scanned-first-page performance and zero UI-thread decoding, D01 T02 §8/§9 and D06 T01 §7/§8.

COMPLETION: gaps

Disposition of the completion review's gaps (each verified against the code before acting):

- Implemented: gap 7, parameter resolution copied what no filter reads (`resolve_stage_parms` cloned every entry of a `/DecodeParms` dictionary once one defined key was indirect, and `deref_array` cloned every element of an array holding a reference). Only the keys a filter defines are carried now, a non-scalar there as an empty string the readers refuse alike, and array elements resolve into pointers, never a copied array. The image stage also stops carrying the internal `/TargetFormat` key, which a document could set to change the layout the pipeline returns. `PdfFilters.ParameterResolutionCopiesOnlyWhatFiltersRead` (peaks under 1 MiB and 12 MiB; the mutants peak at 8,389,879 and 16,778,582 bytes) and `PdfFilters.DocumentCannotSetTargetFormat` fail with the fix reverted. Commit `906eeb2`.
- Implemented: gap 14, `detail::ScopedJpxPath` pins the native or the WIC JPX decoder for a test, so a registered JPEG 2000 codec cannot bypass a native-path test; `PdfFilters.JpxTagTreesCharged` now pins the native decoder and `PdfFilters.JpxPathPinned` decodes through each path. Commit `906eeb2`.
- Filed: D06 T05 §32 (gap 6: JP2 colour, palette and channel definitions, added as an item to the JPX conformance owner)
- Filed: D06 T05 §36 (gap 8: a cumulative work budget for the native JPX decoder)
- Filed: D06 T05 §37 (gap 10: WIC failures classified by HRESULT)
- Filed: D06 T05 §38 (gap 15: DCT decodes only the requested pixel representation)
- Filed: D06 T05 §39 (gap 16: measured per-codec decode latency)
- Filed: D06 T05 §31 (gaps 2 and 4, existing owner)
- Filed: D06 T05 §33 (gap 4, existing owner)
- Filed: D06 T05 §34 (gap 1, existing owner)
- Filed: D06 T05 §35 (gap 3, existing owner)
- Filed: D01 T02 §7 (gap 9, cancellation through an active decode, existing owner)
- Filed: D01 T02 §19 (gap 12, reduced-scale decoding of oversized images, existing owner)
- Filed: D06 T03 §2 (gap 13, healthy pages kept and recovery disclosed, existing owner with D02 T01 §11 and D02 T02 §25)
- Filed: D06 T01 §7 (gap 17, scanned first-page performance, existing owner with D01 T02 §8 and §9)
- Gap 11 (delivering decoded images to rendering, OCR, conversion and redaction) is the work of `D01 T02` and domains 03 to 05 as planned, with `D06 T01 §1` integration suites.

Gates after the completion-review fixes: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 164 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfError.*:PdfFilters.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 31 tests.`

## Round 5: none

- Writer: claude
- Run: 2026-10-23T07:51:19Z
- Skipped: sol (exited 1)
- Lines: 1
- Digest: 0555d87060386853

No reviewer returned a review. This round is not an approval.

## Round 6: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T08:05:44Z
- Effort: xhigh
- Lines: 30
- Digest: 373985b7057bbda1

## adversarial: needs-attention

New native reproductions below are source-derived, not executed.

1. **[P1] JPX tile overrides can retain approximately 768 MiB outside the ceiling.** `src/pdf/filters.cpp:8056` copies `main_qcd` into every tile override without accounting for its vectors. Reproduce with a native 64×64 grayscale codestream, 1×1 tiles, zero decomposition levels, a main reversible QCD with `Lqcd=65535` and 65,532 `0x40` entries, and a COD override in each tile. A 184,382-byte stream retains at least 805,257,216 bytes of copied quantization vectors before the later entry-count validation rejects it. Header-stage accounting charges only 368,640 bytes. The tag-tree guard never protects this allocation path; neither §32 nor §36 owns this memory gap.

2. **[P1] Parameter resolution still multiplies unused payloads.** `src/pdf/filters.cpp:9425`, `:9429`, `:9441` eagerly resolves and retains every parameter-array element, including repeated references to the same dictionary. Extend `ParameterResolutionCopiesOnlyWhatFiltersRead` with eight nested ASCIIHex stages and eight `/DecodeParms` references to its 8 MiB `/Unused` dictionary. The pipeline retains eight independent payloads, at least 64 MiB under a 1 MiB ceiling, although no stage reads them. Removing the additional array clone does not eliminate this amplification.

## consistency: approve

No additional findings.

## integration: needs-attention

3. **[P1] The global allocator replacement disables ASan allocation-mismatch checks throughout the test binary.** `tests/recto-tests/heap_meter.cpp:45`, `:69`, `:123` routes allocation through `malloc`, deletion through `free`, and discards the sized-delete argument. Under ASan, reproduce with `void* p = ::operator new(64); ::operator delete(p, size_t{1});`: the replacement erases the allocation type and deletion size needed to diagnose the mismatch. This affects every linked test even when no heap meter is active. Microsoft explicitly documents this limitation of [custom new/delete overrides](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues?view=msvc-170#overriding-operator-new-and-delete).

## source-defect: needs-attention

4. **[P2] JPX resolves `/Decode` before deciding that it must ignore it.** `src/pdf/filters.cpp:9656`, `:9666`, `:9678` resolves the array and its elements before checking `/ImageMask`. Reproduce with the existing valid JPX seed, `/ImageMask false`, `/Decode [99 0 R 1]`, and a resolver returning `DamagedFormat` for object 99. Decoding fails before reaching either codec, although removing `/Decode` succeeds. The non-mask branch must ignore that entry before resolving it, as required by the [JPX image-dictionary rules](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.5_v6.pdf#page=87).

## design: advisory

Not applicable: no user surface.

## record: advisory

The historical logs contain the recorded 164-test and 31-test passes and mutation peaks. Independently, the existing x64 Release binary passed 23 selected tests and reported ASan enabled. No rebuild or new native reproduction was run, so those passes do not disprove the findings above. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 6

- Finding 1 (P1): correct, and wider than the override copy. A QCD or QCC body is now refused where it is parsed when it lists more subbands than 32 decomposition levels allow (1 + 3 x 32, T.800 A.6.1), so no table longer than 97 entries is ever stored or copied; before, the later entry-count check refused it only after every tile override and every component of every tile had copied it. Tile overrides no longer copy the main QCD at all (their QCD is written whole by the marker that sets `has_qcd`), each override charges a whole per-component coding set to the ceiling before it is built, the image, tile spans, tile streams and coding parameters share one remainder, and overrides are found through a per-tile index instead of a scan of all overrides per marker. `PdfFilters.JpxCodingParametersBounded`: the 65,532-entry QCD is refused with a peak under 1 MiB (3,635,641 bytes before), and 4,096 tiles each with a COD override are refused at 1 MiB (accepted before) and decode at 16 MiB.
- Finding 2 (P1): correct. Every `/DecodeParms` and `/Filter` array element resolved into the pipeline's retained list, so eight references to one 8 MiB dictionary held 64 MiB. A referenced element is now resolved into a value the stage owns, reduced to the keys its filter defines (plus `/JBIG2Globals` for JBIG2, which that decoder reads), and the resolver's result dropped; a referenced `/Filter` element keeps only its name. `/Predictor` and the other defined keys resolve the same way. `ParameterResolutionCopiesOnlyWhatFiltersRead` adds the eight-stage case: peak under 12 MiB (67,112,696 bytes before).
- Finding 3 (P1): correct. `heap_meter.cpp` replaces the global operator new and delete only outside AddressSanitizer builds, so ASan keeps its own allocation-type and sized-delete checks (Microsoft Learn, AddressSanitizer known issues: overriding operator new and delete). `ScopedHeapMeter::available()` says whether a build measures; the memory assertions run in the Debug and Release suites and are skipped under ASan, and `Support.HeapMeterOnlyOutsideAsan` checks both sides.
- Finding 4 (P2): correct. The image stage now decides first whether `/Decode` applies (a JPX image that is not an `/ImageMask` ignores it, ISO 32000-2 Table 87) and resolves `/Decode` only when it does; `/ImageMask` itself resolves into a value the stage drops. `PdfFilters.JpxIgnoredDecodeIsNotResolved` decodes with `/Decode [99 0 R 1]` and with `/Decode 99 0 R` against a resolver that fails object 99; both failed before.
- Proof that the tests can fail: with `src/pdf/filters.cpp` at `17bb0f4`, x64 Debug -> `ParameterResolutionCopiesOnlyWhatFiltersRead`, `JpxCodingParametersBounded` and `JpxIgnoredDecodeIsNotResolved` fail as quoted above.
