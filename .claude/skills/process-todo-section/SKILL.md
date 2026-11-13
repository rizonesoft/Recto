---
name: process-todo-section
description: Process exactly one TODO section end to end. Resolve it, validate and fact-check the plan against the repository, correct every drifted claim, build it, run the gates, hand to review, and commit. Use whenever asked to process, implement, ship, continue, or finish a TODO section.
---

# Process TODO Section

The section is the contract, and a contract is checked before it is signed.

Processing a section is two jobs, in order. First establish that the plan is sound: internally consistent, still true of the code and of its governing documents, buildable as written, and verifiable when built. Then implement it. Do not improvise around the plan, and do not implement a plan you have found to be wrong. Correct it in the file, visibly, then build the corrected version.

## Use this skill when

- The user names a section in any form: a `DNN TNN §N` reference, a path plus section, or a row pasted straight out of `todo/implementation-plan.md`. Step 0 turns all of them into the same thing.
- A runner picks the next ready row from the plan.
- Do not use it to audit shipped work: that is `review-todo-section` in audit stance.

## Step 0: resolve the argument before anything else

Never hand-translate a reference into a filename. Ask the graph:

```powershell
python scripts/todo-graph.py resolve "$ARGUMENTS" --context
```

Run it once: this one call resolves the reference and prints everything step 1 reads, so the section is never fetched twice. Its first line is `# resolve --context DNN TNN §N` (the canonical reference), its `- state:` line repeats the exit code with the reason when the section waits, and the rest is the section with its file context (step 1 says what each part is). A refusal (exit 1 or 2) prints only a `TODO refused:` line on standard error. Its exit code is the instruction:

| Exit | Meaning | Do this |
| :-: | --- | --- |
| 0 | Open, dependencies met, no waiting Need | Proceed to step 1. |
| 1 | No such section | Stop; report the reference as unresolvable. |
| 2 | No section reference in the input | Stop and ask which section. |
| 3 | The section is already `[x]` | Hand to `review-todo-section` in audit stance. |
| 4 | Waiting on deps, file status, or person Need | Stop at the gate and name what it waits on. |

Use the reference in the output's first line (`DNN TNN §N`) as the canonical form for the rest of the run, including the commit message and the review call.

If the section carries `windows-host`, `arm64-host`, `display-session`, `office`, `libreoffice` or `attended` in Needs, confirm that environment is actually reachable before writing `Started:`. If it is not, stop and say what the section waits on: starting an environment-bound section without the environment burns a session producing nothing committable.

Before the first edit in a session, read `todo/README.md` (the format every TODO edit must keep). Before any write, stop here if another writer holds the tree. Check `git status` for unfamiliar uncommitted work you do not understand, and ask before building over it.

`Started:` is not written here: the build start is the first commit that holds the section's `> **Started:**` line, and `stamp` refuses a plan review whose candidate is later than it, so the line is written in step 3a, after the plan review.

## Execution discipline

- **Validate before building.** Step 2 is a gate, not a formality.
- **Follow the corrected contract.** The checklist items define the scope. Adjacent wrongness gets filed with `add-todo` (or in `todo/findings.md` with `file:line` when it is genuinely out of scope), not fixed in passing inside this section's work. The standing rule to fix what you find applies at the owning layer with its own owner; it does not widen this section.
- **Commits follow logical steps.** A section may be committed in one or more commits as steps complete; review fix-loop commits append to the candidate range, never amend. Plan corrections ride with the section commits or land as their own `todo:` commit first when substantial.
- **Never mark `[x]` without evidence.** The Implementation Order row flips only through `stamp`, after review. This skill never flips a row itself.
- **User data first.** Every file write follows the foundation plan's saving rule: temporary file in the destination folder, then atomic replace (`ReplaceFileW`, `MoveFileExW`, or `os.replace`). Read back what was written. Destructive paths are confirmed. The checkpoint exercises a failure path, not only the happy path.
- **The layer rules hold.** Respect the module boundaries and thread-safety contract in `AGENTS.md` and the foundation plan. A fix that violates the module table is wrong even when it works.
- **Fidelity is the bar on surfaces.** Build against `design/SPEC.md` and its screenshot, using tokens from `design/tokens/` only. No raw hex value on a control.

