## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T22:20:01Z
- Effort: high
- Lenses: adversarial, integration, source-defect, plan-delivery
- Lines: 24
- Digest: 76b699943fde7763

**adversarial: needs-attention**

1. **P1**, `src/pdf/filters.cpp:2464`: `/Rows` still bypasses refusal of a damaged row when no resynchronization EOL follows. Open a PDF image using `/CCITTFaxDecode` with `/K 0 /Columns 16 /Rows 1 /EndOfLine true /EndOfBlock true /DamagedRowsBeforeError 1` and stream bytes `001b16c00404`. This encodes a good row followed by an EOL and an invalid code. The candidate returns success with `F0 0F`, because `rows_done_without_recovery` returns true before recovery runs. It must return `DamagedFormat`: the damaged row has no EOL to resume at. The same failure occurs with `/K 2` and bytes `001d8b600301`. Setting the allowance to zero still succeeds.

**integration: needs-attention**

2. **P1**, `src/pdf/filters.cpp:2587`: Recovery starts after the mixed-mode tag, so a damaged row missing its tag consumes the first zero of the next valid row’s EOL. Recovery then skips that row entirely.

   Reproduce through a PDF image with `/K 2 /Columns 16 /Rows 3 /EndOfLine true /DamagedRowsBeforeError 1`, default `/EndOfBlock true`, and bytes `001d8b6002003a8006003001800c006003`. These encode a patterned row, an empty row without its tag, a valid white row, then RTC. Expected output is `F0 0F F0 0F FF FF`; the candidate returns `F0 0F F0 0F`. The next valid EOL begins at bit 39, but recovery begins at bit 40 and finds the RTC instead. Preserve the boundary before reading the possibly damaged tag.

**source-defect: needs-attention**

3. **P2**, `src/pdf/filters.cpp:2475`: `/EndOfLine false` is treated as meaning EOLs are absent, although Table 11 requires accepting them regardless of that flag. Open a PDF image with `/K 0 /Columns 16 /Rows 2 /EndOfBlock false /EncodedByteAlign true /EndOfLine false` and bytes `000198a001a8`. Expected output is `FF 00 FF FF`. After the first row ends at bit 27, alignment advances to bit 32 and cuts into the next EOL, causing refusal. The same stream decodes with `/EndOfLine true`. See [ISO 32000-1 Table 11](https://www.pdftron.com/downloads/pdfref.pdf#page=39).

4. **P2**, `src/pdf/filters.cpp:2475` interacting with `:1780`: Removing alignment exposes the EOL scanner’s 64-zero cutoff and rejects previously accepted, valid T.4 fill. Use the preceding parameters with `/EndOfLine true` and bytes `000198a00000000000000001a8`. Expected output remains `FF 00 FF FF`. The second EOL has 57 fill zeros plus its 11 zeros and ends at bit 96. Previously, alignment skipped five zeros and the scanner accepted the remaining 63. Now it rejects the 68-zero run and reports damage. T.4 permits variable-length zero fill; the scanner must handle it within an input/work bound. See [T.4 §4.1.3](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-T.4-199904-S%21%21PDF-E&lang=e&type=items).

**plan-delivery: needs-attention**

1. **P1**, `tests/recto-tests/pdf_filters_tests.cpp:7084`: The claimed no-EOL refusal coverage leaves finding 1 undetected. Every mixed-mode case sets `/Rows 2` after only one good row, so it never exercises refusal after `/Rows` is satisfied. Repeat those fixtures with `/Rows 1 /EndOfBlock true`; they currently succeed, contradicting the checked requirement at `todo/06-quality/TODO-05-stamp-audits.md:1017`.

Evidence: source inspection and in-memory bit-position traces. The existing Debug executable passed all six `PdfFilters.Ccitt*` tests; it was not rebuilt, and the new fixtures were not executed natively. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Writer note: round 1 dispositions

- Fixed: adversarial 1 and plan-delivery 1 (damage after `/Rows` rows with no EOL after it returned success): `rows_done_without_recovery` now knows whether an EOL introduced the row; under `/EndOfBlock true`, `/EndOfLine true` and K non-negative such a row goes to recovery, which resumes at the next EOL or refuses. Both reproductions (K 0 and K 2, allowance 1 and 0) are refused as `DamagedFormat`.
- Fixed: integration 2 (a mixed-mode row missing its tag swallowed the next EOL's first zero): the row's start is taken before its tag, for recovery and for the end-of-rows check. The reproduction decodes to `F0 0F F0 0F FF FF`.
- Fixed: source-defect 3 (`/EndOfLine false` with T.4-filled EOLs under `/EncodedByteAlign`): a first row that starts with an EOL marks the stream as one with EOLs (Table 11 accepts them whatever `/EndOfLine` says), and then no alignment precedes the EOL scan. The reproduction decodes to `FF 00 FF FF`.
- Fixed: source-defect 4 (fill over 64 zeros rejected): `try_consume_eol` takes fill of any length, and recovery finds the next EOL with `find_eol`, one pass, instead of trying `try_consume_eol` at every bit (which the cap had kept linear). The reproduction decodes to `FF 00 FF FF`; a damaged row followed by a MiB of zeros is refused in 34 ms.
- Mutants killed: each fix reverted (`after-eol-ignored`, `tag-boundary`, `eol-rows-declared-only`, `fill-cap`), and the per-bit scan without a cap, which hung the test (stopped after 527 CPU seconds against 34 ms for the real code).

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T22:39:38Z
- Effort: medium
- Lenses: adversarial
- Lines: 28
- Digest: aed402f9e660f320

## adversarial: needs-attention

1. **P1**, `src/pdf/filters.cpp:2462`: `/EndOfLine false` bypasses damaged-row refusal after `/Rows` is satisfied, despite `/EndOfBlock true`. The helper returns true before checking `after_eol`, so the caller silently returns truncated output instead of reporting recovery is off.

   Reproduce by opening a PDF containing a 16 × 3 monochrome image with `/CCITTFaxDecode` and:

   ```text
   /DecodeParms <<
     /K 0 /Columns 16 /Rows 1
     /EndOfLine false /EndOfBlock true
     /DamagedRowsBeforeError 0
   >>
   ```

   Use stream bytes:

   ```text
   001b16c00404006a001001001001001001
   ```

   These encode a patterned row, an EOL-introduced damaged row, a white row and RTC. The candidate returns success with only `F0 0F`. Expected: `DamagedFormat` with `DamagedRowRecoveryOff`. EOLs remain accepted with `/EndOfLine false`; disabling recovery cannot make identifiable damage a successful end of image.

   Remove the recovery-eligibility shortcut for EOL-introduced rows when `/EndOfBlock true`. Extend the existing recovery-off regression to reach damage after `/Rows` is satisfied.

Evidence: candidate source inspection and bit-position tracing. The fixture was not executed natively; no files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Writer note: round 2 dispositions

- Fixed: adversarial 1 (with `/EndOfLine false`, an EOL-introduced damaged row after `/Rows` rows ended the image as a success): under `/EndOfBlock true` a row an EOL introduced is a row whatever `/EndOfLine` says, so its damage goes to recovery, which refuses it as `DamagedRowRecoveryOff`; the eligibility check now comes after that. The reproduction (its bytes asserted) and its mixed Group 3 twin are refused with that reason in `PdfFilters.CcittRecoveryRoundOne`. Mutant killed: the recovery-off shortcut restored before the EOL check.
