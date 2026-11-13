---
schema_version: 1
id: agent-kits-and-campaign-runtime
domain: 00-workspace
status: active
title: "TODO-05 -- Conclave Kits, Portable LSP, Orchestrator Hooks and Campaign Cadence"
track: M0
---

# TODO-05 -- Conclave Kits, Portable LSP, Orchestrator Hooks and Campaign Cadence

> **Goal:** Recto's developer and campaign toolset runs self-contained across Claude Code (`claude`) and Google Antigravity (`agy`) with zero external Python package dependencies, importing Conclave-SDK's `reviewer`, `todo` and `unattended` kits without breaking historical receipts, serving low-RAM C++23 and Python language servers over LSP and MCP, enforcing Antigravity `PreToolUse` hook guards (`scripts/dispatch_hook.py`) and temp-copy mutation checks (`scripts/mutate-check.py`) aligned with Orchestrator (Carbon) (`gemini-pro-orchestrator`) with zero custom Antigravity subagents, balancing fast RAM-bounded local checks against heavy GitHub Actions CI runs, and running unattended campaigns reliably end to end.

> **Current state:** Measured 2026-11-13 (updated 2026-11-14). Recto holds 105 verified stamps across historical `converge`, panel and rotation pins; importing Conclave-SDK `kits/reviewer`, `kits/todo` and `kits/unattended` naively would invalidate historical receipts or drop Recto's pre-commit hooks, reopened-section cycle checks and `ci_status.py` watches, while `todo-graph.py validate` spawns per-receipt `git show` subprocesses (~50 s on Windows). Neither runner has a portable C++23 + Python LSP/MCP bridge with a zero-dependency fallback when `pyright-langserver` is absent. Antigravity runs on Orchestrator (Carbon) (`gemini-pro-orchestrator`), which pairs Carbon planning with Hydrogen execution automatically, so `scripts/dispatch_hook.py` must enforce background reviews and unattended `ask_question` blocking in `.agents/hooks.json` without restricting Hydrogen execution turns or carrying custom subagent definitions under `.agents/`, alongside temp-copy mutation testing in `scripts/mutate-check.py`. Running all 38 Python suites and full multi-platform MSBuild builds on every edit starves RAM and slows iteration instead of letting GitHub Actions carry the full matrix while `ci_status.py` watches `main`. `scripts/start-unattended-run.ps1` also needs Conclave-SDK `unattended` preflight/runner parity, Windows `SetThreadExecutionState` sleep prevention, UTF-8 console encoding, pure-stdlib shortcut icons, and Stop-hook self-testing.

## Inputs

- Owner directives (2026-11-10, 2026-11-12, 2026-11-13 and 2026-11-14) recorded in `AGENTS.md`: full writer autonomy, self-improvement and self-healing, 100% stdlib portability in `scripts/`, Conclave-SDK kit parity (`kits/reviewer`, `kits/todo`, `kits/unattended`), portable C++23 and Python LSP servers, Antigravity Orchestrator (Carbon) alignment with zero custom `.agents/agents/` subagents, balanced local-vs-CI cadence, and unattended campaign reliability.
- Foundation plan `docs/plans/2026-10-13-recto-foundation.md` D4 (toolchain pins) and D14 (C++23 across `Recto.sln`).
- `.conclave/reviewers.toml`, `.claude/settings.json`, `.agents/hooks.json`, `.agents/skills.json`, `.github/workflows/build.yml` and `.github/workflows/plan.yml`.
- -> XREF: D00 T02 §1 -- the strict TODO graph engine whose `validate` path `§1` speeds up and extends with Conclave-SDK kit parity
- -> XREF: D00 T04 §3 -- MSBuild compiler parallelism; `§4` adds free-RAM `/m:<N>`, `/nr:false` and `build/restore.stamp` ahead of `§3`'s `/MP` pass
- -> XREF: D00 T04 §5 -- C++ build gate tiers; `§4` pairs them with `build.cmd -Quick` and `scripts/run-tests.py --quick` vs `--full`
- -> XREF: D00 T04 §9 -- CI watch after push that `§4` and `§5` wire across both Claude Code and Google Antigravity
- -> XREF: D00 T04 §12 -- MSBuild item-evaluated compile database and indexing benchmark that build on `§2`'s portable LSP servers
- -> XREF: D00 T04 §14 -- scheduled task clone-move check paired with `§5`'s unattended launcher and shortcut self-healing

