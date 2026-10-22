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

## Round 4: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T11:46:46Z
- Effort: xhigh
- Lines: 26
- Digest: 112440092f80acd4

### adversarial: needs-attention

1. **P1, indirect catalog-type resolution bypasses recovery’s work budget.** `src/pdf/xref.cpp:1067` reparses each referenced object without charging `cumulative_parse_bytes` or reusing resolved values. Reproduce with a valid catalog, an uncompressed object containing an array of 100,000 integers, and 20,000 other dictionaries whose `/Type` references that array. Remove `startxref` and call `recover_damaged_xref`. The new pass parses approximately two billion array elements, despite the file passing the forward scan’s budget. This regresses the promised linear recovery scanner and hostile-input work bounds. No object-stream parameters are involved.

### consistency: needs-attention

2. **P3, the cache allocation-failure fix lacks the required regression test.** `tests/recto-tests/pdf_xref_tests.cpp:153` exercises successful cache operations; it cannot expose the bookkeeping corruption fixed in round 3. `docs/reviews/06-quality/audit-d01-t01-s6.md:154` explicitly records source inspection instead of fault injection. Reproduce the coverage gap by running `PdfXref.ObjStmCacheBoundsCountAndBytes` on the buggy parent: the unchanged test passes. The repository requires a regression test that fails before each code fix.

### integration: needs-attention

3. **P2, deferred catalog recognition loses revision order.** `src/pdf/xref.cpp:1076` appends indirect-type catalogs after every directly recognised catalog, using unordered-map iteration order. Fallback at line 1094 then prefers those appended candidates. Reproduce with an earlier catalog `1 0 obj << /Type 4 0 R /Pages 2 0 R >>`, object 4 containing `/Catalog`, and a later direct-type catalog pointing to a different page tree. Remove surviving trailers and `startxref`. Recovery selects the earlier catalog, whereas direct-type candidates preserve file order. Multiple indirect candidates also lack a defined chronological preference.

### source-defect: approve

No additional findings.

### design: advisory

Not applicable; no user surface.

### record: advisory

At clean `3912140`, the existing Release binary passed all 15 `PdfXref.*` tests, 19 document/crypto tests, and `Support.AsanActiveWhenEnabled`, which reported `ASan=enabled`. Graph validation and plan checks passed. Fresh compilation and custom reproductions were not performed; findings 1 and 3 are source-traced. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 4

- Finding 1 (P1): correct, introduced in round 3. The post-scan step resolved each dictionary's `/Type` reference separately, so 20,000 dictionaries pointing at one large array would reparse it 20,000 times. Each distinct reference is now resolved once (a memo keyed by the reference), so every object is parsed at most once more, as in the forward scan, keeping recovery's work linear. No test measures the work: a hostile-sized case would be slow or flaky in CI, so this rests on the memo's construction and is named here rather than claimed as tested.
- Finding 3 (P2): correct, also from round 3. Indirect-type candidates were appended after every direct one, so they won the fallback regardless of file order. They now join `catalog_history` at scan time, in file order, and the post-scan step only marks them valid. `PdfXref.DamagedRecovery` adds both orders: a later direct-type catalog beats an earlier indirect-type one, and the reverse. Dropping the scan-time entries fails it.
- Finding 2 (P3): correct, the round 3 fix had no test that fails before it. `ObjStmCache::insert` now has the same fault seam the crypto code uses (`detail::ScopedObjStmCacheAllocFaultInjector`, a check before each allocation). `PdfXref.ObjStmCacheBoundsCountAndBytes` fails each of the two allocations in turn and checks that the count and byte total still match what is stored, and that later inserts still evict. With the old order (bookkeeping before the second allocation) it fails.

