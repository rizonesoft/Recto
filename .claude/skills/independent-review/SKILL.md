---
name: independent-review
description: Get an independent review of your own work from the reviewer pinned in .conclave/reviewers.toml (GPT-6 Astra, a different model family), through scripts/review-dispatch.py, then verify and act on every finding. Use before calling a nontrivial change done, before a commit that ships, or when a TODO section needs its review round.
---

# Independent review

Your own review is not enough for a nontrivial change: a different model reviews it. Claude Code (`claude`) and Google Antigravity on Gemini 4 Carbon (`agy`) are the writers in this repository (owner directions 2026-11-10, 2026-11-12 and 2026-11-13), and GPT-6 Astra, through the Codex CLI in its read-only sandbox, reviews every pass (owner decision 2026-10-28: the Astra and GPT-6.1 Terra panel was removed), whichever writer the dispatcher is given: the pin routes `claude`, `agy` (Antigravity) and `muse` (Muse Code) to Astra alone (owner decision 2026-11-10). The pin's `[schedule]` (`policy = "three-pass"`) sets each pass's lenses, conditional path lenses, and effort (`plan` `medium`, `round` `high` with conditional `security`/`performance` lenses, `completion` `medium`, `reconciliation` `high`, `blocking = ["P1", "P2"]`), with the round prompt's rule that a `P1` names a real path and its consequence. `scripts/review-dispatch.py` writes the review to the `--record` findings file and appends the writer's settled items (`- Filed:`, `- Not taken:`, `- Settled:` lines in that file) to every prompt.

| Work written by | Reviewers (every pass) | Fallback |
| --- | --- | --- |
| Claude Code (`claude`) | GPT-6 Astra (`codex`, at its scheduled effort) | none |
| Google Antigravity on Gemini 4 Carbon (`agy`) | GPT-6 Astra (`codex`, at its scheduled effort) | none |
| Muse Code (`muse`) | GPT-6 Astra (`codex`, at its scheduled effort) | none |

Pass `--writer` the agent that wrote the work. The pin refuses any other writer, an unknown reviewer CLI, a reviewer named twice or more than four, and a reviewer of the writer's own model family (a route of several reviewers, a panel, is still accepted, each writing its own findings file). For standalone reviews (a `--record` path not named for a TODO section, such as `docs/reviews/<topic>.md`), `scripts/review-dispatch.py --kind round` runs the round lenses with no pass cap and stops when the summary reports `"blocking": false` (`HIGHEST: P3` or `none`); an outage or incomplete reply (exit 1) is no review, so run the round again.

## Steps

1. Review your own change first and fix what you find.
2. Write the review prompt to a scratch file, not the repository, from `docs/review-round-prompt.md` (or fill it with `python scripts/review-prompt.py --kind round`): name the candidate commit or range, the governing documents, and the touched paths (`--paths <file> ...` when conditional lenses apply).
3. Run the round in the background (`run_in_background` in Claude Code; `run_command` with `WaitMsBeforeAsync: 500` and `IsDaemon: false` in Antigravity), passing `--writer claude` or `--writer agy`:

   ```powershell
   python scripts/review-dispatch.py --writer <claude|agy> [--paths <changed files>] --prompt <scratch>/review-prompt.md --record docs/reviews/<topic>.md
   ```

4. Read the review. Verify every finding against the code before acting: fix what is real, and record why anything else is not (`- Not taken: <item> (<reason>)` or `- Filed: DNN TNN §N (<item>)`).
5. Commit fixes as new commits and run another round on the new tip until `"blocking": false` (`HIGHEST: P3` or `none`), filing any remaining `P3` items through `add-todo`. A unit that needs patching three rounds running gets re-thought, not patched again.

## Rules

- The dispatcher's JSON output and the `## Round N` section it appends are the evidence. A verdict with no record is an opinion.
- Exit 1 means the reviewer returned no review (not installed, failed, timed out, empty or blocked), and nothing else runs. That is never an approval: say so, record the outage, and do not ship on it.
- A reviewer that could not reach the work (its sandbox could not read the repository) replies `OVERALL: blocked`, which is recorded as no review. If its sandbox keeps failing, put the diff in the prompt and run the round again.
- The reviewer runs read-only. Never pass it a secret, and never ask it to edit files.
- Change the reviewer only in `.conclave/reviewers.toml`, never by calling a model directly.
