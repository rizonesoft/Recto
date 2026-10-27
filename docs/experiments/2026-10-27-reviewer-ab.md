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
| D01 T02 §33 | `docs/experiments/reviewer-ab/D01-T02-s33.md` | `docs/reviews/01-engine/D01-T02-s33.md` |

## Pass log

| Section | Pass | Candidate | Arm | Effort | Seconds | Findings (P1/P2/P3) | Valid | Invalid | Duplicate |
| ------- | ---- | --------- | --- | ------ | ------- | ------------------- | ----- | ------- | --------- |
| D01 T02 §33 | plan | `b7f73ca` | A | xhigh | 77 | 3/3/0 | 6 | 0 | 0 |
| D01 T02 §33 | plan | `b7f73ca` | B | medium | 103 | 2/2/0 | 4 | 0 | 0 |
| D01 T02 §33 | completion | `e4d9920` | A | xhigh | 131 | 10 gaps (untagged) | 10 | 0 | 0 |
| D01 T02 §33 | completion | `e4d9920` | B | medium | 210 | 9 gaps (untagged) | 7 | 1 | 1 |

## Finding verdicts

| Section | Pass | Arm | # | Severity | Verdict | Matched | Note |
| ------- | ---- | --- | - | -------- | ------- | ------- | ---- |
| D01 T02 §33 | plan | A | 1 | P1 | valid | none | `/Order 3` drawn linearly; ISO 7.10.2 specifies cubic spline. Real, but P2 by reach (a rendering nuance). Planned: natural spline for one input. |
| D01 T02 §33 | plan | A | 2 | P1 | valid | none | `/None` had no rule for images or Indexed over a `/None` base. Real, rare in practice (P2 by reach). |
| D01 T02 §33 | plan | A | 3 | P1 | valid | B1 | Interpolation checked only at grid points; a nearest-sample build would pass. |
| D01 T02 §33 | plan | A | 4 | P2 | valid | B4 | Checkpoint omitted `DeviceColors`; also the conversion memory compared only 4 components (sharper than B4). |
| D01 T02 §33 | plan | A | 5 | P2 | valid | B2 (part) | Clipping, defaults, function arrays and calculator fallbacks unasserted. |
| D01 T02 §33 | plan | A | 6 | P2 | valid | none | `§4` had no failing test for `/None` text and stencil masks. |
| D01 T02 §33 | plan | B | 1 | P1 | valid | A3 | Same as A3, adds domain and range clipping. |
| D01 T02 §33 | plan | B | 2 | P1 | valid | A5 (part) | Calculator division by zero, domain errors, non-finite results unspecified. Names failure classes A5 did not. |
| D01 T02 §33 | plan | B | 3 | P2 | valid | none | `/Range` optional for types 2 and 3; the section implied it was always clipped. |
| D01 T02 §33 | plan | B | 4 | P2 | valid | A4 | Checkpoint omitted `DeviceColors` and `DefaultColorSpaces`. |
| D01 T02 §33 | completion | A | 1 | in-scope | valid | B2 | Multilinear or simplex chosen by fractional count: a colour jump at a sample boundary. Fixed. |
| D01 T02 §33 | completion | A | 2 | in-scope | valid | B1b | One-input spline silently linear past 65,536 samples. Fixed with a recorded bound. |
| D01 T02 §33 | completion | A | 3 | in-scope | valid | none | `/Order` other than 1 or 3 accepted silently. Fixed with a diagnostic. |
| D01 T02 §33 | completion | A | 4 | in-scope | valid | B3 | -2147483648 read as a real. Fixed. |
| D01 T02 §33 | completion | A | 5 | out-of-scope | valid | B6 | Text, image and Form consumers: already owned by `§4`. |
| D01 T02 §33 | completion | A | 6 | out-of-scope | valid | none | No work budget inside colour conversion for a long calculator over a big image. Filed to `§19`. |
| D01 T02 §33 | completion | A | 7 | out-of-scope | valid | B7 (part) | Spot colour goldens. Filed to D06 T01 §1. |
| D01 T02 §33 | completion | A | 8 | in-scope | valid | B7 | Multi-input `/Order 3` drawn linearly. Filed as `§41` (the section had recorded it as a default). |
| D01 T02 §33 | completion | A | 9 | out-of-scope | valid | B8 | Timings against Acrobat. Filed to D06 T01 §7; generic, low value. |
| D01 T02 §33 | completion | A | 10 | out-of-scope | valid | none | Spot plate proofing: already planned in D04 T06 §11. |
| D01 T02 §33 | completion | B | 1a | in-scope | invalid | none | "Order 3 must be ignored below 4 samples": no source found (ISO 32000-1 Table 39 names Order 1 and 3 only). |
| D01 T02 §33 | completion | B | 1b | in-scope | valid | A2 | Spline silently linear past 65,536 samples. |
| D01 T02 §33 | completion | B | 2 | in-scope | valid | A1 | Interpolation discontinuity, with a worked example. |
| D01 T02 §33 | completion | B | 3 | in-scope | valid | A4 | Minimum signed integer. |
| D01 T02 §33 | completion | B | 4 | in-scope | valid | none | No cumulative memory bound: one 16 MiB table named 255 times asks for about 4 GiB from a small file. The most severe finding of the pass (P1 by reach); Terra missed it. Fixed. |
| D01 T02 §33 | completion | B | 5 | in-scope | valid | none | Shared atomic test counter against the Freeze check. Fixed (thread-local). |
| D01 T02 §33 | completion | B | 6 | out-of-scope | valid | A5 | `§4` consumers. |
| D01 T02 §33 | completion | B | 7 | in-scope | valid | A8, A7 | Multi-input cubic and goldens. |
| D01 T02 §33 | completion | B | 8 | in-scope | valid | A9 | Timings against Acrobat; generic. |
| D01 T02 §33 | completion | B | 9 | out-of-scope | duplicate | B6 | Repeats its own gap 6. |

## Results

Pending: filled in when the third section stamps.

## Limits

- Three sections is a small sample: results show direction, not significance.
- Both arms run at the same time on one machine, so absolute seconds include shared local load; the comparison between arms is fair, single numbers are not benchmarks.
- The writer's fixes after a pass change what the next pass sees, so rounds after round 1 compare arms on the same candidate, not on independent histories.
- Verdicts are the writer's judgment against the code and sources, recorded with reasons so they can be audited.
