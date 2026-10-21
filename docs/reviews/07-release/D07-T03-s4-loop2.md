# Review Receipt - D07 T03 §4, loop 2

- **Section:** `D07 T03 §4` (`todo/07-release/TODO-03-listening-to-users.md`)
- **Candidate:** `a05ffe9..2d7324b` (`scripts/release-notes.py`, `tests/scripts/test_release_notes.py`, `docs/support/user-bugs.md`, `.github/pull_request_template.md`, `AGENTS.md`)
- **Date:** 2026-10-21
- **Loop 1:** `docs/reviews/07-release/D07-T03-s4.md` spent all 22 rounds and ended at the cap; its last P1 (a C++ source scan cannot decide what compiles) was filed as `D07 T03 §9`, which replaced the scanner with each `main` build's CI test list recorded as a git note and is stamped (`docs/reviews/d07-t03-s9.json`). This loop reviews §4 as it now stands.

## Self-review

- The existence item is re-ticked: a Python id is resolved by unittest's own loader in a worktree of the fix commit (module, class and method body inside that tree, never run), and a gtest id must be in the fix commit's own CI test list fetched from origin.
- Gates at this tip: `python -m unittest tests.scripts.test_release_notes` -> 24 tests OK; `python -m unittest tests.scripts.test_todo_graph` -> 58 tests OK.
