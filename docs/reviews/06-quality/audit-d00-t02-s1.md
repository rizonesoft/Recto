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

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:31:08Z
- Effort: medium
- Lines: 27
- Digest: e7c4f48fe9735262

## adversarial: needs-attention

1. **P1: Blockquoted checkboxes bypass unfinished-work checks.** `scripts/lib/todo_graph.py:386` checks ordinary and numbered checkboxes, while line 407 counts only unquoted checkboxes. Reproduce by inserting `> - [ ] Unfinished required work.` immediately before section 2 in `todo/00-workspace/TODO-02-todo-system.md`. An in-memory overlay passed validation with 677 sections; section 1 retained disposition `3` (verified). The strict parser must reject this noncanonical checklist syntax instead of silently excluding unfinished work.

## consistency: approve

No additional finding.

## integration: needs-attention

2. **P1: An empty deferral marker disappears from validation and reporting.** `scripts/lib/todo_graph.py:472` requires content after the marker before checking its owner; `scripts/lib/todo_graph.py:636` uses the same pattern for reporting. Reproduce by inserting `> **Deferred:**` as the final line of section 1, immediately before section 2. The marker passes the new blockquote-format check, but section-body trimming leaves it unmatched by both subsequent expressions. The in-memory reproduction passed validation, retained disposition `3`, and returned no deferral for this section. An ownerless deferral is therefore accepted and omitted from `query deferred`.

## source-defect: needs-attention

3. **P1: Malformed qualified references silently resolve to a different section.** `scripts/lib/todo_graph.py:84` uses an unanchored `REF.search`; `scripts/lib/todo_graph.py:292` accepts that partial match as an XREF target. Reproduce by inserting `-> XREF: D00 T999 §1` before section 2 in the audited TODO file. `xrefs_of()` returns `D00 T02 §1`, silently discarding the malformed qualification and substituting the current TODO. The overlaid tree passes validation. Likewise, `-> XREF: D00 T01 §1 -> XREF: D00 T99 §999` validates while checking only the first target. Invalid references evade both unresolved-target and bidirectional-XREF checks.

## design: advisory

Not applicable to this section’s declared surface.

## record: advisory

The full-suite checkpoint remains independently unconfirmed. Loading and running the current suite through `unittest` produced 68 tests with 67 fixture-setup errors because the read-only environment cannot create temporary directories; the Repository test passed. This is an environment limitation, not a candidate defect. Current `plan --check` returned `plan current`, and validation returned `valid: 677 sections`. These results do not establish the historical full-suite pass recorded at the findings file’s top.

All reproductions used in-memory overlays at `d45ae78`. No files were modified; the working tree remains clean.

OVERALL: needs-attention

## Fixes after round 4

The parser now works from an allow-list rather than patching one spelling at a time: each construct the graph reads has exactly one accepted form, and any line shaped like it in another form refuses. In `scripts/lib/todo_graph.py`, tested in `tests/scripts/test_todo_graph.py` (`Ran 70 tests`, `OK`; against `d45ae78` the four new or changed tests report `FAILED (failures=4)`; the live tree validates, `valid: 677 sections`).

- Finding 1 (P1): any checklist-shaped line (a list marker or number, then a box, whether quoted, indented or numbered) must be the canonical `- [ ] ` or `- [x] ` at column 0 (`test_checklist_item_in_another_spelling_refuses`).
- Finding 2 (P1): a `Deferred` or `Resolved` marker must open a blockquote line and carry text; the extraction regexes no longer let `\s*` run onto the next line (`test_empty_deferral_marker_refuses`).
- Finding 3 (P1): every `-> XREF:` on a line is read with `XREF_TARGET` (one space, then exactly one well-formed reference not run into further text), and `find_ref` (adjacency owners) now requires the whole text to be one reference, so `D00 T999 §1` or a second malformed XREF refuses instead of resolving elsewhere (`test_malformed_xref_does_not_resolve_to_another_section`).

## Round 5: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:35:34Z
- Effort: medium
- Lines: 50
- Digest: b5da995410beea75

## adversarial: needs-attention

