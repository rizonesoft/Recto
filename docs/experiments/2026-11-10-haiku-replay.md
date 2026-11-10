# Haiku 5.5 replay for the test-runner and git-runner subagents

Run 2026-11-10 by the writer session (Fable 5.2). The question: do `test-runner` and `git-runner` lose anything when they move from Sonnet 5.5 to Haiku 5.5? Each agent was given the same prompt once on each model, on inputs whose answers were worked out first by running the commands directly. `scripts/verify-agent-tiers.mjs` requires this record before an agent may pin Haiku.

## test-runner

Prompt: run `python -m unittest tests.scripts.test_ci_status` in the repository, and `python -m unittest test_known` on a scratch suite written for the replay (6 tests: 3 pass, 2 fail, 1 raises), then report exit codes, counts and each failure's name and message.

| Known answer | Sonnet 5.5 | Haiku 5.5 |
| ------------ | ---------- | --------- |
| Suite 1: exit 0, 28 run, 0 failed | correct | correct |
| Suite 2: exit 1, 6 run, 2 failures, 1 error | correct | correct |
| `test_page_count`: `AssertionError: 3 != 4 : page count drifted` | exact | exact |
| `test_stride`: `AssertionError: 28 != 32` | exact | exact |
| `test_lookup`: `KeyError: 'missing-key'` | exact | exact |
| No file changed | yes | yes |

Cost: 20,050 tokens and 8.1 s on Sonnet, 20,014 tokens and 7.4 s on Haiku, at Haiku's lower price per token.

## git-runner

Prompt: four read-only questions about the history at `bed1b42`.

| Known answer | Sonnet 5.5 | Haiku 5.5 |
| ------------ | ---------- | --------- |
| `scripts/start-unattended-run.ps1` was added by `e6790ef` | correct | correct |
| 11 commits touch `scripts/campaign_guard.py` | correct | correct |
| `3da4496` changed `src/core/tile_scheduler.cpp` and `tests/recto-tests/scheduler_tests.cpp`, 55 insertions and 39 deletions | correct | correct |
| `claude-fable-5-2` entered `scripts/verify-agent-tiers.mjs` in `c79c299` | correct | correct |

Cost: 18,996 tokens and 7.2 s on Sonnet, 19,371 tokens and 8.2 s on Haiku.

## Decision

Haiku 5.5 lost nothing on either agent, on what the replay covers: running named commands and reporting exit codes, counts and failure text, and answering git questions with identifiers (SHAs, counts, file lists). It does not cover reading what a diff means.

Owner decision 2026-11-10: every subagent stays on Sonnet 5.5 for now, because the writer is due to change to Opus and the tiers will be set again then. Both agents were pinned to Haiku for one commit (`a4a35cd`) and moved back. A later move to Haiku starts from this record, costs the `model:`, `mechanical_share:` and `haiku_replay:` lines in the two agent files, and must first narrow `git-runner` to identifier answers, since its diff summaries were not replayed.
