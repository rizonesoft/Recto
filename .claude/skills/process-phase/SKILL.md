---
name: process-phase
description: Attended runner that takes one phase of todo/implementation-plan.md to 100 percent. Repair the phase, gap-check it, then ship section after section via process-todo-section plus review-todo-section, parking only when every leftover row is blocked here. Use when the user says process, run, or finish a phase.
---

# Process Phase

One phase, start to 100%, or parked when the rest of it cannot run here. You do not stop in between.

Exactly three endings: zero open rows and a written closeout; every leftover row blocked or waiting in this context, so the phase is parked and `process-plan` moves to the next ready phase; or the operator's own pause. A parked phase is neither complete nor a stall.

The whole plan is `process-plan`, not this skill. When the session entered through `process-plan`, return to it after closeout or park; a session pinned to one phase ends here.

Attended means interruptible, not stoppable: answer a mid-run message briefly and continue the loop in the same turn. The one exception outranks everything: if the user tells you to stop or pause, obey immediately, record it in the run file, end the run guard (`build\python.cmd scripts/campaign_guard.py end --session <session> --reason operator-stop`, where `<session>` is `$CLAUDE_CODE_SESSION_ID` in Claude Code bash, `$env:CLAUDE_CODE_SESSION_ID` in Claude Code PowerShell, or the literal `Conversation ID` from `<user_information>` in Antigravity as everywhere this skill names `<session>`, then delete the heartbeat job it names via `CronDelete` or `manage_task` `kill`), and wait.

Completion never buys completion with a bypass: no `--no-verify`, no amend of a candidate under review, no force-push. A red gate is work, not a skip.

## Step 0: open the run

Read `todo/README.md` (the format every TODO edit keeps) before the run's first write, then check that no other writer holds the tree (`git status`; ask about unfamiliar uncommitted work). Writes are serial: this run is the only writer. Open the run file `docs/phase-runs/<YYYY-MM-DD>-phase-<N>.md` (create `docs/phase-runs/` if absent) and append every finding the moment it is made: the file survives session death where chat scrollback does not. A phase started on its own (not from `process-plan`) starts the run guard first, as `process-plan` describes, with `--scope phase`, so its heartbeat resumes this phase and never the rest of the plan; one started from `process-plan` already has it.

```markdown
# Phase run: <heading>

## Phase repairs              (Step 1: what was wrong with the plan, what was corrected, where)
## Shipped-row verification   (Step 1b: each spine [x] row, stamp and re-run checkpoint result)
## Gap audit                  (Step 2: gaps found, where each was filed; the park record lands here)
## Sections                   (Step 3: one entry per section: ref, outcome, review verdict)
## Critical events            (every stop, pause, resume, session death)
## Lessons                    (what was learned this run, worth keeping)
```

Read the most recent prior run file for this phase, if one exists: anything unresolved there is this run's first input.

## Step 1: repair the phase before running it

1. `build\python.cmd scripts/todo-graph.py validate`: fix every refusal now.
2. `build\python.cmd scripts/todo-graph.py plan --check`: if stale, `plan --sync`.
3. For every open row in the phase: `build\python.cmd scripts/todo-graph.py resolve '<ref>'`. Record the exit code. Exit 4 with unmet dependencies outside this phase, or a waiting Need this session cannot satisfy (`owner-decision`, `third-party`, `operator`, or a host that is not reachable), is a leftover, not a stall: leave the row, ship every exit-0 row, and park when only leftovers remain. Exit 1 or 2 means the plan cites a section that does not exist: repair the reference against the TODO files; a broken reference blocks a park.
4. Read each open section's TODO file top to bottom for phase-level staleness only (per-section validation happens again inside `process-todo-section`): work already shipped elsewhere, sections made moot by a later decision, Current state blocks contradicted by the tree. Correct with dated `**Corrected YYYY-MM-DD:**` notes.

### Step 1b: shipped rows are verified, not trusted

`[x]` rows are claims. Re-check the shipped rows an open row in this phase depends on (the spine): `resolve` exits 3, the receipt matches its stamp, and the section's Test checkpoint still passes when re-run. A checkpoint that no longer passes means the section regressed: treat it as this phase's work through `review-todo-section` in audit stance before building on it.

## Step 2: gap-audit the phase for 100% completeness

