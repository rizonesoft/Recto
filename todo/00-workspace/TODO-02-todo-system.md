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

> **Current state:** Measured 2026-10-13 (updated 2026-10-18). Before 2026-10-13 the repository tracked work only as prose: the milestone table in `docs/plans/2026-10-13-recto-foundation.md` section 7 and the interface contract in `design/SPEC.md`. This file's three sections were built by porting the clean strict generation of the owner's TODO system (swisshotelschool-coza `scripts/lib/todo_graph.py`, itself a port of rizonetech-com `scripts/lib/workflow.py`), extended with the ScratchPad and Resolute rules that fit Recto (Adjacency, bidirectional XREFs, index coverage, frozen freeze checks, durable deferrals). The eight workflow skills were adapted from ScratchPad's `.claude/skills/` set. `D00 T01 §1` (`25ac228f`) and `D00 T02 §1` (`77a72bd9`) are now verified and stamped, and on 2026-10-14 the seeded graph expanded to 205 sections for the native C++23 `recto-pdf` engine and full converter/authoring scope.

> **Groomed 2026-10-23:** the graph now holds 750 sections across 11 domains (`python scripts/todo-graph.py query stats`), not the 205 quoted above; the run guard (`scripts/campaign_guard.py`), review dispatch (`scripts/review-dispatch.py`) and the pin checks under `.conclave/` were added after this file shipped and are described in `AGENTS.md`.

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
| 1 | §1 | Graph engine, CLI and tests | D00 T01 §1 | [x] |
| 2 | §2 | Skills, rules and the CI gate | §1 | [x] |
| 3 | §3 | Seeded graph and generated plan | §2 | [x] |
| 4 | §4 | `resolve --context`: one section with its file context, read in place of the file | §1 | [ ] |

## 1. Graph engine, CLI and tests

The engine is the strict core: parse every TODO file, refuse anything malformed with a named reason, order sections by phase and dependency, generate the plan, and stamp only against real commits with review receipts. Recto's extensions over the swisshotelschool port are the milestone Phase vocabulary, the Recto Needs vocabulary, Surface sections requiring Fidelity, Job, Treatment and Chrome, the frozen Freeze check rule, executable Adjacency declarations, bidirectional XREFs, index coverage, durable deferrals and the 55-section file cap. Built 2026-10-13; the stamp waits on D00 T01 §1 for a commit to cite.

> **Started:** 2026-10-13 -- initial port in `720725c`; review-driven hardening completed 2026-10-14
> **Corrected:** 2026-10-14 -- Hardened `scripts/lib/todo_graph.py`, `scripts/todo-graph.py` and `tests/scripts/test_todo_graph.py` for Round 1 review findings: transactional stamp rollback across receipt, TODO and plan writes; strict rejection of malformed checklist lines, empty fields, missing TODO headings, section `0`, and non-step `Build order` blocks; verification of review report files and stamped git commits during graph validation; domain-qualified root index coverage; Windows backslash path resolution in `extract_ref`; iterative explicit-stack dependency traversal for deep graphs; and expanded the unit test suite from 35 to 50 tooling tests plus the live repository test (51 total).

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Port the engine to `scripts/lib/todo_graph.py` with Recto's vocabularies and rules. Done when: `python scripts/todo-graph.py validate` prints the section count for the seeded tree and exits 0.
2. Add the CLI in `scripts/todo-graph.py`: `validate`, `build`, `render`, `plan --sync` and `--check`, `query ready|blocked|stats|deferred|adjacency`, `resolve` (accepting a pasted plan row), `classify`, `stamp`. Done when: `plan --check` exits 0 after `--sync` and exits 2 after a section edit without a sync.
3. Tests in `tests/scripts/test_todo_graph.py` covering every refusal (malformed row, row without body, cycle, later-phase prerequisite, unknown Need, missing index entry, malformed Adjacency, one-sided XREF, surface without Fidelity, freeze mismatch, ownerless deferral, stale deferral, 55-section and 30-item caps, superseded without successor, malformed checklist item, empty Build order or field, missing review report, fabricated commit SHA, stamp without receipt, failed stamp write rollback) and the live repository tree. Done when: `python -m unittest tests.scripts.test_todo_graph` passes, 50 tooling tests plus the repository test.

