---
schema_version: 1
id: office-to-pdf
domain: 04-convert
status: active
title: "TODO-04 -- Office to PDF: Built-in OpenXML/RTF Engine, Office COM and LibreOffice"
track: M5
---

# TODO-04 -- Office to PDF: Built-in OpenXML/RTF Engine, Office COM and LibreOffice

> **Goal:** Deliver `Office -> PDF` conversion that works out of the box on every Windows 11 machine with zero external installs via Recto's built-in C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`) and `.rtf` parser + DirectWrite/Direct2D PDF layout engine (`src/core/office_to_pdf.cpp`), while also supporting optional 1:1 native Microsoft Office COM automation (`AutomationSecurity = msoAutomationSecurityForceDisable`) and LibreOffice headless execution when installed and selected.

> **Current state:** Measured 2026-10-14. No Office-to-PDF code exists yet. On 2026-10-14 decision D10 was expanded so Recto includes its own native C++20 OpenXML and RTF to PDF engine as the always-available built-in converter, alongside optional Microsoft Office COM and LibreOffice adapters behind `IOfficeConverter`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D7, D10, D12), 5 (`Office -> PDF` row) and 5.1 (`Office -> PDF` security and converter rules), 7 (M5 done-when: `.docx`, `.xlsx`, `.pptx`, `.rtf` convert cleanly and macro-bearing `.docm` verified not to execute its macro), 9 (security rules 3 and 5).
- `design/SPEC.md` sections 2 (Principle 5: macros never run), 9.5 (`PDF from Office` card), 9.7 (Converting: Office to PDF converter selector showing Automatic, Built-in OpenXML engine, Microsoft Office, LibreOffice), 9.9 (`Install Office or LibreOffice` dialog when an external suite is explicitly forced or needed for legacy binary `.doc`/`.xls`/`.ppt`).
- -> XREF: D04 T01 §2 -- registers the `PDF from Office` card, file-type applicability rules, and converter selection into the Convert page and `JobQueue`.
- -> XREF: D06 T02 §1 -- the security audit in `D06 T02 §1` re-runs the macro-suppression (`msoAutomationSecurityForceDisable` and `vbaProject.bin` ignore) and safe-subprocess (`CreateProcessW`) proofs built here.

## Outcome

- Any Windows 11 machine converts `.docx`, `.xlsx`, `.pptx`, and `.rtf` files to vector PDFs out of the box using Recto's built-in C++20 OpenXML/RTF layout engine (`src/core/office_to_pdf.cpp`), with zero external software installed.
- When Microsoft Office is installed and selected (or chosen by Automatic mode when preferred for complex legacy binary `.doc`/`.xls`/`.ppt` files), the STA COM adapter converts via Word, Excel, and PowerPoint with `AutomationSecurity = msoAutomationSecurityForceDisable` (3) so VBA macros never execute.
- When LibreOffice is installed and selected, the headless adapter runs `soffice.exe` via `CreateProcessW` in an isolated temp profile and a kill-on-close Win32 Job Object.
- If a user explicitly forces Microsoft Office or LibreOffice in Settings when neither is installed (or drops a legacy binary `.doc`/`.xls`/`.ppt` with no external suite present), the SPEC 9.9 dialog explains the choice and offers one-click fallback to Recto's built-in converter where applicable.

