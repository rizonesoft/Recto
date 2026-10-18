---
name: create-todo
description: Author a whole new TODO file with frontmatter, Goal, Current state, Inputs, Outcome with Adjacency, Implementation Order, sections and Verification, then wire indexes, XREFs and the plan. Use when add-todo routes here.
---

# Create TODO

A new file is warranted when a subject no existing file owns needs a durable home. Most work does not need one: `add-todo` decides, and reaching here directly tends to produce a second TODO over an existing one.

## Workflow

### 1. Confirm the home

Name the domain and the next free `TODO-NN` number in it (numbers are local to the domain and never reused). Confirm no existing file covers the subject by searching as `add-todo` does. Audit the current code and the governing documents first (`docs/plans/2026-10-13-recto-foundation.md`, `design/SPEC.md`): the Current state is written from what you measured, with a date.

### 2. Author from the template

Copy `.claude/skills/create-todo/todo-template.md` (same file as `todo/templates/TODO-template.md`) to `todo/<NN-domain>/TODO-NN-<short-name>.md` and fill every part:

- **Frontmatter:** `schema_version: 1`, a globally unique kebab-case `id` (it survives renames and never changes once referenced), `domain` matching the directory, `status: draft` until the file is reviewed then `active`, `title`. Optional: `depends_on` (whole files that must fully ship first; prefer section edges), `frozen: true` (touches the `recto-pdf` and `DocumentActor` thread-safety contract in foundation plan section 4 or another parity-frozen behavior), `track` (the milestone, e.g. `M1`).
- **Goal:** one paragraph, plain terms, true when the file is done.
- **Current state:** what exists right now, measured, with real paths and real gaps.
- **Inputs:** the plan and SPEC sections, existing files with what each section consumes, and `-> XREF:` lines to related work.
- **Outcome:** observable end states, plus the one `**Adjacency:**` line (every key exactly once, or `all=not-applicable (reason)`) and an `**Adjacency rationale:**` paragraph. Silence is not a decision.
- **Implementation Order:** one row per section with real `Depends On` edges (`Deliverable` cell `<= 88` characters). The table is the dependency graph.
- **Sections:** `## N. <Title>` (`<= 88` characters, matching `Deliverable`), context, Phase, Needs, Surface, Build order with a `Done when:` per step, micro-step checklist ending in `Commit:`, runnable Test checkpoint, Fidelity/Job/Treatment/Chrome on surface sections, Freeze check in frozen files.
- **Verification:** the file-level checks `process-todo-file` will run.

Size sections by what holds together: at most 30 checklist items per section, at most 55 sections per file (split by subject into a new file; section numbers are permanent addresses). Every section must be implementable with zero conversation context: no "as discussed".

### 3. Wire it in

- List the file in the domain `INDEX.md` and under Active TODOs in `todo/TODO-00-INDEX.md` (the filename must appear verbatim in both; the validator refuses otherwise).
- Reciprocate every `-> XREF:`: each target file must point back at this file.
- Regenerate the plan and prove the tree holds:

```powershell
python scripts/todo-graph.py validate
python scripts/todo-graph.py plan --sync
python scripts/todo-graph.py plan --check
```

### 4. Commit and report

Commit as `todo: author <id> (<n> sections)`. Report the file, its phases, its dependency edges, and the new `query stats` totals.

## Guardrails

- Do not author a file whose subject an existing file owns. Search first.
- Do not write stub sections. Every section is born buildable.
- Do not skip the Adjacency line or the indexes. The validator treats both as structure.
- Do not end with the plan unsynced.