- [x] Graph library and command line in `scripts/lib/todo_graph.py` and `scripts/todo-graph.py`. Done when: the validator refuses a malformed tree with a named reason instead of reading it as empty.
- [x] Tests for every refusal and for the live tree in `tests/scripts/test_todo_graph.py`. Done when: the suite passes against the seeded graph.
- [x] Commit: `todo: port the strict TODO graph engine and tests`

**Test checkpoint:** `python -m unittest tests.scripts.test_todo_graph` passes in full (including the Repository test against this tree), and `python scripts/todo-graph.py plan --check` exits 0.

> **Verified:** 2026-10-14 | §1 | 77a72bd92f60597c92f669fcb6de82751be184da
> **Review:** `docs/reviews/d00-t02-s1.json`

## 2. Skills, rules and the CI gate

The engine is useless without the working agreements around it: the format spec, the eight skills that route, author, build, review and run work, the project rules that send every session to the graph first, and CI refusing drift. The skills are Claude Code project skills under `.claude/skills/`, as in ScratchPad and Resolute (owner decision 2026-10-13: Claude skills only, no Codex mirrors).

> **Started:** 2026-10-18T09:04:00Z
> **Corrected:** 2026-10-18 -- Specified per-skill directory paths for `muse skills validate <path> --json` (bare `muse skills validate` without a path exits 1 with usage), and replaced residual pre-2026-10-14 `MuPDF` references in `todo/README.md`, `.claude/skills/create-todo/SKILL.md`, and `.claude/skills/groom-plan/SKILL.md` with `recto-pdf` and the ISO 32000 PDF specification per revised decision D3.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Write `todo/README.md` as the format spec and `todo/templates/TODO-template.md` as the authoring template. Done when: a cold reader can author a valid TODO file from those two files alone, proven by authoring D00 T01 from them.
2. Adapt the eight skills (`add-todo`, `create-todo` with its template, `groom-plan`, `process-todo-section`, `review-todo-section`, `process-todo-file`, `process-phase`, `process-plan`) to Recto: MSBuild and GoogleTest gates, SPEC fidelity, the frozen threading contract, stamp-through-CLI. Done when: `Get-ChildItem .claude/skills -Directory | ForEach-Object { muse skills validate $_.FullName --json }` reports `valid: true` with zero diagnostics for all eight.
3. Point the rules at the graph: `AGENTS.md` (TODO system summary, architecture and interface rules, validation commands), `CLAUDE.md` as its import stub, `.gitignore`, `.gitattributes` (LF for the byte-compared generated plan), and `.github/workflows/plan.yml` running the tooling tests, `validate` and `plan --check`. Done when: no rule file describes a plan anywhere but `todo/`.

- [x] `todo/README.md` spec and `todo/templates/TODO-template.md`. Done when: the template, filled in, passes `validate` (every seeded file is the proof).
- [x] Eight skills under `.claude/skills/`. Done when: all eight validate clean and each names only commands the CLI actually implements.
- [x] `AGENTS.md`, `CLAUDE.md`, `.gitignore`, `.gitattributes`, `.github/workflows/plan.yml`. Done when: a fresh session is routed to `todo/README.md` before its first edit.
- [x] Commit: `todo: skills, rules and CI gate for the TODO graph`

**Test checkpoint:** `Get-ChildItem .claude/skills -Directory | ForEach-Object { muse skills validate $_.FullName --json }` reports `valid: true` and `diagnostics: []` for all eight skills, and `python scripts/todo-graph.py validate` exits 0 on the seeded tree the skills describe.

> **Verified:** 2026-10-18 | §2 | d40bf2be8a4039c7983c186a5ac85c7c5f43e50c
> **Review:** `docs/reviews/d00-t02-s2.json`

## 3. Seeded graph and generated plan

The seed graph translates the foundation plan and the SPEC into executable sections: nine milestone domains, one file per coherent subject, every milestone done-when, decision, screen, dialog and open item owned by exactly one section. Seeding is authoring, not transcription: each section names its paths from the foundation plan's repository layout, its governing plan and SPEC sections, and a checkpoint that can fail.

