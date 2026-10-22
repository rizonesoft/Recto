# Audit - D01 T01 §6: PDF 1.5+ `/Type /XRef` streams, `/Type /ObjStm` streams and damaged-file recovery

- **Audit section:** `D06 T05 §21` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §6` (`docs/reviews/d01-t01-s6.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `6f80de1`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 12 tests.` (12 tests from 1 suite). The checkpoint holds.

## Round 1: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T10:44:26Z
- Effort: xhigh
- Lines: 32
- Digest: 5a6dcfbd27d642b7

### adversarial: needs-attention

1. **P2, recovery omits indirect stream-parameter resolution.** At [src/pdf/xref.cpp:872](/R:/GitHub/Recto/src/pdf/xref.cpp:872), recovery calls `decode_stream(stm)` without a resolver and skips ordinary decoding errors. Reproduce with a missing `startxref`, an otherwise intact ASCIIHex-encoded `/ObjStm` containing the only `/Catalog`, and `/DecodeParms 2 0 R` referencing an uncompressed empty dictionary. Recovery fails to discover the catalog and returns `DamagedFormat`. The normal resolver handles this case after D06 T05 §20, but recovery still cannot.

2. **P2, ignored xref-stream entries can still fail parsing.** [src/pdf/xref.cpp:541](/R:/GitHub/Recto/src/pdf/xref.cpp:541) validates offsets before the `/Size` exclusion at line 1501. Reproduce with an otherwise readable xref-stream PDF using `/Size 4`, `/Index [0 4 99 1]`, and an out-of-file type-1 offset for object 99. Strict parsing returns `DamagedFormat` instead of ignoring object 99. D06 T05 §19 fixed this ordering for classic tables only. The required missing-object treatment also applies to cross-reference streams. [PDF specification errata, §7.5.5](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html#7.5.5)

### consistency: approve

No findings.

### integration: needs-attention

3. **P1, encrypted extraction bypasses the promised forbidden-stream checks.** [src/pdf/document.cpp:1658](/R:/GitHub/Recto/src/pdf/document.cpp:1658) and [src/pdf/crypto.cpp:2312](/R:/GitHub/Recto/src/pdf/crypto.cpp:2312) parse one direct object and return without checking remaining tokens or forbidden object-stream dictionaries. Construct an encrypted `/ObjStm`, `/N 1`, `/First 4`, whose decrypted payload is `2 0 << /Length 4 >>\nstream\nabcd\nendstream`. Resolving object 2 succeeds with a dictionary, rather than returning `DamagedFormat`. The equivalent unencrypted case is explicitly refused by `PdfXref.ObjectStream`. Later encrypted consumers have lost §6’s refusal guarantee.

4. **P2, encrypted documents repeatedly decompress sibling objects.** [src/pdf/document.cpp:1542](/R:/GitHub/Recto/src/pdf/document.cpp:1542) reparses and decrypts the container, then line 1588 decodes it into a local vector for every compressed-object resolution. This path uses neither decoded object-stream cache. Reproduce with two sibling objects in one encrypted `/ObjStm`: resolve both through `Document::resolve_object`, then resolve either again. A breakpoint on the decode call records three decompressions. This contradicts the ticked requirement that sibling objects share a decoded stream per document.

### source-defect: needs-attention

5. **P2, valid indirect `/N` and `/First` values are rejected.** [src/pdf/xref.cpp:1682](/R:/GitHub/Recto/src/pdf/xref.cpp:1682) requires direct integers; the encrypted implementations repeat that restriction. Reproduce with `/N 2 0 R /First 3 0 R`, uncompressed objects 2 and 3 containing integers `1` and `4`, and decoded payload `1 0 42`. Resolving compressed object 1 returns `DamagedFormat`. Object-stream dictionary values may reference uncompressed indirect objects. [PDF specification errata, §7.5.7](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html#7.5.7)

6. **P2, reference-only compressed objects are accepted and treated as valid test data.** [src/pdf/xref.cpp:1879](/R:/GitHub/Recto/src/pdf/xref.cpp:1879) returns an extracted `IndirectRef` without refusal, and resolution subsequently follows it. Reproduce with `/N 1 /First 4` and payload `1 0 2 0 R`. ISO 32000-1 §7.5.7 prohibits a compressed object consisting solely of an object reference. The existing fixture at [tests/recto-tests/pdf_document_tests.cpp:617](/R:/GitHub/Recto/tests/recto-tests/pdf_document_tests.cpp:617) explicitly depends on this prohibited representation; rerunning `PdfDocument.PageTreeAndRotatedPages` confirmed it currently passes. [ISO 32000-1, §7.5.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf)

### design: approve

Not applicable; this section owns no user surface.

### record: advisory

At clean `8acebbe`, the existing Release binary passed all 12 `PdfXref.*` tests plus `Support.AsanActiveWhenEnabled`, which reported `ASan=enabled`. Only the audit Markdown changed since the recorded `6f80de1` checkpoint. A fresh build was not run because it writes files. Except for the reference-only fixture rerun, the reproductions above are source-traced. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

Findings 3 to 6 share one root: an object stream was read by three copies of the same code (`XrefTable`, `SecurityHandler`, `Document`) that had drifted, the two encrypted copies lacking checks the first had. The fix is one reading, not three patches. `parse_object_stream` and `extract_object_stream_member` in `src/pdf/xref.h` hold every ISO 32000-1 section 7.5.7 check, built from `XrefTable`'s (the strictest) copy, and all three paths now call them.

- Finding 3 (P1): correct. The encrypted paths parsed one direct object and returned it, skipping the forbidden-member checks. Through the shared routine they refuse a stream, an `N G obj` wrapper, trailing tokens and nested `/ObjStm` or `/XRef` dictionaries, as the unencrypted path did.
- Finding 4 (P2): correct. `Document`'s encrypted path decrypted and decoded the container for every member. It now keeps a `decrypted_obj_stm_cache_` (bounded by `kMaxCachedObjStreams` like `XrefTable`'s, owned by the `DocumentActor` thread like every member), observable through `cached_decrypted_obj_stream_count()`.
- Finding 5 (P2): correct. `/N` and `/First` resolve one level through each path's cycle-safe resolver.
- Finding 6 (P2): correct. A compressed object that is solely a reference is refused, and the encrypted paths no longer follow one as a hop. `PdfDocument.PageTreeAndRotatedPages` relied on that prohibited form. Its alias chains are rewritten legally: the two large dictionaries are now the compressed objects (10 and 11 in stream 5), and the commented aliases stay uncompressed (6 to 10, 8 to 6, 7 to 11, 9 to 7). Its intent, one cache entry shared across alias chains through compressed objects, is unchanged.
- Finding 2 (P2): correct. Cross-reference stream rows numbered above the governing `/Size` (the merged trailer's, else the stream's own) are skipped before their fields are validated, as D06 T05 §19 did for classic tables.
- Finding 1 (P2): filed, not fixed here. Recovery is a single forward scan, so an indirect parameter can point at an object not yet found. Fixing it means deferring object-stream unpacking until the scan ends while keeping the scan's override order. It is filed with an owner as `D06 T05 §29` (`Damaged-file recovery resolves indirect object-stream parameters`), and `D06 T05 §21` carries the deferral.

Tests, each failing on the code before this round: `PdfDocument.EncryptedObjectStreamsShareOneDecodeAndRefuseForbiddenMembers` (an incremental update of the RC4 corpus file with two encrypted object streams: siblings 21 and 22 resolve with one decode, a stream member is refused by `Document` and by `SecurityHandler`); `PdfXref.ObjStmIndirectCountsAndBareReferences` (`/N 2 0 R /First 3 0 R` resolve, a bare `2 0 R` member is refused); `PdfXref.XrefStreamEntriesAboveSizeAreMissing` (object 99 with an offset outside the file under `/Size 4` is missing, with no recovery).

Architecture check: the change keeps the foundation plan's actor contract. The new cache is an instance member of `Document`, owned by its `DocumentActor` thread like `XrefTable`'s cache, and no state is shared across documents or threads.

Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 122 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 33 tests.`; `python scripts/todo-graph.py validate` -> `valid: 682 sections`.