## Outcome

- Conclave-SDK `reviewer`, `todo` and `unattended` kits (`three-pass` review schedule, conditional path lenses, `review-prompt.py`, `unattended.json`, `unattended-preflight.py`, `unattended-run.py`) are active on `main`, all 105 historical review receipts validate cleanly, and `build\python.cmd scripts/todo-graph.py validate` finishes in ~2.2 s via batch `git cat-file --batch`.
- Portable C++23 (`scripts/clangd-lsp.py` + `scripts/gen-compile-commands.py` + `.clangd`) and Python (`scripts/python-lsp.py` with zero-dependency stdlib AST/C3-MRO/UTF-16/PEP-263 fallback) language servers and `scripts/lsp_mcp.py` serve diagnostics, symbols, definitions, references and hover in both Claude Code and Google Antigravity with 5-minute idle auto-shutdown and zero errors across all 68 C++23 translation units.
- Google Antigravity (`agy`) runs on Orchestrator (Carbon) (`gemini-pro-orchestrator`) with zero custom subagent definitions under `.agents/`, while `scripts/dispatch_hook.py` (`.agents/hooks.json`, `PreToolUse`) enforces background `review-dispatch.py` launches and unattended `ask_question` blocking without rewriting or blocking Hydrogen execution turns, and `scripts/mutate-check.py` runs isolated temp-copy mutation checks.
- Local iteration stays fast and RAM-bounded (`.\build.cmd -Quick`, `build\python.cmd scripts/run-tests.py --quick`, Win32 Job Object / POSIX lifeline process-tree containment in `scripts/lib/proc_group.py`) while `.github/workflows/build.yml` and `.github/workflows/plan.yml` (`--full`) carry the full matrix on GitHub Actions, watched by `scripts/ci_status.py`.
- `scripts/start-unattended-run.ps1` launches unattended campaigns in both `-Runner claude` (`opus`) and `-Runner agy` (`gemini-pro-orchestrator`, Orchestrator (Carbon)) with Windows sleep prevention, UTF-8 encoding, portable token lookup, pure-stdlib shortcut icons (`scripts/campaign-icon.py`), moved-clone shortcut repair, and Stop-hook self-testing.

**Adjacency:** all=not-applicable (developer workspace, agent runner hooks, language servers and CI cadence; no user-facing surface, document or setting)

**Adjacency rationale:** Every script, hook, language server and workflow in this file governs repository development, review dispatch, local/CI test cadence and unattended agent execution; none ships in `Recto.exe` or touches end-user PDF documents or user settings.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Conclave-SDK reviewer, TODO and unattended kit import with receipt parity | -- | [ ] |
| 2 | §2 | Portable C++23 and Python LSP servers and MCP bridge across runners | §1 | [ ] |
| 3 | §3 | Antigravity PreToolUse hook guards, Orchestrator alignment and mutation runner | §1 | [ ] |
| 4 | §4 | RAM-bounded build/test runner cadence and local-vs-CI gate split | §1 | [ ] |
| 5 | §5 | Dual-runner unattended campaign launcher, sleep guard and clone-move recovery | §1, §3, §4 | [ ] |

## 1. Conclave-SDK reviewer, TODO and unattended kit import with receipt parity

