# Review Receipt - D07 T03 §4, loop 2

- **Section:** `D07 T03 §4` (`todo/07-release/TODO-03-listening-to-users.md`)
- **Candidate:** `a05ffe9..2d7324b` (`scripts/release-notes.py`, `tests/scripts/test_release_notes.py`, `docs/support/user-bugs.md`, `.github/pull_request_template.md`, `AGENTS.md`)
- **Date:** 2026-10-21
- **Loop 1:** `docs/reviews/07-release/D07-T03-s4.md` spent all 22 rounds and ended at the cap; its last P1 (a C++ source scan cannot decide what compiles) was filed as `D07 T03 §9`, which replaced the scanner with each `main` build's CI test list recorded as a git note and is stamped (`docs/reviews/d07-t03-s9.json`). This loop reviews §4 as it now stands.

## Self-review

- The existence item is re-ticked: a Python id is resolved by unittest's own loader in a worktree of the fix commit (module, class and method body inside that tree, never run), and a gtest id must be in the fix commit's own CI test list fetched from origin.
- Gates at this tip: `python -m unittest tests.scripts.test_release_notes` -> 24 tests OK; `python -m unittest tests.scripts.test_todo_graph` -> 58 tests OK.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:10:30Z
- Effort: medium
- Lines: 23
- Digest: 38b3eb213ece5a2a

## adversarial: needs-attention

1. **P1: The Python probe accepts an ID that unittest cannot load.** `scripts/release-notes.py:88` substitutes `cls(method)` for unittest’s named-test loader. For a `TestCase` containing `@classmethod def test_case(cls): return None`, the probe prints its success nonce, but `unittest.defaultTestLoader.loadTestsFromName("module.Case.test_case")` raises `TypeError: ... returned None, not a test`. A fix can therefore satisfy the check with a regression ID that fails when invoked through `python -m unittest <id>`. Reproduced using the candidate’s actual probe with an in-memory module. `tests/scripts/test_release_notes.py:305` explicitly classifies this case as valid, preserving the mismatch.

## consistency: approve

## integration: approve

## source-defect: needs-attention

Finding 1 violates the stated unittest loader semantics. Named classmethods enter unittest’s callable-factory branch; constructing `cls(method)` does not reproduce that behavior. Reject unsupported descriptors without executing their bodies, or implement verification consistent with the documented invocation.

## design: advisory

Not applicable.

## record: needs-attention

2. **P2: The checked Python verification requirement overstates the implementation.** `todo/07-release/TODO-03-listening-to-users.md:153` marks complete the corrected requirement that Python IDs are resolved by unittest’s own loader. Finding 1 reproduces a disagreement between that loader and the candidate’s substitute. The item needs correction or implementation changes before completion is claimed.

Verification: four read-only parser/documentation tests passed. The reported 24-test result was not independently reproduced because the full suite creates files and worktrees. No files were modified.

OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P1): a Python regression test must be a plain instance method (`types.FunctionType` in its owner class). For that shape unittest's loader builds `cls(method)`, which is exactly what the probe does; a classmethod, staticmethod or other descriptor takes the loader's callable path and is refused without being run (`Classmethod` fixture moved to refused; its marker still never appears).
- Finding 2 (P2): the §4 correction now says a Python id must be a plain test method built the way unittest's loader builds it. Tests: `Ran 24 tests ... OK`.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:13:09Z
- Effort: medium
- Lines: 37
- Digest: be7bcd4a64a72bea

## adversarial: needs-attention

1. **P1: The Python probe still accepts an ID that unittest cannot load.** `scripts/release-notes.py:96` validates the constructed case but omits the loader’s check of the instance’s resolved method. Reproduce with this module at the fix commit:

   ```python
   import unittest

   class Case(unittest.TestCase):
       def __init__(self, methodName="runTest"):
           super().__init__(methodName)
           self.test_case = lambda: None

       def test_case(self):
           pass
   ```

   Name `module.Case.test_case` in `Regression-Test:`. The candidate probe accepts it. However, `unittest.defaultTestLoader.loadTestsFromName("module.Case.test_case")` takes its callable branch and raises `TypeError: Case.test_case() missing 1 required positional argument: 'self'`. Thus an unusable regression ID passes verification. Reproduced using the actual probe and an in-memory module, without writing files.

## consistency: approve

## integration: approve

## source-defect: needs-attention

Finding 1: a plain function in the class dictionary does not guarantee unittest constructs a suite containing `cls(method)`. Its loader additionally checks `getattr(instance, method)` before choosing that branch. The probe must account for this without executing the test body.

## design: advisory

Not applicable.

## record: needs-attention

Finding 1 contradicts the checked existence-verification requirement at `todo/07-release/TODO-03-listening-to-users.md:153`. The round-1 correction does not fully establish equivalent loader behavior.

Four read-only parser/documentation tests passed. The full 24-test claim was not independently verified because that suite writes fixture repositories and worktrees. No files were modified.

OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P1): after building the case, the probe looks the method up on the instance as unittest's `loadTestsFromName` does; a plain function there (an attribute set in `__init__`, a staticmethod) means the loader would take its callable path, so the id is refused (`ShadowedByInstance` fixture). Mutation: the instance check removed -> `HostileFixtureTest` fails. Tests: `Ran 24 tests ... OK`.