# Reviewer A/B experiment: GPT-6.1 Terra against GPT-6 Astra

Owner request 2026-10-27. Status: running. Review date: 2026-10-27.

## What is being compared?

| Arm | Reviewer | Pin | Plan | Completion | Round 1 | Round 2 | Round 3 | Rotation | Escalation |
| --- | -------- | --- | ---- | ---------- | ------- | ------- | ------- | -------- | ---------- |
| A | GPT-6.1 Terra (`gpt-6.1-terra`, Codex CLI) | `.conclave/experiments/reviewer-ab-terra.toml` | xhigh | xhigh | max | xhigh | high | high | max |
| B | GPT-6 Astra (`gpt-6-astra`, Codex CLI) | `.conclave/reviewers.toml` (live) | medium | medium | high | medium | low | low | medium |

Lenses, prompts, the stop rule (`severity_stop_after = 0`) and the read-only sandbox are identical, so model and effort are the only variables. Arm B stays the live pin: its records are the ones every stamp is checked against, so the experiment never changes what a stamp means. Arm A records live in `docs/experiments/reviewer-ab/`.

## How is it run?

- Sections: at least three, taken in plan order from `python scripts/todo-graph.py query ready`, each built through the normal lifecycle (`process-todo-section`, then `review-todo-section`).
- Paired, lockstep passes: every pass (plan review, completion review, each round) gives both arms the same candidate commit and the same prompt, launched at the same time, so section difficulty and machine load are shared.
- Blind: neither arm sees the other's findings. The writer reads both replies before fixing anything.
- Verification: the writer checks every finding against the code and the sources and records a verdict: `valid` (a real defect, fixed or filed), `invalid` (a false positive, with the reason), or `duplicate` (the same arm repeating an earlier finding). Findings both arms raised about the same defect are matched across arms.
- Stop rule: a section's loop ends when both arms' latest round on the same candidate declares no P1 or P2. An arm that declares a stop while the other still finds a valid P1 or P2 on that candidate is charged a missed severe finding.
- Speed: `scripts/review-dispatch.py` records each pass's reviewer wall-clock time on a `- Seconds:` line (added for this experiment, 2026-10-27).

## What is measured?

- Findings: count per pass and per section, by severity.
- Accuracy: precision (valid / all findings raised), recall against the union of valid findings from both arms, and missed severe findings (a valid P1 or P2 only the other arm raised, including on a candidate this arm called clean).
- Severity agreement: on matched findings, whether both arms gave the same severity.
- Speed: seconds per pass, seconds per section, and rounds to the section's stop.
- Outages: passes that returned no review (timeouts, blocked replies), which count against speed and are not reviews.

## Sections

| Section | Arm A record | Arm B record |
| ------- | ------------ | ------------ |

## Pass log

| Section | Pass | Candidate | Arm | Effort | Seconds | Findings (P1/P2/P3) | Valid | Invalid | Duplicate |
| ------- | ---- | --------- | --- | ------ | ------- | ------------------- | ----- | ------- | --------- |

## Finding verdicts

| Section | Pass | Arm | # | Severity | Verdict | Matched | Note |
| ------- | ---- | --- | - | -------- | ------- | ------- | ---- |

## Results

Pending: filled in when the third section stamps.

## Limits

- Three sections is a small sample: results show direction, not significance.
- Both arms run at the same time on one machine, so absolute seconds include shared local load; the comparison between arms is fair, single numbers are not benchmarks.
- The writer's fixes after a pass change what the next pass sees, so rounds after round 1 compare arms on the same candidate, not on independent histories.
- Verdicts are the writer's judgment against the code and sources, recorded with reasons so they can be audited.
