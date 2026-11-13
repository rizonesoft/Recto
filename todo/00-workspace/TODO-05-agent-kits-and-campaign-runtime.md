---
schema_version: 1
id: agent-kits-and-campaign-runtime
domain: 00-workspace
status: active
title: "TODO-05 -- Conclave Kits, Portable LSP, Hydrogen Helpers and Campaign Cadence"
track: M0
---

# TODO-05 -- Conclave Kits, Portable LSP, Hydrogen Helpers and Campaign Cadence

> **Goal:** Recto's developer and campaign toolset runs self-contained across Claude Code (`claude`) and Google Antigravity (`agy`) with zero external Python package dependencies, importing Conclave-SDK's `reviewer`, `todo` and `unattended` kits without breaking historical receipts, serving low-RAM C++23 and Python language servers over LSP and MCP, offloading mechanical read-only lookups in Antigravity to medium-effort Gemini 4 Hydrogen helpers (`gemini-4-hydrogen-medium`) without risking a single mutation or quality drop, balancing fast RAM-bounded local checks against heavy GitHub Actions CI runs, and running unattended campaigns reliably end to end.

> **Current state:** Measured 2026-11-13. Recto holds 105 verified stamps across historical `converge`, panel and rotation pins; importing Conclave-SDK `kits/reviewer`, `kits/todo` and `kits/unattended` naively would invalidate historical receipts or drop Recto's pre-commit hooks, reopened-section cycle checks and `ci_status.py` watches, while `todo-graph.py validate` spawns per-receipt `git show` subprocesses (~50 s on Windows). Neither runner has a portable C++23 + Python LSP/MCP bridge with a zero-dependency fallback when `pyright-langserver` is absent. Antigravity runs on Gemini 4 Carbon (`gemini-4-carbon-medium`) without read-only Gemini 4 Hydrogen helpers to absorb mechanical file/git/test lookups. Running all 38 Python suites and full multi-platform MSBuild builds on every edit starves RAM and slows iteration instead of letting GitHub Actions carry the full matrix while `ci_status.py` watches `main`. `scripts/start-unattended-run.ps1` also needs Conclave-SDK `unattended` preflight/runner parity, Windows `SetThreadExecutionState` sleep prevention, UTF-8 console encoding, pure-stdlib shortcut icons, and Stop-hook self-testing.

## Inputs

- Owner directives (2026-11-10, 2026-11-12 and 2026-11-13) recorded in `AGENTS.md`: full writer autonomy, self-improvement and self-healing, 100% stdlib portability in `scripts/`, Conclave-SDK kit parity (`kits/reviewer`, `kits/todo`, `kits/unattended`), portable C++23 and Python LSP servers, medium-effort read-only Gemini 4 Hydrogen helpers (`gemini-4-hydrogen-medium`), balanced local-vs-CI cadence, and unattended campaign reliability.
- Foundation plan `docs/plans/2026-10-13-recto-foundation.md` D4 (toolchain pins) and D14 (C++23 across `Recto.sln`).
- `.conclave/reviewers.toml`, `.claude/settings.json`, `.agents/hooks.json`, `.agents/skills.json`, `.github/workflows/build.yml` and `.github/workflows/plan.yml`.
- -> XREF: D00 T02 §1 -- the strict TODO graph engine whose `validate` path `§1` speeds up and extends with Conclave-SDK kit parity
- -> XREF: D00 T04 §3 -- MSBuild compiler parallelism; `§4` adds free-RAM `/m:<N>`, `/nr:false` and `build/restore.stamp` ahead of `§3`'s `/MP` pass
- -> XREF: D00 T04 §5 -- C++ build gate tiers; `§4` pairs them with `build.cmd -Quick` and `scripts/run-tests.py --quick` vs `--full`
- -> XREF: D00 T04 §9 -- CI watch after push that `§4` and `§5` wire across both Claude Code and Google Antigravity
- -> XREF: D00 T04 §12 -- MSBuild item-evaluated compile database and indexing benchmark that build on `§2`'s portable LSP servers
- -> XREF: D00 T04 §14 -- scheduled task clone-move check paired with `§5`'s unattended launcher and shortcut self-healing

## Outcome

