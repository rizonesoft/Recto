## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T17:52:38Z
- Effort: high
- Lenses: adversarial, integration, source-defect, plan-delivery
- Lines: 24
- Digest: 475633e0507a65ee

### adversarial: needs-attention

1. **P2: The length-discovery pass decodes encrypted containers before authentication.** At `src/pdf/xref.cpp:1802`, any recovered stream with an indirect `/Length` enables another container pass. Unlike the earlier pass, this one ignores the encryption guard and calls `decode_stream` at `:1933`, then parses ciphertext as object-stream members at `:1942`.

   Reproduce through `Document::open_memory` with an encrypted PDF containing an intact xref stream, an object stream with an indirect `/Length`, and a damaged final `startxref`. Recovery attempts to decode its ciphertext. For a concrete opening failure, give that container an indirect `/N` whose object has 17 surviving definitions from incremental updates. The 16-reading limit sets `readings_incomplete`, and recovery returns `DamagedFormat` at `:2129` before trying the correct password. These containers should remain deferred to authenticated resolution.

### integration: needs-attention

2. **P1: Compressed-root acceptance relies on a validation check that accepts invalid types.** `src/pdf/xref.cpp:2088` accepts an encrypted compressed root without inspecting it, promising validation after authentication. However, `src/pdf/document.cpp:2002` rejects only a successfully resolved name different from `Catalog`. Missing, non-name and unsuccessfully resolved `/Type` values pass.

   Reproduce using the builder in `tests/recto-tests/pdf_document_tests.cpp:268`, with catalog payload `<< /Type 42 /Pages 32 0 R >>`, then call `Document::open_memory(bytes, "recto-test")`. The recovered document opens instead of returning `DamagedFormat`. Omitting `/Type` also passes. The existing negative case at `:325` tests only `/Type /Pages`, which cannot detect this hole.

### source-defect: needs-attention

Finding **2 (P1)** violates the catalog requirement: `/Type` is required and must be the name `/Catalog`. Treating a non-name or resolution failure as an acceptable missing type does not satisfy that requirement. [ISO 32000-1, §7.7.2, Table 28](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf)

### plan-delivery: needs-attention

Findings **1 (P2)** and **2 (P1)** leave two explicit requirements incomplete: no encrypted-container decoding during recovery, and typed refusal of a recovered compressed root that is not a catalog. The encryption regression at `tests/recto-tests/pdf_xref_tests.cpp:1509` uses direct lengths exclusively, so it misses the second decoding pass.

These findings are source-traced, not executed reproductions. Existing binaries did not establish candidate verification: Release omitted the new compressed-catalog test; Debug included it but failed its root-selection and refusal assertions. Both reported ASan disabled. Their correspondence to the candidate was not established. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-25T18:02:12Z
- Effort: medium
- Lenses: adversarial
- Lines: 8
- Digest: a86257342c02c5aa

### adversarial: approve

No new in-scope findings. The length-candidate behavior explicitly accepted in this request is not re-raised.

Source review only; no tests or ASan runs performed. No files modified.

HIGHEST: none
OVERALL: approve
