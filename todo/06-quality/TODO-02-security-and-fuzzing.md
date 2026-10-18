---
schema_version: 1
id: security-and-fuzzing
domain: 06-quality
status: active
title: "TODO-02 -- Hostile-Input Rules, Fuzz Operations and the Broker Design"
track: M6
---

# TODO-02 -- Hostile-Input Rules, Fuzz Operations and the Broker Design

> **Goal:** Enforce foundation plan section 9 as an auditable engineering system: verify every hostile-input defense across `recto-pdf`, `recto-core`, `recto-services`, and `Recto` (`docs/security-audit.md`), build the M3-M5 fuzz targets (`fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) and run continuous scheduled fuzzing across all six targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) with a proven crash-to-regression triage loop (`fuzz/README.md`), inventory the as-built `src/pdf/recto_pdf.h` public boundary, and author the post-v1 AppContainer broker process design specification (`docs/specs/2026-10-14-broker-process-design.md`) resolving foundation plan section 11 item 5.

> **Current state:** Measured 2026-10-14. No code exists yet. The rules are defined in foundation plan section 9 and decisions D12 and D13: every PDF, Office, and HTML file is treated as hostile; `recto-pdf` contains zero JavaScript interpreter (`/JS` and `/JavaScript` actions are ignored by construction); Office macros are ignored by the built-in OpenXML parser (`vbaProject.bin` skipped) and forced off on external Office COM (`msoAutomationSecurityForceDisable`) and LibreOffice (`MacroSecurityLevel=3`) adapters; HTML `<script>` and remote network fetches are blocked; external links and portfolio attachments ask before opening; PDF `/Launch` actions never execute; physical redaction strips underlying glyphs, vector paths, and image pixels before drawing overlays; and v1 parses in-process behind a clean `recto-pdf` C++20 boundary designed to move into a restricted AppContainer broker process after v1.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D12, D13), 3.1 (`recto-pdf` boundary), 3.2 (`fuzz/`), 4.11 (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`), 9 (security and untrusted-input posture), and 11 item 5 (broker process design).
- -> XREF: D01 T01 §4 -- the 64-level recursive-descent parser and object-graph cycle bounds audited in `§1`.
- -> XREF: D01 T01 §7 -- the 256 MB stream decompression-bomb ceiling and filter bounds audited in `§1`.
- -> XREF: D01 T01 §10 -- the `recto-pdf` public API header (`src/pdf/recto_pdf.h`) and action reader (`/URI` surfaced, `/JS` and `/Launch` ignored) audited in `§1` and inventoried in `§5`.
- -> XREF: D01 T03 §4 -- the three M1 fuzz targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`) extended with `fuzz_text`, `fuzz_forms`, and `fuzz_openxml_zip` and operated continuously in `§3` and `§4`.
- -> XREF: D01 T03 §5 -- the M1 1-hour fuzz pass and CI 60-second smoke gate extended into scheduled 6-target runs in `§3`.
- -> XREF: D02 T02 §9 -- the viewer external URI confirmation dialog and `/Launch` / `/JavaScript` refusal audited in `§1` and `§2`.
- -> XREF: D03 T01 §1 -- the `PageText` extraction and CMap/ToUnicode decoder targeted by `fuzz_text.cpp` in `§3`.
- -> XREF: D03 T03 §6 -- the AcroForm `/Fields`, `/V`, and `/AP` appearance generator targeted by `fuzz_forms.cpp` in `§3`.
- -> XREF: D04 T04 §1 -- the `ZipReader` and OpenXML `.docx`/`.xlsx`/`.pptx` XML parser targeted by `fuzz_openxml_zip.cpp` in `§3`.
- -> XREF: D04 T04 §5 -- the STA Microsoft Office COM macro-suppression (`msoAutomationSecurityForceDisable` and `macro-canary.docm`) proof re-verified in `§2`.
- -> XREF: D04 T04 §6 -- the LibreOffice headless `CreateProcessW` argv and Job Object containment proof re-verified in `§2`.
- -> XREF: D05 T01 §10 -- the physical content-stream redaction (`Redactor`) and `sanitize_document` proofs re-verified in `§2`.

## Outcome

- Every rule in foundation plan section 9 has both a static/symbol-level proof and an automated `recto-tests` regression test recorded with quoted evidence in `docs/security-audit.md`.
- All six foundation-plan fuzz targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) run under continuous scheduled fuzzing with coverage summaries and corpus delta archiving, and `fuzz/README.md`'s crash triage loop is proven end-to-end on a planted crash input.
- The `src/pdf/recto_pdf.h` public API surface is machine-inventoried against the shipped headers (`§5`), and the post-v1 AppContainer shared-memory broker specification (`docs/specs/2026-10-14-broker-process-design.md`, `§6`) proves that moving `recto-pdf` out-of-process requires zero changes to `DocumentViewCanvas` or `ConversionRunner` callers.

**Adjacency:** all=not-applicable (security assurance audit, continuous fuzzing operations, and post-v1 architecture specification; no user-facing product surfaces)

**Adjacency rationale:** Recto has no user-role permission model; its untrusted-input refusal boundaries (external link confirmation, macro suppression, `/Launch` and `/JS` refusal, physical redaction) are implemented in their owning engine, services, and viewer sections (`D01 T01`, `D02 T02 §9`, `D04 T04`, `D05 T01 §10`) and audited end-to-end here in `§1` and `§2`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Hostile-input audit Part 1 (`docs/security-audit.md`: zero-JS, `/Launch` & bomb bounds) | D01 T01 §10, D02 T02 §9 | [ ] |
| 2 | §2 | Hostile-input audit Part 2 (macro suppression, HTML script block, link prompt, redact) | §1, D04 T04 §5, D04 T04 §6, D05 T01 §10 | [ ] |
| 3 | §3 | M3-M5 fuzz targets (`fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) & scheduled workflow | D01 T03 §5, D03 T01 §1, D03 T03 §6, D04 T04 §1 | [ ] |
| 4 | §4 | Crash triage runbook (`fuzz/README.md`) proven end-to-end on a planted crash input | §3 | [ ] |
| 5 | §5 | `recto-pdf` public API boundary inventory (`recto_pdf.h` ownership and mutability table) | §2 | [ ] |
| 6 | §6 | Post-v1 AppContainer broker process design (`2026-10-14-broker-process-design.md`) | §5 | [ ] |

