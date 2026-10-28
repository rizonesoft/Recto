---
name: independent-review
description: Get an independent review of your own work from the reviewer panel pinned in .conclave/reviewers.toml (GPT-6 Astra and GPT-6.1 Terra, a different model family, both on every pass), through scripts/review-dispatch.py, then verify and act on every finding. Use before calling a nontrivial change done, before a commit that ships, or when a TODO section needs its review round.
---

# Independent review

Your own review is not enough for a nontrivial change: a different model reviews it. Opus 5.5 (writer `claude`) is the only writer in this repository, and a panel of GPT-6 Astra and GPT-6.1 Terra, through the Codex CLI in its read-only sandbox, reviews every pass of the same candidate at once (owner decision 2026-10-28). The pin's `[schedule]` sets each pass's lenses for both and each reviewer's effort (Astra: plan and completion `medium`, rounds `high`, `medium`, `low`, then `low` with one lens per round; Terra, through `[schedule.efforts.terra]`: `xhigh`, `xhigh`, `max`, `xhigh`, `high`, then `high`), with the round prompt's rule that a `P1` names a real path and its consequence. `scripts/review-dispatch.py` runs both and writes each reviewer's review to its own findings file (the `--record` file for Astra, `<stem>.terra.md` beside it for Terra), and appends the writer's settled items (`- Filed:`, `- Not taken:`, `- Settled:` lines in those files) to every prompt.

| Work written by | Reviewers (every pass) | Fallback |
| --- | --- | --- |
| Opus 5.5 (`claude`) | GPT-6 Astra and GPT-6.1 Terra (`codex`, each at its scheduled effort) | none |

The pin refuses any other writer, an unknown reviewer CLI, a reviewer named twice or more than four, and a reviewer of the writer's own model family. A loop ends on the first candidate where neither reviewer finds a `P1` or `P2` (the dispatcher's summary says `panel_clean`); an outage of either is no review, so run the round again.

## Steps

1. Review your own change first and fix what you find.
2. Write the review prompt to a scratch file, not the repository, from `docs/review-round-prompt.md`: name the candidate commit or range, the governing documents, and the lenses that apply.
3. Run the round:

   ```bash
   python3 scripts/review-dispatch.py --writer claude --prompt <scratch>/review-prompt.md --record docs/reviews/<topic>.md
   ```

   On Windows use `python` instead of `python3`; the command is otherwise the same.
4. Read the review. Verify every finding against the code before acting: fix what is real, and record why anything else is not.
5. Commit fixes as new commits and run another round on the new tip, up to 33 rounds; the first round with no `P1` or `P2` finding (all `P3` or advisory) ends the loop and its findings are filed as follow-up work (owner decision 2026-10-24; it was from round 4 on). A unit that needs patching three rounds running gets re-thought, not patched again.

## Rules

- The dispatcher's JSON output and the `## Round N` section it appends are the evidence. A verdict with no record is an opinion.
- Exit 1 means the reviewer returned no review (not installed, failed, timed out, empty or blocked), and nothing else runs. That is never an approval: say so, record the outage, and do not ship on it.
- A reviewer that could not reach the work (its sandbox could not read the repository) replies `OVERALL: blocked`, which is recorded as no review. If its sandbox keeps failing, put the diff in the prompt and run the round again.
- The reviewer runs read-only. Never pass it a secret, and never ask it to edit files.
- Change the reviewer only in `.conclave/reviewers.toml`, never by calling a model directly.
