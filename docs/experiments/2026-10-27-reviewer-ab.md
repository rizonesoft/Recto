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
- Verification: the writer checks every finding against the code and the sources and records a verdict: `valid` (a real defect, fixed or filed), `invalid` (a false positive, with the reason), `duplicate` (the same arm repeating an earlier finding), or `unverified` (the source could not be checked; excluded from precision, and not acted on). Findings both arms raised about the same defect are matched across arms.
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
| D01 T02 §33 | completion | `e4d9920` | B | medium | 210 | 9 gaps (untagged) | 8 | 0 | 1 |
| D01 T02 §33 | round 1 | `6be1930` | A | max | 502 | 12/0/0 | 10 | 1 | 1 |
| D01 T02 §33 | round 1 | `6be1930` | B | high | 390 | 6/0/0 | 5 | 0 | 0 (1 unverified) |
| D01 T02 §33 | round 2 | `40eafec` | A | xhigh | 104 | 3/0/0 | 2 | 0 | 0 (1 unverified) |
| D01 T02 §33 | round 2 | `40eafec` | B | medium | 104 | 1/0/0 | 1 | 0 | 0 |

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
| D01 T02 §33 | completion | B | 1a | in-scope | valid | A round 1 src1 | "Order 3 is Order 1 below 4 samples" (Table 39). First judged unsupported; corroborated in round 1 by Terra independently and by a third-party reader's issue quoting Table 39. Fixed in `da69517`. Verdict revised 2026-10-28. |
| D01 T02 §33 | completion | B | 1b | in-scope | valid | A2 | Spline silently linear past 65,536 samples. |
| D01 T02 §33 | completion | B | 2 | in-scope | valid | A1 | Interpolation discontinuity, with a worked example. |
| D01 T02 §33 | completion | B | 3 | in-scope | valid | A4 | Minimum signed integer. |
| D01 T02 §33 | completion | B | 4 | in-scope | valid | none | No cumulative memory bound: one 16 MiB table named 255 times asks for about 4 GiB from a small file. The most severe finding of the pass (P1 by reach); Terra missed it. Fixed. |
| D01 T02 §33 | completion | B | 5 | in-scope | valid | none | Shared atomic test counter against the Freeze check. Fixed (thread-local). |
| D01 T02 §33 | completion | B | 6 | out-of-scope | valid | A5 | `§4` consumers. |
| D01 T02 §33 | completion | B | 7 | in-scope | valid | A8, A7 | Multi-input cubic and goldens. |
| D01 T02 §33 | completion | B | 8 | in-scope | valid | A9 | Timings against Acrobat; generic. |
| D01 T02 §33 | completion | B | 9 | out-of-scope | duplicate | B6 | Repeats its own gap 6. |
| D01 T02 §33 | round 1 | A | adv 1 | P1 | valid | none | DeviceN of 7 or more tints overran a 6-element stack array in `numbers()`: memory corruption from a valid file. The most severe defect of the section so far; Astra missed it, and so did the writer's 6-colorant test. Fixed. |
| D01 T02 §33 | round 1 | A | adv 2 | P1 | valid | B 1 | Trimmed sample vectors kept the decoded buffer's capacity, uncharged. Fixed. |
| D01 T02 §33 | round 1 | A | adv 3 | P1 | valid | B 3 | Calculator programs not charged to any budget. Fixed. |
| D01 T02 §33 | round 1 | A | adv 4 | P1 | valid | none | `/Colorants` resolved before checking for Separation, its bound counting only accepted entries: fan-out hang. P2 by reach. Fixed. |
| D01 T02 §33 | round 1 | A | adv 5 | P1 | valid | B 2 | A huge finite `/Encode` span overflowed to NaN, then an index. Fixed. |
| D01 T02 §33 | round 1 | A | int 1 | P1 | valid | none | Function-array members clipped to the first member's domain. P2 by reach. Fixed. |
| D01 T02 §33 | round 1 | A | src 1 | P1 | valid | B completion 1a | Order 3 below 4 samples. Fixed. |
| D01 T02 §33 | round 1 | A | src 2 | P1 | valid | A completion 8 | Multi-input cubic: already filed as `§41`; a repeat of a filed gap. |
| D01 T02 §33 | round 1 | A | src 3 | P1 | invalid | none | "Refuse a type 2 whose domain breaks 7.10.3": a conformance rule for writers; the reader evaluates the defined points and fails only the undefined one, which is the recorded design. Not taken. |
| D01 T02 §33 | round 1 | A | pd 1 | P1 | duplicate | A adv 1 | Same overrun. |
| D01 T02 §33 | round 1 | A | pd 2 | P1 | valid | none | Function cache capped at 256, so a repeated reference past it was charged again. P3 by reach (a safe refusal). Fixed. |
| D01 T02 §33 | round 1 | A | pd 3 | P1 | valid | B 6 | A cached subtree skipped the depth check. Fixed. |
| D01 T02 §33 | round 1 | B | 1 | P1 | valid | A adv 2 | Retained capacity, with a 4 GiB worked example. |
| D01 T02 §33 | round 1 | B | 2 | P1 | valid | A adv 5 | NaN index. |
| D01 T02 §33 | round 1 | B | 3 | P1 | valid | A adv 3 | Calculator budget. |
| D01 T02 §33 | round 1 | B | 4 | P1 | unverified | none | "Surplus calculator operands must be an error": the ISO text could not be checked here (the spec PDF is past the fetch limit); readers commonly take the top n. Not taken; excluded from precision. |
| D01 T02 §33 | round 1 | B | 5 | P1 | valid | none | `/All` must ignore its alternate and tint transform (8.6.6.4); the section had specified the opposite. Fixed and the contract corrected. |
| D01 T02 §33 | round 1 | B | 6 | P1 | valid | A pd 3 | Cached subtree depth. |
| D01 T02 §33 | round 2 | A | 1 | P1 | valid | none | Indexed lookups kept a decoded stream's capacity (1,024 spaces over a 1 MiB stream hold 1 GiB). Same class as round 1's sample tables, missed by both arms then. Fixed in `d09e3e9`. |
| D01 T02 §33 | round 2 | A | 2 | P1 | valid | A completion 6 | Tint evaluations across 100,000 paints have no page work budget: already filed to `§19`; a repeat of a filed gap. |
| D01 T02 §33 | round 2 | A | 3 | P1 | unverified | B round 1 4 | Surplus calculator operands as an error: the same unverified claim Astra made in round 1; two models agreeing is not a source. Not taken. |
| D01 T02 §33 | round 2 | B | 1 | P1 | valid | none | `/Colorants` Separation entries whose alternate is a DeviceN with more `/Colorants`: 64^4 resolutions from a small file. Round 1's fan-out fix had not closed it. Fixed with a per-call step bound (`ColorSpaceSteps`) in `d09e3e9`. |

## Results

Final results are written when the third section stamps. Running tally after `D01 T02 §33` round 2 (plan, completion, rounds 1 and 2):

| Measure | A: Terra | B: Astra |
| ------- | -------- | -------- |
| Findings raised | 31 | 20 |
| Valid | 28 | 18 |
| Invalid | 1 | 0 |
| Duplicate (own) | 1 | 1 |
| Unverified | 1 | 1 |
| Precision (valid / valid + invalid) | 97% | 100% |
| Valid findings the other arm never raised | 11 | 5 |
| Most severe unique find | DeviceN stack overrun (memory corruption) | 4 GiB function-table request (no memory budget) |
| Seconds: plan, completion, round 1, round 2 | 77, 131, 502, 104 | 103, 210, 390, 104 |
| Severity calibration | tags every round finding P1 | tags every round finding P1 |

## Limits

- Three sections is a small sample: results show direction, not significance.
- Both arms run at the same time on one machine, so absolute seconds include shared local load; the comparison between arms is fair, single numbers are not benchmarks.
- The writer's fixes after a pass change what the next pass sees, so rounds after round 1 compare arms on the same candidate, not on independent histories.
- Verdicts are the writer's judgment against the code and sources, recorded with reasons so they can be audited.
