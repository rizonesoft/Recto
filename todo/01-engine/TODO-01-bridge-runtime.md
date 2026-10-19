---
schema_version: 1
id: bridge-runtime
domain: 01-engine
status: active
title: "TODO-01 -- Native PDF Parser, Filters, Crypto and Document Model"
frozen: true
track: M1
---

# TODO-01 -- Native PDF Parser, Filters, Crypto and Document Model

> **Goal:** Build the parser, stream-filter, decryption and document-model half of `recto-pdf` (`src/pdf`, C++20): `PdfResult<T>` and the D16 diagnostic ring buffer, the `PdfValue` object graph, a binary-safe PDF 1.7 and 2.0 lexer and bounded recursive-descent indirect object parser, classic `xref` tables, compressed `/Type /XRef` and `/Type /ObjStm` streams (`PDF 1.5+`), incremental updates and damaged-file linear object recovery, the full stream filter pipeline (`FlateDecode`, `PngPredictor`, `LZWDecode`, `ASCII85Decode`, `ASCIIHexDecode`, `RunLengthDecode`, `CCITTFaxDecode`, `JBIG2Decode`, `DCTDecode` and `JPXDecode` via WIC), Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6` via Windows CNG `bcrypt.dll`), and the `pdf::Document` page-tree, metadata, outline and link model.

> **Current state:** Measured 2026-10-18. `src/pdf/recto-pdf.vcxproj` compiles the scaffold `recto::pdf::abi_version()` entry point (`src/pdf/pdf.cpp`, `src/pdf/recto_pdf.h`). `D00 T03` (`TODO-03-test-backbone.md`) is `done`, providing `tests/recto-tests/test_support.h`, the 39-file corpus in `tests/corpus/`, and the `-EnableASAN` build and CI job. No PDF parsing code exists yet.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D3, D5, D9, D12, D14, D16), 3.1 to 3.3 (`src/pdf`, `PdfResult<T>`, `PdfError`, `ErrorCode`), 4.1 (what `recto-pdf` implements), 4.2 (thread ownership rules), 9 (security and untrusted input).
- ISO 32000-1:2008 (PDF 1.7) and ISO 32000-2:2020 (PDF 2.0) specifications for syntax, XRef streams, filter parameters, and encryption algorithms.
- -> XREF: D01 T02 §1 -- the `DisplayList` builder (`§1`), DirectWrite font loader (`§2`), content-stream interpreter (`§3`, `§4`), incremental writer (`§5`) and `DocumentActor` (`§8`) in `D01 T02` consume `pdf::Document`, decoded content streams, and page resource dictionaries produced here.
- -> XREF: D02 T01 §11 -- the viewer's `"Couldn't open this file"` dialog (`D02 T01 §11`) maps `ErrorCode` values and copies `diagnostics_tail()` entries from `§1` here.
- -> XREF: D02 T03 §5 -- `OutlinePanel` (`D02 T03 §5`) and `DocumentInfoPanel` (`D02 T03 §9`) consume `DocumentDescription` from `§10` here.
- -> XREF: D04 T02 §2 -- `PdfWriter` in `D04 T02 §2` shares the `PdfValue` object model, filter encoders and CNG cryptographic primitives built here.
- -> XREF: D05 T01 §7 -- the `AES-256` (`V=5, R=6`) encryption writer in `D05 T01 §7` extends `src/pdf/crypto.cpp` built in `§9` here.
- -> XREF: D06 T02 §1 -- the hostile-input audit in `D06 T02 §1` verifies the 64-level recursion cap, 256 MB stream decompression ceiling, and absence of JavaScript execution implemented here.

## Outcome

- `recto::pdf::Document::open` parses valid and incrementally updated PDF 1.0 to 2.0 documents, reconstructs damaged XRef tables via the fallback object scanner, and returns typed `PdfError` values on corrupt or truncated inputs without throwing exceptions or leaking memory.
- Every standard PDF stream filter (`FlateDecode`, predictors, `LZWDecode`, `ASCII85Decode`, `ASCIIHexDecode`, `RunLengthDecode`, `CCITTFaxDecode` Group 3/4, `JBIG2Decode`, `DCTDecode` JPEG, `JPXDecode` JPEG2000) decodes within enforced 256 MB byte ceilings.
- Encrypted PDFs (`RC4` 40-128 bit, `AES-128` `R=4`, `AES-256` `R=5..6`) report `ErrorCode::NeedsPassword`, authenticate user and owner passwords via Windows CNG `bcrypt.dll`, and transparently decrypt indirect strings and streams.
- Document description (page count, page boxes and rotations, metadata, security summary, font inventory, outline tree, page link annotations) is exposed in plain C++20 structs.

**Adjacency:** all=not-applicable (low-level C++20 PDF format parser and object model in `src/pdf`; all user surfaces live in domains 02 to 05 and 07)

**Adjacency rationale:** `recto-pdf` has no UI, settings store or notification channel of its own. Its error diagnostics feed the D16 ring buffer and the viewer's `"Couldn't open this file -> Copy error details"` dialog owned by `D02 T01 §11`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `PdfResult<T>`, `PdfError`, `ErrorCode`, D16 diagnostic ring and ETW `TraceLogging` | D00 T01 §3, D00 T03 §2 | [x] |
| 2 | §2 | `PdfValue` variant object model, dictionary/array helpers and deep cloning | §1 | [x] |
| 3 | §3 | Binary-safe PDF 1.7 and 2.0 lexer (`src/pdf/lexer.cpp`) | §2 | [x] |
| 4 | §4 | Bounded recursive-descent indirect object parser (`src/pdf/parser.cpp`) | §3 | [x] |
| 5 | §5 | Classic `xref` tables, trailer merging and `/Prev` incremental update chains | §4, D00 T03 §4 | [x] |
| 6 | §7 | Lossless stream filters (`Flate`, `LZW`, predictors, `ASCII85`, `ASCIIHex`, `RunLength`) | §4, D00 T03 §4 | [x] |
| 7 | §6 | PDF 1.5+ `/Type /XRef` streams, `/Type /ObjStm` streams and damaged-file recovery | §5, §7 | [x] |
| 8 | §8 | Bi-level and image stream filters (`CCITTFaxDecode`, `JBIG2Decode`, WIC `DCT` and `JPX`) | §7 | [x] |
| 9 | §9 | Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6` via CNG) | §6, §7 | [ ] |
| 10 | §10 | `pdf::Document` page-tree resolver, metadata, `/Outlines`, font list and `/Annots` links | §6, §8, §9 | [ ] |

## 1. `PdfResult<T>`, `PdfError`, `ErrorCode`, D16 diagnostic ring and ETW `TraceLogging`

