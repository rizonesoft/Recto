# Audit - D01 T01 §7: Lossless stream filters (`Flate`, `LZW`, predictors, `ASCII85`, `ASCIIHex`, `RunLength`)

- **Audit section:** `D06 T05 §20` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §7` (`docs/reviews/d01-t01-s7.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `503be5d`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 12 tests.` (12 tests from 1 suite). The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T07:49:47Z
- Effort: medium
- Lines: 24
- Digest: b112f008d648a94f

### adversarial: needs-attention

1. **P1: Truncated zlib streams succeed without checksum validation.** `src/pdf/filters.cpp:951` checks Adler-32 only when at least four trailer bytes remain. Reproduce with `decode_flate` on hex `78 9C 73 04 00`, or that input followed by one to three arbitrary bytes. Static execution returns `A` successfully, although the zlib trailer is missing or incomplete. The permitted missing/corrupt-header fallback does not justify ignoring a truncated trailer after accepting a valid header. [RFC 1950 §2.3](https://www.rfc-editor.org/rfc/rfc1950.html#section-2.3) requires checksum validation.

### consistency: approve

### integration: needs-attention

2. **P2: Legal indirect filter parameters are rejected even when a resolver is supplied.** `src/pdf/filters.cpp:9151` requires `/DecodeParms` to be a direct dictionary or array; `src/pdf/filters.cpp:9185` similarly requires direct filter-array names. `Document::decode_stream` supplies a resolver at `src/pdf/document.cpp:1764`, but the pipeline never uses it for these entries. Reproduce with an ordinary stream containing `/Filter /FlateDecode /DecodeParms 10 0 R`, where object 10 is `<< /Predictor 1 >>`, and valid compressed bytes. Static execution returns `DamagedFormat` instead of decoding. Ordinary dictionary values may be indirect references. [Adobe PDF Reference §3.2.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=61)

### source-defect: needs-attention

3. **P3: The ASCII85 acceptance criterion incorrectly prohibits `u`.** `todo/01-engine/TODO-01-bridge-runtime.md:284` requires rejection of `u..y`; ASCII85 permits `u`. Reproduce the contradiction with `!!!!u~>`, a valid tuple accepted by the implementation. The contract should specify `v..y`; changing the decoder to satisfy the current wording would break valid input. [Adobe PDF Reference §3.3.2](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=67)

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

The checkpoint rerun was independently checked using the existing Release executable: all 12 `PdfFilters.*` tests passed, and `Support.AsanActiveWhenEnabled` reported `ASan=enabled`. This verifies the executable’s test result, not the recorded fresh-build or ARM64 compilation claims. No rebuild or custom C++ reproducer was run because files must remain unchanged. Findings 1 and 2 are source-traced reproductions, not executed regression tests.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P1, as rated): partly agreed. A present but wrong Adler-32 was already refused; a missing or cut-short trailer was passed over silently. Refusing those streams would break files other readers open (producers do truncate zlib trailers, and pdf.js and MuPDF inflate them), so the data is kept, following the engine's convention for recoverable damage (the stream `/Length` fallback records `DamagedStream` and continues): `decode_flate` now records a `DamagedStream` diagnostic whenever fewer than four trailer bytes follow a completely inflated zlib body. `PdfFilters.FlateTrailerDamageIsRecorded` decodes `78 9C 73 04 00` with 0 to 3 trailer bytes (one diagnostic each), with the full trailer (none), and refuses a wrong checksum. It fails on the old code.
- Finding 2 (P2): correct. `execute_filter_pipeline` now resolves `/Filter`, `/DecodeParms` and their array elements one level through the caller's resolver (ISO 32000-1 section 7.3.10 allows any dictionary value to be indirect), into storage that outlives the pipeline; without a resolver a reference is refused as before, and a resolver failure is returned. `PdfFilters.IndirectFilterAndDecodeParmsResolve` decodes with an indirect `/DecodeParms`, an indirect `/Filter`, and arrays of references, refuses each without a resolver, and returns the resolver's error for a missing object. It fails on the old code.
- Finding 3 (P3): correct. `u` is the highest base-85 digit; the invalid range is `v..y`. The decoder was right and the contract wrong: `D01 T01 §7` carries a dated correction, and `PdfFilters.AsciiFilters` now rejects each of `v`, `w`, `x`, `y` and decodes `!!!!u~>` to `00 00 00 54`.

Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 113 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfDocument.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 22 tests.`; `python scripts/todo-graph.py validate` -> `valid: 681 sections`.