> **Started:** 2026-10-18T10:08:00Z
> **Corrected:** 2026-10-18 -- Updated the Test checkpoint's `resolve 'D00 T01 §1'` expected exit code from `0` (ready before `D00 T01 §1` shipped on 2026-10-13) to `3` (verified `[x]`, since `D00 T01 §1` is now stamped at `25ac228f`), and expanded the `Owned by` column in `todo/TODO-00-INDEX.md` so every coverage row explicitly names its resolving section range (`§1-§N`).
> **Completed:** 2026-10-18T11:16:00Z
> **Duration:** 1h 08m

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Author the domain files for `00-workspace` through `07-release` and `99-manual`, with per-domain `INDEX.md` files and `todo/TODO-00-INDEX.md`. Done when: `validate` passes with every file listed in both indexes and every XREF reciprocated.
2. Generate `todo/implementation-plan.md` with `plan --sync`. Done when: `plan --check` exits 0 and `query stats` reports the seeded totals with D00 T01 §1 among the verified rows.
3. Sweep the foundation plan and SPEC once against the graph (milestone rows, open items, GPL-3.0 checklist, SPEC sections 8 to 11) and file anything ownerless through the new `add-todo` skill. Done when: the sweep finds nothing unowned, and `todo/findings.md` exists for what future work notices.

- [x] Seeded TODO files, domain indexes and the root index. Done when: the graph validates and `query ready` returns the true frontier.
- [x] Generated `todo/implementation-plan.md`. Done when: it is byte-identical to a fresh `plan --sync` output.
- [x] Coverage sweep recorded in `todo/TODO-00-INDEX.md` (every SPEC surface and milestone clause with its owner). Done when: each row names a resolving section.
- [x] Commit: `todo: seed the Recto execution graph from the foundation plan`

**Test checkpoint:** `python scripts/todo-graph.py validate`, `plan --check` and the full unittest suite pass, and `python scripts/todo-graph.py resolve 'D00 T01 §1'` exits 3 (verified).

> **Verified:** 2026-10-18 | §3 | 99a2c62fc1588025e8cb41efd4bbf80ea8fcfbcc
> **Review:** `docs/reviews/d00-t02-s3.json`

## 4. `resolve --context`: one section with its file context, read in place of the file

`python scripts/todo-graph.py resolve '<ref>'` already prints one section (its title, phase, dependencies, Needs and body, as JSON), but `process-todo-section` step 2 tells the session to read the entire TODO file as well, because the Goal, Current state, Inputs and Adjacency above the sections carry context a section assumes, and to read the sections it depends on for their stamps and receipts. Measured 2026-11-10: the 47 TODO files total 4,578 KB, and `todo/01-engine/TODO-02-actor-and-render.md` alone is 379 KB, so every section built or reviewed in that file reads about 370 KB of other sections to get a few kilobytes of file context. This section has `resolve` print exactly what the skill needs from the file (the section word for word, the file's context blocks, each dependency's stamp and receipt, and the cross-references that name the section) as Markdown, and changes the skills to read that. Closing a file (`process-todo-file`) and grooming (`groom-plan`) still read whole files: their job is the whole file.