- Conclave-SDK `reviewer`, `todo` and `unattended` kits (`three-pass` review schedule, conditional path lenses, `review-prompt.py`, `unattended.json`, `unattended-preflight.py`, `unattended-run.py`) are active on `main`, all 105 historical review receipts validate cleanly, and `python scripts/todo-graph.py validate` finishes in ~2.2 s via batch `git cat-file --batch`.
- Portable C++23 (`scripts/clangd-lsp.py` + `scripts/gen-compile-commands.py` + `.clangd`) and Python (`scripts/python-lsp.py` with zero-dependency stdlib AST/C3-MRO/UTF-16/PEP-263 fallback) language servers and `scripts/lsp_mcp.py` serve diagnostics, symbols, definitions, references and hover in both Claude Code and Google Antigravity with 5-minute idle auto-shutdown and zero errors across all 68 C++23 translation units.
- Google Antigravity sessions can spawn read-only Gemini 4 Hydrogen swarm helpers pinned to `medium` effort (`gemini-4-hydrogen-medium`, `workspace: "inherit"`), mechanically locked to read-only at both `PreToolUse` (`scripts/dispatch_hook.py`) and execution time (`scripts/hydrogen_exec.py` via `overwrite`, `shell=False`, sanitized environment, isolated temp-dir staging and managed process-tree cleanup) so 100% of writing, diagnosis, design and verdicts stay in Gemini 4 Carbon.
- Local iteration stays fast and RAM-bounded (`.\build.cmd -Quick`, `python scripts/run-tests.py --quick`, Win32 Job Object / POSIX lifeline process-tree containment in `scripts/lib/proc_group.py`) while `.github/workflows/build.yml` and `.github/workflows/plan.yml` (`--full`) carry the full matrix on GitHub Actions, watched by `scripts/ci_status.py`.
- `scripts/start-unattended-run.ps1` launches unattended campaigns in both `-Runner claude` (`opus`) and `-Runner agy` (`gemini-4-carbon-medium`) with Windows sleep prevention, UTF-8 encoding, portable token lookup, pure-stdlib shortcut icons (`scripts/campaign-icon.py`), moved-clone shortcut repair, and Stop-hook self-testing.

**Adjacency:** all=not-applicable (developer workspace, agent runner hooks, language servers and CI cadence; no user-facing surface, document or setting)

**Adjacency rationale:** Every script, hook, language server and workflow in this file governs repository development, review dispatch, local/CI test cadence and unattended agent execution; none ships in `Recto.exe` or touches end-user PDF documents or user settings.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Conclave-SDK reviewer, TODO and unattended kit import with receipt parity | -- | [ ] |
| 2 | §2 | Portable C++23 and Python LSP servers and MCP bridge across runners | §1 | [ ] |
| 3 | §3 | Read-only medium-effort Gemini 4 Hydrogen helpers and execution sandbox | §1 | [ ] |
| 4 | §4 | RAM-bounded build/test runner cadence and local-vs-CI gate split | §1 | [ ] |
| 5 | §5 | Dual-runner unattended campaign launcher, sleep guard and clone-move recovery | §1, §3, §4 | [ ] |

## 1. Conclave-SDK reviewer, TODO and unattended kit import with receipt parity

Recto's review dispatcher, TODO graph engine and unattended runner predated Conclave-SDK's `three-pass` review schedule (`plan`, `round`, `completion`, conditional `reconciliation`), path-scoped conditional lenses (`security`, `performance`) and structured unattended preflight/runner scripts. This section imports `kits/reviewer`, `kits/todo` and `kits/unattended` into Recto's stdlib-only `scripts/` and `.conclave/` configuration without breaking any of Recto's 105 historical review receipts, reopened-section cycle checks, pre-commit hook or `ci_status.py` gates, and replaces per-receipt `git show` subprocesses in `scripts/lib/todo_graph.py` with a persistent `git cat-file --batch` reader.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Import Conclave-SDK `kits/reviewer` (`scripts/review_pin.py`, `scripts/review-prompt.py`, `scripts/review-dispatch.py`, `.conclave/reviewers.toml` `three-pass` schedule and `docs/*-review-prompt.md`). Done when: `python scripts/review_pin.py --check` exits 0 and `scripts/tests/test_review_dispatch.py` passes.
2. Merge Conclave-SDK `kits/todo` `three-pass` stamp/receipt validation into `scripts/lib/todo_graph.py` and `scripts/todo-graph.py` while keeping historical `converge`, panel and rotation receipts valid and batching git blob reads through `git cat-file --batch`. Done when: `python scripts/todo-graph.py validate` and `plan --check` exit 0 in under 5 seconds across all 105 historical receipts and `tests.scripts.test_todo_graph` passes.
3. Import Conclave-SDK `kits/unattended` (`.conclave/unattended.json`, `scripts/unattended-preflight.py`, `scripts/unattended-run.py`, `docs/unattended-prompt.md`) adapted to Recto's dual-runner layout and zero external dependencies. Done when: `python -m unittest scripts.tests.test_preflight scripts.tests.test_unattended` passes.

