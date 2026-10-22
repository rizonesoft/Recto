# Audit - D01 T01 §2: PdfValue variant object model, dictionary/array helpers and deep cloning

- **Audit section:** `D06 T05 §16` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §2` (`docs/reviews/d01-t01-s2.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `7923670`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfObject.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 6 tests.` (6 tests from 1 suite). The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T06:00:34Z
- Effort: medium
- Lines: 26
- Digest: 274afce360e7dfcd

### adversarial: needs-attention

1. **P2: Unterminated language escapes silently discard text and return success.** `src/pdf/object.cpp:746` and `src/pdf/object.cpp:799` consume everything after ESC without requiring a closing delimiter. Reproduce with `pdf_string_to_utf8({0xFE,0xFF,0x00,0x41,0x00,0x1B,0x00,0x42})`: the result is successful `"A"`, silently dropping the remainder. The UTF-8 equivalent, `EF BB BF 41 1B 42`, behaves identically. This contradicts the repository requirement that malformed-input failures return typed errors. These are source-traced reproductions, not newly executed tests.

### consistency: approve

### integration: needs-attention

2. **P1: Decoded dictionary keys select the wrong values during decryption.** `src/pdf/crypto.cpp:1887` passes an already-decoded key to `get_mut(std::string_view)` at line 1891, which strips its leading slash at `src/pdf/object.cpp:551`. The stream-dictionary loop repeats this at `src/pdf/crypto.cpp:1909`.

   Reproduce using the authenticated RC4 handler setup in `tests/recto-tests/pdf_crypto_tests.cpp:573`: construct a dictionary containing encrypted strings under `PdfName::from_decoded("A")` and `PdfName::from_decoded("/A")`, then call `decrypt_object_values`. Both iterations select `"A"`, decrypting it twice, while `"/A"` remains encrypted. RC4's second application restores ciphertext, yet the operation returns success. These are distinct legal keys, represented in PDF syntax as `/A` and `/#2FA`. The caller must use the existing exact `PdfName` overload. This finding is established by source tracing; the additional regression was not executed.

### source-defect: needs-attention

Finding 1 also applies here: a language escape requires a closing ESC and a language code, rather than an arbitrary remainder of the string. The decoder does not validate that structure. See [Adobe PDF Reference 1.7, section 3.8.1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.7old.pdf).

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

The existing x64 Release binary passed all six `PdfObject.*` tests. `Support.AsanActiveWhenEnabled` separately passed and reported `ASan=enabled`. This corroborates the recorded runtime result, but those tests miss the failures above. No rebuild was performed under the no-file-modification constraint, so current x64/ARM64 warning-free compilation was not independently reverified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

