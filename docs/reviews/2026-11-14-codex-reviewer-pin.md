# Codex reviewer pin and plan-gates repair

The owner authorised Codex as a writer in AGENTS.md on 2026-11-14. No active campaign guard was present. This maintenance change was made on main without pushing.

The Codex route remains `codex -> carbon`, now `agy` with `gemini-pro-orchestrator`. Other routes, review lenses, P1/P2 blocking and the 33-pass cap are unchanged. Removing all four shared scheduled medium-effort overrides also makes Astra use its CLI configuration. Its entry-level high effort is unchanged and applies only without a schedule.

CI run 43630010008 failed on the previous commit in three tests. The Codex icon expectation contradicted the installed knot mark; the seeded Windows preflight omitted the Python launcher; the template test rejected the now-authorised Codex wrapper. After correcting those expectations and the fixture, the template test exposed the existing 24000-byte AGENTS.md limit. Reviewer documentation was condensed to fit. The three original tests failed locally before the repairs; the targeted 11-test run passes afterward.

Pin validation: `build\python.cmd scripts/review_pin.py --check --all` and `build\python.cmd scripts/review-dispatch.py --check-pin --json` passed. The TODO graph (882 sections), generated plan check and campaign guard self-test passed.

Full Python verification: `bosun run -- .\build\python.cmd scripts/run-tests.py --full` passed 38 of 39 modules; its sole failure read AGENTS.md before the final shortening. The affected module was rerun with `bosun run -- .\build\python.cmd -m unittest tests.scripts.test_todo_graph`: all 137 tests passed. Remote CI was not rerun because the owner requested no push.

Live attempt: `build\python.cmd scripts/review-dispatch.py --writer codex --kind round --prompt build/carbon-pin-review-prompt.md --record build/carbon-pin-live-review.md --timeout 180`. It exited 1 after 61 seconds with no review. No approval or section stamp is claimed. The dispatcher-generated record follows.

## Round 1: none

- Writer: codex
- Run: 2026-11-14T19:47:23Z
- Lenses: adversarial, integration, source-defect, consistency, quality, optimization
- Seconds: 61
- Skipped: carbon (returned no review)
- Lines: 1
- Digest: 0555d87060386853

No reviewer returned a review. This round is not an approval.
