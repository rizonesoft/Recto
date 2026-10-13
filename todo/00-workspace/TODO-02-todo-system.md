---
schema_version: 1
id: todo-system
domain: 00-workspace
status: active
title: "TODO-02 -- The TODO Graph System"
track: M0
---

# TODO-02 -- The TODO Graph System

> **Goal:** Work on Recto is tracked the way the owner's other repositories track it: a validated TODO graph in `todo/` with a generated implementation plan, workflow skills that run it, rules that point at it, and a CI gate that refuses a malformed graph or a stale plan. Every later section in this tree is executed through this system.

> **Current state:** Measured 2026-10-13. Before today the repository tracked work only as prose: the milestone table in `docs/plans/2026-10-13-recto-foundation.md` section 7 and the interface contract in `design/SPEC.md`. Today this file's three sections were built by porting the clean strict generation of the owner's TODO system (swisshotelschool-coza `scripts/lib/todo_graph.py`, itself a port of rizonetech-com `scripts/lib/workflow.py`), extended with the ScratchPad and Resolute rules that fit Recto (Adjacency, bidirectional XREFs, index coverage, frozen freeze checks, durable deferrals). The eight workflow skills were adapted from ScratchPad's `.claude/skills/` set. The repository is not yet a git repository (D00 T01 §1), so no section here can be stamped until the first commit exists; the checklist ticks below record work actually present on disk, and the stamps land against that first commit.

## Inputs

