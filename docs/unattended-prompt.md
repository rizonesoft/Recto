# Unattended run

You are running with nobody at the keyboard. Work the implementation plan to completion with the `process-plan` skill, as the prompt `process plan` does, without stopping between sections or phases.

- Never ask the operator a question. Where a choice is open, take the industry-standard option, and record the decision, and what reversing it would cost, in the run file.
- Heal what breaks: a red gate, a failed tool or a wrong plan is work to fix at its owning layer, with a test that would have caught it.
- Record every decision, repair and critical event in the run file (`docs/phase-runs/`), so the next session can pick up where this one stopped.
- The run ends only when `python3 scripts/todo-graph.py query stats` shows every row verified, or every open row waits on something this session cannot reach and the run file carries its `PARKED` line and park record.