Define the foundational error-handling and diagnostic primitives in `src/pdf/error.h` and `src/pdf/error.cpp`:
- `ErrorCode` (`Ok`, `FileNotFound`, `AccessDenied`, `NotAPdf`, `DamagedFormat`, `NeedsPassword`, `WrongPassword`, `UnsupportedEncryption`, `UnsupportedFeature`, `DecompressionLimitExceeded`, `RecursionLimitExceeded`, `OutOfMemory`, `IoError`, `Cancelled`).
- `PdfError` (`code`, `message`, `byte_offset`, `object_num`, `generation_num`) and `PdfResult<T>` (`std::expected`-shaped value-or-`PdfError` container with `PdfResult<void>` specialization).
- The D16 thread-safe bounded diagnostic ring buffer (`recto::pdf::record_diagnostic(...)`, `recto::pdf::diagnostics_tail(max_entries)`, `recto::pdf::clear_diagnostics()`), storing the last 256 timestamped engine warnings/errors in memory without touching disk or network, paired with Windows Event Tracing (`<TraceLoggingProvider.h>`, `TRACELOGGING_DEFINE_PROVIDER`, `TraceLoggingWrite`) emitting structured ETW events on `record_diagnostic` and scoped span macros (`RECTO_ETW_SPAN`) for open, render, and job phases (D16).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `PdfResult<T>` never throws C++ exceptions across module boundaries, `diagnostics_tail()` synchronizes ring-buffer access with a lightweight lock over a fixed-capacity 256-slot array, `TraceLoggingWrite` emits only error codes, offsets, and durations, and zero document byte payloads are retained in the diagnostic ring or ETW events.

> **Started:** 2026-10-18

**Build order:**

1. Author `src/pdf/error.h` and `src/pdf/error.cpp` implementing `ErrorCode`, `PdfError`, `PdfResult<T>`, `PdfResult<void>`, `RECTO_PDF_TRY`, the 256-entry thread-safe diagnostic ring buffer, and the D16 `TraceLoggingWrite` ETW provider (`Recto.Diagnostics`) and `RECTO_ETW_SPAN` helpers. Done when: `src/pdf/recto-pdf.vcxproj` compiles `error.cpp` cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_error_tests.cpp` (`PdfError.ResultValueAndError`, `PdfError.DiagnosticRingWraparound`, `PdfError.ConcurrentDiagnosticRecording`, `PdfError.EtwTraceLoggingProviderAndSpans`). Done when: all 4 tests pass under ASan.

- [x] Define `ErrorCode` covering all open, parse, filter, encryption, limit, I/O and cancellation conditions in `src/pdf/error.h`. Done when: every `ErrorCode` maps to a human-readable string via `error_code_name(ErrorCode)`. Cheaper substitute: returning `bool` or raw `int` status codes that lose the failure reason.
- [x] Implement `PdfError` carrying `code`, `message`, optional file `byte_offset`, and indirect object `(object_num, generation_num)` context. Done when: `PdfError::format()` renders a concise diagnostic line with offset and object ID when present.
- [x] Implement `PdfResult<T>` and `PdfResult<void>` with `.has_value()`, `.value()`, `.error()`, and `value_or(...)` accessors. Done when: `PdfError.ResultValueAndError` tests both move-only (`std::unique_ptr`) and copyable payloads.
- [x] Implement the D16 256-slot thread-safe diagnostic ring buffer (`record_diagnostic`, `diagnostics_tail`, `clear_diagnostics`) and `TraceLoggingWrite` ETW provider (`TRACELOGGING_DEFINE_PROVIDER(g_recto_etw_provider, "Recto.Diagnostics", ...)` + `RECTO_ETW_SPAN`) in `src/pdf/error.cpp`. Done when: recording 1,000 diagnostics retains the newest 256 entries in exact chronological order without heap growth and emits ETW trace events without error.
- [x] Verify thread safety of the diagnostic ring buffer and ETW span emission with 8 threads concurrently calling `record_diagnostic`, `RECTO_ETW_SPAN`, and `diagnostics_tail`. Done when: `PdfError.ConcurrentDiagnosticRecording` and `PdfError.EtwTraceLoggingProviderAndSpans` complete 10,000 writes/reads under ASan with zero data races.
- [x] Automatically append a ring-buffer entry and emit a `TraceLoggingWrite` diagnostic event whenever `make_pdf_error(...)` constructs a `DamagedFormat`, `DecompressionLimitExceeded`, or `RecursionLimitExceeded` error. Done when: `diagnostics_tail(10)` reflects the error immediately after construction.
- [x] Commit: `pdf: add PdfResult, PdfError, D16 diagnostic ring buffer and ETW TraceLogging`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfError.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-18 | §1 | a0b2e1721fb9fb1fe3662162c86301e84d74154d
> **Review:** `docs/reviews/d01-t01-s1.json`

## 2. `PdfValue` variant object model, dictionary/array helpers and deep cloning

Implement the in-memory PDF object representation in `src/pdf/object.h` and `src/pdf/object.cpp` (ISO 32000-1 section 7.3):
- Scalar and composite types: `PdfNull`, `bool`, `int64_t`, `double`, `PdfString` (raw binary/literal/UTF-16BE/PDFDocEncoding byte vector), `PdfName` (interned or normalized `/Name` string without leading slash), `IndirectRef` (`obj_num`, `gen_num`), `PdfArray` (`std::vector<PdfValue>`), `PdfDict` (ordered or hash-indexed map of `PdfName -> PdfValue`), and `PdfStream` (`PdfDict dict` + raw/decoded byte buffer or file slice).
- Type-safe accessors (`as_int()`, `as_real()`, `as_number()`, `as_bool()`, `as_name()`, `as_string()`, `as_array()`, `as_dict()`, `as_stream()`, `as_ref()`), rectangle/matrix extractors (`as_rect()`, `as_matrix()`), and cycle-safe `deep_clone()`.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `PdfValue` instances own all their storage by value (no hidden shared mutable pointers between `Document` instances) so distinct `pdf::Document` objects on separate `DocumentActor` threads share zero mutable state.

> **Started:** 2026-10-18

**Build order:**

1. Author `src/pdf/object.h` and `src/pdf/object.cpp` implementing `PdfValue`, `PdfArray`, `PdfDict`, `PdfStream`, `IndirectRef`, `PdfRect`, `PdfMatrix`, typed dictionary lookups, and PDF string UTF-8 conversion (`pdf_string_to_utf8` handling UTF-16BE BOM `FE FF`, UTF-8 BOM `EF BB BF`, and `PDFDocEncoding`). Done when: `object.cpp` compiles across `x64` and `ARM64` with 0 warnings.
2. Add `tests/recto-tests/pdf_object_tests.cpp` testing all 10 `PdfValue` alternatives, `PdfDict` lookups, `PdfRect` normalization (`[llx lly urx ury]` with swapped coordinates normalized), `pdf_string_to_utf8`, and `deep_clone()`. Done when: `PdfObject.*` passes under ASan with zero leaks.

- [x] Define `PdfValue` covering all 10 ISO 32000-1 section 7.3 object kinds (`null`, `bool`, `int64_t`, `double`, `PdfName`, `PdfString`, `PdfArray`, `PdfDict`, `PdfStream`, `IndirectRef`). Done when: `sizeof(PdfValue)` is compact and move-construction is `noexcept`.
- [x] Implement numeric promotion in `PdfValue::as_number()` so integer PDF tokens (`72`) and real PDF tokens (`72.0`) both return `double` seamlessly for coordinate and matrix math. Done when: `PdfObject.NumericPromotion` verifies `int64_t` and `double` interoperability.
- [x] Implement `PdfDict` with deterministic key lookup (`get(key)`, `get_int(key)`, `get_number(key)`, `get_name(key)`, `get_string(key)`, `get_array(key)`, `get_dict(key)`, `get_ref(key)`, `contains(key)`). Done when: missing keys or wrong-typed keys return `nullptr` / `std::nullopt` without throwing.
- [x] Implement `PdfRect::from_array(const PdfArray&)` normalizing `[x1 y1 x2 y2]` so `x_min <= x_max`, `y_min <= y_max`, `width() >= 0`, and `height() >= 0`. Done when: inverted rectangles (`[0 792 612 0]`) normalize to `[0 0 612 792]`.
- [x] Implement `pdf_string_to_utf8(std::span<const uint8_t>)` and `utf8_to_pdf_string(std::string_view)` supporting UTF-16BE (`\xFE\xFF` with surrogate pairs), UTF-8 (`\xEF\xBB\xBF` PDF 2.0), and full ISO 32000-1 Table D.2 `PDFDocEncoding`. Done when: `PdfObject.StringEncoding` round-trips ASCII, smart quotes/euro in `PDFDocEncoding`, and CJK/emoji in UTF-16BE.
- [x] Implement `PdfValue::deep_clone()` bounded by depth 64 so cloning nested dictionaries/arrays for `GraftMap` or `PdfWriter` produces an independent copy. Done when: mutating a clone leaves the original `PdfValue` unchanged.
- [x] Commit: `pdf: implement PdfValue object model, dictionary helpers and PDF string codecs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfObject.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-18 | §2 | 153e4cec9491ddf546724e2e43749e5d18c8f7a4
> **Review:** `docs/reviews/d01-t01-s2.json`

