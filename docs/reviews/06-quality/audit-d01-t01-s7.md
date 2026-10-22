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

## Round 4: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T08:45:28Z
- Effort: xhigh
- Lines: 26
- Digest: 88c82e2e8a548473

### adversarial: needs-attention

1. **P3: Unused parameter entries can prevent valid decoding.** `src/pdf/filters.cpp:9185` resolves every `/DecodeParms` value, including unknown keys. Reproduce using the builder in `tests/recto-tests/pdf_xref_tests.cpp:95`: change object 2 from `<< >>` to `<< /VendorData 1 0 R >>`, retaining `/DecodeParms 2 0 R`. ASCIIHex has no parameters and should decode object 1 to integer `42`. Instead, resolving the irrelevant reference enters the same object stream and returns `DamagedFormat`. This is a source-traced reproduction.

### consistency: approve

### integration: needs-attention

2. **P2: The encrypted-object resolver still omits the filter resolver.** `src/pdf/crypto.cpp:2189` calls `decode_stream(*obj_stm)` without a resolver. The repairs in `XrefTable` and `Document` do not cover the public `SecurityHandler::resolve_and_decrypt_object` path.

   Reproduce by authenticating a `SecurityHandler`, then resolving a compressed child from an encrypted object stream with `/Filter /FlateDecode /DecodeParms 10 0 R`, where object 10 is `<< /Predictor 1 >>`. After decryption, the pipeline leaves the parameter reference unresolved and returns `DamagedFormat`; the equivalent direct dictionary works. This is source-traced, not an executed custom regression.

### source-defect: needs-attention

Finding 1 makes an unknown dictionary entry affect decoding, contrary to the extension behavior described in [Adobe PDF Reference, Appendix H.2](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=1026).

### design: advisory

Not applicable; no user surface.

### record: advisory

Independently reran the existing Release executable: all 16 `PdfFilters.*` tests passed, and `Support.AsanActiveWhenEnabled` reported `ASan=enabled`. The object-stream resolver regression also passed. These runs verify the existing executable’s results, not the recorded fresh-build warning counts or ARM64 compilation. Reviewed sources match `9b15890`; no files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 4

Round 4 is the first on the new pin (GPT-6.1 Sol at xhigh).

- Finding 1 (P3): correct. Resolving every `/DecodeParms` value let a key no filter reads (a reference back into the same object stream) fail decoding, contrary to ISO 32000-1 Appendix H.2's rule that unknown keys are ignored. Only the parameters the standard filters define now resolve (`Predictor`, `Colors`, `BitsPerComponent`, `Columns`, `EarlyChange`, the CCITT parameters, `ColorTransform`, and `/Crypt`'s `Name` and `Type`); `/JBIG2Globals` keeps its own resolution in the decoder. Because that set is closed, resolution stays bounded, and round 2's 16-reference refusal is gone. `PdfXref.ObjStmIndirectDecodeParmsResolve` adds an object stream whose parameter dictionary is `<< /VendorData 1 0 R >>`, which decodes to 42.
- Finding 2 (P2): correct. `SecurityHandler::resolve_and_decrypt_with_visited` also decoded object streams without a resolver. It now passes one that resolves and decrypts through the same path with the container already visited. `obj_stm` points into a node of `decrypted_cache_`, which a nested resolution may grow without moving it, and the cache iterator is reassigned by `emplace` afterwards. `PdfCrypto.EncryptedObjStmIndirectDecodeParmsResolve` resolves a compressed integer from an RC4-encrypted object stream whose `/DecodeParms` is `4 0 R`. This also covers the shape of the `Document` path noted before round 4.

Both tests fail on the round 4 code.