- [ ] Upgrade `.conclave/reviewers.toml`, `scripts/review_pin.py`, `scripts/review-prompt.py`, `scripts/review-dispatch.py` and `docs/{plan,review-round,completion,reconciliation}-review-prompt.md` to Conclave-SDK `kits/reviewer` `three-pass` schedule with `--writer claude|agy|muse` routing to GPT-6 Astra and conditional `security`/`performance` path lenses. Done when: `python scripts/review_pin.py --check` exits 0 and `python -m unittest scripts.tests.test_review_dispatch` passes. Cheaper substitute: overwriting `scripts/review-dispatch.py` verbatim without `agy` writer routing or historical rotation replay, which breaks `validate` on earlier stamps.
- [ ] Extend `scripts/lib/todo_graph.py` and `scripts/todo-graph.py` to validate both `three-pass` receipts and Recto's 105 historical `converge`/panel/rotation receipts, preserving reopened-section `-cycle<N>` checks and replacing per-receipt `git show` spawns with a persistent `git cat-file --batch` reader. Done when: `python scripts/todo-graph.py validate` exits 0 in under 5 seconds on the live tree and `python -m unittest tests.scripts.test_todo_graph` passes. Cheaper substitute: skipping historical receipt pin replay, which silently weakens receipt verification.
- [ ] Add `.conclave/unattended.json`, `scripts/unattended-preflight.py`, `scripts/unattended-run.py` and `docs/unattended-prompt.md` from Conclave-SDK `kits/unattended`, wired to Recto's `todo-graph.py`, `ci_status.py` and `campaign_guard.py` with 100% Python stdlib. Done when: `python -m unittest scripts.tests.test_unattended` passes with zero third-party imports. Cheaper substitute: hardcoding machine paths in `.conclave/unattended.json`, which breaks clone portability.
- [ ] Synchronize `.claude/skills/{process-todo-section,review-todo-section,independent-review,process-phase,process-plan}/SKILL.md` and `.agents/skills.json` with the `three-pass` schedule and dual-runner `claude`/`agy` commands. Done when: `python -m unittest scripts.tests.test_three_pass tests.scripts.test_verify_agent_tiers` passes.
- [ ] Commit: `kits: import Conclave-SDK reviewer, todo and unattended kits with receipt parity`

**Test checkpoint:** `python scripts/review_pin.py --check`, `python scripts/todo-graph.py validate`, and `python -m unittest tests.scripts.test_todo_graph scripts.tests.test_review_dispatch scripts.tests.test_unattended` all exit 0.

## 2. Portable C++23 and Python LSP servers and MCP bridge across runners

Searching and reading multi-hundred-kilobyte C++23 and Python files by text wastes context and misses type errors until build time, while developer hosts may lack `pyright-langserver` or move clones across directories. This section adds portable C++23 (`scripts/gen-compile-commands.py`, `.clangd`, `scripts/clangd-lsp.py`) and Python (`scripts/python-lsp.py` with dynamic `pyright-langserver` discovery plus a zero-dependency stdlib AST, C3-MRO, UTF-16 and PEP-263 fallback) language servers and a low-RAM MCP stdio bridge (`scripts/lsp_mcp.py` with 5-minute idle shutdown), wired into Claude Code (`.lsp.json`, `.mcp.json`) and Google Antigravity (`.agents/plugins/recto-lsp/`), and resolves incomplete-`PdfValue` C++23 member instantiation in `src/pdf/object.h` so `clangd` checks all 68 translation units cleanly.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Generate portable `build/compile_commands.json` from `Recto.sln` and `.vcxproj` files via `scripts/gen-compile-commands.py`, configure `.clangd`, fix `src/pdf/object.h` C++23 `PdfValue` completeness, and wrap `clangd` in `scripts/clangd-lsp.py`. Done when: `python scripts/gen-compile-commands.py --check` exits 0 across all 68 translation units and `clangd --check=src/pdf/object.cpp` reports 0 errors.
2. Build `scripts/python-lsp.py` supporting `pyright-langserver` (including spaced Windows `.cmd` shim paths under `lib.proc_group`) and a full zero-dependency stdlib AST/symtable/tokenize/C3-MRO/UTF-16 fallback. Done when: `tests/scripts/test_lsp.py` proves diagnostics, symbols, definitions, references and hover in both Pyright and stdlib-fallback modes, including Fibonacci-DAG MRO memoization under 0.5 s.
3. Expose `lsp_diagnostics`, `lsp_symbols`, `lsp_definition`, `lsp_references` and `lsp_hover` over stdio MCP (`scripts/lsp_mcp.py`) with fail-closed `publishDiagnostics` version matching and 5-minute idle reaping, wired in `.lsp.json`, `.mcp.json` and `.agents/plugins/recto-lsp/`. Done when: `python -m unittest tests.scripts.test_lsp` passes cleanly with zero leaked child processes.

