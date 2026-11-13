# Gemini 4 Hydrogen Read-Only Helpers (`gemini-4-hydrogen-high`)

In Google Antigravity (`agy`), the main writer is **Gemini 4 Carbon** (`gemini-4-carbon-medium` in unattended campaigns, or Carbon High in attended sessions). To keep the Carbon writer's context lean and fast without losing a single bit of implementation quality, delegate purely mechanical, read-only "dummy work" to **Gemini 4 Hydrogen at High effort (`gemini-4-hydrogen-high`)** on the native swarm board.

## Why implementation quality stays at 100%

1. **Zero writing by Hydrogen:** `scripts/dispatch_hook.py` (wired in `.agents/hooks.json` on `PreToolUse`) mechanically denies `write_file`, `edit_file`, `ask_question`, subagent spawning, and every mutating shell, file, `git` or `todo-graph` command inside any `gemini-4-hydrogen*` turn.
2. **High effort enforced:** `scripts/dispatch_hook.py` refuses `swarm` `add` or `model` calls that name a Hydrogen model below `high` effort (`gemini-4-hydrogen-low` and `gemini-4-hydrogen-medium` are blocked) or a workspace other than `inherit`.
3. **Carbon owns every decision:** Gemini 4 Carbon writes every line of C++23, Python, SPEC and TODO text, diagnoses root causes, designs algorithms, verifies exit codes and `file:line` citations returned by helpers, and decides every review action.
4. **Zero idle cost:** Swarm workers in `waiting` state sleep on the board at zero token, memory and CPU cost, so a campaign session spawns only the helper it needs once and `@mention`s it on later sections.

## Spawning & Reusing Helpers

Spawn a helper with `swarm`:

- `action`: `"add"`
- `name`: `"code-explorer"` | `"git-runner"` | `"test-runner"` | `"debug-evidence"` | `"mutation-tester"`
- `model`: `"gemini-4-hydrogen-high"`
- `workspace`: `"inherit"`

### Role Briefs

- **`code-explorer`:** Read-only codebase locator. Use `lsp_symbols`, `lsp_references`, `lsp_definition`, `grep_search`, `find_by_name` and targeted `read_file` ranges to answer "where and how is X implemented". Return `<= 2 KB` of conclusions with exact `file:line` citations, never raw file dumps.
- **`git-runner`:** Read-only repository inspector. Run `git status`, `git log`, `git diff`, `git show`, `git blame`, `python scripts/ci_status.py` and `python scripts/todo-graph.py query|validate|resolve`. Return commit SHAs, file lists and exact status lines under `2 KB`.
- **`test-runner`:** Read-only check and test runner. Run targeted local checks (`python scripts/run-tests.py --quick`, `python -m unittest <module>`, `.\build.cmd -Quick -TestFilter <Suite.*>`), report exact command, exit code, pass/fail counts and failing assertion tails under `2 KB`, and never touch source or test files.
- **`debug-evidence`:** Read-only failure fact collector. Gather compiler diagnostics (`lsp_diagnostics`), crash stack traces, test failure logs and exact `file:line` call chains around a failing assertion, returning facts only so Gemini 4 Carbon performs the diagnosis and fix.
- **`mutation-tester`:** Read-only test-strength prover. Run `python scripts/mutate-check.py --file <relpath> --old <old> --new <new> -- <test command>` (which stages an isolated OS temp copy outside `D:\Recto`) and return the `KILLED` / `SURVIVED` table to Gemini 4 Carbon.
