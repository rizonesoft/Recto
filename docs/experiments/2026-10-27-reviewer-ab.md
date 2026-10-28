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
| D01 T02 §33 | `docs/reviews/01-engine/D01-T02-s33.terra.md` (moved from `docs/experiments/reviewer-ab/` when the panel went live) | `docs/reviews/01-engine/D01-T02-s33.md` |

## Pass log

| Section | Pass | Candidate | Arm | Effort | Seconds | Findings (P1/P2/P3) | Valid | Invalid | Duplicate |
| ------- | ---- | --------- | --- | ------ | ------- | ------------------- | ----- | ------- | --------- |
| D01 T02 §33 | plan | `b7f73ca` | A | xhigh | 77 | 3/3/0 | 6 | 0 | 0 |
| D01 T02 §33 | plan | `b7f73ca` | B | medium | 103 | 2/2/0 | 4 | 0 | 0 |
| D01 T02 §33 | completion | `e4d9920` | A | xhigh | 131 | 10 gaps (untagged) | 10 | 0 | 0 |
| D01 T02 §33 | completion | `e4d9920` | B | medium | 210 | 9 gaps (untagged) | 8 | 0 | 1 |
| D01 T02 §33 | round 1 | `6be1930` | A | max | 502 | 12/0/0 | 10 | 1 | 1 |
| D01 T02 §33 | round 1 | `6be1930` | B | high | 390 | 6/0/0 | 5 | 1 | 0 |
| D01 T02 §33 | round 2 | `40eafec` | A | xhigh | 104 | 3/0/0 | 2 | 1 | 0 |
| D01 T02 §33 | round 2 | `40eafec` | B | medium | 104 | 1/0/0 | 1 | 0 | 0 |
| D01 T02 §33 | round 3 | `25c5891` | A | high | 62 | 2/0/0 | 2 | 0 | 0 |
| D01 T02 §33 | round 3 | `25c5891` | B | low | 89 | 1/0/0 | 1 | 0 | 0 |
| D01 T02 §33 | round 4 (escalated) | `cdf0338` | A | max | 410 | 5/0/0 | 4 | 1 | 0 |
| D01 T02 §33 | round 4 (escalated) | `cdf0338` | B | medium | 104 | outage: Codex exited 1, no review | - | - | - |
| D01 T02 §33 | round 4 retry (recorded as Round 5) | `cdf0338` | B | medium | 147 | 1/1/0 | 2 | 0 | 0 |
| D01 T02 §33 | panel round 5 (Astra Round 6) | `001c43e` | A | high | 39 | 0/0/0 | 0 | 0 | 0 |
| D01 T02 §33 | panel round 5 (Astra Round 6) | `001c43e` | B | low | 79 | 0/0/2 | 2 | 0 | 0 |

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
| D01 T02 §33 | round 1 | B | 4 | P1 | invalid | none | "Surplus calculator operands must be an error": pdfium's `CPDF_PSFunc::v_Call` refuses only too few and pops the top n, Recto's behaviour. First recorded unverified; revised 2026-10-28 with that evidence. |
| D01 T02 §33 | round 1 | B | 5 | P1 | valid | none | `/All` must ignore its alternate and tint transform (8.6.6.4); the section had specified the opposite. Fixed and the contract corrected. |
| D01 T02 §33 | round 1 | B | 6 | P1 | valid | A pd 3 | Cached subtree depth. |
| D01 T02 §33 | round 2 | A | 1 | P1 | valid | none | Indexed lookups kept a decoded stream's capacity (1,024 spaces over a 1 MiB stream hold 1 GiB). Same class as round 1's sample tables, missed by both arms then. Fixed in `d09e3e9`. |
| D01 T02 §33 | round 2 | A | 2 | P1 | valid | A completion 6 | Tint evaluations across 100,000 paints have no page work budget: already filed to `§19`; a repeat of a filed gap. |
| D01 T02 §33 | round 2 | A | 3 | P1 | invalid | B round 1 4 | Surplus calculator operands: pdfium reads the top n as Recto does (revised 2026-10-28). |
| D01 T02 §33 | round 2 | B | 1 | P1 | valid | none | `/Colorants` Separation entries whose alternate is a DeviceN with more `/Colorants`: 64^4 resolutions from a small file. Round 1's fan-out fix had not closed it. Fixed with a per-call step bound (`ColorSpaceSteps`) in `d09e3e9`. |
| D01 T02 §33 | round 3 | A | 1 | P1 | valid | A completion 6 | Tint work with no page budget: the third raising of the gap filed to `§19`. |
| D01 T02 §33 | round 3 | A | 2 | P1 | valid | none | A huge direct lookup string copied whole before truncation. P3 by reach (transient). Fixed. |
| D01 T02 §33 | round 3 | B | 1 | P1 | valid | none | Interpolation overflowed with finite spans (`(x - x0) * span` before dividing) and silently drew sample 0. Fixed. |
| D01 T02 §33 | round 4 | A | 1 | P1 | valid | none | Colorant names copied per alias with no length bound (1 MiB names x 1,024 aliases). P2 by reach. Fixed with Annex C.2's 127-byte limit in `1168650`. |
| D01 T02 §33 | round 4 | A | 2 | P1 | valid | B round 5 1 | `/Process /Components` resolved every entry past the 32 kept (100,000 references to a 1 MiB string). Fixed. |
| D01 T02 §33 | round 4 | A | 3 | P1 | valid | A completion 6 | Tint work with no page budget: the fourth raising of the gap filed to `§19`. |
| D01 T02 §33 | round 4 | A | 4 | P1 | valid | B round 5 2 | Spline derivatives overflow with a huge `/Decode`, NaN reported as success. Fixed. |
| D01 T02 §33 | round 4 | A | 5 | P1 | invalid | B round 1 4 | Surplus calculator operands: pdfium's `CPDF_PSFunc::v_Call` refuses too few and pops the top n, as Recto does. |
| D01 T02 §33 | round 5 | B | 1 | P1 | valid | A round 4 2 | `/Process /Components` work. |
| D01 T02 §33 | round 5 | B | 2 | P2 | valid | A round 4 4 | Spline NaN; Astra tagged it P2, the only non-P1 round finding so far. |
| D01 T02 §33 | panel round 5 | B | 1 | P3 | valid | none | The `cvr` test gives a real input, so a no-op `cvr` passes. Implemented after the stamp. |
| D01 T02 §33 | panel round 5 | B | 2 | P3 | valid | none | The shared-table colour test passes vacuously when no path is drawn. Implemented after the stamp. |

