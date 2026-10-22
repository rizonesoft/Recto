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

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T08:00:21Z
- Effort: medium
- Lines: 28
- Digest: 64c42bd4fe6037eb

### adversarial: needs-attention

1. **P3: Indirect references exhaust temporary storage within the permitted eight-filter limit.** `src/pdf/filters.cpp:9143` reserves 18 entries, but resolving two indirect arrays, their 16 indirect elements, and two array copies requires 20. At capacity, lines 9146 and 9168 silently retain unresolved values. Reproduce with `/Filter 10 0 R` and `/DecodeParms 11 0 R`, each resolving to eight references: filter references resolve to `/ASCIIHexDecode`, parameter references to empty dictionaries. Supply eight nested ASCIIHex encodings of `A`. The pipeline returns `DamagedFormat` before decoding, although the equivalent direct arrays work. This is a source-traced reproduction.

### consistency: needs-attention

2. **P3: The trailer repair contradicts the frozen statelessness contract.** `src/pdf/filters.cpp:955` calls `record_diagnostic`, which mutates the process-wide ring under a global lock at `src/pdf/error.cpp:936`. The section still promises pure functions sharing zero mutable state. Reproduce with the existing `PdfFilters.FlateTrailerDamageIsRecorded` test: decoding `78 9C 73 04 00` increments global diagnostic state. The test passed during this audit. Keeping recovered data and recording damage is the accepted behavior, but its implementation and the unchanged Freeze check now disagree.

### integration: needs-attention

3. **P2: Indirect values inside `/DecodeParms` still reject valid streams.** `src/pdf/filters.cpp:680` reads `/Predictor` directly with `as_int()`; `/Colors`, `/BitsPerComponent`, `/Columns`, and `/EarlyChange` have the same limitation. The new resolver handles the enclosing dictionary and array elements but never its parameter values.

   Reproduce through `Document::decode_stream` with `/Filter /FlateDecode /DecodeParms << /Predictor 10 0 R >>`, object `10 0 obj 1 endobj`, and bytes `78 9C 73 04 00 00 42 00 42`. Expected output is `A`; source execution instead returns `DamagedFormat` for a non-integer predictor. The resolver supplied at `src/pdf/document.cpp:1764` never receives that reference. This is a source-traced reproduction.

### source-defect: needs-attention

Finding 3 violates the equivalence of direct and indirect parameter values. These integer parameters have no direct-only restriction. See [Adobe PDF Reference §3.2.9 and Table 3.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=62).

### design: advisory

Not applicable; no user surface.

### record: advisory

Independently ran the existing Release executable with `--gtest_filter=PdfFilters.*:Support.AsanActiveWhenEnabled`: all 14 filter tests passed, plus the support test reporting `ASan=enabled`. This verifies the executable’s checkpoint result, including the round 1 regressions. It does not independently establish a fresh build from `6814970`, ARM64 compilation, or warning counts. No files were modified and no custom C++ reproducers were compiled.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P3): correct, and my mistake: the reserved vector capped resolution at 18 entries and silently left references unresolved past it. Resolved values now live in a `std::deque` (stable addresses as it grows) with no silent cap. Arrays longer than the filter chain are left for the existing length checks to refuse, and a `/DecodeParms` dictionary with more than 16 references (more than any filter defines parameters) is refused explicitly. `PdfFilters.IndirectValuesAndFullChainsResolve` decodes eight nested ASCIIHex stages where `/Filter` and `/DecodeParms` are each an indirect array of eight references.
- Finding 3 (P2): correct. Values inside a `/DecodeParms` dictionary (`/Predictor`, `/Colors`, `/BitsPerComponent`, `/Columns`, `/EarlyChange` and the rest) now resolve one level through the resolver, in a direct dictionary and in each array element. The same test decodes a stream whose `/Predictor` is `10 0 R`. It fails on the round 1 code.
- Finding 2 (P3): the file is `frozen: true`, so the Freeze check is not rewritten. Its purpose is thread safety, which holds: filters already wrote to the D16 diagnostic ring through `make_pdf_error` (out of memory, internal errors), a lock-protected sink that no decode reads, and the trailer diagnostic is the same kind of write. A dated note in `D01 T01 §7` records that reading of "zero mutable state" as decode state.