Gates after round 4: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 123 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 34 tests.`

## Round 5: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T12:06:42Z
- Effort: xhigh
- Lines: 26
- Digest: dce6881622579b71

### adversarial: needs-attention

1. **P1, distinct aliases bypass recovery’s work bound.** [src/pdf/xref.cpp:1077](/R:/GitHub/Recto/src/pdf/xref.cpp:1077) memoizes only the initial `/Type` reference. The resolver at line 2153 follows aliases without memoizing their shared target. Reproduce with a valid catalog, one object containing 100,000 integers, 20,000 distinct uncompressed aliases to that array, and 20,000 dictionaries whose `/Type` references those aliases. Remove `startxref` and call `recover_damaged_xref`. Every alias misses the memo, causing approximately two billion array-element parses outside `cumulative_parse_bytes`. Recovery still permits hostile CPU amplification.

### consistency: needs-attention

2. **P3, the round 4 work-bound fix lacks its required regression test.** [docs/reviews/06-quality/audit-d01-t01-s6.md:196](/R:/GitHub/Recto/docs/reviews/06-quality/audit-d01-t01-s6.md:196) explicitly substitutes source inspection for testing. The added `DamagedRecovery` cases assert catalog selection, so removing the memo while preserving file-order handling leaves those assertions satisfied. This violates `AGENTS.md`’s regression-test requirement. A deterministic resolution-count test could verify reuse without a large timed workload.

### integration: needs-attention

3. **P2, recovery loses encrypted compressed objects despite an intact xref stream.** [src/pdf/xref.cpp:881](/R:/GitHub/Recto/src/pdf/xref.cpp:881) copies only an encountered xref stream’s trailer keys, discarding its type-2 entries. Line 892 instead attempts to read encrypted object-stream payloads before authentication, which happens later at [src/pdf/document.cpp:1319](/R:/GitHub/Recto/src/pdf/document.cpp:1319). Reproduce using the PDF constructed by `PdfDocument.EncryptedObjectStreamsShareOneDecodeAndRefuseForbiddenMembers`: corrupt only its final `startxref` offset, open with the correct password, then resolve object 21. Recovery cannot reconstruct that compressed entry, so resolution returns null instead of `7`. All object-stream parameters are direct; this is separate from D06 T05 §29. Encrypted streams require decryption before decoding. [PDF specification errata, §7.6.3](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html#7.6.3)

### source-defect: approve

No additional findings.

### design: advisory

Not applicable; no user surface.

### record: advisory

At clean `f7d26b5`, the existing Release binary passed all 15 `PdfXref.*` tests, 19 document/crypto tests, and the ASan activation check (`ASan=enabled`). Graph validation and the plan check passed. Fresh compilation was not performed. Findings 1 and 3 are source-traced reproductions, not executed custom probes. No files were modified.

HIGHEST: P1  
OVERALL: needs-attention

## Fixes after round 5

- Finding 1 (P1): correct. The memo keyed the first reference, but the lookup followed alias chains, so 20,000 distinct aliases of one large array each reparsed it. Recovery now reads the referenced object one level (`read_indirect_object`, never following a further reference), the same rule every indirect value in this engine follows. With the memo, every object is read at most once, so the work stays linear. An alias therefore names no catalog, a recorded default with the same one-level rule as `/Filter`, `/DecodeParms`, `/N`, `/First` and `/Type` elsewhere.
- Finding 2 (P3): correct. The work bound is now testable. `XrefTable::recovery_type_lookups()` counts the objects recovery reads for this purpose, and `PdfXref.DamagedRecovery` asserts one lookup for the indirect-type catalog and two for three dictionaries sharing a `/Type` object plus one alias of it. With the memo bypassed it fails (4 lookups, not 2).
- Finding 3 (P2): filed, not fixed here. Recovery drops an intact xref stream's type-2 rows and decodes containers before authentication, which in an encrypted file is ciphertext. Fixing it means importing xref-stream rows under the scan's override order and deferring encrypted containers until after authentication, a recovery design change like `D06 T05 §29`. It is filed with an owner as `D06 T05 §30` (`Damaged-file recovery keeps an encrypted file's compressed objects`), and `D06 T05 §21` carries the deferral.

