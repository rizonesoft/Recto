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

> **Goal:** Build the parser, stream-filter, decryption and document-model half of `recto-pdf` (`src/pdf`, C++20): a binary-safe PDF 1.7 and 2.0 lexer and indirect object parser, classic XRef tables, compressed XRef streams (`PDF 1.5+`), incremental updates and damaged-file recovery, the full stream filter pipeline (`FlateDecode`, `PngPredictor`, `LZWDecode`, `ASCII85Decode`, `ASCIIHexDecode`, `RunLengthDecode`, `CCITTFaxDecode`, `DCTDecode` and `JPXDecode` via WIC), Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6` via Windows CNG `bcrypt.dll`), and the `pdf::Document` page-tree, metadata, outline and link model.

> **Current state:** Measured 2026-10-14. `src/pdf/recto-pdf.vcxproj` compiles the scaffold `recto::pdf::abi_version()` entry point. No PDF parsing code exists yet; `D00 T03` will provide the GoogleTest fixtures, the redistributable test corpus, and the ASan job.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D3, D5, D9, D12, D14, D16), 3.1 to 3.3 (`src/pdf`, `PdfResult<T>`, `PdfError`, `ErrorCode`), 4.1 (what `recto-pdf` implements), 4.2 (thread ownership rules), 9 (security and untrusted input).
- ISO 32000-1:2008 (PDF 1.7) and ISO 32000-2:2020 (PDF 2.0) specifications for syntax, XRef streams, filter parameters, and encryption algorithms.
- -> XREF: D01 T02 §1 -- the content-stream interpreter and `DocumentActor` in `D01 T02` consume `pdf::Document`, decoded content streams, and page resource dictionaries produced here.
- -> XREF: D04 T02 §1 -- `PdfWriter` in `D04 T02 §1` shares the object model, filter encoders and CNG cryptographic primitives built here.
- -> XREF: D06 T02 §1 -- the hostile-input audit in `D06 T02 §1` verifies the recursion caps, stream decompression ceilings, and absence of JavaScript execution implemented here.

## Outcome

- `recto::pdf::Document::open` parses valid and incrementally updated PDF 1.0 to 2.0 documents, reconstructs damaged XRef tables via the fallback object scanner, and returns typed `PdfError` values on corrupt or truncated inputs without throwing exceptions or leaking memory.
- Every standard PDF stream filter (`FlateDecode`, predictors, `LZWDecode`, `ASCII85Decode`, `ASCIIHexDecode`, `RunLengthDecode`, `CCITTFaxDecode` Group 3/4, `DCTDecode` JPEG, `JPXDecode` JPEG2000) decodes within enforced byte ceilings.
- Encrypted PDFs (`RC4` 40-128 bit, `AES-128` `R=4`, `AES-256` `R=5..6`) report `ErrorCode::NeedsPassword`, authenticate user and owner passwords via Windows CNG `bcrypt.dll`, and transparently decrypt indirect strings and streams.
- Document description (page count, page boxes and rotations, metadata, security summary, font inventory, outline tree, page link annotations) is exposed in plain C++20 structs.

**Adjacency:** all=not-applicable (low-level C++20 PDF format parser and object model in `src/pdf`; all user surfaces live in domains 02 to 05 and 07)

**Adjacency rationale:** `recto-pdf` has no UI, settings store or notification channel of its own. Its error diagnostics feed the D16 ring buffer and the viewer's "Couldn't open this file -> Copy error details" dialog owned by D02 T01 §4.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | `PdfResult`, error diagnostics, lexer and indirect object parser | D00 T01 §3, D00 T03 §1 | [ ] |
| 2 | §2 | Classic XRef tables, compressed XRef streams and damaged-file recovery | §1, D00 T03 §2 | [ ] |
| 3 | §3 | Stream filter pipeline and WIC image decoders | §2 | [ ] |
| 4 | §4 | Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` via CNG) and document description | §3 | [ ] |

## 1. `PdfResult`, error diagnostics, lexer and indirect object parser

Define the foundational `recto::pdf` result and object types in `src/pdf/`: `ErrorCode`, `PdfError`, `PdfResult<T>`, the D16 thread-safe diagnostic ring buffer (`recto::pdf::diagnostics_tail()`), and `PdfValue` (`null`, `bool`, `int64_t`, `double`, `PdfString`, `PdfName`, `PdfArray`, `PdfDict`, `PdfStream`, `IndirectRef`). Implement the binary-safe PDF lexer (`src/pdf/lexer.cpp`) and recursive-descent object parser (`src/pdf/parser.cpp`) with explicit nesting depth bounds (max 64 nested arrays/dictionaries).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that `src/pdf` uses `PdfResult<T>` for all parse failures (never unhandled C++ exceptions across module boundaries), enforces the 64-level recursion cap on nested arrays and dictionaries, and holds zero mutable global parser state so distinct `pdf::Document` instances on separate threads share nothing.

**Build order:**

