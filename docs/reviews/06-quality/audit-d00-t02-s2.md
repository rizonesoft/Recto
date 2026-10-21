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