Gates after round 4: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 117 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 45 tests.`

## Round 5: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T09:02:09Z
- Effort: xhigh
- Lines: 28
- Digest: 6a1914a7ea7f4b25

### adversarial: needs-attention

1. **P3: Parameters belonging to other filters still affect decoding.** `src/pdf/filters.cpp:9152` uses one allowlist for every filter, and `src/pdf/filters.cpp:9184` resolves matching keys before identifying the applicable filter. Reproduce using the builder at `tests/recto-tests/pdf_xref_tests.cpp:97`: call `build("2 0 R", "<< /Columns 1 0 R >>")`, then resolve object 1. ASCIIHex does not use `/Columns`, but resolving it re-enters the object stream and returns `DamagedFormat` instead of integer `42`. Round 4’s ignored-key repair remains incomplete. This reproduction is source-traced.

### consistency: approve

### integration: needs-attention

2. **P2: Indirect filters are resolved too late for stream decryption.** `src/pdf/crypto.cpp:1932` recognizes `/Crypt` only through direct filter names or direct array elements. It selects and applies the default cipher before the pipeline resolves indirect filters.

   Reproduce in an authenticated document whose default stream cipher is AESV2: use `/Filter 10 0 R`, object 10 containing `[/Crypt /FlateDecode]`, `/DecodeParms [<< /Name /Identity >> null]`, and stream bytes `78 9C 73 04 00 00 42 00 42`. These are valid unencrypted Flate bytes decoding to `A`. The unresolved filter causes default AES decryption, which rejects their nine-byte length at `src/pdf/crypto.cpp:1735`. The equivalent direct filter array selects Identity and works. This reproduction is source-traced.

### source-defect: needs-attention

1. **P3, `src/pdf/filters.cpp:9184`:** Finding 1 violates the ignored-entry behavior described in [Adobe PDF Reference, Appendix H.2](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=1026). The same `/Columns` reproduction demonstrates the violation.

2. **P2, `src/pdf/crypto.cpp:1932`:** Finding 2 breaks direct/indirect equivalence for the filter array. The reference permits indirect objects unless explicitly restricted, and Crypt selects the stream’s decryption method. See [sections 3.2.9 and 3.3.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=61).

### design: advisory

Not applicable; no user surface.

### record: advisory

Independently reran the existing Release executable: all 16 `PdfFilters.*` tests and 29 `PdfXref.*`, `PdfDocument.*`, and `PdfCrypto.*` tests passed. `Support.AsanActiveWhenEnabled` reported `ASan=enabled`. Graph validation and plan checks passed. These runs verify the existing executable’s results, not its correspondence to `7f6e3c9`, fresh-build warnings, or ARM64 compilation. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 5

- Finding 1 (P3): correct, and round 4's fix had the wrong shape: one allowlist for every filter, applied before the stage's filter was known. Resolution of a parameter dictionary's values now happens per stage, in the pipeline loop, for the keys that stage's filter defines (`stage_parameter_keys`: Flate, LZW, CCITTFax, DCT and Crypt per ISO 32000-1 Tables 8, 11, 12 and 14; every other filter defines none). A key another filter would read, or none reads, is never resolved. `PdfXref.ObjStmIndirectDecodeParmsResolve` adds `<< /Columns 1 0 R >>` under ASCIIHex (which has no parameters) beside the unknown-key case; it fails on the round 4 code.
- Finding 2 (P2): correct. Stream decryption chose the `/Crypt` cipher from a direct `/Filter` only, so an indirect filter chain starting with `/Crypt /Identity` was run through the document's default cipher. `SecurityHandler::decrypt_object_values` takes an optional resolver. With it, an indirect `/Filter` and its elements (and, when the chain starts with `/Crypt`, `/DecodeParms`, its first element and `/Name`) are written into the stream dictionary resolved one level, so the existing selection and strip logic sees what a direct chain would. `SecurityHandler`'s own resolution and `Document`'s two decryption sites pass a resolver with the object being decrypted already visited, so a self-reference fails as a cycle. `PdfCrypto.IndirectCryptFilterChoosesTheStreamCipher` decrypts a stream whose `/Filter` is `10 0 R` (`[/Crypt /FlateDecode]`, `/Name /Identity`) under an RC4 handler: the bytes stay as stored, `/Crypt` is stripped, and the stream decodes to `A`. With the inlining disabled it fails.
- The contract note in `D01 T01 §7` now says how parameters resolve and which callers supply a resolver.

Gates after round 5: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 118 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 46 tests.`; `python scripts/todo-graph.py validate` -> valid.

## Round 6: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T09:23:02Z
- Effort: xhigh
- Lines: 26
- Digest: e07a3186059d858b

### adversarial: needs-attention