1. **P1: A second deferral marker bypasses ownership checks.** `scripts/lib/todo_graph.py:479` checks only that the line begins with an accepted marker; line 483 extracts only that first marker. Insert this before section 2 in `todo/00-workspace/TODO-02-todo-system.md`:

   ```markdown
   > **Resolved:** Old work -> XREF: D00 T02 §1 **Deferred:** New unowned work.
   ```

   The in-memory reproduction passed validation with 677 sections and retained disposition `3` (verified). `deferrals()` returned only a resolved entry, hiding the ownerless outstanding work.

## consistency: approve

No additional finding.

## integration: needs-attention

2. **P1: Receipt validation accepts self-review as independent evidence.** `scripts/lib/todo_graph.py:247` requires a matching round with a reviewer, but never compares the parsed reviewer with the writer. Reproduce with a current-format receipt claiming round 1 as `clean` and this report:

   ```markdown
   ## Round 1: claude (`claude-opus-5` via `claude`)

   - Writer: claude
   - Run: 2026-10-21T00:00:00Z

   OVERALL: approve
   ```

   The dispatcher's reader returns `writer='claude'` and `reviewer='claude'`; `receipt_data_ok()` accepts it. The stamp validation path therefore accepts evidence that violates the mandatory independent-review rule.

## source-defect: needs-attention

3. **P2: CLI resolution silently truncates malformed section references.** `scripts/lib/todo_graph.py:106` searches for a reference without checking its trailing boundary. Run:

   ```powershell
   python -B -X utf8 scripts/todo-graph.py classify 'D00 T02 §1oops' 'D00 T02 §1.5'
   ```

   Both return disposition `3`, resolving to `D00 T02 §1`. `resolve` uses the same extractor and supplies that section as `skill_arg`. Malformed input can therefore route downstream execution to a different section instead of being refused.

## design: advisory

Not applicable to this section’s declared surface.

## record: advisory

The full-suite claim at `docs/reviews/06-quality/audit-d00-t02-s1.md:9` remains independently unconfirmed. At `49c6a12`, the suite ran 70 tests with 69 fixture-setup errors because the read-only environment cannot create temporary directories; the Repository test passed. Validation reported `valid: 677 sections`, and `plan --check` returned `plan current`. These results do not establish the recorded historical full-suite pass.

All mutation probes used in-memory overlays. No files were modified; the working tree remains clean.

OVERALL: needs-attention

## Fixes after round 5

In `scripts/lib/todo_graph.py`, tested in `tests/scripts/test_todo_graph.py` (`Ran 72 tests`, `OK`; against `49c6a12` the three new or changed tests report `FAILED (failures=3)`; the live tree validates, `valid: 677 sections`, so every current receipt is a cross-family review).

- Finding 1 (P1): a line may carry at most one `Deferred` or `Resolved` marker (`test_two_deferral_markers_on_one_line_refuse`).
- Finding 2 (P1): a claimed round must be reviewed by another model family than its writer, judged with the dispatcher's own `FAMILY` and `WRITER_FAMILY` maps; a self-review, or an unknown writer, refuses (`test_a_claimed_round_must_be_a_reviewed_round_in_its_report`).
- Finding 3 (P2): `extract_ref` no longer reads a reference run into more text (`§1oops`, `§1.5`) as `§1`; such input exits 2, while a trailing full stop, bracket or table pipe still resolves (`test_reference_run_into_more_text_does_not_resolve`).

## Round 6: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:39:05Z
- Effort: medium
- Lines: 41
- Digest: c34e55248a323981

## adversarial: needs-attention

1. **P1: Malformed XREFs still resolve to another section.** `scripts/lib/todo_graph.py:21` accepts `§1.5` as `§1`. Reproduce by inserting `-> XREF: D00 T02 §1.5` before section 2 in `todo/00-workspace/TODO-02-todo-system.md`. An in-memory overlay passed validation with 677 sections and retained disposition `3` for section 1. The unresolved reference escapes validation. Round 5 hardened `extract_ref`, but the XREF parser retains this defect.

## consistency: approve

No additional finding.

## integration: needs-attention