- swisshotelschool-coza `scripts/lib/todo_graph.py`, `scripts/todo-graph.py`, `tests/scripts/test_todo_graph.py`, `todo/README.md` and `todo/templates/TODO-template.md` (the strict core this port follows).
- ScratchPad `todo/README.md`, `.claude/skills/` (the eight skills), `AGENTS.md` and `.github/workflows/plan.yml` (format rules, skill contracts and the CI gate shape).
- `docs/plans/2026-10-13-recto-foundation.md` (milestones M0 to M6, decisions D1 to D17: the seed graph's domains, phases and section content) and `design/SPEC.md` (the Fidelity source for every surface section).
- -> XREF: D00 T01 §1 -- the first commit this file's stamps cite; its §4 wires the product CI beside the plan gate built here.

## Outcome

- `python scripts/todo-graph.py validate`, `plan --check`, `query ready` and `resolve` work on the live tree, and `python -m unittest tests.scripts.test_todo_graph` proves every refusal plus the live tree.
- A fresh agent session can find, file, build, review and stamp work using only `todo/README.md`, `AGENTS.md` and the eight skills.
- The seeded graph covers every milestone, decision and SPEC screen the foundation plan promises for version 1, each with exactly one owning section.

**Adjacency:** all=not-applicable (workflow tooling over Markdown files; its surfaces are the CLI commands documented in todo/README.md, and its records are the TODO files themselves, owned by their own sections)

**Adjacency rationale:** This file builds the system the other files' Adjacency declarations run on. Its own list, document, settings and audit surfaces are the graph itself: `query` is the list, the generated plan is the document, the closed Phase and Needs vocabularies in `scripts/lib/todo_graph.py` are the settings, and stamps plus receipts are the audit trail. Declaring them again here would double-own them.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Graph engine, CLI and tests | D00 T01 §1 | [ ] |
| 2 | §2 | Skills, rules and the CI gate | §1 | [ ] |
| 3 | §3 | Seeded graph and generated plan | §2 | [ ] |

## 1. Graph engine, CLI and tests

The engine is the strict core: parse every TODO file, refuse anything malformed with a named reason, order sections by phase and dependency, generate the plan, and stamp only against real commits with review receipts. Recto's extensions over the swisshotelschool port are the milestone Phase vocabulary, the Recto Needs vocabulary, Surface sections requiring Fidelity, Job, Treatment and Chrome, the frozen Freeze check rule, executable Adjacency declarations, bidirectional XREFs, index coverage, durable deferrals and the 55-section file cap. Built 2026-10-13; the stamp waits on D00 T01 §1 for a commit to cite.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Port the engine to `scripts/lib/todo_graph.py` with Recto's vocabularies and rules. Done when: `python scripts/todo-graph.py validate` prints the section count for the seeded tree and exits 0.
2. Add the CLI in `scripts/todo-graph.py`: `validate`, `build`, `render`, `plan --sync` and `--check`, `query ready|blocked|stats|deferred|adjacency`, `resolve` (accepting a pasted plan row), `classify`, `stamp`. Done when: `plan --check` exits 0 after `--sync` and exits 2 after a section edit without a sync.
3. Tests in `tests/scripts/test_todo_graph.py` covering every refusal (malformed row, row without body, cycle, later-phase prerequisite, unknown Need, missing index entry, malformed Adjacency, one-sided XREF, surface without Fidelity, freeze mismatch, ownerless deferral, stamp without receipt) and the live repository tree. Done when: `python -m unittest tests.scripts.test_todo_graph` passes, 35 tooling tests plus the repository test.

- [x] Graph library and command line in `scripts/lib/todo_graph.py` and `scripts/todo-graph.py`. Done when: the validator refuses a malformed tree with a named reason instead of reading it as empty.
- [x] Tests for every refusal and for the live tree in `tests/scripts/test_todo_graph.py`. Done when: the suite passes against the seeded graph.
- [ ] Commit: `todo: port the strict TODO graph engine and tests`

**Test checkpoint:** `python -m unittest tests.scripts.test_todo_graph` passes in full (including the Repository test against this tree), and `python scripts/todo-graph.py plan --check` exits 0.

## 2. Skills, rules and the CI gate

The engine is useless without the working agreements around it: the format spec, the eight skills that route, author, build, review and run work, the project rules that send every session to the graph first, and CI refusing drift. The skills are Claude Code project skills under `.claude/skills/`, as in ScratchPad and Resolute (owner decision 2026-10-13: Claude skills only, no Codex mirrors).

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Write `todo/README.md` as the format spec and `todo/templates/TODO-template.md` as the authoring template. Done when: a cold reader can author a valid TODO file from those two files alone, proven by authoring D00 T01 from them.
2. Adapt the eight skills (`add-todo`, `create-todo` with its template, `groom-plan`, `process-todo-section`, `review-todo-section`, `process-todo-file`, `process-phase`, `process-plan`) to Recto: MSBuild and GoogleTest gates, SPEC fidelity, the frozen threading contract, stamp-through-CLI. Done when: `muse skills validate` reports `valid: true` with zero diagnostics for all eight.
3. Point the rules at the graph: `AGENTS.md` (TODO system summary, architecture and interface rules, validation commands), `CLAUDE.md` as its import stub, `.gitignore`, `.gitattributes` (LF for the byte-compared generated plan), and `.github/workflows/plan.yml` running the tooling tests, `validate` and `plan --check`. Done when: no rule file describes a plan anywhere but `todo/`.

- [x] `todo/README.md` spec and `todo/templates/TODO-template.md`. Done when: the template, filled in, passes `validate` (every seeded file is the proof).
- [x] Eight skills under `.claude/skills/`. Done when: all eight validate clean and each names only commands the CLI actually implements.
- [x] `AGENTS.md`, `CLAUDE.md`, `.gitignore`, `.gitattributes`, `.github/workflows/plan.yml`. Done when: a fresh session is routed to `todo/README.md` before its first edit.
- [ ] Commit: `todo: skills, rules and CI gate for the TODO graph`

**Test checkpoint:** `muse skills validate` clean for all eight skills, and `python scripts/todo-graph.py validate` clean on the seeded tree the skills describe.

## 3. Seeded graph and generated plan

The seed graph translates the foundation plan and the SPEC into executable sections: nine milestone domains, one file per coherent subject, every milestone done-when, decision, screen, dialog and open item owned by exactly one section. Seeding is authoring, not transcription: each section names its paths from the foundation plan's repository layout, its governing plan and SPEC sections, and a checkpoint that can fail.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author the domain files for `00-workspace` through `07-release` and `99-manual`, with per-domain `INDEX.md` files and `todo/TODO-00-INDEX.md`. Done when: `validate` passes with every file listed in both indexes and every XREF reciprocated.
2. Generate `todo/implementation-plan.md` with `plan --sync`. Done when: `plan --check` exits 0 and `query stats` reports the seeded totals with D00 T01 §1 among the ready rows.
3. Sweep the foundation plan and SPEC once against the graph (milestone rows, open items, AGPL checklist, SPEC sections 8 to 11) and file anything ownerless through the new `add-todo` skill. Done when: the sweep finds nothing unowned, and `todo/findings.md` exists for what future work notices.

- [x] Seeded TODO files, domain indexes and the root index. Done when: the graph validates and `query ready` returns the true frontier (the repository section first).
- [x] Generated `todo/implementation-plan.md`. Done when: it is byte-identical to a fresh `plan --sync` output.
- [x] Coverage sweep recorded in `todo/TODO-00-INDEX.md` (every SPEC surface and milestone clause with its owner). Done when: each row names a resolving section.
- [ ] Commit: `todo: seed the Recto execution graph from the foundation plan`

**Test checkpoint:** `python scripts/todo-graph.py validate`, `plan --check` and the full unittest suite pass, and `python scripts/todo-graph.py resolve 'D00 T01 §1'` exits 0.

## Verification

- The three sections' stamps land against the first repository commit (D00 T01 §1) with review receipts, after which this file reads `status: done` through the normal stamp path.
- A cold session, given only a pasted plan row, can resolve, process and stamp a section using the skills without inventing a step.
- `plan-gates` CI is green on the seeded tree.
