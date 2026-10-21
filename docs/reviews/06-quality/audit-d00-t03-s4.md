# Audit - D00 T03 §4: Encrypted, broken and seeds corpus fixtures

- **Audit section:** `D06 T05 §12` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T03 §4` (`docs/reviews/d00-t03-s4.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `1cf1f0b`: `python scripts/build-corpus.py --check` -> exit 0, `verified: 39 corpus fixtures in R:\GitHub\Recto\tests\corpus are current`. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:14:13Z
- Effort: medium
- Lines: 30
- Digest: ca28569206ba9240

### adversarial: needs-attention

1. **P1: Encryption validation accepts a dictionary the PDF does not reference.** `scripts/build-corpus.py:2680` hardcodes object 7 instead of resolving the trailer’s `/Encrypt`. At `:2741`, decryption follows `/R` without checking the crypt-filter declaration. Reproduced in memory: replace `/Encrypt 7 0 R` with `/Encrypt 6 0 R` in the AES-128 fixture, update the simulated generator output and provenance, then call `check_corpus()`. Result: **`(True, [])`**, although object 6 is metadata. Replacing `/CFM /AESV2` with `/CFM /V2   ` also passes the encryption validator. The byte comparison catches standalone file corruption, but cannot catch these generator regressions.

2. **P1: The hostile-stream checker allocates without an output bound.** `scripts/build-corpus.py:2831` and `:2835` decompress without limiting output; the size check happens afterward. Reproduced with the committed bomb: the first inner decompression call allocates **269,703,656 bytes**, already exceeding the **268,435,456-byte** limit. Changed input can amplify this further before the byte-mismatch check at `:3171`. Chunking compressed input does not bound decompressed allocation.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the fixture gate supporting downstream crypto tests: regenerated files can pass verification while their declared encryption configuration is unusable. This audit does not establish a downstream engine defect.

### source-defect: needs-attention

