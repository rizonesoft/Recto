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

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:04:56Z
- Effort: medium
- Lines: 32
- Digest: 7e7313b57343a93e

## adversarial: needs-attention

1. **P1: Escaped dictionary names bypass duplicate `/Length` rejection.** `scripts/build-corpus.py:1276` compares raw names without decoding hexadecimal escapes. Reproduce with `ClassicPdfBuilder`, using content object `b'<< /Length 3 /Len#67th 999 >>\nstream\nq Q\nendstream'`, then call `validate_pdf_structure`. It accepts this conflicting duplicate `/Length`. PDF names `/Len#67th` and `/Length` are identical under the [Adobe name-object rules](https://opensource.adobe.com/dc-acrobat-sdk-docs/library/plugin/Plugins_Cos.html). The round-2 promise of exactly one length therefore remains false.

2. **P2: Literal-string delimiters break stream dictionary discovery.** `scripts/build-corpus.py:1281` balances raw `<<` and `>>` bytes without recognizing strings. Reproduce with content object `b'<< /Length 3 /Note (<<) >>\nstream\nq Q\nendstream'`. Despite correct length and builder-generated xref offsets, `validate_pdf_structure` rejects it with `unexpected byte b')' in dictionary`. A valid metadata string prevents corpus validation.

## consistency: approve

No independent findings.

## integration: advisory

The structural checks can certify malformed future fixtures used by engine tests. Byte equality against generator output cannot detect a defect shared by the generator and committed fixture. These reproductions do not establish corruption in the current committed PDFs.

## source-defect: needs-attention

3. **P1: Xref-stream validation ignores metadata that determines decoding and object numbering.** `scripts/build-corpus.py:1081` hardcodes Flate/PNG decoding; `scripts/build-corpus.py:1088` assumes entries enumerate objects starting at zero without checking `/Index`. Two independently reproduced mutations of `build_compressed_xref_2p()` pass `validate_pdf_structure`:
   - Replace `b'/Type /XRef /Size 11'` with `b'/Type /XRef /Index [1 11] /Size 11'`. The declared numbering shifts every entry, but validation still checks the original numbering.
   - Replace `b'/Filter /FlateDecode /DecodeParms'` with `b'/Filter /ASCII85Decode /DecodeParms'`. The payload remains Flate-compressed, but validation accepts the incompatible declared filter.

   Both mutations preserve existing object offsets and stream lengths. The validator must honor or explicitly reject these declarations to substantiate structural validity. See [Adobe PDF Reference, sections 3.2.7 and 3.4.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf).

## design: advisory

Not applicable: no user surface.

## record: advisory

Independently reran the checkpoint at `af33f83`: `python -B scripts/build-corpus.py --check` exited 0 and reported 39 current fixtures. Four targeted regression tests from the previous rounds also passed. These results do not resolve findings 1–3. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

The validator now checks the generator's own narrow PDF dialect and refuses anything outside it, instead of trying to read every legal spelling. `python -m unittest tests.scripts.test_build_corpus` -> `Ran 13 tests`, `OK` (the new tests fail against `af33f83`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): dictionary keys must be plain names (`/[A-Za-z0-9_.+-]+`); an escaped key such as `/Len#67th` refuses, so no second spelling of `/Length` can pass.
- Finding 2 (P2): stream dictionaries are found by walking objects forward (`iter_stream_dicts`): each object's value is skipped with the string-aware parser and each stream's payload by its own `/Length`, so `(<<)` in a string is data, and stream bytes are never read as structure (`test_stream_dictionaries_parse_strings_and_refuse_escaped_keys`).
- Finding 3 (P1): an xref stream must declare exactly what the validator applies: `/Filter /FlateDecode`, `/DecodeParms` with `/Predictor 12` and `/Columns` equal to the `/W` sum, and `/Index` absent or `[0 Size]` (`test_xref_stream_declarations_are_the_ones_applied`, both mutations).

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:12:20Z
- Effort: medium
- Lines: 28
- Digest: 8e7bc7ee0de6b812

## adversarial: needs-attention

1. **P1: A string can override the xref field widths used by validation.** `scripts/build-corpus.py:1067` searches raw dictionary bytes for `/W`. Reproduced by generating `build_compressed_xref_2p()`, replacing `b'/W [1 2 2]'` with `b'/Note (/W [1 2 2]) /W [1 1 3]'`, then calling `validate_pdf_structure`. Validation succeeds using the string’s widths, although the actual `/W` changes how object offsets and indexes decode. Existing object offsets, stream lengths and `startxref` remain unchanged.

2. **P2: Valid literal-string content causes false stream-length failures.** `scripts/build-corpus.py:1364` counts `b'\nendstream'` throughout the file, including strings. Reproduced with `ClassicPdfBuilder`, setting the page’s content object to `b'<< /Length 3 /Note (\nendstream) >>\nstream\nq Q\nendstream'`. Validation rejects it with `1 stream(s) were not length-checked`, despite correct length and builder-generated xref offsets. The final raw count defeats the string-aware object walk.

## consistency: approve

No independent findings.

## integration: advisory

Findings 1 and 3 allow malformed future fixtures to pass structural validation. Deterministic byte comparison cannot catch an error shared by generated and committed bytes. These reproductions do not establish corruption in the currently committed fixtures.

## source-defect: needs-attention

3. **P1: Predictor validation ignores parameters that change decoded row boundaries.** `scripts/build-corpus.py:1082` checks `/Predictor` and `/Columns` but ignores `/Colors` and `/BitsPerComponent`. Reproduced by replacing `b'/Predictor 12 /Columns 5'` with `b'/Predictor 12 /Colors 3 /Columns 5'` in `build_compressed_xref_2p()` output. `validate_pdf_structure` accepts it while decoding five-byte rows; the declaration requires fifteen-byte rows at the default eight bits per component. These parameters govern prediction together, as documented in [Adobe PDF Reference, Table 3.7](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf). The validator must enforce its supported defaults or apply the declared values.

## design: advisory

Not applicable: this section owns no user surface.

## record: advisory

At `d12f073`, independently reran `python -B scripts/build-corpus.py --check`: exit 0, 39 current fixtures. Seven targeted regression tests passed. Two in-memory generations were identical; on-disk corpus size was 60,176 bytes. This confirms the recorded checkpoint’s current result, but does not substantiate complete structural validation given findings 1 and 3. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 4

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 14 tests`, `OK` (the new test fails against `d12f073`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): the xref stream dictionary is read through `dict_entries` (parsed key-value pairs, a repeated key refused) and may hold only the keys the corpus writes, so `/Note (/W [1 2 2])` refuses and `/W` comes from the real entry.
- Finding 2 (P2): the raw `endstream` count is gone; `iter_stream_dicts` now parses every object through its `endobj` (a stream's payload skipped by its `/Length` and required to end in `endstream`), so the keyword inside a string is data and no object can be skipped unparsed.
- Finding 3 (P1): `/DecodeParms` must be exactly `/Predictor 12 /Columns <W sum>`; `/Colors` or `/BitsPerComponent` refuses rather than being ignored.

## Round 5: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:17:19Z
- Effort: medium
- Lines: 28
- Digest: 200ceb14a4f1a04d

## adversarial: needs-attention

1. **P1: Object-stream offsets can still be read from a string.** `scripts/build-corpus.py:1129` searches raw dictionary bytes for `/First`. Reproduced by wrapping `make_stream_obj` during `build_compressed_xref_2p()` and, only for `/ObjStm`, replacing `/First 32` with `/Note (/First 32) /First 0`. The builder recomputes all outer offsets. `validate_pdf_structure` accepts the result using 32, although the actual dictionary declares 0, placing compressed objects at incorrect locations.

2. **P1: Invalid previous-revision pointers pass validation.** `scripts/build-corpus.py:1052` reads `/Prev` through a regex over the trailer. Reproduce with `build_incremental_2rev()`: replace the final `/Prev 577` with `/Note (/Prev 577) /Prev 999999`. Validation succeeds by following the string’s value, leaving the actual out-of-file pointer unchecked. Separately, `scripts/build-corpus.py:1064` allows `/Prev` in xref-stream dictionaries but never validates it: inserting `/Prev 999999` immediately after `/Type /XRef` in `build_compressed_xref_2p()` also passes. `/Prev` must locate the previous cross-reference section. [Adobe PDF Reference, Table 3.13](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf)

## consistency: approve

No independent findings.

## integration: advisory

These gaps allow malformed future fixtures through the structural gate used by downstream engine tests. Deterministic comparison cannot detect a defect shared by generated and committed bytes. The reproductions do not establish corruption in the current committed fixtures.

## source-defect: needs-attention

3. **P1: Object-stream decoding ignores its declared filter.** `scripts/build-corpus.py:1139` unconditionally applies `zlib.decompress`. Reproduced by wrapping `make_stream_obj` during `build_compressed_xref_2p()` and replacing `/Filter /FlateDecode` with `/Filter /ASCII85Decode` only in `/ObjStm`. Outer offsets are recomputed; compressed payload and `/Length` remain intact. `validate_pdf_structure` accepts the PDF despite validating different decoding semantics from those declared. The stream’s `/Filter` specifies how its bytes must be decoded. [Adobe CosStream documentation](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdflsdk/apireference/COS_Layer/CosStream.html)

## design: advisory

Not applicable: no user surface.

## record: advisory

At `ac030f1`, independently reran `python -B scripts/build-corpus.py --check`: exit 0, 39 current fixtures. Seven targeted regression tests passed; two in-memory generations matched; on-disk corpus size was 60,176 bytes. These verify the recorded checkpoint result, but not the complete structural-validation promise given findings 1–3. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 5

Every dictionary the validator reads now comes from one parsed object walk; no raw text search over dictionary bytes is left (the trailer, xref streams, object streams, content-stream filters, AcroForm and Widget checks, and the per-fixture checks were all converted). `python -m unittest tests.scripts.test_build_corpus` -> `Ran 15 tests`, `OK` (the new test fails against `ac030f1` in all four cases); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`; the converted AcroForm checks still refuse a renamed `/DR` and a missing `/Off` appearance.

- Finding 1 (P1): object streams are read with `stream_object_at` and `dict_entries`, limited to the keys the corpus writes (`/Type /N /First /Filter /Length`), so `/Note (/First 32)` refuses and `/First` is the real entry.
- Finding 2 (P1): classic trailers are parsed (keys limited to `/Size /Root /Info /ID /Prev /Encrypt`) and `/Prev` comes from the trailer's own entry; an xref stream's `/Prev` must locate a previous xref section, which is validated in turn (cycle-guarded).
- Finding 3 (P1): an object stream must declare `/Filter /FlateDecode`, the decoding the validator applies.

## Round 6: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:26:13Z
- Effort: medium
- Lines: 26
- Digest: 600842b80e6a58ab

## adversarial: needs-attention

1. **P1: Xref offsets can point inside stream payloads and bypass `/Length` validation.** `scripts/build-corpus.py:1040` checks only whether the offset starts with the expected object-header bytes, without tying it to the parsed object walk. Reproduced with `ClassicPdfBuilder`: create normal objects 1–4, then an uncompressed object 5 whose payload is `b'4 0 obj\n<< /Length 999 >>\nstream\nq Q\nendstream\nendobj\n'`. Redirect object 4’s fixed-width xref entry to that embedded header. `validate_pdf_structure` accepts it. The xref-selected content has an invalid length, while the validator checks the different, physically top-level object 4.

## consistency: approve

No independent findings.

## integration: advisory

These structural-gate gaps can admit malformed future fixtures into downstream engine tests. Deterministic byte comparison cannot detect a defect shared by the generator and committed output. The reproductions do not establish corruption in the current committed fixtures.

## source-defect: needs-attention

2. **P1: Compressed-object validation accepts mismatched delimiters.** `scripts/build-corpus.py:1172` uses a shared depth counter for arrays and dictionaries, allowing `>>` to close `[`. Reproduced by wrapping `flate_compress` during `build_compressed_xref_2p()` to replace `b'<< /Type /Catalog'` with equal-length `b'[  /Type /Catalog'` before compression. The builder recalculates outer offsets and lengths; `validate_pdf_structure` accepts the malformed catalog. Array and dictionary delimiters are distinct under [Adobe PDF Reference, sections 3.2.5–3.2.6](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf). This separate scanner does not establish that each compressed slot contains one valid object.

## design: advisory

Not applicable: no user surface.

## record: advisory

At `225b143`, independently reran `python -B scripts/build-corpus.py --check`: exit 0, 39 current fixtures. Eight targeted regression tests passed; corpus size was 60,176 bytes. The recorded checkpoint result holds, but does not substantiate complete structural validation given findings 1 and 2. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 6

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 17 tests`, `OK` (both new tests fail against `225b143`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): an xref offset (classic entry or xref-stream type-1 entry) must be one of the object-header offsets the forward walk parsed (`object_header_offsets`), so a header written inside a stream payload or a string can never be selected (`test_xref_offsets_name_only_objects_the_walk_parsed`).
- Finding 2 (P1): an object-stream slot is checked with the same string-aware parser as every other object, which keeps `>>` and `]` apart; the separate depth counter is gone (`test_an_object_stream_slot_keeps_dictionary_and_array_delimiters_apart`).

## Round 7: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:33:40Z
- Effort: medium
- Lines: 26
- Digest: 98b4230fc94cf09c

## adversarial: needs-attention

1. **P1: An entirely missing cross-reference index passes validation.** `scripts/build-corpus.py:1027` accepts a zero-entry subsection without checking overall object coverage; `scripts/build-corpus.py:1057` also never validates `/Size`. Reproduced using `d = build_simple_1p()`, locating `x = d.index(b'\nxref\n') + 1` and `t = d.index(b'trailer\n', x)`, then validating `d[:x] + b'xref\n0 0\n' + d[t:]`. Validation succeeds although this single-revision document now has no entries locating its objects. Independently, replacing the trailer’s `/Size` value with `0` also passes. The cross-reference table must cover every object number across its revision chain. [Adobe PDF Reference, §3.4.3](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf)

## consistency: approve

No independent findings.

## integration: advisory

Both findings allow malformed future fixtures through the structural gate used by downstream engine tests. Deterministic byte comparison cannot detect a defect shared by the generator and committed output. These reproductions do not establish corruption in the current committed fixtures.

## source-defect: needs-attention

2. **P1: Compressed-object validation checks delimiters but still accepts invalid dictionary syntax.** `scripts/build-corpus.py:1177` delegates to `_skip_object`, whose dictionary branch at `scripts/build-corpus.py:1244` skips arbitrary tokens without enforcing name/value pairs. Reproduced by wrapping `flate_compress` during `build_compressed_xref_2p()` and replacing `b'<< /Type /Catalog'` with equal-length `b'<< 12345 /Catalog'` before compression. `validate_pdf_structure` accepts the malformed catalog; all object offsets and stream lengths remain correct. Replacing the same prefix with `b'<< /Type XXXXXXXX'` also passes despite the invalid bare value. Dictionary keys must be names, and values must be PDF objects. [Adobe’s object syntax documentation](https://developer.adobe.com/acrobat-pdfl-sdk/guides/plugin/plugins-cos)

## design: advisory

Not applicable: no user surface.

## record: advisory

At `43c6678`, independently reran `python -B scripts/build-corpus.py --check`: exit 0, 39 current fixtures. Ten targeted regression tests passed; two in-memory generations matched; corpus size was 60,176 bytes. The recorded checkpoint result holds, but does not substantiate complete structural validation given findings 1 and 2. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 7

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 19 tests`, `OK` (the two new tests fail against `43c6678` in all nine cases); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): the cross-reference chain must locate every parsed object at its newest definition's header, and the newest `/Size` (trailer or xref stream) must be exactly one past the highest object number; an empty `xref 0 0` section or `/Size 0` refuses (`test_the_xref_chain_locates_every_object_and_sizes_it`).
- Finding 2 (P1): `_skip_object` is now a grammar for the values the corpus writes: dictionaries of plain-name keys each with a value, arrays, literal and hex strings (hex digits only), plain names, numbers, `N G R` references, `true`, `false`, `null`; anything else refuses, so `<< 12345 /Catalog` and `<< /Type XXXXXXXX` no longer pass (`test_every_object_follows_the_pdf_value_grammar`).