- [ ] Add `scripts/gen-compile-commands.py`, `.clangd` and `scripts/clangd-lsp.py` discovering Visual Studio / LLVM `clangd` and MSVC + Windows SDK + vcpkg + NuGet includes dynamically with `-j=2` and `--pch-storage=disk`, and make `PdfArray`/`PdfDict` hold complete `PdfValue` elements safely in `src/pdf/object.h`. Done when: `python scripts/gen-compile-commands.py --check` prints `ok: 68 translation units` and `clangd --check=src/pdf/object.cpp` exits 0 with 0 errors. Cheaper substitute: suppressing `clangd` template diagnostics in `.clangd`, which hides real C++23 standard-library requirements.
- [ ] Add `scripts/python-lsp.py` launching `pyright-langserver` directly via `lib.proc_group.spawn_managed_process` when installed (preserving spaced Windows `.cmd` shim paths) and falling back automatically to a pure-stdlib Python LSP server (`textDocument/publishDiagnostics`, `documentSymbol`, `workspace/symbol`, `definition`, `references`, `hover`, UTF-16 code units, PEP-263 encodings and memoized C3 MRO). Done when: `tests/scripts/test_lsp.py` passes both spaced-shim Pyright launch and pure-stdlib fallback suites. Cheaper substitute: failing when `pyright-langserver` is not installed globally, violating self-contained repo portability.
- [ ] Add `scripts/lsp_mcp.py` implementing MCP stdio (`Content-Length` and newline JSON-RPC, 16 MiB frame bound, fail-closed version-matched diagnostics and 5-minute idle shutdown) and wire `.lsp.json`, `.mcp.json` and `.agents/plugins/recto-lsp/{plugin.json,mcp_config.json}` with relative paths only. Done when: `python -m unittest tests.scripts.test_lsp` passes and `git grep -i "Users\\\\"` finds zero hardcoded user paths in tracked configs. Cheaper substitute: keeping language servers resident indefinitely, wasting gigabytes of RAM while MSBuild runs.
- [ ] Commit: `lsp: add portable C++23 and Python LSP servers and low-RAM MCP bridge`

**Test checkpoint:** `python scripts/gen-compile-commands.py --check` prints `ok: 68 translation units`, and `python -m unittest tests.scripts.test_lsp` exits 0.

## 3. Read-only medium-effort Gemini 4 Hydrogen helpers and execution sandbox

In Google Antigravity (`agy`), Gemini 4 Carbon owns 100% of code, diagnosis, design and review verdicts, while mechanical codebase lookups, git/TODO status queries and targeted test runs can be offloaded to Gemini 4 Hydrogen helpers on the native `swarm` board without degrading implementation quality -- provided Hydrogen runs at `medium` effort (`gemini-4-hydrogen-medium`) and is mechanically incapable of mutating the checkout or spawning arbitrary commands. This section wires that two-layer read-only lock (`scripts/dispatch_hook.py` at `PreToolUse` plus `scripts/hydrogen_exec.py` at execution time) and documents the four helper roles in `.agents/agents/README.md`.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Enforce `gemini-4-hydrogen-medium` (`workspace: "inherit"`) and the positive read-only tool allowlist in `scripts/dispatch_hook.py` (`.agents/hooks.json`, `PreToolUse`, `matcher: "*"`), rewriting every permitted `run_command` via `overwrite` to `scripts/hydrogen_exec.py --payload <hex>`. Done when: `tests/scripts/test_dispatch_hook.py` proves `low`, `high`, `xhigh` and unpinned Hydrogen models, `branch`/`share` workspaces, write/edit/task tools and arbitrary shell commands are denied.
2. Implement `scripts/hydrogen_exec.py` re-validating exact-spelling `git`, `python`, `node` and `recto-tests.exe` argv allowlists at execution time with `shell=False`, sanitized environment variables, isolated temporary-directory repo/binary staging outside the checkout, and `lib.proc_group` process-tree cleanup on cancellation. Done when: `tests/scripts/test_dispatch_hook.py` proves forged hex payloads fail closed, staged tests leave zero files in the live checkout, sibling `Recto/Recto.exe` binaries resolve in staged GTest runs, and cancelling `hydrogen_exec.py` reaps grandchildren and deletes the temp directory.
3. Document the four `gemini-4-hydrogen-medium` swarm helper roles (`code-explorer`, `git-runner`, `test-runner`, `debug-evidence`) in `.agents/agents/README.md` and `AGENTS.md`. Done when: both files name `gemini-4-hydrogen-medium` and `scripts/start-unattended-run.ps1` matches.