3. **P3: The permission-mask comment incorrectly claims assembly is allowed.** `scripts/build-corpus.py:34` lists “assemble” among permissions granted by `-1028`. Reproduce with `bool((-1028 & 0xffffffff) & (1 << 10))`: **`False`**. Bit 11 controls document assembly for these revisions. [QPDF encryption documentation](https://github.com/qpdf/qpdf/blob/main/manual/encryption.rst)

### design: advisory

Not applicable; this section owns no user surface.

### record: needs-attention

4. **P3: The review overstates independent decoder verification.** `docs/reviews/00-workspace/D00-T03-s4.md:11` claims round-trip decoder verification for all 11 filter seeds. Inspect `scripts/build-corpus.py:2930`: CCITT and JBIG2 are compared with generator output; DCT and JPX only receive marker checks. `tests/scripts/test_build_corpus.py:509` calls that same validator. Those checks do not prove the recorded round trips.

The checkpoint was independently rerun at `e2ee5ef`: exit 0, **39 fixtures verified**. The section-specific Python test also passed. Neither detects findings 1 or 2. No files were modified by this audit.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (the two new tests fail against `e2ee5ef`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): `validate_encrypted_pdf_structure` reads everything from parsed objects: the trailer's own `/Encrypt`, `/ID` and `/Root`, the dictionary `/Encrypt` names, and the page content `/Root` reaches through `/Pages` and `/Kids`; it also runs `validate_layout`. The crypt filters declared must be the ones the revision's decryption applies: none for RC4 (V 2, R 3), `/StdCF` as both `/StmF` and `/StrF` with `/CFM /AESV2` (V 4, R 4) or `/AESV3` (V 5, R 6). `/Encrypt 6 0 R` and `/CFM /V2` now refuse (`test_encryption_is_read_from_what_the_trailer_names`).
- Finding 2 (P1): the deflate-bomb check inflates through `inflate_bounded`, 1 MiB of output at a time, stopping just past its limit (16 MiB for the outer stage, 256 MiB counted but not kept for the inner), so it never allocates what it is measuring (`test_inflation_stops_just_past_its_limit`).
- Finding 3 (P3): the permission-mask comment now says `-1028` grants every permission but bit 11, document assembly.
- Finding 4 (P3): a dated audit note in `D00 T03 §4` records that the earlier review overstated decoder verification (CCITT and JBIG2 compared with the generator's own output, DCT and JPX marker checks only).

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:22:35Z
- Effort: medium
- Lines: 33
- Digest: 47665dcc6f32c08f

### adversarial: needs-attention

1. **P1: Encryption validation still ignores parameters that determine authentication.** `scripts/build-corpus.py:2713` reads `/V`, `/R` and `/P` but ignores `/Length` and `/EncryptMetadata`; `:2740` always derives the fixed 128-bit key, and `:2770` checks `/Perms` against literal `T` instead of the declared metadata setting.

   Reproduced without writing files:
   - Replace `/Length 128` with `/Length 040` in `render/encrypted-rc4-128.pdf`. `validate_encrypted_pdf_structure()` accepts it although authentication requires the declared 40-bit key derivation.
   - Intercept `ClassicPdfBuilder.set_obj()` for object 7 and replace `/EncryptMetadata true` with `/EncryptMetadata false` when generating either AES fixture. Both validators accept the result. AES-128 now declares a different key derivation; AES-256 contradicts its encrypted `/Perms` flag.
   - Supply these mutated bytes through mocked `expected_corpus_files()` and `Path.read_bytes()`, with matching regenerated provenance. **`check_corpus()` returns `(True, [])`.**

   Standalone corruption is caught by byte comparison, but a generator regression producing these invalid encryption declarations passes the complete gate.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the fixture gate supporting downstream authentication tests. This audit does not establish a defect in the current committed encrypted fixtures or the C++ engine.

### source-defect: needs-attention

Finding 1 reflects incomplete application of the security-handler algorithms: revision 3 key derivation depends on `/Length`, and revision 4 additionally depends on `/EncryptMetadata`. These inputs are explicit in the [reference implementation of Algorithm 2](https://pdfnaut.readthedocs.io/en/latest/_modules/pdfnaut/security/standard_handler.html).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `0f0e935`, the checkpoint independently exited 0 with **39 fixtures verified**. The section-specific fixture test and both round 1 regression tests passed: **3 tests, OK**. Those checks do not detect finding 1. No files were modified by this audit.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (the new subtests fail against `0f0e935`, each with a length-preserving mutation so the layout check cannot be what catches it); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): each revision the corpus writes now has exactly one form of `/Encrypt` dictionary: its exact key set, `/Length` equal to the key the decryption derives (128 for RC4 and AES-128, 256 for AES-256), `/EncryptMetadata true` where the revision has it (Algorithm 2 step f and the `/Perms` flag both assume it), and a `/CF` holding only `/StdCF` with exactly `/Type /CryptFilter`, the revision's `/CFM`, `/AuthEvent /DocOpen` and its `/Length` (`test_rc4_and_aes_256_declare_the_key_they_derive`, and the extended `test_encryption_is_read_from_what_the_trailer_names`).

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:30:59Z
- Effort: medium
- Lines: 30
- Digest: 051938d30bcc2326

### adversarial: needs-attention

1. **P1: Encrypted-stream validation ignores the declared decoding filter.** `scripts/build-corpus.py:2815` always applies `zlib.decompress()` without checking the content stream’s `/Filter`. Reproduce in memory using `render/encrypted-rc4-128.pdf`: replace `b"/FlateDecode"` with `b"/DCTDecode  "`. The length-preserving mutation passes `validate_encrypted_pdf_structure()`, although the decrypted payload is Flate data, not JPEG data.

2. **P1: Encrypted PDFs can pass with an invalid document catalog.** `scripts/build-corpus.py:2798` follows `/Root` to `/Pages` without verifying the root dictionary’s `/Type`. Replace `b"/Type /Catalog"` with `b"/Type /INVALID"` in the same fixture. The length-preserving mutation passes validation.

For **each** reproduction, I also mocked `expected_corpus_files()` and `Path.read_bytes()` with the mutated fixture and matching regenerated provenance. The complete `check_corpus()` returned **`(True, [])`**. Standalone corruption is caught by byte comparison, but these generator regressions pass the gate.

### consistency: approve

No separate finding.

### integration: advisory

Findings 1 and 2 weaken the fixture gate relied on by downstream encrypted-document tests. These reproductions establish validator defects, not defects in the committed fixtures or C++ engine.

### source-defect: needs-attention

Finding 1 contradicts the stream-decoding contract: the stream dictionary specifies which filters must process its data. Hardcoding Flate decoding verifies a different interpretation from the one declared by the PDF. See [ISO 32000 clause 7.4.1 corrections](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `4a8ce84`, the checkpoint independently exited 0 with **39 fixtures verified**. The section-specific fixture test and three encryption/inflation regression tests passed: **4 tests, OK**. These checks do not detect findings 1 and 2. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 35 tests`, `OK` (`test_the_objects_around_the_encryption_have_one_form` fails against `4a8ce84` for each length-preserving mutation); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Findings 1 and 2 (P1): instead of checking one more entry, the objects around the encryption now have one exact form each, as the generator writes them: the trailer's key set with `/Root 1 0 R` and `/Info 6 0 R`, the catalog, page tree, page and font dictionaries compared whole, objects 1 to 7 with object 4 the only stream, its dictionary exactly `/Filter /FlateDecode` with its own `/Length`, and `/Info` exactly `/Title`, `/Author`, `/CreationDate` as hex strings. `/DCTDecode` in place of `/FlateDecode`, `/Type /INVALID` in the catalog and a changed font each refuse.

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:38:22Z
- Effort: medium
- Lines: 32
- Digest: 5871e6f50ca75f38

### adversarial: needs-attention

1. **P1: Invalid encrypted metadata passes validation.** `scripts/build-corpus.py:2823` checks `/Info` values only for hexadecimal syntax, without decrypting them. Reproduced by replacing every hex digit of `/Title` in `render/encrypted-aes-256.pdf` with `0`, preserving its length. `validate_encrypted_pdf_structure()` accepts it, while decrypting that title with the fixture’s file key raises `invalid PKCS#7 padding in AES-CBC payload`.

2. **P1: The validator does not enforce the encryption revision promised by each filename.** `scripts/build-corpus.py:2728` accepts any supported `(V, R)` pair regardless of `rel_path`. Reproduce by passing the committed RC4 fixture bytes as `render/encrypted-aes-256.pdf`. Validation succeeds despite the section explicitly requiring `V=5, R=6` at that path. A generator regression can therefore remove AES-256 coverage while the checkpoint remains green.

3. **P1: The recoverable fixture check accepts a document without a catalog.** `scripts/build-corpus.py:2881` checks object numbers and the content stream, but does not establish that the surviving objects form a recoverable document. Reproduce in `broken/corrupt-xref-recoverable.pdf` by replacing `b"<< /Type /Catalog /Pages 2 0 R >>"` with `b"null"`. Validation succeeds even though `/Root 1 0 R` now resolves to `null` and no catalog survives recovery.

For each reproduction, I also mocked `expected_corpus_files()` and `Path.read_bytes()` with the mutated fixture and matching regenerated provenance. The complete `check_corpus()` returned **`(True, [])`**. These demonstrate generator-regression gaps; ordinary file corruption still fails the byte comparison.

### consistency: approve

No separate finding.

### integration: advisory

Findings 1 and 2 undermine the fixture checkpoint used by downstream crypto tests. `tests/recto-tests/pdf_crypto_tests.cpp:165` independently specifies the expected revisions and metadata, so the downstream suite has stronger expectations than this checkpoint. Finding 3 undermines the recoverability prerequisite of `D01 T01 §6`. No C++ engine defect is established by these reproductions.

### source-defect: needs-attention

Finding 1 applies encryption validation incompletely: encrypted PDF strings require decryption as well as hexadecimal decoding. AES strings require CBC padding validation, just like streams. See [PDF 1.7 specification, section 3.5.1](https://zxyle.github.io/PDF-Explained/resources/pdf_reference_1.7.pdf). The current validator proves only the content stream decrypts successfully.

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `e347776`, the checkpoint independently exited **0**, reporting **39 fixtures verified**. Four focused tests covering the section fixtures, encryption declarations, surrounding objects and bounded inflation passed. These results do not detect findings 1 to 3. No files were modified by this audit.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 4

`python -m unittest discover -s tests/scripts` -> `Ran 322 tests`, `OK` (the two new tests fail against `e347776`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): every `/Info` string is decrypted with the same per-object rule as the stream (AES padding checked, an AES payload must be an IV and whole blocks) and must equal what the generator wrote: `Recto Encrypted <mode>`, `Recto Corpus Generator` and the fixed creation date.
- Finding 2 (P1): `ENCRYPTED_REVISIONS` binds each encrypted fixture's path to the `(/V, /R)` it promises (and the mode its title names); the RC4 file passed as `encrypted-aes-256.pdf` refuses.
- Finding 3 (P1): the recoverable fixture is checked as the one-page document it is: objects 1 to 6 back to back from the header, the shared exact forms (`one_page_objects`, also used by the encrypted fixtures), the exact `/Info`, and a tail that is exactly the deliberately broken xref and a trailer naming `/Root 1 0 R` and `/Info 6 0 R`. A `null` catalog refuses (`test_the_recoverable_fixture_is_the_one_page_document_exactly`).

## Round 5: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:50:19Z
- Effort: medium
- Lines: 32
- Digest: adc3862a7bd1f051

### adversarial: needs-attention

1. **P1: Encrypted-fixture validation ignores cross-reference liveness and size.** `scripts/build-corpus.py:2703` discards the cross-reference map and sizes collected by `validate_classic_xref_at()`. At `:2709`, it instead locates objects by scanning their bodies. The coverage checks used for ordinary PDFs at `:1624` are absent here.

   Reproduced without writing files using `render/encrypted-rc4-128.pdf`:
   - Replace object 1’s 20-byte xref entry with `b"0000000000 65535 f \n"`, marking the catalog free. `validate_encrypted_pdf_structure()` accepts it.
   - Independently replace `/Size 8` with `/Size 1`. Validation also accepts it.

   With the first mutation supplied through mocked generator output and file reads, plus matching regenerated provenance, **`check_corpus()` returns `(True, [])`**. Thus a generator regression can pass the complete gate with an unusable catalog reference. Ordinary on-disk corruption remains caught by byte comparison.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the fixture gate supporting `D01 T01 §7` authentication tests and downstream encrypted-document CLI tests. This establishes a validator defect, not a defect in the committed encrypted PDFs or engine.

### source-defect: needs-attention

Finding 1 bypasses the cross-reference interpretation of indirect objects. `/Size` must also equal one greater than the highest object number; accepting `/Size 1` for objects 1 through 7 contradicts that requirement. See [ISO 32000-2 clauses 7.5.4 and 7.5.5 corrections](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html).

### design: advisory

Not applicable; no user surface.

### record: approve

Independently reran the checkpoint at `fae3dd7`: exit **0**, **39 fixtures verified**. Three focused tests covering section fixtures and the round 4 fixes passed. These checks do not detect finding 1. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 5

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (`test_encrypted_fixtures_get_the_full_cross_reference_checks` fails against `fae3dd7` for both mutations); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): the encrypted validator now runs the same `validate_pdf_structure` as every other fixture, in an `encrypted` mode that leaves stream content (ciphertext) to the decryption that follows. Object walk, xref coverage and liveness, `/Size` equal to the highest object plus one, the catalog and the layout all apply: a freed catalog entry and `/Size 1` each refuse.

## Round 6: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:56:30Z
- Effort: medium
- Lines: 32
- Digest: d1d3bebf004b71c2

### adversarial: needs-attention

1. **P1: Encrypted-stream validation still permits unbounded decompression.** `scripts/build-corpus.py:2866` calls `zlib.decompress()` without an output ceiling. Validation runs before the byte comparison at `:3304`, so corrupted on-disk input reaches this allocation even without matching provenance. The bounded helper added after round 1 protects only the dedicated bomb path.

   Reproduced in memory: patch `flate_compress()` to return `zlib.compress(b" " * 1048576)`, generate the RC4 fixture, then validate it. The **2,177-byte PDF passes**, allocating **1,048,576 bytes** through a single unrestricted decompression call. Larger expansions have no enforced limit. This probe demonstrated the unrestricted path without attempting an OOM.

2. **P1: The recoverable-fixture validator accepts a file without a PDF header.** `scripts/build-corpus.py:2909` checks the tail and scanned objects but never validates the header. Reproduce by replacing `b"%PDF-1.7"` with `b"NOTAPDF!"` in `broken/corrupt-xref-recoverable.pdf`. `validate_broken_pdf_structure()` accepts it.

   Supplying those bytes through mocked generator output and file reads, with matching regenerated provenance, makes **`check_corpus()` return `(True, [])`**. Thus a generator regression can silently replace the promised recoverable PDF with a non-PDF.

### consistency: approve

No separate finding.

### integration: needs-attention

Finding **2 (P1)** breaks the prerequisite for downstream recovery tests: `src/pdf/xref.cpp:1113` requires `locate_header_offset()` before recovery, and `:278` returns `NotAPdf` when no valid header exists. The accepted mutation cannot exercise the intended damaged-xref recovery path. This establishes a fixture-gate defect, not an engine defect.

### source-defect: needs-attention

Finding **1 (P1)** relies on an unbounded API: Python documents that `zlib.decompress()` grows its output buffer as necessary; bounded output requires `Decompress.decompress(..., max_length=...)`. [Python zlib documentation](https://docs.python.org/3/library/zlib.html)

### design: advisory

Not applicable; no user surface.

### record: approve

At `3aa15a9`, independently reran the checkpoint: **exit 0, 39 fixtures verified**. Three focused regression tests covering encrypted xrefs, recoverable-document structure and bounded inflation passed. They do not detect findings 1 and 2. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 6

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (the two new tests fail against `3aa15a9`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): every validator inflate now goes through `inflate()`: one complete Flate stream, refused past `VALIDATOR_INFLATE_LIMIT` (4 MiB; the largest corpus stream is under 1 KiB) with at most limit + 1 bytes ever produced, and refused when truncated, malformed or followed by stray bytes. No `zlib.decompress` call remains in the file (the PNG-predictor helper included); an encrypted fixture whose content inflates past the ceiling refuses (`test_validator_inflation_is_bounded_and_exact`).
- Finding 2 (P1): the recoverable fixture must start with the exact `%PDF-1.7` header and binary comment, object 1 right after, every object directly after the previous `endobj` line, and the tail directly after the last (`test_the_recoverable_fixture_starts_with_its_exact_header`).

## Round 7: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:02:43Z
- Effort: medium
- Lines: 32
- Digest: d25e3fe2b256335c

### adversarial: needs-attention

1. **P1: The recursion-limit fixture can lose its nested array while passing verification.** `scripts/build-corpus.py:2983` checks only whether 70 opening brackets, `42`, and 70 closing brackets occur anywhere in the file.

   Reproduced entirely in memory: intercept object 6 during `build_broken_deep_nested_array_70()` generation and change `/Depth70 [...[42]...]` into `/Depth70 ([...[42]...])`. The builder recalculates valid offsets. `validate_broken_pdf_structure()` accepts the resulting literal string. Supplying those bytes as simulated generator output and file contents, with matching provenance, makes **`check_corpus()` return `(True, [])`**.

   This permits a generator regression to remove the promised recursion-limit coverage. Ordinary standalone corruption remains caught by byte comparison.

### consistency: approve

No separate finding.

### integration: needs-attention

Finding **1 (P1)** violates the prerequisite explicitly relied upon at `tests/recto-tests/pdf_parser_tests.cpp:317`: object 6 must contain a nested array that produces `RecursionLimitExceeded`. The accepted replacement contains a string instead. This establishes a fixture-validator defect, not an engine defect.

### source-defect: needs-attention

Finding **1 (P1)** confuses bracket bytes with array syntax. Parentheses delimit a literal string; brackets inside it are string characters, not nested objects. [ISO 32000-1, §7.3.4.2](https://developer.adobe.com/document-services/docs/assets/35e4369068f86065372c18787171a17e/PDF_ISO_32000-1.pdf)

### design: advisory

Not applicable; no user surface.

### record: needs-attention

2. **P3: The round 6 fix description overstates strict inflation coverage.** `docs/reviews/06-quality/audit-d00-t03-s4.md:293` says every validator inflation uses strict `inflate()`. However, `scripts/build-corpus.py:3000` and `:3003` still call `inflate_bounded()`, whose implementation at `:2891` does not check stream completion or trailing bytes. Reproduce by inspecting these call sites. The record should distinguish the bomb-specific bounded helper from the strict helper.

At `e2ea8a1`, the checkpoint independently exited **0**, reporting **39 fixtures verified**. Both focused round 6 regression tests passed. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 7

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (`test_broken_fixtures_carry_their_fault_as_parsed_objects` fails against `e2ea8a1` for the nested-array and page-tree cases, each regenerated through the builder so offsets stay valid); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): instead of fixing the nested-array search alone, every parseable broken fixture now carries its fault as parsed objects through `exact_objects()`: the object set, each dictionary compared whole with values as parsed (so `/Depth70` must be a 70-deep array, never a literal string), and exactly the expected objects carrying streams. The nested-array, cyclic page tree and deflate-bomb fixtures use it (the bomb's stream is now located through it instead of a regex), and the truncated-stream fixture must be exactly the generator's three-object page followed by object 4 cut off inside its declared 512 bytes.
- Finding 2 (P3), a record correction: round 6's note said every validator inflate uses the strict `inflate()`. The deflate-bomb check still uses `inflate_bounded()`, which only counts output and deliberately does not require a complete stream (the inner stage is never inflated to its end); its comment now says so.