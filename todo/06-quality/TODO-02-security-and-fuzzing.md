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

> **Current state:** Measured 2026-10-14 (re-measured 2026-10-23: `fuzz/` holds only `fuzz_smoke.cpp`, and none of `docs/security-audit.md`, `scripts/check-security-symbols.py`, `scripts/run-fuzz-campaign.ps1` or `SecurityAudit.*` exists). No code of this file exists yet. The rules are defined in foundation plan section 9 and decisions D12 and D13: every PDF, Office, and HTML file is treated as hostile; `recto-pdf` contains zero JavaScript interpreter (`/JS` and `/JavaScript` actions are ignored by construction); Office macros are ignored and stripped by the 100% native OpenXML parser (`vbaProject.bin` skipped) and the native OLE2 `[MS-CFB]` reader (`_VBA_PROJECT_CUR`, `Macros`, and `VBA` storages skipped; zero Office COM or external converter subprocesses exist); HTML `<script>` and remote network fetches are blocked; external links and portfolio attachments ask before opening; PDF `/Launch` actions never execute; physical redaction strips underlying glyphs, vector paths, and image pixels before drawing overlays; and v1 parses in-process behind a clean `recto-pdf` C++23 boundary designed to move into a restricted AppContainer broker process after v1. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI references rewritten against `todo/10-ui/`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D12, D13), 3.1 (`recto-pdf` boundary), 3.2 (`fuzz/`), 4.11 (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`), 9 (security and untrusted-input posture), and 11 item 5 (broker process design).
- -> XREF: D00 T04 §12 -- the compile database and the Visual Studio LLVM tools found there, which `§12` here runs clang-tidy over.
- -> XREF: D00 T04 §16 -- Release link-time code generation, which waits for `§10` and `§12` here by owner priority (2026-11-17).
- -> XREF: D00 T04 §17 -- unity builds, which wait for `§10` and `§12` here and keep the compile database to single source files so neither gate reads a unity unit.
- -> XREF: D01 T01 §4 -- the 64-level recursive-descent parser and object-graph cycle bounds audited in `§1`.
- -> XREF: D99 T01 §10 -- the operator runs the FIPS proof script of `§9` on a FIPS-enabled host and records the result in `docs/security-audit.md`.
- -> XREF: D01 T01 §7 -- the 256 MB stream decompression-bomb ceiling and filter bounds audited in `§1`.
- -> XREF: D01 T01 §10 -- the `recto-pdf` public API header (`src/pdf/recto_pdf.h`) and action reader (`/URI` surfaced, `/JS` and `/Launch` ignored) audited in `§1` and inventoried in `§5`.
- -> XREF: D01 T03 §4 -- the three M1 fuzz targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`) extended with `fuzz_text`, `fuzz_forms`, and `fuzz_openxml_zip` and operated continuously in `§3` and `§4`.
- -> XREF: D01 T03 §5 -- the M1 1-hour fuzz pass and CI 60-second smoke gate extended into scheduled 6-target runs in `§3`.
- -> XREF: D02 T02 §9 -- the viewer external URI confirmation dialog and `/Launch` / `/JavaScript` refusal audited in `§1` and `§2`.
- -> XREF: D03 T01 §1 -- the `PageText` extraction, CMap/ToUnicode decoder, `SearchEngine` (`§5`), and content-stream `replace_text_in_document` (`§9`) targeted by `fuzz_text.cpp` in `§3`.
- -> XREF: D03 T03 §6 -- the `/Annots` (`§1`), AcroForm `/Fields`, `/V`, `/AP` appearance generator (`§6`), and `/XFA` converter (`§7`) targeted by `fuzz_forms.cpp` in `§3`.
- -> XREF: D04 T04 §1 -- the `ZipReader` and OpenXML `.docx`/`.xlsx`/`.pptx` XML parsers (`§1`, `§2`) targeted by `fuzz_openxml_zip.cpp` in `§3`.
- -> XREF: D04 T04 §4 -- the native OLE2 Compound File (`[MS-CFB]`) reader and macro storage stripper (`§4`) targeted by `fuzz_openxml_zip.cpp` in `§3` and audited in `§2`.
- -> XREF: D04 T04 §5 -- the macro-stream inertness proof on `macro-canary.docm` and `macro-canary.doc` (`§5`) re-verified in `§2`.
- -> XREF: D05 T01 §10 -- the physical content-stream redaction (`Redactor`) and `sanitize_document` proofs re-verified in `§2`.
- -> XREF: D06 T03 §1 -- the user-reported hostile-input fixture pack whose files seed the fuzz corpora scheduled here.
- -> XREF: D02 T05 §15 -- runs the rich media, 3D and geospatial fuzz targets (D02 T05 §15).
- -> XREF: D05 T01 §7 -- FIPS gate in encryption.
- -> XREF: D05 T01 §11 -- FIPS gate in certificate encryption.
- -> XREF: D04 T07 §5 -- encrypt step typed error.
- -> XREF: D07 T02 §4 -- fips_mode in crash bundle.
- -> XREF: D04 T04 §8 -- fuzz_odf.
- -> XREF: D04 T04 §12 -- fuzz_ofd.
- -> XREF: D04 T04 §16 -- fuzz_mime.
- -> XREF: D04 T04 §17 -- fuzz_msg.
- -> XREF: D04 T08 §9 -- fuzz_mcp_message.
- -> XREF: D04 T04 §23 -- the reader-level OLE2 (CFB), RTF, HTML and Markdown harnesses; §3 keeps the container-level `fuzz_openxml_zip` corpus and schedule.

## Outcome

- Every rule in foundation plan section 9 has both a static/symbol-level proof and an automated `recto-tests` regression test recorded with quoted evidence in `docs/security-audit.md`.
- All six foundation-plan fuzz targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) run under continuous scheduled fuzzing with coverage summaries and corpus delta archiving, and `fuzz/README.md`'s crash triage loop is proven end-to-end on a planted crash input.
- The `src/pdf/recto_pdf.h` public API surface is machine-inventoried against the shipped headers (`§5`), and the post-v1 AppContainer shared-memory broker specification (`docs/specs/2026-10-14-broker-process-design.md`, `§6`) proves that moving `recto-pdf` out-of-process requires zero changes to `DocumentView` or `ConversionRunner` callers.
- Windows FIPS mode is detected with `BCryptGetFipsAlgorithmMode` and enforced across decryption, encryption writers and signing digests, with a proof script that is tested here and run on a FIPS-enabled host by the operator (`§7` to `§9`, `D99 T01 §10`).

**Adjacency:** all=not-applicable (security assurance audit, continuous fuzzing operations, and post-v1 architecture specification; no user-facing product surfaces)