2. **P1: Empty or blocked reviewer output qualifies as independent review.** `scripts/lib/todo_graph.py:250` checks the round envelope and model families without checking whether a review was returned. Reproduce with an otherwise valid current-format receipt claiming round 1 as `clean`, pointing to:

   ```markdown
   ## Round 1: astra (`gpt-6-astra` via `codex`)

   - Writer: claude
   - Run: 2026-10-21T00:00:00Z
   - Lines: 1

   OVERALL: blocked
   ```

   `receipt_data_ok()` accepts it. Changing `Lines` to `0` and removing the body also passes. Both reproductions ran in memory. The dispatcher explicitly defines `blocked` as a reviewer that never saw the work (`scripts/review-dispatch.py:55`), so envelope validity does not establish review evidence.

## source-defect: needs-attention

3. **P2: Report normalization rejects valid dispatcher records.** `scripts/lib/todo_graph.py:249` reads reports through `read_text()`, which converts lone carriage returns into newlines. The dispatcher deliberately preserves these characters when counting and hashing review lines (`scripts/review-dispatch.py:332`).

   Reproduce with a correctly counted and digested two-line review whose first line contains `Captured progress: 10%\rCaptured progress: 100%` and whose second line is `OVERALL: approve`. The dispatcher’s `read_rounds(raw)` accepts it; receipt validation rejects it after normalization. Legitimate captured output can therefore prevent stamping or invalidate subsequent graph validation.

## design: advisory

Not applicable to the section’s declared surface.

## record: advisory

The full-suite checkpoint remains independently unconfirmed. At `6b0704b`, the suite reported 72 tests with 71 fixture-setup errors because the read-only environment cannot create temporary directories. The Repository test separately passed. `validate` reported `valid: 677 sections`; `plan --check` reported `plan current`. These results do not establish the historical full-suite pass recorded at `docs/reviews/06-quality/audit-d00-t02-s1.md:9`.

All mutation probes used memory only. No files were modified; the working tree remains clean.

OVERALL: needs-attention

## Fixes after round 6

Tested in `tests/scripts/test_todo_graph.py` (`Ran 73 tests`, `OK`; against `6b0704b` the three new or changed tests report `FAILED (failures=2, errors=1)`); `python -m unittest discover -s scripts/tests` -> `OK (skipped=5)`; the live tree validates (`valid: 677 sections`), so every current receipt round ends in a real `OVERALL` verdict.

- Finding 1 (P1): `XREF_TARGET` gets the same boundary as `extract_ref`, so `-> XREF: D00 T02 §1.5` refuses (`test_malformed_xref_does_not_resolve_to_another_section`).
- Finding 2 (P1): `read_rounds` in `scripts/review-dispatch.py` now returns each round's review text, and a claimed round must hold a review whose last line is `OVERALL: approve` or `OVERALL: needs-attention`; an empty, missing or `blocked` reply refuses (`test_a_claimed_round_must_be_a_reviewed_round_in_its_report`).
- Finding 3 (P2): receipt validation reads reports as raw UTF-8, as the dispatcher writes them, so a review holding a lone carriage return keeps its count and digest (`test_a_review_with_carriage_returns_is_read_as_the_dispatcher_wrote_it`).

## Round 7: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:44:12Z
- Effort: medium
- Lines: 40
- Digest: 703b1490c79c1103

## adversarial: needs-attention

1. **P1: A rejecting review can be claimed as clean.** `scripts/lib/todo_graph.py:252` accepts either final verdict without checking its compatibility with the receipt’s claimed verdict. Reproduction: supply `receipt_data_ok()` with a current-format receipt claiming round 1 as `clean`, `rounds_spent=1`, and a valid cross-family report containing only round 1, an unresolved P1 finding, and `OVERALL: needs-attention`. The in-memory reproduction accepted it. No later review or stopped outcome is required. The stamp path calls this same validation at line 748.

## consistency: approve

No additional finding.

## integration: approve

No additional finding beyond finding 1’s effect on the stamp gate.

## source-defect: needs-attention