Gates after round 5: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 123 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 34 tests.`; `python scripts/todo-graph.py validate` -> `valid: 683 sections`.

## Round 6: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T12:26:10Z
- Effort: xhigh
- Lines: 32
- Digest: 609f0700d94da1d2

### adversarial: needs-attention

1. **P1, recovery’s second pass still permits quadratic parsing.** At `src/pdf/xref.cpp:1078`, reading a `/Type` target can recursively resolve a stream’s `/Length` through `src/pdf/xref.cpp:2054`. Those additional reads bypass both `names_catalog` and `cumulative_parse_bytes`; `recovery_type_lookups` counts only the outer calls.

   Reproduce with a valid catalog, object 4 containing an array of 100,000 integers, 20,000 distinct small streams each declaring `/Length 4 0 R`, and 20,000 dictionaries whose `/Type` references those streams. Remove `startxref` and call `recover_damaged_xref`. The forward scan uses its integer lookup and terminator fallback. The second pass reparses object 4 for every stream, approximately two billion array elements, without exhausting the recovery work budget. This requires neither object streams nor encryption, so §29 and §30 do not cover it.

### consistency: approve

No additional findings.

### integration: approve

No additional findings.

### source-defect: approve

No additional findings.

### design: advisory

Not applicable; no user surface.

### record: needs-attention

2. **P3, the round 5 test does not detect restored alias following.** At `tests/recto-tests/pdf_xref_tests.cpp:1570`, object 8 is an array and object 9 aliases it. Following that alias or stopping after one level produces the same catalog decision. The counter at `src/pdf/xref.cpp:1077` also remains two because it counts outer calls.

   Reproduce by restoring the previous `resolve_object` lookup while retaining the counter: these assertions remain satisfied. A distinguishing fixture would make object 8 `/Catalog` and assert that the later dictionary referencing alias 9 is excluded. The current test verifies memo reuse but misses the alias-following regression, contrary to the required regression-test rule.

At clean `7b3f62d`, the existing Release binary passed 15 `PdfXref.*` tests, 19 document/crypto tests, and `Support.AsanActiveWhenEnabled` reported `ASan=enabled`. Graph validation and the plan check passed. No fresh compilation was performed. Both reproductions above are source-traced, not executed custom probes. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 6

- Finding 1 (P1): correct, and the third work-bound finding against the indirect-`/Type` support I added in round 3. Re-reading objects after the scan was the wrong design: any read can reach further reads (a stream's `/Length` here). The step now reads nothing. The forward scan, which already parses every object once, records which objects are the name `/Catalog` (`catalog_names`, forgotten on redefinition like the other recovery maps), exactly as `recovered_integers` already serves indirect `/Length`. The post-scan step only consults that record, so the bound holds by construction, not by memo. Round 5's lookup counter measured reads that no longer exist and is removed with its assertions.
- Finding 2 (P3): correct. `PdfXref.DamagedRecovery` now distinguishes one level from alias following: object 8 is `/Catalog`, object 9 is `8 0 R`, and a later dictionary with `/Type 9 0 R` must not become the root (the earlier direct catalog stays). Round 4's alias-following code fails it (root 13 instead of 1). A second case redefines the `/Catalog` name object later as `/Pages`, and recovery then finds no catalog.

Gates after round 6: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 123 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 34 tests.`

## Round 7: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T12:45:57Z
- Effort: xhigh
- Lines: 30
- Digest: 14d3a7d0477e453b

### adversarial: needs-attention

1. **P1, failed decompressions bypass recovery’s cumulative decoded-byte limit.** At `src/pdf/xref.cpp:900`, recovery decodes each container with the full per-stream ceiling. Line 906 charges the cumulative budget only on success. A checksum failure occurs after inflation at `src/pdf/filters.cpp:980`, then recovery skips it without charging that work.

   Reproduce with an intact uncompressed catalog, missing `startxref`, and 1,024 `/ObjStm` containers using `/N 1 /First 4 /Filter /FlateDecode`. Compress `b"1 0 42 "` padded to 16 MiB with spaces, then flip the last Adler-32 byte. Each container inflates before returning `DamagedFormat`. Recovery processes approximately 16 GiB of expanded data from 16 MiB of encoded payload while its cumulative decoded-byte counter remains zero. This defeats the hostile-input resource guard and is separate from §29 and §30.

### consistency: approve

No additional findings.

### integration: approve

No additional findings.

### source-defect: approve

No additional findings.

### design: advisory

Not applicable; no user surface.

### record: needs-attention

2. **P3, round 6’s work-bound fix lacks a distinguishing regression test.** The cases at `tests/recto-tests/pdf_xref_tests.cpp:1553` test catalog recognition, aliases and redefinition. They do not detect secondary parsing through a stream-valued `/Type`. Restoring `7b3f62d`’s post-scan `read_indirect_object` loop, without its obsolete counter, preserves these assertions while restoring round 6’s expensive `/Length` reproduction. The required before-failing regression for that fix is absent.

At clean `1cbd5ae`, the existing Release binary passed 35 tests covering `PdfXref.*`, `PdfDocument.*`, `PdfCrypto.*` and ASan activation (`ASan=enabled`). Graph validation and the plan check passed. No rebuild or custom Recto probe was performed; both findings are source-traced. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 7