Recto's review dispatcher, TODO graph engine and unattended runner predated Conclave-SDK's `three-pass` review schedule (`plan`, `round`, `completion`, conditional `reconciliation`), path-scoped conditional lenses (`security`, `performance`) and structured unattended preflight/runner scripts. This section imports `kits/reviewer`, `kits/todo` and `kits/unattended` into Recto's stdlib-only `scripts/` and `.conclave/` configuration without breaking any of Recto's 105 historical review receipts, reopened-section cycle checks, pre-commit hook or `ci_status.py` gates, and replaces per-receipt `git show` subprocesses in `scripts/lib/todo_graph.py` with a persistent `git cat-file --batch` reader.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Import Conclave-SDK `kits/reviewer` (`scripts/review_pin.py`, `scripts/review-prompt.py`, `scripts/review-dispatch.py`, `.conclave/reviewers.toml` `three-pass` schedule and `docs/*-review-prompt.md`). Done when: `build\python.cmd scripts/review_pin.py --check` exits 0 and `scripts/tests/test_review_dispatch.py` passes.
2. Merge Conclave-SDK `kits/todo` `three-pass` stamp/receipt validation into `scripts/lib/todo_graph.py` and `scripts/todo-graph.py` while keeping historical `converge`, panel and rotation receipts valid and batching git blob reads through `git cat-file --batch`. Done when: `build\python.cmd scripts/todo-graph.py validate` and `plan --check` exit 0 in under 5 seconds across all 105 historical receipts and `tests.scripts.test_todo_graph` passes.
3. Import Conclave-SDK `kits/unattended` (`.conclave/unattended.json`, `scripts/unattended-preflight.py`, `scripts/unattended-run.py`, `docs/unattended-prompt.md`) adapted to Recto's dual-runner layout and zero external dependencies. Done when: `build\python.cmd -m unittest scripts.tests.test_preflight scripts.tests.test_unattended` passes.

- [ ] Upgrade `.conclave/reviewers.toml`, `scripts/review_pin.py`, `scripts/review-prompt.py`, `scripts/review-dispatch.py` and `docs/{plan,review-round,completion,reconciliation}-review-prompt.md` to Conclave-SDK `kits/reviewer` `three-pass` schedule with `--writer claude|agy|muse` routing to GPT-6 Astra and conditional `security`/`performance` path lenses. Done when: `build\python.cmd scripts/review_pin.py --check` exits 0 and `build\python.cmd -m unittest scripts.tests.test_review_dispatch` passes. Cheaper substitute: overwriting `scripts/review-dispatch.py` verbatim without `agy` writer routing or historical rotation replay, which breaks `validate` on earlier stamps.
- [ ] Extend `scripts/lib/todo_graph.py` and `scripts/todo-graph.py` to validate both `three-pass` receipts and Recto's 105 historical `converge`/panel/rotation receipts, preserving reopened-section `-cycle<N>` checks and replacing per-receipt `git show` spawns with a persistent `git cat-file --batch` reader. Done when: `build\python.cmd scripts/todo-graph.py validate` exits 0 in under 5 seconds on the live tree and `build\python.cmd -m unittest tests.scripts.test_todo_graph` passes. Cheaper substitute: skipping historical receipt pin replay, which silently weakens receipt verification.
- [ ] Add `.conclave/unattended.json`, `scripts/unattended-preflight.py`, `scripts/unattended-run.py` and `docs/unattended-prompt.md` from Conclave-SDK `kits/unattended`, wired to Recto's `todo-graph.py`, `ci_status.py` and `campaign_guard.py` with 100% Python stdlib. Done when: `build\python.cmd -m unittest scripts.tests.test_unattended` passes with zero third-party imports. Cheaper substitute: hardcoding machine paths in `.conclave/unattended.json`, which breaks clone portability.
- [ ] Synchronize `.claude/skills/{process-todo-section,review-todo-section,independent-review,process-phase,process-plan}/SKILL.md` and `.agents/skills.json` with the `three-pass` schedule and dual-runner `claude`/`agy` commands. Done when: `build\python.cmd -m unittest scripts.tests.test_three_pass tests.scripts.test_verify_agent_tiers` passes.
- [ ] Commit: `kits: import Conclave-SDK reviewer, todo and unattended kits with receipt parity`

