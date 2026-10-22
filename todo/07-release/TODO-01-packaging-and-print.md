---
schema_version: 1
id: packaging-and-print
domain: 07-release
status: active
title: "TODO-01 -- Printing, Inno Setup (Full & Portable) Installer, Optional MSIX and Install Proof"
track: M6
---

# TODO-01 -- Printing, Inno Setup (Full & Portable) Installer, Optional MSIX and Install Proof

> **Goal:** Turn Recto into a printable, installable, and self-contained portable Windows product: verify the print API on the Windows SDK line in use via a side-by-side spike (`PrintManagerInterop` vs `ID2D1PrintControl`), build the non-blocking print pipeline (`Ctrl+P`, page ranges, copies, printer-DPI engine rendering, and automated `Print-to-PDF` verification), author the Inno Setup installer (`installer/recto.iss` built with `ISCC.exe`) supporting both **Full Installation** (`.pdf` file association, icons, Start Menu shortcut, and clean uninstaller) and **Self-Contained Portable Installation** (`/PORTABLE=1`, `<exe-dir>\data\portable.mode`, zero registry or `%LOCALAPPDATA%` footprint), build the release packaging pipeline (`build/build-installer.ps1` producing `RectoSetup-*.exe`, `Recto-Portable-*.zip`, and optional MSIX), and prove both Full Install and Portable Install end-to-end (`tests/scripts/verify_installer.ps1`).

> **Current state:** Measured 2026-10-14. No packaging or print code exists yet. `D02 T01 §1` authors the plain Win32 desktop `Recto.vcxproj` (unpackaged and MSIX-ready, with no Windows App SDK runtime, `D10 T01 §2`), manifest, and visual assets; `D02 T04 §1` and `§6` implement `AppPaths` Portable mode path resolution (`<exe-dir>\data\portable.mode`) and the in-app Portable mode setting toggle; `D02 T02` provides the document canvas and floating page toolbar Print button (`§8`). On 2026-10-14 decision D6 made Inno Setup (`ISCC.exe` at `C:\Program Files\Inno Setup 7\ISCC.exe`) the primary distribution mechanism supporting both Full Install and Self-Contained Portable Install alongside standalone `Recto-Portable-*.zip` archives, with MSIX / Microsoft Store packaging as an optional extra. 2026-10-22: WinUI 3 dropped for `recto-ui` (foundation plan D4); UI references rewritten against `todo/10-ui/`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D6 Inno Setup Full & Portable installer + optional MSIX), 2 (packaging row), 7 (M6 done-when and printing note), and 11 (print API open item).
- `resources/README.md` (asset-to-manifest mapping, `.ico` and `.pdf` association logo rules, splash background) and `design/SPEC.md` sections 3.2 (file-type icon behavior), 9.7 (Portable mode and Default PDF app rows), and 10 (`Ctrl+P` shortcut).
- -> XREF: D02 T01 §1 -- the self-contained `Recto.vcxproj` configuration, `Package.appxmanifest`, and `resources/` assets authored there are packaged by `§4` and `§5` here into the Inno Setup installer, Portable `.zip`, and optional MSIX.
- -> XREF: D02 T02 §8 -- the Floating Page Toolbar's Print button and `file.print` (`Ctrl+P`) command invoke `§2`'s print pipeline.
- -> XREF: D02 T04 §1 -- the Inno Setup Portable installer (`§4`) and `Recto-Portable-*.zip` archives (`§5`) pre-create `data\portable.mode` consumed by `AppPaths` (`D02 T04 §1`).
- -> XREF: D05 T01 §1 -- the shared page-range parser (`parse_page_ranges("1-3, 5, 8-10", page_count)`) is reused by `§2` for custom print page ranges.
- -> XREF: D07 T02 §7 -- v1 ships unsigned (owner decision 2026-10-22 (foundation plan section 1)): there is no Authenticode signing of `Setup.exe`, `Recto.exe` or the Portable `.zip`. `SHA256SUMS.txt` is written by `build/build-installer.ps1` (`§5` here) and verified, with the whole release bundle, in `D07 T02 §7`; the dependency audit `D07 T02 §3` is not part of signing. The Recto PDF driver is the one signed artefact, through the Hardware Dev Center (`§20`).
- -> XREF: D99 T01 §6 -- the operator proves the test-signed install and the end-to-end print through an installed driver that `§16` and `§19` build but never install.
- -> XREF: D99 T01 §7 -- the operator submits the driver to the Windows Hardware Dev Center and drops the signed catalog that the release gate of `§20` looks for.
- -> XREF: D06 T03 §9 -- the printer driver compatibility and failure isolation suite gating §2's print pipeline, and the clean-machine smoke (§12 there) over §4 to §7.
- -> XREF: D03 T01 §11 -- render_page_area for print area.
- -> XREF: D03 T01 §4 -- print area marquee.
- -> XREF: D02 T03 §11 -- print commands.
- -> XREF: D04 T02 §5 -- XPS to PDF for spool.
- -> XREF: D02 T01 §7 -- /virtual-print launch arg.
- -> XREF: D02 T04 §2 -- printer settings keys.
- -> XREF: D02 T04 §6 -- printer settings group.
- -> XREF: D07 T02 §6 -- runbook.
- -> XREF: D07 T02 §6 -- release kit.
- -> XREF: D03 T01 §11 -- render_page_area.
- -> XREF: D03 T03 §25 -- pages_with_comments for print.
- -> XREF: D04 T06 §18 -- print greyscale engine.
- -> XREF: D04 T07 §22 -- batch print step.
- -> XREF: D04 T08 §10 -- installer option.
- -> XREF: D06 T04 §21 -- the `D-Print.png` board and `design/SPEC.md` section 9.16 that §2, §9, §10, §14, §18 here build against.
- -> XREF: D10 T01 §2 -- the Win32 `Recto.exe` host (no Windows App SDK) that §4 and §5 here install and package.
- -> XREF: D10 T01 §4 -- the `ui::Window` HWND that §1 and §2 here show the system print experience for.
- -> XREF: D10 T02 §17 -- the `ui::Dialog` and the option controls (`D10 T02 §6`, `§7`, `§9`, `§12`, `§20`) that §10, §14 and §18 build on.
- -> XREF: D10 T03 §7 -- the `ui::Strings` tables that §11's tooltip string key lives in.

## Outcome

