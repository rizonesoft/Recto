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