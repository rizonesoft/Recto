---
name: independent-review
description: Get an independent review of your own work from GPT-6 Astra, the one reviewer pinned in .conclave/reviewers.toml (a different model family, no fallback), through scripts/review-dispatch.py, then verify and act on every finding. Use before calling a nontrivial change done, before a commit that ships, or when a TODO section needs its review round.
---

# Independent review

Your own review is not enough for a nontrivial change: a different model reviews it. Opus 5.5 (writer `claude`) is the only writer in this repository and GPT-6 Astra, through the Codex CLI in its read-only sandbox, is the only reviewer, and the pin's `[schedule]` sets each pass's effort and lenses (plan and completion reviews at `medium`, round 1 `high`, round 2 `medium`, round 3 `low`, then `low` with one lens per round; owner decisions 2026-10-23, with a proportionality rule in the round and completion prompts: only failures that can happen in real operation are `P1` or `P2`, contrived scenarios are `P3`). `.conclave/reviewers.toml` pins that one route, and `scripts/review-dispatch.py` runs the round.

| Work written by | Reviewer | Fallback |
| --- | --- | --- |
| Opus 5.5 (`claude`) | GPT-6 Astra (`codex`, scheduled effort) | none |

The pin refuses any other writer, any other reviewer CLI, a second reviewer in a route, and a reviewer of the writer's own model family.

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