- Pressing `Ctrl+P` or clicking the Print button opens the system print dialog with live page preview, page-range and copy-count selection, and background printer-DPI rendering via an isolated `recto::pdf::Document` job (`§1`, `§2`), verified automatically by `Print.*` round-tripping through Microsoft Print to PDF / XPS (`§3`).
- `installer/recto.iss` (`§4`) and `build/build-installer.ps1` (`§5`) build `RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, and `Recto-Portable-ARM64.zip` (plus optional MSIX when `-EnableMsix` is passed), with every icon and manifest capability verified by an automated audit script.
- `tests/scripts/verify_installer.ps1` proves silent **Full Install**, `.pdf` `ProgId` and icon registration, `v1.0.0 -> v1.0.1` upgrade continuity of `settings.json` and `recents.json`, clean silent uninstall (`§6`), and zero-registry, zero-`%LOCALAPPDATA%` **Self-Contained Portable Install** isolation (`§7`).
- Explorer shows PDF thumbnails and a preview pane from a process-isolated `RectoShell.dll` (`§8`), and `print`, `printto`, `Combine in Recto` and `Convert to PDF with Recto` work on Explorer selections without ever changing the user's default PDF handler (`§9`).
- Printing can select pages by reverse order, comments, bookmark section and area, print as image, in grayscale or with text as outlines, and add crop, bleed and registration marks, all from the print options pane or `Ctrl+K` (`§11` to `§14`).
- The Full installer installs the `Recto PDF` v4 virtual printer when a Hardware Dev Center signed catalog is present, which converts any application's print job to a PDF in the user's session, with settings and a save window; Portable and per-user installs install nothing, and a missing driver signature removes the feature (the release builds without it and Settings shows "Not installed") rather than shipping an untrusted driver (`§15` to `§20`). Building, validating and gating the package is unattended; the test-signed install and the signing itself are operator sections `D99 T01 §6` and `§7`.

**Adjacency:** list=not-applicable (installed apps are listed by Windows, not Recto); document=applicable @ D07 T01 §2; settings=not-applicable (print choices live in the Windows print dialog; Portable mode and Default PDF app rows live in D02 T04 §6); reporting=not-applicable (crash reporting is D07 T02 §4); notifications=not-applicable (none originate here); permissions=not-applicable (manifest capabilities are declared in D02 T01 §1 and audited in §5); audit=not-applicable (release provenance is D07 T02 §1 and §3); exchange=not-applicable (no data import); reverse=applicable @ D07 T01 §6

**Adjacency rationale:** Printing (`§2`) is the viewer's physical/print document output surface delivered in M6 after `§1` resolves foundation plan section 11's print API open item. The reverse of installation is clean uninstallation for Full Install (`§6`) and directory deletion with zero host residue for Portable Install (`§7`).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Print API spike (`PrintManagerInterop` vs `ID2D1PrintControl`) & section 11 note | D02 T02 §8, D10 T01 §4 | [ ] |
| 2 | §2 | Non-blocking print pipeline (`src/app/printing/`, printer-DPI job, preview & `Ctrl+P`) | §1, D05 T01 §1, D06 T04 §21, D10 T01 §4 | [ ] |
| 3 | §3 | Automated `Print-to-PDF` verification seam (`Print.*` in `recto-tests`) against corpus | §2 | [ ] |
| 4 | §4 | Inno Setup installer (`installer/recto.iss`) for Full and Self-Contained Portable modes | D02 T01 §1, D02 T04 §1, D10 T01 §2 | [ ] |
| 5 | §5 | Release packaging pipeline (`build-installer.ps1`: Setup `.exe`, Portable `.zip` & MSIX) | §3, §4, D10 T01 §2 | [ ] |
| 6 | §6 | Automated Full Install, `.pdf` association, upgrade and uninstall proof (`installer`) | §5 | [ ] |
| 7 | §7 | Automated Self-Contained Portable Install zero-registry and zero-`%LOCALAPPDATA%` proof | §6 | [ ] |
| 8 | §8 | Explorer thumbnail and preview handlers (`IThumbnailProvider`, `IPreviewHandler`) | §4, D01 T02 §6 | [ ] |
| 9 | §9 | Explorer verbs: print, printto, Combine and Convert to PDF (`IExplorerCommand`) | §2, §4, D02 T01 §7, D05 T02 §3, D04 T01 §3, D06 T04 §21 | [ ] |
| 10 | §10 | Print layouts: booklet, multiple pages per sheet and poster tiling | §2, D06 T04 §21, D10 T02 §6, D10 T02 §7, D10 T02 §9, D10 T02 §17 | [ ] |
| 11 | §11 | Print page selection: reverse order, pages with comments, a bookmark section, an area | §2, D01 T01 §10, D10 T03 §7 | [ ] |
| 12 | §12 | Print render modes: print as image, grayscale and text as outlines | §2, §10 | [ ] |
| 13 | §13 | Printer's marks: crop marks, bleed marks, registration marks and page information | §10 | [ ] |
| 14 | §14 | Print options pane and Ctrl+K commands for page selection, render modes and marks | §11, §12, §13, D02 T03 §11, D06 T04 §21, D10 T02 §7, D10 T02 §9, D10 T02 §6, D10 T02 §12, D10 T02 §20 | [ ] |
| 15 | §15 | Recto PDF virtual printer design: v4 driver, hand-off and trust boundary | §1, D04 T02 §5 | [ ] |
| 16 | §16 | Recto PDF v4 driver package: manifest, INF, print capabilities and static validation | §15 | [ ] |
| 17 | §17 | Virtual printer spool hand-off: XPS to PDF in the user's session | §3, §15, D04 T02 §5, D02 T01 §7 | [ ] |
| 18 | §18 | Recto PDF virtual printer settings and the save window | §17, D02 T04 §2, D02 T04 §6, D02 T03 §11, D06 T04 §21, D10 T01 §4, D10 T02 §2, D10 T02 §4, D10 T02 §7, D10 T02 §9 | [ ] |
| 19 | §19 | Full installer integration and end-to-end proof for the Recto PDF printer | §6, §9, §16, §17, §18, D07 T02 §6 | [ ] |
| 20 | §20 | Recto PDF driver signature check, signing runbook and release gate | §5, §16, D07 T02 §6 | [ ] |

## 1. Print API spike (`PrintManagerInterop` vs `ID2D1PrintControl`) & section 11 note

Resolve foundation plan section 11's open item 4 (print API verification: `IPrintManagerInterop::GetForWindow` + `ID2D1PrintControl` against the `ui::Window` HWND vs the WinRT print document source, verified on the Windows SDK line in use) through an empirical spike in `src/app/printing/`: prototype both candidate Win32/WinRT print paths on the plain Win32 `Recto.exe` and on its full-trust MSIX build (the `ui::Window` `HWND` of `D10 T01 §4`), evaluate each on (a) unpackaged `HWND` compatibility without `ElementNotAvailable` / package-identity failures, (b) live print preview support, (c) page-range and copy-count option binding, and (d) vector `ID2D1CommandList` vs printer-DPI raster fidelity, and record the dated decision and reversal cost in both `docs/plans/2026-10-13-recto-foundation.md` section 11 and `docs/specs/2026-10-14-print-architecture.md`.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Build spike harnesses for Candidate A (`IPrintManagerInterop::GetForWindow(hwnd)` + `Windows.Graphics.Printing.PrintManager` / `IPrintDocumentSource`) and Candidate B (`ID2D1Device::CreatePrintControl` + `IPrintDocumentPackageTarget` / Win32 `PrintDlgExW` + `StartDocPrinter`) on the unpackaged `Recto.exe` `ui::Window` HWND (`D10 T01 §4`). Done when: both candidates are executed against `Microsoft Print to PDF` on Windows 11 and their exact behavior, HRESULTs, and preview/vector fidelity are captured.
2. Record the winning architecture, the concrete failure or limitation of the losing path (especially under unpackaged Inno Setup / Portable mode where WinRT `PrintManager` may require fallback or `ID2D1PrintControl`), and the reversal cost in `docs/specs/2026-10-14-print-architecture.md` and `docs/plans/2026-10-13-recto-foundation.md` section 11. Done when: foundation plan section 11 item 4 (print API verification) is marked resolved with the date the spike ran and matches `docs/specs/2026-10-14-print-architecture.md`, and items 1, 2, 3 and 5 are unchanged.

- [ ] Test Candidate A (`IPrintManagerInterop::GetForWindow` + `ShowPrintUIForWindowAsync`) from unpackaged `Recto.exe` (`WindowsPackageType=None`) and from portable mode (`<exe-dir>\data\portable.mode`), recording exact `HRESULT`s, preview behavior, and `ID2D1PrintControl` command-list handoff. Done when: Candidate A's behavior in unpackaged and packaged modes is documented with measured results. Cheaper substitute: choosing a print API from documentation alone without testing whether it works in an unpackaged (`WindowsPackageType=None`) Inno Setup / Portable build.
- [ ] Test Candidate B (Win32 `PrintDlgExW` / `IPrintDocumentPackageTarget` + `ID2D1Device::CreatePrintControl` streaming `ID2D1CommandList` pages recorded from `DisplayList` at printer DPI) in unpackaged and portable modes against `Microsoft Print to PDF`. Done when: Candidate B produces a valid multi-page output PDF from `tests/corpus/render/vector-shapes.pdf`.
- [ ] Compare vector retention (`ID2D1CommandList` preserving vector paths and glyph runs into the XPS/PDF print spooler) versus fallback rasterization at the printer's reported `dpiX`/`dpiY` (`300`/`600` DPI) for pages with unsupported blend modes. Done when: `docs/specs/2026-10-14-print-architecture.md` defines the exact `DisplayList -> ID2D1CommandList` print translation contract.
- [ ] Verify non-interactive programmatic print-to-file (`PrintToStream` / `Microsoft Print to PDF` / XPS print package target without modal UI block) so `§3`'s `recto-tests` suite can test printing headlessly. Done when: the programmatic print entry point writes a spool file in a `TempDir` with zero modal dialogs.
- [ ] Update `docs/plans/2026-10-13-recto-foundation.md` section 11 item 4 (print API verification, not item 1, the trademark reservation) with a dated resolution naming the chosen print architecture, why the alternative lost or how the two compose, and the cost of changing. Done when: section 11 item 4 is resolved, item 1 (trademark) is unchanged, and `python scripts/todo-graph.py validate` exits `0`.
- [ ] Commit: `docs: record print API spike on the Win32 window and architecture decision`

**Test checkpoint:** the spike test log against `Microsoft Print to PDF` in unpackaged mode and the dated resolution paragraph of item 4 in `docs/plans/2026-10-13-recto-foundation.md` section 11 are quoted verbatim.

## 2. Non-blocking print pipeline (`src/app/printing/`, printer-DPI job, preview & `Ctrl+P`)

Implement the production print pipeline in `src/app/printing/PrintService.h` and `src/app/printing/PrintService.cpp` on the architecture selected in `§1`: wire `file.print` (`Ctrl+P`, the Floating Page Toolbar Print button from `D02 T02 §8`, the Classic File menu Print item, and the `Ctrl+K` Command Palette `Print...` entry) to open the system print experience for the active document tab, open a dedicated background `recto::pdf::Document` instance (never sharing the viewer's `DocumentActor` or blocking the UI thread), parse custom page ranges via `D05 T01 §1`'s `parse_page_ranges`, honor copy count, orientation (`Auto portrait/landscape per page`, `Portrait`, `Landscape`), and page scaling (`Fit to printable area`, `Actual size 100%`), render preview and spool pages at printer DPI with cooperative `CancelToken` abort, and surface clear error feedback if the printer or spooler fails.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 10 (`Ctrl+P` triggers system print dialog) and section 8.2 (Floating Page Toolbar Print button, the `RectoIcons` `print` glyph of section 6). Design board: `design/SPEC.md` section 9.16 with `design/screenshots/D-Print.png` (drawn by `D06 T04 §21`).
**Job:** A user viewing any open PDF can press `Ctrl+P` (or click Print), choose a printer, copies, orientation, and page range (all pages, current page, or custom ranges like `1-3, 5`), preview the pages, and print at crisp printer resolution without freezing the viewer window. Consumer: Windows print spooler, physical printers, and `Microsoft Print to PDF`.
**Treatment:** Dedicated background print worker opening its own `recto::pdf::Document` handle, recording `DisplayList` commands into `ID2D1CommandList` / printer-DPI targets (`300`/`600` DPI) with `CancelToken` cancellation on dialog dismiss or job abort. Cheaper substitute that fails the checkpoint: blitting low-resolution `96 DPI` screen viewport tiles to the printer or running print rendering synchronously on the UI thread.
**Chrome:** Consume the platform print dialog and wire the existing `file.print` command in `CommandRegistry` (`D02 T01 §9`) and Floating Page Toolbar (`D02 T02 §8`). Do not invent a custom print dialog shell: the system print experience is shown for the `ui::Window` HWND through `IPrintManagerInterop::GetForWindow`.

**Build order:**

1. Implement `recto::app::PrintJobOptions` (`page_range_spec`, `copies`, `orientation_mode`, `scale_mode`, `dpi_x`, `dpi_y`, `include_annotations`) and the background print renderer in `src/app/printing/PrintService.cpp` that opens a separate `recto::pdf::Document` instance (passing the document session decryption password when opening an unlocked encrypted PDF) and renders requested pages via `DisplayList` into `ID2D1CommandList` / printer-DPI surfaces with `CancelToken` checks. Done when: `PrintService::spool_document_async` renders multi-page documents off the UI thread and aborts in `< 20 ms` when `CancelToken::cancel()` is signaled.
2. Wire `PrintService::show_print_dialog(hwnd, active_tab_session)` to `file.print` (`Ctrl+P`, Floating Page Toolbar Print button, Classic File menu, and `Ctrl+K` Command Palette), connecting preview page generation, custom page-range validation (using `parse_page_ranges`), auto-orientation per page's rotated `CropBox`, and non-modal error reporting via `AnnouncementService` / toast if spooling fails. Done when: pressing `Ctrl+P` on an open PDF opens the system print dialog, updates preview on range/orientation changes, and prints without blocking the UI thread.

- [ ] Open an independent `recto::pdf::Document` instance on a background print worker thread for every print job (never calling `recto::pdf::*` on the UI thread or sharing the viewer's `DocumentActor` instance, per foundation plan section 4.5). Done when: `Print.UsesIsolatedDocumentInstanceOffUiThread` proves `std::this_thread::get_id() != ui_thread_id` and verifies the viewer's `DocumentActor` processes scroll/tile requests concurrently during a print job. Cheaper substitute that fails the checkpoint: rendering print pages on the UI thread or locking the viewer's `DocumentActor` while spooling a 100-page print job.
- [ ] Reuse `D05 T01 §1`'s `parse_page_ranges(spec, page_count)` for custom print ranges (`All pages`, `Current page`, and custom ranges such as `"2-4, 7"`), surfacing invalid range syntax directly in the print options state without crashing or printing wrong pages. Done when: `Print.PageRangeSelection` verifies `"2-3"` on a 4-page PDF spools exactly pages 2 and 3 in order.
- [ ] Support `Auto` orientation (matching each page's effective rotated `CropBox` aspect ratio so mixed portrait/landscape PDFs rotate each sheet automatically) as well as forced `Portrait` and `Landscape`, plus `Fit to printable area` and `Actual size (100%)` scaling. Done when: `Print.MixedOrientationAndScale` verifies a 2-page PDF (page 1 portrait `612 x 792`, page 2 landscape `792 x 612`) sets portrait on sheet 1 and landscape on sheet 2 in `Auto` mode.
- [ ] Render print pages from `DisplayList` into `ID2D1CommandList` / printer-DPI (`300+ DPI`) targets rather than screen-resolution `96 DPI` viewport tiles, including visible `/Annots` and filled `/AcroForm` appearance streams (`/AP /N`) that Do Not Carry the `NoView` or `Print`-disabled annotation flag (`F` bit 3 `Print` per ISO 32000-1 Table 165). Done when: `Print.RespectsAnnotationPrintFlag` verifies annotations with the `Print` bit set appear on printed output while annotations without the `Print` bit are omitted.
- [ ] Wire `CancelToken` into both preview generation and final spooling so dismissing the print dialog or clicking Cancel mid-job aborts the background print worker within `< 20 ms` and deletes any incomplete spool file. Done when: `Print.CancelMidSpoolCleansUp` cancels a 50-page print job on page 3 and verifies `< 20 ms` abort and zero leftover temp files.
- [ ] Wire `file.print` (`Ctrl+P`), the Floating Page Toolbar Print button (`D02 T02 §8`), the Classic File menu `Print...` item (`D02 T01 §10`), and the `Ctrl+K` Command Palette (`D02 T01 §9`) to `PrintService::show_print_dialog`, keeping the action disabled when no document tab is active (on Home, Convert, or Settings). Done when: all 4 entry points invoke `PrintService` when a PDF is open and are disabled on Home.
- [ ] Read paper sizes, custom forms and roll sizes from the selected printer's capabilities, default to the document page at 100% when the driver has no match, and persist paper, duplex, colour and scale per printer. Done when: `Print.DriverPaperSizesAndPersistence` lists a stub driver's 62 by 100 mm form and preselects the previous session's choices.
- [ ] Send in the PrintTicket only the options the user changed, so driver defaults for colour, duplex and paper apply untouched. Done when: `Print.TicketSendsOnlyChangedSettings` finds no colour or duplex override in an untouched job's ticket.
- [ ] Run spooling in a worker with a watchdog so a driver crash or hang ends the job with the full error chain while the viewer stays responsive, and query printer capabilities off the UI thread with a per-printer timeout. Done when: `Print.WorkerFailureIsolated` survives a stub driver that crashes the worker and shows the error dialog with the driver's HRESULT.
- [ ] Disable Print with a stated reason when the document forbids printing, and map `Current page` to the viewed physical page when page labels restart. Done when: `Print.PageRangeSelection` prints the viewed page of a labelled fixture and the restricted fixture shows the reason without opening the dialog.
- [ ] Add `printer_name` (empty means the system default) and `ticket_overrides` (only settings the caller changed: copies, duplex, colour, paper) to `PrintJobOptions` in `src/app/printing/PrintService.h`, resolve the name against the installed queues, and fail with `PrinterNotFound` listing the available names. Done when: `Print.NamedPrinterAndTicket` prints to a named Microsoft Print to PDF queue with `duplex` overridden and finds no other ticket feature set, and an unknown name returns `PrinterNotFound`. Cheaper substitute: always printing to the default printer.
- [ ] Add `PrintService::print_headless(doc_path, PrintJobOptions, cancel_token)`, which spools without opening the print dialog or any window, runs only inside the Recto app process (never a second helper process), and is called by callers that already hold the user's confirmation (the Action Wizard batch print step of `D04 T07 §22` shows its own confirmation first). Done when: `Print.HeadlessNoDialog` prints a corpus PDF with no top-level window created, honours the page range, `CancelToken` and watchdog of this section, and returns a typed error for a refused-print document without prompting. Cheaper substitute: calling the dialog path with the dialog hidden.
- [ ] Commit: `feat(print): implement non-blocking print pipeline with page ranges, printer-DPI rendering, and Ctrl+P`

**Test checkpoint:** `recto-tests --gtest_filter=Print.UsesIsolatedDocumentInstanceOffUiThread:Print.PageRangeSelection:Print.MixedOrientationAndScale:Print.RespectsAnnotationPrintFlag:Print.CancelMidSpoolCleansUp:Print.DriverPaperSizesAndPersistence:Print.TicketSendsOnlyChangedSettings:Print.WorkerFailureIsolated` passes under ASan.

## 3. Automated `Print-to-PDF` verification seam (`Print.*` in `recto-tests`) against corpus

Build the automated end-to-end print verification suite (`Print.*` in `tests/recto-tests/print_tests.cpp`): drive `PrintService::print_to_file_for_test` (using the programmatic XPS/PDF print package target established in `§1`) over `tests/corpus/render/` and `tests/corpus/convert/` fixtures, re-open the printed output in `recto::pdf::Document` (or `xps_to_pdf`), and verify page count, page dimensions, selectable text preservation, and visual render fidelity (`compute_ssim >= 0.98` against `recto-cli render` of the source pages at matching DPI).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `PrintService::print_to_file_for_test(doc_path, options, output_path, cancel_token)` in `src/app/printing/PrintService.cpp` and author `tests/recto-tests/print_tests.cpp` (`Print.EndToEndCorpusRoundTrip`, `Print.MiddleRangeAndCopies`, `Print.EncryptedUnlockedDocumentPrint`). Done when: `recto-tests --gtest_filter=Print.*` runs end-to-end without any interactive user prompt.
2. Verify visual fidelity (`compute_ssim >= 0.98`) and page-range accuracy across `vector-shapes.pdf`, `two-column-article.pdf`, `forms-and-annots.pdf`, and `enc-aes256-r6.pdf`. Done when: all `Print.*` tests pass under ASan.

- [ ] Implement `Print.EndToEndCorpusRoundTrip` printing `vector-shapes.pdf` and `two-column-article.pdf` through `PrintService::print_to_file_for_test` into a `TempDir`, rendering both the printed output and the source PDF at `144 DPI`, and asserting `compute_ssim >= 0.98` on every page. Done when: `Print.EndToEndCorpusRoundTrip` passes with `SSIM >= 0.98` quoted per page. Cheaper substitute: mocking the print service in tests so the actual Direct2D print control / spooler code path never runs.
- [ ] Implement `Print.MiddleRangeAndCopies` printing pages `"2-3"` of a 4-page corpus document with `copies = 2` and verifying that the resulting spool document contains the exact expected page sequence (`[2, 3, 2, 3]` or `2` collated copies of `[2, 3]`) and each page matches source page 2 and source page 3 (`SSIM >= 0.98`). Done when: `Print.MiddleRangeAndCopies` passes and proves page 1 and page 4 are excluded.
- [ ] Implement `Print.FormsAndPrintFlagAnnotations` printing `forms-and-annots.pdf` after filling a text field (`Tx`) and checking a checkbox (`Btn`), verifying that the filled field values and printable markup annotations render on the printed page. Done when: `Print.FormsAndPrintFlagAnnotations` passes `SSIM >= 0.98` against expected filled appearance.
- [ ] Implement `Print.EncryptedUnlockedDocumentPrint` opening `enc-aes256-r6.pdf` with its user password (`"recto-test"`) and printing it through `PrintService`, verifying the background print job authenticates and spools cleanly. Done when: `Print.EncryptedUnlockedDocumentPrint` produces a valid readable print output.
- [ ] Implement `Print.CorruptOrCancelledSpoolRecovery` verifying that an unwritable output path or mid-job `CancelToken` cancellation returns a structured error and deletes any partial output file in `TempDir`. Done when: `Print.CorruptOrCancelledSpoolRecovery` passes with zero leaked files or handles.
- [ ] Commit: `test(print): add automated Print.* Print-to-PDF visual and structural verification suite`

**Test checkpoint:** `recto-tests --gtest_filter=Print.*` passes 100% under ASan with per-page SSIM scores quoted.

## 4. Inno Setup installer (`installer/recto.iss`) for Full and Self-Contained Portable modes

Author the primary Inno Setup 7 installer script (`installer/recto.iss` compiled via `C:\Program Files\Inno Setup 7\ISCC.exe`) per decision D6: present a custom Installation Mode wizard page (and support silent CLI flag `/PORTABLE=1`) allowing the user to choose between:
1. **Full Installation (Default):** installs into `{autopf}\Recto` (or per-user `{localappdata}\Programs\Recto` under `/CURRENTUSER`), registers the `Recto.PDF` `ProgId`, `.pdf` `OpenWithProgids` file association, `DefaultIcon` (`resources/ico/pdf-file.ico` installed as `{app}\pdf-file.ico`; the optional MSIX build uses `Assets\FileTypes\PdfFile.png`), and `ApplicationCapabilities` for Windows Default Apps (`SPEC 3.2` and `9.7`), installs a plain Win32 `Recto.exe` (no Windows runtime or framework installer step), creates Start Menu (and optional Desktop) shortcuts, the Start Menu shortcut carrying the AppUserModelID (`System.AppUserModel.ID`) that the toast notifications of `D04 T01 §6` use, and registers the uninstaller.
2. **Self-Contained Portable Installation (`/PORTABLE=1`):** extracts `Recto.exe` + `recto-cli.exe` + `LICENSE` + `NOTICE.md` (a plain Win32 app, no framework runtime) into the user-selected folder, creates `<target-dir>\data\portable.mode`, sets `Uninstallable=not IsPortableMode` and `CreateUninstallRegKey=not IsPortableMode`, and writes **zero** registry keys, Start Menu shortcuts, or `%LOCALAPPDATA%` files. Portable mode registers no AppUserModelID, so Windows toast notifications are replaced there by in-app notices (default taken 2026-10-22; registering `HKCU\Software\Classes\AppUserModelId` instead would break the zero-registry proof of `§7`, and reversing the default costs that proof's exception list).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `installer/recto.iss` parameterized by `#define AppVersion`, `#define Arch ("x64" | "arm64")`, and `#define StageDir`, including `[Setup]`, `[Files]`, `[Icons]`, `[Registry]`, and `[Code]` Pascal script implementing `IsPortableMode()`, the Installation Mode wizard page (`Full Installation` vs `Self-Contained Portable Installation`), `/PORTABLE=1` CLI flag parsing (`ExpandConstant('{param:PORTABLE|0}') = '1'`), and `data\portable.mode` marker creation. Done when: `ISCC.exe` compiles `installer/recto.iss` for both `x64` and `arm64` with `0` warnings and `0` errors.
2. Add `[Registry]` entries in `installer/recto.iss` (guarded by `Check: not IsPortableMode`) registering `Software\Classes\Recto.PDF` (`FriendlyTypeName`, `DefaultIcon`, `shell\open\command`), `Software\Classes\.pdf\OpenWithProgids` (`Recto.PDF`), and `Software\RegisteredApplications` (`Recto`) so Windows `Settings -> Apps -> Default apps` lists Recto cleanly without forcing hijack of existing default `.pdf` handlers. Done when: every registry entry in `installer/recto.iss` uses `HKA` (respecting `PrivilegesRequiredOverridesAllowed=dialog commandline`) and is skipped when `IsPortableMode` is true.