**Test checkpoint:** `build\python.cmd scripts/review_pin.py --check`, `build\python.cmd scripts/todo-graph.py validate`, and `build\python.cmd -m unittest tests.scripts.test_todo_graph scripts.tests.test_review_dispatch scripts.tests.test_unattended` all exit 0.

## 2. Portable C++23 and Python LSP servers and MCP bridge across runners

Searching and reading multi-hundred-kilobyte C++23 and Python files by text wastes context and misses type errors until build time, while developer hosts may lack `pyright-langserver` or move clones across directories. This section adds portable C++23 (`scripts/gen-compile-commands.py`, `.clangd`, `scripts/clangd-lsp.py`) and Python (`scripts/python-lsp.py` with dynamic `pyright-langserver` discovery plus a zero-dependency stdlib AST, C3-MRO, UTF-16 and PEP-263 fallback) language servers and a low-RAM MCP stdio bridge (`scripts/lsp_mcp.py` with 5-minute idle shutdown), wired into Claude Code (`.lsp.json`, `.mcp.json`) and Google Antigravity (`.agents/plugins/recto-lsp/`), and resolves incomplete-`PdfValue` C++23 member instantiation in `src/pdf/object.h` so `clangd` checks all 68 translation units cleanly.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Generate portable `build/compile_commands.json` from `Recto.sln` and `.vcxproj` files via `scripts/gen-compile-commands.py`, configure `.clangd`, fix `src/pdf/object.h` C++23 `PdfValue` completeness, and wrap `clangd` in `scripts/clangd-lsp.py`. Done when: `build\python.cmd scripts/gen-compile-commands.py --check` exits 0 across all 68 translation units and `clangd --check=src/pdf/object.cpp` reports 0 errors.
2. Build `scripts/python-lsp.py` supporting `pyright-langserver` (including spaced Windows `.cmd` shim paths under `lib.proc_group`) and a full zero-dependency stdlib AST/symtable/tokenize/C3-MRO/UTF-16 fallback. Done when: `tests/scripts/test_lsp.py` proves diagnostics, symbols, definitions, references and hover in both Pyright and stdlib-fallback modes, including Fibonacci-DAG MRO memoization under 0.5 s.
3. Expose `lsp_diagnostics`, `lsp_symbols`, `lsp_definition`, `lsp_references` and `lsp_hover` over stdio MCP (`scripts/lsp_mcp.py`) with fail-closed `publishDiagnostics` version matching and 5-minute idle reaping, wired in `.lsp.json`, `.mcp.json` and `.agents/plugins/recto-lsp/`. Done when: `build\python.cmd -m unittest tests.scripts.test_lsp` passes cleanly with zero leaked child processes.

- [ ] Add `scripts/gen-compile-commands.py`, `.clangd` and `scripts/clangd-lsp.py` discovering Visual Studio / LLVM `clangd` and MSVC + Windows SDK + vcpkg + NuGet includes dynamically with `-j=2` and `--pch-storage=disk`, and make `PdfArray`/`PdfDict` hold complete `PdfValue` elements safely in `src/pdf/object.h`. Done when: `build\python.cmd scripts/gen-compile-commands.py --check` prints `ok: 68 translation units` and `clangd --check=src/pdf/object.cpp` exits 0 with 0 errors. Cheaper substitute: suppressing `clangd` template diagnostics in `.clangd`, which hides real C++23 standard-library requirements.
- [ ] Add `scripts/python-lsp.py` launching `pyright-langserver` directly via `lib.proc_group.spawn_managed_process` when installed (preserving spaced Windows `.cmd` shim paths) and falling back automatically to a pure-stdlib Python LSP server (`textDocument/publishDiagnostics`, `documentSymbol`, `workspace/symbol`, `definition`, `references`, `hover`, UTF-16 code units, PEP-263 encodings and memoized C3 MRO). Done when: `tests/scripts/test_lsp.py` passes both spaced-shim Pyright launch and pure-stdlib fallback suites. Cheaper substitute: failing when `pyright-langserver` is not installed globally, violating self-contained repo portability.
- [ ] Add `scripts/lsp_mcp.py` implementing MCP stdio (`Content-Length` and newline JSON-RPC, 16 MiB frame bound, fail-closed version-matched diagnostics and 5-minute idle shutdown) and wire `.lsp.json`, `.mcp.json` and `.agents/plugins/recto-lsp/{plugin.json,mcp_config.json}` with relative paths only. Done when: `build\python.cmd -m unittest tests.scripts.test_lsp` passes and `git grep -i "Users\\\\"` finds zero hardcoded user paths in tracked configs. Cheaper substitute: keeping language servers resident indefinitely, wasting gigabytes of RAM while MSBuild runs.
- [ ] Commit: `lsp: add portable C++23 and Python LSP servers and low-RAM MCP bridge`

