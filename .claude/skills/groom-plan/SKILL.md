---
name: groom-plan
description: Harden the todo tree for a weaker executor with a sequence check, drift sweep, gap scan and complete-feature pass, without ticking boxes or starting a run. Use before a long run, or when the plan feels stale.
---

# Groom Plan

Hardening the tree so a weaker executor can run it. Grooming adds prerequisites and fills gaps; it never ticks a box, never stamps, and never starts a campaign.

## Workflow

### 1. Sequence check

For every dependency edge (section `Depends On` and file `depends_on`), confirm the prerequisite sits no later than its consumer: phase first, then row order in the generated plan. The validator already refuses a prerequisite in a later phase and any cycle; grooming catches the legal-but-hostile orders. Where an edge points later in practice:

- Prefer adding or splitting the prerequisite as a small new section ahead of the consumer, with a dated note.
- Never renumber a section or reuse a number: addresses are permanent.
- Record each fix with `**Corrected YYYY-MM-DD:**` or `**Groomed YYYY-MM-DD:**`, saying what it said before.

Run `python scripts/todo-graph.py validate` after every structural edit.

### 2. Drift sweep

Walk every open section's concrete claims against today's repository and governing documents: files, classes, paths, counts, versions, `recto-pdf` and Windows App SDK details, SPEC section numbers. Correct drift in place with dated notes, without building anything. Pay special attention to:

- "Nothing exists yet" claims that are no longer true.
- Counts and versions quoted confidently (these age fastest).
- Deferrals whose descriptions drifted while their owners stayed open (`python scripts/todo-graph.py query deferred`).
- XREFs whose targets shipped or moved.
- Current state blocks older than the last shipped section in their domain.

### 3. Gap scan

Read the plan as the user will use the finished product, domain by domain, against `design/SPEC.md` section 9 (every screen, dialog and state) and the foundation plan's milestone "Done when" column. Ask what has no owner: surfaces, controls, handoffs, error paths, settings without consumers, writes without readback, conversions without a failure mode. File each gap with `add-todo` (evidence, owner, dependency) and sync the plan.

Check the Adjacency declarations while here (`python scripts/todo-graph.py query adjacency`): an `applicable` with no owning section, or a `not-applicable` whose reason no longer holds, becomes work or a corrected declaration.

### 4. Complete-feature pass

For every user-facing surface in the plan, confirm the feature is whole against its SPEC screenshot: every control, menu item, dialog and state is working in a named section or deferred to a named, resolving section. For every engine surface, confirm the reverse and the failure path: cancel, abort, wrong password, damaged file, partial write. File what is missing; do not redesign what exists.

### 5. Report and commit

Write the groom record: what was sequenced, what drifted and was corrected, what gaps were filed and where they landed, what `query stats` now says. Commit as `todo: groom <scope> (<date>)` after a final `validate`, `plan --sync` and `plan --check`.

## Guardrails

- Do not tick a box or write a stamp. Grooming never ships.
- Do not start a run. Grooming prepares one.
- Do not redesign sections. Harden them.
- Do not rewrite a `[x]` section's checklist. That is the contract its stamp covers.
- Do not leave the tree unvalidated.
