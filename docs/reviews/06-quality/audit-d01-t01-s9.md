# Audit - D01 T01 §9: Standard Security Handler decryption (RC4, AES-128, AES-256 R=2..6 via CNG)

- **Audit section:** `D06 T05 §23` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §9` (`docs/reviews/d01-t01-s9.json`, earlier process)
- **Date:** 2026-10-23
- **Candidate:** the stamped commit `f760ebe` and every later change to the files it owns (`src/pdf/crypto.h`, `src/pdf/crypto.cpp`, `tests/recto-tests/pdf_crypto_tests.cpp`): `bd78922`, `21aba18`, `7f6e3c9`, `f233e09`, `a5a1430`, `f79830e`, `3912140`.

## Checkpoint re-run

`python scripts/todo-graph.py resolve "D01 T01 §9"` -> exit 3 (still verified). The section's own checkpoint at `d3365a9`: `build/build.ps1 -Platform x64 -Configuration Release -EnableASAN -TestFilter PdfCrypto.*` -> exit 0, `0 Warning(s)`, `[  PASSED  ] 10 tests.` (The working tree also held uncommitted JBIG2 changes for `D06 T05 §31` in `src/pdf/filters.cpp`; the crypto sources and their tests were as committed.) The checkpoint holds.

## Completion review: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T10:05:47Z
- Candidate: d3365a93e7cd56b6e26ec695871cdd7bac9387cc
- Effort: medium
- Lines: 70
- Digest: a8c653e2bd60cfaf

The feature is **not complete for a user**, and superiority over Acrobat Pro is **not demonstrated**.

The three candidate files match `d3365a9`. I ran the existing x64 Release executable: all 10 `PdfCrypto.*` tests passed, `Support.AsanActiveWhenEnabled` reported ASan enabled, and `PdfDocument.OpenAndAuthenticate` passed. I did not rebuild; these results establish the existing executable’s behaviour, while the findings below come from source inspection.

**Part 1: completeness**

1. **present:** Standard-handler parsing and authentication machinery for R2 to R6. Evidence: [crypto.cpp:1139](/R:/GitHub/Recto/src/pdf/crypto.cpp:1139), `SecurityHandler::authenticate` at line 1461, and `PdfCrypto.ParseEncryptDict`. This does not establish complete interoperability coverage, see item 8.

2. **present:** User-password and owner-password unlocking followed by readable strings and streams for the R3, R4 and R6 corpus files. `PdfCrypto.CorpusRc4AndAes128AndAes256UnlockAndDecrypt` checks exact plaintext, both credentials and object caching. `PdfDocument.OpenAndAuthenticate` covers the public document API.

3. **present:** Automatic opening when the user password is empty, while retaining encrypted status and permission flags. Evidence: [crypto.cpp:1366](/R:/GitHub/Recto/src/pdf/crypto.cpp:1366), `PdfCrypto.EmptyUserPasswordAutoUnlock`, and `Document::initialize_xref_and_security`.

4. **missing-in-scope:** A defined, working text-password encoding boundary for R2 to R4. [crypto.cpp:420](/R:/GitHub/Recto/src/pdf/crypto.cpp:420) copies the supplied bytes directly before padding; `Document::authenticate` forwards its string unchanged. Consequently, passing UTF-8 `café` hashes different bytes from the required PDFDocEncoding password. R5 and R6 explicitly interpret the same API input as UTF-8. Add a consistent text-password contract and independently generated non-ASCII user/owner fixtures. The required legacy encoding is documented in [qpdf’s Unicode password explanation](https://qpdf.readthedocs.io/en/stable/cli.html#unicode-passwords). This is a static finding, not a reproduced fixture failure.

5. **missing-in-scope:** Attachment-only authentication scope. The handler records cipher methods but never reads `/AuthEvent`; creation authenticates immediately, and decryption requires authentication even for Identity content. Evidence: [crypto.h:78](/R:/GitHub/Recto/src/pdf/crypto.h:78), [crypto.cpp:1287](/R:/GitHub/Recto/src/pdf/crypto.cpp:1287), lines 1384 and 1681. A document whose public pages are readable but whose attachments require `/EFOpen` authentication needs those pages to open without the attachment password. Engine support belongs here; the attachment prompt belongs to `D02 T03 §8`.

6. **missing-in-scope:** Equivalent encryption treatment when a stream’s `/Type` is indirect. [crypto.cpp:1903](/R:/GitHub/Recto/src/pdf/crypto.cpp:1903) uses direct `get_name("Type")`, despite having a resolver for other stream parameters. Thus an indirect `/Metadata` type misses the `/EncryptMetadata false` exemption, and an indirect `/EmbeddedFile` type misses `/EFF` selection. Resolve the type before choosing the encryption treatment and add direct-versus-indirect equivalence tests. These are static findings.

7. **present:** Typed refusals and basic hostile-input containment. Unsupported handlers, wrong passwords, damaged `/Perms`, truncated AES ciphertext and allocation failures have typed paths. Recursion and reference cycles are checked; decoded object-stream caching is bounded. Evidence: `PdfCrypto.ParseEncryptDict`, `SaslprepAndTruncatedAesCiphertext`, `OutOfMemoryFaultInjection`, and [crypto.cpp:1844](/R:/GitHub/Recto/src/pdf/crypto.cpp:1844). Cancellation remains a separate gap below.

8. **missing-in-scope:** Regression coverage for the full advertised interoperability boundary. The full-file matrix at [pdf_crypto_tests.cpp:155](/R:/GitHub/Recto/tests/recto-tests/pdf_crypto_tests.cpp:155) contains R3, R4 and R6 only. R2 has dictionary parsing coverage; R5 has limited synthetic authentication coverage. Missing proof includes complete R2/R5 user-and-owner opening and plaintext recovery, non-ASCII legacy passwords, attachment-only security, metadata exemptions, `/EFF`, and corrupted padding in both AES modes. Add independent expected plaintext and permission assertions, rather than round trips that can reproduce the same implementation mistake.

9. **present:** Structured reporting of the file’s permission bits and authentication role. Evidence: [crypto.h:38](/R:/GitHub/Recto/src/pdf/crypto.h:38), `PdfCrypto.PermissionsBitmask`, and `DocumentDescription` population at `document.cpp:1310`. These are engine facts, not proof that application commands enforce them.

10. **missing-out-of-scope:** The actual actor-owned, asynchronous opening/authentication lifecycle and hand-off to viewing, search, OCR and conversion. `src/core/core.cpp` remains a scaffold; the candidate contains no runtime integration. Owner: `D01 T02 §8` and `§10`, with consumer integration in domains 02, 03 and 04. Tests must show no engine work on the UI thread, no shared document instance and clean shutdown during pending authentication.

11. **missing-out-of-scope:** User entry points: File > Open, toolbar Open, `Ctrl+O`, the `Ctrl+K` Open entry, recent-file reopening, drag-and-drop and Explorer/CLI activation. Automation names and tooltips must accompany the relevant controls. Owners: `D02 T01 §§5-7, §9` and `D02 T03` for recents and command registration. `src/app/main.cpp:1` explicitly leaves screens and commands to D02.

12. **missing-out-of-scope:** Password dialog and all applicable states: initial focus, submission/loading, disabled controls with a reason, wrong-password retry, successful unlock, cancellation and teardown. Include Enter, Escape, keyboard traversal, screen-reader announcements, scaling and visual tests. Owner: `D02 T01 §10`, whose contract already requires `Shell.PasswordDialogFlow` and password-dialog golden images.

13. **missing-out-of-scope:** User-visible error recovery, security status and permission refusals. Users need a distinction between wrong password, unsupported security, damage and operating-system access failure; readable security/permission information; and explanations when copy, conversion or printing is refused. Owners: `D02 T01 §11`, `D02 T03 §9`, and the operation owners in D03, D04 and D07. The engine exposes facts; these surfaces are not delivered by this candidate.

14. **missing-out-of-scope:** Certificate-protected PDFs and their credential/refusal paths. Owner: `D05 T01`, as explicitly excluded from this candidate.

15. **present:** Operating-system read failures have an owning-layer route. `Document::read_file_bytes` maps missing files, access denial and other I/O failures to typed errors at [document.cpp:1081](/R:/GitHub/Recto/src/pdf/document.cpp:1081). `PdfDocument.OpenAndAuthenticate` tests missing files. Locked-file and access-denied UI recovery still require item 13’s integration tests.

16. **present:** Read-only operation requires no undo command, encryption setting or saved plaintext output. The handler changes in-memory values and caches; opening reads the source again. There is no feature-owned disk write requiring temp-file-then-replace, and full-disk recovery is not an applicable decryption operation. Saving decrypted copies and encrypted crash-recovery snapshots belong to their writer and shell sections.

**Part 2: excellence**

**Comparator: Adobe Acrobat Pro**, the first benchmark in the requested order. Adobe documents separate opening and permissions passwords, attachment-only protection, and accessibility treatment of secured documents. Sources: [password security overview](https://helpx.adobe.com/acrobat/desktop/protect-documents/protect-with-passwords/password-security-overview.html), [Acrobat’s attachment-only protection documentation](https://helpx.adobe.com/archive/acrobat/X/standard/acrobat_X_standard_help.pdf), and [accessibility features](https://helpx.adobe.com/ca/acrobat/using/accessibility-features-pdfs.html). These establish behaviour, not current comparative timings. I did not run Acrobat, Foxit, PDFelement or UPDF, so I cannot establish an empirical winner among them.

1. **in-scope:** Compatibility falls short of a credible Acrobat challenge. The encoding, attachment-authentication and indirect-type gaps in Part 1 can prevent correct opening or decryption. **Target:** every independently generated R2 to R6 fixture opens with its intended user and owner credentials, produces exact expected plaintext, and treats equivalent direct/indirect forms identically. Attachment-only files require zero password submissions to view public pages.

2. **in-scope:** Cancellation cannot interrupt the delivered crypto operations. `authenticate`, `decrypt_bytes` and recursive object decryption expose no cancellation input; CNG processes entire supplied buffers. Moving this work to an actor alone cannot interrupt it. **Target:** cooperative cancellation acknowledged within 100 ms on the reference machines during large-stream decryption and recursive traversal, with no partially decrypted result cached. Verify subsequent retry succeeds. This is a responsiveness target, not a measured claim about Acrobat’s cancellation latency.

3. **in-scope:** There is no comparative performance proof. Providers are opened repeatedly in hashing and cipher helpers, including during iterative authentication, but the existing small-fixture test timings do not establish a user-visible disadvantage or advantage. Evidence: `crypto.cpp:114`, `:211` and Algorithm 2.B. **Target:** benchmark correct, wrong and empty-password authentication plus large-stream decryption; record median/p95 time and peak memory. Require a measured improvement on a meaningful workload with no material regression elsewhere before claiming superiority. Preserve the existing caching and correctness guarantees.

4. **out-of-scope:** Recto has no delivered user journey to compare for steps, defaults or recovery. The common opening workflow must actually reach the handler. **Target:** owner-only files add zero dialogs; protected files need one password entry and one submission after opening; a wrong password retries in place; Cancel leaves zero pending tabs or actors; unrelated tabs survive errors. Owners: `D02 T01 §§3,5,10,11`, with command/recents integration in `D02 T03`. Retain Recto’s already specified inline-retry improvement.

5. **out-of-scope:** Accessibility and polish cannot yet compete at the user surface. Engine permission flags do not provide an accessible password flow, understandable refusals or usable security information. **Target:** complete keyboard operation, announced errors and progress, named controls, correct focus restoration, high-contrast support and passing 100/150/200 percent golden checks. Assistive-technology access must follow the applicable PDF rules rather than treating a raw permission bit as a universal prohibition. Owners: `D02 T01 §10`, `D02 T03 §9` and D03 accessibility integration.

**Consolidated gaps**

1. **missing-in-scope:** Define and implement consistent text-password encoding for R2 to R4, with non-ASCII user/owner regressions.
2. **missing-in-scope:** Honour attachment-only `/AuthEvent /EFOpen` authentication scope in the engine.
3. **missing-in-scope:** Resolve indirect stream `/Type` before metadata exemptions and `/EFF` selection.
4. **missing-in-scope:** Complete the independent interoperability and malformed-input regression matrix.
5. **missing-out-of-scope:** Deliver actor-owned asynchronous authentication and consumer hand-off, `D01 T02 §§8,10` and domains 02 to 04.
6. **missing-out-of-scope:** Deliver all Open entry points, commands, recents, names and tooltips, `D02 T01` and `D02 T03`.
7. **missing-out-of-scope:** Deliver the password dialog, states, accessibility and teardown tests, `D02 T01 §10`.
8. **missing-out-of-scope:** Deliver error recovery, security information and operation-specific permission refusals, D02, D03, D04 and D07.
9. **missing-out-of-scope:** Deliver certificate security, `D05 T01`.
10. **in-scope:** Establish exact interoperability parity across the advertised revisions and crypt-filter behaviours.
11. **in-scope:** Add cooperative crypto cancellation with a measured 100 ms acknowledgement target.
12. **in-scope:** Supply comparative latency and memory measurements before claiming performance superiority.
13. **out-of-scope:** Prove the minimal-step opening, retry and cancellation journey, `D02 T01` and `D02 T03`.
14. **out-of-scope:** Prove accessible, polished password/security surfaces and assistive-technology permission handling, D02 and D03.

COMPLETION: gaps

## Round 1: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T10:10:38Z
- Effort: medium
- Lines: 22
- Digest: 19f9e4f3733e6d62

## adversarial: approve

## consistency: approve

## integration: needs-attention

1. **[P2] Indirect stream `/Type` bypasses encryption exemptions and `/EFF` selection.** `src/pdf/crypto.cpp:1903` reads `/Type` only through `get_name()`, which does not resolve references. Consequently, line 1920 decrypts cleartext metadata, and line 1927 misses embedded-file treatment. Reproduce with an encrypted PDF containing `/EncryptMetadata false` and a cleartext XMP stream whose dictionary has `/Type 20 0 R`, where object 20 is `/Metadata`. Resolve the stream through `SecurityHandler::resolve_and_decrypt_object`: AES rejects its cleartext as ciphertext, while RC4 corrupts it. The equivalent direct `/Type /Metadata` preserves the bytes. The same reference form for `/EmbeddedFile`, with differing `/EFF` and `/StmF`, selects the wrong cipher. Indirect values are permitted by ISO 32000-1 §7.3.10.

## source-defect: needs-attention

2. **[P2] V4 ignores the crypt filter’s key length.** `src/pdf/crypto.cpp:1210` defaults V4 to 128 bits, while line 1309 records only `/CFM` and never reads the filter’s `/Length`. Reproduce by generating a valid V4/R4 RC4 document with `/CF << /StdCF << /CFM /V2 /Length 5 >> >>`, `/StmF /StdCF`, `/StrF /StdCF`, and no top-level `/Length`. Generate its `/O`, `/U` and object encryption using the specified 40-bit key. Opening it with the correct password derives a 128-bit candidate key and returns `WrongPassword`. The Standard handler’s crypt-filter length is expressed in bytes, as specified in [ISO 32000-1 Table 25](https://raw.githubusercontent.com/adobe/dc-acrobat-sdk-docs/master/docs/standards/pdfstandards/pdf/PDF32000_2008.pdf).

3. **[P2] R5/R6 authentication gives user credentials precedence over matching owner credentials.** `src/pdf/crypto.cpp:1573` skips owner validation whenever `/U` matches, then line 1657 reports `unlocked_as_owner_ = false`. Reproduce with an AES-256 PDF generated with identical user and owner passwords and restrictive `/P` flags. Call `Document::open(bytes, password)` and inspect `description().unlocked_as_owner`: Recto reports user access despite the supplied password also being the owner password. Algorithm 2.A tests the owner credential first; see [ISO 32000-2 Algorithm 2.A](https://developer.adobe.com/document-services/docs/assets/5b15559b96303194340b99820d3a70fa/PDF_ISO_32000-2.pdf). This also affects passwords that become identical after SASLprep and truncation.

## design: approve

## record: advisory

The findings are source-derived; their proposed reproductions were not executed. Verification used the existing x64 Release executable with `PdfCrypto.*:Support.AsanActiveWhenEnabled:PdfDocument.OpenAndAuthenticate`. No rebuild was performed, so that execution does not establish candidate build provenance.

HIGHEST: P2
OVERALL: needs-attention

Disposition of the completion review's gaps (each verified against the code before acting):

- Implemented: gap 1, a non-ASCII password for R=2 to R=4: the legacy algorithms hash PDFDocEncoding bytes (ISO 32000-1 7.6.3.3, Algorithm 2 step a), so a UTF-8 password is tried in PDFDocEncoding first, then as given. Commit `9ba7cc9`.
- Implemented: gap 3, an indirect stream `/Type` resolves one level before the `/XRef`, `/Metadata` and `/EmbeddedFile` rules (also round 1, finding 1). Commit `9ba7cc9`.
- Filed: D06 T05 §46 (gap 2, attachment-only `/AuthEvent /EFOpen` scope; the prompt belongs to D02 T03 §8)
- Filed: D06 T05 §47 (gaps 4, 10, 11 and 12: the R=2 to R=6 interoperability matrix with independently generated files, cancellable decryption, measured cost)
- Filed: D02 T01 §10 (gaps 7 and 14, the password dialog, its states and accessibility, existing owner)
- Filed: D01 T02 §8 (gap 5, actor-owned asynchronous authentication, existing owner with D01 T02 §10)
- Filed: D02 T03 §8 (gap 8 in part, attachment prompts, existing owner)
- Filed: D05 T01 §12 (gap 9, certificate security, existing owner with D05 T01 §11)
- Gaps 6 and 13 (open entry points and the opening journey) are the planned work of `D02 T01` and `D02 T03`.

## Fixes after round 1

- Finding 1 (P2): correct, fixed as completion gap 3 above. `PdfCrypto.AuditOwnerFirstEncodingLengthAndIndirectType` decrypts a stream whose `/Type` is a reference to `/XRef` and leaves it unchanged.
- Finding 2 (P2): correct. A `/V 4` crypt filter's `/Length` now sets the key length when the `/Encrypt` dictionary has none (in bytes as Table 25 says, bits accepted as for `/Length`); tested with `/CFM /V2 /Length 5` -> 40 bits.
- Finding 3 (P2): correct, and R=2 to R=4 had the same order. Both families now check the owner password first (ISO 32000-2 Algorithm 2.A), so a password that is both unlocks as the owner; tested in R=3 and R=6 with vectors computed independently by `scripts/build-corpus.py`'s own crypto code.

## Round 2: sol (`gpt-6.1-sol` via `codex`)

- Writer: claude
- Run: 2026-10-23T10:58:40Z
- Effort: medium
- Lines: 20
- Digest: 0aafb1bdaf5a053d

## adversarial: needs-attention

1. **[P1] Indirect filters permit exponential memory and CPU expansion.** `src/pdf/crypto.cpp:1982` accepts any resolved value, and line 1992 inlines every filter-array element before checking that it names a filter. Reproduce with an empty-password encrypted PDF using Identity encryption: point a page’s `/Contents` at stream S0, and give each S0 through S9 `/Filter [next next next next next next next next]`, with all eight references targeting the next stream. S10 has no filter; all streams can have empty bodies. Resolving the content through `Document::resolve_object` recursively constructs eight copies of each subtree, approaching `8^10` leaf values before decoding can reject the malformed filters. The depth and eight-filter limits do not bound this expansion. The separate SecurityHandler resolver also clones these expanded values into its cache. A small hostile PDF can exhaust memory or monopolize processing.

## consistency: approve

## integration: needs-attention

2. **[P2] Valid legacy passwords beginning with BOM-like bytes cannot authenticate through UTF-8 input.** `src/pdf/crypto.cpp:1484` uses the PDF *text-string* encoder, whose BOM-collision protection changes representable PDFDocEncoding bytes into UTF-16BE. Lines 1486-1490 then discard that candidate. Reproduce with a V2/R3 PDF whose user password is `þÿ`, generated using password bytes `FE FF`, and a distinct owner password. Call `Document::open` with UTF-8 bytes `C3 BE C3 BF`: the converted candidate is discarded, and the remaining raw UTF-8 candidate returns `WrongPassword`. Supplying raw `FE FF` succeeds. Password conversion needs PDFDocEncoding bytes without text-string BOM handling.

## source-defect: approve

## design: approve

## record: advisory

Both findings are source-derived; the proposed reproductions were not executed. The existing x64 Release executable passed 13 selected tests (`PdfCrypto.*`, `PdfDocument.OpenAndAuthenticate`, and `Support.AsanActiveWhenEnabled`), with ASan enabled. No rebuild was performed, so this does not establish candidate binary provenance. The three reviewed working-tree files match `9ba7cc9`. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P1): correct. Writing an indirect `/Filter` in resolved whatever the reference named, a stream included, and the resolver decrypted that stream, inlining its own references in turn, so a chain of streams each naming the next eight times grew as 8^n. A value is now written in only when it has the kind the slot uses (a name for `/Filter` and `/Name`, a dictionary or array for `/DecodeParms`), and a decryption nested inside that resolution does not inline again. Tested through `Document::open_memory` with five such streams: refused as damaged in 21 ms with under 4 MiB allocated (5,355 ms and 234,720,508 bytes on `9ba7cc9`).
- Finding 2 (P2): correct. The legacy password conversion used the text-string encoder, which turns a leading `þÿ` into UTF-16BE to avoid a byte-order-mark collision. It now uses `utf8_to_pdf_doc_encoding` (exact PDFDocEncoding, no BOM handling). Tested: a V2/R3 user password `FE FF` authenticates from UTF-8 `C3 BE C3 BF` (refused on `9ba7cc9`).