**Test checkpoint:** `build\python.cmd scripts/gen-compile-commands.py --check` prints `ok: 68 translation units`, and `build\python.cmd -m unittest tests.scripts.test_lsp` exits 0.

## 3. Antigravity PreToolUse hook guards, Orchestrator alignment and mutation runner

In Google Antigravity (`agy`), Orchestrator (Carbon) (`gemini-pro-orchestrator`) pairs Gemini 4 Carbon planning and review with Gemini 4 Hydrogen execution automatically in one session, so custom Antigravity subagent definitions under `.agents/agents/` and Hydrogen read-only sandboxes are both redundant and conflicting with Hydrogen's built-in execution turns. This section aligns `.agents/hooks.json` and `scripts/dispatch_hook.py` (`PreToolUse`, `matcher: "*"`) to enforce tracked background `review-dispatch.py` launches and active-campaign `ask_question` blocking while leaving Orchestrator planning and execution turns unrestricted, forbids `.agents/agents/` kit drift in `tests/scripts/test_todo_graph.py`, and provides `scripts/mutate-check.py` for isolated temp-copy mutation testing without dirtying the checkout.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Enforce Antigravity `PreToolUse` background-review (`WaitMsBeforeAsync: 500`, `IsDaemon: false`, non-detached) and active-campaign `ask_question` guards in `scripts/dispatch_hook.py` (`.agents/hooks.json`, `matcher: "*"`) while allowing Orchestrator (Carbon) and Hydrogen execution turns to edit files and run commands unmodified. Done when: `tests/scripts/test_dispatch_hook.py` proves foreground/daemon/detached reviews and campaign-owned `ask_question` calls are denied while writes, edits and commands pass with no `overwrite` across Carbon, Hydrogen and Orchestrator turns.
2. Build `scripts/mutate-check.py` staging mutations in an isolated temporary copy outside the live working tree with `lib.proc_group` process-tree timeout reaping and refusing uncompiled C++ source mutations against pre-built binaries. Done when: `tests/scripts/test_dispatch_hook.py` proves `OUTCOME: KILLED` leaves the live repository byte-identical, uncompiled `.cpp` mutations exit 2, and timed-out grandchild trees are reaped.
3. Guard `.agents/` against custom subagent or duplicated skill trees (`.codex`, `.agents/skills`, `.agents/agents`) in `tests/scripts/test_todo_graph.py` and document Orchestrator (Carbon) in `AGENTS.md`, `docs/agent-skills-and-rules-structure.md` and `.claude/skills/independent-review/SKILL.md`. Done when: `tests/scripts/test_todo_graph.py` and `tests/scripts/test_bootstrap_tools.py` pass with zero custom Antigravity subagent artifacts.