1. Author `src/pdf/recto_pdf.h`, `src/pdf/error.h/.cpp` (with the D16 bounded diagnostic ring buffer) and `src/pdf/object.h/.cpp` defining `PdfValue` and dictionary lookup helpers. Done when: unit tests construct, query and clone nested dictionaries and arrays under ASan with zero leaks.
2. Implement `src/pdf/lexer.cpp` and `src/pdf/parser.cpp` covering PDF comments (`%`), numbers, literal strings (nested balanced parentheses, octal `\ddd` and line-continuation escapes), hex strings (`<...>`), names (`/Name` with `#XX` hex escapes), arrays (`[...]`), dictionaries (`<<...>>`), indirect object definitions (`N G obj ... endobj`), and indirect references (`N G R`). Done when: `PdfParser.*` tests parse every token kind, reject 65-deep nested arrays with `ErrorCode::DamagedFormat`, and handle truncated tokens at every byte offset without reading past the buffer.

- [ ] `PdfResult<T>`, `PdfError`, diagnostic ring buffer, and `PdfValue` model implemented in `src/pdf/`. Done when: `PdfParser.Diagnostics` verifies ring-buffer wraparound and thread-safe error recording. Cheaper substitute: throwing `std::runtime_error` on malformed PDF tokens.
- [ ] Binary-safe lexer and bounded object parser (`src/pdf/lexer.cpp`, `src/pdf/parser.cpp`). Done when: byte-by-byte truncation tests on sample object streams return `ErrorCode::DamagedFormat` at every cut point under ASan.
- [ ] Commit: `pdf: add PdfResult, diagnostic ring, lexer and bounded object parser`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfParser.*` passes under ASan with 0 warnings.

## 2. Classic XRef tables, compressed XRef streams and damaged-file recovery

Implement `src/pdf/xref.cpp`: locate `startxref` from the end of the file, follow `/Prev` chains across incremental updates with cycle detection, parse classic `xref` subsections and trailer dictionaries, parse PDF 1.5+ cross-reference stream objects (`/Type /XRef` with `/W` field widths and `/Index` subsections) and compressed object streams (`/Type /ObjStm` once `FlateDecode` from §3 is wired, with uncompressed/identity hooks tested here), and implement the damaged-file fallback scanner that linearly scans the file for `N G obj` headers when `startxref` or an XRef table is corrupt.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that lazy object resolution (`Document::resolve(IndirectRef)`) mutates only the owning `pdf::Document`'s object table, detects cyclic indirect-reference chains (`A -> B -> A`) via a per-call visited set, and caps `/Prev` trailer chains at 256 revisions.

**Build order:**

1. Implement classic `xref` table parsing, trailer merging across `/Prev` incremental updates, and cycle detection on both `/Prev` offsets and indirect reference chains. Done when: a multi-revision incremental-update test PDF resolves updated objects from the newest generation and a cyclic `/Prev` trailer fails with `ErrorCode::DamagedFormat`.
2. Implement `/Type /XRef` binary entry unpacking (`/W [w1 w2 w3]`, `/Index`) and `/Type /ObjStm` offset table parsing, plus the damaged-file `N G obj` recovery scanner. Done when: corrupting the `startxref` offset of a corpus PDF still opens and resolves all objects via the recovery scanner, and `broken/` files without any objects return `ErrorCode::DamagedFormat`.

- [ ] Classic `xref`, `/Prev` incremental updates, and `/Type /XRef` + `/Type /ObjStm` parsing with cycle detection. Done when: classic and 1.5+ cross-reference fixtures resolve every indirect object identically. Cheaper substitute: supporting only classic ASCII `xref` tables, which fails on modern PDFs produced by Word, Chrome and Acrobat.
- [ ] Damaged-file fallback scanner reconstructs the object index when `startxref` is corrupt. Done when: a corpus PDF with a zeroed `xref` table recovers all pages and metadata in `PdfXref.DamagedRecovery`.
- [ ] Commit: `pdf: implement XRef tables, compressed XRef streams and damaged-file recovery`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfXref.*` passes under ASan across classic, incremental, compressed XRef and damaged-xref fixtures.

## 3. Stream filter pipeline and WIC image decoders

Implement `src/pdf/filters.cpp` for chained stream filters (`/Filter` single name or array, `/DecodeParms`): `FlateDecode` (RFC 1950 zlib header + RFC 1951 deflate), `LZWDecode` (variable-width LZW with `/EarlyChange`), TIFF Predictor 2 and PNG Predictors 10 to 15 (None, Sub, Up, Average, Paeth), `ASCIIHexDecode`, `ASCII85Decode`, `RunLengthDecode`, `CCITTFaxDecode` (Group 3 1D/2D and Group 4 2D monochrome fax decompression with `/K`, `/Columns`, `/BlackIs1`), and WIC-backed `DCTDecode` (JPEG) and `JPXDecode` (JPEG 2000). Enforce a per-stream decoded output ceiling (default 256 MB) so decompression bombs abort cleanly.

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Verify that stream filters are stateless functions operating on caller-supplied spans/buffers (or thread-local WIC factories initialized per worker/actor thread), never sharing COM/WIC decoder instances across threads, and enforcing the 256 MB decompression cap before allocation growth.