## 1. Hostile-input audit Part 1 (`docs/security-audit.md`: zero-JS, `/Launch` & bomb bounds)

Audit the PDF engine security rules from foundation plan section 9 and decision D12 and record quoted static, symbol-level, and test evidence in `docs/security-audit.md`:
- Zero JavaScript engine or script interpreter linked anywhere in `recto-pdf.lib`, `recto-core.lib`, `recto-services.lib`, or `Recto.exe` (verified via `dumpbin /symbols` and `dumpbin /dependents`), and PDF `/S /JavaScript`, `/JS`, `/AA`, and `/OpenAction` script dictionaries ignored without execution.
- PDF `/S /Launch`, `/ImportData`, `/SubmitForm`, and `/GoToR` executable actions never invoke `CreateProcessW` or `ShellExecuteExW`.
- Every parser recursion cap (`64` nested arrays/dicts, `16` nested `/Form` XObjects), `/Prev` and xref cycle detector, and stream filter decompression-bomb ceiling (`256 MB` per stream, `1000:1` ratio guard) returns a typed `PdfError` without throwing, aborting, or calling `setjmp`/`longjmp` across `tests/corpus/broken/` and `tests/corpus/seeds/`.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tests/recto-tests/security_audit_tests.cpp` (`SecurityAudit.PdfEngine.*`) and `scripts/check-security-symbols.py` checking `dumpbin /symbols` and `dumpbin /dependents` on `recto-pdf.lib` and `Recto.exe` (verifying zero `setjmp`/`longjmp`, zero JS engine symbols, and zero `ShellExecute`/`CreateProcess` imports inside `recto-pdf.lib`). Done when: `python scripts/check-security-symbols.py` and `recto-tests --gtest_filter=SecurityAudit.PdfEngine.*` pass.
2. Author the Part 1 section of `docs/security-audit.md` recording each PDF engine rule verbatim from foundation plan section 9 alongside its owning section, verification command, and quoted output. Done when: `docs/security-audit.md` is committed with complete Part 1 evidence.

- [ ] Implement `scripts/check-security-symbols.py` inspecting `recto-pdf.lib` via `dumpbin /symbols` to verify: (a) zero references to `_setjmp`, `longjmp`, or C++ exception unwind tables across public boundary functions, (b) zero JavaScript interpreter symbols, and (c) zero imports of `CreateProcessW`, `CreateProcessA`, `ShellExecuteW`, `ShellExecuteExW`, `WinExec`, or `system` inside `recto-pdf.lib`. Done when: `python scripts/check-security-symbols.py` exits `0` on Release and Debug builds and fails if a test symbol reference to `ShellExecuteW` is injected. Cheaper substitute: grepping source comments and claiming `recto-pdf` cannot spawn processes without inspecting the compiled static library symbols.
- [ ] Add `SecurityAudit.PdfEngine.JavaScriptAndLaunchIgnored` in `tests/recto-tests/security_audit_tests.cpp` opening crafted PDFs containing `/OpenAction << /S /JavaScript /JS (app.alert(1)) >>`, `/AA` page/field script triggers, and `/A << /S /Launch /F (cmd.exe) >>`, rendering all pages and clicking link hit targets, and verifying zero process creation or script evaluation occurs. Done when: `SecurityAudit.PdfEngine.JavaScriptAndLaunchIgnored` passes under ASan.
- [ ] Add `SecurityAudit.PdfEngine.ParserAndFilterBounds` testing a `65`-deep nested array/dictionary PDF (`PdfErrorCode::RecursionLimitExceeded`), a cyclic `/Prev` xref chain (`PdfErrorCode::CorruptXref`), a cyclic `/Pages` `/Kids` tree, and a `10 KB` Flate stream expanding past `256 MB` (`PdfErrorCode::AllocationLimitExceeded`). Done when: all 4 hostile inputs fail cleanly in `< 50 ms` with peak allocation `< 260 MB`.
- [ ] Add `SecurityAudit.PdfEngine.BrokenAndSeedCorpusSweep` running `pdf::Document::open`, `extract_page_text`, and `build_display_list` across every file in `tests/corpus/broken/` and `tests/corpus/seeds/` under ASan. Done when: 100% of files either succeed or return a structured `PdfError` with zero crashes or sanitizer findings.
- [ ] Record all Part 1 rules, proof commands, and quoted outputs in `docs/security-audit.md`, filing any discovered gap via `add-todo` in `01-engine`. Done when: `docs/security-audit.md` Part 1 table has zero blank evidence cells.
- [ ] Commit: `security: audit PDF engine hostile-input rules and symbol boundaries (Part 1)`

**Test checkpoint:** `python scripts/check-security-symbols.py` exits `0` and `recto-tests --gtest_filter=SecurityAudit.PdfEngine.*` passes under ASan with `docs/security-audit.md` Part 1 table quoted.

## 2. Hostile-input audit Part 2 (macro suppression, HTML script block, link prompt, redact)

Complete `docs/security-audit.md` and `SecurityAudit.*` by auditing every services-level and viewer-level hostile-input boundary:
- Built-in OpenXML/RTF/HTML/Markdown reader ignores `vbaProject.bin`, external XML entities (XXE `<!DOCTYPE` / `<!ENTITY`), ZIP path-traversal (`../`) entries, ZIP bombs (`> 256 MB`), `<script>` blocks, and remote `http://`/`https://` sub-resources.
- External Office COM adapter sets `AutomationSecurity = msoAutomationSecurityForceDisable (3)` before opening (`macro-canary.docm` never writes its canary file), and LibreOffice headless adapter uses `CreateProcessW` with `lpApplicationName` (never `cmd.exe /c`), `MacroSecurityLevel=3`, and a kill-on-close Win32 Job Object.
- Viewer external URI links (`https://`, `mailto:`) always require explicit user confirmation in `ExternalLinkConfirmDialog` (`D02 T02 §9`), non-allowlisted schemes (`file://`, `javascript:`, `ms-msdt:`) are refused, and portfolio attachment extraction blocks executable extensions (`.exe`, `.bat`, `.cmd`, `.ps1`, `.vbs`, `.js`, `.scr`, `.msi`).
- Physical redaction (`Redactor`, `D05 T01 §10`) and `sanitize_document` leave zero redacted text glyphs, vector paths, or image pixels and zero `/JavaScript`, `/Metadata`, or `/EmbeddedFiles` objects in raw uncompressed output streams.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Implement `SecurityAudit.ServicesAndViewer.*` in `tests/recto-tests/security_audit_tests.cpp` covering OpenXML XXE/ZIP-slip/macro refusal, HTML `<script>` and remote URL blocking, external converter subprocess/COM containment, link scheme allowlisting, portfolio executable extension blocking, and physical redaction stream inspection. Done when: `recto-tests --gtest_filter=SecurityAudit.*` passes 100% under ASan.
2. Conduct an independent adversarial review pass over `src/pdf`, `src/core`, `src/services`, and `src/app` against `docs/security-audit.md`, record all Part 2 evidence in `docs/security-audit.md`, and file any findings via `add-todo`. Done when: `docs/security-audit.md` is complete across all foundation plan section 9 rules.

