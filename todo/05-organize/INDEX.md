# 05 Organize

> **Phase 4 (M4, with Conversions A)**

Authoring PDF structure and security: graft-based page operations in the engine (merge, split, reorder, rotate, delete, extract, insert blank page, crop/resize boxes), watermarks, headers/footers and Bates numbering (`src/core/stamp_watermark.cpp`), `AES-256` (`R=6`) encrypt/decrypt, X.509 / PKCS#12 / Windows Certificate Store / PAdES / RFC 3161 digital signatures (`src/pdf/signature.cpp`), and true physical content-stream redaction and sanitization (`src/pdf/redactor.cpp`), paired with SPEC 9.6's Organize grid, live-preview authoring dialogs, and the Viewer's Redaction & Sanitize Bar (`Ctrl+Shift+X`) and Digital Signature surfaces (`Ctrl+Shift+G`). Operations always work on a copy.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-page-operations.md) | Page Operations, Stamping, Digital Signatures and True Redaction | active |
| [TODO-02](./TODO-02-organize-ui.md) | Organize UI, Crop/Watermark/Bates Dialogs, Redaction Bar and Digital Signatures UI | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- `recto-pdf` `PdfWriter` and `GraftMap` (`src/pdf/graft.cpp`), `recto-core` page operation lists, blank-page insertion, page-box cropping/rescaling, watermarks, headers/footers, and Bates numbering (`src/core/stamp_watermark.cpp`)
- `AES-256` (`R=6`) encryption/decryption (`src/pdf/crypto.cpp`), `PKCS#7` / `PAdES` / `RFC 3161` digital signatures (`src/pdf/signature.cpp`), and true physical content-stream redaction and sanitization (`src/pdf/redactor.cpp`)
- The Organize page grid (`Combine`, `Split`, `Reorder`), live-preview `Crop & Resize`, `Watermark`, and `Header/Footer & Bates` dialogs, and the Viewer's `Redaction & Sanitize Bar` (`Ctrl+Shift+X`), `Sign Document Dialog` (`Ctrl+Shift+G`), and `Signatures` side panel, all built on `recto-ui` (`todo/10-ui/`)
- Fuzzing of page-range text and arbitrary edit sequences with reopen invariants (`D05 T01 §31`)

## Out of scope

- Format conversions to Word/Excel/HTML/Images (`04-convert`)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