## Workflow

### 1. Read the whole contract

Read all of the output step 0's command printed (do not run it again; run it again only after the TODO file changes). It holds, as Markdown: the file's state (status, frozen, file dependencies) and why the section waits if it does; the file's own context word for word (title, Goal, Current state and its dated notes, Inputs, Outcome, Adjacency, the Implementation Order table), which the section assumes; the section word for word; the file's Verification block (gates the section may owe); one line per prerequisite with its effective state (`[x] settled`, `[ ] open`, or `[x] suspect` with the open section underneath), its `Verified:` date and commit and its receipt, which tell you what shipped; its waiting Needs; and every cross-reference and deferral in the tree that names the section or that it makes, with `path:line`. That output replaces reading the TODO file: do not open the file to read other sections. Read wider only in two cases: when a prerequisite's body (not only its stamp) decides the work, run `resolve '<prerequisite>' --context` on it; and when a cross-reference line needs its surroundings, read around that `path:line`. The whole file is read only by `process-todo-file` and `groom-plan`. Then read the governing excerpts named in Inputs (foundation plan sections, SPEC sections).

### 2. Validate the plan before building it

Eight questions, each answered against the repository and the governing documents rather than from memory:

| Check | What you are asking | The failure it catches |
| ----- | ------------------- | ---------------------- |
| Legitimacy | Does the plan, SPEC, or spec demand this? | Unwanted work invented by the plan |
| Currency | Do named files, classes, and APIs match? | Section pinned to a moved path |
| Consistency | Do items agree with Current state and deps? | Contradictory items in one file |
| Correctness | Do stated behaviors match the source/SPEC? | Stale behavior ported into code |
| Sufficiency | Can this build without inventing decisions? | Section becomes a design session |
| Verifiability | Can the checkpoint run and fail on fixtures? | Unfalsifiable checkpoint passes |
| Accuracy | Are paths, counts, refs, and deferrals true? | Authority citing moved facts |
| Ambition | Does it, as planned, beat the best competitor? | A plan that only reaches parity |

Ground each answer: `python scripts/todo-graph.py validate`, searches over `src/` and `tests/` for the named symbols, `git log --oneline -5 -- <touched paths>`. Check deferrals in both directions with `python scripts/todo-graph.py query deferred`: deferrals this section owns are part of its scope; deferrals this section wrote get re-read against today's tree (owner shipped means close it as `Resolved:`; description drifted means correct it; work quietly done elsewhere means verify it, close the deferral, and tick the owner's item).

Ambition (owner decision 2026-10-23, `AGENTS.md` Product ambition): look at how the best competitor does this job (Acrobat Pro first, then Foxit, PDFelement, UPDF). If the plan would only reach parity, raise it within the section's surface before building (speed, fewer steps, smarter defaults, better errors and recovery, keyboard paths, accessibility, polish, each with a measurable target), recorded as a dated `**Corrected YYYY-MM-DD:**` note, or file the larger improvement through `add-todo` (a change to the designed surface goes through a SPEC subsection and board first).

Anything unclear becomes a question answered from the source. Where the source is silent, take the industry-standard option, record that it is a default with the cost of changing it, and carry on.

### 3. Correct the section when validation fails

Fix findings in the TODO file, before implementation, so plan and build never disagree in the record. Stale facts are corrected in place and marked `**Corrected YYYY-MM-DD:**` with what they said before. Contradictions resolve toward the source and the later decision. Overlong `Deliverable` cells or `## N. <Title>` headings (`> 88` characters) are shortened to a concise noun phrase so raw Markdown table rows do not wrap. Unbuildable or coarse 1-bullet summary items are decomposed into concrete five-part micro-steps (one action, named path/symbol in backticks, `Done when:` in the same bullet, `Cheaper substitute:` on UI and write items, and source citation). Items already 100% implemented in the codebase are ticked with the XREF, `file:line`, or commit SHA that shipped them; half-implemented items stay `[ ]` and are rewritten to name the exact missing pieces. Genuinely wrong work is struck with a stated reason, never silently dropped. An unfalsifiable checkpoint is rewritten so it can fail. A **frozen** behavior that looks wrong is not changed in code or plan: record the question for the operator and continue around it. If the whole section is wrong, stop and report instead of implementing.