- [ ] Wire Antigravity `PreToolUse` JSON decision handling in `scripts/dispatch_hook.py` and `.agents/hooks.json` to refuse foreground, daemon or detached `review-dispatch.py` launches and session-matched `ask_question` calls during active campaigns while leaving Orchestrator (Carbon) and Hydrogen execution turns unrestricted. Done when: `DispatchHook.test_antigravity_background_review_is_allowed_and_foreground_daemon_or_detached_is_denied`, `test_antigravity_ask_question_is_denied_only_for_the_session_owning_a_live_campaign_guard` and `test_orchestrator_carbon_and_hydrogen_turns_allow_writes_and_commands_unmodified` pass. Cheaper substitute: blocking or rewriting `"hydrogen"` model turns in `PreToolUse`, which breaks Orchestrator (Carbon)'s built-in execution model.
- [ ] Implement `scripts/mutate-check.py` running mutations in a clean temporary copy under `lib.proc_group` timeout containment and rejecting native C++ mutations without a compile step. Done when: `test_mutate_check_runs_in_temp_copy_without_touching_live_tree` and `test_mutate_check_timeout_kills_descendant_process_tree` pass. Cheaper substitute: mutating files directly in the live working tree and restoring on exit, which dirties the checkout if interrupted.
- [ ] Assert `.agents/agents` absence alongside `.codex` and `.agents/skills` in `tests/scripts/test_todo_graph.py` and record Orchestrator (Carbon) (`gemini-pro-orchestrator`) across `AGENTS.md`, `docs/agent-skills-and-rules-structure.md` and `.claude/skills/independent-review/SKILL.md`. Done when: `build\python.cmd -m unittest tests.scripts.test_todo_graph tests.scripts.test_bootstrap_tools` exits 0. Cheaper substitute: leaving stale custom subagent docs under `.agents/`, inviting future drift against Orchestrator.
- [ ] Commit: `hooks: wire Antigravity PreToolUse guards, Orchestrator alignment and mutate-check`

**Test checkpoint:** `build\python.cmd -m unittest tests.scripts.test_dispatch_hook tests.scripts.test_todo_graph` passes in full.

## 4. RAM-bounded build/test runner cadence and local-vs-CI gate split

