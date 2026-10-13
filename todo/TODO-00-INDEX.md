# Recto -- TODO Index

The live execution plan for building Recto to version 1. Format spec: [README.md](./README.md). The governing documents this tree executes are [`docs/plans/2026-10-13-recto-foundation.md`](../docs/plans/2026-10-13-recto-foundation.md) (decisions D1 to D17, architecture, milestones M0 to M6) and [`design/SPEC.md`](../design/SPEC.md) (the interface).

## How to use this tree

- **This file** carries domain order, the coverage map and the active TODOs. Keep it at that altitude: no checklists.
- **Each domain's `INDEX.md`** owns its own backlog and is the place to look for scope within a domain.
- **Execution order is the graph's, not this file's.** Ask it: `python scripts/todo-graph.py query ready`. The generated [`implementation-plan.md`](./implementation-plan.md) is the ordered projection; never infer readiness from a domain number and never repeat fixed totals here (live counts: `python scripts/todo-graph.py query stats`).
- Give every topic one canonical home. Cross-link with `-> XREF:` instead of duplicating scope.
- Numbering is local to a domain and never reused; section addresses (`DNN TNN §N`) are permanent.

## Domain order

| No. | Domain | Milestone | Purpose |
| :-: | ------ | :-------: | ------- |
| 00 | [Workspace](./00-workspace/INDEX.md) | M0 | Git repo, MuPDF pin, solution scaffold, CI, this TODO system, test backbone. |
| 01 | [Engine](./01-engine/INDEX.md) | M1 | C MuPDF bridge, document actors, display lists, tile rendering, CLI, fuzz targets, the thread-safety battery. Frozen domain. |
| 02 | [Viewer](./02-viewer/INDEX.md) | M2 | WinUI shell and tabs, tiled document view, Home and panels, command box, Settings, layout presets, theme. |
| 03 | [Text](./03-text/INDEX.md) | M3 | Text extraction, selection and copy, find, UI Automation text provider, accessibility audit. |
| 04 | [Convert](./04-convert/INDEX.md) | M4, M5 | Job frame and queue, PDF to images, images to PDF, PDF to text, PDF to Word, Office to PDF. |
| 05 | [Organize](./05-organize/INDEX.md) | M4 | Page operations (merge, split, reorder, encrypt) and the Combine, Split and Reorder grid. |
| 06 | [Quality](./06-quality/INDEX.md) | all | Golden, text, round-trip and performance suites; hostile-input audit; fuzz operations; broker design. |
| 07 | [Release](./07-release/INDEX.md) | M6 | Printing, MSIX packaging and install, About and AGPL compliance, Store submission. |
| 99 | [Manual](./99-manual/INDEX.md) | 99 | Operator-only steps: trademark, repository face, Partner Center, reference machines. No agent runner takes these rows. |

## Coverage: every promise in the governing documents, and who owns it

Seeded 2026-10-13 from the foundation plan and the SPEC. A claim found later with no owner gets a row here and a section through `add-todo` before it gets code.

| Promise | Owned by |
| ------- | -------- |
| M0 repo, scaffold, both-architecture CI | D00 T01 |
| This TODO system itself | D00 T02 |
| Test backbone, corpus provenance, ASan | D00 T03 |
| MuPDF locks, context clones, error bridge (plan 4.2 to 4.4) | D01 T01 |
| Actors, display lists, tiles, cancellation, shutdown (plan 4.5 to 4.10) | D01 T02 |
| `recto-cli`, fuzz targets, the 4.11 battery, M1 done-when | D01 T03 |
| App shell, title-bar tabs, open paths, password and error dialogs (SPEC 8, 9.2, 9.9) | D02 T01 |
| Presentation thread, tiles on screen, layouts, zoom, links, page colour (SPEC 9.2) | D02 T02 |
| Home and recents, Pages and Outline, Info, command box (SPEC 9.1, 8.3, 9.4) | D02 T03 |
| Settings page, layout presets, theme, keyboard map (SPEC 8, 9.7, 10) | D02 T04 |
| Selection, copy, find (SPEC 9.3) | D03 T01 |
| UIA text provider, keyboard order and names, announcements (SPEC 11) | D03 T02 |
| Job runtime, Convert page, queue, completion notices (SPEC 9.5, 9.9) | D04 T01 |
| PDF to images, images to PDF, PDF to text (plan 5) | D04 T02 |
| PDF to Word and the 20-file quality decision (plan 5, 11) | D04 T03 |
| Office to PDF both paths, macro safety (plan 5.1, D10) | D04 T04 |
| Merge, split, reorder, rotate, delete, extract, encrypt (plan 5) | D05 T01 |
| Organize grid: Combine, Split, Reorder, save guard (SPEC 9.6) | D05 T02 |
| Goldens, text tables, round-trips, performance targets (plan 8) | D06 T01 |
| Security rules audit, fuzz operations, broker design (plan 9, D13) | D06 T02 |
| Printing decision and implementation, MSIX, clean install (plan 7 M6) | D07 T01 |
| About, NOTICE and license audit, crash opt-in, Store submission (plan 10) | D07 T02 |
| Trademark search, repo face, Partner Center, reference machines (D1, plan 11) | D99 T01 |
| OCR for scanned documents | After v1 by plan section 2; no section yet, by decision |
| Sandboxed broker process | Designed in D06 T02 §3; built after v1 (D13) |