## 3. Binary-safe PDF 1.7 and 2.0 lexer (`src/pdf/lexer.cpp`)

Implement the binary-safe PDF lexical analyzer in `src/pdf/lexer.h` and `src/pdf/lexer.cpp` over `std::span<const uint8_t>` (ISO 32000-1 section 7.2):
- Whitespace (`0x00` NUL, `0x09` HT, `0x0A` LF, `0x0C` FF, `0x0D` CR, `0x20` SP) and `%` comments (preserving `%PDF-x.y` and `%%EOF` markers when requested).
- Keywords (`true`, `false`, `null`, `obj`, `endobj`, `stream`, `endstream`, `xref`, `trailer`, `startxref`, `R`, and content-stream operators).
- Numbers: signed/unsigned 64-bit integers with overflow clamping/detection and floating-point real numbers (`.5`, `-3.14`, `+123.`).
- Literal strings `(...)` with arbitrary nested balanced parentheses, backslash escapes (`\n`, `\r`, `\t`, `\b`, `\f`, `\(`, `\)`, `\\`, `\ddd` 1-to-3 digit octal escapes, and `\<CR>` / `\<LF>` / `\<CRLF>` line continuations) and `CR` / `CRLF` -> `LF` end-of-line normalization.
- Hexadecimal strings `<...>` ignoring internal whitespace and padding an odd trailing hex digit with `0` (`<901FA>` -> `0x90, 0x1F, 0xA0`).
- Name objects `/Name` decoding `#XX` two-digit hex escapes (`/#20` -> space) and rejecting `/#00` null escapes per PDF 1.7/2.0 rules.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `Lexer` operates purely over a read-only `std::span<const uint8_t>` cursor with bounds checks before every byte read, never reading past `span.size()` even on truncated tokens.

> **Started:** 2026-10-18

**Build order:**

1. Implement `src/pdf/lexer.h` and `src/pdf/lexer.cpp` (`Token`, `TokenKind`, `Lexer::next_token()`, `Lexer::peek_token()`, `Lexer::seek()`, `Lexer::consume_stream_eol()`). Done when: `lexer.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_lexer_tests.cpp` testing every token type, escape sequence, odd-length hex string, `#XX` name escape, `stream` EOL rule (`\r\n` or `\n`, never bare `\r` followed by data consumed as 2 bytes), and byte-by-byte truncation safety. Done when: `PdfLexer.*` passes under ASan.

- [x] Tokenize PDF whitespace (including embedded `0x00` NUL bytes), delimiters (`(`, `)`, `<`, `>`, `[`, `]`, `{`, `}`, `/`, `%`), and `%...` comments up to `CR`, `LF`, or `CRLF`. Done when: `PdfLexer.WhitespaceAndComments` handles NUL-separated tokens and comments with arbitrary binary bytes. Cheaper substitute: using `std::istringstream` or `strchr` which truncates at the first `0x00` byte in binary PDFs.
- [x] Parse signed/unsigned integers (`int64_t`) and real numbers (`double`), handling leading `.` (`-.05`), trailing `.` (`42.`), and integer overflow gracefully without undefined behavior. Done when: `PdfLexer.Numbers` covers boundary 64-bit integers and malformed numbers (`--5`, `1.2.3`).
- [x] Parse literal strings `(...)` with nested balanced parentheses `(a(b(c)d)e)`, escape sequences (`\n`, `\r`, `\t`, `\b`, `\f`, `\(`, `\)`, `\\`), 1-3 digit octal `\ddd` escapes, and backslash-newline continuation. Done when: `PdfLexer.LiteralStrings` matches all ISO 32000-1 section 7.3.4.2 examples byte-for-byte.
- [x] Parse hexadecimal strings `<...>` ignoring embedded whitespace and padding odd-digit strings with a trailing `0` nibble (`<4142A>` -> `0x41, 0x42, 0xA0`). Done when: `PdfLexer.HexStrings` verifies even, odd, whitespace-padded, and empty `<>` hex strings, and returns `ErrorCode::DamagedFormat` on non-hex characters.
- [x] Parse `/Name` tokens with `#XX` two-digit hex escape decoding (`/A#42` -> `"AB"`, `/Lime#20Green` -> `"Lime Green"`). Done when: `PdfLexer.Names` verifies `#XX` decoding and rejects truncated `#X` escapes.
- [x] Implement `Lexer::consume_stream_eol()` enforcing the PDF `stream` keyword EOL rule (skipping optional horizontal spaces, then consuming `\r\n` or `\n` before stream byte 0). Done when: a stream starting with `\n\n` preserves the second `\n` as byte 0 of the stream payload.
- [x] Run a byte-by-byte truncation sweep in `PdfLexer.TruncationSweep` cutting a complex token buffer at every offset `0..N` under ASan. Done when: zero out-of-bounds reads occur at any cut point.
- [x] Commit: `pdf: implement binary-safe PDF 1.7/2.0 lexer`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfLexer.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-18 | §3 | e6f27c75cdb972ab32d912de8cc34ac8dae30e14
> **Review:** `docs/reviews/d01-t01-s3.json`

## 4. Bounded recursive-descent indirect object parser (`src/pdf/parser.cpp`)