- [ ] Add `SecurityAudit.ServicesAndViewer.OpenXmlHostileInputs` testing `.docx`/`.xlsx`/`.pptx` archives containing (a) `word/vbaProject.bin` macros, (b) `<!DOCTYPE foo [<!ENTITY xxe SYSTEM "file:///C:/Windows/win.ini">]>` XML entity expansion, (c) `../../evil.dll` ZIP path traversal entries, and (d) a `256 MB+` ZIP bomb entry. Done when: `vbaProject.bin` is ignored and (b), (c), and (d) are rejected with typed errors and zero disk writes or external file reads. Cheaper substitute: trusting ZIP entry names or XML parsers with default entity resolution enabled.
- [ ] Add `SecurityAudit.ServicesAndViewer.HtmlScriptAndRemoteFetchBlocked` converting an HTML document containing `<script>window.location='http://127.0.0.1:9/'</script>`, `<img src="http://127.0.0.1:9/track.png">`, and `<iframe src="file:///C:/Windows/win.ini">` to PDF and verifying zero outbound socket connections and zero local file inclusion outside the explicitly allowed base directory. Done when: `SecurityAudit.ServicesAndViewer.HtmlScriptAndRemoteFetchBlocked` passes under ASan.
- [ ] Re-verify `SecurityAudit.ServicesAndViewer.ExternalConverterContainment` checking `macro-canary.docm` (`D04 T04 §5`), hostile filename injection (`report & calc.exe .docx`), and Job Object termination (`D04 T04 §6`), plus a static source scan confirming zero calls to `system()`, `_wsystem()`, `popen()`, `_wpopen()`, or `ShellExecuteA/W` on document paths across `src/`. Done when: both runtime tests and the static banned-API check pass.
- [ ] Add `SecurityAudit.ServicesAndViewer.LinkAllowlistAndAttachmentGuard` verifying that `classify_link_action` (`D02 T02 §9`) routes `http://`, `https://`, and `mailto:` to `ConfirmExternalUri` while blocking `file://`, `javascript:`, `vbscript:`, `ms-msdt:`, `search-ms:`, and UNC paths (`\\server\share`), and that portfolio attachment save blocks executable extensions (`.exe`, `.cmd`, `.bat`, `.ps1`, `.vbs`, `.js`, `.msi`, `.scr`). Done when: all hostile URI schemes and executable attachment extensions are refused in unit tests.
- [ ] Add `SecurityAudit.ServicesAndViewer.PhysicalRedactionAndSanitizeProof` running `Redactor::apply_redactions` and `sanitize_document` (`D05 T01 §10`) on a PDF containing secret text, an embedded raster image, `/JavaScript`, `/EmbeddedFiles`, `/Metadata`, and `/PieceInfo`, decompressing every stream in the output PDF, and asserting `0` occurrences of the secret bytes or hidden objects. Done when: raw stream inspection confirms physical removal.
- [ ] Complete `docs/security-audit.md` Part 2 and record the independent adversarial review sign-off and any `add-todo` refs. Done when: `docs/security-audit.md` covers 100% of foundation plan section 9 bullets with quoted test output.
- [ ] Commit: `security: complete hostile-input rules audit across converters, viewer, and redaction (Part 2)`