**Adjacency rationale:** Recto has no user-role permission model; its untrusted-input refusal boundaries (external link confirmation, macro suppression, `/Launch` and `/JS` refusal, physical redaction) are implemented in their owning engine, services, and viewer sections (`D01 T01`, `D02 T02 §9`, `D04 T04`, `D05 T01 §10`) and audited end-to-end here in `§1` and `§2`.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Hostile-input audit Part 1 (`docs/security-audit.md`: zero-JS, `/Launch` & bomb bounds) | D01 T01 §10, D02 T02 §9, D01 T02 §19 | [ ] |
| 2 | §2 | Hostile-input audit Part 2 (macro suppression, HTML script block, link prompt, redact) | §1, D04 T04 §4, D04 T04 §5, D05 T01 §10 | [ ] |
| 3 | §3 | M3-M5 fuzz targets (`fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) & scheduled workflow | D01 T03 §5, D03 T01 §5, D03 T01 §9, D03 T03 §7, D04 T04 §2, D04 T04 §4, D07 T01 §17 | [ ] |
| 4 | §4 | Crash triage runbook (`fuzz/README.md`) proven end-to-end on a planted crash input | §3 | [ ] |
| 5 | §5 | `recto-pdf` public API boundary inventory (`recto_pdf.h` ownership and mutability table) | §2 | [ ] |
| 6 | §6 | Post-v1 AppContainer broker process design (`2026-10-14-broker-process-design.md`) | §5 | [ ] |
| 7 | §7 | FIPS mode detection and the algorithm policy in `recto-pdf` | D01 T01 §9, D07 T02 §4 | [ ] |
| 8 | §8 | Enforce the FIPS policy in decryption, encryption writers, signing digests and the CLI | §7, D01 T02 §5, D05 T01 §7, D05 T01 §11, D04 T07 §5, D07 T02 §15 | [ ] |
| 9 | §9 | FIPS mode proof script, tested to refuse on an ordinary host | §8 | [ ] |
| 10 | §10 | MSVC code analysis (`/analyze`) over every module, clean, as a CI job | -- | [x] |
| 11 | §11 | CodeQL scanning of the C++ sources, with open alerts read by the run | -- | [x] |
| 12 | §12 | clang-tidy bug, CERT and concurrency checks over the compile database | D00 T04 §12 | [ ] |

## 1. Hostile-input audit Part 1 (`docs/security-audit.md`: zero-JS, `/Launch` & bomb bounds)

> **Groomed 2026-10-23:** the error names are the real ones in `src/pdf/error.h` (`PdfErrorCode`, `CorruptXref` and `AllocationLimitExceeded` do not exist), and the form-nesting cap is `32` (`kMaxFormResourceDepth`), not `16`. The audit also covers the interpreter work ceilings and tile failure isolation of `D01 T02 §19` and `§20` once they ship (the `SecurityAudit.PdfEngine.ParserAndFilterBounds` case adds an operator-count bomb and a 20,000 x 20,000 one-bit image).

Audit the PDF engine security rules from foundation plan section 9 and decision D12 and record quoted static, symbol-level, and test evidence in `docs/security-audit.md`:
- Zero JavaScript engine or script interpreter linked anywhere in `recto-pdf.lib`, `recto-core.lib`, `recto-services.lib`, or `Recto.exe` (verified via `dumpbin /symbols` and `dumpbin /dependents`), and PDF `/S /JavaScript`, `/JS`, `/AA`, and `/OpenAction` script dictionaries ignored without execution.
- PDF `/S /Launch`, `/ImportData`, `/SubmitForm`, and `/GoToR` executable actions never invoke `CreateProcessW` or `ShellExecuteExW`.
- Every parser recursion cap (`64` nested arrays/dicts, `32` nested `/Form` XObjects as `D01 T02 §4` and `kMaxFormResourceDepth` in `src/pdf/document.h` set), `/Prev` and xref cycle detector, and stream filter decompression-bomb ceiling (`256 MB` per stream, `1000:1` ratio guard) returns a typed `PdfError` without throwing, aborting, or calling `setjmp`/`longjmp` across `tests/corpus/broken/` and `tests/corpus/seeds/`.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tests/recto-tests/security_audit_tests.cpp` (`SecurityAudit.PdfEngine.*`) and `scripts/check-security-symbols.py` checking `dumpbin /symbols` and `dumpbin /dependents` on `recto-pdf.lib` and `Recto.exe` (verifying zero `setjmp`/`longjmp`, zero JS engine symbols, and zero `ShellExecute`/`CreateProcess` imports inside `recto-pdf.lib`). Done when: `python scripts/check-security-symbols.py` and `recto-tests --gtest_filter=SecurityAudit.PdfEngine.*` pass.
2. Author the Part 1 section of `docs/security-audit.md` recording each PDF engine rule verbatim from foundation plan section 9 alongside its owning section, verification command, and quoted output. Done when: `docs/security-audit.md` is committed with complete Part 1 evidence.

- [ ] Implement `scripts/check-security-symbols.py` inspecting `recto-pdf.lib` via `dumpbin /symbols` to verify: (a) zero references to `_setjmp`, `longjmp`, or C++ exception unwind tables across public boundary functions, (b) zero JavaScript interpreter symbols, and (c) zero imports of `CreateProcessW`, `CreateProcessA`, `ShellExecuteW`, `ShellExecuteExW`, `WinExec`, or `system` inside `recto-pdf.lib`. Done when: `python scripts/check-security-symbols.py` exits `0` on Release and Debug builds and fails if a test symbol reference to `ShellExecuteW` is injected. Cheaper substitute: grepping source comments and claiming `recto-pdf` cannot spawn processes without inspecting the compiled static library symbols.
- [ ] Add `SecurityAudit.PdfEngine.JavaScriptAndLaunchIgnored` in `tests/recto-tests/security_audit_tests.cpp` opening crafted PDFs containing `/OpenAction << /S /JavaScript /JS (app.alert(1)) >>`, `/AA` page/field script triggers, and `/A << /S /Launch /F (cmd.exe) >>`, rendering all pages and clicking link hit targets, and verifying zero process creation or script evaluation occurs. Done when: `SecurityAudit.PdfEngine.JavaScriptAndLaunchIgnored` passes under ASan.
- [ ] Add `SecurityAudit.PdfEngine.ParserAndFilterBounds` testing a `65`-deep nested array/dictionary PDF (`ErrorCode::RecursionLimitExceeded`), a cyclic `/Prev` xref chain (`ErrorCode::DamagedFormat` with `DiagnosticId::DamagedXRef`), a cyclic `/Pages` `/Kids` tree, and a `10 KB` Flate stream expanding past `256 MB` (`ErrorCode::DecompressionLimitExceeded`). Done when: all 4 hostile inputs fail cleanly in `< 50 ms` with peak allocation `< 260 MB`.
- [ ] Add `SecurityAudit.PdfEngine.InterpreterCeilings` driving a content stream with 30 million `q Q` operators, a 9,000-operand operator, a 5-million-segment path and a generated 20,000 x 20,000 one-bit image through `build_display_list` with the default `InterpretOptions` of `D01 T02 §19`. Done when: each returns a truncated list or a reduced image in under 12 seconds with peak working set under 700 MB, and no case aborts or hangs under ASan.
- [ ] Add `SecurityAudit.PdfEngine.BrokenAndSeedCorpusSweep` running `pdf::Document::open`, `extract_page_text`, and `build_display_list` across every file in `tests/corpus/broken/` and `tests/corpus/seeds/` under ASan. Done when: 100% of files either succeed or return a structured `PdfError` with zero crashes or sanitizer findings.
- [ ] Record all Part 1 rules, proof commands, and quoted outputs in `docs/security-audit.md`, filing any discovered gap via `add-todo` in `01-engine`. Done when: `docs/security-audit.md` Part 1 table has zero blank evidence cells.
- [ ] Commit: `security: audit PDF engine hostile-input rules and symbol boundaries (Part 1)`

**Test checkpoint:** `python scripts/check-security-symbols.py` exits `0` and `recto-tests --gtest_filter=SecurityAudit.PdfEngine.*` passes under ASan with `docs/security-audit.md` Part 1 table quoted.

## 2. Hostile-input audit Part 2 (macro suppression, HTML script block, link prompt, redact)

Complete `docs/security-audit.md` and `SecurityAudit.*` by auditing every services-level and viewer-level hostile-input boundary:
- Built-in OpenXML/RTF/HTML/Markdown reader ignores `vbaProject.bin`, external XML entities (XXE `<!DOCTYPE` / `<!ENTITY`), ZIP path-traversal (`../`) entries, ZIP bombs (`> 256 MB`), `<script>` blocks, and remote `http://`/`https://` sub-resources.
- Built-in legacy OLE2 Compound File (`[MS-CFB]`) reader (`Ole2Reader`) strips `_VBA_PROJECT_CUR`, `Macros`, `VBA`, and `_SRP_*` storages by construction (`macro-canary.doc` and `macro-canary.docm` never write their canary files), enforces FAT/MiniFAT/DIFAT cycle and `256 MB` stream caps, and zero external Office COM or LibreOffice subprocesses exist anywhere in `recto-core` or `recto-services`.
- Viewer external URI links (`https://`, `mailto:`) always require explicit user confirmation in `ExternalLinkConfirmDialog` (`D02 T02 §9`), non-allowlisted schemes (`file://`, `javascript:`, `ms-msdt:`) are refused, and portfolio attachment extraction blocks executable extensions (`.exe`, `.bat`, `.cmd`, `.ps1`, `.vbs`, `.js`, `.scr`, `.msi`).
- Physical redaction (`Redactor`, `D05 T01 §10`) and `sanitize_document` leave zero redacted text glyphs, vector paths, or image pixels and zero `/JavaScript`, `/Metadata`, or `/EmbeddedFiles` objects in raw uncompressed output streams.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

> **Groomed 2026-10-23:** removed `display-session`: every case here is a unit or raw-byte test (`classify_link_action`, converters, `Redactor`, symbol scans) that opens no window.

**Build order:**

1. Implement `SecurityAudit.ServicesAndViewer.*` in `tests/recto-tests/security_audit_tests.cpp` covering OpenXML XXE/ZIP-slip/macro refusal, OLE2 `[MS-CFB]` cyclic-FAT and macro-storage refusal, HTML `<script>` and remote URL blocking, zero-external-subprocess static proof, link scheme allowlisting, portfolio executable extension blocking, and physical redaction stream inspection. Done when: `recto-tests --gtest_filter=SecurityAudit.*` passes 100% under ASan.
2. Conduct an independent adversarial review pass over `src/pdf`, `src/core`, `src/services`, and `src/app` against `docs/security-audit.md`, record all Part 2 evidence in `docs/security-audit.md`, and file any findings via `add-todo`. Done when: `docs/security-audit.md` is complete across all foundation plan section 9 rules.

- [ ] Add `SecurityAudit.ServicesAndViewer.OpenXmlHostileInputs` testing `.docx`/`.xlsx`/`.pptx` archives containing (a) `word/vbaProject.bin` macros, (b) `<!DOCTYPE foo [<!ENTITY xxe SYSTEM "file:///C:/Windows/win.ini">]>` XML entity expansion, (c) `../../evil.dll` ZIP path traversal entries, and (d) a `256 MB+` ZIP bomb entry. Done when: `vbaProject.bin` is ignored and (b), (c), and (d) are rejected with typed errors and zero disk writes or external file reads. Cheaper substitute: trusting ZIP entry names or XML parsers with default entity resolution enabled.
- [ ] Add `SecurityAudit.ServicesAndViewer.HtmlScriptAndRemoteFetchBlocked` converting an HTML document containing `<script>window.location='http://127.0.0.1:9/'</script>`, `<img src="http://127.0.0.1:9/track.png">`, and `<iframe src="file:///C:/Windows/win.ini">` to PDF and verifying zero outbound socket connections and zero local file inclusion outside the explicitly allowed base directory. Done when: `SecurityAudit.ServicesAndViewer.HtmlScriptAndRemoteFetchBlocked` passes under ASan.
- [ ] Re-verify `SecurityAudit.ServicesAndViewer.NativeOfficeMacroInertnessAndZeroSubprocess` checking `macro-canary.docm` and `macro-canary.doc` (`D04 T04 §4, §5`), cyclic `[MS-CFB]` FAT chains, and hostile filename handling (`report & calc.exe .doc`), plus a static symbol/source scan confirming zero calls to `CreateProcessW`, `CreateProcessA`, `ShellExecuteA/W`, `ShellExecuteExW`, `CoCreateInstance` for `Word.Application`/`Excel.Application`/`PowerPoint.Application`, `system()`, `_wsystem()`, `popen()`, or `_wpopen()` across `recto-core` and `recto-services`. Done when: both runtime tests and the static banned-API check pass.
- [ ] Add `SecurityAudit.ServicesAndViewer.LinkAllowlistAndAttachmentGuard` verifying that `classify_link_action` (`D02 T02 §9`) routes `http://`, `https://`, and `mailto:` to `ConfirmExternalUri` while blocking `file://`, `javascript:`, `vbscript:`, `ms-msdt:`, `search-ms:`, and UNC paths (`\\server\share`), and that portfolio attachment save blocks executable extensions (`.exe`, `.cmd`, `.bat`, `.ps1`, `.vbs`, `.js`, `.msi`, `.scr`). Done when: all hostile URI schemes and executable attachment extensions are refused in unit tests.
- [ ] Add `SecurityAudit.ServicesAndViewer.PhysicalRedactionAndSanitizeProof` running `Redactor::apply_redactions` and `sanitize_document` (`D05 T01 §10`) on a PDF containing secret text, an embedded raster image, `/JavaScript`, `/EmbeddedFiles`, `/Metadata`, and `/PieceInfo`, decompressing every stream in the output PDF, and asserting `0` occurrences of the secret bytes or hidden objects. Done when: raw stream inspection confirms physical removal.
- [ ] Complete `docs/security-audit.md` Part 2 and record the independent adversarial review sign-off and any `add-todo` refs. Done when: `docs/security-audit.md` covers 100% of foundation plan section 9 bullets with quoted test output.
- [ ] Commit: `security: complete hostile-input rules audit across converters, viewer, and redaction (Part 2)`

**Test checkpoint:** `recto-tests --gtest_filter=SecurityAudit.*` passes under ASan and every row in `docs/security-audit.md` carries quoted command output or a filed `DNN TNN §N` owner.

## 3. M3-M5 fuzz targets (`fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) & scheduled workflow

> **Groomed 2026-10-23:** targets are built into the one `recto-fuzz.exe` dispatcher that `D01 T03 §4` defines (`recto-fuzz.exe <target> <libFuzzer args>`), and campaign summary files carry the run date in their name.

Complete the foundation plan section 4.11 fuzz target suite by authoring `fuzz/fuzz_text.cpp` (`PageText` extraction, CMap/`/ToUnicode` parsing, reading-order clustering, `SearchEngine` matching, and content-stream `replace_text_in_document`), `fuzz/fuzz_forms.cpp` (`/Annots` parsing, AcroForm `/Fields` traversal, `/XFA` conversion, field value mutation, and `/AP /N` appearance stream synthesis), and `fuzz/fuzz_openxml_zip.cpp` (`ZipReader` archive parsing, `Ole2Reader` `[MS-CFB]` compound file parsing, and built-in `.docx`/`.xlsx`/`.pptx`/`.doc`/`.xls`/`.ppt` parsers), and stand up the continuous fuzzing operation across all six libFuzzer + ASan targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`): author `.github/workflows/fuzz-scheduled.yml` (scheduled nightly/weekly plus `workflow_dispatch`) and local runner script `scripts/run-fuzz-campaign.ps1` that execute bounded campaigns across all six targets, minimize and merge newly discovered coverage-expanding inputs into `tests/corpus/seeds/` under provenance rules (`cmin`), and publish coverage/execution summaries and any crash artifacts without silently stopping future scheduled runs.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `fuzz/fuzz_text.cpp`, `fuzz/fuzz_forms.cpp`, and `fuzz/fuzz_openxml_zip.cpp` in `recto-fuzz.exe` (`Release | x64` with `/fsanitize=address,fuzzer`), and author `scripts/run-fuzz-campaign.ps1` and `.github/workflows/fuzz-scheduled.yml` running all 6 targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) for configurable budgets, running `-merge=1` corpus minimization, and writing a structured campaign report (`build/fuzz-report.json` and Markdown summary). Done when: `pwsh -NoProfile -File scripts/run-fuzz-campaign.ps1 -SecondsPerTarget 60` runs all 6 targets, outputs coverage metrics (`cov:`, `ft:`, `exec/s`), and writes `build/fuzz-report.json`.
2. Execute two consecutive scheduled/campaign runs across all 6 targets, merge any new coverage-increasing seed inputs into `tests/corpus/seeds/` with updated `tests/corpus/PROVENANCE.md` entries, and archive the campaign summaries in `fuzz/campaigns/`. Done when: two consecutive campaign summaries are committed and `Corpus.Provenance` passes.

- [ ] Implement `fuzz/fuzz_text.cpp` (`extract_page_text`, `/ToUnicode` CMap parser, `SearchEngine`, and `replace_text_in_document`), `fuzz/fuzz_forms.cpp` (`/Annots` parser, AcroForm `/Fields` and `/XFA` parser, field mutation, and `/AP /N` synthesis), and `fuzz/fuzz_openxml_zip.cpp` (`ZipReader::open_bytes`, `Ole2Reader::open`, and `OfficeToPdf` OpenXML/OLE2 parsers) inside `fuzz/` and wire them into `recto-fuzz` and CI smoke runs. Done when: all 6 fuzz targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) compile with `/fsanitize=address,fuzzer` and run cleanly on `tests/corpus/seeds/`.
- [ ] Write `scripts/run-fuzz-campaign.ps1` supporting `-SecondsPerTarget <int>`, `-Minimize`, and `-UpdateSeeds` flags, executing all 6 targets (`fuzz_open`, `fuzz_filters`, `fuzz_render`, `fuzz_text`, `fuzz_forms`, `fuzz_openxml_zip`) sequentially and capturing libFuzzer's final `cov` (edge coverage), `ft` (feature count), `corp` (corpus count/bytes), `exec/s`, and `rss_mb` metrics into `build/fuzz-report.json`. Done when: `scripts/run-fuzz-campaign.ps1 -SecondsPerTarget 30` produces valid JSON metrics for all 6 targets. Cheaper substitute: treating the one-time M1 1-hour fuzz run (`D01 T03 §5`) as a permanent substitute for ongoing fuzzing as `recto-pdf` and `recto-core` grow in M3-M5.
- [ ] Ensure `scripts/run-fuzz-campaign.ps1` runs all 6 targets even if one target encounters a crash (`crash-*`, `leak-*`, `timeout-*`, `oom-*`), preserving the crash artifact in `build/fuzz-crashes/<target>/` and recording the reproducer command in `build/fuzz-report.json` before exiting with status code `1` at the end of the campaign. Done when: a simulated crash in target 1 still executes targets 2 through 6 and preserves the crash file.
- [ ] Implement `-Minimize` in `scripts/run-fuzz-campaign.ps1` using libFuzzer `-merge=1` to deduplicate new coverage-expanding inputs against `tests/corpus/seeds/` while enforcing the `5 MB` total corpus size budget (`D00 T03 §3`). Done when: `-Minimize` shrinks a scratch directory with duplicate seeds down to the minimal unique-feature set.
- [ ] Author `.github/workflows/fuzz-scheduled.yml` triggered on a cron schedule (`0 3 * * *`) and `workflow_dispatch`, uploading `build/fuzz-report.json` and any `build/fuzz-crashes/` artifacts on failure. Done when: workflow YAML validates cleanly and matches the invocation contract of `scripts/run-fuzz-campaign.ps1`.
- [ ] Run two consecutive campaigns via `scripts/run-fuzz-campaign.ps1`, record their dated summaries in `fuzz/campaigns/<YYYY-MM-DD>-campaign-summary.md`, and update `tests/corpus/PROVENANCE.md` for any merged seed inputs. Done when: `fuzz/campaigns/<YYYY-MM-DD>-campaign-summary.md` is committed and `recto-tests --gtest_filter=Corpus.Provenance` passes.
- [ ] Add XPS and OpenXPS spool inputs and the `VirtualPrintJob::validate_spool` entry point of `D07 T01 §17` to the `fuzz_openxml_zip` dispatcher and seeds. Done when: `recto-fuzz.exe fuzz_openxml_zip` runs `60 s` on the XPS seeds without findings and the seeds are registered in `tests/corpus/PROVENANCE.md`.
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
- [ ] Create a padded malformed PDF input (`scratch/planted-crash-unminimized.pdf`, e.g., a truncated object stream with 4 KB of padding bytes) and run `python scripts/triage-fuzz-crash.py` under a canary trigger compiled only when the fuzz build defines `RECTO_FUZZ_TRIAGE_CANARY` (never in a shipping `recto-pdf` build, which `scripts/check-security-symbols.py` confirms) to prove `recto-fuzz.exe` catches the canary crash and `-minimize_crash=1` strips the padding down to the minimal reproducer `tests/corpus/broken/fuzz-triage-proof.pdf`. Done when: `tests/corpus/broken/fuzz-triage-proof.pdf` is `< 256 bytes` and reproduces the canary trigger deterministically.
- [ ] Remove the test canary trigger (or guard it strictly behind a unit-test flag) so production `recto-pdf` returns a clean `PdfError` on `tests/corpus/broken/fuzz-triage-proof.pdf`, and register `fuzz-triage-proof.pdf` in `tests/corpus/PROVENANCE.md`. Done when: `recto-cli info tests/corpus/broken/fuzz-triage-proof.pdf` exits `2` with the structured error JSON of the `D01 T03 §1` contract and `Corpus.Provenance` passes.
- [ ] Add `SecurityAudit.FuzzTriageProofRegression` in `tests/recto-tests/security_audit_tests.cpp` opening `tests/corpus/broken/fuzz-triage-proof.pdf` under ASan and asserting it returns `PdfError` without crashing. Done when: `recto-tests --gtest_filter=SecurityAudit.FuzzTriageProofRegression` passes under ASan.
- [ ] Commit: `fuzz: add crash triage runbook, minimization script, and proven regression test`

**Test checkpoint:** `python scripts/triage-fuzz-crash.py` minimization output (`before_bytes -> after_bytes`), `recto-cli info tests/corpus/broken/fuzz-triage-proof.pdf` output, and `recto-tests --gtest_filter=SecurityAudit.FuzzTriageProofRegression:Corpus.Provenance` passing under ASan quoted.

## 5. `recto-pdf` public API boundary inventory (`recto_pdf.h` ownership and mutability table)

Verify foundation plan section 3.1 and decision D13 against the as-built `src/pdf/` headers by writing a deterministic Python inspection script (`scripts/inventory-pdf-boundary.py`) that parses the public headers exported by `recto-pdf` (`src/pdf/recto_pdf.h` and its included public headers), enumerates every public struct, class, and function in `namespace recto::pdf`, verifies that zero `recto-ui`, `recto-core`, or `recto-services` types leak into `src/pdf/`, and classifies every public entry point for the post-v1 broker split (`Broker-Internal`, `IPC-Request-Response`, `Shared-Memory-Snapshot`, or `Stateless-Helper`).

**Phase:** 6
**Needs:** none
**Surface:** no

**Build order:**

1. Author `scripts/inventory-pdf-boundary.py` scanning `src/pdf/*.h` to extract all public declarations in `namespace recto::pdf`, check include hygiene (zero includes from `src/core`, `src/services`, `src/app`, `src/ui`, or WinRT headers), and output a deterministic Markdown table (`--check` and `--write` modes). Done when: `python scripts/inventory-pdf-boundary.py --check` exits `0` and verifies zero upward header dependencies.
2. Add unit tests in `tests/scripts/test_pdf_boundary_inventory.py` verifying that `scripts/inventory-pdf-boundary.py` detects any uncategorized public function or illegal upward `#include` in `src/pdf/`. Done when: `python -m unittest tests.scripts.test_pdf_boundary_inventory` passes.

- [ ] Implement `scripts/inventory-pdf-boundary.py` to scan all header files in `src/pdf/` and verify that no header includes anything from `src/core/`, `src/services/`, `src/app/`, `src/ui/`, or `winrt/`. Done when: `python scripts/inventory-pdf-boundary.py --check` passes on the live `src/pdf/` directory and fails on a synthetic header containing `#include "../core/core.h"`. Cheaper substitute: writing a hand-crafted table from memory without machine-checking it against the actual headers in `src/pdf/`.
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

## 7. FIPS mode detection and the algorithm policy in `recto-pdf`

Foxit PDF Editor honours Windows FIPS mode for regulated customers (Acrobat Pro has a FIPS mode setting), and a Windows machine in FIPS mode must not be offered or asked to use RC4 or MD5. This section adds the detection and the single algorithm policy that the readers, writers and signers consult. Detection uses `BCryptGetFipsAlgorithmMode` from `bcrypt.dll`, which reports whether Windows FIPS policy is enabled; Recto reads it once per process because Windows applies a change after a restart. The policy adds no setting: it follows the operating system.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/pdf/crypto_policy.h/.cpp` with `FipsProbe`, `AlgorithmPolicy` and the typed error. Done when: `Fips.PolicyTable` passes with an injected probe.
2. Wire the real probe and expose the state to `recto-cli info --json`. Done when: `Fips.RealProbeReturnsState` passes and the JSON carries `fips_mode` as one of the three state strings.

- [ ] Append an `AlgorithmNotPermittedByFips` code to `ErrorCode` in `src/pdf/error.h` after the last code, as `D01 T02 §18` did, with a stable string name and a message naming the algorithm. Done when: `Fips.ErrorCodeStable` checks the code, name and that the message names RC4 or MD5. **Corrected 2026-10-23:** (`D01 T02 §22`: named unqualified until this item appends the code).
- [ ] Implement `FipsProbe` as an interface with the default `BCryptFipsProbe` calling `BCryptGetFipsAlgorithmMode` once per process, returning a three-state result (`Enabled`, `Disabled`, or `Unknown` when the call fails, which is logged to the D16 ring log) and treating `Unknown` as `Enabled` for every refusal decision, so a failed query never enables a prohibited algorithm. Done when: `Fips.RealProbeReturnsState` runs the real probe and gets `Enabled` or `Disabled` (never `Unknown` on a healthy host), and `Fips.ProbeFailureRefuses` injects a failing call and finds RC4 and MD5 operations refused. Cheaper substitute: reading the Lsa registry value directly, which skips the supported API.
- [ ] Implement `AlgorithmPolicy::check(Algorithm, Use)` returning `PdfResult<void>`: in FIPS mode refuse RC4 (any key length), MD5, and encryption revisions that depend on them (`R=2` to `R=4`); allow AES with SHA-2 key derivation (`R=6`), SHA-256 or stronger digests and `BCryptGenRandom`. The policy is keyed by algorithm and by use: creating or verifying an MD5 or SHA-1 digest is refused in FIPS mode (a signature that needs one is reported as `DigestUnavailableUnderFips`, never as verified), while SHA-1 used only as a certificate thumbprint identifier is not a cryptographic use and is allowed. Outside FIPS mode every existing algorithm stays allowed for reading. Done when: `Fips.PolicyTable` checks each algorithm and use in both modes.
- [ ] Add `fips_mode` to `recto-cli info --json` and to the crash bundle `version.json` of `D07 T02 §4` as a string, `"enabled"`, `"disabled"` or `"unknown"` (never a Boolean, so a failed probe is never shown as an observed state), plus `fips_probe_status` holding the `NTSTATUS` of the failed call as `0x` hex when the state is `"unknown"` and `null` otherwise. Done when: `Fips.CliInfoReportsMode` injects each of the three probe results and finds the matching string and status, and `CrashReport.BundleContainsOnlyThreeAllowlistedFiles` still passes.
- [ ] Commit: `feat(security): detect Windows FIPS mode and add the algorithm policy`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Fips.ErrorCodeStable:Fips.RealProbeReturnsState:Fips.ProbeFailureRefuses:Fips.PolicyTable:Fips.CliInfoReportsMode` passes under ASan.

## 8. Enforce the FIPS policy in decryption, encryption writers, signing digests and the CLI

The detection of §7 only protects users if every code path that opens an encrypted file or writes an `/Encrypt` dictionary asks it. This section routes those paths through `AlgorithmPolicy`, refuses RC4 and MD5 work in FIPS mode with a typed error and a readable reason, and checks by source scan that no writer bypasses the gate. Recto writes only `AES-256` (`R=6`) for new encrypted files (`D05 T01 §7`), so the refusal that matters for new files is saving or updating a file whose existing handler uses RC4 or MD5.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Gate the reader and every writer. Done when: `Fips.DecryptRefusesRc4` and `Fips.WritersUseGate` pass.
2. Gate signing digests and show the refusal in the app and CLI. Done when: `Fips.SignatureDigests` and `Fips.CliExitCode` pass.

- [ ] Call `AlgorithmPolicy::check` in the Standard Security Handler open path of `D01 T01 §9` before any key derivation, so a FIPS machine returns `AlgorithmNotPermittedByFips` for `R=2` to `R=4` files instead of depending on the CNG provider failing. Done when: `Fips.DecryptRefusesRc4` opens `encrypted-rc4-128.pdf` and `encrypted-aes-128.pdf` with an injected FIPS probe and gets the typed error, while `encrypted-aes-256.pdf` opens.
- [ ] Route every writer that emits an `/Encrypt` dictionary (`PdfWriter`, `IncrementalWriter` for encrypted bases, the `AES-256` writer of `D05 T01 §7` and the public-key writer of `D05 T01 §11`) through one `check_encrypt(handler)` function. Done when: `Fips.WritersUseGate` scans `src/pdf` and fails when a writer emits `/Encrypt` without calling it, and `Fips.IncrementalUpdateOfRc4Refused` saves an edit to the RC4 fixture and gets the typed error with the original file untouched.
- [ ] Restrict signature creation to SHA-256 or stronger digests in FIPS mode, and for a signature whose digest is SHA-1 or MD5 report the verification as unavailable under policy (`DigestUnavailableUnderFips`) rather than verified, consistent with the §7 use table. Done when: `Fips.SignatureDigests` signs with SHA-256, and verifies SHA-1 and MD5 fixtures as `DigestUnavailableUnderFips` in FIPS mode and as verified (with the existing weak-digest warning) outside it.
- [ ] Map the typed error to the localised key `Error_AlgorithmNotPermittedByFips` ("This file uses RC4 or MD5 encryption, which Windows FIPS mode does not allow."), shown by the could-not-open dialog (`D02 T01 §11`) and the Protect flow (`D05 T02`), and make `recto-cli` exit `1` with the code name in its JSON error. Done when: `Fips.CliExitCode` checks the exit code and JSON, and `Localization.EveryErrorCodeHasMessage` still passes.
- [ ] Confirm that the Action Wizard encrypt step (`D04 T07 §5`) surfaces the typed error as a per-file failure rather than a crash. Done when: `Fips.ActionStepReportsFailure` runs the step on the RC4 fixture with an injected FIPS probe and finds a failed row with the code name.
- [ ] Commit: `feat(security): refuse RC4 and MD5 work in Windows FIPS mode across readers, writers and signers`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Fips.DecryptRefusesRc4:Fips.WritersUseGate:Fips.IncrementalUpdateOfRc4Refused:Fips.SignatureDigests:Fips.CliExitCode:Fips.ActionStepReportsFailure` passes under ASan.

## 9. FIPS mode proof script, tested to refuse on an ordinary host

Injected probes prove the logic; only a host with Windows FIPS policy enabled proves the real call and the real CNG behaviour. This section authors the proof script and tests it on the campaign host, where it must refuse; the run on a FIPS-enabled test machine is an operator release-checklist item (`D99 T01 §10`) that records its evidence in `docs/security-audit.md`. The script refuses to continue unless `recto-cli info --json` reports `fips_mode` as `"enabled"`, so it never reports a pass on a normal machine.

> **Groomed 2026-10-22:** removed `attended`. Authoring the script and proving that it refuses on an ordinary host is unattended; switching a machine into FIPS mode and running it is the operator release-checklist section `D99 T01 §10`, and no section depends on that run (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `tests/scripts/verify_fips.ps1` with a `-CliPath` parameter. Done when: it exits `2` on this machine without FIPS mode and `tests/scripts/test_verify_fips.py` drives it with stub CLIs reporting `enabled`, `disabled` and `unknown`.
2. Add the `FIPS mode` section to `docs/security-audit.md` with the expected `[PASS]` lines and the status `pending operator run (D99 T01 §10)`. Done when: the section exists in `docs/security-audit.md` with the pending status.

- [ ] Write `tests/scripts/verify_fips.ps1`, which runs `recto-cli info --json`, stops with exit code `2` unless `fips_mode` is `"enabled"` (a `"disabled"` or `"unknown"` state both stop it), then opens each encrypted corpus fixture and encrypts a copy with `AES-256`. Done when: on a normal machine it exits `2` with the message "FIPS mode is not enabled on this host".
- [ ] Extend the script to expect `AlgorithmNotPermittedByFips` for `encrypted-rc4-128.pdf` and `encrypted-aes-128.pdf`, success for `encrypted-aes-256.pdf` and for a new `AES-256` file, and a signed fixture with SHA-256. Done when: the expectations are encoded and a failed one exits non-zero.
- [ ] Add the `FIPS mode` section to `docs/security-audit.md` naming the script, the expected `[PASS]` lines and the status `pending operator run (D99 T01 §10)`; the operator replaces the status with the quoted output, host build and date. Done when: the section holds the expected lines and the pending status.
- [ ] Commit: `test(security): add the FIPS mode proof script and its pending audit record`

**Test checkpoint:** `pwsh -NoProfile -File tests/scripts/verify_fips.ps1` exits `2` with "FIPS mode is not enabled on this host" on this machine, and `python -m unittest tests.scripts.test_verify_fips` passes; the exit `0` run on a FIPS-enabled host is quoted by `D99 T01 §10`.

## 10. MSVC code analysis (`/analyze`) over every module, clean, as a CI job

> **Started:** 2026-11-26T23:34:00Z

Filed 2026-11-10. Recto's engine parses hostile files, and its only compile-time checks today are `/W4 /WX` and, outside Debug, `/sdl`; nothing in the tree turns on the compiler's own static analyzer (a search of every project, `Directory.Build.props` and `Directory.Build.targets` for `EnablePREfast`, `RunCodeAnalysis` and `/analyze` finds nothing). `/analyze` follows paths through a function and reports buffer overruns, null and uninitialized reads, arithmetic overflow before allocation, leaked handles and misused locks: the defects AddressSanitizer and fuzzing find only when a test or an input reaches them. This section runs it over all eight projects with the recommended native rules and the C++ Core Guidelines lifetime and bounds checks, fixes or justifies every finding, and keeps it clean with a CI job. It runs in CI and on demand, not in the local gate: the analyzer needs far more memory and time than a compile, and this host is short of both. **Corrected 2026-11-27:** Since `D00 T04 §8` made CMake and Ninja the default in `build/build.ps1`, `.\build\build.ps1 -Analyze` (and `.\build.cmd -Analyze`) selects the MSBuild (`Recto.sln`) path (`x64 Debug`, `-SkipTests`, sccache off) so `Directory.Build.props` and `Directory.Build.targets` own `obj-analyze`/`bin-analyze`, isolated analysis logs (`build\msbuild-analyze-Debug-x64.log`) and boundary manifests (`build\boundary-analyze\x64\Debug\`), `EnablePREfast`, a tracked `build/RectoAnalyze.ruleset` (allowlisted in `.gitignore` under `build/*`, including `NativeRecommendedRules` plus the high-confidence non-GSL C++ Core Check lifetime `C26815`/`C26816`/`C26822`, bounds `C26459`/`C26483` and type `C26437`/`C26478`/`C26494` rules alongside `NativeRecommendedRules`'s `C26800`/`C26829`/`C26859`/`C26495`, with zero rules disabled; the full GSL/low-confidence Core Check rule sets emitted 18,184 `C26823` `allotemp.*` false positives and 6,000+ GSL-only `C26446`/`C26472`/`C26481`/`C26482`/`C26485` style errors on `recto-pdf` alone), and per-project SARIF v2.1.0 aggregation (`scripts/merge-analyze-sarif.py`) into `build\analyze\<project>.sarif` for all 8 projects (`recto-pdf`, `recto-core`, `recto-services`, `recto-ui`, `Recto`, `recto-cli`, `recto-tests`, `recto-fuzz`). `build\analyze\` is cleaned before each run, every eligible TU's current-run `.nativecodeanalysis.sarif` is verified and merged (failing on any missing/unreadable/malformed report or unexpected project set), and findings are collected across all 8 projects during discovery and fixed at their owning source (`src/`, `tests/`, or `fuzz/`). Because trunk-based workflow on `main` (`AGENTS.md`) forbids feature branches, CI failure on a planted overrun is proved by running `.\build\build.ps1 -Analyze` locally on both a native C6386 overrun and a C++ Core Check violation (e.g. C26800 use-after-move) and asserting in `tests/scripts/test_build_ps1.py` that `.github/workflows/build.yml`'s `analyze` job runs on the intended events, checks out `inputs.sha || github.sha`, propagates non-zero exit codes (no `continue-on-error: true`, no exit swallowing, verified by a negative test), and uploads `build/analyze/*.sarif` with `if: always()`. Isolation is proved separately on unchanged sources by warming both plain CMake and plain MSBuild builds, running `.\build\build.ps1 -Analyze`, and asserting 0 rewritten objects in both backends. Review-skill and `AGENTS.md` updates cover `AGENTS.md`, `.agents/skills/review-todo-section/SKILL.md` and `.claude/skills/review-todo-section/SKILL.md` and describe `build.yml`'s actual trigger (code, build or workflow pushes to `main` and pull requests, skipping `todo/**`, `docs/**` and `**/*.md`-only changes).

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add an analysis build. Done when: `.\build\build.ps1 -Analyze` compiles every project under `/analyze` into isolated folders (`obj-analyze`, `bin-analyze`, `boundary-analyze`, `msbuild-analyze-Debug-x64.log`), verifies current-run coverage across all 8 projects, and writes one validated SARIF 2.1.0 log per project under `build\analyze\`.
2. Clear the findings. Done when: the analysis build completes all 8 projects with 0 findings, each finding fixed at its actual cause (`src/`, `tests/`, or `fuzz/`) or suppressed at its line with the reason.
3. Hold it in CI. Done when: `.github/workflows/build.yml`'s `analyze` job fails on a new finding, checks out `inputs.sha || github.sha`, and uploads SARIF logs even on failure.

- [x] Add `-Analyze` to `build/build.ps1` (MSBuild path, x64 Debug, tests not run, sccache off) setting `EnableAnalyze`, allowlist `!build/RectoAnalyze.ruleset` in `.gitignore`, add `build/RectoAnalyze.ruleset` (including `NativeRecommendedRules` plus high-confidence non-GSL C++ Core Check lifetime, bounds and type rules with zero disabled rules), add `scripts/merge-analyze-sarif.py` (cleans stale logs, verifies current-run compiler evidence for every eligible TU across all 8 projects, merges per-TU `.nativecodeanalysis.sarif` into one validated SARIF 2.1.0 log per project under `build\analyze\<project>.sarif`, refuses missing/unreadable/empty/stale reports), and in `Directory.Build.props` and `Directory.Build.targets` give that build isolated `obj-analyze`, `bin-analyze`, `boundary-analyze` and `msbuild-analyze-Debug-x64.log` paths, `EnablePREfast`, `CodeAnalysisRuleSet`, and findings as warnings that fail the build. Done when: `.\build\build.ps1 -Analyze` fails on a planted native C6386 overrun (`int a[4]; a[4] = 0;`) and separately on a planted C++ Core Check diagnostic (e.g. C26800 use-after-move), with each finding present in its project's `build\analyze\<project>.sarif`; unit tests in `tests/scripts/test_build_ps1.py` prove multi-TU SARIF aggregation preserves two findings from different TUs with rule IDs and locations, refuses missing/malformed reports, and fails if analysis folders/manifests/logs collide with plain ones; and warming both plain CMake and plain MSBuild builds then running `.\build\build.ps1 -Analyze` on unchanged sources leaves 0 rewritten objects in both backends.
- [x] Record the first full 8-project run (repeating or continuing discovery until every one of the 8 projects has been analyzed): every finding by rule, file and line, in `docs/reviews/06-quality/D06-T02-s10.md`, with the complete 8-project build's wall time and peak memory. Done when: the full 8-project list is recorded and each finding is marked a defect, a hardening, or a false report with the reason.
- [x] Fix every defect and hardening at its actual cause in `src/`, `tests/`, or `fuzz/` (a bounds check where the bound is missing, a checked multiply before an allocation, an annotation such as `_In_reads_` where the contract was unstated), each defect with a regression test that fails before the fix. Done when: the suite passes in Debug, Release and under ASan, and each fixed finding names its test in the findings file.
- [x] Suppress each false report at its line with `#pragma warning(suppress: NNNN)` and a comment giving the reason; no rule is turned off for a file, a project or the build. Done when: `.\build\build.ps1 -Analyze` reports 0 findings across all 8 projects, and a search finds no `/wd`, `disable:` or rule-set edit that silences an analysis rule more widely than one line.
- [x] Add an `analyze` job to `.github/workflows/build.yml` running `.\build\build.ps1 -Analyze` on `ref: ${{ inputs.sha || github.sha }}` (matching `build`/`asan`/`msbuild`), uploading `build/analyze/*.sarif` with `if: always()` (so failure runs still upload SARIF logs), with a timeout set from the measured complete 8-project time. Done when: the job passes on the pushed commit, and `tests/scripts/test_build_ps1.py` parses `.github/workflows/build.yml` to assert the `analyze` job runs on `push`/`pull_request`/`workflow_dispatch`, checks out `inputs.sha || github.sha`, propagates non-zero exit codes (failing if `continue-on-error: true` or exit-swallowing is introduced), and uploads `build/analyze/*.sarif` with `if: always()`.
- [x] Name the command in `AGENTS.md`'s Validation list and in both `.agents/skills/review-todo-section/SKILL.md` and `.claude/skills/review-todo-section/SKILL.md` as a check for sections that touch `src/pdf` parsing or decoding. Done when: all three name `.\build\build.ps1 -Analyze` and accurately state it runs in CI (`build.yml`) on code, build or workflow pushes to `main` and pull requests (skipping `todo/**`, `docs/**` and `**/*.md`-only changes).
- [x] Commit: `build: MSVC code analysis over every module, clean, in CI`

**Test checkpoint:** `.\build\build.ps1 -Analyze` exits 0 on the tree with 8 validated current-run SARIF 2.1.0 logs under `build\analyze\` covering every eligible TU of all 8 projects; separately planting `int a[4]; a[4] = 0;` and a Core Check violation (e.g. C26800 use-after-move) in `src/pdf/error.cpp` each makes `.\build\build.ps1 -Analyze` exit non-zero naming the line and recording the finding in `build\analyze\recto-pdf.sarif`; and `build\python.cmd -m unittest tests.scripts.test_build_ps1` passes.

> **Verified:** 2026-11-27 | §10 | 02e5d08dd22ad50d80d3abac430af12cde210817
> **Review:** `docs/reviews/d06-t02-s10.json`

## 11. CodeQL scanning of the C++ sources, with open alerts read by the run

> **Started:** 2026-11-27T05:51:00Z

Filed 2026-11-10. GitHub's CodeQL runs a second, independent static analysis (data flow from untrusted input to a sink across functions and files, which `/analyze` does not follow) at no cost for a public repository and with no load on the development host. The repository has three workflows (`build.yml`, `plan.yml`, `pins.yml`) and none scans code. This section adds the scan and, because an alert that nobody reads fixes nothing, a script that reads the open alerts so a run and the scheduled automations treat a new one as work. **Corrected 2026-11-27:** The repository now has four workflows (`build.yml`, `ci-red.yml`, `pins.yml`, `plan.yml`). `github/codeql-action`'s latest major action tag is `@v4` (`refs/tags/v4`), whereas its `/releases/latest` tag is `codeql-bundle-v2.27.6` (not a numeric `vN.M.P` version); `scripts/check-actions-pins.py` therefore falls back to `/repos/{repo}/git/matching-refs/tags/v` (highest `refs/tags/v<N>`) when `/releases/latest` is not a numeric version tag, with a regression test in `tests/scripts/test_pin_automations.py`. The CodeQL manual build invokes `.\build\build.ps1 -Platform x64 -Configuration Debug -SkipTests -NoSccache` so CodeQL's compiler tracer intercepts every `cl.exe` invocation rather than `sccache` cache hits, and `codeql.yml` verifies its generated SARIF 2.1.0 output (`scripts/code-scanning.py --verify-sarif`) to prove `security-extended` rules and all first-party production C++ source roots (`src/pdf/`, `src/core/`, `src/services/`, `src/ui/`, `src/app/`, `src/cli/`) were extracted. Push triggers include `src/**`, `libs/**`, `tests/**`, `fuzz/**`, `vcpkg.json`, `vcpkg-configuration.json`, `CMakeLists.txt`, `CMakePresets.json`, `cmake/**`, `Directory.Build.*`, `Recto.sln`, `build/**` and `.github/workflows/codeql.yml`. `scripts/code-scanning.py` queries `state=open&ref=refs/heads/main` with explicit pagination AND checks the latest `codeql.yml` run + uploaded analysis on `refs/heads/main` so a failed/cancelled scan (exit 1) or pending scan (exit 2) is never mistaken for a clean scan; its `--scheduled` mode uses bounded batches, run-matched delivery confirmation via `build/automations/recto-code-scanning/last.json`, and same-directory atomic replacement (`os.replace`) for `build/code-scanning/state.json`. The `recto-code-scanning` automation uses `{"at_logon_delay_minutes": 20, "every_minutes": 1440}` in `.conclave/automations/tasks.json`, and both `.agents/skills/process-phase/SKILL.md` and `.claude/skills/process-phase/SKILL.md` step 3 run ordinary `build\python.cmd scripts/code-scanning.py` with explicit actions for exits 0, 1, 2 and 3.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the scanning workflow and pin support. Done when: `.github/workflows/codeql.yml` and `scripts/check-actions-pins.py` are committed and pushed, the `codeql.yml` run on `main` finishes green, its SARIF verification proves `security-extended` and all first-party C++ production targets (`src/pdf/`, `src/core/`, `src/services/`, `src/ui/`, `src/app/`, `src/cli/`) were extracted, and an uploaded analysis exists on `refs/heads/main`.
2. Read the alerts where work is chosen and triage the first run. Done when: `scripts/code-scanning.py` enforces scan status + paginated `refs/heads/main` open alerts + bounded confirmed `--scheduled` reporting, all first-run alerts are reconciled in `docs/reviews/06-quality/D06-T02-s11.md` (fixed at their owning layer across `src/`, `tests/`, or `fuzz/` with a regression test and re-scanned green, or dismissed with written reason), the `\Recto\recto-code-scanning` task is registered, and both `process-phase` skills handle exits 0, 1, 2 and 3.

- [x] Add `.github/workflows/codeql.yml`: on pushes to `main` that change `src/**`, `libs/**`, `tests/**`, `fuzz/**`, `vcpkg.json`, `vcpkg-configuration.json`, `CMakeLists.txt`, `CMakePresets.json`, `cmake/**`, `Directory.Build.*`, `Recto.sln`, `build/**` or `.github/workflows/codeql.yml`, weekly on a schedule, and on demand (`workflow_dispatch`); language `c-cpp` with a manual build (`powershell -NoProfile -ExecutionPolicy Bypass -File build/build.ps1 -Platform x64 -Configuration Debug -SkipTests -NoSccache`), the `security-extended` query suite, `output: build/codeql-sarif` verified by `scripts/code-scanning.py --verify-sarif build/codeql-sarif` (requiring `security-extended` rules and extracted first-party sources from `src/pdf/`, `src/core/`, `src/services/`, `src/ui/`, `src/app/`, `src/cli/`), `security-events: write` on that job only, every action pinned as `scripts/check-actions-pins.py` requires (`actions/checkout@v7`, `github/codeql-action/*@v4`), and a timeout; extend `scripts/check-actions-pins.py` with a major-tag fallback for non-numeric latest release tags (tested in `tests/scripts/test_pin_automations.py`). Done when: `build\python.cmd scripts/check-actions-pins.py check` exits 0 with the new actions covered, `tests/scripts/test_code_scanning.py` verifies trigger path coverage (including `vcpkg.json` and `libs/vellum/...`) and SARIF verification failure when `src/pdf/` or `security-extended` is absent, and the pushed `codeql.yml` run on `main` passes green with an uploaded analysis on `refs/heads/main`.
- [x] Add `scripts/code-scanning.py` (stdlib Python, through `gh`): verifies the latest `codeql.yml` workflow run on `main` and uploaded `refs/heads/main` analysis (exit 1 if latest scan failed/cancelled or no analysis exists, exit 2 if latest scan is queued/in_progress, exit 3 if unreadable), reads all open alerts via paginated `repos/{owner}/{repo}/code-scanning/alerts?state=open&ref=refs/heads/main&per_page=100` (prints rule, severity, file, line, URL for each; exits 1 on any open alert, 0 when scan is green and zero open alerts, 3 on any first- or later-page read failure); add `--verify-sarif <dir>` for CI extraction/suite verification and `--scheduled` mode with bounded batching (`<= 15` alerts per email), delivery confirmation matched against `build/automations/recto-code-scanning/last.json`, and same-directory atomic `os.replace` for `build/code-scanning/state.json`. Done when: `tests/scripts/test_code_scanning.py` drives it with a fake `gh` through clean green scan (0), one open alert (1), two paginated pages with distinct alert numbers (1), later-page failure (3), dismissed alert (not counted), non-`main` ref alert (not counted), failed `codeql.yml` run with older green analysis and zero open alerts (1), pending `codeql.yml` run (2), unreadable API (3), and `--scheduled` failed delivery retry, foreign-run receipt rejection, over-cap batching, and atomic state replacement failure leaving prior state readable.
- [x] Run it where work is chosen: a `recto-code-scanning` task in `.conclave/automations/tasks.json` (daily via `every_minutes: 1440`, `at_logon_delay_minutes: 20`, running `python scripts/code-scanning.py --scheduled`), a matching row in `.conclave/automations/README.md`, and explicit handling in both `.agents/skills/process-phase/SKILL.md` and `.claude/skills/process-phase/SKILL.md` step 3 running ordinary `build\python.cmd scripts/code-scanning.py` before claiming a new row (exit 0 carry on, exit 1 fix open alert or failed scan first, exit 2 wait/recheck pending scan, exit 3 record unknown in run file, never treat as green). Done when: the task is registered on this machine (`Get-ScheduledTask -TaskPath \Recto\` and `pwsh .conclave/automations/install.ps1 -Check` exits 0), `scripts/tests/test_automations.py` passes, the README's task table lists it, and both skill copies specify all four exit codes for ordinary mode.
- [x] Push the workflow candidate to `main`, watch the pushed `codeql.yml` run complete green, and triage every alert from the first run: record all first-run alert IDs and the analyzed SHA in `docs/reviews/06-quality/D06-T02-s11.md`, fix each defect at its owning layer (`src/`, `tests/`, or `fuzz/`) with a regression test (and verify a subsequent green `codeql.yml` run on the fix commit), or dismiss false positives with written reason on GitHub. Done when: `build\python.cmd scripts/code-scanning.py` exits 0 on `main` after triage, every first-run alert ID is reconciled in `docs/reviews/06-quality/D06-T02-s11.md`, and `gh api "repos/rizonesoft/Recto/code-scanning/alerts?state=open&ref=refs/heads/main"` returns `[]`.
- [x] Commit: `ci: CodeQL scanning of the C++ sources, with alerts read by the run`

**Test checkpoint:** `build\python.cmd -m unittest tests.scripts.test_code_scanning tests.scripts.test_pin_automations` and `build\python.cmd -m unittest discover -s scripts/tests -p "test_automations.py"` pass; `build\python.cmd scripts/check-actions-pins.py check` exits 0; `pwsh .conclave/automations/install.ps1 -Check` exits 0; the `codeql.yml` run on the pushed `main` commit is green with `security-extended` and all first-party C++ production targets verified extracted; and `build\python.cmd scripts/code-scanning.py` exits 0 on `main` after triage.

> **Verified:** 2026-11-27 | §11 | 86429ab43bd1bec6f1dc49cbb0b65dd35abac695
> **Review:** `docs/reviews/d06-t02-s11.json`

## 12. clang-tidy bug, CERT and concurrency checks over the compile database

Filed 2026-11-10. A second compiler front end reads the same sources differently from MSVC, and clang-tidy's `bugprone-*`, `cert-*`, `concurrency-*` and `clang-analyzer-*` checks report use after move, dangling references and views, narrowing in size arithmetic, unchecked return values and lock misuse, several of which neither `/W4` nor `/analyze` reports. `clang-tidy.exe` ships beside `clangd.exe` in Visual Studio's LLVM tools, and `D00 T04 §12` writes the compile database it needs and proves clang reads every source, so this section adds no tool and depends on that one. It chooses the checks, clears the tree, and keeps it clean in CI.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Choose the checks and run them. Done when: a tracked `.clang-tidy` names them and one command runs clang-tidy over every database entry.
2. Clear the findings. Done when: the run reports none, each fixed at its cause or suppressed at its line with the reason.
3. Hold it in CI. Done when: a CI job fails on a new finding.

- [ ] Add a tracked `.clang-tidy` at the repository root: `bugprone-*`, `cert-*`, `concurrency-*`, `clang-analyzer-*` and `performance-*`, findings as errors, headers under `src/` included, and each check left out named with the reason (a style rule, or one that contradicts a foundation plan decision). Done when: the file parses (`clang-tidy --verify-config`) and lists every exclusion with its reason.
- [ ] Add `scripts/run-clang-tidy.py` (stdlib Python): runs the Visual Studio `clang-tidy.exe` (found as `build/clangd.cmd` finds clangd) over every entry of `build/compile_commands.json`, at most as many at once as `D00 T04 §3` allows compilers, prints each finding once, and exits 1 on any, 2 when the database or the tool is missing. Done when: `tests/scripts/test_run_clang_tidy.py` drives it with a fake tool through a clean run, a finding (1), a missing database (2) and a tool that crashes on one file (2, the file named), and a real run checks as many files as the database holds.
- [ ] Record the first full run by check, file and line in the findings file, each marked a defect, a hardening or a false report with the reason. Done when: the list is recorded with the run's wall time and peak memory.
- [ ] Fix every defect and hardening at its cause in `src/`, each defect with a regression test that fails before the fix. Done when: the suite passes in Debug, Release and under ASan, and each fixed finding names its test.
- [ ] Suppress each false report at its line with `// NOLINT(<check>): <reason>`; no check is turned off for a file or a folder beyond the exclusions `.clang-tidy` names with reasons. Done when: `python scripts/run-clang-tidy.py` exits 0, and a search finds no bare `NOLINT` without a check name and a reason.
- [ ] Add a `clang-tidy` job to `.github/workflows/build.yml` that builds x64 Debug, then runs `scripts/run-clang-tidy.py`, with a timeout set from the measured time. Done when: the job passes on the pushed commit, and a branch with a planted use after `std::move` fails it naming `bugprone-use-after-move`.
- [ ] Name the command in `AGENTS.md`'s Validation list. Done when: it is there with what it checks.
- [ ] Commit: `build: clang-tidy bug, CERT and concurrency checks, clean, in CI`

**Test checkpoint:** `python -m unittest tests.scripts.test_run_clang_tidy` passes; `python scripts/run-clang-tidy.py` exits 0 on the tree; and a use after `std::move` planted in `src/pdf/error.cpp` makes it exit 1 naming `bugprone-use-after-move` and the line.

## Verification

- `build.ps1 -Analyze`, `python scripts/run-clang-tidy.py` and `python scripts/code-scanning.py` all exit 0 on `main`, and CI runs the first two on every push and CodeQL on every source change.
- Foundation plan section 9 is auditable end-to-end from `docs/security-audit.md` and `recto-tests --gtest_filter=SecurityAudit.*` alone.
- The scheduled fuzz campaign (`scripts/run-fuzz-campaign.ps1`) and crash triage runbook (`fuzz/README.md` + `scripts/triage-fuzz-crash.py`) are proven on the repository and committed.
- `docs/specs/2026-10-14-broker-process-design.md` is verified against the as-built `src/pdf/` headers by `scripts/inventory-pdf-boundary.py --check`.
- `python scripts/todo-graph.py validate` passes.