Implement the recursive-descent PDF object parser in `src/pdf/parser.h` and `src/pdf/parser.cpp`:
- Parse arrays `[...]`, dictionaries `<<...>>`, indirect references `N G R` (via 3-token lookahead disambiguating `12 0 R` from two integer elements `[12 0]`), indirect object wrappers `N G obj ... endobj`, and `stream ... endstream` slices (using direct integer `/Length` or resolving an indirect `/Length N G R` callback when provided, with fallback scan for `endstream` on damaged `/Length`).
- Enforce a strict maximum nesting depth of **64 levels** across nested arrays and dictionaries, returning `ErrorCode::RecursionLimitExceeded` (`DamagedFormat`) before stack exhaustion can occur.
- Enforce a per-container element count ceiling (`1,000,000` array entries or dictionary pairs) against amplification attacks.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `Parser` holds all state on its local call frame, enforces the 64-level recursion cap on every `[` and `<<` entry, and never mutates shared static state.

> **Started:** 2026-10-18

**Build order:**

1. Implement `src/pdf/parser.h` and `src/pdf/parser.cpp` (`parse_direct_object`, `parse_indirect_object`, `parse_dictionary`, `parse_array`, `parse_stream_slice` with indirect `/Length` resolver callback and 64-level recursion guard). Done when: `parser.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_parser_tests.cpp` testing nested dictionaries/arrays, `N G R` lookahead inside arrays and dicts, stream `/Length` handling (exact, indirect, and damaged fallback), 65-deep recursion rejection (`tests/corpus/broken/deep-nested-array-70.pdf`), and byte-by-byte truncation safety. Done when: `PdfParser.*` passes under ASan.

- [x] Implement 3-token lookahead in `Parser` to distinguish `<int> <int> R` (`IndirectRef`) from standalone integer tokens inside arrays (`[10 0 R 20 30]`) and dictionary values (`/Length 15 0 R`). Done when: `PdfParser.IndirectRefsAndIntegers` parses mixed integer and reference arrays accurately.
- [x] Parse `PdfArray` (`[...]`) and `PdfDict` (`<<...>>`), skipping null-valued dictionary entries per ISO 32000-1 section 7.3.7 (`<< /Key null >>` treated as absent). Done when: `PdfParser.ArraysAndDicts` verifies nested structures and `null` value elision.
- [x] Enforce the 64-level recursion limit across nested arrays and dictionaries (`[[[...]]]`, `<< /A << /B ... >> >>`), returning `ErrorCode::RecursionLimitExceeded`. Done when: `PdfParser.RecursionLimit` accepts 64-deep nesting and rejects 65-deep nesting and `tests/corpus/broken/deep-nested-array-70.pdf`. Cheaper substitute: unbounded recursion that crashes with `STATUS_STACK_OVERFLOW` on crafted inputs.
- [x] Parse `N G obj ... endobj` definitions and validate that object number `N > 0` and generation `0 <= G <= 65535`. Done when: negative or overflowing object numbers return `ErrorCode::DamagedFormat`.
- [x] Parse `stream ... endstream` payloads using direct `/Length`, an optional indirect `/Length` resolver callback, and a fallback scan for `endstream` when `/Length` is out of bounds or missing `endstream` at the declared offset. Done when: streams with both accurate and wrong `/Length` values recover their payload bytes in `PdfParser.Streams`.
- [x] Run a byte-by-byte truncation test across sample indirect objects, nested dictionaries, and streams under ASan. Done when: every truncated slice returns `ErrorCode::DamagedFormat` with zero memory errors.
- [x] Commit: `pdf: add bounded recursive-descent PDF object and stream parser`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfParser.*` passes under ASan with 0 warnings.

> **Verified:** 2026-10-18 | §4 | 92005cad5d10e594fed6e96a600e367b0a50b129
> **Review:** `docs/reviews/d01-t01-s4.json`

## 5. Classic `xref` tables, trailer merging and `/Prev` incremental update chains

Implement classic cross-reference table and trailer parsing in `src/pdf/xref.h` and `src/pdf/xref.cpp` (ISO 32000-1 section 7.5.4 and 7.5.6):
- Locate `%PDF-x.y` header (allowing leading junk bytes up to the first 1,024 bytes with offset adjustment) and scan backward from the last 1,024 bytes for `startxref <offset>` and `%%EOF`.
- Parse classic `xref` tables with one or multiple subsections (`<first_obj> <count>` followed by 20-byte `oooooooooo ggggg n/f` entries).
- Follow `/Prev` byte offsets in `trailer << ... >>` dictionaries across incremental revisions (capped at 256 revisions with a visited-offset set to block `/Prev` cycles), keeping the newest revision's entry for each object number so incremental saves override older generations.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `XrefTable` belongs exclusively to one `pdf::Document` instance, caps `/Prev` chains at 256 revisions, and detects cyclic `/Prev` offsets (`A -> B -> A`) immediately.

> **Started:** 2026-10-18

**Build order:**

1. Author `src/pdf/xref.h` and the classic `xref` + `/Prev` incremental chain parser in `src/pdf/xref.cpp`. Done when: `xref.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_xref_tests.cpp` testing `render/simple-1p.pdf`, `render/incremental-2rev.pdf`, multi-subsection `xref` tables, leading-header offset adjustment, and `broken/cyclic-xref-prev.pdf`. Done when: `PdfXref.Classic*` and `PdfXref.Incremental*` pass under ASan.

- [x] Scan the first 1,024 bytes for `%PDF-1.x` / `%PDF-2.0` and compute the header base offset so files with a leading byte preamble adjust all `xref` offsets automatically. Done when: prepending 32 header junk bytes to `simple-1p.pdf` still resolves every object in `PdfXref.HeaderOffsetAdjustment`.
- [x] Scan the trailing 1,024 bytes for the last `startxref` keyword and parse the initial cross-reference byte offset. Done when: files with trailing whitespace or multiple incremental `%%EOF` markers select the final `startxref`.
- [x] Parse classic `xref` subsections (`first_id count` and `offset gen n/f` rows), tolerating both `\r\n` and ` \n` 20-byte line endings and single-space variations produced by non-conformant writers. Done when: multi-subsection `xref` tables populate `XrefEntry` (`Uncompressed`, `Free`) accurately.
- [x] Parse `trailer << ... >>` dictionaries and merge `/Root`, `/Info`, `/Encrypt`, `/ID`, and `/Size` from the newest trailer while following `/Prev` offsets for older revisions. Done when: `render/incremental-2rev.pdf` resolves the updated page object from revision 2 and untouched objects from revision 1.
- [x] Enforce a 256-revision ceiling and a `std::unordered_set<uint64_t>` visited-offset guard on `/Prev` chains (plus hybrid `/XRefStm` pointers). Done when: `tests/corpus/broken/cyclic-xref-prev.pdf` fails immediately with `ErrorCode::DamagedFormat` instead of looping forever. Cheaper substitute: following `/Prev` recursively without cycle detection.
- [x] Maintain a per-resolution visited object set (`std::vector<IndirectRef>`) when resolving indirect references (`A -> B -> C`) capped at depth 32 so cyclic indirect object chains (`1 0 obj 2 0 R endobj`, `2 0 obj 1 0 R endobj`) return `ErrorCode::DamagedFormat`. Done when: `PdfXref.CyclicIndirectRef` passes under ASan.
- [x] Commit: `pdf: implement classic xref tables, incremental /Prev chains and cycle guards`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfXref.Classic*:PdfXref.Incremental*:PdfXref.Cyclic*` passes under ASan.