## Round 2: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T11:06:46Z
- Effort: xhigh
- Lines: 28
- Digest: 466715be2ab14e6f

### adversarial: needs-attention

1. **P1, encrypted object-stream retention has no cumulative limit.** [src/pdf/crypto.cpp:2237](/R:/GitHub/Recto/src/pdf/crypto.cpp:2237) inserts every decoded container into `decrypted_obj_stm_cache_` without eviction or a byte budget. The encrypted container and extracted values also remain in `decrypted_cache_`. Reproduce through `SecurityHandler::resolve_and_decrypt_object` with distinct encrypted Flate object streams, each containing an integer followed by 16 MiB of whitespace. Resolving one member per container retains approximately 16 GiB after 1,024 containers, despite every stream satisfying the individual decoding ceiling. This violates the hostile-input allocation contract.

### consistency: approve

No additional findings.

### integration: needs-attention

2. **P2, recovery accepts forbidden members and can invalidate an intact catalog.** [src/pdf/xref.cpp:971](/R:/GitHub/Recto/src/pdf/xref.cpp:971) still parses members independently and omits the shared reader’s bare-reference refusal. At [src/pdf/xref.cpp:1008](/R:/GitHub/Recto/src/pdf/xref.cpp:1008), that accepted member can invalidate another container. Reproduce using `PdfXref.DamagedRecovery`’s existing `pdf_rejected_inner` construction, replacing `bad_inner_stm` with `"9 0\n2 0 R"`. Recovery accepts the forbidden compressed reference, invalidates container 9 and its catalog, then returns `DamagedFormat`. The invalid member should be skipped, preserving the intact container. This requires no indirect stream parameters.

