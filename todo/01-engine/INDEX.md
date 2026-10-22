# 01 Engine

> **Phase 1 (M1 Engine core)**

Recto's native C++23 PDF engine (`recto-pdf` in `src/pdf`) and concurrency runtime (`recto-core` in `src/core`): PDF 1.7 and 2.0 lexer, XRef tables and streams, damaged-file recovery, stream filters, Windows CNG `bcrypt.dll` decryption, content-stream interpreter, DirectWrite font loader, immutable `DisplayList`s, document actors, parallel tile rendering, `recto-cli`, libFuzzer targets, and the thread-safety proof. This is the frozen domain: every file is `frozen: true` because it establishes or guards the foundation plan section 4 contract.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-02](./TODO-02-actor-and-render.md) | Content Interpreter, Display Lists, Document Actor and Render Pool | active |
| [TODO-03](./TODO-03-cli-and-fuzz.md) | recto-cli, Fuzz Targets and the Thread-Safety Battery | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |
| [TODO-01](./TODO-01-bridge-runtime.md) | Native PDF Parser, Filters, Crypto and Document Model | 2026-10-20 |

## In scope

- `src/pdf` (`recto-pdf`: lexer, XRef parser, stream filters, CNG crypto, page tree, content interpreter, font loader, display lists), `src/core` (`recto-core` engine runtime, actors, tile render pool, scheduler, cancellation, shutdown), `src/cli`, `fuzz/`
- Single-owner `pdf::Document` per actor thread, immutable `DisplayList`s across worker threads, bounded parsing and decoding, deterministic rendering

## Out of scope

- GPU presentation and every UI surface (02), text selection/search/OCR features above core extraction (03)
- Conversion and page-surgery workflows (04, 05), suite-level quality programs (06)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
