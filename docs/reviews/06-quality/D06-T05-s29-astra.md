## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T16:01:56Z
- Effort: high
- Lenses: adversarial, integration, source-defect, plan-delivery
- Lines: 38
- Digest: cfdaf5cf0ac1f942

All reproductions below are source-derived, not executed. No files were modified.

## adversarial: needs-attention

1. **[P1] References inside an indirect parameter array escape candidate discovery.** `src/pdf/xref.cpp:1820` collects references from the container dictionary but never expands referenced objects; the candidate resolver at `src/pdf/xref.cpp:1873` returns null for their dependencies.

   Reproduce by adapting `RecoveryRoundThreeFixes`: define object 2 as `[3 0 R]`, object 3 as `/ASCIIHexDecode`, and container 20 with `/Filter 2 0 R /N 1 /First 5`, containing integer 88 equal to the complete payload length of stream 30. Stream 30 uses `/Length 88 0 R`; its payload starts with `endstream/endobj`, redefines object 3 as zero, and replaces catalog 1.

   Normal deferred decoding sees the poisoned object 3. Candidate decoding discovers only reference 2, so resolving array element 3 returns null. Neither path extracts integer 88, and recovery retains the embedded catalog. Opening this PDF through `Document::open_memory`, with genuine and embedded catalogs pointing to different valid page trees, displays the embedded catalog’s pages.

2. **[P1] Integer-candidate deduplication permits quadratic CPU consumption.** `src/pdf/xref.cpp:1007` linearly searches every previously collected value for the same object number and generation.

   Reproduce through `Document::open_memory` with missing cross-reference data, an intact catalog/page tree, and 900,000 definitions of `88 0 obj`, containing successive integers from 0 through 899,999. This remains below the default million-entry limit and requires no streams. Deduplication performs **404,999,550,000 integer comparisons**. Neither the parse-byte budget nor the decode budget charges this work, and the loop has no cancellation check. A crafted file can monopolize the document thread.

## integration: needs-attention

3. **[P1] An accepted stale length can swallow later document objects permanently.** `src/pdf/xref.cpp:1299` accepts the currently scanned integer, but `src/pdf/xref.cpp:1933` considers only candidates longer than that initial extent. The consistency check at `src/pdf/xref.cpp:1976` excludes the first pass.

   Reproduce by adapting the obsolete-length fixture at `tests/recto-tests/pdf_xref_tests.cpp:1091`: replace its second uncompressed `88 0 obj 1 endobj` with an ASCIIHex object stream containing `88 0 1`. Keep the earlier oversized integer and calculate it to land on stream 31’s terminator, as the fixture already does.

   Container unpacking is deferred, so stream 30 initially uses the obsolete large length and swallows the replacement catalog. Final unpacking correctly restores integer 88 to one, but that shorter candidate is discarded and the first-pass table is returned. Through `Document::open_memory`, give the replacement catalog a different valid page tree: recovery selects the obsolete document contents.

4. **[P2] Failure to find a catalog in the provisional scan prevents an otherwise repairable rescan.** `src/pdf/xref.cpp:714` immediately returns a failed first pass, including the root-validation failure at `src/pdf/xref.cpp:2041`, even when that pass populated a usable length plan.

   Reproduce using the fake-payload fixture at `tests/recto-tests/pdf_xref_tests.cpp:689`, replacing the embedded catalog definition with `1 0 obj null endobj` and recomputing the length stored in container 20. The first scan imports that null and loses its only catalog. Deferred unpacking discovers the correct length and constructs the correction plan, but root validation fails before the wrapper can use it. Opening the damaged PDF fails despite an intact catalog and sufficient information to exclude the payload redefinition.

## source-defect: needs-attention