- [ ] Pin `HYDROGEN_ALLOWED_MODELS` in `scripts/dispatch_hook.py` to `gemini-4-hydrogen-medium` (`Gemini 4 Hydrogen (Medium)`) with `workspace: "inherit"`, allow only read-only file/search/board/`recto-lsp` tools inside any Hydrogen turn, and rewrite allowed `run_command` calls via `overwrite` to `scripts/hydrogen_exec.py --payload <hex>`. Done when: `DispatchHook.test_hydrogen_swarm_spawn_requires_medium_effort_and_inherit_workspace` and `test_hydrogen_worker_is_strictly_read_only` pass. Cheaper substitute: prompt-only instructions telling Hydrogen not to edit files, which has zero mechanical enforcement.
- [ ] Build `scripts/hydrogen_exec.py` with exact-spelling `git`/`python`/`node`/`recto-tests` option allowlists, sanitized env (`GIT_EXTERNAL_DIFF`, `PYTHONSTARTUP`, `NODE_OPTIONS` stripped), isolated temp-dir worktree and `<Plat>/<Cfg>` runtime staging (skipping `.pdb`/`.idb`/`.lib`), and `lib.proc_group` passthrough lifecycle management. Done when: `test_hydrogen_exec_isolates_checkout_and_rejects_forged_payloads_at_execution_time` and `test_hydrogen_exec_cancellation_reaps_grandchild_and_cleans_staged_temp_dir` pass. Cheaper substitute: running Hydrogen test commands directly in the live working tree under `pwsh`, allowing side-effect writes or orphaned grandchildren.
- [ ] Document `gemini-4-hydrogen-medium` helper spawning, reuse and role briefs in `.agents/agents/README.md`, `AGENTS.md` and `scripts/start-unattended-run.ps1`. Done when: `git grep "gemini-4-hydrogen-medium"` matches all three files and no tracked doc instructs spawning `high` or `low` effort Hydrogen helpers.
- [ ] Commit: `agents: lock Gemini 4 Hydrogen swarm helpers to medium effort and read-only execution`

**Test checkpoint:** `python -m unittest tests.scripts.test_dispatch_hook` passes in full, including live `hydrogen_exec.py` isolation and cancellation reaping.

## 4. RAM-bounded build/test runner cadence and local-vs-CI gate split

