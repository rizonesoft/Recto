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

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:16:39Z
- Effort: medium
- Lines: 39
- Digest: 69d0d000c9ec22a0

## adversarial: needs-attention

1. **P1: Instance shadowing still bypasses method verification.** `scripts/release-notes.py:101` rejects only `FunctionType`. A built-in bound method passes, although it replaces the repository method whose provenance was checked:

   ```python
   import unittest

   class Case(unittest.TestCase):
       def __init__(self, methodName="runTest"):
           super().__init__(methodName)
           self.test_case = [].clear

       def test_case(self):
           raise AssertionError("Regression body must run")
   ```

   Name `module.Case.test_case` in `Regression-Test:`. Reproduced with the candidate’s actual probe and an in-memory module: the probe prints its success nonce, and unittest reports success without executing the regression body. Assigning `None` instead also passes verification, then errors during execution.

   Validate the instance’s resolved method and its provenance, not merely the class dictionary function and the absence of `FunctionType`.

## consistency: approve

## integration: approve

## source-defect: needs-attention

Finding 1 (`scripts/release-notes.py:101`, **P1**): unittest’s loader accepts these instances. Mirroring its branch condition alone does not establish that the callable executed is the repository method already checked.

## design: advisory

Not applicable.

## record: needs-attention

Finding 1 contradicts the checked test-verification requirement at `todo/07-release/TODO-03-listening-to-users.md:153`: verification can accept a replacement callable outside the fix commit’s code.

Four read-only parser/documentation tests passed. The full 24-test result was not independently reproduced because that suite creates files and worktrees. No files were modified.

OVERALL: needs-attention

## Fixes after round 3

- Finding 1 (P1): the instance lookup must return the verified method itself, bound to this case (`types.MethodType`, `__self__ is case`, `__func__ is` the class-dictionary function already checked for provenance); a built-in bound method, `None` or any other replacement set on the instance is refused (`ShadowedByBuiltin` fixture). Mutation: back to the plain-function check -> `HostileFixtureTest` fails. Tests: `Ran 24 tests ... OK`.

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:19:53Z
- Effort: medium
- Lines: 35
- Digest: bb505cb3b22b4bd7

## adversarial: needs-attention

1. **P1, `scripts/release-notes.py:89`: External decorators bypass test-body provenance.** `inspect.unwrap()` discards the callable wrapper before checking its source. The later bound-method check accepts that wrapper, even when its implementation lives outside the fix commit and never calls the regression body.

   Reproduce with an external decorator:
   ```python
   def replace(fn):
       @functools.wraps(fn)
       def wrapper(self):
           pass
       return wrapper
   ```
   Apply it to a repository `TestCase.test_regression` whose body raises `AssertionError`. The candidate’s actual probe printed its success nonce; unittest reported `1 run, 0 failures, 0 errors`, without executing the regression body. This was reproduced entirely in memory.

   Check the actual callable’s provenance before unwrapping, and validate each wrapper layer.

## consistency: approve

## integration: approve

## source-defect: needs-attention

Finding 1 also applies at `scripts/release-notes.py:108` (**P1**): unittest executes the bound wrapper, not the function returned by `inspect.unwrap()`. The provenance check and execution target differ.

## design: advisory

Not applicable.

## record: needs-attention

Finding 1 contradicts the checked verification requirement at `todo/07-release/TODO-03-listening-to-users.md:153` (**P1**): an external replacement callable still counts as the fix commit’s regression test.

Four read-only parser/documentation tests passed. The full 24-test claim was not independently verified because that suite creates files and worktrees. No files were modified.

OVERALL: needs-attention

## Fixes after round 4

- Finding 1 (P1): provenance is checked layer by layer from the function unittest actually runs: the class-dictionary function, then each `__wrapped__` inside it, must each be a plain function whose code is in the fix commit's tree. An outside decorator wrapping a repository test (`DecoratedFromOutside`) and a repository decorator wrapping an outside test (`LocalWrapperOfOutside`) are both refused. Mutations: unwrap before checking -> `FAILED (failures=1)`; stop after the outer layer -> `FAILED (failures=1)`. Tests: `Ran 24 tests ... OK`.

## Round 5: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:25:36Z
- Effort: medium
- Lines: 37
- Digest: 647dd629bcf5e12f

## adversarial: needs-attention

1. **P2, `scripts/release-notes.py:90`: Valid `unittest.mock.patch` tests are rejected.** Requiring every wrapper’s code to reside inside the worktree rejects standard-library decorators that execute a repository-owned test.

   Reproduce by committing this test and naming `module.Case.test_regression` in a fix’s trailer:

   ```python
   import unittest
   from unittest.mock import patch

   class Case(unittest.TestCase):
       @patch("os.getcwd", return_value="fixture")
       def test_regression(self, getcwd):
           self.assertEqual(getcwd(), "fixture")
   ```

   The outer wrapper lives in `unittest/mock.py`, so the probe exits 3 and the release report marks the existing test invalid. Reproduced in memory using the candidate’s actual probe: exit 3, no success nonce. Unittest loaded and executed the same test: `1 run, 0 failures, 0 errors`.