**Test checkpoint:** `recto-tests --gtest_filter=SecurityAudit.*` passes under ASan and every row in `docs/security-audit.md` carries quoted command output or a filed `DNN TNN §N` owner.

## 3. M3-M5 fuzz targets (`fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) & scheduled workflow

Complete the foundation plan section 4.11 fuzz target suite by authoring `fuzz/fuzz_text.cpp` (`PageText` extraction, CMap/`/ToUnicode` parsing, and reading-order clustering), `fuzz/fuzz_forms.cpp` (AcroForm `/Fields` traversal, field value mutation, and `/AP /N` appearance stream synthesis), and `fuzz/fuzz_openxml_zip.cpp` (`ZipReader` archive parsing and built-in `.docx`/`.xlsx`/`.pptx` OpenXML XML parsing), and stand up the continuous fuzzing operation across all six libFuzzer + ASan targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`): author `.github/workflows/fuzz-scheduled.yml` (scheduled nightly/weekly plus `workflow_dispatch`) and local runner script `scripts/run-fuzz-campaign.ps1` that execute bounded campaigns across all six targets, minimize and merge newly discovered coverage-expanding inputs into `tests/corpus/seeds/` under provenance rules (`cmin`), and publish coverage/execution summaries and any crash artifacts without silently stopping future scheduled runs.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `fuzz/fuzz_text.cpp`, `fuzz/fuzz_forms.cpp`, and `fuzz/fuzz_openxml_zip.cpp` in `recto-fuzz.exe` (`Release | x64` with `/fsanitize=address,fuzzer`), and author `scripts/run-fuzz-campaign.ps1` and `.github/workflows/fuzz-scheduled.yml` running all 6 targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) for configurable budgets, running `-merge=1` corpus minimization, and writing a structured campaign report (`build/fuzz-report.json` and Markdown summary). Done when: `pwsh -NoProfile -File scripts/run-fuzz-campaign.ps1 -SecondsPerTarget 60` runs all 6 targets, outputs coverage metrics (`cov:`, `ft:`, `exec/s`), and writes `build/fuzz-report.json`.
2. Execute two consecutive scheduled/campaign runs across all 6 targets, merge any new coverage-increasing seed inputs into `tests/corpus/seeds/` with updated `tests/corpus/PROVENANCE.md` entries, and archive the campaign summaries in `fuzz/campaigns/`. Done when: two consecutive campaign summaries are committed and `Corpus.Provenance` passes.

- [ ] Implement `fuzz/fuzz_text.cpp` (`extract_page_text`, `/ToUnicode` CMap parser, and `SearchEngine`), `fuzz/fuzz_forms.cpp` (AcroForm `/Fields` parser, field mutation, and `/AP /N` synthesis), and `fuzz/fuzz_openxml_zip.cpp` (`ZipReader::open_bytes` and `OfficeToPdf` `.docx`/`.xlsx`/`.pptx` XML parser) inside `fuzz/` and wire them into `recto-fuzz` and CI smoke runs. Done when: all 6 fuzz targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) compile with `/fsanitize=address,fuzzer` and run cleanly on `tests/corpus/seeds/`.
- [ ] Write `scripts/run-fuzz-campaign.ps1` supporting `-SecondsPerTarget <int>`, `-Minimize`, and `-UpdateSeeds` flags, executing all 6 targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) sequentially and capturing libFuzzer's final `cov` (edge coverage), `ft` (feature count), `corp` (corpus count/bytes), `exec/s`, and `rss_mb` metrics into `build/fuzz-report.json`. Done when: `scripts/run-fuzz-campaign.ps1 -SecondsPerTarget 30` produces valid JSON metrics for all 6 targets. Cheaper substitute: treating the one-time M1 1-hour fuzz run (`D01 T03 §5`) as a permanent substitute for ongoing fuzzing as `recto-pdf` and `recto-core` grow in M3-M5.
- [ ] Ensure `scripts/run-fuzz-campaign.ps1` runs all 6 targets even if one target encounters a crash (`crash-*`, `leak-*`, `timeout-*`, `oom-*`), preserving the crash artifact in `build/fuzz-crashes/<target>/` and recording the reproducer command in `build/fuzz-report.json` before exiting with status code `1` at the end of the campaign. Done when: a simulated crash in target 1 still executes targets 2 through 6 and preserves the crash file.
- [ ] Implement `-Minimize` in `scripts/run-fuzz-campaign.ps1` using libFuzzer `-merge=1` to deduplicate new coverage-expanding inputs against `tests/corpus/seeds/` while enforcing the `5 MB` total corpus size budget (`D00 T03 §3`). Done when: `-Minimize` shrinks a scratch directory with duplicate seeds down to the minimal unique-feature set.
- [ ] Author `.github/workflows/fuzz-scheduled.yml` triggered on a cron schedule (`0 3 * * *`) and `workflow_dispatch`, uploading `build/fuzz-report.json` and any `build/fuzz-crashes/` artifacts on failure. Done when: workflow YAML validates cleanly and matches the invocation contract of `scripts/run-fuzz-campaign.ps1`.
- [ ] Run two consecutive campaigns via `scripts/run-fuzz-campaign.ps1`, record their dated summaries in `fuzz/campaigns/2026-10-14-campaign-summary.md`, and update `tests/corpus/PROVENANCE.md` for any merged seed inputs. Done when: `fuzz/campaigns/2026-10-14-campaign-summary.md` is committed and `recto-tests --gtest_filter=Corpus.Provenance` passes.
- [ ] Commit: `fuzz: add fuzz_text, fuzz_forms, fuzz_openxml_zip targets and scheduled campaign workflow`

**Test checkpoint:** `pwsh -NoProfile -File scripts/run-fuzz-campaign.ps1 -SecondsPerTarget 60` completes across all 6 targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`), with two consecutive campaign summaries in `fuzz/campaigns/` and `Corpus.Provenance` passing quoted.

