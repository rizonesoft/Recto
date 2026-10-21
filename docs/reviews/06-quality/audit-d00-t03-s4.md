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