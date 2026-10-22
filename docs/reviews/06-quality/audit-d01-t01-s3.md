# Audit - D01 T01 §3: Binary-safe PDF 1.7 and 2.0 lexer (src/pdf/lexer.cpp)

- **Audit section:** `D06 T05 §17` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §3` (`docs/reviews/d01-t01-s3.json`, earlier process)
- **Date:** 2026-10-22

## Checkpoint re-run

At `373a07b`: `powershell -File build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfLexer.*` -> exit 0, `0 Warning(s)`, `Staged for recto-tests: clang_rt.asan_dynamic-x86_64.dll`, `[  PASSED  ] 7 tests.` (7 tests from 1 suite). The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-22T06:28:35Z
- Effort: medium
- Lines: 28
- Digest: c6e6e90477acb5bf

## adversarial: needs-attention

1. **P2: Valid PDF 2.0 names containing braces are split incorrectly.** `src/pdf/lexer.cpp:523` ends a name whenever `is_pdf_regular()` rejects a byte; `src/pdf/lexer.h` unconditionally classifies `{` and `}` as delimiters. Reproduce with `/A{B}`: the lexer produces Name `A`, BraceOpen, Keyword `B`, BraceClose instead of Name `A{B}`. This is a source-traced reproduction, not an executed custom test.

## consistency: approve

No findings.

## integration: needs-attention

Finding **1 (P2)** propagates into `src/pdf/parser.cpp:517`: parsing `<< /A{B} 1 >>` encounters BraceOpen where the dictionary value should begin and returns `DamagedFormat`. Thus the lexer mismatch rejects an otherwise valid PDF 2.0 dictionary.

## source-defect: needs-attention

Finding **1 (P2)** also affects the contract at `todo/01-engine/TODO-01-bridge-runtime.md:143`: it requires braces to be delimiters without qualifying the context. The [PDF Association’s ISO 32000-2 syntax errata](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html#7.2.2) restricts these additional delimiters to Type 4 PostScript calculator functions. Both the contract and implementation need that distinction.

## design: advisory

Not applicable; this section owns no user surface.

## record: needs-attention

2. **P3: The literal-string completion claim exceeds its test coverage.** `todo/01-engine/TODO-01-bridge-runtime.md:145` claims that `PdfLexer.LiteralStrings` matches **all** ISO examples byte-for-byte. The test at `tests/recto-tests/pdf_lexer_tests.cpp:273` substitutes selected examples and omits the two complete trailing-newline examples and the full mixed-text octal example. Reproduce by comparing that test with [ISO 32000-1 §7.3.4.2, examples 3 and 4](https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/pdfstandards/pdf/PDF32000_2008.pdf). This is an evidence gap, not a demonstrated decoding failure.

Checkpoint verification was limited to the existing executable: `build/bin/x64/Release/recto-tests/recto-tests.exe --gtest_filter=PdfLexer.*:Support.AsanActiveWhenEnabled` passed eight tests and reported `ASan=enabled`. No fresh build was performed under the no-file-modification constraint, so current-source build provenance and zero compiler warnings remain unverified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P2): correct. ISO 32000-2 section 7.2.2, as clarified by the PDF Association's errata, makes `{` and `}` delimiters only inside a Type 4 (PostScript calculator) function. `Lexer::is_pdf_delimiter` now holds only the general delimiters; `LexerSyntax` (`Pdf` by default, `PostScriptCalculator` through `Lexer::set_syntax`) decides whether braces are tokens, and names and keywords stop at a brace only in calculator syntax. Nothing in the engine reads calculator functions yet; the mode is there for the function evaluator. `PdfLexer.BracesDelimitOnlyInCalculatorSyntax` lexes `/A{B}` as one name in PDF syntax, tokenizes `{ 2 mul {x}if }` in calculator syntax, and parses `<< /A{B} 1 >>` into key `A{B}`. With braces tokenized in both syntaxes again, it fails. `PdfLexer.Tokens` now expects `{}endobj` as one keyword. The keyword boundary checks in `xref.cpp`, `document.cpp` and `parser.cpp` use `is_pdf_regular`, so they follow the same rule. The contract line in `D01 T01 §3` carries a dated correction.
- Finding 2 (P3): correct. `PdfLexer.LiteralStrings` now includes section 7.3.4.2 examples 3 and 4 as printed (an end-of-line kept raw and as `\n`, and octal escapes inside text), and the correction note records that the earlier claim was short of the test.

Gates after round 1: `.\build.cmd` -> exit 0, `0 Warning(s)` in all four configurations, `[  PASSED  ] 108 tests.` for x64 Debug and Release; `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfLexer.*:PdfParser.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 16 tests.`; `python scripts/todo-graph.py validate` -> `valid: 681 sections`.