Correct the plan, never the goalpost: narrowing a section so the code you were about to write happens to satisfy it is fitting the contract to the implementation. Report every plan correction alongside the implementation. If step 2 finds nothing, say so in one line and move on.

### 3a. Plan review by the pinned reviewer (one pass, before any code)

Once the section is corrected, commit it (with `plan --sync`) so the reviewer reads the exact contract you will build. Check the pin first (`python scripts/review_pin.py --check`; its `[schedule] policy` is `three-pass`, which `review-todo-section` explains), and make sure the pin is committed: under a `three-pass` pin the section runs under the pin committed at this review's candidate, and a later pin change does not move it. Fill the prompt into a scratch file from `docs/plan-review-prompt.md` with `scripts/review-prompt.py` (passing `--paths <file> ...` when the pin has conditional lenses for `plan` and you know which files the section changes), then run the review in tracked background (`run_in_background` in Claude Code; `run_command` with `WaitMsBeforeAsync: 500` and `IsDaemon: false` in Antigravity), passing `--writer claude` or `--writer agy`:

```powershell
python scripts/review-prompt.py --kind plan --candidate <full SHA of that commit> --ref "D<NN> T<NN> §<N>" [--fidelity <design source>] --governing <the spec or plan> --out <scratch-plan-prompt.md>
python scripts/review-dispatch.py --kind plan --candidate <full SHA of that commit> --writer <claude|agy> --prompt <scratch-plan-prompt.md> --record docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md
```

The dispatcher sets the effort and the `plan` lens from the pin's `[schedule] plan` step and appends a `## Plan review` section; it is not a round, it does not move the round schedule, and a findings file takes one returned plan review. Under `three-pass` the reply must carry its `LENS plan: clean` or `LENS plan: findings` line and a `HIGHEST:` line, or it is recorded as no review. Fix every `P1` and `P2` gap in the section the same way as step 3 (corrected in place, dated), and file a `P3` you do not take through `add-todo`. Under the plan review in the findings file, add one line per gap: `- Planned: <gap> in <commit>` or `- Filed: DNN TNN §N (<gap>)`. When the dispatcher exits 1, run the pass again before building.