## consistency: approve

## integration: needs-attention

Finding 1 (**P2**, `scripts/release-notes.py:90`) blocks release-note generation and §7’s JSON build input for fixes using ordinary mock decorators. The documented test-ID contract does not exclude these tests.

## source-defect: needs-attention

Finding 1 (**P2**, `scripts/release-notes.py:90`): an external wrapper’s location does not establish that the underlying repository test is absent. Standard-library `patch` preserves and executes that test.

## design: advisory

Not applicable.

## record: advisory

The claimed 24-test pass remains independently unverified. Running `python -B -m unittest tests.scripts.test_release_notes` produced four completed tests and two fixture-setup errors because this read-only sandbox cannot create temporary directories. No files were modified.

OVERALL: needs-attention

## Fixes after round 5

- Finding 1 (P2): a wrapper layer whose code is `unittest/mock.py` itself (`patch` and friends, which always call what they wrap) is accepted; trust is decided by the code's file, because `functools.wraps` copies `__module__` from the test. The innermost function, the test body, must still come from the fix commit's tree, and any other outside wrapper is refused (`MockPatched` accepted, `DecoratedFromOutside` and `LocalWrapperOfOutside` still refused). Mutation: trusting any wrapper -> `HostileFixtureTest` fails. `docs/support/user-bugs.md` states the rule. Tests: `Ran 24 tests ... OK`.

## Round 6: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:31:08Z
- Effort: medium
- Lines: 37
- Digest: e1dbb525c19797bb

## adversarial: needs-attention

1. **P1, `scripts/release-notes.py:82`: Coroutine methods count as regression tests even when unittest never executes their bodies.** `types.FunctionType` includes `async def`, and the remaining checks accept it on an ordinary `unittest.TestCase`.

   Reproduce by committing this as `test_example.py` and naming `test_example.Case.test_regression` in the trailer:

   ```python
   import unittest

   class Case(unittest.TestCase):
       async def test_regression(self):
           raise AssertionError("regression still broken")
   ```

   I exercised the candidate’s actual probe with this fixture in memory. It printed the success nonce. Unittest reported `1 run; 0 failures; 0 errors`, with a warning that the coroutine was never awaited. The check therefore accepts a regression body that cannot detect the bug. Reject coroutine methods on ordinary `TestCase`, while preserving support for correctly executed `IsolatedAsyncioTestCase` methods.

## consistency: approve

## integration: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:82`) lets invalid regression evidence pass into the release report and §7’s JSON input without a violation.

## source-defect: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:82`) confuses a loadable method with an executable unittest test body. Ordinary `TestCase` does not await coroutine methods.

## design: advisory

Not applicable.

## record: needs-attention

Finding 1 contradicts the checked regression-verification requirement at `todo/07-release/TODO-03-listening-to-users.md:153`.

Four read-only parser/documentation tests passed. The full 24-test claim remains independently unverified because its fixtures create files and worktrees. No files were modified.

OVERALL: needs-attention

## Fixes after round 6

- Finding 1 (P1): a test body unittest would not run is refused: a coroutine on a plain `TestCase` (never awaited), and a generator or async generator on any case. A coroutine on `unittest.IsolatedAsyncioTestCase`, which awaits it, is accepted (`AsyncOnPlain` and `Generator` refused, `AsyncOnIsolated` accepted). Mutation: the coroutine rule removed -> `HostileFixtureTest` fails. Tests: `Ran 24 tests ... OK`.

## Round 7: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:34:30Z
- Effort: medium
- Lines: 45
- Digest: 2cf43b6be4d2b723

## adversarial: needs-attention

1. **P1, `scripts/release-notes.py:105`: A synchronous decorator lets an async regression body pass verification without executing.** The probe permits coroutine layers whenever the class inherits `IsolatedAsyncioTestCase`. However, unittest only awaits the outer callable, which can be synchronous.

   Reproduce with this repository-local module and trailer `Regression-Test: test_example.Case.test_regression`:

   ```python
   import unittest
   from functools import wraps

   def wrapper(fn):
       @wraps(fn)
       def call(self):
           return fn(self)
       return call

   class Case(unittest.IsolatedAsyncioTestCase):
       @wrapper
       async def test_regression(self):
           raise AssertionError("regression still broken")
   ```

   Exercised the candidate’s actual probe with an in-memory fixture: it printed the success nonce. Unittest reported `1 run, 0 failures, 0 errors`; the body never executed, and the coroutine was never awaited. Validate awaitability of the callable unittest actually invokes, not merely the class and individual wrapper layers.

## consistency: approve

## integration: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:105`) admits invalid regression evidence without a violation into release reporting and §7’s JSON input.

## source-defect: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:105`) misrepresents `IsolatedAsyncioTestCase` semantics. Its `_callMaybeAsync` checks `inspect.iscoroutinefunction` on the outer callable; it does not automatically await a coroutine returned by a synchronous wrapper.

## design: advisory

Not applicable.

## record: needs-attention