2. **P2: Round counting still normalizes reports and counts quoted headings as rounds.** `scripts/lib/todo_graph.py:686` uses `read_text()`, which converts carriage returns into newlines. When this breaks the dispatcher’s count or digest, line 690 falls back to matching headings throughout the report.

   Reproduction: construct one correctly counted and digested round whose review contains these four newline-delimited lines:

   ```text
   Captured: 10%\rCaptured: 100%
   Example heading:
   ## Round 999: none
   OVERALL: approve
   ```

   Here `\r` denotes an actual carriage-return character. The raw dispatcher reader returns only round `1`; `rounds_recorded()` returns `999`. Receipt validation accepts that inflated count. This persists false evidence through `stamp()` at line 744 and affects `query rounds`. The reproduction used an in-memory report overlay.

## design: advisory

Not applicable to this section’s declared surface.

## record: advisory

The full-suite claim at `docs/reviews/06-quality/audit-d00-t02-s1.md:9` remains independently unconfirmed. At `23183fb`, running the current suite through `unittest` produced 73 tests with 72 fixture-setup errors because the read-only environment cannot create temporary directories; the Repository test passed. This is an environment limitation, not a candidate defect.

`validate` reported `valid: 677 sections`; `plan --check` reported `plan current`. These results do not establish the historical full-suite pass.

No files were modified; the working tree remains clean.

OVERALL: needs-attention

## Fixes after round 7