### source-defect: needs-attention

3. **P2, valid indirect `/Type` values remain unsupported.** [src/pdf/xref.cpp:1681](/R:/GitHub/Recto/src/pdf/xref.cpp:1681) requires a direct `/ObjStm` name. [src/pdf/document.cpp:1565](/R:/GitHub/Recto/src/pdf/document.cpp:1565) and [src/pdf/crypto.cpp:2215](/R:/GitHub/Recto/src/pdf/crypto.cpp:2215) repeat that restriction. Reproduce with an object stream using `/Type 2 0 R /N 1 /First 4`, uncompressed object 2 containing `/ObjStm`, and decoded payload `1 0 42`. Resolving compressed object 1 returns `DamagedFormat`. Object-stream dictionary values may reference uncompressed indirect objects, including `/Type`. [PDF specification errata, §7.5.7](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html#7.5.7)

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

At clean `a5a1430`, reran the existing Release binary: all 14 `PdfXref.*` tests passed, and `Support.AsanActiveWhenEnabled` passed with `ASan=enabled`. All 19 `PdfDocument.*` and `PdfCrypto.*` tests also passed. Graph validation reported `valid: 682 sections`; the generated plan was current.

The three reproductions above are source-traced, not executed. No fresh compilation was performed and no files were modified. The indirect recovery-parameter limitation is already owned by `D06 T05 §29`.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P1): correct, and wider than `SecurityHandler`. `SecurityHandler`'s decoded-container cache had no bound, and `XrefTable`'s and `Document`'s were bounded by count only (64 containers of up to 256 MiB each). All three now use one `ObjStmCache` (`src/pdf/xref.h`), bounded by `kMaxCachedObjStreams` and by `kMaxCachedObjStmBytes` (256 MiB of decoded bytes), with the oldest entries leaving first. A container larger than the whole byte budget is used once and never kept. `SecurityHandler::decrypted_cache_` holds decrypted object values whose size is bounded by the file's own bytes, so it is unchanged. `PdfXref.ObjStmCacheBoundsCountAndBytes` checks both bounds, oldest-first eviction, the oversized container and `erase`; with the byte eviction disabled it fails.
- Finding 2 (P2): correct. Recovery read object-stream members with its own copy of the checks, which lacked the bare-reference rule, so a forbidden member was accepted and then invalidated another container's catalog. Recovery now reads members through `extract_object_stream_member` too, skipping an invalid member (as it skipped unparsable ones) and keeping the rest. `PdfXref.DamagedRecovery` adds the reviewer's case (`9 0\n2 0 R` in container 10), and container 9's catalog survives. It fails on the round 1 code.
- Finding 3 (P2): correct. `is_object_stream` resolves an indirect `/Type` one level through each path's cycle-safe resolver, and `XrefTable`, `SecurityHandler` and `Document` all use it. `PdfXref.ObjStmIndirectCountsAndBareReferences` adds `/Type 3 0 R` (object 3 holds `/ObjStm`); it fails with the resolution disabled.