The encryption validators (`validate_encrypted_*`) still read some dictionaries by text search; they validate the encrypted fixtures that `D00 T03 §4` owns, and are taken up in that section's audit (`D06 T05 §12`).

## Round 8: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:41:09Z
- Effort: medium
- Lines: 46
- Digest: d3118f5661c433ee

## adversarial: needs-attention

1. **P1: A newer free entry incorrectly falls back to an older live object.** `scripts/build-corpus.py:1041` records only `n` entries, so `/Prev` traversal resurrects objects explicitly freed by the newest revision. Reproduced in memory:

   ```python
   d = m.build_incremental_2rev()
   t = d.rfind(b"trailer\n")
   d = d[:t] + b"1 1\n0000000000 00001 f \n" + d[t:]
   m.validate_pdf_structure("probe.pdf", d)
   ```

   Validation succeeds although the latest index frees catalog object 1 while `/Root 1 0 R` still references it. Existing object offsets and `startxref` remain unchanged. This defeats round 7’s requirement that validation reflect the newest cross-reference state.

## consistency: approve

No independent findings.

## integration: advisory

Both findings let malformed generated fixtures pass the structural gate relied upon by downstream engine tests. Deterministic byte comparison cannot detect defects shared by the generator and committed output. These reproductions do not establish corruption in the current committed fixtures.

