# Contributing to Recto

Recto is built milestone by milestone from a validated TODO graph in [`todo/`](./todo/TODO-00-INDEX.md), governed by the architecture plan in [`docs/plans/2026-10-13-recto-foundation.md`](./docs/plans/2026-10-13-recto-foundation.md) and the interface specification in [`design/SPEC.md`](./design/SPEC.md).

## Before you start

1. Read [`AGENTS.md`](./AGENTS.md) for the non-negotiable architecture and interface rules (`recto-pdf` owns low-level PDF parsing and serialization, one `DocumentActor` thread per open document, immutable `DisplayList`s across threads, tokens only from `design/tokens/RectoColors.xaml`, atomic file writes).
2. Check [`todo/implementation-plan.md`](./todo/implementation-plan.md) or run `python scripts/todo-graph.py query ready` to see what work is dependency-safe right now. Every change corresponds to a section (`DNN TNN §N`), or captures a new one first under [`todo/README.md`](./todo/README.md).
3. Review [`CLA.md`](./CLA.md). Every pull request must include the contributor license agreement confirmation statement before it can be merged.

## Validation gates

Every change to `todo/` or `scripts/` must pass the graph gates locally and in CI (`plan-gates`):

```powershell
python scripts/todo-graph.py validate
python scripts/todo-graph.py plan --check
python -m unittest tests.scripts.test_todo_graph
```

Never edit `todo/implementation-plan.md` by hand; regenerate it with `python scripts/todo-graph.py plan --sync`.

## Building the solution

From a clean clone, one command restores NuGet packages, installs the vcpkg manifest (`gtest`, test-only), builds every project in `Recto.sln` (`recto-pdf`, `recto-core`, `recto-services`, `Recto`, `recto-cli`, `recto-tests`, `recto-fuzz`) for x64 and ARM64 in Debug and Release, and runs the x64 test binaries:

```powershell
.\build.cmd
```

Pass `-Platform x64` or `-Configuration Release` (or `-SkipTests`) through `build.cmd` to narrow a local run; see `build/build.ps1` for the full option list.

**Prerequisites:** Windows 11, Visual Studio 2026 with the Desktop development with C++ and WinUI application development workloads (v145 toolset, MSVC x64 and ARM64 compilers, Windows 11 SDK 10.0.26100, and the bundled `VC\vcpkg`), `git`, and network access on the first restore. Visual Studio 2022 cannot substitute: its MSBuild lacks the Windows Store ARM64 platform targets that the WinUI app project requires (`D00 T01 §3`).

## Pull requests and commits

- Branch from `main`, keep one logical section per commit, and cite the section reference (`DNN TNN §N`) in the commit body.
- Do not include credentials, signing material or private paths in commits, logs or pull-request descriptions.
- By submitting a pull request, you agree to the [`CLA.md`](./CLA.md) terms under `GPL-3.0-or-later`.