Gates after round 2: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 123 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 34 tests.`

## Round 3: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T11:26:45Z
- Effort: xhigh
- Lines: 28
- Digest: 9a8ea108790ddebb

### adversarial: needs-attention

1. **P1, extracted members bypass the cache byte budget.** `src/pdf/crypto.cpp:2252` retains each extracted member in the unbounded `decrypted_cache_`, independently of container eviction. Reproduce with 32 encrypted, RunLength-encoded object streams, each containing a 16 MiB literal string. Resolve each member through `SecurityHandler::resolve_and_decrypt_object`, discarding returned values. The member cache retains approximately 512 MiB despite the 256 MiB container budget. More containers keep increasing retention.

### consistency: approve

No additional findings.

### integration: needs-attention

2. **P2, allocation failure corrupts cache state and makes retry unsafe.** `src/pdf/xref.cpp:1729` updates `order_` and `bytes_` before `by_num_.emplace` succeeds. Reproduce by inserting a budget-sized container and injecting `std::bad_alloc` at the map insertion. After catching the exception, insert a one-byte container. Eviction finds the phantom entry in `order_`, then dereferences `by_num_.end()` at line 1725. Public resolver wrappers return `OutOfMemory` without clearing this corrupted cache, so a subsequent resolution can crash.

### source-defect: needs-attention

3. **P2, recovery rejects an intact catalog with an indirect `/Type`.** `src/pdf/xref.cpp:855` recognizes catalogs only through direct names; root validation at line 1053 subsequently depends exclusively on that recognition. Reproduce with an otherwise intact document whose catalog contains `/Type 4 0 R`, object 4 contains `/Catalog`, and the surviving trailer correctly references the catalog. Remove `startxref`: recovery returns `DamagedFormat`. Dictionary values may be indirect unless explicitly restricted. This case uses no object stream and is separate from D06 T05 §29. [PDF specification, indirect objects](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.5_v6.pdf)

### design: advisory

Not applicable.

### record: needs-attention

4. **P3, the recorded retention justification is false.** `docs/reviews/06-quality/audit-d01-t01-s6.md:108` states that `decrypted_cache_` values are bounded by the file’s own bytes. Finding 1 reproduces the contradiction: cached members contain expanded data.

The existing Release binary passed all 15 `PdfXref.*` tests, 19 document/crypto tests, and the ASan activation check. Graph validation and plan checks passed. No fresh compilation was performed. Findings 1 to 3 are source-traced reproductions, not executed probes. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

- Finding 1 (P1): correct, and finding 4 (P3) with it: my round 2 note said `decrypted_cache_` was bounded by the file's own bytes, which is false for object-stream members, since a member can expand far past its share of the file. Members are no longer kept in `decrypted_cache_`. They stay decoded in the bounded `decrypted_obj_stm_cache_` and are re-read from it (extraction is a parse of a slice). `decrypted_cache_` now holds only decrypted uncompressed objects and containers, whose size the file does bound, and `crypto.h` says so. `PdfCrypto.Exemptions` now expects one cached decrypted object (the container) after a member resolves, where it used to expect two, and adds a third resolution with no file bytes served from the decoded container. This is a deliberate change of what is cached, not a weakened test. It fails on the round 2 code.
- Finding 2 (P2): correct. `ObjStmCache::insert` updated `order_` and `bytes_` before the map insert, so a `bad_alloc` there left a phantom entry that a later eviction would dereference. Both allocations (reserving `order_`, inserting into the map) now happen before any bookkeeping changes, and eviction tolerates a missing entry. No allocation fault injector reaches the standard containers, so this is verified by reading the ordering and by `PdfXref.ObjStmCacheBoundsCountAndBytes` covering the normal path, not by an injected failure.
- Finding 3 (P2): correct, and separate from `D06 T05 §29`. Recovery recognized a catalog only by a direct `/Type /Catalog`. Dictionaries whose `/Type` is a reference are now remembered (and forgotten when their object is redefined, like the other recovery maps) and resolved after the scan, when every object is known; one naming `/Catalog` becomes a catalog candidate. `PdfXref.DamagedRecovery` adds a catalog with `/Type 4 0 R` where object 4 comes after it, and the root is recovered. It fails on the round 2 code.

Gates after round 3: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 123 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 34 tests.`
