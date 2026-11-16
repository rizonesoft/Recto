# Contributing to Recto

Recto is built milestone by milestone from a validated TODO graph in [`todo/`](./todo/TODO-00-INDEX.md), governed by the architecture plan in [`docs/plans/2026-10-13-recto-foundation.md`](./docs/plans/2026-10-13-recto-foundation.md) and the interface specification in [`design/SPEC.md`](./design/SPEC.md).

## Before you start

1. Read [`AGENTS.md`](./AGENTS.md) for the non-negotiable architecture and interface rules (`recto-pdf` owns low-level PDF parsing and serialization, one `DocumentActor` thread per open document, immutable `DisplayList`s across threads, colours only from the `recto-ui` tokens generated from `design/tokens/colors.json`, atomic file writes).
2. Check [`todo/implementation-plan.md`](./todo/implementation-plan.md) or run `build\python.cmd scripts/todo-graph.py query ready` (`./scripts/run-python.sh` on Linux/WSL) to see what work is dependency-safe right now. Every change corresponds to a section (`DNN TNN §N`), or captures a new one first under [`todo/README.md`](./todo/README.md).
3. Review [`CLA.md`](./CLA.md). Every pull request must include the contributor license agreement confirmation statement before it can be merged.

## Validation gates

Every change to `todo/` or `scripts/` must pass the graph gates locally and in CI (`plan-gates`):

```powershell
build\python.cmd scripts/todo-graph.py validate
build\python.cmd scripts/todo-graph.py plan --check
build\python.cmd -m unittest tests.scripts.test_todo_graph
```

Never edit `todo/implementation-plan.md` by hand; regenerate it with `build\python.cmd scripts/todo-graph.py plan --sync`.

## Commit hook

Once per clone, turn on the repository's hooks:

```powershell
git config core.hooksPath scripts/hooks
```

`scripts/hooks/pre-commit` refuses a commit that stages `todo/` changes while `todo/implementation-plan.md` is stale or the TODO graph is invalid (ticking a checklist item changes the generated plan), the same check CI's `plan-gates` runs. Fix with `build\python.cmd scripts/todo-graph.py plan --sync`, stage the plan, and commit again.

## Building the solution

From a clean clone, one command restores NuGet packages, installs the vcpkg manifest (`gtest`, test-only), builds every project in `Recto.sln` (`recto-pdf`, `recto-core`, `recto-services`, `Recto`, `recto-cli`, `recto-tests`, `recto-fuzz`) for x64 in Debug and Release, and runs the x64 test binaries (CI also builds ARM64 on every code push):

```powershell
.\build.cmd
```

Pass `-Platform x64,ARM64` or `-Platform ARM64` (or `-Configuration Release` / `-SkipTests`) through `build.cmd` when building ARM64 locally or narrowing a run; see `build/build.ps1` for the full option list.

**Prerequisites:** Windows 11, Visual Studio 2026 with the Desktop development with C++ workload (v145 toolset, MSVC x64 compiler, plus the MSVC ARM64 compiler when passing `-Platform ARM64`, Windows 11 SDK 10.0.26100, and the bundled `VC\vcpkg`), `git`, and network access on the first restore. Visual Studio 2022 cannot substitute: the projects use the v145 toolset, which only Visual Studio 2026 ships. `build.cmd` picks the newest Visual Studio 2026 that has the C++ x64 tools (and ARM64 tools when `-Platform` includes `ARM64`); the Windows 11 SDK 10.0.26100 is a separate install, and NuGet supplies C++/WinRT and the Windows SDK build tools.

### Language server (clangd)

- **Launcher (`build\clangd.cmd`):** Resolves Visual Studio 2026's LLVM `clangd.exe` (`Microsoft.VisualStudio.Component.VC.Llvm.Clang` C++ Clang tools component; install it alongside the C++ workload if `build\clangd.cmd` exits `2` naming that component) and runs it with `-j=2`, `--pch-storage=disk`, `--background-index`, and `--compile-commands-dir=build` unless overridden by caller flags (the CMake build names each SDK and CRT folder on every compile line as `/external:I`, so `build\compile_commands.json` is complete without the developer environment).
- **Compile database (`build\compile_commands.json`):** the CMake build (`CMAKE_EXPORT_COMPILE_COMMANDS`, D00 T04 §8) writes it, and `build\build.ps1` copies it into place only after a full x64 Debug build without ASan, so `-Project`, `Release`, ARM64 and `-EnableASAN` builds leave it alone; `scripts/gen-compile-commands.py` and `scripts/clangd-lsp.py` keep a CMake-written database as it is. `tests/scripts/test_cmake_build.py` (`LiveCompileDatabase`, `CompileDatabaseScope`) proves its coverage and scope.
- **Claude Code & MCP wiring:** Per Claude Code's documentation (`code.claude.com/docs/en/plugins-reference` and `code.claude.com/docs/en/mcp`), Claude Code reads no native LSP server table under `.claude/` (`.claude/settings.json` holds hooks, permissions, `env`, and MCP project-approval keys). Instead, repository-root `.lsp.json` uses Claude Code's plugin `.lsp.json` format (`extensionToLanguage` mapping `.c`, `.cpp`, `.cc`, `.cxx`, `.h`, `.hpp`, and `.inc` to `scripts/clangd-lsp.py`), which loads into Claude Code's built-in `LSP` tool when started with `claude --plugin-dir .` (with `ENABLE_LSP_TOOL=1` set when required by the installed Claude Code build), while `.mcp.json` (`mcpServers.recto-lsp` -> `scripts/lsp_mcp.py`) at the repository root is discovered automatically in every Claude Code session (approved on first prompt interactively, or automatically under `--dangerously-skip-permissions`) and `.agents/plugins/recto-lsp/mcp_config.json` serves Antigravity. On Windows, both launch `build\clangd.cmd` unless `RECTO_CLANGD` is set, and `.clangd` strips MSBuild-only flags so background indexing works identically across Claude Code and Antigravity.

## Pull requests and commits

- Branch from `main`, keep one logical section per commit, and cite the section reference (`DNN TNN §N`) in the commit body.
- Do not include credentials, signing material or private paths in commits, logs or pull-request descriptions.
- Sign off every commit you contribute with `git commit -s` (`Signed-off-by:`), certifying the [Developer Certificate of Origin 1.1](https://developercertificate.org/) as [`CLA.md`](./CLA.md) explains.
- By submitting a pull request, you agree to the [`CLA.md`](./CLA.md) terms under `GPL-3.0-or-later`.