> **Verified:** 2026-10-18 | §5 | ff4810a6f584beef9962d33c29f5ccb9e373f185
> **Review:** `docs/reviews/d01-t01-s5.json`

## 6. PDF 1.5+ `/Type /XRef` streams, `/Type /ObjStm` streams and damaged-file recovery

Complete `src/pdf/xref.cpp` with PDF 1.5+ cross-reference streams, compressed object streams, and the linear damaged-file recovery scanner (ISO 32000-1 section 7.5.7 and 7.5.8):
- Parse `/Type /XRef` stream objects: validate `/W [w1 w2 w3]` field byte widths (each `0..8` bytes, default type `1` when `w1 == 0`) and `/Index [first_0 count_0 ...]` subsection pairs (default `[0 /Size]`), and unpack type `0` (free), type `1` (uncompressed byte offset + generation), and type `2` (compressed inside `/Type /ObjStm` object number + index).
- Parse `/Type /ObjStm` compressed object streams: read `/N` (object count) and `/First` (byte offset of first object), parse the `/N` pairs of `<obj_num> <relative_offset>` in the header, and extract the requested inner object by index (rejecting nested `/ObjStm` inside `/ObjStm` per spec).
- Implement `recover_damaged_xref`: when `startxref` is missing or points to corrupt data, linearly scan the file buffer for `<N> <G> obj` headers and `trailer << ... >>` dictionaries (or `/Type /Catalog` objects when no trailer survives) to reconstruct the full object table.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `/Type /ObjStm` caching is local to the owning `pdf::Document` instance and that nested `/ObjStm -> /ObjStm` chains are refused.

> **Started:** 2026-10-18

**Build order:**

