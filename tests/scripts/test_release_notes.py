"""Tests for scripts/release-notes.py (D07 T03 §4): user fixes mapped to releases, one regression test per fix."""
import contextlib
import importlib.util
import io
import json
import pathlib
import subprocess
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("release_notes", REPO / "scripts" / "release-notes.py")
release_notes = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release_notes)

URL = "https://github.com/rizonesoft/Recto"


def _git(repo, *args):
    subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, text=True)


def _commit(repo, files, message):
    for name, text in files.items():
        path = repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
    _git(repo, "add", "-A")
    # The message goes through a file so control characters in it reach git byte for byte.
    message_file = repo.parent / f"{repo.name}-message.txt"
    message_file.write_text(message, encoding="utf-8", newline="\n")
    _git(repo, "commit", "-q", "--allow-empty", "--cleanup=verbatim", "-F", str(message_file))
    return subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], check=True, capture_output=True,
                          text=True).stdout.strip()


def _init(tmp):
    repo = pathlib.Path(tmp.name) / "repo"
    repo.mkdir()
    _git(repo, "init", "-q", "-b", "main")
    _git(repo, "config", "user.name", "Fixture")
    _git(repo, "config", "user.email", "fixture@example.invalid")
    _git(repo, "config", "commit.gpgsign", "false")
    return repo


def _gtest_list(tmp, suites):
    """A `--gtest_list_tests` file listing {suite: [cases]}."""
    path = pathlib.Path(tmp.name) / "gtest-list.txt"
    path.write_text("".join(f"{suite}.\n" + "".join(f"  {case}\n" for case in cases)
                            for suite, cases in suites.items()), encoding="utf-8")
    return path