1. **P1: Corrupt-header recovery bypasses checksum validation.** `src/pdf/filters.cpp:1012` inflates after skipping two bytes without checking the remaining Adler-32 trailer. Reproduce with `decode_flate` on `78 00 73 02 00 00 42 00 42`: it returns `B`, whose checksum is `00430043`, despite the present trailer specifying `00420042`. Changing only the second byte to `9C` correctly returns `DamagedFormat`. This contradicts the section’s explicit promise to refuse present, incorrect checksums. This reproduction is source-traced; Python raw inflation independently confirmed the payload and checksum mismatch.

2. **P2: Corrupt-header recovery excludes corruption of the first header byte.** `src/pdf/filters.cpp:1010` requires an intact compression-method field before trying the header-skipping fallback. Reproduce with `79 9C 73 04 00 00 42 00 42`, obtained by changing `78` to `79` in a valid stream containing `A`. Raw decoding fails and the fallback is skipped, returning `DamagedFormat` despite an intact body and checksum. The contract at `todo/01-engine/TODO-01-bridge-runtime.md:263` promises recovery for a corrupted two-byte header without this restriction. This reproduction is source-traced.

### consistency: approve

### integration: needs-attention

1. **P1, finding 1:** `src/pdf/document.cpp:1786` passes ordinary document streams through the affected decoder. Put finding 1’s bytes in a `/FlateDecode` stream and call `Document::decode_stream`: the document receives unchecked bytes as successful output.

### source-defect: needs-attention