1. Implement `/Type /XRef` binary entry unpacking (`/W`, `/Index`), `/Type /ObjStm` header and object extraction (decoding compressed stream payloads through `§7`'s `decode_stream`), and `recover_damaged_xref` linear scanner in `src/pdf/xref.cpp`. Done when: `xref.cpp` compiles cleanly on `x64` and `ARM64` and `render/compressed-xref-2p.pdf` resolves all compressed objects.
2. Extend `tests/recto-tests/pdf_xref_tests.cpp` testing `/Type /XRef` unpacking, `/Type /ObjStm` extraction, `tests/corpus/broken/corrupt-xref-recoverable.pdf` recovery, and unrecoverable `broken/` files (`empty-0b.pdf`, `non-pdf-header.pdf`, `truncated-header.pdf`). Done when: all `PdfXref.*` tests pass under ASan.

- [x] Unpack `/Type /XRef` binary records for any valid `/W [w1 w2 w3]` combination (`0 <= wi <= 8`, `w1 + w2 + w3 > 0`) and `/Index` subsection array. Done when: `PdfXref.XrefStreamUnpack` verifies type 0, type 1, type 2, and `w1 = 0` (implicit type 1) records. Cheaper substitute: supporting only classic ASCII `xref` tables, which fails on modern PDFs from Word, Chrome and Acrobat.
- [x] Validate `/Type /XRef` stream length against `sum(count_i) * (w1 + w2 + w3)` and reject malformed `/Index` arrays (odd element count or negative IDs) with `ErrorCode::DamagedFormat`. Done when: truncated `/XRef` streams fail cleanly under ASan.
- [x] Parse `/Type /ObjStm` streams (`/N`, `/First`), extract inner objects by index `0..N-1`, and cache decoded object streams per `pdf::Document` so sibling objects in the same `/ObjStm` do not re-decompress the stream. Done when: `PdfXref.ObjectStream` resolves multiple compressed objects from one `/ObjStm`.
- [x] Refuse nested `/ObjStm` (a type-2 XRef entry pointing to an object that is itself type-2) and refuse indirect `stream` objects inside an `/ObjStm` per ISO 32000-1 section 7.5.7. Done when: a crafted nested `/ObjStm` returns `ErrorCode::DamagedFormat`.
- [x] Implement `recover_damaged_xref` scanning the file for `N G obj` definitions and locating `/Root` from surviving `trailer` dictionaries or by identifying the indirect object with `/Type /Catalog`. Done when: `tests/corpus/broken/corrupt-xref-recoverable.pdf` recovers all objects and opens cleanly in `PdfXref.DamagedRecovery`.
- [x] Verify that `broken/empty-0b.pdf`, `broken/non-pdf-header.pdf`, and `broken/truncated-header.pdf` return `ErrorCode::NotAPdf` or `ErrorCode::DamagedFormat` with diagnostic ring entries recorded. Done when: `PdfXref.RejectsBrokenFiles` passes under ASan.
- [x] Commit: `pdf: add PDF 1.5+ XRef streams, ObjStm parser and damaged-file recovery scanner`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfXref.*` passes under ASan across classic, incremental, compressed XRef, ObjStm, and damaged-xref fixtures.

> **Verified:** 2026-10-19 | §6 | 46a5312ac5f294852704c1d2dfc5e4040bf85bbd
> **Review:** `docs/reviews/d01-t01-s6.json`

## 7. Lossless stream filters (`Flate`, `LZW`, predictors, `ASCII85`, `ASCIIHex`, `RunLength`)

Implement the lossless stream filter pipeline in `src/pdf/filters.h` and `src/pdf/filters.cpp` (ISO 32000-1 section 7.4):
- Chained filter execution: accept a single `/Filter` name or an array of `/Filter` names with matching `/DecodeParms` (or `/F` / `/FFilter` handling).
- `FlateDecode`: RFC 1950 zlib wrapper + RFC 1951 deflate decompression (with raw-deflate fallback for non-conformant PDFs that omit or corrupt the 2-byte zlib header).
- `LZWDecode`: variable-width (9 to 12-bit) LZW decompression with clear code `256`, EOD code `257`, and `/EarlyChange` (`1` default, `0` optional).
- TIFF Predictor `2` (horizontal differencing) and PNG Predictors `10..15` (`None=0`, `Sub=1`, `Up=2`, `Average=3`, `Paeth=4`, `Optimum=15` reading the per-row filter tag byte) parameterized by `/Columns`, `/Colors` (`1..4`), and `/BitsPerComponent` (`1, 2, 4, 8, 16`).
- `ASCIIHexDecode`, `ASCII85Decode` (`z` -> `0x00000000`, `~>` EOD, partial final 2-4 byte tuple), and `RunLengthDecode` (`0..127` literal run, `129..255` repeat run, `128` EOD).
- Strict **256 MB per-stream decoded output ceiling** (`max_decoded_bytes = 256 * 1024 * 1024`) enforced before buffer expansion during decompression and across chained filter stages.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that all stream filters are pure stateless functions operating on caller-supplied `std::span<const uint8_t>` inputs and returning `PdfResult<std::vector<uint8_t>>`, sharing zero mutable state across threads and enforcing the 256 MB ceiling before allocation growth.

> **Started:** 2026-10-18

**Build order:**

1. Implement `FlateDecode` (RFC 1950/1951 + raw-deflate fallback), `LZWDecode`, TIFF/PNG predictors, `ASCIIHexDecode`, `ASCII85Decode`, `RunLengthDecode`, and the chained filter runner (`decode_stream`) with the 256 MB output cap in `src/pdf/filters.h` and `src/pdf/filters.cpp`. Done when: `filters.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_filters_tests.cpp` testing every filter, predictor (`None`, `Sub`, `Up`, `Average`, `Paeth`), chained `[ /ASCII85Decode /FlateDecode ]`, and `broken/deflate-bomb-10kb.pdf`. Done when: `PdfFilters.*` passes under ASan.

- [x] Implement `FlateDecode` (RFC 1950 zlib + RFC 1951 deflate with automatic raw-deflate fallback when the 2-byte header fails checksum) enforcing the 256 MB output cap during block expansion. Done when: `PdfFilters.FlateDecode` decodes standard zlib and raw-deflate streams and rejects `tests/corpus/broken/deflate-bomb-10kb.pdf` with `ErrorCode::DecompressionLimitExceeded`. Cheaper substitute: pre-allocating from an untrusted `/DL` or growing buffers without a ceiling.
- [x] Implement `LZWDecode` (9-12 bit codes, clear table `256`, EOD `257`, `/EarlyChange` `0` and `1`) with bounds checks against out-of-range table indices on corrupt bitstreams. Done when: `PdfFilters.LzwDecode` decodes both `/EarlyChange 1` and `/EarlyChange 0` test vectors and rejects corrupt codes safely.
- [x] Implement TIFF Predictor `2` and PNG Predictors `10..15` (`Sub`, `Up`, `Average`, `Paeth`) for `/Colors` `1..4` and `/BitsPerComponent` `1, 2, 4, 8, 16`. Done when: `PdfFilters.Predictors` decodes all 5 PNG row filters and TIFF Predictor `2` test vectors.
- [x] Implement `ASCIIHexDecode` and `ASCII85Decode` (handling whitespace, `z` four-zero-byte shorthand, partial 2-4 byte final tuples, and `~>` EOD). Done when: `PdfFilters.AsciiFilters` matches RFC/ISO test vectors and rejects invalid `u..y` characters or single-character final tuples.
- [x] Implement `RunLengthDecode` (`0..127` literal copy, `129..255` repeat `257 - length` times, `128` EOD). Done when: `PdfFilters.RunLengthDecode` round-trips literal and repeated byte runs and handles truncated runs without reading past the input buffer.
- [x] Implement chained filter execution (`/Filter [ /ASCII85Decode /FlateDecode ]`) passing `/DecodeParms` entry `i` to filter `i` and capping chained pipeline length at 8 filters. Done when: `PdfFilters.ChainedPipeline` decodes multi-stage streams and rejects 9-stage filter chains.
- [x] Commit: `pdf: implement Flate, LZW, TIFF/PNG predictors, ASCII85/Hex, RunLength and 256 MB cap`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan covering every lossless filter, predictor, and decompression-bomb refusal.

> **Verified:** 2026-10-18 | §7 | 2d527326c7727bfb3603c5182eb139eafa5e4001
> **Review:** `docs/reviews/d01-t01-s7.json`

## 8. Bi-level and image stream filters (`CCITTFaxDecode`, `JBIG2Decode`, WIC `DCT` and `JPX`)

Complete `src/pdf/filters.cpp` with monochrome bi-level fax/document decompression and WIC-backed JPEG / JPEG 2000 image decoding:
- `CCITTFaxDecode` (ITU-T T.4 Group 3 1D/2D and ITU-T T.6 Group 4 2D MMR monochrome decompression honoring `/K`, `/Columns` default `1728`, `/Rows`, `/EndOfLine`, `/EncodedByteAlign`, `/EndOfBlock`, `/BlackIs1`, and `/DamagedRowsBeforeError`).
- `JBIG2Decode` (ISO/IEC 14492 bi-level generic/symbol/halftone region decoding with `/JBIG2Globals` dictionary stream support and bounded bitmap dimensions).
- `DCTDecode` (JPEG) and `JPXDecode` (JPEG 2000) image decoding via thread-local Windows Imaging Component (`IWICImagingFactory` initialized per worker/actor thread, converting grayscale/RGB/CMYK/YCCK frames into known pixel buffers within the 256 MB cap).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that WIC `IWICImagingFactory` instances are thread-local (never shared across threads), `CCITTFaxDecode` and `JBIG2Decode` are pure stateless decoders, and image dimensions (`width * height * bytes_per_pixel`) are checked against the 256 MB ceiling before bitmap allocation.

> **Started:** 2026-10-19

**Build order:**

1. Implement `CCITTFaxDecode` (Group 3 1D/2D and Group 4 2D Huffman/mode tables), `JBIG2Decode` (with `/JBIG2Globals`), and thread-local WIC `DCTDecode` / `JPXDecode` image decoding in `src/pdf/filters.cpp`. Done when: `filters.cpp` compiles cleanly on `x64` and `ARM64`.
2. Extend `tests/recto-tests/pdf_filters_tests.cpp` testing `CCITTFaxDecode` Group 3 and Group 4 (`/BlackIs1 true` and `false`), `JBIG2Decode`, `DCTDecode` JPEG (`render/images-and-vectors.pdf`), `JPXDecode`, and oversized image header rejection (`width = 100000, height = 100000`). Done when: `PdfFilters.*` passes under ASan.

- [x] Implement `CCITTFaxDecode` Group 4 2D (`K < 0`: Pass, Vertical `V(0)`, `VR(1..3)`, `VL(1..3)`, Horizontal `H` with white/black terminating and make-up Huffman codes, and EOFB `000000000001000000000001`). Done when: `PdfFilters.CcittGroup4` decodes monochrome scanned lines in `render/images-and-vectors.pdf` to exact 1-bit/8-bit pixel rows.
- [x] Implement `CCITTFaxDecode` Group 3 1D (`K == 0`) and 2D (`K > 0`) modes and `/BlackIs1` polarity inversion + `/EncodedByteAlign` byte alignment. Done when: `PdfFilters.CcittGroup3AndBlackIs1` verifies both polarities and aligned/unaligned rows.
- [x] Implement `JBIG2Decode` (file header/page information segment `48`, immediate generic region `38`/`39` MMR and arithmetic template `0..3` decoding, symbol dictionary `0` with `/JBIG2Globals`, and end-of-page `49`). Done when: `PdfFilters.Jbig2Decode` decodes bi-level JBIG2 stream fixtures and rejects malformed segment lengths under ASan.
- [x] Implement thread-local `IWICImagingFactory` (`CoInitializeEx(nullptr, COINIT_MULTITHREADED)` per thread) for `DCTDecode` (JPEG) and `JPXDecode` (JPEG 2000) stream decoding. Done when: 8 concurrent threads decoding JPEG streams in `PdfFilters.ConcurrentWicDecode` succeed with zero COM/WIC errors under ASan. Cheaper substitute: a single global `IWICImagingFactory` shared unsafely across threads.
- [x] Handle CMYK / YCCK JPEG colour conversion (`GUID_WICPixelFormat32bppCMYK` -> RGBA/BGRA via `IWICFormatConverter` or ISO CMYK-to-sRGB formula) and `/Decode` array inversion (`[1 0 1 0 1 0 1 0]` for Adobe inverted CMYK JPEGs). Done when: `PdfFilters.DctRgbAndCmyk` decodes both RGB and Adobe CMYK JPEGs to expected pixel values.
- [x] Check image header dimensions (`uint64_t(width) * uint64_t(height) * 4 <= 256 MB`) before allocating pixel buffers in `CCITTFaxDecode`, `JBIG2Decode`, `DCTDecode`, and `JPXDecode`. Done when: a crafted JPEG/CCITT header claiming `65535 x 65535` pixels is rejected with `ErrorCode::DecompressionLimitExceeded` before allocating memory.
- [x] Commit: `pdf: add CCITTFax Group 3/4, JBIG2 and thread-safe WIC DCT/JPX decoders`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan across all lossless, CCITT, JBIG2, and WIC image tests.

> **Verified:** 2026-10-19 | §8 | 56de060e40d733fa1bcc0edd8890cbcc8799a0fc
> **Review:** `docs/reviews/d01-t01-s8.json`

## 9. Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6` via CNG)