- [ ] Configure `[Setup]` in `installer/recto.iss` with `AppId`, `AppName=Recto`, `AppPublisher`, `AppVersion={#AppVersion}`, `LicenseFile=..\LICENSE` (`GPL-3.0-or-later`), `SetupIconFile=..\resources\ico\recto.ico`, `WizardImageFile` and `WizardSmallImageFile` (the next item), `ArchitecturesAllowed`, `ArchitecturesInstallIn64BitMode`, `ChangesAssociations=not IsPortableMode`, `Uninstallable=not IsPortableMode`, and `CreateUninstallRegKey=not IsPortableMode`. Done when: `ISCC.exe` validates all `[Setup]` directives with zero warnings. Cheaper substitute: hard-coding a single install mode that always writes `HKLM`/`HKCU` registry keys even when the user wants a portable USB/folder install.
- [ ] Set `WizardImageFile=..\resources\installer\wizard-image-100.png,..\resources\installer\wizard-image-125.png,..\resources\installer\wizard-image-150.png,..\resources\installer\wizard-image-175.png,..\resources\installer\wizard-image-200.png,..\resources\installer\wizard-image-225.png,..\resources\installer\wizard-image-250.png` and `WizardSmallImageFile=..\resources\installer\wizard-small-100.png,..\resources\installer\wizard-small-125.png,..\resources\installer\wizard-small-150.png,..\resources\installer\wizard-small-175.png,..\resources\installer\wizard-small-200.png,..\resources\installer\wizard-small-225.png,..\resources\installer\wizard-small-250.png` (SPEC 3.6: comma-separated scale lists of the images `design/tools/build_resources.py` generates, so Setup picks the one that matches the display scale), and never draw, resize or copy them by hand. Done when: `ISCC.exe` compiles with all fourteen files found (a missing or renamed file fails the compile) and `tests/scripts/test_installer_script.py` asserts both directives list the seven scales `100` to `250` in order and that each listed file exists.
- [ ] Implement the `[Code]` Pascal script `CreateInputOptionPage(wpWelcome, ...)` with two radio buttons (`Full Installation (recommended)` and `Self-Contained Portable Installation (no registry changes, stores settings in .\data\portable.mode)`) plus command-line `/PORTABLE=1` support for unattended runs. Done when: `IsPortableMode()` returns `True` when either the Portable radio button is selected or `/PORTABLE=1` is passed on the command line.
- [ ] Guard all `[Icons]` (Start Menu and optional Desktop shortcuts) and all `[Registry]` entries with `Check: not IsPortableMode`, and add a portable-only step (`Check: IsPortableMode`) creating `{app}\data` and writing `{app}\data\portable.mode`. Done when: inspecting `installer/recto.iss` confirms zero unguarded `[Registry]` or `[Icons]` rows.
- [ ] Register polite Windows 11 Default Apps capabilities under `[Registry]` (`Software\Recto\Capabilities\FileAssociations` `.pdf -> Recto.PDF` and `Software\RegisteredApplications` + `Software\Classes\.pdf\OpenWithProgids`) with `DefaultIcon` set to the installed `pdf-file.ico` (from `resources/ico/pdf-file.ico`, SPEC 3.2) so `.pdf` files show the file-type icon rather than the app icon, and Recto appears in `Open with` and `Settings -> Apps -> Default apps` (`SPEC 3.2` and `9.7`). Done when: `Recto.PDF` ProgId points `DefaultIcon` to `{app}\pdf-file.ico` and `shell\open\command` to `"{app}\Recto.exe" "%1"`.
- [ ] Include `[UninstallDelete]` rules (only for non-portable installs, leaving user documents untouched) and an optional prompt/task on uninstall to remove `%LOCALAPPDATA%\Recto` cache/settings while preserving user settings across in-place version upgrades. Done when: upgrading `v1.0.0 -> v1.0.1` preserves `%LOCALAPPDATA%\Recto\settings.json` and `recents.json`.
- [ ] Add static validation tests in `tests/scripts/test_installer_script.py` verifying that `installer/recto.iss` exists, references `..\LICENSE`, `..\resources\ico\recto.ico`, `..\resources\ico\pdf-file.ico` and the fourteen `..\resources\installer\wizard-*.png` files, guards every `[Registry]` and `[Icons]` line with `Check: not IsPortableMode`, and supports `/PORTABLE=1`. Done when: `python -m unittest tests.scripts.test_installer_script` passes.
- [ ] Commit: `release: author Inno Setup installer script (installer/recto.iss) for Full and Portable modes`

**Test checkpoint:** `python -m unittest tests.scripts.test_installer_script` passes and `C:\Program Files\Inno Setup 7\ISCC.exe` compiles `installer/recto.iss` for `x64` and `ARM64` with `0` warnings quoted, finding every `WizardImageFile` and `WizardSmallImageFile` scale.

## 5. Release packaging pipeline (`build-installer.ps1`: Setup `.exe`, Portable `.zip` & MSIX)

Build the single-command release packaging pipeline in `build/build-installer.ps1` (and wire `-Package` and `-EnableMsix` in `build/build.ps1`): stage the `Release | x64` and `Release | ARM64` outputs (`Recto.exe`, `recto-cli.exe`, `LICENSE`, `NOTICE.md`; a plain Win32 app with no framework runtime DLLs and no `.pri` resources), invoke `ISCC.exe` on `installer/recto.iss` to produce `build/dist/RectoSetup-x64.exe` and `build/dist/RectoSetup-ARM64.exe`, build standalone `build/dist/Recto-Portable-x64.zip` and `build/dist/Recto-Portable-ARM64.zip` with `data\portable.mode` pre-created inside the archive, optionally produce `Recto-x64.msix` / `Recto-ARM64.msix` when `-EnableMsix` is passed, and run `scripts/check-package-assets.py` to audit every referenced icon, manifest asset, and capability.

> **Groomed 2026-10-22:** v1 ships unsigned (owner decision 2026-10-22 (foundation plan section 1)): `build/build-installer.ps1` runs no `signtool` step on `Setup.exe`, `Recto.exe` or the Portable `.zip`, writes `SHA256SUMS.txt` beside the artifacts, and the release notes carry the SmartScreen notice (`D07 T02 §6`). MSIX is signed by Microsoft only through the optional Store submission.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `build/build-installer.ps1` and wire `build/build.ps1 -Package [-EnableMsix]` to stage self-contained `x64` and `ARM64` Release bundles under `build/stage/<arch>/`, invoke `C:\Program Files\Inno Setup 7\ISCC.exe`, create `Recto-Portable-<arch>.zip` with `data\portable.mode`, and write `build/dist/SHA256SUMS.txt`. Done when: `pwsh -NoProfile -File build/build-installer.ps1` produces `RectoSetup-x64.exe`, `RectoSetup-ARM64.exe`, `Recto-Portable-x64.zip`, `Recto-Portable-ARM64.zip`, and `SHA256SUMS.txt` under `build/dist/`.
2. Author `scripts/check-package-assets.py` (with unit tests in `tests/scripts/test_package_assets.py`) auditing `src/app/Package.appxmanifest`, `installer/recto.iss`, and `build/stage/<arch>/` against `resources/README.md` (verifying every `.png`/`.ico` exists at its exact pixel dimension and `Package.appxmanifest` declares only `runFullTrust` with zero network or broad filesystem capabilities). Done when: `python scripts/check-package-assets.py` exits `0`.

- [ ] Stage `Release | x64` and `Release | ARM64` bundles in `build/build-installer.ps1`: verify `Recto.exe`, `recto-cli.exe`, `LICENSE`, and `NOTICE.md` are present and that zero framework runtime DLLs (`Microsoft.*.dll`), zero `.pri` files and zero debug or test binaries (`recto-tests.exe`, `recto-fuzz.exe`, `gtest.dll`, `.pdb`, `.ilk`) are staged into the distribution folder. Done when: `build/build-installer.ps1` fails fast if any required runtime file is missing or any test binary is present. Cheaper substitute: zipping the raw `build/bin/x64/Release/` folder directly, leaking `recto-tests.exe`, `recto-fuzz.exe`, and intermediate build files into user releases.
- [ ] Invoke `C:\Program Files\Inno Setup 7\ISCC.exe` in `build/build-installer.ps1` for both `x64` and `ARM64` stage directories to produce `build/dist/RectoSetup-x64.exe` and `build/dist/RectoSetup-ARM64.exe`. Done when: both setup executables build with exit code `0`.
- [ ] Build `build/dist/Recto-Portable-x64.zip` and `build/dist/Recto-Portable-ARM64.zip` containing the staged files plus `data/portable.mode` (`0`-byte or UTF-8 marker file) so extracting the `.zip` anywhere runs immediately in Self-Contained Portable Mode (`D02 T04 §1`). Done when: inspecting `Recto-Portable-x64.zip` via Python `zipfile` confirms `Recto.exe`, `recto-cli.exe`, `LICENSE`, `NOTICE.md`, and `data/portable.mode` are present.
- [ ] Support `-EnableMsix` in `build/build-installer.ps1` and `build/build.ps1 -Package -EnableMsix` to build optional MSIX packages (`Recto-x64.msix`, `Recto-ARM64.msix`) using `src/app/Package.appxmanifest` (a full-trust Win32 desktop package, `runFullTrust`, with no framework `PackageDependency`) for owners who choose Microsoft Store submission (`D99 T01 §3`). Done when: `-EnableMsix` is wired cleanly without blocking standard Inno Setup + Portable `.zip` builds when Store signing certificates are absent.
- [ ] Write `scripts/check-package-assets.py` verifying that every logo/icon path in `src/app/Package.appxmanifest` (`Square44x44Logo`, `Square150x150Logo`, `Wide310x150Logo`, `SmallTile`, `LargeTile`, `SplashScreen`, `StoreLogo`, and the `.pdf` `uap:Logo` `Assets\FileTypes\PdfFile.png` resolved through its `PdfFile.targetsize-*.png` files) and `installer/recto.iss` (`resources/ico/recto.ico`, `resources/ico/pdf-file.ico` and the fourteen `resources/installer/wizard-*.png` images) resolves to a committed file in `resources/` with exact pixel dimensions matching `resources/README.md`, and that `Package.appxmanifest` declares only `<rescap:Capability Name="runFullTrust" />` (zero `internetClient` or unapproved capabilities) and no `PackageDependency` element. Done when: `python scripts/check-package-assets.py` exits `0`.
- [ ] Add `tests/scripts/test_package_assets.py` testing `scripts/check-package-assets.py` against the repository and verifying it catches a missing icon or an injected `internetClient` capability. Done when: `python -m unittest tests.scripts.test_package_assets` passes.
- [ ] Commit: `release: add build-installer.ps1 packaging pipeline and manifest/asset capability audit`

