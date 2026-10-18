# 00 Workspace

> **Phase 0 (M0 Repository and build)**

The git repository, the GPL-3.0 repo-face files, the native C++20 `recto-pdf` and solution scaffold, this TODO system, and the test backbone every later domain builds on. Nothing here ships to users; everything later depends on it being boring and green.

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |

(none active)

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |
| [TODO-01](./TODO-01-repo-and-build.md) | Repository, Native Engine Policy and Build Scaffold | 2026-10-18 |
| [TODO-02](./TODO-02-todo-system.md) | The TODO Graph System | 2026-10-18 |
| [TODO-03](./TODO-03-test-backbone.md) | Test Backbone | 2026-10-18 |

## In scope

- Git repository, `GPL-3.0-or-later` repo-face files, native `recto-pdf` dependency policy (zero-MuPDF)
- Solution scaffold for every module in foundation plan 3.1 (`src/pdf`, `src/core`, `src/services`, `src/app`, `src/cli`, `tests/recto-tests`, `fuzz`), x64 and ARM64, one-command build, CI
- This TODO system's engine, skills, rules, CI gate and seeded graph
- GoogleTest runner, licensed corpus, ASan and warning configurations

## Out of scope

- The engine implementation itself (01) and every product surface (02 to 05)
- Test strategy content beyond the backbone, which is 06-quality's
- Packaging, Store and compliance (07), operator-only steps (99)

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