Implement `src/pdf/crypto.h` and `src/pdf/crypto.cpp` using Windows Next Generation Cryptography (`bcrypt.dll`) for ISO 32000-1 section 7.6 and ISO 32000-2 section 7.6.4:
- Parse the trailer `/Encrypt` dictionary (`/Filter /Standard`, `/V` `1..5`, `/R` `2..6`, `/Length` `40..256`, `/P` permission bitmask, `/O`, `/U`, `/OE`, `/UE`, `/Perms`, `/EncryptMetadata`, `/CF`, `/StmF`, `/StrF`).
- Key derivation and password verification:
  - Revisions `R=2, 3, 4`: 32-byte padding string, MD5 key derivation (`Algorithm 2`), user password verification (`Algorithm 4` / `Algorithm 5`), and owner password unwrap (`Algorithm 7`).
  - Revisions `R=5, 6` (`AES-256`): UTF-8 SASLprep/truncation (up to 127 bytes), SHA-256 (`R=5`) and Algorithm 2.B iterative SHA-256/SHA-384/SHA-512 + AES-128-CBC 64-round hash (`R=6`), unwrapping the 32-byte file encryption key from `/UE` or `/OE`, and verifying `/Perms` via AES-256-ECB.
- Per-object decryption (`decrypt_object`): derive per-object `(obj_num, gen_num)` RC4 (`40..128` bit) or `AES-128-CBC` (with `"sAlT"` suffix and 16-byte IV + PKCS#7 padding) keys for `R=2..4`, or decrypt directly with the 32-byte `AES-256-CBC` file key for `R=5..6`, skipping the `/Encrypt` dictionary itself, strings inside `/ID`, and `/Sig /Contents` hex byte ranges.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `bcrypt.dll` algorithm providers are opened safely, `SecurityHandler` state is owned per `pdf::Document` on its actor thread, and decrypted indirect objects are cached inside the owning `pdf::Document` so decryption runs once per object.

> **Started:** 2026-10-19

**Build order:**

1. Implement `src/pdf/crypto.h` and `src/pdf/crypto.cpp` (`SecurityHandler` parsing `/Encrypt`, `authenticate(password)` for `R=2..6` user and owner passwords, `/Perms` verification, permission flag extraction, and `decrypt_object_values` via `bcrypt.dll`). Done when: `crypto.cpp` compiles cleanly on `x64` and `ARM64` linking `bcrypt.lib`.
2. Add `tests/recto-tests/pdf_crypto_tests.cpp` testing `render/encrypted-rc4-128.pdf` (`R=3`), `render/encrypted-aes-128.pdf` (`R=4`), and `render/encrypted-aes-256.pdf` (`R=6`) with empty password, wrong password, user password `"recto-test"`, and owner password `"recto-owner"`. Done when: `PdfCrypto.*` passes under ASan.

- [x] Parse `/Encrypt` dictionaries (`/V`, `/R`, `/Length`, `/P`, `/O`, `/U`, `/OE`, `/UE`, `/Perms`, `/EncryptMetadata`, `/CF`, `/StmF`, `/StrF`) and reject unknown `/Filter` or unsupported `/V`/`/R` combinations with `ErrorCode::UnsupportedEncryption`. Done when: `PdfCrypto.ParseEncryptDict` validates all standard revisions `R=2..6`.
- [x] Implement `R=2, 3, 4` MD5 key derivation (`BCRYPT_MD5_ALGORITHM`) and `RC4` (`BCRYPT_RC4_ALGORITHM`) / `AES-128-CBC` (`BCRYPT_AES_ALGORITHM`) user and owner password authentication. Done when: `render/encrypted-rc4-128.pdf` and `render/encrypted-aes-128.pdf` unlock with `"recto-test"` and `"recto-owner"` and reject `"wrong-pw"` with `ErrorCode::WrongPassword`.
- [x] Implement `R=5, 6` (`AES-256`) Algorithm 2.B iterative SHA-256/SHA-384/SHA-512 + AES-128-CBC key derivation, `/UE` and `/OE` 32-byte file key unwrap, and `/Perms` AES-256-ECB verification via `bcrypt.dll`. Done when: `render/encrypted-aes-256.pdf` unlocks with `"recto-test"` and `"recto-owner"` and verifies `/Perms` integrity. Cheaper substitute: supporting only unencrypted PDFs or legacy RC4.
- [x] Support empty user password (`""`) auto-unlock on open so PDFs with an owner-only restrictions password open immediately without prompting the user, while reporting `is_encrypted = true` and the active permission flags. Done when: an owner-restricted PDF with empty user password opens in one step without returning `ErrorCode::NeedsPassword`.
- [x] Implement recursive per-object `PdfString` and `PdfStream` decryption (`(obj_num, gen_num)` key derivation, 16-byte random IV prefix stripping, and PKCS#7 padding validation for AES-128/AES-256) before stream filter execution. Done when: decrypted page streams in all 3 encrypted corpus files decompress via `FlateDecode` to their exact plaintext content streams.
- [x] Preserve unencrypted exemptions per ISO 32000-1 section 7.6.2: never decrypt strings inside the `/Encrypt` dictionary, trailer `/ID`, `/Sig /Contents` signature hex strings, or objects inside an already-decrypted `/Type /ObjStm` (where the `/ObjStm` stream itself was decrypted). Done when: `PdfCrypto.Exemptions` verifies `/Sig /Contents` and `/ObjStm` children are not double-decrypted.
- [x] Extract structured `PdfPermissions` (`can_print`, `can_print_high_quality`, `can_modify`, `can_copy_text`, `can_annotate`, `can_fill_forms`, `can_extract_for_accessibility`, `can_assemble`) from the signed 32-bit `/P` bitmask. Done when: `PdfCrypto.PermissionsBitmask` matches ISO 32000-1 Table 22 bit positions (`bits 3, 4, 5, 6, 9, 10, 11, 12`).
- [x] Commit: `pdf: implement Windows CNG Standard Security Handler decryption (R2-R6)`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCrypto.*` passes under ASan across `RC4-128`, `AES-128`, and `AES-256` (`R=6`) fixtures.

## 10. `pdf::Document` page-tree resolver, metadata, `/Outlines`, font list and `/Annots` links

Implement `src/pdf/document.h` and `src/pdf/document.cpp` (`recto::pdf::Document`) bringing together the lexer, parser, XRef/ObjStm resolver, stream filters, and CNG security handler:
- `Document::open(path)` and `Document::open_memory(bytes)` + `authenticate(password)` returning `PdfResult<std::unique_ptr<Document>>`.
- Flattened `/Pages` tree traversal with inherited `/Resources`, `/MediaBox`, `/CropBox`, and `/Rotate` (`0`, `90`, `180`, `270` normalized) and a visited-node set that rejects cyclic `/Pages /Kids` graphs (`tests/corpus/broken/cyclic-pages-tree.pdf`).
- Document metadata extraction from `/Info` (`/Title`, `/Author`, `/Subject`, `/Keywords`, `/Creator`, `/Producer`, `/CreationDate`, `/ModDate` parsed from `D:YYYYMMDDHHmmSSOHH'mm'`) and `/Catalog /Metadata` XMP stream fallback.
- Hierarchical `/Outlines` bookmark tree (`title`, `page_index`, `dest_y_pt`, `zoom`, `is_open`, `children`) resolving explicit destination arrays (`[page /XYZ left top zoom]`, `/Fit`, `/FitH`, etc.) and named destinations (`/Names /Dests` and `/Dests` dictionary) with cycle protection on `/First`/`/Next`/`/Parent` pointers.
- Per-page `/Annots` `/Subtype /Link` extraction (`rect_pt`, internal destination `page_index`/`y_pt` or external `uri`, while marking `/S /Launch` and `/S /JavaScript` actions as `InertAction` so they never execute) and document font inventory (`/BaseFont`, `/Subtype`, `is_embedded`, `is_subset` `ABCDEF+` prefix, `/Encoding`).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `pdf::Document` has no static mutable fields, every lazy object resolution and stream decode occurs on the owning `Document` instance, and `/Launch` / `/JavaScript` actions are flagged inert and never executed.

**Build order:**

1. Implement `src/pdf/document.h` and `src/pdf/document.cpp` (`Document::open`, `open_memory`, `authenticate`, `description()`, `outline()`, `page_links(page_index)`, `fonts()`, `decoded_page_contents(page_index)`, and `page_resources(page_index)`). Done when: `document.cpp` compiles cleanly on `x64` and `ARM64`.
2. Add `tests/recto-tests/pdf_document_tests.cpp` testing every file in `tests/corpus/render/`, `tests/corpus/text/`, `tests/corpus/convert/`, and `tests/corpus/broken/`. Done when: `PdfDocument.*` passes 100% under ASan.

- [ ] Implement `Document::open(const std::filesystem::path&)` and `Document::open_memory(std::span<const uint8_t>)` wiring XRef loading (`§5`, `§6`), damaged-file fallback recovery (`§6`), and automatic empty-password check (`§9`). Done when: unencrypted corpus files open directly and encrypted corpus files return `ErrorCode::NeedsPassword` until `authenticate("recto-test")` is called.
- [ ] Traverse the `/Pages` `/Kids` tree iteratively/recursively with a `std::unordered_set<int>` visited object-number guard and a 64-level depth cap, propagating inherited `/MediaBox`, `/CropBox`, `/Rotate`, and `/Resources` down to each `/Type /Page` leaf. Done when: `render/rotated-pages.pdf` reports exact per-page boxes and rotations and `broken/cyclic-pages-tree.pdf` fails cleanly with `ErrorCode::DamagedFormat`.
- [ ] Concatenate and decode `/Page /Contents` whether `/Contents` is a single stream reference or an array of stream references (inserting a single whitespace byte between array streams so adjacent tokens at stream boundaries never fuse). Done when: multi-stream `/Contents` arrays decode cleanly in `PdfDocument.PageContents`.
- [ ] Parse `/Info` metadata strings (`pdf_string_to_utf8`), PDF `D:YYYYMMDDHHmmSS` date strings, PDF version (`1.0..2.0` from header and `/Catalog /Version` override), and XMP `/Metadata` `dc:title` / `dc:creator` fallback. Done when: `PdfDocument.Metadata` reads exact title, author, PDF version, and dates from corpus files.
- [ ] Resolve `/Catalog /Outlines` (`/First`, `/Next`, `/Count`) and named destinations (`/Catalog /Names /Dests` name tree and `/Catalog /Dests` dict) into a cycle-safe tree of `OutlineNode` structs. Done when: `text/two-column-links-outline.pdf` returns its complete multi-level outline with target 0-based page indices and vertical coordinates.
- [ ] Extract per-page `/Subtype /Link` annotations from `/Page /Annots` (`/Rect`, `/Dest` or `/A << /S /GoTo | /URI | /GoToR | /Launch | /JavaScript >>`), classifying internal page jumps, external `http://`/`https://`/`mailto:` URIs, and flagging `/Launch` and `/JavaScript` as inert (`LinkKind::RefusedAction`). Done when: `PdfDocument.LinksAndRefusedActions` extracts internal and URI links from `text/two-column-links-outline.pdf` and marks a crafted `/Launch` action as `RefusedAction`.
- [ ] Enumerate page and Form XObject `/Font` resource dictionaries across the document into a deduplicated `std::vector<FontSummary>` (`name`, `subtype`, `encoding`, `is_embedded` via `/FontDescriptor /FontFile|/FontFile2|/FontFile3`, `is_subset` via `^[A-Z]{6}\+`). Done when: `PdfDocument.FontInventory` reports accurate font descriptors on corpus files.
- [ ] Verify that every file in `tests/corpus/broken/` (except `corrupt-xref-recoverable.pdf`, which recovers cleanly) returns a typed `PdfError` without crashing or leaking under ASan. Done when: `PdfDocument.BrokenCorpusSweep` passes 100% under ASan.
- [ ] Commit: `pdf: implement pdf::Document page-tree, metadata, outlines, fonts and links`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfError.*:PdfObject.*:PdfLexer.*:PdfParser.*:PdfXref.*:PdfFilters.*:PdfCrypto.*:PdfDocument.*` passes under ASan with 0 warnings.

## Verification

- `recto-tests.exe --gtest_filter=Pdf*` passes in Debug, Release, and AddressSanitizer (`-EnableASAN`) builds with 0 warnings.
- Every file in `tests/corpus/broken/` returns a typed `PdfError` (or recovers via `recover_damaged_xref` on `corrupt-xref-recoverable.pdf`) with zero crashes or leaks.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
