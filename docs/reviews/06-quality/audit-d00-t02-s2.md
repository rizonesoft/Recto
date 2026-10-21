# Audit - D00 T02 §2: Skills, rules and the CI gate

- **Audit section:** `D06 T05 §6` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T02 §2` (`docs/reviews/d00-t02-s2.json`, 2 rounds under the earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

At `fb739e6`, `muse skills validate <dir> --json` for every folder under `.claude/skills`:

- `add-todo valid=True diagnostics=0`
- `authoring-skills valid=True diagnostics=0`
- `create-todo valid=True diagnostics=0`
- `groom-plan valid=True diagnostics=0`
- `independent-review valid=True diagnostics=0`
- `local-tool-safety valid=True diagnostics=0`
- `process-phase valid=True diagnostics=0`
- `process-plan valid=True diagnostics=0`
- `process-todo-file valid=True diagnostics=0`
- `process-todo-section valid=True diagnostics=0`
- `review-todo-section valid=True diagnostics=0`
- `scheduled-automations valid=True diagnostics=0`

`python scripts/todo-graph.py validate` -> `valid: 677 sections`. The checkpoint's "all eight skills" is now 12 skill folders; every one validates.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:22:19Z
- Effort: medium
- Lines: 26
- Digest: 1b719037a5eb7adf

## adversarial: needs-attention

1. **P2: Audit instructions corrupt the graph’s stamp consistency.** `.claude/skills/review-todo-section/SKILL.md:91` instructs changing a stamped `[x]` row to `[ ]` while retaining its evidence. I reproduced this entirely in memory against D00 T02 §2: `graph()` refuses with `checkbox/stamp/receipt mismatch: D00 T02 §2`. This blocks subsequent graph operations. Removing the stamp markers also cannot enable the prescribed restamp while the receipt exists: `scripts/lib/todo_graph.py:783` refuses it. The original review explicitly recorded this instruction as fixed; it has regressed.

## consistency: needs-attention

2. **P3: Grooming gives mutually exclusive checkbox instructions.** `.claude/skills/groom-plan/SKILL.md:27` tells the executor to tick already implemented micro-steps, while lines 8 and 56 prohibit ticking any box. Reproduce by following the drift sweep on an open checklist item whose implementation already exists: either action violates the same skill.

## integration: needs-attention

3. **P2: Audit stance targets historical code when its consumer needs current regression detection.** `.claude/skills/review-todo-section/SKILL.md:34` explicitly selects the stamped commit, “not today's tree”; line 98 reinforces that restriction. However, `.claude/skills/process-phase/SKILL.md:44` routes a checkpoint that fails today into this audit stance. Reproduce with a regression introduced after a section’s stamp: the prescribed historical candidate excludes the defective change. Rerunning today’s checkpoint and reviewing yesterday’s implementation cannot establish the cause or verify its repair.

## source-defect: approve

## design: advisory

Not applicable: the section declares `Surface: no`.

## record: needs-attention

4. **P3: The two advertised copies of the authoring template have diverged.** `.claude/skills/create-todo/SKILL.md:18` and `todo/README.md:17` describe them as the same file. Byte comparison returns `False`. Starting at `todo/templates/TODO-template.md:35`, the canonical template lacks the skill copy’s added section-scoping, refusal/readback and regression-test prompts. Reproduce by comparing it with `.claude/skills/create-todo/todo-template.md`. Authors receive different guidance depending on their entry point.

The findings file’s checkpoint rerun is reproducible: all 12 skill directories report `valid: true` and `diagnostics: []`; graph validation reports `valid: 677 sections`; `plan --check` reports `plan current`. These checks do not detect the defects above. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 1

`python -m unittest tests.scripts.test_todo_graph` -> `Ran 81 tests`, `OK` (the unstamp and template tests fail against `fb739e6`); `muse skills validate` -> `valid=True` for the changed skills in all three kits; `valid: 677 sections`; `plan current`.

- Finding 1 (P2): the audit stance had no lawful way to reopen a regressed row: a hand flip leaves the graph refusing a checkbox/stamp mismatch. `scripts/lib/todo_graph.py` gains `unstamp` (CLI `todo-graph.py unstamp '<ref>' --reason ...`): it removes the stamp lines, unticks the `Commit:` item and the row, reopens the file, keeps the receipt as `docs/reviews/<ref>.revoked-<date>.json`, adds a dated `Unstamped:` note and syncs the plan, rolling everything back on any failure or interrupt; a re-stamp then writes a fresh receipt (`test_unstamp_reopens_a_regressed_section_and_allows_a_restamp`, `test_an_interrupted_unstamp_rolls_back`). The `review-todo-section` skills and `todo/README.md` name it as the only way to flip a row back.
- Finding 2 (P3): `groom-plan` no longer ticks: an already-shipped micro-step gets an `Already shipped: <SHA or file:line>` note and stays `[ ]` for `process-todo-section` to re-verify and tick.
- Finding 3 (P2): an audit's candidate is now the stamped commit plus every later change to the files the section owns, up to today's tip, so a regression introduced after the stamp is inside what is reviewed.
- Finding 4 (P3): `todo/templates/TODO-template.md` now matches the skill copies byte for byte, and `test_the_authoring_template_copies_are_identical` keeps the four copies equal.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:29:28Z
- Effort: medium
- Lines: 24
- Digest: b8473ee297033617

## adversarial: needs-attention

1. **P2: Reopening a stamped prerequisite still fails.** `scripts/lib/todo_graph.py:862` validates after reopening only the target, but line 554 prohibits stamped consumers with open dependencies. Consequently, `.claude/skills/review-todo-section/SKILL.md:91` promises an audit transition the graph rejects. Reproduced against `74e9354` entirely in memory, mocking filesystem mutations: unstamp `D00 T02 §2` returns `stamped section has open dependency: D00 T01 §4`. Rollback preserves the existing state, but the regressed section cannot reopen through the prescribed workflow. `tests/scripts/test_todo_graph.py:797` misses the stamped-consumer case.

## consistency: needs-attention

2. **P2: The writer check happens after the first write.** `.claude/skills/process-todo-section/SKILL.md:40` instructs adding `Started:`; line 42 subsequently checks whether another writer holds the tree. This contradicts `AGENTS.md:66`, which requires serial writes. Reproduce by invoking the skill directly on an unstarted section while another session owns unfamiliar changes: following the documented order modifies its TODO before detecting the conflict.

## integration: needs-attention

Finding 1 also blocks `.claude/skills/process-phase/SKILL.md:44`: its shipped-prerequisite regression check routes into an audit workflow that cannot reopen prerequisites with stamped consumers.

## source-defect: approve

## design: advisory

Not applicable; the section declares `Surface: no`.

## record: approve

Checkpoint re-run confirmed all 12 skills valid with zero diagnostics, `valid: 677 sections`, and `plan current`. The template-equality test passed. These checks do not exercise findings 1 and 2. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 2

`Ran 82 tests`, `OK` (`test_unstamping_a_prerequisite_leaves_its_consumers_stamped_and_suspect` fails against `74e9354`); `valid: 677 sections`; `plan current`.

- Finding 1 (P2): reopening a prerequisite with stamped consumers now works. The graph accepts a stamped section on an open prerequisite only when that prerequisite carries an `Unstamped:` note (a consumer cannot be stamped on an open prerequisite in the first place, so only stamps that predate the regression qualify); the consumers stay stamped and are suspect until it re-stamps, which `unstamp` prints from the new `suspects()`. Without the note the old refusal stands (same test).
- Finding 2 (P2): `process-todo-section` (all three kits) checks for another writer before any write, then writes `Started:`.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:33:29Z
- Effort: medium
- Lines: 24
- Digest: 955e5da6bd258bd5

## adversarial: needs-attention

1. **P1: Suspect stamps still satisfy dependency gates.** `scripts/lib/todo_graph.py:590` and `scripts/lib/todo_graph.py:762` check only whether direct prerequisites are stamped. After reopening A, its stamped consumer B remains suspect, but an open section C depending on B is still considered ready and passes the stamp dependency check.

   Reproduced without filesystem writes using an in-memory fixture: reopened `D00 T02 §2`, retained its stamped consumer `D00 T02 §3`, and configured an open section to depend on that consumer. `suspects()` included B, but `disposition(C)` returned `0`; invoking `stamp()` with a sentinel at `commit_ok` reached that sentinel after passing the dependency gate. Retaining historical stamps must not let new work bypass an unresolved ancestor regression.

## consistency: approve

## integration: needs-attention

Finding 1 affects `.claude/skills/review-todo-section/SKILL.md:91` and `.claude/skills/process-phase/SKILL.md:44`: the audit workflow declares consumers suspect, but readiness and stamping continue treating those consumers as sufficient prerequisites. Subsequent sessions can build on the regressed dependency chain.

## source-defect: approve

## design: advisory

Not applicable; the section declares `Surface: no`.

## record: approve

Checkpoint independently reproduced at `dca3611`: all 12 skill directories valid with zero diagnostics; `valid: 677 sections`; `plan current`. These checks do not detect finding 1. No files modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

`Ran 82 tests`, `OK` (the extended suspect test fails against `dca3611`); `valid: 677 sections`; `plan current`.

- Finding 1 (P1): a prerequisite now holds only when it is `settled()`: it and every ancestor are stamped. `disposition`, `stamp` and `resolve`'s unmet list use it, so new work on a suspect stamp waits (exit 4) and cannot stamp until the reopened ancestor re-stamps (`test_unstamping_a_prerequisite_leaves_its_consumers_stamped_and_suspect`).

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:37:35Z
- Effort: medium
- Lines: 24
- Digest: bec1408e8074838a

## adversarial: needs-attention

1. **P2: Audit instructions still invalidate the graph before reopening a section.** `.claude/skills/review-todo-section/SKILL.md:34` instructs reviewers to untick checklist items whose `Done when:` fails. Line 91 applies that step to stamped sections before `unstamp`. Reproduced against `92120ec` using mocked reads: unticking the first non-Commit checklist item in `D00 T02 §2` makes both `graph()` and `unstamp()` refuse with `unfinished stamped checklist: D00 T02 §2`. The prescribed recovery cannot proceed. Audit findings must leave the stamped checklist intact until `unstamp` succeeds.

## consistency: needs-attention

Finding 1 also contradicts `.claude/skills/review-todo-section/SKILL.md:85`, which prohibits rewriting a stamped section’s checklist. Following the audit’s step 1 violates that rule.

## integration: needs-attention

Finding 1 breaks the recovery path invoked by `.claude/skills/process-phase/SKILL.md:44` when a shipped prerequisite’s checkpoint fails. Once the reviewer follows the unticking instruction, subsequent graph operations refuse.

## source-defect: approve

## design: advisory

Not applicable: `Surface: no`.

## record: approve

Independently reran the checkpoint: all 12 skill directories validated with zero diagnostics; graph validation reported `valid: 677 sections`; `plan --check` reported `plan current`. These checks miss finding 1. No files modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 4

- Finding 1 (P2): `review-todo-section` (all three kits) now says that on a stamped section the audit edits nothing in the section by hand: an unmet item is recorded in the findings file, a regression goes through `unstamp` first, and only the reopened section's items are unticked afterwards. This matches its own rule that a stamped checklist is never rewritten. `muse skills validate` -> `valid=True` in all three kits.

## Round 5: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:40:20Z
- Effort: medium
- Lines: 24
- Digest: 0de8617aeac0ffd3

## adversarial: needs-attention

1. **P2: Template drift can skip its CI gate.** `.github/workflows/plan.yml:21` and `:36` include only `.claude/skills/**`, although `tests/scripts/test_todo_graph.py:897` now checks templates in `.codex` and `.agents` too. Reproduce with a change confined to `.codex/skills/create-todo/todo-template.md`: neither event filter matches, so `plan-gates` does not run. An in-memory alteration reproduced one template-equality test failure, with zero matching workflow paths. GitHub documents this [path-filter behaviour](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#onpushpull_requestpull_request_targetpathspaths-ignore).

## consistency: needs-attention

2. **P2: File closeout preserves stamps it discovers were unearned.** `.claude/skills/process-todo-file/SKILL.md:32` directs an executor finding a partially shipped, ticked item to create a new section while retaining the stamped checklist. Unlike `.claude/skills/review-todo-section/SKILL.md:34` and `:91`, it never routes that discovery through audit and `unstamp`. Reproduce by following file closeout after discovering an existing ticked `Done when:` is unmet: the prescribed action leaves the original section verified and its consumers eligible, despite the known incomplete prerequisite. This concerns unmet existing scope, not newly requested granularity.

## integration: needs-attention

Findings **1 (P2)** and **2 (P2)** affect downstream workflows: the added template regression test is skipped for two of its inputs, and file closeout bypasses the reopening mechanism that makes dependent stamps suspect.

## source-defect: approve

## design: advisory

Not applicable: the section declares `Surface: no`.

## record: approve

Independently reproduced the checkpoint at `f5e972c`: all 12 skills valid with zero diagnostics; `valid: 677 sections`; `plan current`. Both read-only `Repository` tests passed. These checks do not detect the workflow defects above. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 5

- Finding 1 (P2): `.github/workflows/plan.yml` runs on changes under `.codex/skills/**` and `.agents/skills/**` as well as `.claude/skills/**`, in both the push and pull-request filters, so the template-equality test runs for every copy it checks.
- Finding 2 (P2): `process-todo-file` (all three kits) sends a partly shipped ticked item in a stamped section through `review-todo-section`'s audit stance, which files the repair and reopens the section with `unstamp`; only work beyond the item's own `Done when:` becomes a new section. `muse skills validate` -> `valid=True` in all three kits.

## Round 6: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:42:52Z
- Effort: medium
- Lines: 33
- Digest: 7333bb50ae094d2e

## adversarial: needs-attention

1. **P2: Host-blocked work does not follow the prescribed parking path.** `.claude/skills/process-phase/SKILL.md:39` treats an unreachable host as an exit-4 leftover; line 52 skips only nonzero resolutions. However, `scripts/lib/todo_graph.py:600` checks person Needs, not host availability. Reproduced in memory by replacing a ready row’s Needs with `arm64-host`: `disposition()` still returned `0`. On a machine without that host, section execution stops at its environment check, but the phase instructions still direct review and retry instead of reliably classifying the row as parked.

## consistency: approve

## integration: needs-attention

Finding **1 (P2)** affects `.claude/skills/process-plan/SKILL.md:17` and `:94`: both use `query ready` to resume work after parking, although that query retains environmentally unavailable rows. The runner needs to carry session-specific blockers through phase selection and heartbeat resumption.

## source-defect: needs-attention

2. **P2: Run-guard commands lose the session identifier in PowerShell.** `.claude/skills/process-plan/SKILL.md:74`, `:76`, `:90`, and `.claude/skills/process-phase/SKILL.md:14` use `$CLAUDE_CODE_SESSION_ID` without assigning that PowerShell variable. Environment variables require `$env:CLAUDE_CODE_SESSION_ID`. [Microsoft documents this syntax](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_environment_variables#use-the-variable-syntax).

   Reproduction in a fresh PowerShell process:

   ```powershell
   $env:CLAUDE_CODE_SESSION_ID = 'audit-fixture'
   python -B scripts/campaign_guard.py whoami --session $CLAUDE_CODE_SESSION_ID --generation audit-fixture
   ```

   Actual result: `campaign_guard: bad argument 'audit-fixture'`, exit 1. This affects acquisition, ownership checks and shutdown. The corrected syntax reached the guard implementation, but further verification was blocked by the read-only sandbox denying its lock-file creation.

## design: advisory

Not applicable: `Surface: no`.

## record: approve

Independently reproduced the checkpoint at `e7cf65d`: all 12 skills valid with zero diagnostics; `valid: 677 sections`; `plan current`. Both read-only `Repository` tests passed. These checks do not detect the findings above. No files were modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 6

`muse skills validate` -> `valid=True` for `process-plan` and `process-phase` in all three kits.

- Finding 1 (P2): `process-phase` and `process-plan` (all three kits) now say that `resolve` and `query ready` do not know which hosts are reachable, so a row whose Needs name an unreachable host is a leftover even at exit 0: phase step 3 skips it, and the plan loop and the after-park step pick only phases with a ready row this session can run. Teaching the graph which hosts a session reaches would need a host registry; the industry-standard default here is the runner's own environment check (step 0 of `process-todo-section`), now named in each place that selects work.
- Finding 2 (P2): `process-plan` (Claude kit, the only one with the run guard) and `process-phase` state that the session id is `$env:CLAUDE_CODE_SESSION_ID` in PowerShell; the bare bash spelling is an empty PowerShell variable and the guard refuses its arguments.

## Round 7: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:45:53Z
- Effort: medium
- Lines: 24
- Digest: 4f177dda6c60875a

## adversarial: needs-attention

1. **P2: Closing the last runnable phase can leave the run guard active.** `.claude/skills/process-plan/SKILL.md:103` prescribes `end` with `plan-done` or `park` when no remaining phase can run locally. Reproduce by completing a phase while another phase has only unreachable-host rows: `process-phase/SKILL.md:56` writes only a closeout heading. `plan-done` refuses because `query ready` still lists the host-bound rows; `park` refuses because no `PARKED` marker exists. Verified with mocked reads against `_end_locked`: both paths raise `GuardError`. The instructions omit writing the plan-level park marker before ending the guard.

## consistency: approve

## integration: needs-attention

Finding **1 (P2)** also affects heartbeat recovery at `.claude/skills/process-plan/SKILL.md:94`: host-bound rows keep the heartbeat on its resume path, which returns to the same incomplete shutdown instructions.

## source-defect: approve

## design: advisory

Not applicable: the section declares `Surface: no`.

## record: needs-attention

2. **P3: The promised first-edit routing is not enforced by the instructions.** `todo/00-workspace/TODO-02-todo-system.md:86` claims a fresh session reads `todo/README.md` before its first edit. `AGENTS.md:25` merely identifies that file, while `.claude/skills/process-todo-section/SKILL.md:42` explicitly directs writing `Started:` without first requiring it. Reproduce by following that skill from a fresh session: resolution, environment and writer checks lead directly to an edit before reading the format specification.

Checkpoint independently reproduced at `f829ffa`: all 12 skills valid with zero diagnostics, `valid: 677 sections`, and `plan current`. No files modified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 7

`muse skills validate` -> `valid=True` for the changed skills in all three kits.

- Finding 1 (P2): `process-plan` (Claude kit, the one with the run guard) now says that when no remaining phase has a row this session can run but `query ready` still lists rows, the run writes the column-0 `PARKED ... run=<id>` line and park record, commits, then ends with `--reason park` (which refuses without that marker); `plan-done` is only for an empty `query ready`. The canonical heartbeat prompt's step 5 does the same when only unreachable-host rows remain.
- Finding 2 (P3): `process-todo-section` (all three kits) reads `todo/README.md` before a session's first edit, as `D00 T02 §2` promised.