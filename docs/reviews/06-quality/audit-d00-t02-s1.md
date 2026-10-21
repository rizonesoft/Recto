# Audit - D00 T02 §1: Graph engine, CLI and tests

- **Audit section:** `D06 T05 §5` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T02 §1` (`docs/reviews/d00-t02-s1.json`, 3 rounds under the earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `566b833`: `python -m unittest tests.scripts.test_todo_graph` -> `Ran 58 tests in 16.935s`, `OK` (including the Repository test against this tree); `python scripts/todo-graph.py plan --check` -> `plan current`. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:17:57Z
- Effort: medium
- Lines: 33
- Digest: cb2beff63b5c04d1

## adversarial: needs-attention

1. **P1: Malformed work is silently omitted.** `scripts/lib/todo_graph.py:311` skips table lines without a leading `|`; lines 350 and 368 only recognize bullet checkboxes. Two separate in-memory reproductions against the candidate passed validation:
   - Append ` | 4 | §999 | Orphan | -- | [ ] |` to the Implementation Order table. The missing section body is never reported.
   - Insert `1. [ ] Unfinished work.` before the stamped section’s Commit item. The unchecked task is ignored, and the section remains verified.

   Both violate the section’s explicit promise to refuse malformed work instead of treating it as absent.

## consistency: needs-attention

2. **P1: The round-9 severity threshold is not enforced.** `scripts/lib/todo_graph.py:207` and `scripts/lib/todo_graph.py:664` validate an outcome string but never enforce `SEVERITY_CUTOFF_ROUND`. Using the existing `todo_file` fixture with completed work, submit review round `1`, verdict `stopped-low-severity`, and `cap_outcome='filed'`. The actual `stamp()` function completed successfully with filesystem writes redirected to memory. This permits stopping eight rounds before the repository rule allows it. The test at `tests/scripts/test_todo_graph.py:394` exercises only the permitted case.

## integration: needs-attention

3. **P1: A waiting Need can coexist with a valid stamp.** `scripts/lib/todo_graph.py:388` validates stamped checklists and receipts without rejecting waiting Needs; `scripts/lib/todo_graph.py:482` then returns “verified” before considering Needs. Reproduce by replacing the first `**Needs:** none` in `TODO-02-todo-system.md` with `**Needs:** owner-decision`. An in-memory overlay passed graph validation with 677 sections, and `disposition()` returned `3`. Dependencies remain satisfied despite the newly declared blocker, allowing downstream work to proceed.

## source-defect: needs-attention

4. **P2: Keyboard interruption bypasses stamp rollback.** `scripts/lib/todo_graph.py:702` catches `Exception`, which excludes `KeyboardInterrupt`. Inject `KeyboardInterrupt` on the second `atomic()` call, after receipt creation. The in-memory execution left the receipt present and the TODO unchanged; retry failed with `receipt already exists: docs/reviews/d00-t01-s1.json`. Interrupting between writes therefore breaks the promised rollback and retry behaviour. The rollback test at `tests/scripts/test_todo_graph.py:492` injects only `OSError`.

## design: advisory

Not applicable to this section’s declared surface.

## record: needs-attention

5. **P1: A nonempty report substitutes for an actual review round.** `scripts/lib/todo_graph.py:180` validates receipt fields, while line 201 checks only that the report exists and contains text. Reproduce with a report containing only `# No independent review has run`, a claimed round `1:…:clean`, and otherwise valid stamp inputs. `stamp()` completed successfully in memory. Separately, changing this section’s receipt to claim round `999` against its existing three-round report still passed graph validation. Negative round numbers also passed. The positive fixture at `tests/scripts/test_todo_graph.py:324` explicitly accepts a heading-only report, leaving this evidence gap untested.

**Checkpoint verification limitation:** The owned source files match `566b833`. Validation and plan comparison also passed using the committed version of the locally modified TODO file. However, I could not independently confirm the findings file’s full-suite `OK`: my run reported 58 tests with 57 setup errors because the read-only environment cannot create temporary fixtures. The Repository test passed. Those setup errors are an environment limitation, not a candidate defect.

No files were modified.

OVERALL: needs-attention

## Fixes after round 1

All in `scripts/lib/todo_graph.py`, each with a regression test in `tests/scripts/test_todo_graph.py` that fails against `566b833` and passes now (`Ran 64 tests`, `OK`; against the old library the six new tests report `FAILED (failures=6)`).