## source-defect: needs-attention

2. **P1: Compressed dictionaries still accept duplicate keys with conflicting values.** `scripts/build-corpus.py:1275` parses dictionary pairs without checking key uniqueness. Compressed objects reach this parser through `_complete_pdf_object` at `scripts/build-corpus.py:1192`, bypassing `dict_entries`’ duplicate rejection. Reproduce by wrapping `flate_compress` during `build_compressed_xref_2p()`:

   ```python
   original = m.flate_compress
   m.flate_compress = lambda raw: original(
       raw.replace(b"/Type /Catalog /Pages", b"/Type /Catalog /Type ")
   )
   d = m.build_compressed_xref_2p()
   m.validate_pdf_structure("probe.pdf", d)
   ```

   Validation succeeds with two `/Type` entries and no `/Pages` in the compressed catalog. The equal-length replacement preserves compressed-object offsets; the builder recalculates stream lengths and outer offsets. Duplicate dictionary keys have undefined values under [Adobe PDF Reference §3.2.6](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.6.pdf). The claimed strict grammar still certifies this malformed fixture.

## design: advisory

Not applicable: no user surface.

## record: advisory

At `00ec641`, independently reran `python -B scripts/build-corpus.py --check`: exit 0, 39 current fixtures. Four targeted structural regression tests passed. Corpus size was 60,176 bytes. The recorded checkpoint result holds, but does not establish the structural guarantees contradicted above. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 8

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 20 tests`, `OK` (`test_a_newer_free_entry_and_a_repeated_key_refuse` fails against `00ec641`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): free entries are recorded in the chain too (classic `f`, xref-stream type 0), newest first, so an object a newer revision frees cannot fall back to an older live entry; the coverage check then refuses the walked object the chain no longer locates.
- Finding 2 (P1): the grammar refuses a dictionary key that appears twice, wherever the dictionary sits (compressed objects included).

## Round 9: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:45:42Z
- Effort: medium
- Lines: 51
- Digest: 14792bf61bd646d5

## adversarial: needs-attention

1. **P1: Structural validation accepts an unavailable document catalog.** `scripts/build-corpus.py:1065` parses `/Root` without validating its target; `scripts/build-corpus.py:1509` finds catalogs by scanning objects instead of resolving `/Root`. The coverage check at `scripts/build-corpus.py:1550` excludes compressed objects, so round 8’s free-entry protection does not catch a freed compressed catalog.

   Reproduced in memory, with `m` importing `scripts/build-corpus.py`:

   ```python
   original = m.flate_png_up_compress

   def freed_catalog(raw, columns):
       rows = bytearray(raw)
       rows[5:10] = struct.pack(">BHH", 0, 0, 1)
       return original(bytes(rows), columns)

   m.flate_png_up_compress = freed_catalog
   d = m.build_compressed_xref_2p()
   m.flate_png_up_compress = original
   m.validate_pdf_structure("probe.pdf", d)
   ```

   Validation succeeds although catalog object 1 is free and `/Root` remains `1 0 R`. The builder recalculates stream lengths and offsets. A simpler reproduction also passes:

   ```python
   d = m.build_simple_1p().replace(b"/Root 1 0 R", b"/Root 9 0 R")
   m.validate_pdf_structure("probe.pdf", d)
   ```

   Object 9 does not exist. The structural gate therefore certifies a document without a usable root catalog.

## consistency: approve

No independent findings.

## integration: advisory

Finding 1 allows malformed generated fixtures through the structural gate used to support downstream PDF tests. Deterministic byte comparison cannot detect a defect shared by generated and committed bytes. These probes do not establish corruption in the currently committed fixtures.

## source-defect: needs-attention

Finding 1 contradicts the requirement for a valid document catalog reference. Free objects resolve as null, so retaining catalog bytes inside an object stream does not make a freed catalog available. See [Adobe PDF Reference, sections 3.4 and 3.6.1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf).

## design: advisory

Not applicable: this section owns no user surface.

## record: advisory

Independently reran `python -B scripts/build-corpus.py --check` at `0c9bae9`: exit 0, **39 current fixtures**. Sixteen selected read-only regression tests passed; two in-memory generations matched; corpus size was **60,176 bytes**. The recorded checkpoint result holds, but does not establish the structural guarantee contradicted by finding 1. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 9

`python -m unittest tests.scripts.test_build_corpus` -> `Ran 21 tests`, `OK` (`test_the_root_resolves_to_a_live_catalog` fails against `0c9bae9`); `python scripts/build-corpus.py --check` -> `verified: 39 corpus fixtures ... are current`.

- Finding 1 (P1): the newest trailer's (or xref stream's) `/Root` is resolved through the cross-reference chain, to a walked object or a live object-stream slot (`compressed_dict`), and must be a `/Type /Catalog` dictionary; a `/Root` naming a missing object, a non-catalog, or a catalog the newest xref stream frees refuses. The AcroForm and Widget checks now read that resolved catalog instead of scanning for one.