## 4. Crash triage runbook (`fuzz/README.md`) proven end-to-end on a planted crash input

Document the repeatable crash triage protocol in `fuzz/README.md` (reproduce via `recto-fuzz.exe` and `recto-cli`, minimize via `-minimize_crash=1`, classify the owning `src/pdf` or `src/core` subsystem, promote the minimized file to `tests/corpus/broken/` with `PROVENANCE.md` registration, file an owned TODO section via `add-todo`, fix, and add a permanent `recto-tests` regression test) and prove the entire loop once end-to-end using a controlled planted defect and a synthetic crash input.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `fuzz/README.md` documenting the 6-step crash triage workflow (`Reproduce -> Minimize -> Classify -> Register in broken/ -> File/Fix -> Regression Test`) across all 6 fuzz targets and add helper script `scripts/triage-fuzz-crash.py` that automates crash minimization (`-minimize_crash=1 -max_total_time=30`) and `recto-cli info` reproduction. Done when: `fuzz/README.md` and `scripts/triage-fuzz-crash.py` are complete.
2. Prove the triage loop end-to-end on a synthetic crash case: run `scripts/triage-fuzz-crash.py` to minimize the input into `tests/corpus/broken/fuzz-triage-proof.pdf`, register it in `tests/corpus/PROVENANCE.md`, and add regression test `SecurityAudit.FuzzTriageProofRegression` in `tests/recto-tests/security_audit_tests.cpp` verifying clean `PdfError` handling. Done when: `SecurityAudit.FuzzTriageProofRegression` and `Corpus.Provenance` pass under ASan.