- Finding 1 (P1): an Implementation Order line that holds a pipe but does not start with one refuses (`test_table_line_without_its_leading_pipe_refuses`), and a numbered checkbox (`1. [ ]`, `2) [x]`) refuses as a checklist item the graph would not count (`test_numbered_checkbox_refuses`).
- Finding 2 (P1): `receipt_data_ok` refuses `stopped-low-severity` on any round up to `SEVERITY_CUTOFF_ROUND`, so `stamp` refuses it before writing (`test_a_severity_stop_before_the_cutoff_refuses`).
- Finding 3 (P1): a stamped section declaring a waiting Need refuses validation (`test_a_stamped_section_with_a_waiting_need_refuses`).
- Finding 4 (P2): `stamp` rolls back on `BaseException`, so an interrupt between writes leaves the tree as it was and a retry succeeds (`test_an_interrupted_stamp_rolls_back_and_allows_retry`).
- Finding 5 (P1): every receipt round must be an integer of at least 1 (bools refused); a receipt with `rounds_spent` (every stamp since 2026-10-21) must name rounds its report records as reviewed `## Round N: <reviewer>` headings written by `scripts/review-dispatch.py`, not `none` (`test_a_claimed_round_must_be_a_reviewed_round_in_its_report`, including a receipt edited to claim round 999). The Stamp fixture now records a real round. Receipts from before 2026-10-21 predate the dispatcher's round format (their reports name rounds in free text), so they keep the presence check; each is re-audited by its own `D06 T05` section, which re-stamps it under the current format.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:23:03Z
- Effort: medium
- Lines: 27
- Digest: d7cb77f33906c94a

## adversarial: needs-attention

1. **P1: An unexpected heading silently hides unfinished work.** `scripts/lib/todo_graph.py:344` ends a section at any level-two heading, but validation does not reject unrecognized headings. Reproduction: in an in-memory overlay of `TODO-02-todo-system.md`, insert `## Remaining work` followed by `- [ ] Unfinished required work.` immediately before section 2. `graph()` accepts all 677 sections, excludes the task from section 1’s body, and `disposition()` still returns `3` (verified). Malformed work disappears instead of being refused, leaving downstream prerequisites satisfied.

## consistency: needs-attention

2. **P1: Cap-stop verdicts bypass the review-round minimum.** `scripts/lib/todo_graph.py:211` checks that `rounds_spent` covers the claimed round, but never ties a `stopped-at-N` verdict to that cap. Reproduction: call `receipt_data_ok()` with otherwise valid evidence, the existing audit report’s round 1, `rounds_spent=1`, `round_cap=22`, `verdict="stopped-at-22"` and `cap_outcome="filed"`. It accepts. Substituting `stopped-at-3` also accepts a current-format receipt, although `todo/README.md:92` reserves that verdict for earlier receipts. The severity-stop fix therefore leaves another route to premature closure.

## integration: needs-attention

3. **P1: Receipt validation accepts reports the dispatcher rejects as invalid.** `scripts/lib/todo_graph.py:216` treats a matching heading as sufficient evidence of a reviewed round, bypassing the dispatcher’s envelope parser at `scripts/review-dispatch.py:347`. Reproduction: overlay an existing report with only `## Round 1: fake`, a blank line, and `No reviewer returned a review.` Supply a current-format receipt claiming round 1 as `clean`. `receipt_data_ok()` accepts it; the dispatcher’s `read_rounds()` returns `None`. This reproduces the unreviewed-round gap despite the round-1 fix, without invoking the intentional legacy-receipt exception.

## source-defect: approve

No additional finding.

## design: advisory

Not applicable to this section’s declared surface.

## record: advisory

The recorded full-suite pass remains independently unconfirmed. Running the current 64-test suite through `unittest` produced 63 setup errors because the read-only sandbox cannot create temporary fixtures; the Repository test passed. This is an environment limitation, not a candidate defect. Current `plan --check` returned `plan current`, and validation reported 677 sections. These results do not establish the historical checkpoint at `566b833`.

All reproductions used in-memory overlays. No files were modified; the working tree remains clean at `15df83c`.

OVERALL: needs-attention

## Fixes after round 2

In `scripts/lib/todo_graph.py`, tested in `tests/scripts/test_todo_graph.py` (`Ran 66 tests`, `OK`; against `15df83c` the three new or changed tests report `FAILED (failures=3)`). The live tree still validates (`valid: 677 sections`), so all eight receipts carrying `rounds_spent` pass the stricter rules.