**Adjacency:** all=not-applicable (converter engines and adapters inside `D04 T01`'s Convert frame; converter preference setting is owned by `D02 T04 §2` and read here)

**Adjacency rationale:** The `PDF from Office` card, queue UI, and settings selector live in `D04 T01` and `D02 T04 §2`. This file owns the built-in OpenXML/RTF engine, the optional COM and LibreOffice adapters, converter detection, and the macro-safety proofs.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Built-in C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`) and `.rtf` to PDF engine | D04 T02 §1, D04 T03 §1 | [ ] |
| 2 | §2 | Converter detection and Microsoft Office COM adapter with macros forced off | §1 | [ ] |
| 3 | §3 | LibreOffice headless adapter with Job Object isolation | §1 | [ ] |
| 4 | §4 | Convert UI wiring, Settings integration and missing-suite dialog | §2, §3, D04 T01 §2, D02 T04 §2 | [ ] |

## 1. Built-in C++20 OpenXML (`.docx`, `.xlsx`, `.pptx`) and `.rtf` to PDF engine

Implement `src/core/office_to_pdf.cpp` using `zip_archive.cpp` (`D04 T03 §1`), a bounded XML/RTF reader (enforcing zip-bomb and entity-expansion caps and ignoring any `vbaProject.bin` macro streams), DirectWrite text layout (`IDWriteTextLayout`), and `PdfWriter` (`D04 T02 §1`):
- **Word (`.docx`) and Rich Text (`.rtf`):** Parse page setup (`w:pgSz`, `w:pgMar`), styles, numbering/lists, paragraphs, formatted runs (fonts, sizes, bold, italic, underline, colour, highlight), tables (`w:tbl` rows, cells, borders, shading), headers/footers, and embedded images (`word/media/*`); paginate with DirectWrite line/page breaking and emit vector text and image streams via `PdfWriter`.
- **Excel (`.xlsx`):** Parse `xl/sharedStrings.xml`, `xl/styles.xml`, and `xl/worksheets/sheet*.xml`; compute column widths and row heights, format numbers/dates, draw cell borders/fills, and paginate sheets onto portrait/landscape PDF pages via `PdfWriter`.
- **PowerPoint (`.pptx`):** Parse `ppt/presentation.xml` slide dimensions and `ppt/slides/slide*.xml` (plus slide layout/master shapes, text frames, fills, and images); emit one PDF page per slide via `PdfWriter`.
- Expose `recto-cli office-to-pdf <in.docx|xlsx|pptx|rtf> --out <out.pdf> [--engine <builtin|office|libreoffice>]`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement the `.docx` and `.rtf` parsers and DirectWrite pagination -> `PdfWriter` pipeline in `src/core/office_to_pdf.cpp`. Done when: `OfficeToPdf.BuiltinDocxAndRtf` converts corpus `.docx` (paragraphs, headings, tables, images) and `.rtf` files into valid multi-page vector PDFs whose extracted `PageText` contains all source text and whose `vbaProject.bin` in a `.docm` is ignored completely.
2. Implement the `.xlsx` (sheets, shared strings, styles, grid pagination) and `.pptx` (slides, text boxes, shapes, images) parsers in `src/core/office_to_pdf.cpp`, plus `recto-cli office-to-pdf --engine builtin`. Done when: `OfficeToPdf.BuiltinXlsxAndPptx` converts corpus `.xlsx` and `.pptx` files into valid vector PDFs with expected page counts and extractable cell/slide text under ASan.

- [ ] Built-in C++20 `.docx` and `.rtf` to PDF layout engine over DirectWrite and `PdfWriter`. Done when: `.docx` (with tables and images) and `.rtf` corpus files convert to vector PDFs with zero external dependencies under ASan. Cheaper substitute: requiring Microsoft Office or LibreOffice for every `.docx` conversion.
- [ ] Built-in C++20 `.xlsx` and `.pptx` to PDF layout engine and `recto-cli office-to-pdf --engine builtin`. Done when: `.xlsx` and `.pptx` corpus files convert to vector PDFs whose `PageText` matches the source cells and slide titles.
- [ ] Commit: `convert: implement built-in C++20 OpenXML (.docx, .xlsx, .pptx) and .rtf to PDF engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.Builtin*` passes under ASan across `.docx`, `.docm`, `.xlsx`, `.pptx` and `.rtf` corpus files with `--engine builtin`.

## 2. Converter detection and Microsoft Office COM adapter with macros forced off

Implement converter detection (`src/services/office_converter.cpp`) probing installed Microsoft Office (`Word.Application`, `Excel.Application`, `PowerPoint.Application` CLSIDs) and LibreOffice (`soffice.exe` in registry App Paths and standard Program Files paths), and implement the STA worker-thread Microsoft Office COM adapter (`src/services/office_com.cpp`) per foundation plan section 5.1:
- Sets `Visible = VARIANT_FALSE`, `DisplayAlerts = 0`, and **`AutomationSecurity = msoAutomationSecurityForceDisable` (3)** before opening any file.
- Opens with `ReadOnly = VARIANT_TRUE` and `AddToRecentFiles = VARIANT_FALSE`, exports to a sibling temp path (`ExportAsFixedFormat` / `SaveAs`), commits via `AtomicFileWriter`, closes without saving, and calls `Quit()` on the spawned application instance under a per-job timeout.

**Phase:** 5
**Needs:** windows-host, office
**Surface:** no

**Build order:**

1. Implement `detect_office_converters()` (reporting Built-in OpenXML engine as always present, plus detected Microsoft Office and LibreOffice installations, with injectable mock overrides for tests) and the STA COM adapter for Word, Excel and PowerPoint in `src/services/office_com.cpp`. Done when: on a host with Office installed, `.docx`, `.xlsx`, `.pptx` and `.rtf` convert to PDF via `--engine office` with zero orphaned `WINWORD.EXE` / `EXCEL.EXE` / `POWERPNT.EXE` processes after completion or cancellation.
2. Build a macro-bearing test fixture (`tests/corpus/convert/macro-canary.docm` containing an `AutoOpen` / `Document_Open` macro that attempts to write a canary file in `TempDir`) and run it through the COM adapter. Done when: the PDF is produced cleanly and the canary file does **not** exist on disk, proving `msoAutomationSecurityForceDisable` suppressed macro execution.

- [ ] STA COM adapter (`src/services/office_com.cpp`) for Word, Excel and PowerPoint with timeout and process cleanup. Done when: `OfficeToPdf.ComRoundTrip` converts all four formats and leaves zero orphaned Office processes. Cheaper substitute: invoking Office on the UI thread or leaving `AutomationSecurity` at its default value.
- [ ] Macro-canary proof (`macro-canary.docm`) verifying `msoAutomationSecurityForceDisable` prevents `AutoOpen` execution. Done when: `OfficeToPdf.ComMacroBlocked` passes with the canary file absent.
- [ ] Commit: `convert: add Microsoft Office COM adapter with AutomationSecurity forced off`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.Com*` passes on the Office-equipped host, confirming PDF output and zero canary file creation from `macro-canary.docm`.

## 3. LibreOffice headless adapter with Job Object isolation

Implement the optional LibreOffice headless adapter (`src/services/office_libreoffice.cpp`) per foundation plan section 5.1 and security rule 5: invoke `soffice.exe --headless --norestore --nolockcheck -env:UserInstallation=<isolated-temp-file-url> --convert-to pdf --outdir <temp>` using `CreateProcessW` (never `cmd.exe` or `system()`), assign the process to a Win32 Job Object configured with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, monitor `CancelToken` and a timeout, and commit the resulting PDF through `AtomicFileWriter`.

**Phase:** 5
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/services/office_libreoffice.cpp` with `CreateProcessW`, explicit argument quoting, isolated temp `UserInstallation` profile directory (cleaned up by `TempDir`), Win32 Job Object termination on cancel/timeout, and `AtomicFileWriter` commit. Done when: unit tests using a mock/stub process (and real `soffice.exe` when present) prove that tripping `CancelToken` terminates the child process tree immediately and leaves zero temp profile or partial PDF files behind.
2. Verify no macro execution on `macro-canary.docm` (LibreOffice headless disables macros by default; pass explicit macro-off configuration in the temporary profile as defense-in-depth). Done when: `OfficeToPdf.LibreOffice*` passes under ASan.

- [ ] `CreateProcessW` + Win32 Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) adapter with isolated temp profile and atomic commit. Done when: `OfficeToPdf.LibreOfficeCancelAndIsolation` proves child-process termination and temp-dir cleanup on cancel. Cheaper substitute: `std::system()` or shared default user profile that deadlocks when LibreOffice is already open.
- [ ] Commit: `convert: add LibreOffice headless adapter with Job Object process isolation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.LibreOffice*` passes under ASan.

## 4. Convert UI wiring, Settings integration and missing-suite dialog

Connect `IOfficeConverter` (`Automatic`, `Built-in OpenXML engine`, `Microsoft Office`, `LibreOffice`) to the `PDF from Office` card in `ConvertPage` (`design/SPEC.md` section 9.5), the `Office to PDF converter` selector in Settings (`SPEC` section 9.7, `D02 T04 §2`), and the `Install Office or LibreOffice` dialog (`SPEC` section 9.9, `D-Dialogs.png` when the user forces an external suite that is not installed or attempts to convert a legacy binary `.doc`/`.xls`/`.ppt` without Office/LibreOffice).

**Phase:** 5
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.5 (`PDF from Office` card enable/disable rule), 9.7 (Converting group selector), and 9.9 (`Install Office or LibreOffice` dialog with `D-Dialogs.png`).
**Job:** The user can convert Office documents to PDF seamlessly using the built-in engine or their preferred installed suite, and gets a clear, helpful dialog if they request an external suite that is not installed. Consumer: `JobQueue` and the viewer.
**Treatment:** Strategy selection reading `Settings -> Converting -> Office to PDF converter`, defaulting `Automatic` to the built-in OpenXML/RTF engine for `.docx`/`.xlsx`/`.pptx`/`.rtf` (or Microsoft Office COM when installed and preferred), and showing the SPEC 9.9 dialog via injectable detection state when an unavailable external suite is required. Cheaper substitute that fails the checkpoint: failing silently in the queue when an external suite is missing, or disabling `PDF from Office` on machines without Microsoft Office even though the built-in OpenXML engine is present.
**Chrome:** consume the Convert step 2 card from `D04 T01 §2`, the Settings row from `D02 T04 §2`, and the shared dialog style from `D02 T01 §4`.

**Build order:**

1. Wire `PDF from Office` card applicability in `ConvertPage` (enabled whenever `.docx`, `.doc`, `.xlsx`, `.xls`, `.pptx`, `.ppt` or `.rtf` files are in the chip list; disabled with `"Add Word, Excel or PowerPoint files to use this"` when only PDFs/images are present) and connect the Settings converter selector to live detection results. Done when: `ConvertPage` converts `.docx`, `.xlsx`, `.pptx` and `.rtf` end to end through the queue on a machine with zero external suites using the built-in engine.
2. Wire the `Install Office or LibreOffice` dialog (`D-Dialogs.png`) when an unavailable external suite is forced or required for legacy binary formats, and drive all three converter states (Built-in, Office COM, Missing-suite dialog) in tests. Done when: screenshot of the dialog matches `D-Dialogs.png` and all three states pass in `OfficeToPdf.Ui*`.

- [ ] `PDF from Office` card, Settings converter selector (`Automatic`, `Built-in OpenXML engine`, `Microsoft Office`, `LibreOffice`), and queue execution wired end to end. Done when: `.docx`, `.xlsx`, `.pptx` and `.rtf` convert via the queue using the built-in engine and via Office COM. Cheaper substitute: hard-coding one converter path and ignoring the Settings selector.
- [ ] `Install Office or LibreOffice` dialog (`SPEC 9.9`) driven and verified when an external suite is forced/needed and absent. Done when: display-session screenshot matches `D-Dialogs.png`.
- [ ] Commit: `convert: wire Office to PDF UI, Settings selector and missing-suite dialog`

**Test checkpoint:** `recto-tests.exe --gtest_filter=OfficeToPdf.*` passes, and display-session screenshots confirm the `PDF from Office` card states and the `D-Dialogs.png` missing-converter dialog.

## Verification

- M5 Office-to-PDF obligations (`docs/plans/2026-10-13-recto-foundation.md` section 7): `.docx`, `.xlsx`, `.pptx` and `.rtf` convert cleanly to PDF via the built-in C++20 engine and via Microsoft Office COM when installed, and `macro-canary.docm` is proven not to execute its macro.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` exit 0.
