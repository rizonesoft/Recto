## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T01:25:20Z
- Effort: high
- Lenses: adversarial, integration, source-defect, plan-delivery
- Lines: 20
- Digest: 56fce7f2e286de38

### adversarial: needs-attention

1. **[P1] Malformed replies advance the schedule without earning a review.** `scripts/review-dispatch.py:220` counts any named reviewer of another family, without requiring a valid final `OVERALL:` line. Real trigger: the reviewer exits successfully but returns incomplete prose. Record that as round 1, then run another round against the same findings file. Round 2 becomes medium/adversarial, and its approval passes receipt validation even though no valid high-effort, four-lens review exists. An in-memory probe using the dispatcher’s recorder and `receipt_data_ok()` reproduced acceptance. Use the same returned-review predicate for scheduling and receipt validation.

### integration: needs-attention

2. **[P2] Receipt validation now requires filesystem write access.** `scripts/lib/todo_graph.py:383` creates a temporary TOML file whenever it loads the stamped commit’s schedule. Consequently, once a new-format receipt exists, `validate`, queries and other `graph()` consumers fail in this repository’s read-only reviewer sandbox. Directly calling `pin_schedule()` on `80daa4c4076208bb4bde92339bc2115331af9b6a` reproduced `FileNotFoundError: No usable temporary directory`. Load and validate the Git-provided TOML bytes in memory.

### source-defect: needs-attention

3. **[P2] The fallback build-start search matches different section numbers.** `scripts/lib/todo_graph.py:338` uses fixed-string substring matching without a reference boundary. In the current repository, `D01 T02 §2` is open and has no Started line, yet `build_start()` returns `17bb0f441b3f1888ce162e8c8ab01cac5bb29a03` because its message mentions **§22 and §23**, not §2. A legitimate later plan review is therefore rejected against an unrelated build-start commit. Reproduce by calling `build_start()` with that section’s current path and body. Match the complete section reference.

### plan-delivery: needs-attention

4. **[P1] Plan and completion outages escape schedule validation.** `scripts/lib/todo_graph.py:412` checks only the selected successful plan record; lines 415-418 explicitly filter completion outages out. Record a low-effort/adversarial outage for each one-pass review, followed by successful medium-effort reviews using the current schedule. The receipt passes despite the requirement to check outages too. This can occur when an unsuccessful invocation used a different pin before retrying. An in-memory probe using `record_completion()` and `receipt_data_ok()` reproduced acceptance. Replay every recorded attempt in both referenced files.

Verification: `validate` returned `valid: 831 sections`. The requested unittest command ran 113 tests with 110 temporary-directory setup errors caused by the read-only sandbox; it did not establish a passing suite. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T01:36:49Z
- Effort: medium
- Lenses: adversarial
- Lines: 10
- Digest: 8a8f3d960fb8526e

### adversarial: needs-attention

1. **P1, `scripts/lib/todo_graph.py:363`: A different section’s plan review satisfies the gate.** `plan_record()` checks family, verdict and ancestry, but never binds the review to `ref`. An agent can accidentally reuse another section’s `--plan` path and stamp without reviewing the intended plan.

   Reproduced read-only using actual repository records: load `docs/reviews/d01-t02-s26.json` into memory, populate its `plan` through `plan_record()` using `docs/reviews/01-engine/D01-T02-s24.md` and section 26’s actual `build_start()`, and populate `schedule` through `pin_schedule()`. Both the ancestry check and full `receipt_data_ok()` validation accept it. Bind dispatcher-recorded plan evidence to the section reference and verify that binding when stamping and revalidating.

Verification: `validate` returned `valid: 831 sections`. The unit suite reported 113 errors across 116 tests because the read-only sandbox prevented temporary-directory creation. No files modified.

HIGHEST: P1
OVERALL: needs-attention

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T01:53:17Z
- Effort: low
- Lenses: adversarial
- Lines: 8
- Digest: a8b5d490998a6b1a

## adversarial: needs-attention

1. **P2** `scripts/lib/todo_graph.py:335`: `build_start()` searches the entire file for a substring, rather than locating the section’s own Started line. Repeated dates, including dates that prefix longer timestamps, select another section’s earlier commit and reject a legitimate plan review. Read-only reproduction on `todo/00-workspace/TODO-03-test-backbone.md`: calling `build_start()` for `D00 T03 §5` returns `2458fb42c827eb720efc40c3910df054fda85b6a`; `git show 9d9772a -- todo/00-workspace/TODO-03-test-backbone.md` proves its Started line was added in `9d9772a`. Resolve history within the identified section and match the full line.

Validation returned `valid: 831 sections`. The unit suite ran 117 tests with 114 errors because the sandbox denied temporary-directory creation; test verification remains incomplete.

HIGHEST: P2
OVERALL: needs-attention

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-24T02:07:09Z
- Effort: low
- Lenses: consistency
- Lines: 8
- Digest: 0995e1b14a351db6

## consistency: approve

No actionable consistency findings.

Verification: `python -B scripts/todo-graph.py validate` returned `valid: 831 sections`. The requested unit suite ran 118 tests, with 115 setup errors because the read-only sandbox had no writable temporary directory; test verification remains incomplete.

HIGHEST: none
OVERALL: approve