Running every heavy Python suite and full multi-platform MSBuild build on each local edit turns 90% of campaign wall time into redundant local test execution and exhausts host RAM, whereas skipping local checks altogether risks pushing broken commits. This section establishes the balanced local-vs-CI cadence: `.\build.cmd -Quick` (`build/build.ps1` with free-RAM `/m:<N>`, `/nr:false` and verified `build/restore.stamp`) and `build\python.cmd scripts/run-tests.py --quick` (skipping heavy unchanged suites locally while scaling workers from available physical memory and bounding child process trees through `scripts/lib/proc_group.py`), paired with `.github/workflows/plan.yml` (`--full`) and `.github/workflows/build.yml` carrying the full matrix in GitHub Actions under `scripts/ci_status.py` monitoring.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Build `scripts/lib/proc_group.py` (Win32 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` suspended-spawn Job Objects and POSIX `/proc` + `_RECTO_MANAGED_GROUP` + kernel-EOF lifeline reaper) and wire it into `scripts/run-tests.py`, `scripts/mutate-check.py` and `scripts/review-dispatch.py`. Done when: hung or interrupted test workers and cross-session grandchildren are terminated cleanly without pipe deadlocks in `tests/scripts/test_run_tests.py` and `scripts/tests/test_review_dispatch.py`.
2. Add `scripts/run-tests.py` discovering both `tests/scripts` and `scripts/tests`, scaling worker count from free RAM, running `--quick` by default locally (deferring heavy suites unless their watched paths have uncommitted or unpushed changes relative to `origin/main`) and `--full` in `.github/workflows/plan.yml`. Done when: `build\python.cmd scripts/run-tests.py --quick` passes locally and `.github/workflows/plan.yml` runs `--full`, `todo-graph.py validate`, `plan --check` and `verify-agent-tiers.mjs` as explicit steps.
3. Add `-Quick`, `-Project`, free-RAM MSBuild `/m:<N>` + `/nr:false`, and input-hashed `build/restore.stamp` to `build.cmd` and `build/build.ps1`. Done when: `.\build.cmd -Quick -Project recto-pdf -SkipTests` builds `recto-pdf` x64 Debug with `/nr:false` and skips redundant NuGet/vcpkg restores when `restore.stamp` matches `project.assets.json`, verified by `tests/scripts/test_build_ps1.py`.

- [ ] Implement `scripts/lib/proc_group.py` providing lifetime-bound Win32 Job Objects and POSIX cross-session `/proc` + lifeline-reaper process-tree containment, and wire it into `scripts/run-tests.py`, `scripts/mutate-check.py` and `scripts/review-dispatch.py`. Done when: `build\python.cmd -m unittest tests.scripts.test_run_tests` passes all timeout, `KeyboardInterrupt` and orphan-grandchild reaping tests. Cheaper substitute: `Popen.kill()` on the immediate child PID alone, which leaks grandchildren holding stdout pipes open.
- [ ] Add `scripts/run-tests.py` (`--quick`, `--full`, `--filter`, `--jobs`, `--timeout`, `--list`) with RAM-bounded worker scaling (`_available_ram_mib` on Windows, Linux and macOS) and git-diff input detection against `origin/main`, and wire `scripts/run-tests.py --full` into `.github/workflows/plan.yml`. Done when: `tests/scripts/test_run_tests.py` verifies `--quick` skips heavy suites only when clean and runs them as soon as watched inputs change, and `plan.yml` runs `--full`. Cheaper substitute: dropping heavy suites from local `--quick` even when `scripts/todo-graph.py` or `scripts/ci_status.py` was edited, hiding local regressions.
- [ ] Add `-Quick`, `-Project <name>`, free-RAM `/m:<N>` + `/nr:false` node-reuse disabling, and `build/restore.stamp` verification to `build.cmd` and `build/build.ps1`, and record the local-vs-CI cadence in `AGENTS.md`. Done when: `build\python.cmd -m unittest tests.scripts.test_build_ps1` passes and `.\build.cmd -Quick -Project recto-pdf -SkipTests` exits 0. Cheaper substitute: leaving `/nr:true` default MSBuild node reuse enabled, keeping idle `MSBuild.exe` workers holding memory after builds finish.
- [ ] Commit: `build: add RAM-bounded build/test cadence and managed process-tree cleanup`

**Test checkpoint:** `build\python.cmd -m unittest tests.scripts.test_run_tests tests.scripts.test_build_ps1` passes, and `.\build.cmd -Quick -Project recto-pdf -SkipTests` exits 0.

## 5. Dual-runner unattended campaign launcher, sleep guard and clone-move recovery

An unattended campaign launched from Desktop shortcuts (`Recto Claude campaign` or `Recto Carbon campaign`) or `scripts/start-unattended-run.ps1` must run overnight without human intervention: the host must not idle-sleep mid-review, PowerShell UTF-8 output must not corrupt JSON hooks, Claude Code's OAuth token must resolve portably without leaking into Antigravity, Desktop shortcut icons must render without third-party packages and self-heal when the clone moves, and both runners must enforce the `SessionStart`, `PreToolUse` (`ask_question` blocked during active campaigns) and `Stop` (`scripts/campaign_guard.py`) lifecycle hooks.

**Phase:** 0
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Harden `scripts/start-unattended-run.ps1` across `-Runner claude` (`opus`) and `-Runner agy` (`gemini-pro-orchestrator`, Orchestrator (Carbon)): UTF-8 console encoding, Win32 `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)` sleep prevention restored in `finally`, portable 1Password `op` / `CLAUDE_CODE_OAUTH_TOKEN` lookup scoped strictly to `claude`, git hook-path verification, Stop-hook self-test, and Conclave preflight check. Done when: `tests/scripts/test_start_unattended_run.py` passes across both runners.
2. Add pure-stdlib `scripts/campaign-icon.py` generating multi-size `.ico` badges into `%LOCALAPPDATA%\Recto` for `claude` and `agy`, and have `scripts/start-unattended-run.ps1 -InstallShortcut` rewrite both Desktop shortcuts cleanly after a clone move. Done when: `scripts/tests/test_campaign_icon.py` and `tests/scripts/test_start_unattended_run.py` verify valid `.ico` headers and moved-clone shortcut updates.
3. Wire `.claude/settings.json` and `.agents/hooks.json` to `scripts/ci_status.py --gate --brief`, `scripts/dispatch_hook.py` (blocking `ask_question` during active unattended campaigns while allowing attended confirmations) and `scripts/campaign_guard.py`. Done when: `tests/scripts/test_campaign_guard.py` and `tests/scripts/test_dispatch_hook.py` pass for both Claude Code and Antigravity payloads.

- [ ] Add Win32 `SetThreadExecutionState` sleep guard, UTF-8 `[Console]::InputEncoding`/`OutputEncoding`, `core.hooksPath` check, Stop-hook self-test, and runner-isolated `claude` token resolution to `scripts/start-unattended-run.ps1`. Done when: `build\python.cmd -m unittest tests.scripts.test_start_unattended_run` passes and verifies `agy` never receives `CLAUDE_CODE_OAUTH_TOKEN`. Cheaper substitute: leaving host power settings untouched, allowing Windows idle sleep to suspend overnight builds and reviews.
- [ ] Add pure-stdlib `scripts/campaign-icon.py` rendering multi-resolution `.ico` files (`16`, `24`, `32`, `48`, `64`, `128`, `256` px PNG frames) for `Recto Claude campaign` and `Recto Carbon campaign`, invoked by `scripts/start-unattended-run.ps1 -InstallShortcut`. Done when: `build\python.cmd -m unittest scripts.tests.test_campaign_icon` passes with zero Pillow/external dependencies. Cheaper substitute: requiring Pillow or shipping binary `.ico` blobs per runner path.
- [ ] Enforce unattended question-free execution and Stop-hook continuation across both `.claude/settings.json` and `.agents/hooks.json` (`scripts/dispatch_hook.py` refusing `ask_question` when `build/claude-campaign-guard.json` owns the session `conversationId`, and `scripts/campaign_guard.py` blocking premature stops). Done when: `build\python.cmd -m unittest tests.scripts.test_campaign_guard tests.scripts.test_dispatch_hook` exits 0. Cheaper substitute: blocking `ask_question` only in Claude Code via `--disallowedTools`, allowing an Antigravity campaign to stall waiting on user input.
- [ ] Commit: `unattended: harden dual-runner campaign launcher, sleep guard and shortcut icons`

**Test checkpoint:** `build\python.cmd -m unittest tests.scripts.test_start_unattended_run scripts.tests.test_campaign_icon tests.scripts.test_campaign_guard` exits 0, and `pwsh -NoProfile -File scripts/start-unattended-run.ps1 -Runner agy -PrintOnly` and `-Runner claude -PrintOnly` both exit 0.

## Verification

- `build\python.cmd scripts/review_pin.py --check`, `build\python.cmd scripts/todo-graph.py validate` and `build\python.cmd scripts/todo-graph.py plan --check` all exit 0, with `validate` completing in under 5 seconds across all historical and `three-pass` receipts.
- `build\python.cmd scripts/gen-compile-commands.py --check` reports `ok: 68 translation units`, and `build\python.cmd -m unittest tests.scripts.test_lsp` passes both C++23/Pyright and zero-dependency stdlib Python LSP modes with zero leaked processes.
- `build\python.cmd -m unittest tests.scripts.test_dispatch_hook` proves Antigravity background review enforcement, campaign-scoped `ask_question` blocking, unrestricted Orchestrator (Carbon) / Hydrogen execution turns, and `mutate-check.py` temp-copy isolation and timeout reaping.
- `build\python.cmd scripts/run-tests.py --quick` and `.\build.cmd -Quick -Project recto-pdf -SkipTests` pass locally within the memory floor while `.github/workflows/plan.yml` runs `scripts/run-tests.py --full`.
- `build\python.cmd -m unittest tests.scripts.test_start_unattended_run scripts.tests.test_campaign_icon tests.scripts.test_campaign_guard scripts.tests.test_unattended` passes with zero third-party Python packages.
