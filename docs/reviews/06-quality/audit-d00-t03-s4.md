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