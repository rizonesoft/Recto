# Contributing to Recto

Recto is open source from its first public commit, under the [GNU Affero General Public License, version 3 or later](LICENSE). Contributions are welcome: code, tests, documentation and corpus files.

## Contributor License Agreement

Every contributor must agree to the [Contributor License Agreement](CLA.md) before a first contribution is merged (decision D11 in the foundation plan). It keeps future relicensing possible without hunting down every past contributor. State your agreement in your first pull request, in the form CLA.md specifies.

## How work is planned

All work runs through the TODO graph in [`todo/`](todo/). That tree is the execution plan, and it is machine-validated; [`todo/implementation-plan.md`](todo/implementation-plan.md) is generated from it and is never edited by hand.

- Read [`todo/README.md`](todo/README.md) first: it is the format spec (section addresses, dependency graph, stamps).
- Find ready work with `python scripts/todo-graph.py query ready`, and inspect any row with `python scripts/todo-graph.py resolve 'D00 T01 §1'`.
- New work is filed as a section in the owning TODO file (or a new TODO file when no file owns the subject), never only as an issue or a comment in code. The `add-todo` and `create-todo` skills under [`.claude/skills/`](.claude/skills/) describe the routing and authoring workflow that maintainers use.
- One TODO section is one logical commit, closed by an independent review and a stamp (`python scripts/todo-graph.py stamp`), which is the only way a plan row flips to `[x]`.

The design's source of truth is [`design/SPEC.md`](design/SPEC.md); architecture, decisions and milestones live in [`docs/plans/2026-10-13-recto-foundation.md`](docs/plans/2026-10-13-recto-foundation.md). Working rules for humans and agents alike are in [`AGENTS.md`](AGENTS.md).

## Building

Requirements: Windows 11, Visual Studio 2026 with the Desktop development with C++ workload (v145 toolset and its bundled vcpkg) and the Windows 11 SDK 10.0.26100, network access for the first restore. VS 2022 cannot substitute: it ships no Windows Store ARM64 platform, so the app does not build for ARM64 there. Then, from a clean clone, one command builds everything (x64 and ARM64, Debug and Release) and runs the x64 tests:

```powershell
git clone https://github.com/rizonesoft/RectoPDF.git
cd RectoPDF
.\build.cmd
```

`build.cmd` fetches the MuPDF submodule and its nested thirdparty sources (all except MuJS, which stays uninitialized by design, see `build/mupdf-win32.targets`), restores the NuGet packages, installs the vcpkg manifest (GoogleTest, test-only) and builds `Recto.sln`. Narrow it with `.\build.cmd -Platform x64 -Configuration Release`. Artifacts land under `build/bin/<Platform>/<Configuration>/<Project>/`; run the tests any time with `build\bin\x64\Release\recto-tests\recto-tests.exe`. In Visual Studio, open `Recto.sln` and build as usual; the `mupdf` project is a Makefile wrapper and skips quickly when the MuPDF libraries are already built from the pinned commit and Recto config.

Today you can run the plan tooling, which needs only Python 3:

```powershell
python scripts/todo-graph.py validate
python scripts/todo-graph.py plan --check
python -m unittest tests.scripts.test_todo_graph
```

## Commits

- Trunk-based `main`, concise imperative messages. Section work uses `<area>: <what now works> (<DNN TNN §N>)` with the test checkpoint evidence in the body; plan-only changes use `todo: ...`.
- Never rewrite pushed history, never bypass hooks, and keep generated files out of hand edits: `todo/implementation-plan.md` regenerates with `python scripts/todo-graph.py plan --sync`, and everything in `resources/` regenerates with `design/tools/build_resources.py` (see [`resources/README.md`](resources/README.md)).
