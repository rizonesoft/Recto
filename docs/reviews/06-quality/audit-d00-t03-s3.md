# Audit - D00 T03 §3: Deterministic PDF corpus generator

- **Audit section:** `D06 T05 §11` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T03 §3` (`docs/reviews/d00-t03-s3.json`, earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `2367356`: `python scripts/build-corpus.py --check` -> exit 0, `verified: 39 corpus fixtures in R:\GitHub\Recto\tests\corpus are current`. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:39:37Z
- Effort: medium
- Lines: 28
- Digest: 479c262538549392

## adversarial: needs-attention

1. **P1: Stream-length validation silently skips multiline dictionaries.** `scripts/build-corpus.py:1181` only matches dictionaries without newlines, so a stream can evade the promised `/Length` check entirely. Reproduced in memory: use `ClassicPdfBuilder` to create a page whose content object is `b'<<\n/Length 999\n>>\nstream\nq Q\nendstream'`, then call `validate_pdf_structure`. It returns successfully despite the three-byte payload. The builder supplies correct xref offsets, isolating the missing length validation. Byte equality cannot catch this if the generator produces the malformed fixture consistently.

## consistency: approve

No independent findings.

## integration: advisory

The structural-validation gaps in findings 1 and 2 affect the generator’s validation of future fixtures. They are not evidence that the currently committed PDFs have those corruptions. The selected existing tests pass without detecting either gap.

## source-defect: needs-attention

2. **P1: Compressed-object byte offsets are never validated.** `scripts/build-corpus.py:1121` checks the object-stream header’s token count and object IDs but ignores every offset token. Reproduced in memory: intercept `flate_compress` while running `build_compressed_xref_2p()`, replacing the decoded header prefix `b'1 0 2 '` with `b'1 9 2 '`. The builder recomputes stream lengths and outer xref offsets; `validate_pdf_structure` still accepts the result, although the catalog’s declared offset now points inside its dictionary. Object-stream offsets must locate the corresponding objects, as specified in [Adobe’s PDF Reference, section 3.4.6](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf). This leaves the compressed-object fixture outside the promised structural verification.

## design: advisory

Not applicable: this section owns no user surface.

## record: needs-attention

3. **P3: Generated provenance claims fixture coverage that does not exist.** `scripts/build-corpus.py:2552` claims bleed, trim and art boxes, but `build_rotated_pages()` emits only MediaBox and CropBox. Lines `2560` and `2561` claim DOCX headings, styled runs, a list and a 3×3 table, plus XLSX shared strings and a SUM formula. The DOCX contains two plain paragraphs; the XLSX uses inline strings and contains no formula. Reproduce by inspecting the page dictionaries and reading the Office ZIP members with `zipfile.ZipFile`. These descriptions mislead downstream fixture selection; correct them at `CORPUS_PURPOSES`, which generates the provenance table.

The recorded checkpoint was independently rerun: exit 0, `verified: 39 corpus fixtures ... are current`. That result establishes reproducibility, but does not resolve findings 1 and 2. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 9 tests`, `OK`; against the previous `scripts/build-corpus.py` the new tests fail (`test_a_stream_length_is_checked_whatever_the_dictionary_layout`, `test_object_stream_offsets_must_locate_their_objects`, `test_fixture_descriptions_claim_only_what_the_fixtures_hold`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current` (only `PROVENANCE.md` was regenerated; every fixture's bytes are unchanged); `recto-tests.exe --gtest_filter=Corpus.*` -> `[  PASSED  ] 4 tests.`.

- Finding 1 (P1): `validate_pdf_structure` finds every `stream` keyword after a dictionary's `>>`, whatever the dictionary's line layout, walks back to its balanced `<<` (`_dict_open`), checks `/Length`, and refuses when any `endstream` was not length-checked.
- Finding 2 (P1): `validate_objstm_offsets` requires an object stream's offsets to start at 0, rise strictly, stay inside the stream, and bound exactly one complete object per slot (`_complete_pdf_object`), so a shifted offset into the catalog's dictionary refuses.
- Finding 3 (P3): `CORPUS_PURPOSES` (which generates `tests/corpus/PROVENANCE.md`) now describes the rotated pages' `/MediaBox` and `/CropBox`, the two-paragraph `.docx` and the inline-string `.xlsx` as they are; a test ties each description to the fixture's actual content.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T18:57:55Z
- Effort: medium
- Lines: 26
- Digest: f9c6180874402d61

## adversarial: needs-attention

1. **P1: `/Length` validation can read the wrong dictionary entry.** `scripts/build-corpus.py:1243` searches the entire dictionary text, including nested dictionaries and literal strings. Reproduced using `ClassicPdfBuilder` with content object `b'<< /Params << /Length 3 >> /Length 999 >>\nstream\nq Q\nendstream'`: `validate_pdf_structure` accepts it despite the actual stream dictionary declaring 999 bytes for a three-byte payload. Replacing `/Params << /Length 3 >>` with `/Note (/Length 3)` also passes. The multiline fix therefore still does not establish the promised stream-length validation.

## consistency: approve

No independent findings.

## integration: advisory

Findings 1 and 2 allow malformed generator output to pass structural validation. Comparing generated bytes against committed bytes cannot detect these defects when both contain the same mistake. These reproductions do not establish corruption in the current committed fixtures.

## source-defect: needs-attention

2. **P1: Invalid classic-xref flags bypass offset validation.** `scripts/build-corpus.py:1039` validates offsets only when the flag equals `n`; other flags are silently accepted. Reproduce by generating `build_simple_1p()`, replacing its first in-use xref entry with `b'9999999999 00000 x \n'`, preserving the entry length, then calling `validate_pdf_structure`. It returns successfully despite the invalid flag and out-of-file offset. Classic entries must use `n` or `f`, as specified in [Adobe PDF Reference §3.4.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf). This defeats the section’s structural verification promise.

## design: advisory

Not applicable: no user surface.

## record: advisory

Independently reran the checkpoint at `a64bc0a`: exit 0, `verified: 39 corpus fixtures in R:\GitHub\Recto\tests\corpus are current`. The three round-1 regression tests also pass. Those results do not substantiate complete structural validation given findings 1 and 2. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 11 tests`, `OK`; both new tests fail against `a64bc0a`; `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): stream dictionaries are parsed, not searched: `top_level_values` walks a dictionary body object by object (strings with escapes, hex strings, nested dictionaries and arrays skipped whole, `N G R` references kept whole), and a stream needs exactly one direct `/Length` among its own entries. A `/Length` inside a nested dictionary or a string no longer counts; a duplicate, missing or indirect `/Length` refuses (`test_only_the_stream_dictionarys_own_length_counts`). The xref-stream and object-stream `/Length` reads use the same parser.
- Finding 2 (P1): every classic xref entry must be the one 20-byte form ISO 32000-1 7.5.4 allows (`\d{10} \d{5} [nf]` plus a two-byte end of line), so an `x` flag, a bad end of line or a non-digit offset refuses (`test_a_classic_xref_entry_has_one_form`).