# 01 Engine

> **Phase 1 (M1 Engine core)**

The MuPDF bridge, document actors, display lists, tile rendering, the CLI and the thread-safety proof. This is the frozen domain: every file is `frozen: true` because it establishes or guards the foundation plan section 4 contract.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-bridge-runtime.md) | MuPDF Bridge Runtime | active |
| [TODO-02](./TODO-02-actor-and-render.md) | Document Actor, Display Lists and Render Pool | active |
| [TODO-03](./TODO-03-cli-and-fuzz.md) | recto-cli, Fuzz Targets and the Thread-Safety Battery | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- `src/bridge` (the only MuPDF-header code), `src/core` engine types, `src/cli`, `fuzz/`
- Context clones, locks, error bridge, actors, display lists, scheduler, cancellation, shutdown

## Out of scope

- GPU presentation and every UI surface (02), text features above extraction (03)
- Conversion semantics (04, 05), suite-level quality programs (06)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