- Finding 2 (P1): correct. `decrypt_value_recursive` iterated a dictionary's entries and looked each one up again through `get_mut(std::string_view)`, which strips a leading `/`, so the distinct keys `/A` and `/#2FA` (decoded `"/A"`) both selected `A`: one entry was decrypted twice (RC4 restoring the ciphertext) and the other never. Both loops (dictionary and stream dictionary) in `src/pdf/crypto.cpp` now pass the entry's own `PdfName` to the exact overload. `PdfCrypto.DecryptsKeysThatDifferOnlyByALeadingSlash` decrypts a dictionary and a stream dictionary holding both keys and requires each value to be plaintext. On the old code it fails. No other caller passes a decoded entry key to a `string_view` overload (searched `src/` for `first.view()` and `entries()[`).
- Finding 1 (P2): correct that text was dropped; the fix follows the decoder's existing policy for malformed units (an unpaired surrogate or an odd trailing byte becomes U+FFFD and decoding continues), not a failed result, since `pdf_string_to_utf8` decodes display text. A language escape is skipped only in the form ISO 32000-1 section 7.9.2.2 defines (ESC, a 2-letter ISO 639 code, an optional 2-letter ISO 3166 code, ESC), in both UTF-16BE and UTF-8 strings; any other ESC becomes U+FFFD and the text after it is decoded. The policy is stated in `src/pdf/object.h`. `PdfObject.StringEncoding` adds eight cases (well-formed with a country code, unterminated, one letter, digits, too long), and 12 of its assertions fail on the old decoder.

Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 107 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfObject.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 14 tests.`

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T06:13:46Z
- Effort: medium
- Lines: 37
- Digest: 15fca77de49b1e3d

### adversarial: needs-attention

1. **P3: The encoder silently turns literal ESC characters into PDF language-control syntax.** `src/pdf/object.cpp:909` emits U+001B unchanged into UTF-16BE. The decoder at `src/pdf/object.cpp:756` then interprets it as an escape delimiter, so encoding and decoding valid UTF-8 can remove text.

   Reproduce:
   ```cpp
   const std::string input = "A\x1B" "en\x1B" "B";
   auto encoded = utf8_to_pdf_string(input);
   auto decoded = encoded.value().to_utf8();
   // Both succeed, but decoded.value() == "AB", not input.
   ```
   This is a source-traced reproduction, not an executed additional test. Literal ESC is an uncommon input, hence P3. The encoder needs an explicit handling policy and regression coverage rather than silently reporting a successful, lossy conversion.

### consistency: approve

No findings.

### integration: approve

No additional findings.

### source-defect: needs-attention

Finding **1 (P3)** also applies here: Unicode PDF text strings reserve these sequences for language metadata. The encoder must account for that syntax when accepting plain UTF-8. See [PDF Association’s explanation of Unicode text strings](https://pdfa.org/understanding-utf-8-in-pdf-2-0/).

### design: advisory

Not applicable; the section owns no user surface.

### record: advisory

Verified checkout `21aba18931af3357665fee618cd3f5067bbc03ff`. The existing x64 Release executable passed all six `PdfObject.*` tests, the decryption-key regression, and `Support.AsanActiveWhenEnabled`, which reported `ASan=enabled`.

This corroborates the recorded runtime checkpoint. Warning-free compilation, ARM64 compilation, and correspondence of the existing binary to the current sources were not independently established: rebuilding would modify files. The encoder edge case above is absent from `PdfObject.StringEncoding`.

HIGHEST: P3
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P3): correct. `utf8_to_pdf_string` wrote a literal U+001B into UTF-16BE, where it delimits a language escape, so a round trip could drop text. U+001B has no form in any PDF text string (reserved in the Unicode forms, absent from PDFDocEncoding), so the encoder now writes U+FFFD for it, the policy it already applies to surrogates and values above U+10FFFF; `src/pdf/object.h` states it. `PdfObject.StringEncoding` round-trips `"A\x1Ben\x1BB"` to `A`, U+FFFD, `en`, U+FFFD, `B`, and fails on the round 1 encoder.

Gates after round 2: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 107 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfObject.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 14 tests.`

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T06:24:38Z
- Effort: medium
- Lines: 28
- Digest: 0a0396b5ab34b01c

### adversarial: approve

No actionable findings in the audited implementation at `ab0ae28`.

### consistency: approve

No findings.

### integration: approve

No findings.

### source-defect: approve

No actionable findings.

### design: advisory

Not applicable; this section owns no user surface.

### record: advisory

The existing x64 Release binary passed the exact `--gtest_filter=PdfObject.*` checkpoint: six tests, exit 0. The separate ASan check reported `ASan=enabled`; the dictionary-key decryption regression also passed.

This corroborates the recorded runtime result. Warning-free x64/ARM64 compilation and binary correspondence to HEAD were not independently established: rebuilding would violate the no-file-modification constraint.

HIGHEST: none
OVERALL: approve

## Audit outcome

The stamp on `D01 T01 §2` is confirmed. The audit found defects present since that stamp, not regressions: decryption looking dictionary entries up through the slash-stripping `string_view` overload (`/A` and `/#2FA` collided), the decoder dropping all text after a malformed language escape, and the encoder writing a literal U+001B that decoding then read as an escape. Each was fixed at its owning layer in `21aba18` and `ab0ae28`, with tests that fail on the old code. Dependants of `D01 T01 §2` are not suspect: the public API did not change, and all 107 tests pass in x64 Debug and Release.

Test checkpoint at `ab0ae28`: `python scripts/todo-graph.py resolve "D01 T01 §2"` -> exit 3 (still verified); round 3 approves (`OVERALL: approve`). `D01 T01 §2`'s own checkpoint: `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfObject.*:PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 14 tests.`