**Test checkpoint:** `pwsh -NoProfile -File build/build-installer.ps1` exits `0`, `python scripts/check-package-assets.py` exits `0`, `python -m unittest tests.scripts.test_package_assets` passes, and the SHA-256 hashes in `build/dist/SHA256SUMS.txt` are quoted.

## 6. Automated Full Install, `.pdf` association, upgrade and uninstall proof (`installer`)

Prove the **Full Installation** lifecycle end-to-end by writing and executing the first half of `tests/scripts/verify_installer.ps1` against the real `build/dist/RectoSetup-x64.exe` binary in an isolated user-mode install root:
1. Run silent per-user Full Install (`RectoSetup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="<temp-install-dir>"`).
2. Verify installed binaries (`Recto.exe`, `recto-cli.exe`, `unins000.exe`, `LICENSE`, `NOTICE.md`), confirm `data\portable.mode` does **not** exist in Full Install mode, and verify `HKCU\Software\Classes\Recto.PDF` (`DefaultIcon`, `shell\open\command`), `HKCU\Software\Classes\.pdf\OpenWithProgids` (`Recto.PDF`), and `HKCU\Software\RegisteredApplications` registry keys.
3. Launch the installed `Recto.exe` (and `recto-cli.exe info`) on a corpus PDF (`tests/corpus/render/hello-world.pdf`) with an isolated `LOCALAPPDATA` environment variable, modify a setting and open a file so `%LOCALAPPDATA%\Recto\settings.json` and `recents.json` are populated.
4. Build and run an in-place upgrade installer (`v1.0.1` over `v1.0.0` in the same `/DIR`), relaunch, and verify `settings.json` and `recents.json` survive intact and the upgraded binary reports `1.0.1`.
5. Run silent uninstall (`unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`) and verify that `<temp-install-dir>` binaries, shortcuts, and `HKCU\Software\Classes\Recto.PDF` registry keys are completely removed while user PDF documents remain untouched.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` sections 3.2 (`.pdf` file-type icon and `OpenWithProgids`) and 9.7 (`Default PDF app` setting row), with `resources/preview.png` as the visual reference.
**Job:** A desktop Windows user can install Recto via `RectoSetup-x64.exe`, open `.pdf` files from Explorer with Recto's file-type icon, upgrade to a newer release without losing preferences or recent files, and uninstall cleanly with zero orphaned program files or registry associations. Consumer: Windows Explorer, `HKCU\Software\Classes`, and the end user.
**Treatment:** Automated execution of the compiled `RectoSetup-x64.exe` (`v1.0.0` and `v1.0.1`) and `unins000.exe` in `tests/scripts/verify_installer.ps1` with programmatic registry, file-system, and process assertions. Cheaper substitute that fails the checkpoint: running `Recto.exe` only from `build/bin/x64/Release/` and never executing `RectoSetup-x64.exe` or `unins000.exe`.
**Chrome:** Consume the generated icons unchanged: `resources/ico/recto.ico` (app), `resources/ico/pdf-file.ico` (the `.pdf` `DefaultIcon`) and `resources/msix/Assets/FileTypes/PdfFile.targetsize-*.png` (the optional MSIX file-type logo).

**Build order:**

1. Implement the `Test-FullInstallLifecycle` stage in `tests/scripts/verify_installer.ps1` covering silent `/CURRENTUSER` install (`v1.0.0`), registry ProgId/icon assertions, installed `Recto.exe` and `recto-cli.exe` execution against `hello-world.pdf`, `v1.0.0 -> v1.0.1` in-place upgrade with `settings.json`/`recents.json` survival checks, and silent `unins000.exe` cleanup verification. Done when: `pwsh -NoProfile -File tests/scripts/verify_installer.ps1 -Mode Full` exits `0` with all assertions passing.
2. Ensure `tests/scripts/verify_installer.ps1` backs up and restores any pre-existing `HKCU\Software\Classes\Recto.PDF` test keys in a `try / finally` block so the developer workstation is left completely clean even if a test assertion fails mid-run. Done when: a simulated failure inside `try` still executes `finally` and cleans up all test registry keys and temp folders.

- [ ] Execute `RectoSetup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR="<temp>\RectoFull"` in `tests/scripts/verify_installer.ps1` and assert `LASTEXITCODE -eq 0`, `Recto.exe`, `recto-cli.exe`, `unins000.exe`, `LICENSE`, and `NOTICE.md` exist, and `<temp>\RectoFull\data\portable.mode` does **not** exist. Done when: the Full Install file check passes in `verify_installer.ps1`. Cheaper substitute that fails the checkpoint: checking that `RectoSetup-x64.exe` exists on disk without actually running the installer and uninstaller.
- [ ] Verify registry file-association registration under `HKCU\Software\Classes\Recto.PDF\DefaultIcon` (pointing at `<temp>\RectoFull\pdf-file.ico`), `HKCU\Software\Classes\Recto.PDF\shell\open\command` (containing `"<temp>\RectoFull\Recto.exe" "%1"`), and `HKCU\Software\Classes\.pdf\OpenWithProgids` (`Recto.PDF`). Done when: `verify_installer.ps1` reads back each registry value and asserts exact match.
- [ ] Launch the installed `<temp>\RectoFull\recto-cli.exe info tests/corpus/render/hello-world.pdf --json` and `<temp>\RectoFull\Recto.exe --perf-cold-start` with `env:LOCALAPPDATA` redirected to `<temp>\LocalAppData`, write custom preferences (`"theme": "dark"`) and a recent entry into `<temp>\LocalAppData\Recto\settings.json` and `recents.json`, and verify the installed app reads and preserves them. Done when: both `recto-cli.exe` and `Recto.exe` exit `0` from the installed folder.
- [ ] Build a `1.0.1` test installer (`ISCC.exe /DAppVersion=1.0.1`), run it silently over the existing `<temp>\RectoFull` directory, and verify that `<temp>\LocalAppData\Recto\settings.json` (`"theme": "dark"`) and `recents.json` are intact while the installed executable version updates to `1.0.1`. Done when: the `v1.0.0 -> v1.0.1` upgrade continuity assertion passes.
- [ ] Run `<temp>\RectoFull\unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`, wait for exit code `0`, and assert that `Recto.exe` is removed, `HKCU\Software\Classes\Recto.PDF` is deleted, `HKCU\Software\Classes\.pdf\OpenWithProgids` no longer lists `Recto.PDF`, and the opened PDF (`hello-world.pdf`) is completely untouched. Done when: all post-uninstall cleanup assertions pass.
- [ ] Wrap the entire Full Install verification in a `try / finally` block that guarantees removal of `<temp>` and any `HKCU\Software\Classes\Recto.PDF` keys if interrupted. Done when: `verify_installer.ps1 -Mode Full` leaves zero residual keys or directories after completion.
- [ ] Commit: `test(release): add automated Full Install, file-association, upgrade, and uninstall proof`

**Test checkpoint:** `pwsh -NoProfile -File tests/scripts/verify_installer.ps1 -Mode Full` exits `0` with every step's `[PASS]` log line quoted.

## 7. Automated Self-Contained Portable Install zero-registry and zero-`%LOCALAPPDATA%` proof

Complete `tests/scripts/verify_installer.ps1` by proving both **Self-Contained Portable** distribution paths end-to-end:
1. Run the Inno Setup installer in silent Portable mode (`RectoSetup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /PORTABLE=1 /DIR="<temp>\RectoPortableIss"`).
2. Extract `build/dist/Recto-Portable-x64.zip` into `<temp>\RectoPortableZip`.
3. For both portable folders, verify that `<dir>\data\portable.mode` exists, `unins000.exe` does **not** exist, launching `Recto.exe` creates `<dir>\data\settings.json` and `<dir>\data\recents.json` inside the portable directory, **zero** files or directories are created under the redirected `%LOCALAPPDATA%\Recto`, and **zero** registry keys are created under `HKCU\Software\Classes\Recto.PDF` or `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall`.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (`Portable mode` setting row in `D-Settings.png`) and foundation plan decision D6.
**Job:** A user can install Recto in Portable mode via `RectoSetup-x64.exe /PORTABLE=1` or by unzipping `Recto-Portable-x64.zip` onto a USB drive or local folder, use the full app with all settings and recent files stored inside `<exe-dir>\data\`, and delete the folder to remove 100% of Recto with zero registry or `%LOCALAPPDATA%` trace on the host PC. Consumer: privacy-conscious users, locked-down workstations without admin rights, and portable USB deployments.
**Treatment:** End-to-end execution of both `RectoSetup-x64.exe /PORTABLE=1` and `Recto-Portable-x64.zip` in `tests/scripts/verify_installer.ps1`, verifying `<portable-dir>\data\` state persistence and asserting zero registry and zero `%LOCALAPPDATA%\Recto` mutations. Cheaper substitute that fails the checkpoint: unit-testing `AppPaths` in isolation without testing the compiled `RectoSetup-x64.exe /PORTABLE=1` installer and `Recto-Portable-x64.zip` bundle.
**Chrome:** No extra UI; portable mode uses the standard `SPEC 9.7` `Portable mode` toggle (showing `Active` when `data\portable.mode` is present).

**Build order:**

1. Implement the `Test-PortableInstallLifecycle` stage in `tests/scripts/verify_installer.ps1` testing both `RectoSetup-x64.exe /PORTABLE=1` and `Recto-Portable-x64.zip` extraction, launching `Recto.exe` with an isolated `env:LOCALAPPDATA`, verifying `<portable-dir>\data\settings.json` creation, and asserting zero `%LOCALAPPDATA%\Recto` files and zero `HKCU` registry keys. Done when: `pwsh -NoProfile -File tests/scripts/verify_installer.ps1` (running both `Full` and `Portable` stages by default) exits `0`.
2. Verify that deleting `<temp>\RectoPortableIss` and `<temp>\RectoPortableZip` removes 100% of files with zero locked handles or background processes lingering after `Recto.exe` exits. Done when: `Remove-Item -Recurse -Force` succeeds immediately after `Recto.exe` exits with zero leftover files or registry entries.

- [ ] Run `RectoSetup-x64.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /PORTABLE=1 /DIR="<temp>\RectoPortableIss"` in `verify_installer.ps1` and assert `LASTEXITCODE -eq 0`, `<temp>\RectoPortableIss\Recto.exe` and `<temp>\RectoPortableIss\data\portable.mode` exist, and `unins000.exe` does **not** exist. Done when: the `/PORTABLE=1` installer layout check passes. Cheaper substitute that fails the checkpoint: creating an uninstaller registry key during a portable install.
- [ ] Assert after `RectoSetup-x64.exe /PORTABLE=1` that `HKCU\Software\Classes\Recto.PDF` does **not** exist, `HKCU\Software\Classes\.pdf\OpenWithProgids` does **not** contain `Recto.PDF`, and no `Recto` entry was added under `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall`. Done when: all 3 zero-registry assertions pass.
- [ ] Extract `build/dist/Recto-Portable-x64.zip` to `<temp>\RectoPortableZip` and assert that `Recto.exe`, `recto-cli.exe`, `LICENSE`, `NOTICE.md`, and `data\portable.mode` exist out of the box. Done when: the `.zip` structure assertion passes.
- [ ] Launch `Recto.exe` from both `<temp>\RectoPortableIss` and `<temp>\RectoPortableZip` with `env:LOCALAPPDATA="<temp>\EmptyLocalAppData"`, trigger settings/recents flush, and assert that `<portable-dir>\data\settings.json` exists while `<temp>\EmptyLocalAppData\Recto` does **not** exist. Done when: both portable launches write exclusively inside `<portable-dir>\data\` and leave `<temp>\EmptyLocalAppData` empty.
- [ ] Verify that `Remove-Item -Recurse -Force` on both portable directories immediately after `Recto.exe` exits succeeds on the first attempt (proving all `DocumentActor`, `RenderPool`, and font/file handles closed cleanly on exit). Done when: directory removal succeeds with zero `Access is denied` or locked-file errors.
- [ ] Commit: `test(release): add automated Self-Contained Portable Install and .zip isolation proof`

**Test checkpoint:** `pwsh -NoProfile -File tests/scripts/verify_installer.ps1` (running both `Full` and `Portable` verification suites) exits `0` with its complete `[PASS]` summary quoted.

## 8. Explorer thumbnail and preview handlers (`IThumbnailProvider`, `IPreviewHandler`)

Users of comparable PDF apps ask for PDF thumbnails in Explorer and a preview pane in Explorer and Outlook, and complain when every PDF shows the app icon instead of a document icon. Recto ships a COM server `RectoShell.dll` with a thumbnail provider and a preview handler that render page 1 through `recto-pdf`. Explorer must never load the parser into its own process: both handlers run process-isolated in the system surrogate, with a hard time and memory budget, no JavaScript, no links and no network, and they fail to the stock document icon on any error.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add `src/shell/RectoShell.vcxproj` building `RectoShell.dll` (the COM handlers, which never link `recto-pdf`) and `recto-shellrender.exe` (the job-limited worker that renders page 1 with `recto-pdf`). Done when: `Shell.ThumbnailProvider` renders the corpus `hello-world.pdf` to a non-blank 256 px thumbnail through the worker.
2. Register both handlers in `installer/recto.iss` (Full mode only) under the `Recto.PDF` ProgID with process isolation left on, and remove them on uninstall. Done when: `tests/scripts/verify_installer.ps1` sees the thumbnail in `IShellItemImageFactory` output and no registration after uninstall.

- [ ] Author `src/shell/ThumbnailProvider.h/.cpp` implementing `IInitializeWithStream` and `IThumbnailProvider::GetThumbnail` that refuses streams over 64 MiB (reported through `IStream::Stat` or found while copying) and hands the bytes to a `recto-shellrender.exe` worker, which opens them with `recto-pdf`, renders page 1 at the requested size and returns pixels through an anonymous pipe as `WTSAT_ARGB`. Done when: the corpus thumbnails match the viewer's page 1 within the golden tolerance and a 65 MiB fixture returns `E_FAIL` without starting the worker.
- [ ] Author `src/shell/PreviewHandler.h/.cpp` implementing `IPreviewHandler`, `IInitializeWithStream` and `IObjectWithSite`, showing pages rendered by the same job-limited `recto-shellrender.exe` worker, with next and previous page buttons and nothing interactive in the document. Done when: Outlook's attachment preview and Explorer's preview pane show page 1.
- [ ] Enforce the render limits at a process boundary, not by polling: the handler starts `recto-shellrender.exe` inside a Job Object with `JOB_OBJECT_LIMIT_JOB_MEMORY` 256 MiB (an aggregate limit over every process in the job, so a child cannot escape it), `JOB_OBJECT_LIMIT_ACTIVE_PROCESS` 1 and `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, waits at most 2 s, and terminates only that job on timeout so the shared preview surrogate and other handlers are never killed; it returns `E_FAIL` so Explorer falls back to the stock icon on timeout, error or encrypted input. Done when: every `tests/corpus/broken/` file returns within 3 s, a fixture that commits 300 MiB is killed by the job limit, and the surrogate process survives.
- [ ] Bound the stream copy in the surrogate without creating threads that can outlive the call: read synchronously on the calling thread in 1 MiB chunks into a buffer capped at 64 MiB, checking a 1 s total deadline between chunks and stopping at the cap. A single `IStream::Read` has no timeout parameter, so a provider that blocks inside one read can only be bounded by the caller; record that limit in `docs/security-audit.md` and keep the per-call state (buffer, stream reference) released on every return path. Done when: 200 consecutive timed-out or capped calls leave the surrogate's thread, handle and private-bytes counts within 5% of their starting values.
- [ ] Register with process isolation (no `DisableProcessIsolation` value) so the handlers run in the system surrogate, never inside `explorer.exe`, and parsing happens only in `recto-shellrender.exe`. Done when: a test lists `explorer.exe` and surrogate modules during preview and finds no `recto-pdf` code in either.
- [ ] Register the handlers and a dedicated PDF document icon (separate from the app icon) in `installer/recto.iss` for Full installs only, and skip them in Portable mode. Done when: `verify_installer.ps1` finds the registration in Full mode and none in Portable mode.
- [ ] Add `Shell.ThumbnailProvider` and `Shell.PreviewHandlerHostile` tests in `tests/recto-tests/` driving the COM objects in-process for the test only. Done when: both pass under ASan.
- [ ] Commit: `shell: add process-isolated Explorer thumbnail and preview handlers for PDFs`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.ThumbnailProvider:Shell.PreviewHandlerHostile` passes under ASan, and `pwsh tests/scripts/verify_installer.ps1` reports the handlers registered after install and gone after uninstall.

## 9. Explorer verbs: print, printto, Combine and Convert to PDF (`IExplorerCommand`)

Users of comparable PDF apps ask repeatedly for "Combine" and "Convert to PDF" on a multi-file Explorer selection, for Outlook's Quick Print to print silently instead of opening the viewer, and object when the app makes itself the default PDF handler on first run. Recto registers `print` and `printto` verbs that print without a window, and `IExplorerCommand` classic shell verbs (Windows 10 menu and Windows 11 "Show more options") that hand the whole selection to one running instance in selection order. It never changes the user's default PDF handler.

> **Groomed 2026-10-22:** the Windows 11 top-level (modern) context menu entry needed a signed sparse package, and v1 ships unsigned (owner decision 2026-10-22 (foundation plan section 1)), so v1 registers only the classic shell verbs (`HKCU\Software\Classes\...\shell`) from the Full installer; Portable mode registers nothing. Adding Trusted Signing or a certificate later restores the sparse package (cost: medium, one signing step plus the manifest).

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface: the Windows Explorer context menu, not a SPEC screen. Labels follow `design/SPEC.md` section 2 voice ("Combine in Recto", "Convert to PDF with Recto"). Design board: `design/SPEC.md` section 9.16 with `design/screenshots/D-Print.png` (drawn by `D06 T04 §21`).
**Job:** A user selects several files in Explorer and combines or converts them in one Recto window, or prints PDFs from Explorer or Outlook without opening Recto. Consumer: `D05 T02 §3` Combine mode, `D04 T01 §3` Convert page, and `D07 T01 §2` print pipeline.
**Treatment:** `IExplorerCommand` entries (registered through `ExplorerCommandHandler` under the classic `shell` verb keys) with a `Recto` submenu, shown in the Windows 10 menu and in Windows 11 "Show more options". Cheaper substitute that fails the checkpoint: one process launched per selected file, or a print verb that opens the viewer.
**Chrome:** `resources/ico/recto.ico` for menu icons; no app chrome is shown for `print` and `printto`.

**Build order:**

1. Add `print` (`Recto.exe /p "%1"`) and `printto` (`Recto.exe /pt "%1" "%2" "%3" "%4"`) verbs to `installer/recto.iss`, executed by `LaunchArgs` (`D02 T01 §7`) through the `D07 T01 §2` print worker without creating a window. Done when: `ShellExecuteW(..., L"print", ...)` on a corpus PDF prints to Microsoft Print to PDF and no top-level window is created.
2. Add `src/shell/ExplorerCommands.h/.cpp` in `RectoShell.dll` implementing `IExplorerCommand` for "Combine in Recto" (two or more PDF or image files) and "Convert to PDF with Recto" (Office, HTML, Markdown and image files), registered as classic shell verbs. Done when: invoking the command on five files opens one Recto window with five staged files in Explorer selection order.

- [ ] Parse `/p <file>` and `/pt <file> <printer> <driver> <port>` in `LaunchArgs` into a print-only request and run it headless through the print worker, exiting with 0 on success and a non-zero code with a logged reason on failure. Done when: `Shell.PrintVerbHeadless` asserts no window and the expected exit code.
- [ ] When the document needs a password, show the password dialog and then print; when its permissions forbid printing, refuse with the reason and exit code 3 without opening any print dialog, matching §2's refusal. Done when: `Shell.PrintVerbHeadless` sees the password dialog for the encrypted fixture and exit code 3 with no dialog for the print-restricted fixture.
- [ ] Implement `ExplorerCommands` returning `ECS_HIDDEN` for selections the command cannot handle and passing the full `IShellItemArray` to the running instance through the single-instance channel. Done when: a mixed selection shows only the applicable commands.
- [ ] Preserve Explorer selection order and stage the files in Organize Combine or the Convert page without starting the job until the user confirms. Done when: the staged list matches the selection order in `Shell.ExplorerCombineOrder`.
- [ ] Register `ExplorerCommands` as classic shell verbs under `HKCU\Software\Classes\SystemFileAssociations\.pdf\shell` (and the Office, HTML, Markdown and image extensions for Convert) from `installer/recto.iss`, each with `ExplorerCommandHandler` pointing at the `IExplorerCommand` class ID that `RectoShell.dll` registers under `HKCU\Software\Classes\CLSID`; the Windows 11 top-level menu entry (a signed sparse package) is not part of v1. Done when: on Windows 11 the entries appear under "Show more options" with the five-file selection order intact, and the install needs no certificate.
- [ ] Make Portable mode (`/PORTABLE=1` and the `.zip`) register no verbs and no COM class, and make uninstall remove every key written above. Done when: `verify_installer.ps1` finds no `HKCU\Software\Classes` key added by a Portable run and none left after a Full uninstall.
- [ ] Register the `print` and `printto` classic verbs and keep a failed `IExplorerCommand` registration non-fatal (logged), so the plain verbs still work. Done when: the `print` verb prints to Microsoft Print to PDF after a simulated `RectoShell.dll` registration failure and none remain after uninstall.
- [ ] Assert install and first run never change the user's default PDF handler; only `Settings -> Default PDF app -> Make Recto your default PDF viewer` opens the Windows Default Apps page. Done when: `verify_installer.ps1` reads `UserChoice` before and after install and finds it unchanged.
- [ ] Route the `print` and `printto` verbs through `PrintService::print_headless` with the `printto` printer argument as `PrintJobOptions::printer_name`. Done when: `Shell.PrintVerbHeadless` prints to the named queue given by `/pt` and `Print.NamedPrinterAndTicket` passes.
- [ ] Commit: `shell: add print verbs and Explorer Combine and Convert commands for multi-file selections`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.PrintVerbHeadless:Shell.ExplorerCombineOrder` passes on the display-session host, and `pwsh tests/scripts/verify_installer.ps1` shows the verbs registered, the `UserChoice` default unchanged, and the verbs removed after uninstall.

