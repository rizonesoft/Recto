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

No loss on either agent, so both pin `claude-haiku-5-5`. What the replay covers is what the agents now claim: running named commands and reporting exit codes, counts and failure text, and answering git questions with identifiers (SHAs, counts, file lists). It does not cover reading what a diff means, so `git-runner` no longer offers that: the main session reads a diff itself. The main session checks what these agents return against the exit code or the identifier, which is the condition Haiku runs under.

`code-explorer`, `debug-evidence` and `mutation-tester` stay on Sonnet 5.5: each reads code or evidence and chooses what matters, which is not 90 percent mechanical.

There is no Opus subagent. Opus 5.5 sits below the writer (Fable 5.2), so anything too hard for Sonnet (diagnosis, design, a verdict) stays in the main session and gains nothing from a weaker model at a higher price than Sonnet. Reversing this costs one agent file with a `model_rationale:` line.

Reversing the Haiku pins costs the `model:`, `mechanical_share:` and `haiku_replay:` lines in the two agent files.