## Results

Final results are written when the third section stamps. Tally for `D01 T02 §33` when the experiment stopped (plan, completion, rounds 1 to 4; owner decision 2026-10-28 below):

| Measure | A: Terra | B: Astra |
| ------- | -------- | -------- |
| Findings raised | 38 | 23 |
| Valid | 34 | 21 |
| Invalid | 3 | 1 |
| Duplicate (own) | 1 | 1 |
| Unverified | 0 | 0 |
| Precision (valid / valid + invalid) | 92% | 95% |
| Valid findings the other arm never raised | 13 | 6 |
| Most severe unique find | DeviceN stack overrun (memory corruption) | 4 GiB function-table request (no memory budget) |
| Seconds: plan, completion, rounds 1 to 4 | 77, 131, 502, 104, 62, 410 | 103, 210, 390, 104, 89, 147 (plus a 104-second outage) |
| Severity calibration | tags every round finding P1 | tags all but one round finding P1 |
| Relative cost (owner, 2026-10-28) | about one tenth of Astra | 1 |
| Outages | 0 | 1 |

**Owner decision 2026-10-28:** keep both reviewers on every pass (a panel), the loop ending only when neither finds a P1 or P2 on the same candidate, with two prompt changes: settled items (filed or not taken, with reasons) are given to every reviewer so they do not block the stop, and P1 must be justified with a real path. The panel is wired into `scripts/review-dispatch.py` and the stamp; `D01 T02 §33` resumes its loop under it. The experiment's remaining sections are replaced by a one-section trial of two further reviewers (below).

## Trial: Gemini 4 Argon and Muse Wonder 1.0 (owner request 2026-10-28)

Each reviewed `D01 T02 §33` once, as round 1 with all four round-1 lenses, at the round 1 candidate (`6be1930`) and with the round prompt template of that commit, so the result compares directly with Terra's and Astra's round 1 and with every defect the later rounds proved present at that commit. They ran from a worktree of `6be1930` through the panel dispatcher with `.conclave/experiments/trial-gemini-muse.toml`; their replies are `docs/experiments/trial/D01-T02-s33.gemini.md` and `docs/experiments/trial/D01-T02-s33.muse.md`.

| Reviewer | CLI | Effort | Seconds | Distinct findings | Valid | Invalid | Own duplicates | Known defects found (of 17) | New defects |
| -------- | --- | ------ | ------- | ----------------- | ----- | ------- | -------------- | --------------------------- | ----------- |
| GPT-6.1 Terra (round 1) | codex | max | 502 | 11 | 10 | 1 | 1 | 9 | - |
| GPT-6 Astra (round 1) | codex | high | 390 | 6 | 5 | 1 | 0 | 5 | - |
| Gemini 4 Argon | agy | medium | 339 | 3 | 3 | 0 | 1 | 2 | 1 (P2) |
| Muse Wonder 1.0 | muse | high | 359 | 3 | 3 | 0 | 6 | 1 | 2 (P3) |

The 17 known defects are the valid code defects the panel's rounds 1 to 5 found present at `6be1930` (the DeviceN overrun, retained sample buffers, unbudgeted calculators, `/Colorants` fan-out, a NaN index from a huge `/Encode`, function-array clipping, Order 3 below 4 samples, `/All`, the shared-subtree depth bypass, the function cache cap, retained Indexed lookups, `/Colorants` through DeviceN alternates, interpolation overflow, a full lookup-string copy, unbounded colorant names, `/Process /Components` work and spline NaN).