- [ ] Author `fuzz/README.md` detailing the exact commands for: (1) building `recto-fuzz.exe` with ASan + libFuzzer across all 6 targets (`open`, `filters`, `render`, `text`, `forms`, `openxml_zip`), (2) running `scripts/run-fuzz-campaign.ps1`, (3) reproducing a crash via `recto-fuzz.exe <target> <crash-file>` and `recto-cli info <crash-file>`, (4) minimizing via `python scripts/triage-fuzz-crash.py`, (5) promoting to `tests/corpus/broken/` + `PROVENANCE.md`, and (6) filing a section via `add-todo`. Done when: every command in `fuzz/README.md` uses real repository paths and flags. Cheaper substitute: prose guidelines with no minimization script or end-to-end proof that the triage commands actually work on Windows MSVC libFuzzer.
- [ ] Write `scripts/triage-fuzz-crash.py` that accepts `--target <open|filters|render|text|forms|openxml_zip> --input <crash-path> --out <tests/corpus/broken/name.pdf>`, runs `recto-fuzz.exe -minimize_crash=1 -max_total_time=30`, verifies reproduction before minimization, and prints the SHA-256 and byte-size reduction (`before_bytes -> after_bytes`). Done when: `python scripts/triage-fuzz-crash.py --help` and a dry-run minimization test in `TempDir` succeed.
- [ ] Create a padded malformed PDF input (`scratch/planted-crash-unminimized.pdf`, e.g., a truncated object stream with 4 KB of padding bytes) and run `python scripts/triage-fuzz-crash.py` under a test-only `RECTO_FUZZ_TRIAGE_CANARY` trigger to prove `recto-fuzz.exe` catches the canary crash and `-minimize_crash=1` strips the padding down to the minimal reproducer `tests/corpus/broken/fuzz-triage-proof.pdf`. Done when: `tests/corpus/broken/fuzz-triage-proof.pdf` is `< 256 bytes` and reproduces the canary trigger deterministically.
- [ ] Remove the test canary trigger (or guard it strictly behind a unit-test flag) so production `recto-pdf` returns a clean `PdfError` on `tests/corpus/broken/fuzz-triage-proof.pdf`, and register `fuzz-triage-proof.pdf` in `tests/corpus/PROVENANCE.md`. Done when: `recto-cli info tests/corpus/broken/fuzz-triage-proof.pdf` exits cleanly (`1`, structured error JSON) and `Corpus.Provenance` passes.
- [ ] Add `SecurityAudit.FuzzTriageProofRegression` in `tests/recto-tests/security_audit_tests.cpp` opening `tests/corpus/broken/fuzz-triage-proof.pdf` under ASan and asserting it returns `PdfError` without crashing. Done when: `recto-tests --gtest_filter=SecurityAudit.FuzzTriageProofRegression` passes under ASan.
- [ ] Commit: `fuzz: add crash triage runbook, minimization script, and proven regression test`

**Test checkpoint:** `python scripts/triage-fuzz-crash.py` minimization output (`before_bytes -> after_bytes`), `recto-cli info tests/corpus/broken/fuzz-triage-proof.pdf` output, and `recto-tests --gtest_filter=SecurityAudit.FuzzTriageProofRegression:Corpus.Provenance` passing under ASan quoted.

## 5. `recto-pdf` public API boundary inventory (`recto_pdf.h` ownership and mutability table)

Verify foundation plan section 3.1 and decision D13 against the as-built `src/pdf/` headers by writing a deterministic Python inspection script (`scripts/inventory-pdf-boundary.py`) that parses the public headers exported by `recto-pdf` (`src/pdf/recto_pdf.h` and its included public headers), enumerates every public struct, class, and function in `namespace recto::pdf`, verifies that zero WinUI, `recto-core`, or `recto-services` types leak into `src/pdf/`, and classifies every public entry point for the post-v1 broker split (`Broker-Internal`, `IPC-Request-Response`, `Shared-Memory-Snapshot`, or `Stateless-Helper`).

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Author `scripts/inventory-pdf-boundary.py` scanning `src/pdf/*.h` to extract all public declarations in `namespace recto::pdf`, check include hygiene (zero includes from `src/core`, `src/services`, `src/app`, or WinUI headers), and output a deterministic Markdown table (`--check` and `--write` modes). Done when: `python scripts/inventory-pdf-boundary.py --check` exits `0` and verifies zero upward header dependencies.
2. Add unit tests in `tests/scripts/test_pdf_boundary_inventory.py` verifying that `scripts/inventory-pdf-boundary.py` detects any uncategorized public function or illegal upward `#include` in `src/pdf/`. Done when: `python -m unittest tests.scripts.test_pdf_boundary_inventory` passes.

