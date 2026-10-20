# Findings

Defects noticed while doing something else, each filed with `path:line`, a date and one line on the failure a user, test or gate would observe. A finding is not scheduled work: scheduling it means writing a section through the `add-todo` skill, then striking the finding here with its `DNN TNN §N`. A finding with no `file:line` and no observable failure is an opinion; delete it instead.

| Date | Where | Finding | Scheduled |
| ---- | ----- | ------- | --------- |
| 2026-10-20 | `scripts/lib/todo_graph.py:550` | The Conclave SDK's 100-character plan row refusal fails `plan --check` on 194 of 204 Recto rows (titles follow AGENTS.md's 88-character rule); the refusal is held here as a marked workaround and the next `forge.py sync --include-scripts` restores it. Owner fix: the SDK reads the limit per project from `todo/plan.json`, filed for the SDK's own `todo/findings.md` once its tree is free. | SDK, not a Recto section |