Gates after round 2: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 114 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfDocument.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 23 tests.`

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T08:10:55Z
- Effort: medium
- Lines: 24
- Digest: 79325dc24d2c42e3

### adversarial: needs-attention

Finding 1 below leaves a narrow gap in the promised RFC 1951 support. Reproduction is source-traced, not executed against a custom C++ harness.

### consistency: approve

### integration: approve

### source-defect: needs-attention

1. **P3: Flate rejects permitted distance-table sizes.** `src/pdf/filters.cpp:493` rejects `hdist > 30`. RFC 1951 permits 1 to 32 distance-table entries; unused entries have zero code length. Prohibiting reserved distance symbols from occurring does not require rejecting their unused table entries. [RFC 1951 §3.2.7](https://www.rfc-editor.org/rfc/rfc1951.html#section-3.2.7)

   Reproduce with a final dynamic DEFLATE block declaring 257 literal/length entries and 31 distance entries: give only literal symbol 256 a one-bit code, distance symbol 0 a one-bit code, and all other entries zero lengths; encode only EOB. Expected output is empty. `decode_flate` instead returns `DamagedFormat` at the header check before reading those tables. This is an uncommon interoperability edge case.

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

Independently ran the existing Release executable with `--gtest_filter=PdfFilters.*:Support.AsanActiveWhenEnabled`: all 15 filter tests passed, and the support test reported `ASan=enabled`. This verifies the existing executable’s checkpoint result, including round 2 regressions. It does not independently establish the recorded fresh-build warning counts, ARM64 compilation, or that the executable was built from `1c768d3`. No files were modified.

HIGHEST: P3
OVERALL: needs-attention

## Fixes after round 3

- Finding 1 (P3), kept as a deliberate refusal: RFC 1951 section 3.2.7 does allow HDIST up to 32 when the extra entries have length 0 (**Corrected before round 4:** this note first also said HLIT up to 288; the RFC caps HLIT at 286). zlib's `inflate`, the reference implementation every producer is tested against, refuses those headers ("too many length or distance symbols") unless built with `PKZIP_BUG_WORKAROUND`, so no stream other readers open is lost by refusing the same; accepting them would make Recto read streams zlib rejects. The reason is now a comment at the check in `src/pdf/filters.cpp`. No behaviour change.

Gates after round 3: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 114 tests.` for x64 Debug and Release.

## Fixes before round 4 (found by the reviewer trial)

The owner's reviewer trial (`.conclave/reviewer-trial-2026-10-22.md`) ran the round 4 prompt at `50af32b` through GPT-6 Astra at medium and GPT-6.1 Sol at high and xhigh, outside this record. Its three findings were verified against the source and fixed before the official round 4:

- Incomplete single-code Huffman trees (P1 as Sol xhigh rated it on the distance table): `build_huffman` accepted a lone code of any length; RFC 1951 section 3.2.7 and zlib's `inftrees.c` accept an incomplete code only as one code of length 1. It now requires `count[1] == 1`. `PdfFilters.LoneHuffmanCodeLongerThanOneBitIsRefused` refuses both trial vectors (a lone two-bit literal/length code and a lone two-bit distance code, each also refused by zlib: `invalid literal/lengths set`, `invalid distances set`).
- Object streams decoded without a resolver (P2): `XrefTable` and the encrypted-document path in `Document` called `decode_stream` with no resolver, so an indirect `/DecodeParms` on an object stream failed. Both now pass one that resolves with the container already visited, so a parameter pointing back into the same stream fails as a cycle. `PdfXref.ObjStmIndirectDecodeParmsResolve` resolves an object from a stream whose `/DecodeParms` is `2 0 R` and refuses one pointing at `1 0 R` inside the stream. The encrypted-document path is covered by the build and the shared resolver pattern only: no corpus file is an encrypted document with object streams.
- My round 3 comment said RFC 1951 lets HLIT reach 288; it caps HLIT at 286, and only HDIST reaches 32. The comment is corrected, and so is round 3's note above, which repeated it.

Both new tests fail on the round 3 code.

Gates before round 4: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 116 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 36 tests.`