## 10. Print layouts: booklet, multiple pages per sheet and poster tiling

Acrobat Pro's print dialog offers Booklet, Multiple pages per sheet and Poster as page sizing modes; Recto's print pipeline (§2) prints one page per sheet only (`docs/acrobat-parity.md`, foundation plan D18). Comparable apps produce wrong output in exactly these modes: three pages per sheet put four on the first sheet, poster scale overflows, and the booklet back side prints upside down. This section adds the three layouts as imposition computed on the print worker from the same `DisplayList`s, never a rasterized shortcut, and keeps every guarantee of §2 (own document instance, printer-DPI rendering, cancellation, driver isolation).

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new options inside the print dialog of §2, following its chrome; `design/SPEC.md` section 9.9 dialog rules apply. Design board: `design/SPEC.md` section 9.16 with `design/screenshots/D-Print.png` (drawn by `D06 T04 §21`).
**Job:** A user prints a booklet for saddle stitching, several pages per sheet, or a large page tiled across sheets, and gets exactly the sheets the preview showed. Consumer: the print worker of §2 and the printer.
**Treatment:** A `Page sizing` choice (`Fit`, `Actual size`, `Booklet`, `Multiple pages per sheet`, `Poster`) with each mode's options and a live sheet preview. Cheaper substitute that fails the checkpoint: printing a pre-imposed temporary PDF rasterized at screen resolution.

**Chrome:** The §2 print dialog, `ui::ComboBox`, `ui::NumberBox` and `ui::ToggleSwitch` (`D10 T02 §9`, `§6`, `§7`) in a `ui::Dialog` (`D10 T02 §17`), `Recto*Brush` tokens (`D10 T01 §8`).

**Build order:**

1. Implement `src/app/printing/Imposition.h/.cpp` computing sheet plans: booklet signatures (pages padded to a multiple of four, both-sides or front-only and back-only passes, left or right binding, sheets from-to), N-up grids (2, 4, 6, 9, 16 and custom rows by columns, page order, border, auto-rotate), and poster tiles (tile scale, overlap, cut marks, labels). Done when: `Print.ImpositionPlans` matches the expected sheet maps for 1 to 17 pages in every mode.
2. Render each sheet on the print worker by placing page `DisplayList`s with a transform per cell or tile at printer DPI, and add the options and a sheet preview to the §2 dialog. Done when: `Print.ImpositionOutput` prints every mode to Microsoft Print to PDF and the output pages match the plans.

- [ ] Author `Imposition::booklet(page_count, binding, sides)` returning signature sheets with front and back cell assignments, padding blank pages at the end. Done when: 17 pages yield 20 page slots on five sheets in the standard saddle-stitch order.
- [ ] Author `Imposition::n_up(page_count, rows, cols, order, auto_rotate)` so exactly N pages land on each sheet and the last sheet holds the remainder. Done when: 7 pages at 3 per sheet yield sheets of 3, 3 and 1.
- [ ] Author `Imposition::poster(page_size, tile_scale, overlap, paper)` with tile count bounded at 400 and a typed refusal past it. Done when: an A0 page on A4 at 100 percent yields 16 tiles plus overlap, and a 1e6 percent scale returns `PosterTooLarge` without allocating.
- [ ] Render booklet back sides rotated for the printer's duplex edge (long or short, from the driver) and offer a manual two-pass mode with a "flip the stack" prompt between passes. Done when: a captured duplex job's back sides are upright when folded.
- [ ] Draw optional page borders, cut marks and tile labels as vector content in the sheet, never on the source pages. Done when: the printed PDF shows the marks only when enabled. Cheaper substitute: marks burned into a copy of the document.
- [ ] Add the `Page sizing` modes and their options to the §2 print dialog with a sheet preview that renders thumbnails from the same plans, and keep `Current page` and custom ranges working with every mode. Done when: `Print.ImpositionDialog` shows the preview sheet count equal to the plan's.
- [ ] Persist the last layout per printer with the other print settings (§2). Done when: reopening the dialog restores the mode and its options.
- [ ] Add `Print.ImpositionPlans`, `Print.ImpositionOutput` and `Print.ImpositionDialog` in `tests/recto-tests/`. Done when: the three suites pass on the display-session host.
- [ ] Commit: `feat(print): add booklet, multiple pages per sheet and poster print layouts`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Print.ImpositionPlans:Print.ImpositionOutput:Print.ImpositionDialog` passes, and 7 pages at 3 per sheet print exactly three sheets of 3, 3 and 1 pages to Microsoft Print to PDF.

## 11. Print page selection: reverse order, pages with comments, a bookmark section, an area

Foxit PDF Editor, PDFelement and UPDF let a user print pages in reverse order, print only the pages that carry comments, print one bookmarked section and print a selected area of a page; Acrobat Pro also reverses pages and prints the pages a bookmark covers (`docs/acrobat-parity.md`, foundation plan D18). Recto's pipeline (§2) prints an explicit page range only. This section adds the page selection engine as pure, testable resolution on the print worker's own `recto::pdf::Document` instance, so the dialog (§14) only binds to it. It keeps every §2 guarantee: no `recto::pdf` call on the UI thread, nothing written to the open document, and a clear refusal instead of printing zero pages or silently printing all pages.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/app/printing/PrintSelection.h/.cpp` with `PrintSelection` (`range_spec`, `reverse`, `comments_only`, `bookmark_node`, `area`) and `resolve_print_pages(selection, DocumentSnapshot)` returning `PdfResult<ResolvedPrintPages>` of `PageRef` values (`page_index`, optional clip rectangle). Done when: `Print.SelectionResolve` passes the table of selection cases below.
2. Feed `ResolvedPrintPages` into the §2 spool loop and the §10 imposition input in place of the raw range. Done when: `Print.SelectionFeedsSpool` prints the resolved pages in resolved order to the print package target of §3.

