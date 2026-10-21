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

## Round 8: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:09:09Z
- Effort: medium
- Lines: 30
- Digest: f6e989951559594e

### adversarial: needs-attention

1. **P1: Seed verification accepts loss of the promised syntax coverage.** `scripts/build-corpus.py:3035` checks size, general PDF validity and whichever filters happen to exist. It never requires each seed’s defining constructs. `tests/scripts/test_build_corpus.py:629` likewise counts files and invokes that validator.

   Reproduced entirely in memory: replace all six `seeds/*.pdf` entries from `expected_corpus_files()` with `seed-classic-xref.pdf` bytes. Every `validate_seed_pdf_structure()` call succeeds. Mock those entries as generator output and file reads, with matching regenerated provenance: **`check_corpus()` returns `(True, [])`**.

   A generator regression can therefore remove xref-stream, object-stream and outline coverage while passing the section checkpoint. This demonstrates a validator defect, not missing coverage in the currently committed fixtures.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the seed prerequisite consumed by `todo/01-engine/TODO-03-cli-and-fuzz.md:152`. The reproduced replacement removes starting inputs for distinct parser paths. No downstream engine defect is established.

### source-defect: approve

No separate finding.

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

Independently reran the checkpoint at `c23dccb`: exit **0**, **39 fixtures verified**. The section fixture test and round 7 parsed-object regression test also passed: **2 tests, OK**. These checks do not detect finding 1. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 8

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 43 tests`, `OK` (`test_each_seed_proves_its_own_construct` fails against `c23dccb`: any seed stood in for another); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): each seed now proves what it exists to exercise, from parsed objects. `SEED_FORMS` pins every object's dictionary (the filters, the CCITT image parameters, the outline tree, the three link actions, the primitive types), `SEED_XREF_STREAM` the cross-reference form (classic table, or an `/XRef` stream for the object-stream seed), `SEED_OBJSTM_FORMS` the four objects inside the `/ObjStm`, and every content stream is decoded by its parsed `/Filter` (the regex scan of single-line dictionaries is gone) and must hold a text block; the render-ops seed's page and form streams must use exactly their operator sets, read by `content_operators()`, a tokenizer that skips strings, names, numbers, arrays, dictionaries and comments so text inside a string is never an operator.

## Round 9: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:17:11Z
- Effort: medium
- Lines: 30
- Digest: 9e7808fcf99d43a8

### adversarial: needs-attention

1. **P1: Render-seed validation accepts operator names without their required operands.** `scripts/build-corpus.py:3284` compares only operator sets. The syntax check at `:1259` does not validate operand counts, types or execution order.

   Reproduced entirely in memory: intercept `make_stream_obj()` during `build_seed_render_ops()` and replace the page content containing `Seed 6: Render Ops` with `b" ".join(sorted(SEED_RENDER_OPERATORS[4]))`. Let the builder regenerate compression, lengths and offsets. `validate_seed_pdf_structure()` accepts it, although instructions such as `Do`, `Tf`, `cm` and `re` have no operands.

   Supply these bytes through mocked `expected_corpus_files()` and `Path.read_bytes()`, with matching regenerated provenance: **`check_corpus()` returns `(True, [])`**. A generator regression can therefore remove usable rendering coverage while passing the checkpoint. This is a validator defect, not evidence that the committed seed is malformed.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the rendering seed prerequisite consumed by `todo/01-engine/TODO-03-cli-and-fuzz.md:152` and `:156`. The accepted replacement cannot exercise the intended successful rendering operations. No downstream engine defect is established.

### source-defect: needs-attention

Finding **1 (P1)** treats lexical operator presence as executable content. PDF operators have operand requirements and graphics-state constraints; set equality cannot establish those semantics. See [ISO 32000-2 graphics provisions](https://pdf-issues.pdfa.org/32000-2-2020/clause08.html).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `663a5c6`, independently reran `python -B scripts/build-corpus.py --check`: exit **0**, **39 fixtures verified**. The two focused round 8 tests also passed: **2 tests, OK**. Neither detects finding 1. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 9

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (`test_seed_content_must_be_executable_as_written` fails against `663a5c6`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): operator presence is no longer the test. Every seed content stream goes through `validate_content_instructions()`: each instruction's operands are read whole by `content_instructions()` (strings, arrays and dictionaries included) and must match the operator's count and kinds (`CONTENT_OPERANDS`, from ISO 32000-1 Tables 51, 57, 59, 74, 105, 107 and 320); `q`/`Q` and `BT`/`ET` must balance, text-showing operators sit inside `BT ... ET`, a path runs from `m` or `re` to a painting operator with only construction and clipping between, and every `Tf`, `Do` and `gs` name must exist in that stream's own resources (the page's for its `/Contents`, a form's own for the form). The render seed regenerated with bare operator names, and each of five smaller malformations, refuse.

## Round 10: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:23:34Z
- Effort: medium
- Lines: 26
- Digest: 4babb306c9073f36

### adversarial: needs-attention

1. **P1: Invalid array operands still pass seed verification.** `scripts/build-corpus.py:3233` discards array element types, and `:3302` checks only the outer operand kind. Reproduced entirely in memory by intercepting `make_stream_obj()` during `build_seed_render_ops()` and replacing `[(Vec) -40 (tor & Text)] TJ` with `[/Invalid] TJ`. The builder recalculates compression, lengths and offsets; `validate_seed_pdf_structure()` accepts the result. Supplying those bytes through mocked generator output and file reads, with matching regenerated provenance, makes **`check_corpus()` return `(True, [])`**. A generator regression can therefore replace valid rendering coverage with malformed instructions while passing the checkpoint. The committed seed itself is not shown to be malformed.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the rendering seeds consumed by `todo/01-engine/TODO-03-cli-and-fuzz.md:156`: accepted content can fail before exercising the intended rendering operation. No downstream engine defect is established.

### source-defect: needs-attention

Finding **1 (P1)**: accepting any array for `TJ` does not enforce its operand contract. Its elements must be strings or numbers; `/Invalid` is a name. See the [PDF Association operator reference](https://pdfa.org/wp-content/uploads/2023/08/PDF-Operators-CheatSheet.pdf).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `9956476`, independently reran `python -B scripts/build-corpus.py --check`: exit **0**, **39 fixtures verified**. The section fixture test and round 9 instruction-validator regression test also passed: **2 tests, OK**. Neither detects finding 1. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 10

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 44 tests`, `OK` (the five new subtests fail against `9956476`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): an array operand now carries its elements' kinds, and the operand table says what they must be: `TJ` takes a non-empty array of strings and numbers, `d` an array of numbers. `[/Invalid] TJ` refuses. The same layer now also checks operand values (`_check_operand_values`, ISO 32000-1 8.4.3, 8.6.4 and 9.3.6): colour components from 0 to 1, line cap and join 0 to 2, text rendering mode 0 to 7, a non-negative line width, and dash lengths non-negative and not all zero.