Running every heavy Python suite and full multi-platform MSBuild build on each local edit turns 90% of campaign wall time into redundant local test execution and exhausts host RAM, whereas skipping local checks altogether risks pushing broken commits. This section establishes the balanced local-vs-CI cadence: `.\build.cmd -Quick` (`build/build.ps1` with free-RAM `/m:<N>`, `/nr:false` and verified `build/restore.stamp`) and `python scripts/run-tests.py --quick` (skipping heavy unchanged suites locally while scaling workers from available physical memory and bounding child process trees through `scripts/lib/proc_group.py`), paired with `.github/workflows/plan.yml` (`--full`) and `.github/workflows/build.yml` carrying the full matrix in GitHub Actions under `scripts/ci_status.py` monitoring.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Build `scripts/lib/proc_group.py` (Win32 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` suspended-spawn Job Objects and POSIX `/proc` + `_RECTO_MANAGED_GROUP` + kernel-EOF lifeline reaper) and wire it into `scripts/run-tests.py`, `scripts/mutate-check.py` and `scripts/review-dispatch.py`. Done when: hung or interrupted test workers and cross-session grandchildren are terminated cleanly without pipe deadlocks in `tests/scripts/test_run_tests.py` and `scripts/tests/test_review_dispatch.py`.
2. Add `scripts/run-tests.py` discovering both `tests/scripts` and `scripts/tests`, scaling worker count from free RAM, running `--quick` by default locally (deferring heavy suites unless their watched paths have uncommitted or unpushed changes relative to `origin/main`) and `--full` in `.github/workflows/plan.yml`. Done when: `python scripts/run-tests.py --quick` passes locally and `.github/workflows/plan.yml` runs `--full`, `todo-graph.py validate`, `plan --check` and `verify-agent-tiers.mjs` as explicit steps.
3. Add `-Quick`, `-Project`, free-RAM MSBuild `/m:<N>` + `/nr:false`, and input-hashed `build/restore.stamp` to `build.cmd` and `build/build.ps1`. Done when: `.\build.cmd -Quick -Project recto-pdf -SkipTests` builds `recto-pdf` x64 Debug with `/nr:false` and skips redundant NuGet/vcpkg restores when `restore.stamp` matches `project.assets.json`, verified by `tests/scripts/test_build_ps1.py`.

- [ ] Implement `scripts/lib/proc_group.py` providing lifetime-bound Win32 Job Objects and POSIX cross-session `/proc` + lifeline-reaper process-tree containment, and wire it into `scripts/run-tests.py`, `scripts/mutate-check.py` and `scripts/review-dispatch.py`. Done when: `python -m unittest tests.scripts.test_run_tests` passes all timeout, `KeyboardInterrupt` and orphan-grandchild reaping tests. Cheaper substitute: `Popen.kill()` on the immediate child PID alone, which leaks grandchildren holding stdout pipes open.
- [ ] Add `scripts/run-tests.py` (`--quick`, `--full`, `--filter`, `--jobs`, `--timeout`, `--list`) with RAM-bounded worker scaling (`_available_ram_mib` on Windows, Linux and macOS) and git-diff input detection against `origin/main`, and wire `python scripts/run-tests.py --full` into `.github/workflows/plan.yml`. Done when: `tests/scripts/test_run_tests.py` verifies `--quick` skips heavy suites only when clean and runs them as soon as watched inputs change, and `plan.yml` runs `--full`. Cheaper substitute: dropping heavy suites from local `--quick` even when `scripts/todo-graph.py` or `scripts/ci_status.py` was edited, hiding local regressions.
- [ ] Add `-Quick`, `-Project <name>`, free-RAM `/m:<N>` + `/nr:false` node-reuse disabling, and `build/restore.stamp` verification to `build.cmd` and `build/build.ps1`, and record the local-vs-CI cadence in `AGENTS.md`. Done when: `python -m unittest tests.scripts.test_build_ps1` passes and `.\build.cmd -Quick -Project recto-pdf -SkipTests` exits 0. Cheaper substitute: leaving `/nr:true` default MSBuild node reuse enabled, keeping idle `MSBuild.exe` workers holding memory after builds finish.
- [ ] Commit: `build: add RAM-bounded build/test cadence and managed process-tree cleanup`

**Test checkpoint:** `python -m unittest tests.scripts.test_run_tests tests.scripts.test_build_ps1` passes, and `.\build.cmd -Quick -Project recto-pdf -SkipTests` exits 0.

## 5. Dual-runner unattended campaign launcher, sleep guard and clone-move recovery

An unattended campaign launched from Desktop shortcuts (`Recto Claude campaign` or `Recto Carbon campaign`) or `scripts/start-unattended-run.ps1` must run overnight without human intervention: the host must not idle-sleep mid-review, PowerShell UTF-8 output must not corrupt JSON hooks, Claude Code's OAuth token must resolve portably without leaking into Antigravity, Desktop shortcut icons must render without third-party packages and self-heal when the clone moves, and both runners must enforce the `SessionStart`, `PreToolUse` (`ask_question` blocked during active campaigns) and `Stop` (`scripts/campaign_guard.py`) lifecycle hooks.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Harden `scripts/start-unattended-run.ps1` across `-Runner claude` (`opus`) and `-Runner agy` (`gemini-4-carbon-medium` with `gemini-4-hydrogen-medium` helpers): UTF-8 console encoding, Win32 `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)` sleep prevention restored in `finally`, portable 1Password `op` / `CLAUDE_CODE_OAUTH_TOKEN` lookup scoped strictly to `claude`, git hook-path verification, Stop-hook self-test, and Conclave preflight check. Done when: `tests/scripts/test_start_unattended_run.py` passes across both runners.
2. Add pure-stdlib `scripts/campaign-icon.py` generating multi-size `.ico` badges into `%LOCALAPPDATA%\Recto` for `claude` and `agy`, and have `scripts/start-unattended-run.ps1 -InstallShortcut` rewrite both Desktop shortcuts cleanly after a clone move. Done when: `scripts/tests/test_campaign_icon.py` and `tests/scripts/test_start_unattended_run.py` verify valid `.ico` headers and moved-clone shortcut updates.
3. Wire `.claude/settings.json` and `.agents/hooks.json` to `scripts/ci_status.py --gate --brief`, `scripts/dispatch_hook.py` (blocking `ask_question` during active unattended campaigns while allowing attended confirmations) and `scripts/campaign_guard.py`. Done when: `tests/scripts/test_campaign_guard.py` and `tests/scripts/test_dispatch_hook.py` pass for both Claude Code and Antigravity payloads.

- [ ] Add Win32 `SetThreadExecutionState` sleep guard, UTF-8 `[Console]::InputEncoding`/`OutputEncoding`, `core.hooksPath` check, Stop-hook self-test, and runner-isolated `claude` token resolution to `scripts/start-unattended-run.ps1`. Done when: `python -m unittest tests.scripts.test_start_unattended_run` passes and verifies `agy` never receives `CLAUDE_CODE_OAUTH_TOKEN`. Cheaper substitute: leaving host power settings untouched, allowing Windows idle sleep to suspend overnight builds and reviews.
- [ ] Add pure-stdlib `scripts/campaign-icon.py` rendering multi-resolution `.ico` files (`16`, `24`, `32`, `48`, `64`, `128`, `256` px PNG frames) for `Recto Claude campaign` and `Recto Carbon campaign`, invoked by `scripts/start-unattended-run.ps1 -InstallShortcut`. Done when: `python -m unittest scripts.tests.test_campaign_icon` passes with zero Pillow/external dependencies. Cheaper substitute: requiring Pillow or shipping binary `.ico` blobs per runner path.
- [ ] Enforce unattended question-free execution and Stop-hook continuation across both `.claude/settings.json` and `.agents/hooks.json` (`scripts/dispatch_hook.py` refusing `ask_question` when `build/claude-campaign-guard.json` owns the session `conversationId`, and `scripts/campaign_guard.py` blocking premature stops). Done when: `python -m unittest tests.scripts.test_campaign_guard tests.scripts.test_dispatch_hook` exits 0. Cheaper substitute: blocking `ask_question` only in Claude Code via `--disallowedTools`, allowing an Antigravity campaign to stall waiting on user input.
- [ ] Commit: `unattended: harden dual-runner campaign launcher, sleep guard and shortcut icons`

**Test checkpoint:** `python -m unittest tests.scripts.test_start_unattended_run scripts.tests.test_campaign_icon tests.scripts.test_campaign_guard` exits 0, and `pwsh -NoProfile -File scripts/start-unattended-run.ps1 -Runner agy -PrintOnly` and `-Runner claude -PrintOnly` both exit 0.

## Verification

- `python scripts/review_pin.py --check`, `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` all exit 0, with `validate` completing in under 5 seconds across all historical and `three-pass` receipts.
- `python scripts/gen-compile-commands.py --check` reports `ok: 68 translation units`, and `python -m unittest tests.scripts.test_lsp` passes both C++23/Pyright and zero-dependency stdlib Python LSP modes with zero leaked processes.
- `python -m unittest tests.scripts.test_dispatch_hook` proves `gemini-4-hydrogen-medium` (`workspace: "inherit"`) enforcement, read-only tool filtering, `hydrogen_exec.py` `shell=False` isolation, and grandchild cleanup on cancellation.
- `python scripts/run-tests.py --quick` and `.\build.cmd -Quick -Project recto-pdf -SkipTests` pass locally within the memory floor while `.github/workflows/plan.yml` runs `python scripts/run-tests.py --full`.
- `python -m unittest tests.scripts.test_start_unattended_run scripts.tests.test_campaign_icon tests.scripts.test_campaign_guard scripts.tests.test_unattended` passes with zero third-party Python packages.