- [ ] Implement `resolve_print_pages` so the order of operations is fixed: parse `range_spec` with `D05 T01 §1`'s `parse_page_ranges`, intersect with the comments filter and the bookmark section, then reverse. Done when: `Print.SelectionResolve` resolves range `2-5` with `comments_only` on a document with comments on pages 2, 4 and 6 to `[2, 4]`, and to `[4, 2]` with `reverse`.
- [ ] Implement the comments filter in `src/app/printing/PrintSelection.cpp`: a page matches when its `/Annots` holds at least one annotation whose subtype is a markup type (Text, FreeText, Line, Square, Circle, Polygon, PolyLine, Highlight, Underline, Squiggly, StrikeOut, Stamp, Caret, Ink, FileAttachment, Redact, Popup excluded) with the Print flag set and the Hidden flag clear (NoView only suppresses on-screen display and does not stop printing). Links and widgets never count. Done when: `Print.SelectionCommentPages` resolves `[2, 5]` for a six page fixture with printable comments on pages 2 and 5, ignores a Hidden comment on page 3 and a Link on page 4, counts a comment with Print set, NoView set and Hidden clear on page 6 (so a fixture with that comment added resolves `[2, 5, 6]`), and returns `NoPagesMatch` for a fixture with no comments.
- [ ] Implement the bookmark section: from the chosen `/Outlines` node, resolve its destination (explicit and named destinations) to a start page, and end the section on the page before the next bookmark in document order at the same or a shallower level, never before the start page; children are included. Done when: `Print.SelectionBookmarkSection` resolves an outline `A (pages 1-3)`, `B (4-6)` with child `B1 (4-5)` to `A = [1, 2, 3]`, `B = [4, 5, 6]` and `B1 = [4, 5]`, and refuses a node whose destination does not resolve with `BookmarkDestinationUnresolved`.
- [ ] Implement the area selection: a rectangle in the page's default user space on one page, mapped through `/Rotate` and the `CropBox` so the rectangle drawn on a rotated page selects the same content, clipped at replay time on the print worker and scaled by the §2 scale mode (`Fit to printable area` or `Actual size`). Done when: `Print.SelectionArea` prints a `100 x 100` pt area of a `/Rotate 90` page and the output equals the same area of the unrotated fixture within `SSIM >= 0.98`. Cheaper substitute: cropping a screen raster of the area, which loses vector quality.
- [ ] State in the code and in the dialog tooltip text that an area print only clips the printed output and hides nothing in the file; redaction stays in `D05 T01 §10`. Done when: `Print.SelectionArea` asserts the source document bytes are unchanged and the tooltip string key `Print_Area_NotRedaction` exists in the `ui::Strings` tables (`STRINGTABLE` resources, `D10 T03 §7`).
- [ ] Refuse combinations that have no meaning with a reason string, never a silent change: `reverse` with the §10 booklet mode, and `area` with any imposition mode other than `Fit` or `Actual size`. Done when: `Print.SelectionCombinationRefusals` returns `ReverseNotValidForBooklet` and `AreaNotValidForImposition` with their reason keys.
- [ ] Compute the selection on the print worker only, from the worker's own document instance, and cancel it with the job's `CancelToken`. Done when: `Print.SelectionResolveOffUiThread` asserts the thread id differs from the UI thread and that cancelling a 2,000 page resolve returns within `20 ms`.
- [ ] Commit: `feat(print): add print page selection by reverse order, comments, bookmark section and area`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Print.SelectionResolve:Print.SelectionCommentPages:Print.SelectionBookmarkSection:Print.SelectionArea:Print.SelectionCombinationRefusals:Print.SelectionResolveOffUiThread:Print.SelectionFeedsSpool` passes under ASan.

## 12. Print render modes: print as image, grayscale and text as outlines

PDFelement and UPDF print a page as an image so a driver cannot reinterpret fonts or transparency, Foxit PDF Editor prints text as outlines, and all three print in grayscale; Acrobat Pro has Print As Image and Print in Grayscale (`docs/acrobat-parity.md`, foundation plan D18). Recto's pipeline (§2) always replays the `DisplayList` as vector content with the document's colours. This section adds the three render modes on the print worker. They keep the §2 guarantees (own document instance, printer DPI, `CancelToken`, driver isolation) and never modify the document; the choice affects only what is sent to the spooler.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `PrintRenderOptions` (`image_mode`, `image_dpi`, `grayscale`, `text_as_outlines`) to `PrintJobOptions` in `src/app/printing/PrintService.h` and apply them in the replay stage of `src/app/printing/PrintRenderer.cpp`. Done when: `Print.RenderModesOptionPlumbing` shows each option reaches the replay stage.
2. Implement the three modes below behind the options. Done when: `Print.AsImage*`, `Print.Grayscale*` and `Print.TextAsOutlines*` pass.

- [ ] Implement print as image: rasterize each sheet with the tile rasterizer of `D01 T02 §6` at `150`, `300` or `600` DPI (default: the printer's reported DPI capped at `600`) in horizontal bands of at most `4096` rows, send each band into the print stream, and read bitmaps through their reported stride, never `width * 4`. Done when: `Print.AsImageBandedMemory` prints an A0 page at `600` DPI with peak private bytes under `300 MB` and `Print.AsImageStride` passes with a bitmap whose stride is wider than `width * 4`. Cheaper substitute: one full-page bitmap, which fails on large pages.
- [ ] Implement grayscale by replaying the sheet through a Direct2D colour matrix effect with Rec. 709 luminance weights (`0.2126`, `0.7152`, `0.0722`) so vector paths, shadings, images and annotation appearances all convert, and request monochrome in the PrintTicket only when the driver's `PrintCapabilities` advertise it and the user chose grayscale. Done when: `Print.GrayscaleConvertsAllContent` prints a fixture with a red fill, a gradient and an RGB image and every output pixel has equal R, G and B within `1`, and `Print.GrayscaleTicketOnlyWhenChosen` finds no colour feature in an untouched job's ticket.
- [ ] Implement text as outlines by replaying glyph runs as filled and stroked paths from `IDWriteFontFace::GetGlyphRunOutline`, honouring text render modes (fill, stroke, clip) and skipping invisible text (mode 3), so the spool carries no font resources. Done when: `Print.TextAsOutlinesNoFonts` prints `two-column-article.pdf` to the print package target of §3 and finds no font parts in the output while page `SSIM >= 0.97` against the normal print. Cheaper substitute: rasterizing the text, which is the image mode.
- [ ] Define the interactions: image mode overrides text as outlines (the option is disabled with a reason), grayscale composes with both, and every mode applies after the §10 imposition so a booklet or N-up sheet prints in the chosen mode. Done when: `Print.RenderModesCompose` prints a two-up sheet in image mode and in grayscale mode and matches the expected pixels.
- [ ] Check the `CancelToken` between bands and between glyph runs and delete any partial spool. Done when: `Print.RenderModesCancel` cancels mid-band and mid-outline jobs within `20 ms` and finds no leftover temporary files.
- [ ] Commit: `feat(print): add print as image, grayscale and text as outlines render modes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Print.RenderModesOptionPlumbing:Print.AsImageBandedMemory:Print.AsImageStride:Print.GrayscaleConvertsAllContent:Print.GrayscaleTicketOnlyWhenChosen:Print.TextAsOutlinesNoFonts:Print.RenderModesCompose:Print.RenderModesCancel` passes under ASan.

## 13. Printer's marks: crop marks, bleed marks, registration marks and page information

PDFelement and Foxit PDF Editor print crop and bleed marks for pages bound for a commercial printer, and Acrobat Pro offers marks and bleeds in its output settings (`docs/acrobat-parity.md`, foundation plan D18). Recto prints only the page box today (§2). This section adds marks as vector content in the slug area around the page, using the page boundaries of ISO 32000-1 section 14.11.2 (`MediaBox`, `CropBox`, `BleedBox`, `TrimBox`), drawn on the sheet and never added to the document. It reuses the sheet layer and cut-mark drawing of §10 so the two features cannot disagree.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/app/printing/PrinterMarks.h/.cpp` computing mark geometry from the trim box (fallback `CropBox`, then `MediaBox`), the bleed box (fallback trim plus a user bleed), and the options. Done when: `Print.MarksGeometry` matches the expected line coordinates.
2. Draw the marks in the §10 sheet layer and check paper fit before spooling. Done when: `Print.MarksOutput` prints a fixture with marks to the print package target of §3 and finds them only outside the bleed box.

- [ ] Implement `PrinterMarks::compute(trim, bleed, options)` returning crop marks at each trim corner (offset `6 pt` from the bleed edge by default, length `18 pt`), bleed marks at each bleed corner, registration targets at the middle of each side, and a page information line (file name, page number, date) drawn with a fixed font in the top slug, all with a default weight of `0.25 pt` and black ink. Done when: `Print.MarksGeometry` matches the expected coordinates for a `200 x 300` pt trim box at `(50, 50)` with a `9 pt` bleed.
- [ ] Compute the slug as bleed plus offset plus mark length and refuse with the minimum paper size (`PaperTooSmallForMarks`) when the selected paper cannot hold it, never scaling the page silently; marks force `Actual size` and disable `Fit to printable area` with a reason. Done when: `Print.MarksPaperTooSmall` returns the required width and height for A4 paper and a Letter page with a `0.25 in` bleed.
- [ ] Take the bleed from the `BleedBox` when present, from a user value in points, inches or millimetres otherwise, and clamp it inside the `MediaBox`, never beyond the page content. Done when: `Print.MarksBleedSources` covers `BleedBox` present, user value and clamping.
- [ ] Draw the marks as vector content in the sheet on the print worker, rotated with the page and the §10 duplex edge, and never burn them into a copy of the document. Done when: `Print.MarksOutput` finds mark paths in the output, none inside the bleed box, and the source document bytes unchanged. Cheaper substitute: stamping the marks into a temporary PDF and printing that file.
- [ ] Combine marks with the §10 booklet, N-up and poster modes: marks apply per sheet in N-up (around the grid) and are refused with a reason for booklet. Done when: `Print.MarksWithImposition` covers the three modes.
- [ ] Commit: `feat(print): add crop, bleed and registration marks with page information`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Print.MarksGeometry:Print.MarksPaperTooSmall:Print.MarksBleedSources:Print.MarksOutput:Print.MarksWithImposition` passes under ASan.

## 14. Print options pane and Ctrl+K commands for page selection, render modes and marks

The engine sections §11 to §13 need one place where a user sets them. This section adds the pages, output and marks groups to the Recto options pane that §10 extends beside the platform print dialog, and registers each option as a `Ctrl+K` command that opens the print dialog with the option preselected. It matches the print dialogs of Foxit PDF Editor, PDFelement and UPDF (Acrobat Pro has several of these options), and keeps Recto's rule of consuming the platform print dialog and never inventing a print shell.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new options inside the §2 and §10 print options pane, following `design/SPEC.md` section 9.9 dialog rules and the Floating Page Toolbar print button of section 8.2 (`print` glyph, section 6). Design board: `design/SPEC.md` section 9.16 with `design/screenshots/D-Print.png` (drawn by `D06 T04 §21`).
**Job:** A user who wants only the commented pages, one bookmarked chapter, a reversed stack, a grayscale or image copy, or pages with crop marks sets that in the print dialog or from `Ctrl+K` in two steps and sees the sheet count update before printing. Consumer: the print worker of §2.
**Treatment:** `recto-ui` controls (`D10 T02`) bound to `PrintSelection`, `PrintRenderOptions` and `PrinterMarksOptions`, each unavailable option disabled with its reason shown, and a live sheet count. Cheaper substitute that fails the checkpoint: a free text box where the user types page numbers computed by hand, or options reachable only by mouse.
**Chrome:** `ui::RadioButtons`, `ui::ToggleSwitch`, `ui::ComboBox`, `ui::NumberBox`, `ui::TreeView` and `ui::InfoBar` (`D10 T02 §7`, `§9`, `§6`, `§12`, `§20`), `Recto*Brush` tokens (`D10 T01 §8`), Fluent UI System Icons (`RectoIcons`), a reason line under each disabled control.

**Build order:**

1. Add a `Pages` group (All, Current, Range, Bookmark section, Pages with comments, Selected area, Reverse order) and an `Output` group (Print as image with DPI, Grayscale, Text as outlines) to the options pane. Done when: `Print.OptionsPaneBindings` drives each control and the sheet count matches the imposition plan of the resolved pages.
2. Add a `Marks` group, the `Ctrl+K` commands and per-printer persistence. Done when: `Print.OptionsCommandsReachable` finds every command and `Print.OptionsPersistence` restores the stored values.

- [ ] Bind the `Pages` group to `PrintSelection`, with a bookmark `TreeView` built from the document outline (disabled with "This document has no bookmarks" when empty), a comments option disabled with "This document has no printable comments" when none, and a live count of sheets and printed sides computed from the §10 imposition plan applied to the resolved pages (a sheet is one piece of paper, a side is one printed face; duplex halves sheets, N-up and booklet reduce them). Done when: `Print.OptionsPaneBindings` selects each option on a fixture and the displayed sheets equal the plan's sheet count, so 7 pages at 3 per sheet show 3 sheets, and a duplex job of 5 pages shows 3 sheets and 5 sides. Cheaper substitute: computing the count only after printing.
- [ ] Implement `Choose area`: hide the dialog, show a marquee on the viewer that confirms with `Enter` and cancels with `Esc`, then return with a chip such as "Page 3, 4.2 x 2.9 in" and a `Clear` button; the marquee reuses the viewer selection layer. Done when: `Print.OptionsAreaChooser` drives the marquee with the keyboard alone and the chip shows the confirmed size.
- [ ] Bind the `Output` group to `PrintRenderOptions` with the §12 interaction rules shown as reasons (image mode disables text as outlines). Done when: `Print.OptionsPaneBindings` asserts each disabled control carries a non-empty reason string.
- [ ] Bind the `Marks` group (crop, bleed, registration, page information, offset, weight, bleed source) to `PrinterMarksOptions` and show the `PaperTooSmallForMarks` size in a `ui::InfoBar`. Done when: `Print.OptionsMarksPaperWarning` shows the minimum paper size for the A4 and Letter fixture.
- [ ] Register `file.print.reverse`, `file.print.comments`, `file.print.bookmark`, `file.print.area`, `file.print.image`, `file.print.grayscale`, `file.print.outlines` and `file.print.marks` in `CommandRegistry` (`D02 T03 §11`) so `Ctrl+K` lists them, each opening the print dialog with that option set and disabled with a reason when no document is active. Done when: `Print.OptionsCommandsReachable` finds all eight commands and each preselects its option.
- [ ] Persist output and marks options per printer with the other print settings of §2 and never persist the page selection, which belongs to one job. Done when: `Print.OptionsPersistence` restores output and marks values after a restart and starts with `All pages` every time.
- [ ] Give every control an automation name and tooltip, a complete keyboard path and tokens only. Done when: `Print.OptionsAccessibility` finds no control without a name and no hex colour in the dialog source.
- [ ] Capture light and dark screenshots of the options pane with every group expanded. Done when: the screenshots are committed under `docs/release-screenshots/` and match the SPEC 9.9 chrome.
- [ ] Commit: `feat(print): add print options pane groups and Ctrl+K commands for selection, output and marks`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Print.OptionsPaneBindings:Print.OptionsAreaChooser:Print.OptionsMarksPaperWarning:Print.OptionsCommandsReachable:Print.OptionsPersistence:Print.OptionsAccessibility` passes on the display-session host.

