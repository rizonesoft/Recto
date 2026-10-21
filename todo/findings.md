# Findings

Defects noticed while doing something else, each filed with `path:line`, a date and one line on the failure a user, test or gate would observe. A finding is not scheduled work: scheduling it means writing a section through the `add-todo` skill, then striking the finding here with its `DNN TNN §N`. A finding with no `file:line` and no observable failure is an opinion; delete it instead.

| Date | Where | Finding | Scheduled |
| ---- | ----- | ------- | --------- |
| 2026-10-20 | `scripts/lib/todo_graph.py:552` | ~~The former SDK copy's 100-character plan row refusal failed `plan --check` on 194 of 204 Recto rows (titles follow AGENTS.md's 88-character rule).~~ | Resolved 2026-10-21: the SDK link was removed, the 88-character rule is Recto's own and the dead row-limit constant is gone |
| 2026-10-21 | `.claude/skills/process-plan/SKILL.md:45` | ~~The process-plan audit ran only `scripts/tests`, which would skip Recto's own tooling tests in `tests/scripts` (graph, run guard, pins).~~ | Resolved 2026-10-21: the skill runs both directories; the SDK link was removed |
| 2026-10-21 | `scripts/lib/todo_graph.py:34` | ~~Phase 98 (Recto Cloud, D20) needed a local phase-list change.~~ | Resolved 2026-10-21: D20 withdrawn and phase 98 removed; the SDK link was removed |