**Corrected 2026-11-10:** validation before the build, against `scripts/todo-graph.py`, `scripts/lib/todo_graph.py` and the skills as they stand. Decided, each a default with its cost of changing (one function, `context()` in `scripts/lib/todo_graph.py`, and its tests):
- The instruction to read the entire TODO file is `process-todo-section` step 1 ("Read the whole contract"), not step 2 as the paragraph above and the checklist said; `review-todo-section` step 0 calls `resolve` and gains the flag.
- The output is written as bytes (`sys.stdout.buffer`), so Windows never turns `\n` into `\r\n`: the file context and the section are slices of the file's own bytes (TODO files are LF, `.gitattributes`), and "byte-identical" is tested on the bytes.
- The parts, in order: a `# resolve --context <ref>` heading; a `- file:` line (path, frontmatter `status`, `frozen`, `depends_on`); a `- state:` line (the exit code `resolve` returns and its meaning); the file context, which is the file from its `# TODO-NN -- Title` heading to the line before the first `## N.` heading (so Goal, Current state and its dated notes, Inputs, Outcome, Adjacency with its rationale and the Implementation Order table arrive unedited and in the file's order); the section slice; `## Dependencies`; `## Waiting Needs`; `## Cross-references in`; `## Cross-references out`; the size line.
- `## Dependencies` prints one line per ref in the section's own `Depends On` cell, and one line per file in the frontmatter `depends_on` (the id, its path, how many of its sections are stamped, and the open ones by ref), because the graph expands a file dependency into every section of that file and one line each would bury the list.
- A cross-reference line is one that holds the XREF arrow marker of `todo/README.md` ("Cross-reference notation"); a deferral line is a blockquote line opening with the bold Deferred or Resolved marker, and is listed once, as a deferral. Inbound lines come from every TODO file, outside the section's own text.
- The size line counts every byte printed before it, against the file's bytes on disk.

**Phase:** 0
**Needs:** none
**Surface:** no

**Build order:**

1. Add `--context` to `resolve`. Done when: `python scripts/todo-graph.py resolve 'D01 T02 §10' --context` prints the section and its file context as Markdown with `resolve`'s existing exit codes.
2. Point the skills at it. Done when: `process-todo-section` and `review-todo-section` read the context output and no longer tell the session to read the entire file.
3. Measure it. Done when: the bytes read for one section, before and after, are recorded.

- [ ] Add `--context` to the `resolve` command in `scripts/todo-graph.py`, built in `scripts/lib/todo_graph.py`: it prints, as Markdown and in the file's order, the file's title, Goal, Current state with every dated note under it (`Groomed`, `Corrected`), Inputs, Outcome, Adjacency and its rationale, and the Implementation Order table, then the section from its `## N.` heading to the line before the next `## ` heading. Done when: the section part is byte-identical to that slice of the file (`tests/scripts/test_todo_graph.py` compares them on a fixture and on every section of the live tree), and the exit codes are those `resolve` returns without the flag.
- [ ] After the section, print one line for each section in its `Depends On` (the ref, title, `[x]` or `[ ]`, the `Verified:` date and commit, the receipt path) and each of its waiting Needs. Done when: for a section with one stamped and one open prerequisite, the test reads both lines with the right state, commit and receipt.
- [ ] Print every cross-reference line and every deferral line in the tree that names the section, with its file and line, and every one in the section's own text that names another section. Done when: the test reads an inbound cross-reference from another file, an inbound deferral and an outbound cross-reference, each with `path:line`, and a section with none prints `none`.
- [ ] End the output with a size line: the bytes printed, the file's bytes and the share. Done when: `resolve 'D01 T02 §10' --context` ends with a line such as `context 21,400 bytes of 388,000 (5.5%)`, and the test checks the three numbers against the fixture.
- [ ] Refuse what `resolve` refuses, with the same message and exit code, and print nothing else: no such section (1), no reference in the input (2). Done when: the test reads both refusals with `--context` and an empty standard output.
- [ ] Change `.claude/skills/process-todo-section/SKILL.md` step 1 and `.claude/skills/review-todo-section/SKILL.md` to read `resolve '<ref>' --context` in place of the entire TODO file, saying when a wider read is still right: a dependency's section when its body (not only its stamp) decides the work, read through `resolve '<dependency>' --context`, and the whole file only in `process-todo-file` and `groom-plan`. Done when: both skills name the command, neither says to read the entire file, and `python -m unittest discover -s scripts/tests` passes.
- [ ] Document the flag in `todo/README.md`, `docs/todo-workflow.md` and the usage text of `scripts/todo-graph.py`. Done when: all three show the command and say what it prints.
- [ ] Measure it on `D01 T02 §10` and on the smallest and largest TODO files' first open sections. Done when: the findings file records, for each, the file's bytes, the `--context` bytes and the share, and the largest file's section reads at most a tenth of its file.
- [ ] Commit: `todo: resolve --context prints a section with its file context`

**Test checkpoint:** `python -m unittest tests.scripts.test_todo_graph` passes, including the byte-identical section slice on every section of the live tree; `python scripts/todo-graph.py resolve 'D01 T02 §10' --context` prints that section, its file's Goal and its dependencies' stamps; and changing one character of the printed section in the fixture fails the test.

## Verification

- `python scripts/todo-graph.py resolve '<ref>' --context` prints any section with its file context, and the section skills read it in place of the whole file.
- The three sections' stamps land against the first repository commit (D00 T01 §1) with review receipts, after which this file reads `status: done` through the normal stamp path.
- A cold session, given only a pasted plan row, can resolve, process and stamp a section using the skills without inventing a step.
- `plan-gates` CI is green on the seeded tree.