## 15. Recto PDF virtual printer design: v4 driver, hand-off and trust boundary

Foxit PDF Editor and PDFelement install a virtual PDF printer so any Windows application can save a PDF, and Acrobat Pro installs `Adobe PDF`; owner decision D21 gives Recto a signed `Recto PDF` v4 printer driver on the XPS print pipeline, installed by the Full installer only. A print driver runs in the spooler as `SYSTEM`, so the design must keep every conversion in the user's session and treat the spooled file as hostile input. This section is a spike that ends in a written, dated decision; it implements nothing shippable. It keeps Recto's rules: offline, no telemetry, Portable mode installs nothing, temp-file-then-replace writes.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Prototype the hand-off candidates against Windows 11 and record measured behaviour. Done when: each candidate below has a recorded run with HRESULTs, file locations and account names.
2. Write `docs/specs/2026-10-21-virtual-printer-design.md` with the decision, trust boundary, install commands and rollback. Done when: the document is committed and quotes the measured results.

- [ ] Prototype candidate A: a v4 manifest driver whose queue uses the `PORTPROMPT:` port and a `FileSave` section for XPS and OpenXPS, then a Recto step that converts the file the user saved. Done when: the run records which account writes the file, where the dialog appears, and whether a job from a service or another session still works.
- [ ] Prototype candidate B: a v4 driver whose queue writes the spooled XPS to a per-user inbox folder that a user-session process watches and converts. Done when: the run records the inbox ACL needed, how the right user is chosen on a multi-user machine, and the behaviour when no user process is running.
- [ ] Prototype candidate C: a v4 driver with a `PrinterExtensions` `DriverEvent` app that is notified for the job and receives its spool file. Done when: the run records the events delivered, the account the app runs under and whether the spool file can be read.
- [ ] Evaluate candidate D, which Microsoft's v4 driver documentation now recommends for new work: the inbox IPP class driver with a Print Support App. Done when: the document states whether it can meet the D21 requirements (named queue, XPS or PDF output to a Recto handler, Full installer only) and, if it is clearly better, records a correction request for the owner and does not silently switch.
- [ ] Fix the trust boundary: the spooler and port only write the spool file; the conversion runs in the interactive user's session as that user; the spool file has a user-only ACL, a size cap of `1 GiB`, is never opened by `SYSTEM` code and is deleted after conversion on success and failure; no network is used. Done when: the design document lists each rule with the check that proves it (`§17`, `§19`).
- [ ] Record the driver identity: queue name `Recto PDF`, a fixed `PrinterDriverID` GUID, `NotShareable`, supported XPS formats, architecture coverage for `x64` and `ARM64`, default paper sizes and colour. Done when: the document lists the values and the source of the GUID.
- [ ] Record the exact install and removal commands (`pnputil`, `Add-PrinterDriver`, `Add-Printer`, `Remove-Printer`, `Remove-PrinterDriver`) and the signing route Hardware Dev Center accepts for a v4 printer driver (attestation or HLK submission). Done when: `§16`, `§19` and `§20` can be implemented from the document alone.
- [ ] Commit: `docs: record the Recto PDF virtual printer design decision (D21)`

**Test checkpoint:** the dated decision section of `docs/specs/2026-10-21-virtual-printer-design.md` and the measured candidate runs are quoted verbatim.

## 16. Recto PDF v4 driver package: manifest, INF, print capabilities and static validation

This section builds the driver package chosen in §15 and validates it statically, so the Full installer (§19) has a package to install and the operator can prove a test-signed install (`D99 T01 §6`). Nothing here installs a driver or switches the machine into test-signing mode. It is the Windows driver artefact of owner decision D21 and matches the virtual printers of Foxit PDF Editor and PDFelement. The package contains no code that parses documents; that stays in `Recto.exe` (§17), so a driver defect cannot become a parser exploit running as `SYSTEM`.