**Build order:**

1. Implement `FlateDecode`, `LZWDecode`, TIFF/PNG predictors, `ASCIIHexDecode`, `ASCII85Decode`, and `RunLengthDecode`, plus the chained filter runner and 256 MB output cap. Wire `FlateDecode` + PNG predictor into §2's `/Type /XRef` and `/Type /ObjStm` loader. Done when: compressed XRef streams and chained `[ /ASCII85Decode /FlateDecode ]` streams decode byte-exactly against known test vectors, and a 10 KB deflate bomb targeting 512 MB aborts with `ErrorCode::DamagedFormat`.
2. Implement `CCITTFaxDecode` (Group 3 and Group 4 tables) and WIC-backed `DCTDecode` / `JPXDecode` image stream decoding. Done when: monochrome CCITT scanned-page streams and embedded JPEG/JPX image streams decode to expected pixel buffers under ASan.

- [ ] Stateless filter pipeline (`Flate`, `LZW`, TIFF/PNG predictors, `ASCII85`, `ASCIIHex`, `RunLength`, `CCITTFax`) with 256 MB decompression bomb guard. Done when: all filter test vectors and compressed XRef/ObjStm corpus files decode cleanly and the bomb vector refuses safely. Cheaper substitute: unbounded buffer doubling on corrupt `/Length` or deflate streams.
- [ ] WIC `DCTDecode` and `JPXDecode` decoders with per-thread COM/WIC safety. Done when: embedded JPEG image XObjects in the corpus decode to exact dimensions and pixel values.
- [ ] Commit: `pdf: implement stream filter pipeline, CCITT fax and WIC image decoders`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfFilters.*` passes under ASan covering every filter, predictor, CCITT Group 4, JPEG decode and decompression-bomb refusal.

## 4. Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` via CNG) and document description

Implement `src/pdf/crypto.cpp` using Windows Next Generation Cryptography (`bcrypt.dll`): Standard Security Handler revisions `R=2, 3, 4` (`RC4` 40 to 128-bit and `AES-128` CBC with MD5 key derivation and `/CF` crypt filters) and `R=5, 6` (`AES-256` CBC with Algorithm 2.B iterative SHA-256/SHA-384/SHA-512 password validation and file encryption key unwrapping). Then implement `src/pdf/document.cpp` exposing the flattened `/Pages` tree (with inherited `/MediaBox`, `/CropBox`, `/Rotate`, `/Resources` and cycle detection), `/Info` and XMP metadata, security and permissions summary, document font inventory, hierarchical `/Outlines` bookmark tree, and per-page `/Annots` link annotations (internal destinations and external URIs, with `/Launch` and `/JavaScript` flagged inert).

**Phase:** 1
**Needs:** windows-host
**Surface:** no
**Freeze check:** Confirm that `bcrypt.dll` algorithm handles are either opened per operation or held read-only, decryption integrates into `pdf::Document`'s single-owner object loader so strings and streams are decrypted once on the owning actor thread, and `/Launch` / `/JavaScript` actions are never executed.

**Build order:**

1. Implement `src/pdf/crypto.cpp` (`R=2..6` password verification, file encryption key derivation, and per-object `RC4` / `AES-128` / `AES-256` string and stream decryption via `bcrypt.dll`). Done when: an unauthenticated encrypted corpus PDF returns `ErrorCode::NeedsPassword`, a wrong password returns `ErrorCode::WrongPassword`, and `"recto-test"` unlocks both `AES-128` (`R=4`) and `AES-256` (`R=6`) fixtures and decodes their page streams.
2. Implement `src/pdf/document.cpp` (`Document::open`, `authenticate`, page tree traversal with cycle guard, page geometry `PageSizePt`, metadata, security flags, font list, `/Outlines` tree, and `/Annots` link extraction). Done when: `PdfDocument.*` tests read page count, dimensions, rotations, metadata, outlines, links, fonts, and security details across the entire `tests/corpus/` set, and every `tests/corpus/broken/` file fails gracefully with a `PdfError`.

- [ ] Windows CNG (`bcrypt.dll`) Standard Security Handler (`R=2..6`, `RC4`, `AES-128`, `AES-256`) integrated into object and stream loading. Done when: encrypted corpus files unlock with the user or owner password and report exact permission flags. Cheaper substitute: supporting only unencrypted PDFs or rolling custom unverified crypto routines.
- [ ] Page tree, metadata, outline tree, font inventory, and link annotation extraction with cyclic `/Pages` and `/Outlines` protection. Done when: all `render/`, `text/`, `convert/` and `broken/` corpus files pass `PdfDocument.*` under ASan.
- [ ] Commit: `pdf: add CNG decryption (R2-R6) and document page-tree, outline and link model`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfCrypto.*:PdfDocument.*` passes under ASan across encrypted, outlined, linked, rotated and broken corpus files.

## Verification

- `recto-tests.exe --gtest_filter=Pdf*` passes in both Debug and Release and under AddressSanitizer with 0 warnings.
- Every file in `tests/corpus/broken/` returns a typed `PdfError` with zero crashes or leaks.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