Then write `Started:`: add `> **Started:** <current UTC instant>` directly under the section heading and commit it (with `plan --sync`) before the first line of the build. On resume, never overwrite a `Started:` line that was committed after the section's plan review. A line committed before any plan review existed (a section started before the plan review, a session that paused between writing it and the review, or a section `unstamp` reopened) is replaced once with the current instant after the plan review, since the build start is the first commit holding the exact line (and, in a reopened section, every `Unstamped:` note). `stamp` requires this review as `--plan '<findings file>'`, naming the section's own findings file (the `--record` path above; another section's file refuses), refuses one whose candidate is later than the commit that records `Started:`, and refuses one run at another effort or lenses than the schedule's `plan` step; a section stamped before 2026-10-24 has no plan review and stays valid only as frozen in `docs/reviews/legacy-receipts.json`.

### 4. Build the corrected contract

Work the Build order and micro-step checklist top to bottom. Tick each micro-step item as its `Done when:` becomes true, in the file, as you go; never tick a partially finished item (`partial-flip-shipped` is fatal). If every micro-step in the section was already 100% implemented in the codebase, do not churn working code just to manufacture a diff: verify every `Done when:` and `Cheaper substitute:`, write any missing falsifiable test, run the checkpoint in Step 6, commit the ticked checklist and verification evidence in Step 7, and hand to `review-todo-section`. Write the failing test first where the section names one. Build the cheaper substitute's failure into the checkpoint so the wrong thing cannot pass. UI sections consume the shared controls and slots named in `**Chrome:**` (one `ShellPage`, named slots, Vellum controls from `libs/vellum` or `src/ui` until `D10 T04 §1`); a second toolbar, panel or dialog shape is a defect.

Run the iteration gate while building and after each fix, preserving the section's actual Test checkpoint commands: for C++ work, run `.\build.cmd -Platform x64 -Configuration Debug -TestFilter '<the section's tests>'` (with optional `-Project <vcxproj>` when narrowing compilation to a touched project, plus `-EnableASAN` for the section's ASan checkpoint when it names one); for Python-only work, run the section's Python `unittest` suites / `python scripts/run-tests.py --filter <pat>` and `python scripts/run-tests.py --quick` (never represent Python-only work as a GoogleTest `-TestFilter` or verify it solely with unrelated C++ tests); for mixed C++ and Python work, run both the filtered `.\build.cmd` iteration command and the section's Python suites. Use LSP (`clangd` / `pyright-langserver`) for instant type and symbol diagnostics while editing, and run the stamp gate (`.\build.cmd`, x64 Debug and Release, every test, alongside the section's own Test checkpoint commands) once on the final candidate tip that stamps (`review-todo-section` step 7, quoted in `--check`). Warnings are errors at the configured level.

### 5. Surface and feature completeness

Before the checkpoint, account for 100% of every control, menu item, dialog and state on the Fidelity screenshot and SPEC section: each is working (proven on the rendered surface with real data and read-back) or deferred to a named, resolving section (`python scripts/todo-graph.py resolve '<ref>'` must not exit 1 or 2). A control disabled with a reason that names no section is missing, not deferred. Confirm the refusal/error/recovery path and undo/reverse path required by the section hold. `review-todo-section` refuses the stamp for an unaccounted control, so decide here. This pass is yours; after the commit, `review-todo-section` has the pinned reviewer review the built feature (`--kind round`, then `--kind completion` of the final candidate, and `--kind reconciliation` if blocking gaps remain under the `three-pass` pin).

### 6. Run the Test checkpoint, for real

Execute the checkpoint command and read the output. Quote the result in the commit body. If the checkpoint cannot run (no host, no fixture, no corpus file), the section is not done: record what ran, what did not and why, and stop without a stamp. Then run the section's other owed local gates: affected tests green, touched build warnings clean, `python scripts/todo-graph.py validate` clean, and the `**Freeze check:**` in frozen files.

### 7. Commit, push to CI, then hand to review

Commit the section work (in one or more commits as steps complete), recording the checkpoint evidence in the final implementation commit body:

```text
<area>: <what now works> (<DNN TNN §N>)

<checkpoint output, quoted>
<plan corrections, if any>
```

Push the candidate commit to `main` and start `python scripts/ci_status.py --wait <pushed sha> --since <before>` in tracked background so GitHub Actions runs the heavy full matrix (`build.yml` x64 + ARM64 + ASan and `plan.yml` `--full`) in parallel while you invoke `review-todo-section` on the same ref. Under the `three-pass` pin, `review-todo-section` runs one round (`--kind round --candidate <tip>`), applies fixes and re-runs the iteration gate, runs the completion and excellence review (`--kind completion --candidate <tip>`), reconciles any blocking gaps (`--kind reconciliation`) or files non-blocking `P3` gaps via `add-todo`, runs the stamp gate (`.\build.cmd`, x64 Debug and Release, every test, alongside the section's own Test checkpoint commands) once on the final candidate tip, checks `python scripts/ci_status.py --gate` is green, and writes the stamp through `stamp --plan --completion` (quoting the stamp gate and section checkpoint in `--check`), which flips the row and syncs the plan. Commit the stamp and the regenerated plan as the stamp commit (`todo: stamp <ref>`).

### 8. Report

Tell the user plainly: what was built, what the checkpoint proved (quoted), what plan corrections were made, what was filed rather than fixed, and where the stamp stands. End with the `Layer:` line the standing rules require.

## Guardrails

- Do not implement a section you judged wrong. Correct it or stop.
- Do not widen the section. File adjacent work.
- Do not flip the Implementation Order row or hand-write `Verified:` lines. `stamp` owns both.
- Do not claim a checkpoint passed without running it: quote the output.
- Do not build a UI surface the SPEC does not describe. The SPEC change ships first.
- Do not end with the plan unsynced.