> **Groomed 2026-10-22:** removed `attended`. The package is authored and checked by script (`InfVerif`, `Inf2Cat` catalog generation, XML validation of the capabilities and ticket, the install scripts' dry-run and refusal paths); the test-signed install (`bcdedit` test signing, `pnputil /add-driver`, printing through the queue) moved to the operator section `D99 T01 §6` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/printer/RectoPrinter.vcxproj` producing the package (`RectoPDF.inf`, the v4 manifest, print capabilities and an unsigned catalog) for `x64` and `ARM64`. Done when: `build.cmd` builds both, `InfVerif` reports no errors and `Inf2Cat` generates the catalog without errors.
2. Author `scripts/install-test-printer.ps1` and `scripts/remove-test-printer.ps1` for the operator's test-signing machine (run only in `D99 T01 §6`), each with a `-WhatIf` plan mode. Done when: `tests/scripts/test_printer_package.py` shows `-WhatIf` prints every planned step and changes nothing, and a real run on a host that is not in test-signing mode exits `2` without touching the machine.

- [ ] Author `src/printer/RectoPDF.inf` and the v4 manifest with the §15 `PrinterDriverID`, `NotShareable`, the XPS formats and the port or extension wiring chosen there. Done when: `InfVerif /v` passes and the manifest parses with `MakeCat`. Cheaper substitute: reusing the name of another vendor's driver.
- [ ] Generate the catalog with `Inf2Cat /driver:<package folder> /os:10_X64,10_ARM64` (unsigned) in `src/printer/RectoPrinter.vcxproj`, and record the catalog name that `D07 T01 §20` and `D99 T01 §7` submit and replace. Done when: `RectoPDF.cat` exists for both architectures and `Inf2Cat` exits `0`.
- [ ] Author the `PrintDeviceCapabilities` and default `PrintTicket` files: paper sizes A3, A4, A5, Letter, Legal and Tabloid, portrait and landscape, colour, and no finishing or duplex features. Done when: `tests/scripts/test_printer_package.py` parses both XML files against the Print Schema namespaces, lists those six sizes, and finds the default ticket's paper size, orientation and colour as defined (the live query through `PrintQueue.GetPrintCapabilitiesAsXml` is part of `D99 T01 §6`).
- [ ] Write `scripts/install-test-printer.ps1`, which refuses to run unless the machine is in test-signing mode and the shell is elevated, stages the package with `pnputil`, adds the driver and the queue, and prints each step. Done when: `-WhatIf` lists the steps and the refusal path exits `2` on this host in `test_printer_package.py`; the real install on a clean test machine is proven in `D99 T01 §6`.
- [ ] Write `scripts/remove-test-printer.ps1`, which removes the queue, the driver and the staged package and reports anything left behind. Done when: `-WhatIf` lists the removal steps and a run with nothing installed exits `0` and reports nothing left (the real removal is proven in `D99 T01 §6`).
- [ ] Author `tests/scripts/verify_printer.ps1 -Mode Driver`, which installs the test package, prints a one page test document to the queue with the §17 hand-off disabled, finds one spool file, and removes the package in a `try / finally`; it exits `2` before touching anything unless the host is in test-signing mode. Done when: `test_printer_package.py` runs it on this host and gets exit `2` with no queue, driver or file created (the passing run is `D99 T01 §6`).
- [ ] Commit: `feat(printer): add the Recto PDF v4 driver package and static validation`

**Test checkpoint:** `python -m unittest tests.scripts.test_printer_package` passes, and `InfVerif /v` and `Inf2Cat` exit `0` on the built `x64` and `ARM64` packages, with their output lines quoted.

## 17. Virtual printer spool hand-off: XPS to PDF in the user's session

A job printed to `Recto PDF` must become a PDF without a second program, a network call or a conversion in the spooler. This section implements the user-session side of §15: `Recto.exe /virtual-print <spool>` and `recto-cli virtual-print` convert the spooled XPS with Recto's native XPS to PDF converter (`D04 T02 §5`) in an independent `recto::pdf::Document`, and save the result with temp-file-then-replace. The spool file comes from any local application, so it is hostile input like every other XPS file.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/app/printing/VirtualPrintJob.h/.cpp` (`validate_spool`, `convert_to_pdf`, `publish`) and the `recto-cli virtual-print --spool <file> --out <pdf> [--import]` verb over the same code. A spool is owned by Recto only when it sits in the private inbox fixed by §15 and was created by the driver path; `--import` marks a caller-supplied file, which is copied into a private scratch folder first and never deleted or modified. Both public entry points (`Recto.exe /virtual-print` and `recto-cli virtual-print`) are supervisors only: they never parse the spool in their own process and always run the conversion through `VirtualPrintSupervisor` (`src/app/printing/VirtualPrintSupervisor.h/.cpp`) in a bounded worker. Done when: `VirtualPrint.ConvertsSpool` converts a corpus spool with `--import` and `recto-cli virtual-print` exits `0` with the corpus file still present.
2. Add `/virtual-print` to `LaunchArgs` (`D02 T01 §7`) and forward to the running instance through the single-instance channel. Done when: `VirtualPrint.LaunchArgsForwarded` shows one instance handles two jobs in order.

- [ ] Implement `validate_spool`: refuse files over `1 GiB`, archives that fail the `ZipReader` bounds of `D04 T04 §1`, path traversal entries, external resource references and more than `10,000` parts. Done when: `VirtualPrint.HostileSpool` returns a typed error for a zip bomb, a `../` entry, an oversize file and a part count over the cap, each in under `50 ms` and without writing outside the temp folder.
- [ ] Implement `convert_to_pdf` over the `D04 T02 §5` converter in its own `recto::pdf::Document`, with the job name as the PDF title, producer `Recto PDF`, and the text kept selectable. Done when: `VirtualPrint.ConvertsSpool` extracts the fixture's words from the output with `recto-cli text` and the page count and sizes match the spool.
- [ ] Implement `publish` by writing a temp file in the destination folder and publishing it with `MoveFileExW` without `MOVEFILE_REPLACE_EXISTING`, which fails if the name exists, then retrying `name (2).pdf`, `name (3).pdf` and so on up to `1000`, so an existing file is never overwritten even when another job takes the name between check and publish. Done when: `VirtualPrint.PublishAtomicNoOverwrite` publishes twice to one folder, `VirtualPrint.PublishRace` runs 8 concurrent publishes of one name and ends with 8 distinct intact files, and killing the process between write and publish leaves no partial `.pdf`.
- [ ] Delete only owned spool files (inside the private inbox, opened once with a handle held through validation and conversion so a swapped path or reparse point cannot redirect the delete), after success and after failure, log the failure reason in the D16 ring log without document text, and return exit codes (`0` ok, `2` invalid spool, `3` destination not writable, `4` cancelled, `5` `ConversionMemoryLimit`, `6` `ConversionTimedOut`). Done when: `VirtualPrint.SpoolAlwaysDeleted` checks the owned spool is gone on each path, `VirtualPrint.ImportedFileNeverDeleted` imports a corpus file and finds it intact, `VirtualPrint.PathOutsideInboxRefused` refuses an owned-mode path outside the inbox, and `VirtualPrint.ReparsePointSubstitution` replaces the inbox file with a junction to a victim folder after validation and finds the victim untouched.
- [ ] Implement `VirtualPrintSupervisor::run`, used by both `Recto.exe` and `recto-cli virtual-print`: it creates a Job Object with `JOB_OBJECT_LIMIT_JOB_MEMORY` of `1 GiB` (a hard cap: commits beyond it fail, they do not end the process) and `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, registers a `JobObjectNotificationLimitInformation` job memory notification limit of `960 MiB` (whose `JOB_OBJECT_MSG_NOTIFICATION_LIMIT` delivery Windows guarantees, unlike `JOB_OBJECT_MSG_JOB_MEMORY_LIMIT`) on an associated I/O completion port, starts the worker mode `recto-cli.exe virtual-print --worker` (an internal flag of the same verb, so the verb registry of `D04 T08 §3` gains no verb) with `CREATE_SUSPENDED`, assigns it to the job before resuming it, and waits on the completion port and the process. The supervisor itself ends the worker with `TerminateJobObject`: on the notification limit message (confirmed with `JobObjectLimitViolationInformation`) or on the best-effort `JOB_OBJECT_MSG_JOB_MEMORY_LIMIT` it returns `ConversionMemoryLimit`, and after `30 s` of wall-clock time it returns `ConversionTimedOut`, so neither result depends on the worker cooperating. Inside the worker an allocation failure becomes `PdfError` `OutOfMemory` and exit code `5`, which the supervisor also maps to `ConversionMemoryLimit`. The worker inherits no handles except the spool and output, and `--worker` mode refuses to run (exit `2`, nothing parsed) unless `QueryInformationJobObject` on its own job shows the memory cap and kill-on-close flags. A test-only stub converter (`stall`, `allocate-and-swallow`, selected through `RECTO_TEST_HOOKS` builds only) stands in for the parser. Done when: `VirtualPrint.WorkerBounds` runs through `Recto.exe` and kills the `stall` stub at `30 s` with `ConversionTimedOut`, and kills the `allocate-and-swallow` stub (which catches every allocation failure and keeps looping) with `ConversionMemoryLimit` in under `5 s`, each leaving the app responsive and the owned spool deleted; `VirtualPrint.CliWorkerBounds` repeats both stubs by invoking `recto-cli virtual-print --spool <copy> --import` directly and gets exit codes `6` and `5` with no worker process left; and `VirtualPrint.WorkerRefusesUnsupervised` starts `recto-cli virtual-print --worker` outside a job and gets exit `2` without the spool being opened.
- [ ] Add the XPS spool entry point to the `fuzz_openxml_zip` dispatcher (`D06 T02 §3`) and seed it from `tests/corpus/`. Done when: `recto-fuzz.exe fuzz_openxml_zip` runs `60 s` on the XPS seeds without findings.
- [ ] Commit: `feat(printer): convert Recto PDF virtual printer spool files to PDF in the user session`

**Test checkpoint:** `recto-tests.exe --gtest_filter=VirtualPrint.*` (including `VirtualPrint.WorkerBounds`, `VirtualPrint.CliWorkerBounds` and `VirtualPrint.WorkerRefusesUnsupervised`) passes under ASan and `recto-cli virtual-print --spool tests/corpus/render/printed-sample.xps --import --out R:\scratch\vp.pdf` exits `0` with the corpus file still present.

## 18. Recto PDF virtual printer settings and the save window

When a user prints from another program to `Recto PDF`, the result is either saved to a configured folder or offered in a small save window, as in the virtual printers of Foxit PDF Editor and PDFelement. This section adds the three settings, their Settings page group, the save window and the status line that tells a user whether the printer is installed. It keeps Recto's rules: a setting that something reads (§17 reads all of them), `Ctrl+K` reachability, tokens and Fluent UI System Icons (`RectoIcons`), and an automation name on every icon-only control.

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** `design/SPEC.md` section 9.7 (the `Recto PDF virtual printer` group) and section 9.9 (dialog family) for the window. Design board: `design/SPEC.md` section 9.16 with `design/screenshots/D-Print.png` (drawn by `D06 T04 §21`).
**Job:** A user who prints a web page or an Office file to `Recto PDF` either finds the PDF in their chosen folder or names and places it in one small window, then optionally opens it in Recto. Consumer: `VirtualPrintJob` (§17).
**Treatment:** `SettingsStore` keys `VirtualPrinterSaveMode` (`AskEachTime` default, `SaveToFolder`), `VirtualPrinterFolder` and `VirtualPrinterOpenAfterSave` (default on), a Settings group with an install status line, and a small `ui::Window` (`D10 T01 §4`) with the file name, folder, a page 1 thumbnail and progress. Cheaper substitute that fails the checkpoint: saving every job to a fixed folder with no choice, or a settings row nothing reads.
**Chrome:** `ui::ToggleSwitch`, `ui::ComboBox`, `ui::TextBox` and `ui::Button` (`D10 T02 §7`, `§9`, `§4`, `§2`), the Settings row pattern of section 9.7, `Recto*Brush` tokens (`D10 T01 §8`).

**Build order:**

1. Add the three keys to `SettingsStore` (`D02 T04 §2`) and the `Recto PDF virtual printer` group to the Settings page. Done when: `VirtualPrintSettings.RoundTrip` persists and restores each key.
2. Add the save window shown by `/virtual-print` when the mode is `AskEachTime`. Done when: `VirtualPrintWindow.SaveFlow` saves a fixture job from the window.

- [ ] Add `VirtualPrinterSaveMode`, `VirtualPrinterFolder` and `VirtualPrinterOpenAfterSave` to `src/services/settings.h/.cpp` with validation (an absolute existing folder, defaulting to the user's Documents). Done when: `VirtualPrintSettings.RoundTrip` and `VirtualPrintSettings.RejectsInvalidFolder` pass.
- [ ] Build the `Recto PDF virtual printer` Settings group (SPEC 9.7, between `Assistant` and `Language and updates` through `kSettingsGroupOrder` of `D02 T04 §6`) with the save mode, folder picker, open-after-save toggle and a status line reading "Installed", "Not installed: run the Full installer as administrator" or "Not available in Portable mode", decided from `Get-Printer` equivalent API calls (`EnumPrintersW`). Done when: `VirtualPrintSettings.StatusLine` shows each of the three states with a stub printer list. Cheaper substitute: a static label that never reflects the real queue.
- [ ] Build the save window opened by `/virtual-print` in `AskEachTime` mode: editable file name, folder chip with `Change`, first page thumbnail, progress while converting, `Save` and `Cancel`, and `Save and open` when the open-after-save setting is on. Done when: `VirtualPrintWindow.SaveFlow` saves, cancels (exit code `4`, spool deleted) and opens the result in Recto.
- [ ] Skip the window and save directly in `SaveToFolder` mode, showing a Windows toast with `Open` and `Show in folder` only when the app is already running. Done when: `VirtualPrintWindow.DirectSave` produces the file with no window created.
- [ ] Register `printer.settings` and `printer.openfolder` in `CommandRegistry` so `Ctrl+K` finds "Recto PDF printer settings". Done when: `VirtualPrintSettings.CommandsReachable` finds both and the first scrolls to the group.
- [ ] Give every control an automation name and tooltip and check the keyboard path. Done when: `VirtualPrintWindow.Accessibility` finds none missing.
- [ ] Commit: `feat(printer): add Recto PDF virtual printer settings, status line and save window`

**Test checkpoint:** `recto-tests.exe --gtest_filter=VirtualPrintSettings.*:VirtualPrintWindow.*` passes on the display-session host.

## 19. Full installer integration and end-to-end proof for the Recto PDF printer

The Full installer installs the signed `Recto PDF` driver and queue; Portable mode and per-user installs install nothing, and uninstall removes everything (owner decision D21). This section wires the §16 package into `installer/recto.iss`, extends `tests/scripts/verify_installer.ps1`, and proves the hand-off from a spooled XPS reaches a PDF; the print through a really installed driver is proven by the operator in `D99 T01 §6`. The optional MSIX build cannot install a driver, so it omits the printer and says so.

> **Groomed 2026-10-22:** removed `attended`. The installer task, the uninstall rows, the Settings install status and the hand-off are proven in this section with the installer built into a temporary folder with the printer task off and with the spool pipeline fed a recorded XPS spool; installing the driver and printing through the installed queue (including upgrade and uninstall of a real queue) is the operator section `D99 T01 §6` (owner decision 2026-10-22 (foundation plan section 1)).

**Phase:** 6
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add the printer task and the `[Run]` and `[UninstallRun]` steps to `installer/recto.iss`, all guarded by `Check: not IsPortableMode` and an elevation check. Done when: `python -m unittest tests.scripts.test_installer_printer` passes.
2. Add `verify_installer.ps1 -Mode Printer`, which installs the Full installer silently into a temporary folder with the printer task off, asserts no queue, driver or `pnputil` entry appears, runs the spool hand-off, and uninstalls; `-Mode Printer -Live` (operator, `D99 T01 §6`) installs the printer task, prints, upgrades and uninstalls on a test-signing machine. Done when: `-Mode Printer` exits `0` on this host.

- [ ] Add a `printer` task to `installer/recto.iss` (checked by default, shown only when the installer runs elevated and the printer package is present in the stage folder) running the §15 install commands, and skip it with a logged message in `/CURRENTUSER` installs and in Portable mode. Done when: `tests/scripts/test_installer_printer.py` finds every printer `[Run]` row guarded and the task hidden when not elevated.
- [ ] Add the matching `[UninstallRun]` removal (queue, driver, staged package) that tolerates a missing queue and reports a stuck job. Done when: `tests/scripts/test_installer_printer.py` finds each removal row tolerant of a missing queue, and `verify_installer.ps1 -Mode Printer` finds no queue, driver or `pnputil` entry after uninstall (the real queue removal runs in `-Live`).
- [ ] On upgrade, replace the driver package in place when the version is newer and keep the queue and the user's saved settings. Done when: `test_installer_printer.py` finds the upgrade rows compare package versions and keep the queue name; `verify_installer.ps1 -Mode Printer -Live` (operator) upgrades `1.0.0` to `1.0.1` and finds the same queue name with the new package version.
- [ ] Feed the hand-off a spool made by printing `tests/corpus/render/two-column-article.pdf` with `Recto.exe /pt` to the in-box `Microsoft XPS Document Writer` queue when it is installed, otherwise the recorded `tests/corpus/render/printed-sample.xps`, through `Recto.exe /virtual-print --import` in `SaveToFolder` mode, and compare the output PDF text and page images. Done when: the PDF exists, `recto-cli text` returns the fixture's words, and page `SSIM >= 0.95` against the source render; the same print through the real `Recto PDF` queue is `D99 T01 §6`.
- [ ] Assert Portable mode (`/PORTABLE=1` and the `.zip`) installs no queue and the Settings status line reads "Not available in Portable mode". Done when: `verify_installer.ps1 -Mode Printer` finds no Recto printer after a portable run.
- [ ] Make `build/build-installer.ps1 -EnableMsix` skip the printer and write "The Recto PDF printer is not part of the MSIX package" to the build log and to `docs/store-listing.md`. Done when: the MSIX build log contains the line and `scripts/verify-release-kit.py` finds the sentence.
- [ ] Commit: `release: integrate and prove the Recto PDF virtual printer in the Full installer`

**Test checkpoint:** `pwsh -NoProfile -File tests/scripts/verify_installer.ps1 -Mode Printer` exits `0` with its `[PASS]` lines quoted, and `python -m unittest tests.scripts.test_installer_printer` passes.

## 20. Recto PDF driver signature check, signing runbook and release gate

A production printer driver must carry a signature that Windows accepts without test mode, and Microsoft issues it through the Windows Hardware Dev Center (owner decision D21). This section documents the signing procedure, adds the check that keeps an unsigned or test-signed driver out of a release, and defines what ships without a signature: the release script includes the printer only when a Hardware Dev Center signed catalog is present, otherwise it builds the release without the printer and the Settings status line says "Not installed", so a missing signature removes a feature and never ships a broken or untrusted driver. The submission and signing itself is the operator section `D99 T01 §7`.

> **Groomed 2026-10-22:** removed `third-party` and `operator`. The runbook, the signature check and the release-script rule are built and tested here without any account; submitting the package and signing it moved to `D99 T01 §7` (owner decision 2026-10-22 (foundation plan section 1)). No release-candidate section depends on that step.

**Phase:** 6
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `docs/driver-signing-runbook.md` and `scripts/check-driver-signature.ps1`. Done when: the script rejects an unsigned or test-signed catalog and accepts a Microsoft-signed one (an in-box catalog under `%SystemRoot%\System32\CatRoot` is the positive fixture).
2. Gate `build/build-installer.ps1` on that check and on the drop-in folder `installer/driver/signed/<arch>/` holding the signed catalog. Done when: a release build without a signed catalog, or with a test-signed one, omits the printer task and its files.

- [ ] Write `docs/driver-signing-runbook.md` covering the Hardware Dev Center account and its extended validation code signing certificate, building the submission package for `x64` and `ARM64`, the route chosen in §15 (attestation or HLK), downloading the signed package, placing it in `installer/driver/signed/<arch>/`, and verifying it on a clean machine without test mode. Done when: an operator can follow it without other documents, and the account steps are the operator actions of `D99 T01 §7`.
- [ ] Write `scripts/check-driver-signature.ps1` running `signtool verify /kp /v` on the catalog and failing on a missing signature or a test certificate (the `pnputil /add-driver` check on a clean machine belongs to `D99 T01 §7`). Done when: `tests/scripts/test_installer_printer.py` gets a non-zero exit for the §16 unsigned catalog and for a catalog signed with a throwaway test certificate it creates, and exit `0` for the in-box Microsoft-signed catalog.
- [ ] Add the gate to `build/build-installer.ps1`: a release build includes the printer task only when the signed catalog is present in `installer/driver/signed/<arch>/` and `check-driver-signature.ps1` passes; otherwise it builds the release without the printer task and files and logs "Recto PDF printer not included: no Hardware Dev Center signed catalog", so the Settings status line (`§18`) shows "Not installed". `-AllowTestSignedPrinter` exists for developer builds only and writes a banner to the log and the setup file name. Done when: `tests/scripts/test_installer_printer.py` finds a release build without a signature contains no printer task and no printer files.
- [ ] Record the shipping default in `docs/release-and-store-runbook.md`: version 1 ships without the printer if the signature is missing (the cost is a missing feature, not a broken one). Done when: the runbook states the default and how to add the printer in a point release.
- [ ] Commit: `release: gate the Recto PDF driver on a Hardware Dev Center signature`

**Test checkpoint:** `python -m unittest tests.scripts.test_installer_printer` passes and `pwsh scripts/check-driver-signature.ps1 -Path build/stage/x64/printer` exits non-zero for the unsigned and test-signed fixtures and `0` for the Microsoft-signed one, with the outputs quoted.

## Verification

- Milestone M6's printing, Inno Setup Full & Portable installer, Portable `.zip`, optional MSIX, `.pdf` file-association, upgrade continuity, clean uninstall, and portable zero-footprint clauses each carry dated, machine-verified evidence.
- The exact `RectoSetup-x64.exe` and `Recto-Portable-x64.zip` binaries produced by `build/build-installer.ps1` (`§5`) are tested by `tests/scripts/verify_installer.ps1` (`§6` and `§7`) with matching SHA-256 digests.
- `python scripts/todo-graph.py validate` passes.