def _run_main(repo, *args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        code = release_notes.main(list(args), repo=repo)
    return code, out.getvalue()


class ReleaseNotesTest(unittest.TestCase):
    """A fixture repository: v1.0.0, then five fixes and an unrelated commit, then v1.1.0, then a late test."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        repo = cls.repo = _init(cls._tmp)
        cpp = "#include <gtest/gtest.h>\n\nTEST(PdfXref, ParsesTable)\n{\n}\n"
        py = "import unittest\n\n\nclass ModTest(unittest.TestCase):\n    def test_a(self):\n        pass\n"
        _commit(repo, {"tests/recto-tests/xref_tests.cpp": cpp, "tests/scripts/test_mod.py": py}, "initial")
        _git(repo, "tag", "v1.0.0")
        # An existing test file gains a real case: passes.
        cls.gtest_fix = _commit(
            repo, {"tests/recto-tests/xref_tests.cpp": cpp + "\nTEST_F(PdfXref, EmptyTable)\n{\n}\n"},
            "pdf: accept an empty xref table\n\nFixes-User: #12\nRegression-Test: PdfXref.EmptyTable")
        # A Python test in an existing module; it also fixes a second issue.
        cls.python_fix = _commit(
            repo, {"tests/scripts/test_mod.py": py + "\n    def test_b(self):\n        pass\n"},
            "scripts: handle b\n\nFixes-User: #14, #20\nRegression-Test: tests.scripts.test_mod.ModTest.test_b")
        # Documentation only: no test, excused or not depending on the call.
        cls.docs_fix = _commit(repo, {"docs/help.md": "fixed wording\n"}, "docs: fix the help wording\n\nFixes-User: #15")
        # Names a test that only exists in a later commit: a violation.
        cls.late_fix = _commit(repo, {"src/c.cpp": "int c;\n"},
                               "core: fix c\n\nFixes-User: #16\nRegression-Test: PdfLate.Case")
        # A second fix for #12.
        cls.second_fix = _commit(
            repo, {"src/d.cpp": "int d;\n"},
            "pdf: finish the xref fix\n\nFixes-User: #12\nRegression-Test: PdfXref.ParsesTable")
        _commit(repo, {"README.md": "unrelated\n"}, "repo: unrelated change")
        _git(repo, "tag", "v1.1.0")
        _commit(repo, {"tests/recto-tests/late_tests.cpp": "TEST(PdfLate, Case)\n{\n}\n"}, "tests: late test")
        cls.listed = {"PdfXref.ParsesTable", "PdfXref.EmptyTable", "PdfLate.Case"}
        cls.list_file = _gtest_list(cls._tmp, {"PdfXref": ["ParsesTable", "EmptyTable"], "PdfLate": ["Case"]})

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def run_main(self, *args):
        return _run_main(self.repo, *args)

    def report(self, **kwargs):
        kwargs.setdefault("gtest_list", self.listed)
        return release_notes.report(self.repo, "v1.0.0", "v1.1.0", **kwargs)

    def test_every_fixes_user_commit_is_an_entry(self):
        result = self.report()
        self.assertEqual([fix["sha"] for fix in result["fixes"]],
                         [self.gtest_fix, self.python_fix, self.docs_fix, self.late_fix, self.second_fix])
        self.assertEqual([issue["number"] for issue in result["issues"]], [12, 14, 15, 16, 20])

    def test_closing_comments_name_the_release_and_every_commit(self):
        comments = {issue["number"]: issue["comment"] for issue in self.report()["issues"]}
        release = f"{URL}/releases/tag/v1.1.0"
        self.assertEqual(comments[12], f"Fixed in v1.1.0 ({self.gtest_fix[:7]}, {self.second_fix[:7]}, {release})")
        self.assertEqual(comments[14], f"Fixed in v1.1.0 ({self.python_fix[:7]}, {release})")

    def test_existing_tests_pass_and_missing_ones_are_violations(self):
        violations = self.report()["violations"]
        self.assertEqual(len(violations), 2, violations)
        self.assertTrue(any(self.docs_fix[:7] in v and "without a Regression-Test" in v for v in violations))
        self.assertTrue(any(self.late_fix[:7] in v and "PdfLate.Case is not declared" in v for v in violations))
        for sha in (self.gtest_fix, self.python_fix, self.second_fix):
            self.assertFalse(any(sha[:7] in v for v in violations), sha)

    def test_gtest_ids_need_the_release_build_list(self):
        unverified = self.report(gtest_list=None)["violations"]
        for sha in (self.gtest_fix, self.second_fix):
            self.assertTrue(any(sha[:7] in v and "unverified" in v for v in unverified), unverified)
        missing = self.report(gtest_list={"PdfXref.ParsesTable"})["violations"]
        self.assertTrue(any(self.gtest_fix[:7] in v and "not in the release build's test list" in v for v in missing))

    def test_allow_no_test_excuses_one_commit_and_is_reported(self):
        result = self.report(allowances={self.docs_fix[:10]: "documentation only"})
        self.assertEqual(len(result["violations"]), 1, result["violations"])
        docs = next(fix for fix in result["fixes"] if fix["sha"] == self.docs_fix)
        self.assertEqual(docs["allowed_no_test"], "documentation only")
        code, text = self.run_main("v1.0.0", "v1.1.0", "--gtest-list", str(self.list_file),
                                   "--allow-no-test", f"{self.docs_fix[:10]}=documentation only")
        self.assertIn("no test; excused: documentation only", text)
        self.assertEqual(code, 1)  # the late test is still a violation

    def test_an_unused_allowance_is_a_violation(self):
        result = self.report(allowances={"abcdef1": "typo"})
        self.assertTrue(any("--allow-no-test abcdef1" in v for v in result["violations"]))

    def test_issues_group_by_their_most_severe_kind_label(self):
        labels = {12: ["bug", "crash", "engine"], 14: ["bug"], 16: ["bug", "data-loss"]}
        groups = {issue["number"]: issue["group"] for issue in self.report(issue_labels=labels)["issues"]}
        self.assertEqual(groups, {12: "crash", 14: "bug", 15: "unlabelled", 16: "data-loss", 20: "unlabelled"})

    def test_text_and_json_output_and_exit_codes(self):
        code, text = self.run_main("v1.0.0", "v1.1.0", "--gtest-list", str(self.list_file))
        self.assertEqual(code, 1)
        self.assertIn(f"  #12: Fixed in v1.1.0 ({self.gtest_fix[:7]}, {self.second_fix[:7]}, ", text)
        self.assertIn("Violations:", text)
        code, text = self.run_main("v1.0.0", "v1.1.0", "--gtest-list", str(self.list_file), "--json")
        self.assertEqual(json.loads(text)["to"], "v1.1.0")
        code, text = self.run_main("v1.1.0", "HEAD")
        self.assertEqual(code, 0)
        self.assertIn("none", text)

    def test_unreadable_input_exits_2(self):
        bad_list = pathlib.Path(self._tmp.name) / "not-a-list.txt"
        bad_list.write_text("Note: Google Test filter = *\n", encoding="utf-8")
        self.assertEqual(self.run_main("v0.9.0", "v1.1.0")[0], 2)
        self.assertEqual(self.run_main("v1.0.0", "v1.1.0", "--allow-no-test", "nosha")[0], 2)
        self.assertEqual(self.run_main("v1.0.0", "v1.1.0", "--issues", str(self.repo / "missing.json"))[0], 2)
        self.assertEqual(self.run_main("v1.0.0", "v1.1.0", "--gtest-list", str(bad_list))[0], 2)
        bad_issues = pathlib.Path(self._tmp.name) / "bad-issues.json"
        for labels in ("1", "{}", "0", "false", '""'):
            bad_issues.write_text(f'[{{"number": 12, "labels": {labels}}}]', encoding="utf-8")
            self.assertEqual(self.run_main("v1.0.0", "v1.1.0", "--issues", str(bad_issues))[0], 2, labels)

    def test_root_covers_the_first_release(self):
        result = release_notes.report(self.repo, "root", "v1.0.0")
        self.assertEqual(result["fixes"], [])

    def test_worktrees_are_cleaned_up(self):
        self.report()
        listed = subprocess.run(["git", "-C", str(self.repo), "worktree", "list"], capture_output=True, text=True)
        self.assertEqual(len(listed.stdout.strip().splitlines()), 1, listed.stdout)


class GtestListTest(unittest.TestCase):
    def test_reads_plain_typed_and_parameterised_entries(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        path = pathlib.Path(tmp.name) / "list.txt"
        path.write_text("Running main() from C:/Program Files/gtest/src/gtest_main.cc\nPdfXref.\n  ParsesTable\n"
                        "  EmptyTable\nInstance/PdfRender.\n  Draws/0  # GetParam() = 1\n  Draws/1  # GetParam() = 2\n"
                        "PdfTyped/0.  # TypeParam = int\n  Holds\nPdfTyped/1.  # TypeParam = long\n  Holds\n"
                        "Ints/PdfTypedP/0.  # TypeParam = int\n  Grows\nNamed/int.  # TypeParam = int\n  Works\n"
                        "Ints/NamedP/int.  # TypeParam = int\n  Grows\n", encoding="utf-8")
        self.assertEqual(release_notes.read_gtest_list(path),
                         {"PdfXref.ParsesTable", "PdfXref.EmptyTable", "PdfRender.Draws", "PdfTyped.Holds",
                          "PdfTypedP.Grows", "Named.Works", "NamedP.Grows"})


class HostileFixtureTest(unittest.TestCase):
    """Test ids that only look real, trailers that only look separate, and subjects holding separator characters."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        repo = cls.repo = _init(cls._tmp)
        cpp = (
            "// TEST(Commented, Case)\n"
            "/* TEST(Block, Case) */\n"
            "#if 0\nTEST(Disabled, Case)\n{\n}\n#else\nTEST(Enabled, Case)\n{\n}\n#endif\n"
            "#if 1\nTEST(Taken, Case)\n{\n}\n#else\nTEST(NotTaken, Case)\n{\n}\n#endif\n"
            "#if (0)\nTEST(Parenthesised, Case)\n{\n}\n#endif\n"
            "#define UNUSED TEST(Macro, Case) {}\n"
            "// a comment that continues \\\nTEST(Continued, Case)\n{\n}\n"
            "const char* s = \"TEST(Quoted, Case)\";\n"
            "const char* r = R\"x(TEST(Raw, Case))x\";\n"
            "TEST (Multi,\n      Line)\n{\n}\n"
            "TYPED_TEST(Typed, Case)\n{\n}\n"
            "TEST(Spliced, Ca\\\nse)\n{\n}\n"
            "constexpr wchar_t c = L'\"'; TEST(Prefixed, Char) {}\n"
            "constexpr int n = 1'000; TEST(Separated, Num) {}\n"
            "constexpr double d = .1'0; TEST(Fraction, Case) {}\n"
            "const char* q = R\"(abc)\\\n\"; TEST(InRaw, Case) {} //\n)\"; TEST(AfterRaw, Case) {}\n"
            "int z = 1\\\n'000; TEST(SplicedNumber, Case) {}\n"
            "// spliced comment \\\nTEST(InComment, Case) {}\n"
            "/\\\n* opener split by a splice\nTEST(InSplitBlock, Case) {}\n*/\n"
            "/* closer split by a splice *\\\n/\nTEST(AfterSplitClose, Case) {}\n"
            # A splice before a raw string's quote, and a splice after an escaped backslash in a string: both
            # keep the TEST inside the literal.
            "const char* p = R\\\n\"(a\" TEST(SplitRawPrefix, Case) {} \")\";\n"
            "const char* e = \"abc\\\\\\\nn TEST(SplitEscape, Case) {}\";\n"
        )
        py = (
            "import abc\nimport unittest\n"
            "from unittest import TestCase as Aliased\n\n\n"
            "class Abstract(abc.ABC, unittest.TestCase):\n    @abc.abstractmethod\n    def test_abstract(self):\n        pass\n\n\n"
            "class PretendTestCase:\n    pass\n\n\n"
            "class Pretend(PretendTestCase):\n    def test_fake(self):\n        pass\n\n\n"
            "class Ordinary:\n    def test_missing(self):\n        pass\n\n    def save(self):\n        pass\n\n\n"
            "class Base(unittest.TestCase):\n    pass\n\n\n"
            "class Derived(Base):\n    def test_real(self):\n        pass\n\n    def helper(self):\n        pass\n\n\n"
            "class Child(Derived):\n    pass\n\n\n"
            "class ViaAlias(Aliased):\n    def test_alias(self):\n        pass\n"
        )
        # A test whose body leaves a marker: verifying it must never run it.
        cls.marker = pathlib.Path(cls._tmp.name) / "test-body-ran.txt"
        py += ("\n\nimport types\n\n\nclass FakeNew(unittest.TestCase):\n"
               "    def __new__(cls, method):\n        return types.SimpleNamespace(_testMethodName=method)\n\n"
               "    def test_case(self):\n        pass\n")
        py += ("\n\nclass WithClassmethod(unittest.TestCase):\n    @classmethod\n    def test_case(cls):\n"
               f"        open({str(cls.marker)!r}, 'w').close()\n")
        # A TestCase that lives outside the repository, which a repository module re-exports or inherits from.
        outside = pathlib.Path(cls._tmp.name) / "outside"
        outside.mkdir()
        (outside / "outside_mod.py").write_text(
            "import unittest\n\n\nclass OutsideTest(unittest.TestCase):\n    def test_outside(self):\n        pass\n",
            encoding="utf-8")
        reexport = (f"import sys\nimport unittest\nsys.path.append({str(outside)!r})\n"
                    "from outside_mod import OutsideTest as ReExported\n\n\n"
                    "class Local(ReExported):\n    pass\n\n\n"
                    "class Assigned(unittest.TestCase):\n    test_assigned = ReExported.test_outside\n")
        noisy = ("import os\nimport unittest\nos.write(1, bytes([255, 129]) + b'\\n')\n\n\n"
                 "class NoisyTest(unittest.TestCase):\n    def test_noisy(self):\n        pass\n")
        _commit(repo, {"tests/recto-tests/hostile_tests.cpp": cpp, "tests/scripts/test_sample.py": py,
                       "tests/scripts/test_exits.py": "raise SystemExit(0)\n",
                       "tests/scripts/test_sleeps.py": "import time\ntime.sleep(60)\n",
                       "tests/scripts/test_reexport.py": reexport, "tests/scripts/test_noisy.py": noisy,
                       # A byte-order mark before `#if 0`, and lone-CR line endings.
                       "tests/recto-tests/bom_tests.cpp":
                           "\ufeff#if 0\nTEST(BomDisabled, Case) {}\n#endif\nTEST(BomEnabled, Case) {}\n",
                       "tests/recto-tests/cr_tests.cpp": "// header comment\rTEST(CrOnly, Case) {}\r"},
                "initial")
        _git(repo, "tag", "v2.0.0")
        cls.fake = {
            "Commented": "Commented.Case", "Block": "Block.Case", "Disabled": "Disabled.Case",
            "NotTaken": "NotTaken.Case", "Parenthesised": "Parenthesised.Case", "Macro": "Macro.Case",
            "Continued": "Continued.Case", "Quoted": "Quoted.Case", "Raw": "Raw.Case",
            "InRaw": "InRaw.Case", "InComment": "InComment.Case", "InSplitBlock": "InSplitBlock.Case",
            "SplitRawPrefix": "SplitRawPrefix.Case", "SplitEscape": "SplitEscape.Case",
            "BomDisabled": "BomDisabled.Case",
            "Pretend": "tests.scripts.test_sample.Pretend.test_fake",
            "Ordinary": "tests.scripts.test_sample.Ordinary.test_missing",
            "Save": "tests.scripts.test_sample.Ordinary.save",
            "Helper": "tests.scripts.test_sample.Derived.helper",
            "Abstract": "tests.scripts.test_sample.Abstract.test_abstract",
            "ExitsOnImport": "tests.scripts.test_exits.Missing.test_missing",
            "SleepsOnImport": "tests.scripts.test_sleeps.Slow.test_slow",
            "ReExported": "tests.scripts.test_reexport.ReExported.test_outside",
            "InheritedFromOutside": "tests.scripts.test_reexport.Local.test_outside",
            "AssignedFromOutside": "tests.scripts.test_reexport.Assigned.test_assigned",
            "FakeNew": "tests.scripts.test_sample.FakeNew.test_case",
            "Stdlib": "unittest.case.TestCase.test_missing",
        }
        cls.real = {
            "Enabled": "Enabled.Case", "Taken": "Taken.Case", "Multi": "Multi.Line", "Typed": "Typed.Case",
            "Derived": "tests.scripts.test_sample.Derived.test_real",
            "Inherited": "tests.scripts.test_sample.Child.test_real",
            "Alias": "tests.scripts.test_sample.ViaAlias.test_alias",
            "Noisy": "tests.scripts.test_noisy.NoisyTest.test_noisy",
            "Classmethod": "tests.scripts.test_sample.WithClassmethod.test_case", "Spliced": "Spliced.Case",
            "Prefixed": "Prefixed.Char", "Separated": "Separated.Num", "Fraction": "Fraction.Case",
            "AfterRaw": "AfterRaw.Case", "SplicedNumber": "SplicedNumber.Case",
            "AfterSplitClose": "AfterSplitClose.Case", "BomEnabled": "BomEnabled.Case", "CrOnly": "CrOnly.Case",
        }
        cls.shas = {}
        for name, test_id in {**cls.fake, **cls.real}.items():
            cls.shas[name] = _commit(repo, {f"src/{name}.txt": name}, f"fix {name}\n\nFixes-User: #1\n"
                                                                     f"Regression-Test: {test_id}\n")
        # One physical trailer line holding a vertical tab: git reads it as one Fixes-User value.
        cls.shas["VerticalTab"] = _commit(
            repo, {"src/vt.txt": "vt"}, "fix vt\n\nFixes-User: #2\x0bRegression-Test: Enabled.Case\n")
        # An issue "number" too long to be one is reported, not converted.
        cls.shas["HugeIssue"] = _commit(repo, {"src/huge.txt": "huge"},
                                        "fix huge\n\nFixes-User: #" + "1" * 5000 + "\nRegression-Test: Enabled.Case\n")
        # Separator characters in a subject must not break the report.
        cls.shas["Separators"] = _commit(repo, {"src/sep.txt": "sep"}, "odd \x1e subject \x1f here\n")
        cls.shas["ExcusedWithTest"] = _commit(
            repo, {"src/ex.txt": "ex"}, "fix ex\n\nFixes-User: #3\nRegression-Test: Enabled.Case\n")
        _git(repo, "tag", "v2.1.0")
        # What the compiler builds: the macro, the continued comment and the #if (0) block are not tests.
        cls.compiled = {"Enabled.Case", "Taken.Case", "Multi.Line", "Typed.Case", "Spliced.Case", "Prefixed.Char",
                        "Separated.Num", "Fraction.Case", "AfterRaw.Case", "SplicedNumber.Case",
                        "InRaw.Case", "InComment.Case", "InSplitBlock.Case", "AfterSplitClose.Case",
                        "SplitRawPrefix.Case", "SplitEscape.Case", "BomDisabled.Case", "BomEnabled.Case",
                        "CrOnly.Case"}
        # A module that never finishes importing must time out into a violation, not abort the report; the short
        # timeout holds for every report this class builds and is restored in tearDownClass.
        cls._timeout, release_notes.PROBE_TIMEOUT = release_notes.PROBE_TIMEOUT, 3
        cls.result = release_notes.report(repo, "v2.0.0", "v2.1.0", gtest_list=cls.compiled)

    @classmethod
    def tearDownClass(cls):
        release_notes.PROBE_TIMEOUT = cls._timeout
        cls._tmp.cleanup()

    def violations_for(self, name, result=None):
        return [v for v in (result or self.result)["violations"] if v.startswith(self.shas[name][:7])]

    def test_only_real_tests_count(self):
        for name in self.fake:
            with self.subTest(fake=name):
                self.assertNotEqual(self.violations_for(name), [], self.result["violations"])
        for name in self.real:
            with self.subTest(real=name):
                self.assertEqual(self.violations_for(name), [])

    def test_source_check_alone_refuses_what_the_list_would_also_refuse(self):
        # Without the compiler's list, the source scan must still reject comments, literals and #if 0 forms.
        for name in ("Commented", "Block", "Disabled", "NotTaken", "Parenthesised", "Continued", "Quoted", "Raw"):
            with self.subTest(fake=name):
                self.assertFalse(release_notes.gtest_declared(self.repo, self.shas[name], self.fake[name]))

    def test_a_vertical_tab_does_not_split_a_trailer(self):
        self.assertTrue(any("without a Regression-Test" in v for v in self.violations_for("VerticalTab")),
                        self.result["violations"])

    def test_verifying_a_test_never_runs_it(self):
        self.assertFalse(self.marker.exists(), "the probe ran a test body")

    def test_an_overlong_issue_number_is_a_violation(self):
        self.assertTrue(any("names no issue number" in v for v in self.violations_for("HugeIssue")),
                        self.result["violations"])

    def test_separator_characters_in_a_subject_are_kept(self):
        self.assertEqual(len(self.result["fixes"]), len(self.fake) + len(self.real) + 3)

    def test_an_allowance_never_hides_a_named_test(self):
        excuse = {self.shas["ExcusedWithTest"][:10]: "documentation only"}
        result = release_notes.report(self.repo, "v2.0.0", "v2.1.0", allowances=excuse, gtest_list=self.compiled)
        self.assertTrue(any("drop one" in v for v in self.violations_for("ExcusedWithTest", result)))
        self.assertIn("Enabled.Case; excused: documentation only", release_notes.render(result))


class UserBugRuleDocsTest(unittest.TestCase):
    def test_rule_states_each_step(self):
        text = (REPO / "docs" / "support" / "user-bugs.md").read_text(encoding="utf-8")
        for heading in ("## 1. Reproduce first", "## 2. Write the test, and watch it fail",
                        "## 3. Fix it, with both trailers", "## 4. Label it `fixed-on-main`, and leave it open",
                        "## 5. Close it when the release ships"):
            self.assertIn(f"\n{heading}\n", text)
        for needle in ("tests/corpus/user-reports/", "tests/corpus/PROVENANCE.md", "Fixes-User: #",
                       "Regression-Test: ", "scripts/release-notes.py", "--gtest-list"):
            self.assertIn(needle, text)

    def test_template_and_agents_point_at_the_rule(self):
        template = (REPO / ".github" / "pull_request_template.md").read_text(encoding="utf-8")
        for needle in ("docs/support/user-bugs.md", "Fixes-User: #", "Regression-Test: ",
                       "Regression test: failing on parent, passing on fix"):
            self.assertIn(needle, template)
        self.assertIn("docs/support/user-bugs.md", (REPO / "AGENTS.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