- Finding 1 (P1): correct. Recovery charged its cumulative decoded `/ObjStm` budget only for containers that decoded, so a container that inflated and then failed its checksum cost nothing against the budget, and 1,024 of them expanded 16 GiB. Decoding work is now measured: `detail::ScopedDecodeWorkMeter` (`src/pdf/filters.h`) counts the output capacity every expanding decoder reserves (all seven output reservations in `filters.cpp` go through `reserve_metered`, including `ensure_bounded_capacity`'s growth), whether the decode then succeeds or fails. Recovery charges each container `max(measured work, decoded size)` and fails with `DecompressionLimitExceeded` past the budget, as it already did for successful decodes. `PdfXref.RecoveryChargesFailedObjStmDecodes` runs a 16-entry recovery (1 MiB budget) over three Flate containers that each inflate about 400 KiB and fail their checksum. Recovery is refused, while the same file without them recovers, and the meter counts a failed decode's work. The test fails on the round 6 code.
- Finding 2 (P3): correct. Round 6's "no post-scan reads" had no test that fails if reads return, because the reads would produce the same answer. Work that produces the same answer is invisible at run time, so the guard is on the source, as for §15's ETW registration: `tests/scripts/test_recovery_post_scan.py` asserts the post-scan catalog step calls no reader, parser, decoder or `file_bytes` and consults `catalog_names`. It fails on `7b3f62d` (`read_indirect_object`, `file_bytes`), and `plan-gates` runs it.

Gates after round 7: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 124 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*:PdfFilters.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 52 tests.`; `python -m unittest tests.scripts.test_recovery_post_scan` -> `OK`.

## Round 8: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-22T13:08:09Z
- Effort: xhigh
- Lines: 28
- Digest: 445177a4a3f9dd2f

### adversarial: needs-attention

1. **P1: Failed JBIG2 decodes still bypass recovery’s cumulative work budget.** `src/pdf/filters.cpp:2453` allocates bitmap output with `data.assign`, bypassing `reserve_metered`. Recovery at `src/pdf/xref.cpp:909` consequently charges zero when that decoder subsequently fails.

   Reproduce with an intact uncompressed catalog, missing `startxref`, and three `/ObjStm` containers using `/N 1 /First 4 /Filter /JBIG2Decode /Length 31`. Each payload is a JBIG2 Page Information segment declaring a 2048 × 2048 page, followed by one byte that triggers “Truncated JBIG2 segment header”:

   ```python
   struct.pack(">IBBBI", 1, 48, 0, 1, 19) + struct.pack(">IIIIBH", 2048, 2048, 0, 0, 0, 0) + b"\0"
   ```

   Each attempt allocates and initializes 512 KiB before returning `DamagedFormat`. `recover_damaged_xref(bytes, 16)` therefore succeeds after 1.5 MiB of output allocation despite its 1 MiB cumulative budget. Scaling to 1,024 containers with 8192 × 8192 pages causes 8 GiB of allocation work while charging zero. This is separate from §29 and §30. The reproduction is source-traced, not executed.

### consistency: approve

### integration: approve

### source-defect: approve

### design: advisory

Not applicable; no user surface.

### record: advisory

The existing Release binary passed all 16 `PdfXref.*` tests plus ASan activation (`ASan=enabled`). Graph validation, the generated-plan check, and the post-scan source guard passed. No fresh compilation was performed, so this verifies the available binary rather than independently establishing a build from `6b0bcbe`. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 8

- Finding 1 (P1): correct. The meter covered every decoder's output reservation, but the image decoders allocate their raster buffers elsewhere (JBIG2's page bitmap here), so a container behind an image filter could fail with its work uncharged. Metering every image buffer would chase allocation sites one round at a time. The rule instead closes the class: an object stream holds PDF syntax, which only the general-purpose lossless filters produce, so recovery (a speculative path) decodes a container only when `filters_are_metered_lossless` holds for its whole filter chain (`FlateDecode`, `LZWDecode`, `ASCIIHexDecode`, `ASCII85Decode`, `RunLengthDecode` and their abbreviations, all metered). Any other container is recorded as `DamagedStream` damage and left undecoded. Normal resolution is unchanged. JBIG2's page bitmap now also counts on the meter, which is what lets the test see an attempted decode. `PdfXref.RecoveryChargesFailedObjStmDecodes` adds three JBIG2 containers declaring 2048 x 2048 pages: recovery skips them, the catalog recovers, and the meter stays at zero. With the round 7 recovery it fails (the attempted decodes exhaust the budget). `filters_are_metered_lossless` is checked for an abbreviated chain, an image filter and no filter.

Gates after round 8: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 124 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfXref.*:PdfDocument.*:PdfCrypto.*:PdfFilters.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 52 tests.`