1. **[P1] Findings 1 and 3 violate stream extent semantics.** At `src/pdf/xref.cpp:1873` and `src/pdf/xref.cpp:1933`, recovery respectively promotes payload into document structure or consumes genuine following objects as payload. The reproductions above demonstrate both directions. `/Length` specifies the stream’s byte extent; recognizing a terminator elsewhere does not establish that extent. See [ISO 32000-1, §7.3.8.2](https://udp.adobe.io/document-services/docs/assets/35e4369068f86065372c18787171a17e/PDF_ISO_32000-1.pdf).

## plan-delivery: needs-attention

1. **[P1] The promised parameter resolution and definition precedence remain incomplete.** Findings 1 and 3 reproduce failures at `src/pdf/xref.cpp:1873` and `src/pdf/xref.cpp:1933`: a supported indirect filter array cannot supply length candidates, and a later compressed length definition cannot correct an earlier accepted extent. Finding 4 additionally prevents recovery when the provisional scan loses the catalog.

Verification limitation: the available Release executable passed nine tests with `--gtest_filter=PdfXref.Recovery*:PdfDocument.*Recover*`, but its output lacks the newly added recovery tests. Those results do not verify this exact candidate; no fresh build or candidate ASan run was performed.

HIGHEST: P1
OVERALL: needs-attention

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T16:11:17Z
- Effort: medium
- Lenses: adversarial
- Lines: 17
- Digest: bc4d9b5b8de2864f

## adversarial: needs-attention

1. **[P1] The 16-reading limit can silently preserve a payload-injected catalog.** `src/pdf/xref.cpp:1907` stops candidate discovery after the first 16 definition combinations, even when none recovered the stream’s length. Recovery then returns the contaminated first-pass table.

   Reproduce by adapting the round-12 `/N` fixture in `PdfXref.RecoveryRoundThreeFixes`:
   - Before its existing `2 0 obj 1 endobj`, insert 16 definitions of object 2 containing zero.
   - Keep container 20’s `/N 2 0 R`, with compressed integer 88 holding `fake.size()`.
   - Keep stream 30’s `/Length 88 0 R` and payload containing an early `endstream/endobj`, another zero definition of object 2, and the fake catalog.

   Normal deferred unpacking sees the payload’s `/N 0`. The alternative readings consume all 16 attempts on the earlier zero definitions; the genuine `/N 1` is never tried. Integer 88 is never recovered, no corrective rescan occurs, and object 1 points into stream 30’s payload.

   This requires only opening a crafted PDF without `startxref` through `Document::open_memory`, which enables recovery at `src/pdf/document.cpp:1300`. Give the genuine and injected catalogs different valid page trees to expose the wrong document contents. Keep the work bound, but do not accept provisional structure as trustworthy when candidate exploration is incomplete.

Source-traced reproduction, not executed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T16:18:00Z
- Effort: low
- Lenses: adversarial
- Lines: 14
- Digest: 4504db541be78cd8

## adversarial: needs-attention

1. **[P1] A stale integer length bypasses the incomplete-reading safeguard.** `src/pdf/xref.cpp:1978` rejects incomplete candidate exploration only when `sized_by` is absent. An earlier integer can match an embedded terminator while the actual length remains hidden in an unexplored container reading.

   Reproduce using the 16-decoy fixture at `tests/recto-tests/pdf_xref_tests.cpp:475`: insert `88 0 obj\n0\nendobj\n` before container 20. Keep its compressed replacement for object 88, the 16 zero definitions of object 2, and the payload containing the fake catalog.

   Stream 30 accepts the obsolete zero length, so `sized_by` is populated. Deferred decoding sees the payload’s `/N 0`; the 16 alternative readings never reach `/N 1`. Consequently, no longer length is discovered, the new safeguard does not fire, and recovery returns the payload-injected catalog.

   Open this crafted PDF through `Document::open_memory`, without `startxref`, with genuine and injected catalogs pointing to different valid page trees outside the stream. Recovery selects the injected document contents. An accepted provisional integer must not establish trust when exploration of its replacement remains incomplete.

Source-traced reproduction, not executed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T16:23:03Z
- Effort: medium
- Lenses: adversarial
- Lines: 18
- Digest: 25441905947e798c

## adversarial: needs-attention

1. **[P1] Correcting one stream bypasses the incomplete-reading safeguard for another.** `src/pdf/xref.cpp:1807` disables candidate exploration and its ambiguity check on every rescan. The consistency check at `src/pdf/xref.cpp:2059` checks only streams supplied by the oracle, leaving other streams unchecked.

   Reproduce from `PdfXref.RecoveryRefusesWhenReadingsAreCut`, using its 16-decoy case. Before stream 30, add an independent stream 40 with `/Length 89 0 R`, payload `\nendstream\nendobj\n%padding\n`, and its real terminator. Define integer object 89 after stream 40 as the complete payload length. This supplies a valid correction plan for stream 40 while container 20’s genuine length remains undiscovered.

   The first pass rejects the ambiguous file, but the wrapper retries because the plan is nonempty. The rescan corrects stream 40, still scans stream 30’s injected catalog, skips the incomplete-reading safeguard, and passes consistency because object 89 matches. Opening this PDF through `Document::open_memory`, with genuine and injected catalogs naming different valid page trees, selects the injected document contents.

2. **[P1] Candidate discovery still misses supported nested predictor references.** `src/pdf/xref.cpp:1866` expands only the initially named references. References discovered during that expansion are never expanded themselves, yet the decoder resolves parameter-array elements and their dictionary values.

   Reproduce with container 20 holding integer 88, the complete length of stream 30, using `/Filter /FlateDecode /DecodeParms 2 0 R`. Define object 2 as `[3 0 R]`, object 3 as `<< /Predictor 12 /Columns 5 0 R >>`, and object 5 as the correct positive column count. Encode the container with that predictor. Stream 30 uses `/Length 88 0 R`; after an embedded `endstream/endobj`, its payload redefines object 5 as zero and replaces catalog 1.

   Normal decoding sees the poisoned column count. Candidate discovery finds objects 2 and 3 but never object 5, so its resolver returns null for `/Columns` and extracts no length. Exploration nevertheless counts as complete, allowing the injected catalog to survive. Opening the crafted PDF without `startxref` through `Document::open_memory` exposes the substituted pages.

Both reproductions are source-traced, not executed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention
