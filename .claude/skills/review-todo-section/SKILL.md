---
name: review-todo-section
description: Quality-gate a just-implemented TODO section with self-review, independent review lenses and a bounded fix loop, then write the Verified stamp through the stamp command. Also runs in audit stance on shipped sections. Use after process-todo-section or when asked whether a section is really done.
---

# Review TODO Section

The gate between "code exists" and "the row says `[x]`". Nothing else may flip that row, and the flip happens only through `build\python.cmd scripts/todo-graph.py stamp`, which refuses a section with unticked items, an open prerequisite, a waiting Need, or no review evidence.

Review is mandatory and independent: the implementing session self-reviews, and every external pass comes from the reviewer pinned in `.conclave/reviewers.toml`: GPT-6 Astra through the Codex CLI, of a different model family from the writer, run through `scripts/review-dispatch.py` (Claude Code `claude` and Google Antigravity `agy` are the writers, owner directions 2026-11-10, 2026-11-12 and 2026-11-13; one reviewer, owner decision 2026-10-28, after the Astra and Terra panel, and the same one for the writers `claude`, `agy` and `muse`, owner decision 2026-11-10). The pin's `[schedule]` owns each pass's reasoning effort and lenses, so the writer never picks them. Sections stamped while the panel ran keep their `<file>.terra.md` records, which their receipts name. Record the writer's decisions as settled items in the findings files (`- Filed: DNN TNN §N (...)` and `- Not taken: <item> (<reason>)` under the review or a `## Writer note: settled` heading): the dispatcher gives them to every reviewer, so a repeated one never keeps a loop running. Every `scripts/review-dispatch.py` invocation MUST run in tracked background (`run_in_background` in Claude Code; `run_command` with `WaitMsBeforeAsync: 500` and `IsDaemon: false` in Antigravity). A verdict with no record is an opinion.

## Use this skill when

- A section was just implemented (`process-todo-section` ends by invoking this).
- The user asks "is §N really done?", "re-verify §N", "audit §N": run in audit stance (below).
- Before closing a TODO file with `process-todo-file`.

## Step 0: resolve the argument

Same front door as `process-todo-section`, run once: `build\python.cmd scripts/todo-graph.py resolve "$ARGUMENTS" --context`. Exit 3 means the row is already `[x]`: that is the signal for audit stance, not an error. The one call gives the canonical reference (its first line, `# resolve --context DNN TNN §N`), the section's contract and everything it needs from its file (the file's Goal, Outcome and Verification block, each prerequisite's effective state and receipt, the cross-references and deferrals that name it): read that output in place of the TODO file.

## Which review schedule applies

Check the pin before the first pass (`build\python.cmd scripts/review_pin.py --check`, `build\python.cmd scripts/review_pin.py --lenses <kind>`, `build\python.cmd scripts/review_pin.py --caps`).