## Round 11: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:28:39Z
- Effort: medium
- Lines: 30
- Digest: 9379cd7a856d924d

### adversarial: needs-attention

1. **P1: Seed validation accepts text shown before a font is selected.** `scripts/build-corpus.py:3298` tracks nesting but no current font; `:3344` checks only that a `Tf` resource exists.

   Reproduced entirely in memory: intercept `flate_compress()` during `build_seed_render_ops()`, remove `/F1 12 Tf` immediately after `BT`, and insert it immediately before `ET`. Both `Tj` and `TJ` now execute without a selected font, while all required operators remain present. `validate_seed_pdf_structure()` accepts this seed. Mocking regenerated expected bytes, file reads and matching provenance makes **`check_corpus()` return `(True, [])`**.

   This permits a generator regression to remove valid text-rendering coverage while passing the checkpoint. It does not establish malformed content in the committed seed.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the rendering seeds required by `todo/01-engine/TODO-03-cli-and-fuzz.md:156`. No downstream engine defect is established.

### source-defect: needs-attention

Finding **1 (P1)**: resource existence and eventual `Tf` presence do not establish a valid font at text-showing time. Text font and size have no default initial values. See the [PDF Association operator reference](https://pdfa.org/wp-content/uploads/2023/08/PDF-Operators-CheatSheet.pdf).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `45aba13`, independently reran `python -B scripts/build-corpus.py --check`: exit **0**, **39 fixtures verified**. `test_seed_content_must_be_executable_as_written` also passed. Neither detects finding 1. No files were modified; the working tree remains clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 11

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (the four new subtests fail against `45aba13`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): the text font is tracked as graphics state (set by `Tf`, saved by `q`, restored by `Q`, with no default), and `Tj` or `TJ` before a font is selected refuses, including after a `Q` that restored a state with none. The same validator now also enforces what a text object may contain (ISO 32000-1 Figure 9): `q`, `Q`, `Do` and path operators between `BT` and `ET` refuse.

## Round 12: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:33:56Z
- Effort: medium
- Lines: 30
- Digest: 36649fceaf65b754

### adversarial: needs-attention

1. **P1: Seed validation accepts unmatched marked-content operators.** `scripts/build-corpus.py:3301` tracks graphics saves, text objects and paths, but no marked-content nesting. `EMC` is accepted at `:3203`, and the final balance check at `:3360` never checks it.

   Reproduced entirely in memory: intercept `flate_compress()` during `build_seed_classic_xref()` and append `b"\nEMC\n"` to its plaintext. `validate_seed_pdf_structure("seeds/seed-classic-xref.pdf", mutated)` accepts the resulting 1,023-byte PDF. Supplying those bytes through mocked generator output and file reads, with matching regenerated provenance, makes **`check_corpus()` return `(True, [])`**.

   A generator regression can therefore introduce invalid content while passing the complete checkpoint. This does not establish corruption in the committed seed.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the seed-validation gate supporting downstream fuzz targets. No downstream engine defect was established.

### source-defect: needs-attention

Finding **1 (P1)** violates the marked-content contract: `EMC` ends a sequence begun by `BMC` or `BDC`; matching pairs must be properly nested. The validator accepts an end operator without any beginning. See [ISO 32000-1, section 14.6](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `77e9207`, independently reran `python -B scripts/build-corpus.py --check`: exit **0**, **39 fixtures verified**. The focused `test_seed_content_must_be_executable_as_written` test also passed. Neither detects finding 1. No files were modified; the working tree remains clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 12

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 44 tests`, `OK` (the two new subtests fail against `77e9207`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): no seed uses marked content, yet the operand table admitted `BMC`, `BDC` and `EMC` (and a dozen other operators no seed uses) without tracking them. The table now holds exactly the 33 operators the seeds' content uses, so any other operator, an unmatched `EMC` included, refuses as one the corpus content does not use; the path, text and value sets were narrowed to match.

## Round 13: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:39:02Z
- Effort: medium
- Lines: 30
- Digest: 4a93e6ea58b5a214

### adversarial: needs-attention

1. **P1: Seed validation accepts path construction after a clipping operator.** `scripts/build-corpus.py:3318` permits further path construction while `in_path` remains true; `:3328` tracks no separate state requiring painting immediately after `W` or `W*`.

   Reproduced entirely in memory: intercept `flate_compress()` during `build_seed_classic_xref()` and append `b"\n0 0 m W 10 10 l n\n"` to the plaintext. `validate_seed_pdf_structure("seeds/seed-classic-xref.pdf", mutated)` accepts the resulting **1,028-byte PDF**. Supply those bytes through mocked generator output and file reads, with matching regenerated provenance: **`check_corpus()` returns `(True, [])`**.

   A generator regression can therefore introduce invalid operator ordering while passing the complete checkpoint. This does not establish corruption in the committed fixture.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the seed-validation gate supporting downstream rendering fuzz tests. No downstream engine defect was established.

### source-defect: needs-attention

Finding **1 (P1)**: ISO 32000-1 sections 8.5.1 and 8.5.4 place clipping after the last path-construction operator and immediately before painting. The accepted sequence constructs another segment between clipping and painting. See [ISO 32000-1:2008](https://www.levien.com/pdf/PDF32000_2008.pdf).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `7b81ff1`, independently reran `python scripts/build-corpus.py --check`: exit **0**, **39 fixtures verified**. The focused `test_seed_content_must_be_executable_as_written` test also passed. Neither detects finding 1. No files were modified; the working tree remains clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 13

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 44 tests`, `OK` (the three new subtests fail against `7b81ff1`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): the path is a three-state machine (none, building, clipped). `W` or `W*` needs a path under construction and moves it to clipped, where only a painting operator may follow (ISO 32000-1 8.5.4): construction after clipping, a second clip, and a clip with no path each refuse.

## Round 14: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:43:56Z
- Effort: medium
- Lines: 30
- Digest: a76ae2c2d4870f9c

### adversarial: needs-attention

1. **P1: Encrypted fixtures bypass executable-content validation.** `scripts/build-corpus.py:2888` checks decrypted content using only `validate_content_stream_syntax()`, which checks parentheses and text coordinates, not operator validity or nesting.

   Reproduced entirely in memory: intercept `flate_compress()` during `build_encrypted_rc4_128()` and append `b"\nEMC\n"` to the plaintext before compression and encryption. `validate_encrypted_pdf_structure()` accepts the resulting **1,292-byte PDF**. Supply those bytes through mocked generator output and file reads, with matching regenerated provenance: **`check_corpus()` returns `(True, [])`**.

   A generator regression can therefore produce malformed encrypted page content while passing the complete checkpoint. This does not establish corruption in the committed fixtures.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens validation of encrypted rendering fixtures consumed by the downstream rendering fuzz target (`todo/01-engine/TODO-03-cli-and-fuzz.md:156`). No downstream engine defect was established.

### source-defect: needs-attention

Finding **1 (P1)**: `EMC` must end a marked-content sequence begun by `BMC` or `BDC`. The accepted content contains neither opening operator. See [ISO 32000-1, section 14.6, Table 320](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf).

### design: advisory

Not applicable; this section owns no user surface.

### record: approve

At `1c526d7`, independently reran `python scripts/build-corpus.py --check`: exit **0**, **39 fixtures verified**. The focused `test_seed_content_must_be_executable_as_written` test also passed. Neither detects finding 1. No files were modified; the working tree remains clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 14

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 45 tests`, `OK` (`test_encrypted_and_broken_content_must_be_executable_too` fails against `1c526d7` for all three fixtures); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): every decodable content stream this section's fixtures carry now goes through the same instruction validator as the seeds (`validate_page_content`, with the page's own `/Resources`): the decrypted page content of the encrypted fixtures, and the content of the recoverable and nested-array fixtures. The operator table's description now names the fixtures it governs. A stray `EMC` regenerated into any of the three refuses. (The deflate bomb's content is deliberately never inflated in full, and the truncated stream cannot be, so neither carries content to check.)

## Round 15: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:48:57Z
- Effort: medium
- Lines: 28
- Digest: a404f8e42cd608d8

### adversarial: needs-attention

1. **P1: Encryption validation accepts invalid permission flags.** `scripts/build-corpus.py:2772` reads `/P` but never validates its reserved bits or enforces the documented `/P -1028`. Authentication then derives its key using the unchecked value, so matching encryption does not detect the defect.

   Reproduced entirely in memory: patch `CORPUS_PERMISSIONS_MASK` to `0`, call `build_encrypted_rc4_128()`, then validate the result. `validate_encrypted_pdf_structure()` accepts `/P 0`. Substitute those bytes into mocked generator output and file reads, with matching regenerated provenance: **`check_corpus()` returns `(True, [])`**. A generator regression can therefore pass the complete checkpoint with invalid permissions. This does not establish corruption in the committed fixtures.

### consistency: approve

No separate finding.

### integration: advisory

Finding 1 weakens the encrypted-fixture gate consumed by downstream engine and CLI tests. No downstream implementation defect established.

### source-defect: needs-attention

Finding **1 (P1)** violates the permission-field requirements: reserved bits 7, 8 and 13 through 32 must be set, while bits 1 and 2 must be clear. `/P 0` fails these requirements and contradicts `tests/corpus/README.md:19`. See [QPDF’s permission-field documentation](https://qpdf.readthedocs.io/en/stable/encryption.html#pdf-security-restrictions).

### design: advisory

Not applicable; no user surface.

### record: approve

At `46c6686`, independently reran `python -B scripts/build-corpus.py --check`: exit **0**, **39 fixtures verified**. The focused `test_encrypted_and_broken_content_must_be_executable_too` test also passed. Neither detects finding 1. No files modified; working tree remains clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 15

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 46 tests`, `OK` (`test_encrypted_permissions_are_the_documented_mask` fails against `46c6686` for all three masks); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): `/P` must be a 32-bit value with the reserved bits ISO 32000-1 Table 22 fixes (bits 7, 8 and 13 to 32 set, bits 1 and 2 clear) and must equal `-1028`, the value `tests/corpus/README.md` documents. The expected value is a literal in the validator, not the generator's `CORPUS_PERMISSIONS_MASK`, so a regenerated fixture with a changed constant refuses (`0` and `-1092` on the reserved bits, `-4` on the documented value).

## Round 16: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T22:53:27Z
- Effort: medium
- Lines: 28
- Digest: c03047da36c41377

### adversarial: needs-attention

1. **P1: Password validation shares the generator’s unchecked password constants.** `scripts/build-corpus.py:2824`, `:2827` and `:2843` authenticate using `CORPUS_USER_PASSWORD` and `CORPUS_OWNER_PASSWORD`, rather than independently enforcing the section’s promised `recto-test` / `recto-owner`.

   Reproduced entirely in memory: patch those constants to `b"changed-user"` and `b"changed-owner"`, generate fixtures, and supply the regenerated bytes and matching provenance through mocked file reads. **`check_corpus()` returns `(True, [])`.** Restore the documented passwords and validate the same RC4 fixture: **`R=3 user password authentication failed`**. A generator regression can therefore pass the checkpoint while breaking the explicit password contract. This does not establish corruption in the committed fixtures.

### consistency: approve

No separate finding.

### integration: advisory

Finding **1 (P1)** affects consumers that authenticate with literal documented passwords, including `tests/recto-tests/pdf_crypto_tests.cpp:207` and `:257`. Those downstream tests would encounter authentication failures despite the corpus checkpoint passing.

### source-defect: approve

The round-15 `/P` issue is resolved at `scripts/build-corpus.py:2775`: reserved bits are checked and the expected value is literal `-1028`. The bit requirements agree with [QPDF’s permission-field documentation](https://qpdf.readthedocs.io/en/stable/encryption.html#pdf-security-restrictions).

### design: advisory

Not applicable; no user surface.

### record: approve

Independently reran `python -B scripts/build-corpus.py --check` at `d46d9ac`: exit **0**, **39 fixtures verified**. The focused permissions and executable-content regression tests both passed. Neither detects finding 1. No files modified; working tree remains clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 16

`python -m unittest tests.scripts.test_build_corpus` -> `OK` (`test_a_changed_generator_password_is_caught` fails against `d46d9ac` for all three encrypted fixtures); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1), fixed as a class with round 15's: the validators no longer read any generator constant. The documented contract (`tests/corpus/README.md`) is held in separate literals, `DOCUMENTED_USER_PASSWORD`, `DOCUMENTED_OWNER_PASSWORD`, `DOCUMENTED_PERMISSIONS`, `DOCUMENTED_PDF_DATE`, `DOCUMENTED_SEED_MAX_BYTES` and `DOCUMENTED_STREAM_CEILING`, which every validator uses in place of `CORPUS_USER_PASSWORD`, `CORPUS_OWNER_PASSWORD`, `CORPUS_PERMISSIONS_MASK`, `FIXED_PDF_DATE`, `MAX_SEED_FILE_BYTES` and `DEFLATE_BOMB_LIMIT_BYTES`. `test_the_documented_contract_matches_the_readme` ties each literal to the README's own wording, and fixtures regenerated with changed passwords refuse.
Correction to the round 16 note: as first committed (`92924c9`), `test_a_changed_generator_password_is_caught` validated after leaving the patched constants, so it also passed against `d46d9ac` and proved nothing. It now regenerates and validates while the generator's passwords are still changed, the regression as it would happen, and fails against `d46d9ac` for all three encrypted fixtures.