## Active TODOs

- [00 Workspace] [TODO-01 Repository, MuPDF Pin and Build Scaffold](./00-workspace/TODO-01-repo-and-build.md): git, AGPL repo-face files, pinned MuPDF with JS disabled, all-module scaffold, build CI, reference machines.
- [00 Workspace] [TODO-02 The TODO Graph System](./00-workspace/TODO-02-todo-system.md): engine, CLI and tests; the eight skills, rules and plan CI gate; this seeded graph. Built 2026-10-13, stamps against the first commit.
- [00 Workspace] [TODO-03 Test Backbone](./00-workspace/TODO-03-test-backbone.md): GoogleTest via vcpkg, licensed corpus, ASan and warning bar.
- [01 Engine] [TODO-01 MuPDF Bridge Runtime](./01-engine/TODO-01-bridge-runtime.md): locks and base context, `rc_status` error bridge, document open and description. Frozen.
- [01 Engine] [TODO-02 Document Actor, Display Lists and Render Pool](./01-engine/TODO-02-actor-and-render.md): actor threads, tile rendering, scheduler with cancellation, ordered shutdown. Frozen.
- [01 Engine] [TODO-03 recto-cli, Fuzz Targets and the Thread-Safety Battery](./01-engine/TODO-03-cli-and-fuzz.md): CLI verbs, libFuzzer targets, the 4.11 suite as M1's evidence. Frozen.
- [02 Viewer] [TODO-01 App Shell, Tabs and Document Lifecycle](./02-viewer/TODO-01-app-shell.md): packaged app, tokens, title-bar tabs and slots, four open paths, shell dialogs.
- [02 Viewer] [TODO-02 Document View](./02-viewer/TODO-02-document-view.md): presentation thread, tile cache, scroll and zoom, layouts, safe links, page colour, full screen.
- [02 Viewer] [TODO-03 Home, Panels, Document Info and the Command Box](./02-viewer/TODO-03-home-and-panels.md): recents and reading position, thumbnails and outline, Info, Ctrl+K over one registry.
- [02 Viewer] [TODO-04 Settings, Layout Presets and Theme](./02-viewer/TODO-04-settings-and-layout.md): settings store, preset engine over slots, all SPEC 9.7 groups, theme, the keyboard audit.
- [03 Text] [TODO-01 Text Extraction, Selection and Search](./03-text/TODO-01-selection-and-search.md): `PageText` model, quad-precise selection and copy, find with true counts.
- [03 Text] [TODO-02 Accessibility](./03-text/TODO-02-accessibility.md): UIA text provider, keyboard order and names inventory, polite announcements.
- [04 Convert] [TODO-01 Conversion Jobs and the Queue](./04-convert/TODO-01-jobs-and-queue.md): isolated job runtime, the Convert page frame, queue card, completion notices.
- [04 Convert] [TODO-02 Conversions A](./04-convert/TODO-02-pdf-conversions-a.md): writer plumbing and atomic saves, PDF to images, images to PDF, PDF to text.
- [04 Convert] [TODO-03 PDF to Word (DOCX)](./04-convert/TODO-03-pdf-to-word.md): docx writer wiring, the 20-file quality review and layout decision.
- [04 Convert] [TODO-04 Office to PDF](./04-convert/TODO-04-office-to-pdf.md): detection, COM path with macros off, LibreOffice headless, missing-converter proof.
- [05 Organize] [TODO-01 Page Operations](./05-organize/TODO-01-page-operations.md): graft merge and split, copy-based edits, AES-256 encrypt and decrypt.
- [05 Organize] [TODO-02 Organize Modes](./05-organize/TODO-02-organize-ui.md): the page grid in Reorder, Combine and Split, saves as jobs, unsaved-changes guard.
- [06 Quality] [TODO-01 Golden, Text, Round-Trip and Performance Suites](./06-quality/TODO-01-corpus-and-suites.md): pixel goldens, expectation tables, round-trip matrix, plan section 8 harness.
- [06 Quality] [TODO-02 Hostile-Input Rules, Fuzz Operations and the Broker Design](./06-quality/TODO-02-security-and-fuzzing.md): plan section 9 audit, scheduled fuzzing with triage, D13 design record.
- [07 Release] [TODO-01 Printing, MSIX Packaging and Install](./07-release/TODO-01-packaging-and-print.md): print API decision, packages both architectures, clean-machine and upgrade proof.
- [07 Release] [TODO-02 About, AGPL Compliance and Store Submission](./07-release/TODO-02-store-and-compliance.md): About with generated truth, NOTICE audit, opt-in crash reporting, submission to certification.
- [99 Manual] [TODO-01 Operator Setup (Manual)](./99-manual/TODO-01-operator-setup.md): trademark search, repository face, Partner Center reservation, reference machines. Operator only.

## Completed TODOs

- (none yet: the tree was seeded 2026-10-13 and the first stamps land on the first repository commit, D00 T01 §1)

## Queries

```powershell
python scripts/todo-graph.py query ready      # what can be worked right now
python scripts/todo-graph.py query blocked    # what is waiting, and on what
python scripts/todo-graph.py query stats      # tree health
python scripts/todo-graph.py query deferred   # open and resolved deferrals
python scripts/todo-graph.py validate         # structural and graph integrity
```
