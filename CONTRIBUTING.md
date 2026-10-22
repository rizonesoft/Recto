# Contributing to Recto

Recto is built milestone by milestone from a validated TODO graph in [`todo/`](./todo/TODO-00-INDEX.md), governed by the architecture plan in [`docs/plans/2026-10-13-recto-foundation.md`](./docs/plans/2026-10-13-recto-foundation.md) and the interface specification in [`design/SPEC.md`](./design/SPEC.md).

## Before you start

1. Read [`AGENTS.md`](./AGENTS.md) for the non-negotiable architecture and interface rules (`recto-pdf` owns low-level PDF parsing and serialization, one `DocumentActor` thread per open document, immutable `DisplayList`s across threads, colours only from the `recto-ui` tokens generated from `design/tokens/colors.json`, atomic file writes).
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

## Commit hook

Once per clone, turn on the repository's hooks:

```powershell
git config core.hooksPath scripts/hooks
```

`scripts/hooks/pre-commit` refuses a commit that stages `todo/` changes while `todo/implementation-plan.md` is stale or the TODO graph is invalid (ticking a checklist item changes the generated plan), the same check CI's `plan-gates` runs. Fix with `python scripts/todo-graph.py plan --sync`, stage the plan, and commit again.

## Building the solution

From a clean clone, one command restores NuGet packages, installs the vcpkg manifest (`gtest`, test-only), builds every project in `Recto.sln` (`recto-pdf`, `recto-core`, `recto-services`, `Recto`, `recto-cli`, `recto-tests`, `recto-fuzz`) for x64 and ARM64 in Debug and Release, and runs the x64 test binaries:

```powershell
.\build.cmd
```

Pass `-Platform x64` or `-Configuration Release` (or `-SkipTests`) through `build.cmd` to narrow a local run; see `build/build.ps1` for the full option list.

**Prerequisites:** Windows 11, Visual Studio 2026 with the Desktop development with C++ workload (the WinUI application development workload is needed only until `D10 T01 §2` replaces the WinUI scaffold) (v145 toolset, MSVC x64 and ARM64 compilers, Windows 11 SDK 10.0.26100, and the bundled `VC\vcpkg`), `git`, and network access on the first restore. Visual Studio 2022 cannot substitute: the projects use the v145 toolset, which only Visual Studio 2026 ships. `build.cmd` picks the newest Visual Studio 2026 that has the C++ x64 and ARM64 tools; the Windows 11 SDK 10.0.26100 is a separate install, and NuGet supplies C++/WinRT, the build tools and (until `D10 T01 §2`) the Windows App SDK.

## Pull requests and commits

- Branch from `main`, keep one logical section per commit, and cite the section reference (`DNN TNN §N`) in the commit body.
- Do not include credentials, signing material or private paths in commits, logs or pull-request descriptions.
- Sign off every commit you contribute with `git commit -s` (`Signed-off-by:`), certifying the [Developer Certificate of Origin 1.1](https://developercertificate.org/) as [`CLA.md`](./CLA.md) explains.
- By submitting a pull request, you agree to the [`CLA.md`](./CLA.md) terms under `GPL-3.0-or-later`.
