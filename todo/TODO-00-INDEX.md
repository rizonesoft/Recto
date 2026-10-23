# Recto -- TODO Index

The live execution plan for building Recto to version 1. Format spec: [README.md](./README.md). The governing documents this tree executes are [`docs/plans/2026-10-13-recto-foundation.md`](../docs/plans/2026-10-13-recto-foundation.md) (decisions D1 to D21, D20 withdrawn; D4 revised 2026-10-22 for Recto's own UI toolkit; architecture, milestones M0 to M6) and [`design/SPEC.md`](../design/SPEC.md) (the interface).

## How to use this tree

- **This file** carries domain order, the coverage map and the active TODOs. Keep it at that altitude: no checklists.
- **Each domain's `INDEX.md`** owns its own backlog and is the place to look for scope within a domain.
- **Execution order is the graph's, not this file's.** Ask it: `python scripts/todo-graph.py query ready`. The generated [`implementation-plan.md`](./implementation-plan.md) is the ordered projection; never infer readiness from a domain number and never repeat fixed totals here (live counts: `python scripts/todo-graph.py query stats`).
- Give every topic one canonical home. Cross-link with `-> XREF:` instead of duplicating scope.
- Numbering is local to a domain and never reused; section addresses (`DNN TNN §N`) are permanent.

## Domain order

| No. | Domain | Milestone | Purpose |
| :-: | ------ | :-------: | ------- |
| 00 | [Workspace](./00-workspace/INDEX.md) | M0 | Git repo, GPL-3.0 repo-face files, native `recto-pdf` policy, scaffold, CI & test suite. |
| 01 | [Engine](./01-engine/INDEX.md) | M1 | Native C++23 `recto-pdf` parser, filters, CNG crypto, `DisplayList`s, actors & tile pool. |
| 02 | [Viewer](./02-viewer/INDEX.md) | M2 | `recto-ui` shell, tabs, tiled canvas, Home, panels, bookmarks, layers, settings & presets. |
| 03 | [Text](./03-text/INDEX.md) | M3 | Native `PageText`, built-in OCR, selection, regex find, replace, compare, markup & forms. |
| 04 | [Convert](./04-convert/INDEX.md) | M4, M5 | Job queue, Images/SVG/XPS, OCR PDF, tables, 3-mode Word, Excel, Web & PDF/A-UA-ZUGFeRD. |
| 05 | [Organize](./05-organize/INDEX.md) | M4 | Graft merge/split/edit, crop, stamps, Bates, AES-256, digital signatures & redaction. |
| 06 | [Quality](./06-quality/INDEX.md) | all | Golden, text, round-trip and performance suites; hostile-input audit, fuzz & broker spec. |
| 07 | [Release](./07-release/INDEX.md) | M6 | Printing, Inno Setup (Full & Portable), Portable `.zip`, MSIX, About, GPL & crash bundle. |
| 08 | [Assistant](./08-assistant/INDEX.md) | 7 | Opt-in AI assistant with the user's own Anthropic, OpenAI, OpenRouter or Gemini key, or a local model on loopback. |
| 10 | [UI Toolkit](./10-ui/INDEX.md) | M2 | `recto-ui`: Win32 window, composition, layout, tokens, text, input, Fluent controls & UIA. |
| 99 | [Manual](./99-manual/INDEX.md) | 99 | Operator-only steps: name, repo face, Partner Center, machines, proofs and release. |

## Coverage: every promise in the governing documents, and who owns it

Seeded 2026-10-13 from the foundation plan and the SPEC, and revised 2026-10-14 for the native C++23 `recto-pdf` engine, built-in OCR, full SautinSoft / Aspose.PDF / Syncfusion feature set, native converters, Inno Setup Full & Portable distribution, Portable mode setting, and `GPL-3.0-or-later`. A claim found later with no owner gets a row here and a section through `add-todo` before it gets code.

| Promise | Owned by |
| ------- | -------- |
| M0 repo, GPL-3.0 repo-face files, native scaffold, both-architecture CI | D00 T01 §1-§5 |
| This TODO system itself | D00 T02 §1-§3 |
| Test backbone, corpus provenance, ASan | D00 T03 §1-§6 |
| `recto-pdf` lexer, XRef parser, stream filters, CNG crypto & document model (4.1-4.4) | D01 T01 §1-§10 |
| Content interpreter, DirectWrite fonts, incremental save, `DisplayList` & `RenderPool` | D01 T02 §1-§10 |
| `recto-cli`, fuzz targets, the 4.9 battery, M1 done-when | D01 T03 §1-§7 |
| `recto-ui` toolkit replacing WinUI 3 (D4 revised 2026-10-22): window, composition, layout, tokens, text, input | D10 T01 §1-§20 |
| The Fluent control set the SPEC uses, rebuilt in `recto-ui`, with a golden-image gallery | D10 T02 §1-§27 |
| UI Automation providers, high contrast, text scaling and string tables (SPEC 4, 11) | D10 T03 §1-§8 |
| App shell, title-bar tabs, open/save paths, password and error dialogs (SPEC 8, 9.2, 9.9) | D02 T01 §1-§12 |
| Presentation thread, tiles on screen, layouts, zoom, links, page colour (SPEC 9.2) | D02 T02 §1-§10 |
| Home, Pages, Bookmark Editor, Layers (OCG), Portfolios, editable Info & `Ctrl+K` | D02 T03 §1-§12 |
| Settings page (including Portable mode `<exe-dir>\data\`), layout presets, theme & keys | D02 T04 §1-§10 |
| Native `PageText`, Hybrid Neural + `Windows.Media.Ocr`, selection, regex find, replace & compare | D03 T01 §1-§10 |
| UIA text provider, keyboard order and names, announcements (SPEC 11) | D03 T02 §1-§6 |
| 15 annotation/markup types, `/AP` synthesis, Markup Bar, Comments, AcroForms & Designer | D03 T03 §1-§10 |
| Job runtime, Convert page, queue, completion notices (SPEC 9.5, 9.9) | D04 T01 §1-§7 |
| `PdfWriter`, atomic saves, Images/TIFF/GIF/SVG/XPS, OCR PDF, Table/Image & Optimizer | D04 T02 §1-§13 |
| 3-mode (`Flowing`, `Exact`, `Continuous`) native `DocxWriter`, `RtfWriter` & 20-file gate | D04 T03 §1-§7 |
| 100% native C++23 OpenXML, legacy OLE2 (`.doc`/`.xls`/`.ppt`), RTF, HTML & MD to PDF (D7, D10) | D04 T04 §1-§7 |
| Native `XlsxWriter`, `WebDocWriter` (`HTML5`/`MD`/`EPUB`/`LaTeX`) & `PDF/A`/`UA`/`ZUGFeRD` | D04 T05 §1-§9 |
| `GraftMap` merge/split/edit, crop, watermarks, Bates, AES-256, PKCS#7/PAdES & redaction | D05 T01 §1-§10 |
| Organize grid (`Combine`/`Split`/`Reorder`), authoring dialogs, Redact Bar & Sign UI | D05 T02 §1-§11 |
| Goldens, text tables, round-trips, performance targets (plan 8) | D06 T01 §1-§8 |
| Security rules audit, fuzz operations, broker design (plan 9, D13) | D06 T02 §1-§6 |
| Gates for user-reported failure classes common in PDF tools | D06 T03 §1-§12 |
| SPEC subsections and `D-*.png` boards for every undesigned screen, with the board check | D06 T04 §1-§29 |
| `RectoIcons` font, `RectoChipStyle` and the Classic status bar (SPEC 6, 7, 8.4) | D02 T01 §18-§20 |
| Printing, Inno Setup (`Full` & `Portable`) installer, Portable `.zip`, MSIX & proof | D07 T01 §1-§26 |
| About, NOTICE and GPL-3.0 audit, opt-in local crash bundle & GitHub Release / Store kit | D07 T02 §1-§21 |
| Group Policy templates and locks (D21), ten interface languages and right-to-left readiness | D07 T02 §8-§17 |
| Listening to users: intake, triage, diagnostics, reproducers, releases, demand | D07 T03 §1-§11 |
| Versioning: SemVer from git tags, VERSIONINFO, downgrade guard, changelog, next version | D07 T01 §24-§26, D07 T02 §18-§21, D07 T03 §10-§11 |
| Opt-in AI assistant with the user's own key or a loopback local model, and its tools (D19) | D08 T01 §1-§19, D08 T02 §1-§35 |
| Trademark search, repo face, optional Partner Center, reference machines (D1, plan 11) | D99 T01 §1-§4 |
| Operator proofs left out of the unattended run, and the v1 release checklist (plan 1) | D99 T01 §5-§11 |
| Sandboxed broker process | Pre-approved design in D06 T02 §6; built after v1 (D13) |

## Active TODOs

- [01 Engine] [TODO-02 Content Interpreter, Display Lists, Document Actor and Render Pool](./01-engine/TODO-02-actor-and-render.md): content-stream interpreter, DirectWrite font loader, incremental save, immutable `DisplayList`s, actor threads, tile rendering, scheduler with cancellation, ordered shutdown. Frozen.
- [01 Engine] [TODO-03 recto-cli, Fuzz Targets and the Thread-Safety Battery](./01-engine/TODO-03-cli-and-fuzz.md): CLI verbs, libFuzzer targets, the 4.9 suite as M1's evidence. Frozen.
- [02 Viewer] [TODO-01 App Shell, Tabs, Menus, File Open/Save and Shell Dialogs](./02-viewer/TODO-01-app-shell.md): self-contained unpackaged and MSIX-ready app, tokens, custom title bar, tabs, named slots, Classic menu bar, four open paths, Save/Save As/Unsaved changes, Password and Could-not-open dialogs.
- [02 Viewer] [TODO-02 Document View: Presentation, Tiles, Layouts, Scroll, Zoom, Toolbar and Links](./02-viewer/TODO-02-document-view.md): presentation thread, viewport channel, worker D3D11 tile cache, Continuous/Single/Two-Page geometry, scroll, zoom, Floating Page Toolbar, safe links, dark page colour, Full Screen.
- [02 Viewer] [TODO-03 Home, Side Panels, Outlines, Layers, Portfolios, Document Info and Command Box](./02-viewer/TODO-03-home-and-panels.md): recents and reading position, Home page, `SidePanelHost`, thumbnails, Outline tree, interactive Bookmark Editor, OCG Layers, Attachments/Portfolios, read-only and editable Info/XMP/ViewerPreferences, `CommandRegistry`, `Ctrl+K` Command Box.
- [02 Viewer] [TODO-04 Settings Store, Portable Mode, Layout Presets, Settings Page, Theme and Keyboard Map](./02-viewer/TODO-04-settings-and-layout.md): `AppPaths` Portable mode resolver and live migration, `SettingsStore`, `LayoutPresetEngine`, all SPEC 9.7 settings groups, live theme and HighContrast, `167 ms` motion, and the audited SPEC 10 keyboard map.
- [03 Text] [TODO-01 Text Extraction, Built-in OCR, Selection and Search](./03-text/TODO-01-selection-and-search.md): native `PageText` extractor, built-in Hybrid C++23 Neural OCR + `Windows.Media.Ocr` pipeline (morphological line removal, XY-cut layout, CTC quad alignment, scanned pages & vector-outlined text), quad-precise selection and copy, find with regex, content-stream Find & Replace (`Ctrl+H`), and Compare PDFs (`Ctrl+Shift+D`). Frozen.
- [03 Text] [TODO-02 Accessibility](./03-text/TODO-02-accessibility.md): UIA text provider, keyboard order and names inventory, polite announcements.
- [03 Text] [TODO-03 Interactive Annotations, Markup, AcroForms and Form Designer](./03-text/TODO-03-annotations-and-forms.md): 15 annotation/markup types, `/AP` synthesis, flattening, `XFDF`/`JSON` I/O, Markup Bar (`Ctrl+Shift+A`), Comments panel, AcroForm & XFA-to-AcroForm engine, and Form Designer.
- [04 Convert] [TODO-01 Conversion Jobs and the Queue](./04-convert/TODO-01-jobs-and-queue.md): isolated job runtime, the Convert page frame, queue card, completion notices. Frozen.
- [04 Convert] [TODO-02 Conversions A: PDF Writer, Images/SVG/XPS, Text, OCR, Table/Image Extraction and Optimization](./04-convert/TODO-02-pdf-conversions-a.md): `PdfWriter` and atomic saves, PDF <-> images/multipage TIFF/GIF/SVG/XPS, PDF to text, searchable OCR PDF, `TableExtractor` & `ImageExtractor`, `PdfOptimizer` & Fast Web View `Linearizer`.
- [04 Convert] [TODO-03 PDF to Word (DOCX and RTF): 3-Mode Native OpenXML/RTF Writer and Quality Review](./04-convert/TODO-03-pdf-to-word.md): 3-mode (`Flowing`, `Exact`, `Continuous`) native C++23 `DocxWriter` and `RtfWriter` and the 20-file quality review.
- [04 Convert] [TODO-04 Office, Legacy Binary Office, HTML and Markdown to PDF: 100% Native C++23 Engines](./04-convert/TODO-04-office-to-pdf.md): 100% built-in C++23 OpenXML (`.docx`/`.xlsx`/`.pptx`), legacy OLE2 (`[MS-CFB]`/`.doc`/`.xls`/`.ppt`), RTF, CSV, HTML5/CSS, and Markdown to PDF engines with zero Office or LibreOffice dependency.
- [04 Convert] [TODO-05 PDF to Excel/CSV/XML, HTML5/Markdown/EPUB/LaTeX, and PDF/A, PDF/UA & ZUGFeRD Compliance](./04-convert/TODO-05-excel-web-and-compliance.md): native C++23 `XlsxWriter`, `WebDocWriter`, and `ComplianceEngine` (`PDF/A`, `PDF/UA-1`, `ZUGFeRD / Factur-X`) + Viewer Standards side panel.
- [05 Organize] [TODO-01 Page Operations, Stamping, Digital Signatures and True Redaction](./05-organize/TODO-01-page-operations.md): `GraftMap` merge and split, copy-based edits, blank-page insert, crop/resize, watermarks, headers/footers, Bates numbering, CNG AES-256 encrypt/decrypt, PKCS#7/PAdES/RFC 3161 digital signatures, and physical redaction & sanitization.
- [05 Organize] [TODO-02 Organize UI, Crop/Watermark/Bates Dialogs, Redaction Bar and Digital Signatures UI](./05-organize/TODO-02-organize-ui.md): the page grid in Reorder, Combine and Split, live-preview Crop/Watermark/Bates dialogs, Viewer Redaction & Sanitize Bar (`Ctrl+Shift+X`), Sign Document Dialog (`Ctrl+Shift+G`), and Signatures panel.
- [06 Quality] [TODO-01 Golden, Text, Round-Trip and Performance Suites](./06-quality/TODO-01-corpus-and-suites.md): pixel goldens, expectation tables, round-trip matrix, plan section 8 harness.
- [06 Quality] [TODO-02 Hostile-Input Rules, Fuzz Operations and the Broker Design](./06-quality/TODO-02-security-and-fuzzing.md): plan section 9 audit, scheduled fuzzing with triage, pre-approved D13 shared-memory broker design record.
- [07 Release] [TODO-01 Printing, Inno Setup (Full & Portable) Installer, Optional MSIX and Install Proof](./07-release/TODO-01-packaging-and-print.md): print API decision, Inno Setup Full & Portable installer, Portable `.zip`, optional MSIX, automated install/portable/upgrade proof.
- [07 Release] [TODO-02 About, GPL-3.0-or-later Compliance, Local Crash Bundle and Release Distribution](./07-release/TODO-02-store-and-compliance.md): About with generated truth, NOTICE audit, opt-in local minidump-plus-log crash bundle, GitHub Release bundle and optional Store submission kit.
- [06 Quality] [TODO-04 Design Boards and SPEC Text for Undesigned Screens](./06-quality/TODO-04-design-boards.md): a board check script and single-board rendering, then 26 new SPEC subsections, the completed section 9.3.1 and 27 `D-*.png` boards for the panels, bars, dialogs and modes no board drew, each before its first consumer. Draft until reviewed.
- [06 Quality] [TODO-03 User-Reported Failure Class Gates](./06-quality/TODO-03-user-failure-gates.md): hostile-input and recovered-error fixtures, a recovered-content page notice, the single-interpreter differential suite, save fault injection, raw-byte redaction proof, Unicode authoring round trips, UI glyph coverage, printer driver matrix, render and text fidelity fixtures, and the clean-machine release smoke.
- [06 Quality] [TODO-05 Audits of Sections Stamped Before the Current Review Process](./06-quality/TODO-05-stamp-audits.md): re-verify the 24 pre-2026-10-21 stamps under the current review.
- [07 Release] [TODO-03 Listening to Users: Intake, Triage, Diagnostics, Releases and Demand](./07-release/TODO-03-listening-to-users.md): issue forms and `SECURITY.md`, triage rules, in-app `Report a problem`, `recto-cli scrub` reproducers, a regression test per user bug, closing on release, the release gate, the scope page and demand ledger, and What's new with an opt-in update notice.
- [02 Viewer] [TODO-05 Rich Media, 3D Models and Geospatial PDFs](./02-viewer/TODO-05-rich-media-3d-geo.md): safe media playback, 3D model viewing and geospatial coordinates and measurement.
- [02 Viewer] [TODO-06 Cloud Storage Connectors: OneDrive, SharePoint, Google Drive, Dropbox and Box](./02-viewer/TODO-06-cloud-storage-connectors.md): opt-in OAuth PKCE connectors, browse, open as temporary copy, conditional save, off by default.
- [03 Text] [TODO-04 Edit PDF: Text, Image and Vector Object Editing](./03-text/TODO-04-edit-pdf.md): Acrobat-class Edit PDF over a stable paragraph model, images and vector objects.
- [03 Text] [TODO-05 Search Across Files and Local Index](./03-text/TODO-05-multi-file-search.md): search a folder of PDFs, with an optional local index.
- [03 Text] [TODO-06 Attachment Pin Markup Tool](./03-text/TODO-06-attachment-pin.md): the SPEC 8.2 `Attachment Pin` tool, embedding a file as a `FileAttachment` pin that asks before opening.
- [03 Text] [TODO-07 Command Box Rows for Comments, Form Fields and Indexed Files](./03-text/TODO-07-command-box-sources.md): the SPEC 9.4 `Go to` and `Files` row kinds that `D02 T03 §12` does not cover.
- [04 Convert] [TODO-06 Print Production: Preflight, PDF/X, Output Preview and Separations](./04-convert/TODO-06-print-production.md): preflight profiles and fixups, PDF/X, output preview, separations and ink coverage.
- [04 Convert] [TODO-07 Action Wizard: Saved Multi-Step Actions over Files and Folders](./04-convert/TODO-07-action-wizard.md): saved action sequences run over files and folders.
- [04 Convert] [TODO-08 Automation: recto-cli Verb Registry and the Local recto-mcp Server](./04-convert/TODO-08-automation-mcp.md): `recto-cli verbs --json` registry and a stdio-only `recto-mcp` server with an allow-list, staged inputs, new-file outputs and a call log (D21).
- [08 Assistant] [TODO-01 Opt-in AI Assistant with User-Supplied Provider Keys](./08-assistant/TODO-01-ai-assistant.md): off by default; summarize, ask with citations, explain, translate and alt text with the user's own key.
- [08 Assistant] [TODO-02 Assistant Workspace: Translate, Rewrite, Chat, Search, Commands and Tools](./08-assistant/TODO-02-assistant-workspace.md): whole-document translation keeping layout, rewrite and proofread, multi-file chat, semantic search, command planning, AI bookmarks and page fixes, study and contract tools, image and audio, and a keyless local provider.
- [10 UI Toolkit] [TODO-01 recto-ui Foundation: Window, Composition, Elements, Layout, Tokens, Text and Input](./10-ui/TODO-01-ui-foundation.md): `recto-ui` library and headless harness, the Win32 host replacing the WinUI scaffold, window frame, Mica Alt, DPI, DirectComposition, elements, layout, generated tokens, drawing and elevation, DirectWrite text, icons, input, focus, motion, popups, binding, drag-and-drop, the page canvas `SwapChainHost`, Snap Layouts hit testing and golden images.
- [10 UI Toolkit] [TODO-02 recto-ui Controls: the Fluent Control Set the SPEC Uses](./10-ui/TODO-02-ui-controls.md): buttons, tooltips, `TextBox` with TSF, password and number boxes, check, radio and toggle, sliders and progress, combo boxes, `ScrollViewer` with DirectManipulation, virtualized `ItemsView`, `TreeView`, `TabStrip`, menus and `MenuBar`, flyouts and cards, `Dialog`, segmented controls, settings cards, info bars, toasts and pills, `SearchBox`, colour swatches and custom colour entry, spell checking, calendar and date picker, pane splitter and the control gallery.
- [10 UI Toolkit] [TODO-03 recto-ui Accessibility: UI Automation, High Contrast, Text Scale and Strings](./10-ui/TODO-03-ui-accessibility.md): UIA provider tree and patterns, the reusable text-range adapter, announcements, high contrast, text scaling, string tables with pseudo-localization and right-to-left readiness, and the accessibility verification suite.
- [99 Manual] [TODO-01 Operator Setup (Manual)](./99-manual/TODO-01-operator-setup.md): trademark search, repository face, optional Partner Center reservation, reference machines, then the Narrator pass, driver install and signing, translation reviews, OAuth app registrations, clean-machine and FIPS proofs and the v1 release checklist. Operator only.

## Completed TODOs

- [00 Workspace] [TODO-01 Repository, Native Engine Policy and Build Scaffold](./00-workspace/TODO-01-repo-and-build.md) (2026-10-18): git, GPL-3.0 repo-face files, native `recto-pdf` policy, all-module C++23 scaffold, build CI, reference machines (`docs/reference-machines.md`).
- [00 Workspace] [TODO-02 The TODO Graph System](./00-workspace/TODO-02-todo-system.md) (2026-10-18): engine, CLI and tests (`scripts/todo-graph.py`); the eight skills (`.claude/skills/`), rules (`AGENTS.md`, `todo/README.md`) and plan CI gate (`.github/workflows/plan.yml`); this seeded 205-section execution graph.
- [00 Workspace] [TODO-03 Test Backbone](./00-workspace/TODO-03-test-backbone.md) (2026-10-18): `test_support` fixtures and RGBA/SSIM helpers (`tests/recto-tests/test_support.h`), 39-file deterministic CC0-1.0 PDF corpus (`scripts/build-corpus.py`, `tests/corpus/PROVENANCE.md`), and MSVC AddressSanitizer (`/fsanitize=address`) build and CI job.
- [01 Engine] [TODO-01 Native PDF Parser, Filters, Crypto and Document Model](./01-engine/TODO-01-bridge-runtime.md) (2026-10-20): `recto-pdf` error model (`src/pdf/error.h`), `PdfValue` AST (`src/pdf/object.h`), PDF 1.7/2.0 lexer and recursive-descent parser (`src/pdf/lexer.cpp`, `src/pdf/parser.cpp`), classic and compressed XRef/ObjStm resolver with damaged-file recovery (`src/pdf/xref.cpp`), stream filters (`src/pdf/filters.cpp`), Windows CNG `bcrypt.dll` `RC4`/`AES-128`/`AES-256` `R=2..6` security handler (`src/pdf/crypto.cpp`), and `recto::pdf::Document` model (`src/pdf/document.cpp`). Frozen.

## Queries

```powershell
python scripts/todo-graph.py query ready      # what can be worked right now
python scripts/todo-graph.py query blocked    # what is waiting, and on what
python scripts/todo-graph.py query stats      # tree health
python scripts/todo-graph.py query deferred   # open and resolved deferrals
python scripts/todo-graph.py validate         # structural and graph integrity
```