Read the phase as a user would use it, end to end, against its governing documents (the milestone "Done when" in the foundation plan, the SPEC sections the phase implements). Ask what is missing for a 100% complete phase: screens, dialogs, controls, settings consumers, refusal/error paths, undo/reverse paths, persistence read-backs, or handoffs between domains nobody specified. Split any coarse umbrella sections into small, single-responsibility sections and expand any summary checkboxes into concrete five-part micro-steps. File each gap with `add-todo` (evidence, owner, dependency) and sync the plan. Never cap planning or silently drop scope. A gap found is a gap filed the same turn.

## Step 3: ship the phase, one row at a time

In the generated plan's table order, for each open row: `process-todo-section`, then `review-todo-section` (under `.conclave/reviewers.toml`'s `three-pass` pin: `--kind plan` before code, `--kind round` after build, `--kind completion` on the final candidate, and `--kind reconciliation` if blocking gaps remain, stamped with `stamp --plan '<findings file>' --completion '<findings file>:<complete|reconciled|filed>'`). Run the iteration gate while implementing and after each review fix, preserving each section's actual Test checkpoint commands (`.\build.cmd -Platform x64 -Configuration Debug -TestFilter '<the section's tests>'` with optional `-Project <vcxproj>` and `-EnableASAN` when named for C++ work; the section's Python `unittest` suites / `build\python.cmd scripts/run-tests.py --filter <pat>` and `build\python.cmd scripts/run-tests.py --quick` for Python-only work without fabricating a GoogleTest `-TestFilter` or relying solely on unrelated C++ tests; both for mixed C++ and Python work), and run the stamp gate (`.\build.cmd`, x64 Debug and Release, every test, alongside the section's own Test checkpoint commands) once on the final candidate tip that stamps, quoted in `--check`, while pushing candidates so GitHub Actions (`ci_status.py --wait`) runs the full x64/ARM64/ASan matrix in parallel with review. Every gap filed becomes a new section through `add-todo` in this phase or the one that owns it. Record each outcome in the run file, with the completion verdict and any sections filed. Commit the section work, then its stamp commit. After the stamp commit is pushed, run `build\python.cmd scripts/merge_prs.py` before claiming the next row: it merges green Dependabot and pins pull requests only when the tree is clean, `HEAD` is `origin/main` and no section is in progress, one at a time, and leaves any that fails for the operator. Record its last line in the run file when it merged or stopped.

CI is watched after every push, not only at a boundary (owner direction 2026-11-09, `D00 T04 §9`): record `origin/main` before pushing (`before`), push, then start `build\python.cmd scripts/ci_status.py --wait <pushed sha> --since <before>` in the background (`run_in_background` in Claude Code; `run_command` with `WaitMsBeforeAsync: 500` and `IsDaemon: false` in Antigravity). It follows the runs that push starts, reading every 10 seconds, prints each change of state, and writes `build/ci-watch/<sha>.json`; it ends the moment the verdict is known, which for a red is the poll that sees it, even while another workflow still runs. Its last line is `ci watch: exit <n> for <sha>. next: <what to do, with the command>`: do what that line says. Work on the current row may go on while the watch runs, and its notification is acted on when it arrives: exit 1 is the red-first rule below at once (finish or stop the step in hand at its next safe point, never during a review), exit 5 (a cancelled run) is one `gh run rerun <id>` watched again on the same sha, exit 3 or 4 a second watch (`--wait <sha>` again; it keeps the first watch's `--since` by itself, and with no earlier watch it reads the push's range from GitHub) and, when the second ends the same way, a Critical event; exit 3 with `cannot write <path>` means the state file could not be written (free disk space or close the program holding the file, then run the same command again), never a red; exit 6 means another watch of that commit is still alive, so none was started. Before claiming the next row, and before any stamp, run `build\python.cmd scripts/ci_status.py --gate`: 0 lets it go (the watch ended green and GitHub still lists that commit's runs green); 1 is red; 6 is anything else, and its line ends with `next:` and the exact command (wait for a running watch, start one, pull a head this clone has not fetched, rerun a cancelled run); 3 means git or `gh` could not answer, which is unknown and never green. Do what the `next:` part says, and never start a second watch beside a live one.

Then check CI, red first: run `build\python.cmd scripts/ci_status.py` (the latest completed `build.yml` and `plan.yml` runs on main, and whether newer runs are still in progress) before claiming the next row.

- Exit 0: no watched workflow is red; carry on.
- Exit 1 (red): red comes before any new row. Read the failed job's log, bounded (`gh run view <id> --log-failed`, the last 200 lines into scratch). If it is an infrastructure or flaky failure (a runner outage, a network or download failure, or a test the run file already records as flaky), run `gh run rerun <id> --failed` once and record it; a second red on the same commit is a real failure. Otherwise diagnose and fix it at the owning layer: a fix commit with a regression test that fails before it, or a section through `add-todo` when the fix is real scope (that section is then the next row). Never skip, disable or loosen a check, test or workflow to make CI green.
- Exit 3 (`gh` missing, unauthenticated or failing): the state is unknown, never green. Record it in the run file and carry on; the next boundary checks again.
- Record every red in the run file's Sections or Critical events: workflow, commit, run URL, failed jobs, the cause and the outcome (rerun green, fix commit, or the section filed).

Long jobs never stall the run (owner direction 2026-10-23, after a review job died silently and a build was killed for memory):

- One heavy job at a time: a build, the AddressSanitizer build, a review round and a long test suite never overlap, because other sessions share this machine. Before each, read `(Get-Counter '\Memory\Available MBytes').CounterSamples[0].CookedValue`; under 3000 MB, wait (re-check every minute, up to 30 minutes) instead of starting a job the host may kill.
- A review runs alone: nothing touches a tracked file while a reviewer reads the tree. Launch `scripts/review-dispatch.py` through the runner's tracked background option (`run_in_background` in Claude Code; `run_command` with `WaitMsBeforeAsync: 500` and `IsDaemon: false` in Antigravity), which `scripts/dispatch_hook.py` enforces (a foreground, daemon or detached run is refused, because only a tracked background task tells the session when it finishes), then end the turn and wait: while `build/review-in-flight/` names a live dispatcher, the Stop hook lets the turn end without a push and without counting toward the breaker, and the review's notification resumes the row. Stage `docs/reviews/` with the next commit, since the dispatcher appends to it.
- After launching a background job, confirm within a few minutes that its process exists and its output file is growing or it has finished. Never wait on a job whose process is gone: a job that ended without its result (killed, no output, no notification) is relaunched once, alone, and the relaunch recorded in the findings file and the run file; a second loss is a Critical event, and the row continues with the next step that does not need that result or parks as a leftover.
- An idle wait longer than 15 minutes with no job alive is a stall: re-check the job, resume the row, and record it.

Skip rows whose `resolve` is not exit 0 here, and rows whose Needs name a host this session cannot reach (`resolve` and `query ready` do not know which hosts are reachable, so a host-bound row can exit 0 and still be a leftover; step 1.3 records which), and re-check them after each stamp: the graph moves as rows flip. When every remaining open row is a leftover, the phase parks: write a column-0 `PARKED <UTC stamp> run=<run id> <one-line reason>` line in the run file (the run id `acquire` printed, so the Stop hook lets the turn end) followed by the park record (each leftover, what blocks it, where the blocker lives), commit the run file, and return to `process-plan`. A phase started on its own ends its guard here: `build\python.cmd scripts/campaign_guard.py end --session <session> --reason park`, delete the job it names (`CronDelete` or `manage_task` `kill`), confirm by listing jobs (`CronList` or `manage_task` `list`), then `cancel-confirmed`.

## Step 4: closeout

When the phase table is all `[x]`: `process-todo-file` on any file the phase exhausted, re-run the full test suite once, confirm `query stats` and the regenerated plan show the phase complete, write the closeout under a `## Closeout run=<run id>` heading in the run file (what shipped, what was repaired, what was learned), commit, and report. A phase started on its own ends its guard before the report: `build\python.cmd scripts/campaign_guard.py end --session <session> --reason closeout`, delete the job it names (`CronDelete` or `manage_task` `kill`), confirm by listing jobs (`CronList` or `manage_task` `list`), then `cancel-confirmed`. A phase is complete when its table says so and the closeout is written: not before.

## Guardrails

- Do not invent a side loop that ships rows outside `process-todo-section` plus `review-todo-section`.
- Do not tick `implementation-plan.md` by hand. It is generated; sync it.
- Do not claim a phase complete while its table has `[ ]` rows, and do not call a parked phase complete or a stall.
- Do not end the turn on the audit. Ship, park, or close out.
- Do not claim a new row while `scripts/ci_status.py` reports main red, and never make CI green by skipping, disabling or loosening a check.
