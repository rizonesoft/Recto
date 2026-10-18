---
name: review-todo-section
description: Quality-gate a just-implemented TODO section with self-review, independent review lenses and a bounded fix loop, then write the Verified stamp through the stamp command. Also runs in audit stance on shipped sections. Use after process-todo-section or when asked whether a section is really done.
---

# Review TODO Section

The gate between "code exists" and "the row says `[x]`". Nothing else may flip that row, and the flip happens only through `python scripts/todo-graph.py stamp`, which refuses a section with unticked items, an open prerequisite, a waiting Need, or no review evidence.

Review is mandatory and independent: the implementing session self-reviews, and at least one review round comes from an independent reviewer running `GPT-6 Astra` (`gpt-6-astra`) via the Codex CLI (`Get-Content "<scratch-review-prompt.md>" -Raw | codex exec -m gpt-6-astra --ephemeral -`). Every round is recorded in a findings file; the stamp's receipt names the rounds. A verdict with no record is an opinion.

## Use this skill when

- A section was just implemented (`process-todo-section` ends by invoking this).
- The user asks "is §N really done?", "re-verify §N", "audit §N": run in audit stance (below).
- Before closing a TODO file with `process-todo-file`.

## Step 0: resolve the argument

Same front door as `process-todo-section`: `python scripts/todo-graph.py resolve "$ARGUMENTS"`. Exit 3 means the row is already `[x]`: that is the signal for audit stance, not an error.

## The three questions

Every review answers these with code and behavior evidence, never assertion:

1. **Does it make sense?** Does the implementation produce the exact outcome the section asked for, without unnecessary scope?
2. **Is it logical?** Do the contracts agree: threading rules, error paths, settings consumers, UI states, failure behavior?
3. **Did it break anything else?** Inspect callers, consumers, tests and adjacent workflows that read what changed.

## Workflow

### 1. Fix the candidate

Name the candidate: the implementation commit, or the commit range when fix-loop commits append (commits are never amended once review starts). Record the full SHA(s) in the findings file at `docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md`. For an audit, the candidate is the section's own stamped commit, not today's tree. Read the actual diff (`git show`), not memory of writing it. Map each ticked checklist item to the code that satisfies it; an item with no corresponding change is not done (untick it) or was already true (note where).

### 2. Self-review

Work the three questions across the diff and its blast radius. Fix what you find, then re-ask them. Look specifically for the failure modes this codebase invites:

- A `recto::pdf::Document` call on the wrong thread, a document instance shared across threads instead of owned by a single `DocumentActor`, or unbounded recursion/allocation on hostile PDF inputs (foundation plan sections 3 and 4).
- An engine or parser function that throws or aborts instead of returning `PdfResult<T>` / `PdfError`.
- A tile cached after abort, a bitmap stride assumed to be `width * 4`, or a `CancelToken` polled without cooperative abort semantics.
- A file write that is not temp-then-replace, or originals modified in place by Organize operations.
- Office automation without `AutomationSecurity` forced off, or a document link opened without asking.
- A hex colour, font size or glyph set directly on a control instead of SPEC tokens and Segoe Fluent Icons.
- A setting nothing reads, a command unreachable from the command box, a control with no `AutomationProperties.Name`.
- A test that passes without exercising the behavior.

### 3. Independent review rounds (`GPT-6 Astra` via `codex`)

Run the lenses as separate passes over the candidate, at least one round by the independent `GPT-6 Astra` (`gpt-6-astra`) reviewer invoked via:

```powershell
Get-Content "<scratch-review-prompt.md>" -Raw | codex exec -m gpt-6-astra --ephemeral -
```

Record every round in the findings file: round number, reviewer (`GPT-6 Astra` via `codex exec -m gpt-6-astra`), candidate SHA, per-lens verdicts (`approve`, `needs-attention` with numbered findings naming `file:line`, or `advisory`), and the quoted gate outputs (the Live proof).

