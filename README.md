![Recto: open-source PDF viewer and converter for Windows](design/brand/github-banner.png)

# Recto

*the right-hand page*

Recto is being built as an open-source PDF viewer and converter for Windows 11: a fast, quiet reader on MuPDF with a native WinUI 3 interface, which will also convert (PDF to images, text and Word; images and Office documents to PDF) and organize pages (merge, split, reorder, rotate, encrypt). It is free software under the [GNU Affero General Public License, version 3 or later](LICENSE), and will be distributed through the Microsoft Store as MSIX.

**Status: pre-code.** This repository currently contains the approved design, the foundation plan and the machine-validated execution plan that will build version 1 milestone by milestone. There is no downloadable build yet.

## Start here

| Document | What it is |
| -------- | ---------- |
| [`design/SPEC.md`](design/SPEC.md) | The interface specification: every screen, dialog, token and keyboard shortcut. The screenshots it governs are in [`design/screenshots/`](design/screenshots/). |
| [`docs/plans/2026-10-13-recto-foundation.md`](docs/plans/2026-10-13-recto-foundation.md) | Decisions, architecture (a C MuPDF bridge, document-actor threading, tiled rendering), milestones M0 to M6 and performance targets. |
| [`todo/implementation-plan.md`](todo/implementation-plan.md) | The live, generated build order. What can run right now: `python scripts/todo-graph.py query ready`. |
| [`resources/`](resources/) | Every icon and graphic the app will ship, with rebuild instructions. |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | How to contribute, including the Contributor License Agreement. |

## License

Recto is copyright its contributors and licensed under the AGPL-3.0-or-later: see [LICENSE](LICENSE). Third-party components are listed in [NOTICE.md](NOTICE.md).