- Finding 1 (P1): a TODO file may hold only the level-two headings the format defines (`## Inputs`, `## Outcome`, `## Implementation Order`, `## Verification`, `## N. <Title>`); any other refuses, so no heading can cut a section short (`test_unknown_level_two_heading_refuses`).
- Finding 2 (P1): on a receipt with `round_cap`, a `stopped-at-N` verdict must be `stopped-at-<round_cap>` on round `round_cap`; earlier cap verdicts stay valid only on receipts older than that field (`test_a_cap_stop_must_be_the_current_cap_reached`).
- Finding 3 (P1): a claimed round is checked with the dispatcher's own reader, `read_rounds` in `scripts/review-dispatch.py`, loaded by `review_dispatch()`: the report must read back whole and hold that round with a reviewer. A bare heading, a `none` round, or a round with no metadata block refuses. The test fixtures now write real dispatcher envelopes (`envelope()`).

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:27:11Z
- Effort: medium
- Lines: 27
- Digest: f8522268159a0a0d

## adversarial: needs-attention

1. **P1: Allowed headings still hide unfinished work.** `scripts/lib/todo_graph.py:363` allows repeated structural headings, while line 369 ends the section at any `## ` heading. Reproduce by inserting `## Inputs`, followed by `- [ ] Unfinished required work.`, immediately before section 2 in `TODO-02-todo-system.md`. An in-memory overlay passed validation with 677 sections; section 1 excluded the unchecked task and retained disposition `3` (verified). The round-2 fix rejects unknown headings but leaves this equivalent bypass.

## consistency: approve

No additional finding.

## integration: needs-attention

2. **P1: A malformed deferral disappears from validation and reporting.** `scripts/lib/todo_graph.py:465` and `scripts/lib/todo_graph.py:629` recognize only blockquoted deferral markers and never reject other forms. Insert `**Deferred:** Unimplemented required work.` before section 2 in `TODO-02-todo-system.md`. The text remains inside section 1, but validation accepts its ownerless deferral, the section remains verified, and `deferrals()` omits it. A missing `>` therefore bypasses the promised durable-deferral checks and hides outstanding work from consumers.

## source-defect: needs-attention

3. **P2: Round accounting interprets reviewer content as dispatcher metadata.** `scripts/lib/todo_graph.py:655` scans raw headings instead of using the dispatcher's parsed rounds. Reproduce with a valid round-1 envelope containing `- Lines: 1` and the single review-body line `## Round 999: example quoted by the reviewer`. The dispatcher's `read_rounds()` returns only round 1, but `rounds_recorded()` returns `999`. `receipt_data_ok()` accepts the resulting receipt with `rounds_spent=999` and `round_cap=22`. Stamping can consequently persist false review expenditure and corrupt `query rounds` statistics.

## design: advisory

Not applicable to this section’s declared surface.

## record: advisory

The full-suite checkpoint remains independently unconfirmed. At `32e1d51`, running the suite through `unittest` produced 66 tests with 65 fixture-setup errors because the read-only environment cannot create temporary directories. The Repository test passed. `plan --check` returned `plan current`; validation returned `valid: 677 sections`. This does not establish the historical full-suite result recorded at the findings file’s top.

All reproductions used in-memory overlays. No files were modified; the working tree remains clean.

OVERALL: needs-attention

## Fixes after round 3

In `scripts/lib/todo_graph.py`, tested in `tests/scripts/test_todo_graph.py` (`Ran 68 tests`, `OK`; against `32e1d51` the three new or changed tests report `FAILED (failures=3)`). The live tree validates (`valid: 677 sections`), so all 39 TODO files already follow the stricter heading order and deferral form.

- Finding 1 (P1): the level-two headings must be exactly `## Inputs`, `## Outcome`, `## Implementation Order`, the numbered sections and `## Verification`, once each and in that order, so a repeated structural heading can no longer cut a section short (`test_unknown_level_two_heading_refuses` now also inserts `## Inputs`, `## Outcome` and `## Verification`).
- Finding 2 (P1): a `**Deferred:**` or `**Resolved:**` marker anywhere in a section must open a `> ` blockquote line, the only form the deferral checks and `deferrals()` read (`test_deferral_marker_outside_a_blockquote_refuses`).
- Finding 3 (P2): `rounds_recorded` counts rounds with the dispatcher's `read_rounds`, so a round heading quoted inside a review is not counted; only a report the reader cannot read (older than its format) falls back to the headings (`test_a_round_heading_quoted_inside_a_review_is_not_counted`).