| Lens | Asks |
| ---- | ---- |
| `adversarial` | How does it fail on malformed PDFs, races, aborts, macros, or links? |
| `consistency` | Does it match module rules, naming, shared controls, settings, errors? |
| `integration` | Do actor contract, tile hand-off, job queue, and shell slots hold? |
| `source-defect` | Are PDF 1.7/2.0, OpenXML, WinUI, and SPEC rules read and followed? |
| `design` | Does the rendered surface match the SPEC screenshot and tokens? |
| `record` | Does evidence match what ran, with owned deferrals and earned flip? |

The loop is bounded: at most 3 rounds. Findings are fixed in new commits appended to the candidate, gates re-run, and the affected lenses re-run against the new tip. A unit patched three rounds running is stopped and re-thought instead of patched again. Round verdicts for the receipt: `clean` (no findings), `fixed` (findings found and fixed in a later round), `stopped-at-3` (cap reached; every leftover is filed through `add-todo` with an owner, or the run stops with no stamp when a leftover is safety, data-integrity or stamp-invalidating). When the independent reviewer is unreachable (no CLI, auth failure, timeout), record the outage in the findings file with an owner and a due date and stop without a stamp: a session-only pass is not a substitute.

Architecture check: on sections touching the bridge boundary, the threading contract, document lifetime, the tile hand-off, conversion isolation or the shell slot model, one findings-file paragraph answers whether the change keeps foundation plan sections 3 and 4 intact, naming any decision it effectively revises. A revision is recorded as a plan correction, never smuggled in as code.

### 4. Re-run the gates

After the last fix, re-run the section's Test checkpoint and its owed gates (affected GoogleTest suites, build warnings clean, `validate`, the Freeze check in frozen files, ASan where the section names it). Quote the outputs in the findings file. A fix verified by reasoning is not verified. A red gate reopens the candidate: fix, re-run to green, and re-run the affected lens, because earlier verdicts rested on the broken candidate.

### 5. Surface and frozen checks

Surface sections: every control, menu item, dialog and state on the Fidelity counterpart is working (proven on the rendered surface in this review) or deferred to a named, resolving section. Refuse the stamp for an unaccounted control. Frozen files: every `**Freeze check:**` ran and passed with the result quoted; a frozen behavior that moved without a recorded operator approval means no stamp and a question for the operator.

### 6. Stamp

Tick nothing by hand. Confirm every checklist item except `Commit:` is `[x]`, then:

```powershell
python scripts/todo-graph.py stamp 'DNN TNN §N' --sha <full candidate tip SHA> --review '1:docs/reviews/<domain>/D<NN>-T<NN>-s<N>.md:clean' --check '<checkpoint command> -> <quoted result>'
```

One `--review` per round, one `--check` per gate actually run. `stamp` writes the receipt `docs/reviews/<ref>.json`, ticks the `Commit:` item and the Implementation Order row, appends the `Verified:` and `Review:` lines, marks the file `done` when its last section stamps, and the CLI syncs the plan. Commit the stamp, receipt, findings file and regenerated plan together as `todo: stamp <ref>`. Add any `Deferred:` lines (each with its `-> XREF:` owner) and `Started:` to `Duration:` notes to the section before stamping, since a stamped section's checklist is never rewritten; a later change is a new section that names the old one.

### 7. Audit stance

On an already-`[x]` section: run steps 1 to 5 against the section's own candidate and receipt. Confirm the stamp's evidence still holds (re-run the checkpoint) or find the regression. A stamped section's `[x]` row, checklist, `Verified:` line, and receipt are immutable (`todo/README.md` and `validate` refuse reopening a stamped row or overwriting a receipt): when a regression is found, record a dated `> **Audit YYYY-MM-DD:**` note in the section naming the failing check, file a new repair section through `add-todo` (citing the regressed section and any downstream sections whose assumptions are affected), and sync the plan. A re-confirmed row keeps its stamp untouched; say so in one line.

## Guardrails

- Do not stamp without a findings file and at least one independent round.
- Do not stamp an unaccounted control on a surface section, or a moved frozen behavior.
- Do not hand-write `Verified:` lines, hand-flip rows, or edit a receipt. `stamp` owns the record.
- Do not review the working tree when the candidate is a commit. Name the hashes.
- Do not transfer evidence across changed candidates: a new tip re-runs its gates.