1. **P1, finding 1:** `src/pdf/filters.cpp:1020` accepts the recovered zlib body without validating its available checksum. Header recovery does not establish data integrity. [RFC 1950 §2.3](https://www.rfc-editor.org/rfc/rfc1950#section-2.3) requires an error indication for an incorrect Adler-32 value.

### design: advisory

Not applicable; no user surface.

### record: advisory

The existing Release executable passed all 16 `PdfFilters.*` tests plus the ASan-support test, reporting `ASan=enabled`. The broader selection passed 47 tests; graph validation and plan checks passed. These runs do not establish executable correspondence to `f233e09`, fresh-build warnings, or ARM64 compilation. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 6

- Finding 1 (P1): correct. The header-recovery path inflated the body after a damaged header with no trailer check, so a present, wrong Adler-32 passed there although the valid-header path refused it. The trailer check is now one routine (`check_adler_trailer`) used by every path that inflates a zlib body from byte 2: wrong is refused, missing or short keeps the data and records `DamagedStream`. `78 00 73 02 00 00 42 00 42` (the body inflates to `B`, the trailer names `A`) is refused.
- Finding 2 (P2): correct. Recovery skipped the header only when byte 0 still looked like a deflate CMF byte, so damage to byte 0 was unrecoverable although the contract promises recovery from a corrupted two-byte header. When raw deflate from byte 0 fails, the body after the header is now tried whatever the header bytes are; the trailer check above means a skipped header yields only data its checksum vouches for (or, without a trailer, recorded damage), and a recovered header is itself recorded as `DamagedStream`. `79 9C ...` and `78 00 ...` around a valid `A` body now decode, each recording damage.
- `PdfFilters.FlateTrailerDamageIsRecorded` covers all three cases and fails on the round 5 code.

Gates after round 6: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 118 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 46 tests.`

## Round 7: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T09:38:50Z
- Effort: xhigh
- Lines: 26
- Digest: 73b5911d31dab5d3

### adversarial: needs-attention

1. **P1: Successful raw inflation still bypasses header recovery and checksum validation.** `src/pdf/filters.cpp:1002` accepts the raw interpretation immediately. Reproduce with `decode_flate` on `7B 9C 73 04 00 00 42 00 42`, a valid `A` stream with its first header byte damaged. It returns `E3 6C C4` successfully instead of recovering `A`, despite the intact body and matching Adler-32. Changing the final trailer byte to `43` also succeeds. The checksum routine is never reached. This is a source-traced reproduction; Python zlib independently confirmed both inflation interpretations.

### consistency: approve

### integration: needs-attention

2. **P2: JBIG2 globals lose the caller’s filter resolver.** `src/pdf/filters.cpp:9433` explicitly passes `nullptr` into the globals stream’s lossless pipeline.

   Reproduce using the globals fixture at `tests/recto-tests/pdf_filters_tests.cpp:1678`: ASCIIHex-encode `globals_bytes`, append `>`, and return that globals stream with `/Filter 43 0 R`. Extend the existing resolver so object 43 returns `/ASCIIHexDecode`. The page should still decode to `3C 00 3C 00`, but decoding returns `DamagedFormat` because the nested pipeline cannot resolve object 43. Using the direct filter name works. This reproduction is source-traced.

### source-defect: needs-attention

2. **P2, finding 2, `src/pdf/filters.cpp:9433`:** The globals path breaks direct/indirect equivalence for ordinary stream filter entries. The PDF reference permits indirect values and explicitly illustrates an ASCIIHex-filtered globals stream. The reproduction above exercises that combination. [Indirect objects](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=62), [JBIG2 globals example](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=80).

### design: advisory

Not applicable; no user surface.

### record: advisory

Independently reran the existing Release executable: 16 filter tests and 30 XRef/document/crypto tests passed; the support test reported `ASan=enabled`. Graph validation and plan checks passed. These results do not establish executable correspondence to `d28a162`, fresh-build warning counts, or ARM64 compilation. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 7

- Finding 1 (P1): correct. With a damaged header, raw deflate from byte 0 was tried first, and bytes that happen to parse as raw deflate (`7B 9C 73 04 00 ...`) came back as garbage without the trailer ever being consulted. The evidence now decides: if the body after the header inflates with a 4-byte trailer behind it, the stream is zlib-shaped and that trailer judges it (a match recovers `A` with the damaged header recorded; a mismatch is refused). Raw deflate from byte 0 applies only when there is no such trailer, and the body after the header (recorded as damaged, its missing trailer recorded too) only after raw deflate fails.
- Finding 2 (P2): correct. `decode_jbig2_globals_stream` ran the globals stream's own pipeline with no resolver, so an indirect `/Filter` on it failed. It now takes and passes on the caller's resolver at both call sites. `PdfFilters.Jbig2Decode` adds a globals stream whose `/Filter` is `43 0 R` (ASCIIHex) and decodes the same page.
- `PdfFilters.FlateTrailerDamageIsRecorded` adds the ambiguous `7B 9C` stream (recovered) and its wrong-trailer twin (refused). Both tests fail on the round 6 code.

Gates after round 7: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 118 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 46 tests.`

## Round 8: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T09:55:20Z
- Effort: xhigh
- Lines: 24
- Digest: ecaa6879059e8e90

### adversarial: needs-attention

1. **P2: Header recovery rejects valid raw deflate.** `src/pdf/filters.cpp:1014` treats any four bytes following the alternate inflation as a zlib trailer, preventing raw fallback. Reproduce with `01 FF 03 00 FC` followed by 1,023 `41` bytes. This valid stored block decodes to 1,023 `A` bytes. From byte 2, however, `03 00` decodes to empty output; `FC 41 41 41` becomes its supposed checksum, causing `DamagedFormat`. Python zlib confirmed both interpretations; the Recto failure is source-traced. This breaks the section’s explicit raw-deflate promise.

### consistency: approve

### integration: needs-attention

2. **P3: Unused predictor parameters can prevent object-stream decoding.** `src/pdf/filters.cpp:9251` resolves `/Columns`, `/Colors` and `/BitsPerComponent` even when `/Predictor` defaults to `1`, although `parse_predictor_params` ignores them at line 696. Reproduce with object stream 5 containing plaintext `1 0 42`, `/N 1 /First 4 /Filter /FlateDecode /DecodeParms << /Columns 1 0 R >>`. Its compressed payload is `78 9C 33 54 30 50 30 31 02 00 03 86 01 08`. Resolving contained object 1 should return integer `42`; instead, resolving the unused `/Columns` re-enters the container and returns `DamagedFormat`. This reproduction is source-traced.

### source-defect: needs-attention

2. **P3, same finding, `src/pdf/filters.cpp:9251`:** Predictor geometry parameters apply only when `Predictor` exceeds `1`. Eager resolution makes an unused parameter affect decoding. The object-stream reproduction above demonstrates the discrepancy. [Adobe PDF Reference, Table 3.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=72)

### design: advisory

Not applicable; no user surface.

### record: advisory

The existing Release executable passed 16 filter tests and 30 XRef/document/crypto tests; the support test reported `ASan=enabled`. Graph validation and plan checks passed. These runs do not independently establish executable correspondence to `55ce750`, fresh-build warnings or ARM64 compilation. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 8

- Finding 1 (P2): correct. Round 7 treated any four bytes behind an alternate inflation as a trailer, which turned a valid raw stored block into a refusal. Rounds 6 to 8 each patched one case of the same ambiguity: some bytes are valid both as raw deflate from byte 0 and as a zlib body after a damaged header. Rather than patch a fourth case, the rule is now stated once, in the code and in the `D01 T01 §7` note. (1) A body after the header whose 4 trailing bytes are its Adler-32 is verified zlib (an accidental match is a 2^-32 event) and wins. (2) Otherwise a raw-deflate reading from byte 0 wins: it is a valid reading, which an unverified zlib reading does not outrank. (3) Otherwise the body after the header is kept with recorded damage when it has no trailer, and refused when the trailer is wrong. This reverses one round 7 expectation: `7B 9C ...` with a wrong trailer now yields its valid raw reading (`E3 6C C4`) instead of a refusal, because a wrong checksum is no evidence for the zlib reading. `PdfFilters.FlateTrailerDamageIsRecorded` covers the verified recovery, that raw reading, and the stored block (1,023 `A` bytes).
- Finding 2 (P3): correct. `/Colors`, `/BitsPerComponent` and `/Columns` shape a Flate or LZW predictor only, so they now resolve only when `/Predictor` (itself resolved first when indirect) is above 1. `PdfXref.ObjStmIndirectDecodeParmsResolve` adds a Flate object stream with `<< /Columns 1 0 R >>` and the default predictor, which decodes to 42.
- Both tests fail on the round 7 code.

Gates after round 8: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 118 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 46 tests.`; `python scripts/todo-graph.py validate` -> valid.

## Round 9: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T10:12:01Z
- Effort: xhigh
- Lines: 24
- Digest: 0c655fcf71d554a1

### adversarial: needs-attention

1. **P2: An unverified header-skipping attempt can prevent valid raw decoding.** `src/pdf/filters.cpp:1013` returns resource errors before trying byte 0, contradicting the stated selection rule. Reproduce with `decode_flate(bytes, nullptr, 1024)`, where bytes are `01 05 00 FA FF 05 00 41 41 41`. The valid raw stream produces five bytes, `05 00 41 41 41`. The byte-2 interpretation instead declares a 65,530-byte stored block, hits the ceiling at line 424 and returns `DecompressionLimitExceeded`; raw fallback is never reached. This failure is source-traced. Python zlib independently confirmed the valid raw output.

### consistency: approve

### integration: needs-attention

1. **P2, same defect:** `src/pdf/document.cpp:1786` forwards the caller’s ceiling into the affected decoder. Put finding 1’s bytes in a `/FlateDecode` stream and call `Document::decode_stream(stream, 1024)`: the valid five-byte result is refused. The failure therefore reaches the document API.

### source-defect: needs-attention

2. **P3: The documented HDIST restriction still falls short of promised RFC 1951 support.** `src/pdf/filters.cpp:498` rejects 31 or 32 distance-table entries. Reproduce with a dynamic block declaring 257 literal/length entries and 31 distance entries, using only a one-bit EOB literal code and one-bit distance code 0, with all other lengths zero, then emitting EOB. Expected output is empty; the header check returns `DamagedFormat`. The zlib compatibility rationale documents this restriction but does not make it RFC compliant. [RFC 1951 §3.2.7](https://www.rfc-editor.org/rfc/rfc1951.html#section-3.2.7)

### design: advisory

Not applicable; no user surface.

### record: advisory

The existing Release executable passed 16 filter tests and 30 XRef/document/crypto tests; the support test reported `ASan=enabled`. Graph validation and plan checks passed. These reruns do not independently verify executable correspondence to `cee1dcd`, fresh-build warnings or ARM64 compilation. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 9

- Finding 1 (P2): correct, an implementation gap in round 8's rule. A resource ceiling hit by the speculative attempt after the header returned before the raw reading had its turn, so `01 05 00 FA FF 05 00 41 41 41` at a 1,024-byte ceiling was refused although its raw reading is five valid bytes. The speculative attempt's resource error is now held and reported only if the raw reading fails too. `PdfFilters.FlateTrailerDamageIsRecorded` decodes that stream at that ceiling; it fails on the round 8 code.
- Finding 2 (P3), unchanged and recorded: the HDIST refusal (above 30) was decided in round 3 to match zlib's reference inflater, documented at the check, and is not reopened.

Gates after round 9: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 118 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 46 tests.`

## Round 10: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T10:26:46Z
- Effort: xhigh
- Lines: 24
- Digest: 30d4414ec644d60d

### adversarial: needs-attention

1. **P3: Indirect null external-stream entries cause false refusals.** `src/pdf/filters.cpp:9483` tests `/F` before resolving it. Source-traced reproduction: call `decode_stream` with bytes `41>`, dictionary `<< /Filter /ASCIIHexDecode /F 10 0 R >>`, and a resolver returning `PdfNull` for object 10. Expected output is `A`; execution returns `UnsupportedFeature` before consulting the resolver. Indirect null `/FFilter` and `/FDecodeParms` similarly produce false orphan errors at lines 9492 and 9499. This is an uncommon encoding edge case.

### consistency: approve

### integration: needs-attention

1. **P3, finding 1:** `src/pdf/document.cpp:1786` forwards streams into this validation unchanged. The same reproduction through `Document::decode_stream` therefore refuses valid inline content.

### source-defect: needs-attention

1. **P3, finding 1:** `src/pdf/filters.cpp:9483` distinguishes direct null from a reference resolving to null. Null dictionary entries are equivalent to absent entries, and direct and indirect values have equivalent semantics. [Adobe PDF Reference §§3.2.8–3.2.9](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf#page=61)

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

Independently reran the existing Release executable: 16 filter tests passed, the support test reported `ASan=enabled`, and 30 XRef/document/crypto tests passed. Graph validation and plan checks passed. These runs do not independently establish executable correspondence to `42e069c`, fresh-build warnings, or ARM64 compilation. Finding 1 is source-traced, not an executed custom regression. The worktree remains clean.

HIGHEST: P3
OVERALL: needs-attention

## Fixes after round 10

Round 10's findings are all P3, so under the operator's rule (from round 9 on, an all-P3 round ends the loop) the loop stops here, `stopped-low-severity`. Its one finding is fixed rather than filed, since the fix is small:

- Finding 1 (P3): correct. `/F`, `/FFilter` and `/FDecodeParms` were tested before resolution, so a reference resolving to null (the same as an absent entry, ISO 32000-1 section 7.3.9) was refused as an external stream or an orphan entry. `stream_entry_present` resolves the entry through the caller's resolver first, in `decode_stream` and in the JBIG2 globals path; without a resolver a reference still counts as present. `PdfFilters.IndirectNullExternalEntriesAreAbsent` decodes `41>` with each of the three entries pointing at null, and fails on the round 9 code.

Gates after round 10: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 119 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 47 tests.`

## Audit outcome

The stamp on `D01 T01 §7` is confirmed: every defect found existed at that stamp (none is a regression) and each is fixed at its owning layer with a test that fails on the code before it, in `6814970` through the round 10 fix. They fall into four groups:

- Indirect `/Filter`, `/DecodeParms`, parameter values and external-stream entries, now resolved through the caller's resolver per stage and in every object-stream, decryption and JBIG2 globals path.
- Zlib integrity: the trailer is checked on every body; a damaged header follows one stated rule; lone multi-bit Huffman codes are refused.
- A contract error: ASCII85's invalid range is `v..y`.
- The `/Crypt` cipher chosen from an indirect filter.

The HDIST refusal above 30 is a recorded decision (zlib parity). Dependants are not suspect: every change widens what decodes correctly or refuses what zlib refuses, the corpus decodes unchanged, and all 119 tests pass. The loop stopped at round 10 with only P3 findings (`stopped-low-severity`), and that finding is fixed.

Test checkpoint: `python scripts/todo-graph.py resolve "D01 T01 §7"` -> exit 3 (still verified). `D01 T01 §7`'s own checkpoint: `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfFilters.*:PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 47 tests.`