- [ ] Implement `scripts/inventory-pdf-boundary.py` to scan all header files in `src/pdf/` and verify that no header includes anything from `src/core/`, `src/services/`, `src/app/`, `winrt/`, or `microsoft.ui.xaml`. Done when: `python scripts/inventory-pdf-boundary.py --check` passes on the live `src/pdf/` directory and fails on a synthetic header containing `#include "../core/core.h"`. Cheaper substitute: writing a hand-crafted table from memory without machine-checking it against the actual headers in `src/pdf/`.
- [ ] Enumerate every public type and function in `src/pdf/` (`Document`, `Page`, `DisplayList`, `PageText`, `PdfValue`, `PdfError`, `PdfResult<T>`, `IncrementalWriter`, `PdfWriter`, `Redactor`, `ComplianceEngine`, `Linearizer`, signature/encryption APIs) and classify each as `Broker-Internal` (mutable `Document` graph stays inside broker), `IPC-Request-Response` (serialized parameters/results), `Shared-Memory-Snapshot` (`DisplayList`, `PageText`, raster `Bitmap` tiles transferred via read-only file mapping), or `Stateless-Helper`. Done when: 100% of public declarations in `src/pdf/` map to one of the 4 categories with zero `unclassified` entries.
- [ ] Verify the immutability contract on cross-boundary data types (`DisplayList` and `PageText`): confirm that neither type holds a raw pointer or reference back to `recto::pdf::Document` or mutable object-table entries. Done when: `scripts/inventory-pdf-boundary.py` inspects `DisplayList` and `PageText` field types and confirms zero `Document*` or `Document&` members.
- [ ] Support `--write <spec-file>` and `--check <spec-file>` in `scripts/inventory-pdf-boundary.py` so the generated Markdown inventory table inside `docs/specs/2026-10-14-broker-process-design.md` (`§6`) is verified to match `src/pdf/` with an empty diff. Done when: running `--write` twice produces an empty `git diff`.
- [ ] Add `tests/scripts/test_pdf_boundary_inventory.py` testing header parsing, upward-include rejection, and `--check` drift detection. Done when: `python -m unittest tests.scripts.test_pdf_boundary_inventory` passes.
- [ ] Commit: `tools: add src/pdf public API boundary inventory script and tests`

**Test checkpoint:** `python scripts/inventory-pdf-boundary.py --check` exits `0` and `python -m unittest tests.scripts.test_pdf_boundary_inventory` passes with the summary counts of `Broker-Internal`, `IPC-Request-Response`, `Shared-Memory-Snapshot`, and `Stateless-Helper` APIs quoted.

## 6. Post-v1 AppContainer broker process design (`2026-10-14-broker-process-design.md`)