| Reviewer | # | Severity given | Verdict | Matched | Note |
| -------- | - | -------------- | ------- | ------- | ---- |
| Gemini | 1 | P1 | valid | Terra round 1 adv 1 | DeviceN overrun (raised again under plan-delivery: an own duplicate). |
| Gemini | 2 | P2 | valid, new | none | A tint transform that fails, or comes after the table budget is spent, is decoded again by every space naming it: up to 16 MiB of decompression per selection. Missed by Terra and Astra in five rounds. Fixed in `3bbe0f8` (failed functions remembered, a 256 MiB decode budget per page). |
| Gemini | 3 | P2 | valid | Terra and Astra round 1 | NaN from a span too wide for a double, then an undefined float-to-integer cast. |
| Muse | 1 | P1 | valid | Terra round 1 adv 1 | DeviceN overrun (raised again under integration and plan-delivery). |
| Muse | 2 | P2 | valid, new | none | Caches keyed by object number alone: a reference to a generation the file does not hold reused another generation's parsed function. P3 by reach. Fixed in `3bbe0f8`, the `§32` ICC cache included. |
| Muse | 3 | P2 | valid, new | none | A failing piece of a stitching function had its failure values overwritten by the stitching function's. P3 by reach. Fixed in `3bbe0f8`. |

What the trial shows: both found the section's worst defect (the overrun), neither raised a false finding, and between them they found three defects the panel had not, one of them (Gemini's repeated decoding) a real P2. Against the panel's round 1 they found fewer of the known defects (Gemini 2, Muse 1, against Terra 9 and Astra 5). Gemini's reply carried its working notes ahead of the formatted review (318 lines), and Muse repeated each finding under every lens it touched (6 duplicates), both costing the writer reading time. A single pass is not a basis for a panel decision; their cost was not measured.

## Panel log (owner request 2026-10-28)

The panel's data is kept per section from here, so the review effort can be judged on more than `D01 T02 §33`: rounds to the loop's end, each reviewer's seconds and findings, and how many held up. **Owner decision 2026-10-28:** Gemini 4 Argon and Muse Wonder 1.0 are not added; the panel stays Astra and Terra.

| Section | Rounds to the end | Astra seconds (all passes) | Terra seconds (all passes) | Astra findings (valid) | Terra findings (valid) | Ended on |
| ------- | ----------------- | -------------------------- | -------------------------- | ---------------------- | ---------------------- | -------- |
| D01 T02 §33 | 5 (plus an outage) | 1,226 | 1,325 | 25 (23) | 38 (34) | `001c43e`: Astra P3 only, Terra clean |
| D01 T02 §42 | 5 | 1,060 | 1,211 | 19 (19) | 35 (34) | `53044b5`: both P3 only |
| D01 T01 §11 | 3 | 845 | 882 | 11 (11) | 19 (19) | `4fcef18`: both clean |

D01 T02 §42 notes: findings count the plan review's gaps, the completion review's gap list and every round's findings; Terra's one invalid finding is round 1 finding 8 (an empty string recording no run, not taken). Only Astra found the embedded program left out of the font budget (round 2), `/Flags` converted unchecked and the missing vertical spacing tests (completion). Only Terra found the Standard 14 box with `/Widths`, the zero Type 3 box, decoder limits taken as recoverable, `/Differences` retention (twice), the glyph vector's byte bound, uncharged decode work, the scanned name cache, the per-string fallback and the uncounted CFF tables. Both found the font memory budget, indirect encodings, the indirect `gs` size, unknown subtypes and fallback allocation. Terra's round 4 escalation at `max` (409 s) found four P1s after Astra's round 3 and 4 approvals.

D01 T01 §11 notes: both found the Debug allocation in `heap_bytes` and the noexcept cache constructors; only Astra found the uncharged hash buckets first (Terra later too) and the descendant's per-object parse count; only Terra found the per-tier totals, the doorkeeper's false admissions, the remaining descendant copies, the crafted-collision probing and the copied `/DecodeParms`.

**Owner decision 2026-10-28 (end of the panel):** the reviewer system was more than the work needed: Terra is removed, GPT-6 Astra reviews alone with every effort one level higher (plan and completion `high`, rounds `xhigh`, `high`, `medium`, rotation `medium`, escalation `high`), and the experiment pins, the 2026-10-22 trial record and the Antigravity and Muse dispatcher support are removed. `D01 T01 §11` was the last section reviewed by the panel; this ledger stays as the record.

## Limits

- Three sections is a small sample: results show direction, not significance.
- Both arms run at the same time on one machine, so absolute seconds include shared local load; the comparison between arms is fair, single numbers are not benchmarks.
- The writer's fixes after a pass change what the next pass sees, so rounds after round 1 compare arms on the same candidate, not on independent histories.
- Verdicts are the writer's judgment against the code and sources, recorded with reasons so they can be audited.