Finding 1 contradicts the checked regression-verification requirement at `todo/07-release/TODO-03-listening-to-users.md:153`.

The claimed 24-test pass remains independently unverified: `python -B -m unittest tests.scripts.test_release_notes` completed four tests, with two fixture-setup errors because the read-only sandbox cannot create temporary directories. No files were modified.

OVERALL: needs-attention

## Fixes after round 7

- Finding 1 (P1): a coroutine anywhere in the layer chain is accepted only when the outermost callable, the one unittest calls, is itself a coroutine function on an `IsolatedAsyncioTestCase` (which awaits it); a synchronous wrapper around an `async` body is refused (`SyncWrappedAsync`), as is any coroutine on a plain `TestCase`. Mutation: the outermost check removed -> `HostileFixtureTest` fails. Tests: `Ran 24 tests ... OK`.

## Round 8: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:38:27Z
- Effort: medium
- Lines: 36
- Digest: ee7200a96efc157d

## adversarial: needs-attention

1. **P1, `scripts/release-notes.py:135`: Skipped test classes count as verified regression evidence.** The probe prints its success nonce without checking `__unittest_skip__`. Reproduce with this module at the fix commit and trailer `Regression-Test: test_example.Case.test_regression`:

   ```python
   import unittest

   @unittest.skip("temporarily disabled")
   class Case(unittest.TestCase):
       def test_regression(self):
           raise AssertionError("bug still present")
   ```

   The candidate’s actual probe accepted an in-memory reproduction and printed its success nonce. Unittest reported one skipped test, zero failures and success. The regression body never executes, but this fix produces no regression-test violation. Reject class-level and method-level skip flags, including inherited flags.

## consistency: approve

## integration: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:135`) allows skipped regression evidence into release reporting and §7’s JSON input without a violation.

## source-defect: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:135`) misses unittest’s execution semantics: `TestCase.run()` checks the class and method skip flags and returns before executing the body. Constructing a valid case and verifying its bound method does not detect this.

## design: advisory

Not applicable.

## record: needs-attention

Finding 1 contradicts the checked regression-verification requirement at `todo/07-release/TODO-03-listening-to-users.md:153`.

Four read-only parser/documentation tests passed. The full 24-test claim remains independently unverified because its fixtures create files and worktrees. No files were modified.

OVERALL: needs-attention

## Fixes after round 8

- Finding 1 (P1): a test unittest would skip, or run as an expected failure, is refused: `__unittest_skip__` or `__unittest_expecting_failure__` on the class (inherited included) or on the method (outer layer or its unwrapped function). Fixtures `SkippedClass`, `SkippedMethod`, `InheritsSkip` and `ExpectedFailure` are refused. Mutation: the flag check removed -> `HostileFixtureTest` fails. Tests: `Ran 24 tests ... OK`.

## Round 9: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T13:42:35Z
- Effort: medium
- Lines: 30
- Digest: dbe5615a4cfa7861

## adversarial: needs-attention

1. **P1, `scripts/release-notes.py:191`: Disabled gtests count as verified regression tests.** The parser retains `DISABLED_` cases and suites, and line 305 accepts their membership without checking whether they run. Reproduce with a fix carrying `Regression-Test: PdfXref.DISABLED_EmptyTable` and this CI note:

   ```text
   PdfXref.
     DISABLED_EmptyTable
   ```

   In-memory verification returned `test_problem(...) == None`. The same happened for `DISABLED_PdfXref.EmptyTable`. A disabled parameterized instantiation, `DISABLED_Instance/PdfXref.EmptyTable/0`, is additionally normalized to the apparently enabled `PdfXref.EmptyTable`. These fixes produce no regression-test violation although their tests do not execute by default. Reject disabled entries before discarding instantiation prefixes.

## consistency: approve

## integration: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:305`) affects genuine §9 notes: CI records `--gtest_list_tests`, which includes disabled tests. Invalid evidence consequently reaches closing comments and §7’s JSON input without a violation.

## source-defect: needs-attention

Finding 1 (**P1**, `scripts/release-notes.py:191`) conflates registration with execution eligibility. GoogleTest’s `ListTestsMatchingFilter()` checks `matches_filter_`, while execution separately excludes disabled tests through `is_runnable`. [GoogleTest source](https://github.com/google/googletest/blob/main/googletest/src/gtest.cc)

## design: advisory

Not applicable.

## record: advisory

The claimed 24-test result was not independently reproduced: that suite creates files and repositories. Four read-only parser/documentation tests passed; the disabled-test reproductions ran entirely in memory. No files were modified.

OVERALL: needs-attention

## Fixes after round 9

- Finding 1 (P1): `parse_gtest_list` leaves out disabled tests, which gtest does not run: a `DISABLED_` case, a `DISABLED_` suite, or a `DISABLED_` instantiation prefix (checked before the prefix is dropped), so `PdfXref.DISABLED_EmptyTable`, `DISABLED_PdfOld.Case` and `DISABLED_Instance/PdfParam.Draws/0` are never evidence. Mutation: disabled entries kept -> `GtestListTest` fails. Tests: `Ran 25 tests ... OK`.