In `scripts/lib/todo_graph.py`, tested in `tests/scripts/test_todo_graph.py` (`Ran 74 tests`, `OK`; against `07f3563` the two new or changed tests report `FAILED (failures=2)`; the live tree validates, `valid: 677 sections`, so every current receipt's verdicts match its reviews).

- Finding 1 (P1): a round's receipt verdict must match its review: `clean` needs a last line `OVERALL: approve`, every other verdict `OVERALL: needs-attention`; and a receipt must hold a clean round or a stopped verdict, so a round whose findings were fixed needs a later round that saw the fixes (`test_a_round_verdict_must_match_its_review`).
- Finding 2 (P2): `rounds_recorded` reads reports byte for byte too, so a carriage return inside a review no longer breaks the dispatcher's reader into the heading fallback that counted a quoted `## Round 999` (`test_a_review_with_carriage_returns_is_read_as_the_dispatcher_wrote_it` now quotes one and expects `rounds_spent` 1).

Separately, CI builds had failed since `59902fc` (D06 T05 §3): its `vswhere -requires` list named Windows SDK and WinUI components that GitHub's `windows-latest` image does not register. `07f3563` narrows it to the C++ x64 and ARM64 tools; the refusal now names what is missing.

## Round 8: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:49:49Z
- Effort: medium
- Lines: 29
- Digest: de4aeff6a1fdf80f

## adversarial: needs-attention

1. **P1: An earlier approval bypasses a later failing review.** `scripts/lib/todo_graph.py:262` checks whether *any* receipt round is clean or stopped, rather than requiring the review loop to end that way. Reproduced using valid cross-family report envelopes: round 1 ends `OVERALL: approve`, round 2 ends `OVERALL: needs-attention`; the receipt claims `1:clean`, `2:fixed`. `receipt_data_ok()` accepts it without a subsequent review of the fixes. Omitting round 2 from the receipt also passes, even though `rounds_recorded()` returns 2. The stamp path invokes this gate at line 752.

## consistency: needs-attention

2. **P1: A severity stop accepts unresolved P1 findings.** `scripts/lib/todo_graph.py:212` enforces the round-number threshold but never checks finding severity. Reproduced with nine valid cross-family review envelopes containing `1. P1: unresolved unsafe stamp.` and ending `OVERALL: needs-attention`. Claiming round 9 as `stopped-low-severity`, with `rounds_spent=9`, `round_cap=22`, and `cap_outcome="filed"`, passes `receipt_data_ok()`. This contradicts `todo/README.md:92`, which permits that stop only for P3 or advisory findings.

## integration: needs-attention

Finding 1 affects both stamp creation and subsequent graph validation. A receipt retaining an earlier approval can satisfy the gate despite a later rejecting review, allowing downstream sections to rely on that stamp. No separate finding.

## source-defect: approve

No additional finding.

## design: advisory

Not applicable to this section’s declared surface.

## record: advisory

The full-suite claim at `docs/reviews/06-quality/audit-d00-t02-s1.md:9` remains independently unconfirmed. At `68cb8f2`, the checkpoint suite ran 74 tests with 73 temporary-fixture setup errors under the read-only sandbox; the Repository test passed. This is an environment limitation, not a candidate defect.

`plan --check` exited 0, `validate` reported 677 sections, and resolving `D00 T02 §1` exited 3. These results do not establish the recorded historical full-suite pass.

All mutation probes used memory only. No files were modified; `main` remains clean.

OVERALL: needs-attention

## Fixes after round 8

In `scripts/lib/todo_graph.py`, tested in `tests/scripts/test_todo_graph.py` (`Ran 76 tests`, `OK`; against `68cb8f2` the three new or changed tests report `FAILED (failures=3)`; the live tree validates, `valid: 677 sections`).

- Finding 1 (P1): each findings file is one review loop, and the receipt must name that loop's last reviewed round, which must be clean or a stop; an earlier approval can no longer stand in for a later failing review, and a receipt cannot omit the last round (`test_a_loop_ends_at_its_last_reviewed_round`). This replaces the receipt-wide "any clean round" rule.
- Finding 2 (P1): a `stopped-low-severity` round must carry no `P1` or `P2` tag in its review (`test_a_severity_stop_with_a_p1_or_p2_finding_refuses`). The check is deliberately strict: a review that mentions P1 or P2 at all cannot be a severity stop.

## Round 9: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:54:09Z
- Effort: medium
- Lines: 41
- Digest: 31c7b8c6b3e311e5

## adversarial: needs-attention

1. **P1: Untagged findings bypass the severity-stop gate.** `scripts/lib/todo_graph.py:265` only rejects explicit `P1` or `P2` text. An untagged finding, which the audit contract counts as P1, passes.

   Reproduced in memory at `b90550c`: create nine valid cross-family review envelopes using the test suite’s `envelope()` helper, each containing:

   ```markdown
   ## adversarial: needs-attention

   1. `scripts/lib/todo_graph.py:265`: unresolved unsafe stamp accepts rejected work.

   OVERALL: needs-attention
   ```

   Call `receipt_data_ok()` with round 9, `verdict="stopped-low-severity"`, `rounds_spent=9`, `round_cap=22`, and `cap_outcome="filed"`. It accepts. The same probe with an explicit P1 tag refuses. The gate therefore permits closing a loop with unresolved findings whose severity defaults to P1.

## consistency: needs-attention

Finding 1 violates the explicit untagged-finding rule and the requirement that a severity stop contain only P3 or advisory findings. No separate finding.

## integration: needs-attention

Finding 1 affects the shared receipt gate used by `stamp()` and subsequent graph validation. An invalid severity stop can satisfy downstream prerequisites. No separate finding.

## source-defect: approve

No additional finding.

## design: advisory

Not applicable to this section’s declared surface.

## record: advisory

The checkpoint claim at `docs/reviews/06-quality/audit-d00-t02-s1.md:9` remains independently unconfirmed. Running the current suite through `unittest` produced 76 tests with 75 temporary-fixture setup errors because the sandbox is read-only; the Repository test passed. This is an environment limitation, not a candidate defect.

`plan --check` returned `plan current`; validation reported 677 sections. These checks do not establish the historical full-suite pass or the claimed regression failures against `68cb8f2`.

No files were modified; the working tree remains clean at `b90550c`.

OVERALL: needs-attention

## Fixes after round 9

- Finding 1 (P1): a severity stop now works from an allow-list: every numbered finding in the stopping round must carry a `P3` tag (an untagged finding counts as P1), and no `P1` or `P2` may appear. `test_a_severity_stop_with_a_p1_or_p2_finding_refuses` adds an untagged finding and a P3 list with one untagged item, and fails against `b90550c`; `test_a_severity_stop_with_only_p3_findings_stamps` keeps the allowed case. `Ran 77 tests`, `OK`; `valid: 677 sections`.