Author the complete post-v1 sandboxed broker architecture specification in `docs/specs/2026-10-14-broker-process-design.md` (implementing decision D13, foundation plan section 9's final bullet, and resolving foundation plan section 11 item 5): specify how `recto-pdf` and `DocumentActor` move into a restricted Windows AppContainer broker process (`CreateAppContainerProfile` / `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` with zero capabilities, zero filesystem access, and zero network access), how the UI host process brokers read-only file handles (`CreateFileW` + `DuplicateHandle`) or read-only `SEC_IMAGE`/`SEC_COMMIT` file mappings into the broker, how immutable `DisplayList`s, `PageText` snapshots, and `Bitmap` tiles cross back over shared-memory rings/arenas (`CreateFileMappingW` / `MapViewOfFile`), how broker crashes are contained and isolated per document tab without taking down `Recto.exe`, and how the design satisfies foundation plan section 8's latency and memory budgets.

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Author `docs/specs/2026-10-14-broker-process-design.md` covering: (1) Threat Model & AppContainer Security Profile, (2) As-Built `src/pdf/` API Boundary Inventory Table (embedded via `scripts/inventory-pdf-boundary.py --write`), (3) Handle Brokering & Shared-Memory Arena Protocol (`CreateFileMappingW`), (4) Zero-Copy `DisplayList`, `PageText`, and Tile `Bitmap` Serialization, (5) Crash Containment & Per-Tab Actor Restart State Machine, (6) Performance & Memory Cost Model against Plan Section 8, and (7) Step-by-Step Post-v1 Migration Sequence. Done when: `docs/specs/2026-10-14-broker-process-design.md` is complete and `python scripts/inventory-pdf-boundary.py --check docs/specs/2026-10-14-broker-process-design.md` exits `0`.
2. Update `docs/plans/2026-10-13-recto-foundation.md` section 11 item 5 with a dated resolution note pointing to `docs/specs/2026-10-14-broker-process-design.md`, and verify all cross-references and graph rules with `python scripts/todo-graph.py validate`. Done when: validation exits `0` with an empty diff on the generated boundary table.

- [ ] Specify the Windows AppContainer sandbox profile in `docs/specs/2026-10-14-broker-process-design.md` section 1: `CreateAppContainerProfile(L"Recto.PdfBroker.v1", ...)`, zero capability SIDs (`CapabilityCount = 0`: no `internetClient`, no `documentsLibrary`, no `lpacCom`), `PROCESS_MITIGATION_DYNAMIC_CODE_POLICY` (ACG), `PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY` (`DisallowWin32kSystemCalls` where supported after font handle warmup), and Job Object memory limit per broker. Done when: section 1 lists every Win32 API, mitigation flag, and token restriction. Cheaper substitute: hand-waving "run in a sandbox" without naming the Win32 AppContainer APIs, mitigation policies, or handle-duplication mechanics.
- [ ] Embed and verify the machine-generated `src/pdf/` boundary inventory table in section 2 using `python scripts/inventory-pdf-boundary.py --write docs/specs/2026-10-14-broker-process-design.md`. Done when: `python scripts/inventory-pdf-boundary.py --check docs/specs/2026-10-14-broker-process-design.md` exits `0` with 0 drift.
- [ ] Specify the IPC channel and handle-brokering protocol in section 3: anonymous named pipe (`CreateNamedPipeW` with `FILE_FLAG_OVERLAPPED`) paired with `DuplicateHandle` for passing read-only source PDF `HANDLE`s, temporary output `HANDLE`s (for `AtomicFileWriter` stays in host while broker writes to an anonymous temp handle), and read-only shared-memory section handles (`CreateFileMappingW(INVALID_HANDLE_VALUE, ..., PAGE_READWRITE)` downgraded to `FILE_MAP_READ` via `DuplicateHandle` before crossing to the host). Done when: section 3 includes the exact message framing header (`msg_id`, `request_id`, `CancelToken` generation, payload size, attached handle count) and state diagram.
- [ ] Specify the flat shared-memory layout for `DisplayList`, `PageText`, and rasterized `Bitmap` tiles in section 4 so `DisplayListBuilder` and `RenderPool` can allocate directly into a shared-memory arena without pointer swizzling (`offset_t` relative offsets instead of heap pointers). Done when: section 4 defines the arena header, offset-table validation rules on the host side (never trusting offsets from a compromised broker without bounds checking), and tile ring buffer sizing.
- [ ] Specify the per-document crash containment and recovery state machine in section 5: when a broker process terminates abnormally, only the affected document tab displays a non-modal `Reload Document` banner while other open tabs, the `QueueCard`, and the UI shell continue uninterrupted. Done when: section 5 defines the process-exit watcher (`RegisterWaitForSingleObject` on the broker `HANDLE`) and recovery flow.
- [ ] Quantify the latency and memory cost model in section 6 against foundation plan section 8's five targets: pre-warmed broker pool (`< 5 ms` handle handoff so `First page visible` remains `< 300 ms`), zero-copy shared-memory tile rings (`0 ms` extra copy on scroll/zoom), and shared read-only font/PDF mappings (`WorkingSetSize` remains `< 600 MB` on 500-page PDFs). Done when: section 6 accounts for each of the 5 section 8 targets with concrete byte and millisecond budgets.
- [ ] Update `docs/plans/2026-10-13-recto-foundation.md` section 11 item 5 with a dated resolution note pointing to `docs/specs/2026-10-14-broker-process-design.md`. Done when: foundation plan section 11 item 5 is marked resolved.
- [ ] Commit: `docs: author post-v1 AppContainer broker process design specification (D13)`

**Test checkpoint:** `python scripts/inventory-pdf-boundary.py --check docs/specs/2026-10-14-broker-process-design.md` exits `0` and `python scripts/todo-graph.py validate` exits `0`.

## Verification

- Foundation plan section 9 is auditable end-to-end from `docs/security-audit.md` and `recto-tests --gtest_filter=SecurityAudit.*` alone.
- The scheduled fuzz campaign (`scripts/run-fuzz-campaign.ps1`) and crash triage runbook (`fuzz/README.md` + `scripts/triage-fuzz-crash.py`) are proven on the repository and committed.
- `docs/specs/2026-10-14-broker-process-design.md` is verified against the as-built `src/pdf/` headers by `scripts/inventory-pdf-boundary.py --check`.
- `python scripts/todo-graph.py validate` passes.
