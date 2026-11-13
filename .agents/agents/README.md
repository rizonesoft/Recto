# Gemini 4 Hydrogen Read-Only Helpers (`gemini-4-hydrogen-medium`)

In Google Antigravity (`agy`), the main writer is **Gemini 4 Carbon** (`gemini-4-carbon-medium` in unattended campaigns, or Carbon High in attended sessions). To keep the Carbon writer's context lean and fast without losing a single bit of implementation quality, delegate purely mechanical, read-only "dummy work" to **Gemini 4 Hydrogen at Medium effort (`gemini-4-hydrogen-medium`)** on the native swarm board.

## Why implementation quality stays at 100%

1. **Zero writing by Hydrogen:** `scripts/dispatch_hook.py` (wired in `.agents/hooks.json` on `PreToolUse` with `matcher: "*"`) enforces a strict positive allowlist of read-only tools and literal read-only commands inside any `gemini-4-hydrogen*` turn, and rewrites every allowed `run_command` via `overwrite` to `scripts/hydrogen_exec.py --payload <hex>` so execution runs `shell=False` without PowerShell evaluation and stages test runs in an isolated temporary directory outside the checkout.
2. **Medium effort enforced:** `scripts/dispatch_hook.py` refuses `swarm` `add` or `model` calls that name a Hydrogen model other than `medium` effort (`gemini-4-hydrogen-low`, `gemini-4-hydrogen-high` and `gemini-4-hydrogen-xhigh` are blocked) or a workspace other than `inherit`.
3. **Carbon owns every decision:** Gemini 4 Carbon writes every line of C++23, Python, SPEC and TODO text, diagnoses root causes, designs algorithms, verifies exit codes and `file:line` citations returned by helpers, and decides every review action.
4. **Zero idle cost:** Swarm workers in `waiting` state sleep on the board at zero token, memory and CPU cost, so a campaign session spawns only the helper it needs once and `@mention`s it on later sections.

## Spawning & Reusing Helpers

Spawn a helper with `swarm`:

- `action`: `"add"`
- `name`: `"code-explorer"` | `"git-runner"` | `"test-runner"` | `"debug-evidence"`
- `model`: `"gemini-4-hydrogen-medium"`
- `workspace`: `"inherit"`

### Role Briefs

- **`code-explorer`:** Read-only codebase locator. Use `lsp_symbols`, `lsp_references`, `lsp_definition`, `grep_search`, `find_by_name` and targeted `read_file` ranges to answer "where and how is X implemented". Return `<= 2 KB` of conclusions with exact `file:line` citations, never raw file dumps.
- **`git-runner`:** Read-only repository inspector. Run `git status`, `git log`, `git diff`, `git show`, `git blame`, `python scripts/ci_status.py` and `python scripts/todo-graph.py query|validate|resolve`. Return commit SHAs, file lists and exact status lines under `2 KB`.
- **`test-runner`:** Read-only check and test runner. Run targeted checks (`python scripts/run-tests.py --quick`, `python -m unittest <module>`, `build\bin\x64\Debug\recto-tests\recto-tests.exe --gtest_filter=<Suite.*>`), report exact command, exit code, pass/fail counts and failing assertion tails under `2 KB`, and leave all C++ compilation (`.\build.cmd`), mutation checks (`scripts/mutate-check.py`), and edits to Gemini 4 Carbon.
- **`debug-evidence`:** Read-only failure fact collector. Gather compiler diagnostics (`lsp_diagnostics`), crash stack traces, test failure logs and exact `file:line` call chains around a failing assertion, returning facts only so Gemini 4 Carbon performs the diagnosis and fix.