- **Three-pass** (`policy = "three-pass"`, Recto's active schedule in `.conclave/reviewers.toml`): a plan review before the build (`process-todo-section` step 3a), one round after it, one completion review of the final candidate, and reconciliations only after blocking gaps. Step 3 and the three-pass stamp in step 7.
- **Rotation** (no `policy` key, kept on historical pre-three-pass sections whose plan review began under rotation): a plan review, a completion review before the rounds, then rounds until one has no `P1` or `P2` finding. Step 4 and the rotation stamp in step 7.

A section runs under the pin committed at its plan review's candidate (schedule and route both), so a pin change made later does not move a section already planned: finish it under the flow it began on.

## The three questions

Every review answers these with code and behavior evidence, never assertion:

1. **Does it make sense?** Does the implementation produce the exact outcome the section asked for, without unnecessary scope?
2. **Is it logical?** Do the contracts agree: threading rules, error paths, settings consumers, UI states, failure behavior?
3. **Did it break anything else?** Inspect callers, consumers, tests and adjacent workflows that read what changed.

## Workflow

### 1. Fix the candidate

Name the candidate: the implementation commit, or the commit range (`<BASE_SHA>..<TIP_SHA>`) when the section landed across multiple logical step commits or when fix-loop commits append (commits are never amended once review starts). Record the full SHA(s) in the findings file at `docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md`. For an audit, the candidate is the section's own stamped commit plus every later change to the files it owns, up to today's tip (`<STAMPED_SHA>..HEAD -- <owned paths>`): a regression lives in those later changes. Read the actual diff (`git show` or `git diff <BASE_SHA>..<TIP_SHA>`), not memory of writing it. Map each ticked micro-step checklist item (its `Done when:` and `Cheaper substitute:`) to the code and test that satisfy it; an item with no corresponding change or a partially met `Done when:` (`partial-flip-shipped`) is not done (untick it) or was already true (note where). On a stamped section (audit stance) edit nothing in the section by hand: record the item in the findings file, and when it is a regression, run `unstamp` first; the item is unticked in the reopened section afterwards.

### 2. Self-review

Work the three questions across the diff and its blast radius. Fix what you find, then re-ask them. Look specifically for the failure modes this codebase invites:

- The failure modes `AGENTS.md`, the foundation plan and `design/SPEC.md` call out for the area you touched: list them in the findings file and check each one.
- An error path that throws, aborts or swallows instead of returning `PdfResult<T>` / `PdfError`, or a failure reported as success.
- Work on the wrong thread, mutable state shared across threads, or unbounded recursion or allocation on hostile input.
- A file write that is not temp-then-replace, or an original modified in place.
- A hard-coded colour, size or glyph where design tokens, theme roles or `RectoIcons` own it, or a control without an accessible name and tooltip.
- A setting nothing reads, a command unreachable from `Ctrl+K`, or a link or attachment that opens without asking.
- A test that passes without exercising the behavior.

### 3. Review passes under the three-pass pin

The passes run in this order for `docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md`. Each pass is on a commit descending from the one before, the dispatcher refuses a pass out of order before a reviewer runs, and it refuses a second pass while one is running. Push the candidate to `main` and watch GitHub Actions (`build\python.cmd scripts/ci_status.py --wait <sha> --since <before>` in tracked background) so heavy full-matrix builds and suites run on GitHub in parallel with Astra's review.

1. **Plan review**, before the build (`process-todo-section` step 3a): `--kind plan --candidate <commit holding the section>`.
2. **The round**, after the build, its local gates and its commit. Fill the prompt into a scratch file from `docs/review-round-prompt.md` with `scripts/review-prompt.py`, then run `scripts/review-dispatch.py` in tracked background with `--surface` on a `Surface: yes` section (and `--paths <changed files>` when `[[schedule.conditional]]` applies):

```powershell
build\python.cmd scripts/review-prompt.py --kind round --candidate <full tip SHA> --ref "D<NN> T<NN> §<N>" --purpose "<one or two sentences>" --governing <the spec or plan> --out <scratch-round-prompt.md>
build\python.cmd scripts/review-dispatch.py --kind round --candidate <full tip SHA> --writer <claude|agy> [--surface] --prompt <scratch-round-prompt.md> --record docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md
```

   The dispatcher appends a `## Round 1` section and the pin's lenses. Verify every finding against the code, fix what is real in new commits appended to the candidate, record why anything else is not real under a `## Writer note:` heading, and re-run the touched gates (step 5). There is no second round: the dispatcher refuses one.
3. **The completion and excellence review** of the final candidate: fill the prompt from `docs/completion-review-prompt.md` with `scripts/review-prompt.py` (passing `--fidelity` on `Surface: yes` sections) and run in tracked background:

```powershell
build\python.cmd scripts/review-prompt.py --kind completion --candidate <full tip SHA> --ref "D<NN> T<NN> §<N>" [--fidelity <design source>] --purpose "<one or two sentences>" --governing <the spec or plan> --out <scratch-completion-prompt.md>
build\python.cmd scripts/review-dispatch.py --kind completion --candidate <full tip SHA> --writer <claude|agy> --prompt <scratch-completion-prompt.md> --record docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md
```

   Part 1 lists every control, menu item, toolbar entry, `Ctrl+K` command, dialog, state, setting consumer, error and refusal path, undo or reverse path and persistence read-back the feature needs (`present`, `missing-in-scope` or `missing-out-of-scope`). Part 2 names how the strongest competitor (Acrobat Pro first, then Foxit, PDFelement, UPDF) does the job and lists where Recto's feature falls short of beating it (`in-scope` or `out-of-scope`). It ends `COMPLETION: complete` or `COMPLETION: gaps`.
4. **Reconciliation after blocking gaps.** On `COMPLETION: gaps` whose `HIGHEST:` is in `blocking` (`P1` or `P2`), implement every in-scope gap in new commits without changing the section's checklist text, add `- Implemented: <gap> in <commit>` lines under the review, re-run the touched gates, then fill from `docs/reconciliation-review-prompt.md` and run `--kind reconciliation` in tracked background:

```powershell
build\python.cmd scripts/review-prompt.py --kind reconciliation --candidate <full new tip SHA> --ref "D<NN> T<NN> §<N>" --failed <the failed completion's SHA> --findings docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md --gates <scratch-gates.txt> --governing <the spec or plan> --out <scratch-reconciliation-prompt.md>
build\python.cmd scripts/review-dispatch.py --kind reconciliation --candidate <full new tip SHA> --writer <claude|agy> --prompt <scratch-reconciliation-prompt.md> --record docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md
```

   File each out-of-scope or non-blocking (`P3`) gap through `add-todo` as a new section (surface changes go through a `design/SPEC.md` subsection and board first, the `D06 T04` way) and add `- Filed: DNN TNN §N (<gap>)` under the review that raised it (and `> **Deferred:** <what> -> XREF: DNN TNN §N` in the section when its surface shows it). Under a last pass that ended complete write no `- Filed:` line: `stamp` takes `- Filed:` lines under the last returned pass only with `:filed`.

Every reply under `three-pass` must carry `LENS <name>: clean` or `LENS <name>: findings` for each scheduled lens and `HIGHEST: P1|P2|P3|none` before the final verdict lines. Headings starting `## Round`, `## Completion review`, `## Plan review` or `## Reconciliation review` are reserved for `--record`.

- **Incomplete or outage reply (exit 1):** does not count toward the cap; run the same command again.
- **Last pass ended gaps with `HIGHEST: P3` (below `blocking = ["P1", "P2"]`):** file each finding via `add-todo`, add `- Filed: DNN TNN §N (<what>)` under that last review, and stamp with `--completion '<findings file>:filed'`.
- **Blocking gap (`P1` or `P2`):** repair in new commits, re-run gates and run `--kind reconciliation` until one ends `RECONCILIATION: complete`, then stamp with `:reconciled`.
- **Cap reached (`pass_cap = 12` or `no_progress_cap = 2`):** leave unstamped, record under `## Cap reached` in the findings file and in the run file, and continue with the phase's other ready rows.

### 4. Review passes under a rotation pin

Skip this step on a `three-pass` pin. For a section whose plan review began under a rotation pin: run `--kind completion` first, implement every in-scope gap and excellence item, file out-of-scope items via `add-todo`, then run `scripts/review-dispatch.py --writer <claude|agy> [--surface] --prompt <scratch> --record <findings>` rounds up to 33 until a round has no `P1` or `P2` finding (`clean`, `fixed`, `stopped-low-severity` or `stopped-at-33` with `--cap-outcome`).

| Lens | Asks |
| ---- | ---- |
| `plan` | Will the section as written produce a 100% complete feature? (plan review only) |
| `plan-delivery` | Does the build deliver every item the plan promised, with nothing quietly narrowed? |
| `adversarial` | How does this fail on hostile input, races, aborts, automation or links? |
| `consistency` | Does it match the module rules, naming, shared components and settings? |
| `integration` | Do callers and consumers still hold, with every contract kept? |
| `source-defect` | Is the upstream spec or SPEC read accurately with deviations noted? |
| `quality` | Is the code clear, tested where it can fail, and free of dead or duplicated logic? |
| `design` | Does the rendered UI match the design source and its tokens? |
| `optimization` | Is there avoidable work, allocation, latency or step count on a hot path? |
| `record` | Does evidence match what ran, with owned deferrals and earned flip? |

When rounds keep finding spellings that slip past a home-made scanner of something a tool owns (includes, link inputs, symbols, encodings, file formats), move the proof to that tool's own output (`/sourceDependencies`, `dumpbin`, `lib /list`, an OS API) and keep the scanner only as an early warning.

Architecture check: on sections touching an architectural boundary the foundation plan names (process or thread contracts, data ownership, module seams), one findings-file paragraph answers whether the change keeps the foundation plan architecture intact, naming any decision it effectively revises.

### 5. Re-run the gates and check CI

After each fix, run the iteration gate while preserving the section's actual Test checkpoint commands: for C++ work, run `.\build.cmd -Platform x64 -Configuration Debug -TestFilter '<the section's tests>'` (with optional `-Project <vcxproj>` when narrowing compilation to a touched project, plus `-EnableASAN` for the section's ASan checkpoint when it names one); for Python-only work, run the section's Python `unittest` suites / `build\python.cmd scripts/run-tests.py --filter <pat>` and `build\python.cmd scripts/run-tests.py --quick` (never represent Python-only work as a GoogleTest `-TestFilter` or verify it solely with unrelated C++ tests); for mixed C++ and Python work, run both the filtered `.\build.cmd` iteration command and the section's Python suites, plus `build\python.cmd scripts/todo-graph.py validate` and the Freeze check in frozen files. Push the final candidate tip and verify `build\python.cmd scripts/ci_status.py --gate` exits 0 so GitHub Actions confirms the full matrix. Quote the checkpoint output in the findings file. A red local gate or red CI reopens the candidate: fix at the owning layer, re-run to green, and re-run the affected review pass.

### 6. Surface, feature, and frozen checks

Surface and feature completeness: 100% of every control, menu item, dialog and state on the Fidelity counterpart is working (proven on the rendered surface with real data and read-back in this review) or deferred to a named, resolving section, and every required refusal/error/undo path is exercised. Refuse the stamp for an unaccounted control or a `Cheaper substitute` shortcut. Frozen files: every `**Freeze check:**` ran and passed with the result quoted; a frozen behavior that moved without a recorded operator approval means no stamp and a question for the operator.

### 7. Stamp

Before stamping, run the stamp gate once on the candidate tip that stamps: `.\build.cmd` (x64 Debug and Release, every test) alongside the section's own Test checkpoint commands (including its Python `unittest` suites on Python-only or mixed sections and its ASan checkpoint when named), and quote both the section's Test checkpoint and the full-suite `.\build.cmd` result in `--check` arguments to `stamp`. Tick nothing by hand. Confirm every checklist item except `Commit:` is `[x]`, then under the `three-pass` pin (with the last returned pass having reviewed the stamped commit):

```powershell
build\python.cmd scripts/todo-graph.py stamp 'DNN TNN §N' --sha <full tip SHA> --plan 'docs/reviews/<domain>/D<NN>-T<NN>-s<N>.md' --completion 'docs/reviews/<domain>/D<NN>-T<NN>-s<N>.md:<complete|reconciled|filed>' --review '<N>:docs/reviews/<domain>/D<NN>-T<NN>-s<N>.md:<clean|fixed>' --check '<checkpoint command> -> <quoted result>' --check '.\build.cmd -> <quoted x64 Debug and Release full-suite result>'
```

Under `three-pass`, `--completion` is `complete`, `reconciled` or `filed` (`completed`, `--cap-outcome` and `stopped-` verdicts are rotation-only), and `--review` names the one returned round (`1` unless an earlier attempt was an outage or incomplete reply) with `clean` or `fixed`. Under a historical rotation pin, `--completion` takes `complete`, `completed` or `filed`, `--review` lists every round, and stopped verdicts carry `--cap-outcome`. `stamp` writes `docs/reviews/<ref>.json`, ticks the `Commit:` item and Implementation Order row, appends `Verified:` and `Review:`, marks the file `done` when its last section stamps, and syncs the plan. Commit the stamp, receipt, findings file and regenerated plan together as `todo: stamp <ref>`. Add any `Deferred:` lines (each with `-> XREF:`) and `Started:` to `Duration:` notes before stamping.

### 8. Audit stance

On an already-`[x]` section: run steps 1, 2, 5, 6 and standalone `--kind round` passes recorded in `docs/reviews/<NN-domain>/audit-d<NN>-t<NN>-s<N>.md` (never in the section's stamped findings file), changing nothing in the section itself until `unstamp` has reopened it. Stop when a round's summary shows `"blocking": false`. If a regression is confirmed, file the repair with `add-todo` first, then run `build\python.cmd scripts/todo-graph.py unstamp '<ref>' --reason '<what regressed; repair -> XREF: <ref>>'`. Once `unstamp` is committed, a reopened section's own findings file is `D<NN>-T<NN>-s<N>-cycle<n+1>.md` (`n` being its `Unstamped:` notes), and downstream stamped sections are suspect until it re-stamps. A re-confirmed row keeps its stamp; say so in one line.

## Guardrails

- Do not stamp without a findings file, its one plan review, its one round, and its completion review (`complete`, `reconciled` or `filed` within blocking levels under `three-pass`).
- Do not run a second round under a `three-pass` pin, use a stopped verdict or `--cap-outcome` under it, or stamp at `pass_cap` with blocking gaps open.
- Do not stamp an unaccounted control on a surface section, or a moved frozen behavior.
- Do not hand-write `Verified:` lines, hand-flip rows, or edit a receipt. `stamp` owns the record.
- Do not review the working tree when the candidate is a commit or range. Name the hashes.
- Do not transfer evidence across changed candidates: a new tip re-runs its gates.
