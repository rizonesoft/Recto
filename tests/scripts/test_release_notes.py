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


def _init(repo):
    _git(repo, "init", "-q", "-b", "main")
    _git(repo, "config", "user.name", "Fixture")
    _git(repo, "config", "user.email", "fixture@example.invalid")
    _git(repo, "config", "commit.gpgsign", "false")


class ReleaseNotesTest(unittest.TestCase):
    """A fixture repository: v1.0.0, then five fixes and an unrelated commit, then v1.1.0, then a late test."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        repo = cls.repo = pathlib.Path(cls._tmp.name) / "repo"
        repo.mkdir()
        _init(repo)
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

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def run_main(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = release_notes.main(list(args), repo=self.repo)
        return code, out.getvalue()

    def report(self, **kwargs):
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
        self.assertTrue(any(self.late_fix[:7] in v and "PdfLate.Case does not exist" in v for v in violations))
        for sha in (self.gtest_fix, self.python_fix, self.second_fix):
            self.assertFalse(any(sha[:7] in v for v in violations), sha)

    def test_allow_no_test_excuses_one_commit_and_is_reported(self):
        result = self.report(allowances={self.docs_fix[:10]: "documentation only"})
        self.assertEqual(len(result["violations"]), 1, result["violations"])
        docs = next(fix for fix in result["fixes"] if fix["sha"] == self.docs_fix)
        self.assertEqual(docs["allowed_no_test"], "documentation only")
        code, text = self.run_main("v1.0.0", "v1.1.0", "--allow-no-test", f"{self.docs_fix[:10]}=documentation only")
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
        code, text = self.run_main("v1.0.0", "v1.1.0")
        self.assertEqual(code, 1)
        self.assertIn(f"  #12: Fixed in v1.1.0 ({self.gtest_fix[:7]}, {self.second_fix[:7]}, ", text)
        self.assertIn("Violations:", text)
        code, text = self.run_main("v1.0.0", "v1.1.0", "--json")
        self.assertEqual(json.loads(text)["to"], "v1.1.0")
        code, text = self.run_main("v1.1.0", "HEAD")
        self.assertEqual(code, 0)
        self.assertIn("none", text)

    def test_unreadable_input_exits_2(self):
        self.assertEqual(self.run_main("v0.9.0", "v1.1.0")[0], 2)
        self.assertEqual(self.run_main("v1.0.0", "v1.1.0", "--allow-no-test", "nosha")[0], 2)
        self.assertEqual(self.run_main("v1.0.0", "v1.1.0", "--issues", str(self.repo / "missing.json"))[0], 2)

    def test_root_covers_the_first_release(self):
        result = release_notes.report(self.repo, "root", "v1.0.0")
        self.assertEqual(result["fixes"], [])


class HostileFixtureTest(unittest.TestCase):
    """Test ids that only look real, trailers that only look separate, and subjects holding separator characters."""

    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        repo = cls.repo = pathlib.Path(cls._tmp.name) / "repo"
        repo.mkdir()
        _init(repo)
        cpp = (
            "// TEST(Commented, Case)\n"
            "/* TEST(Block, Case) */\n"
            "#if 0\nTEST(Disabled, Case)\n{\n}\n#else\nTEST(Enabled, Case)\n{\n}\n#endif\n"
            "#if 1\nTEST(Taken, Case)\n{\n}\n#else\nTEST(NotTaken, Case)\n{\n}\n#endif\n"
            "const char* s = \"TEST(Quoted, Case)\";\n"
            "const char* r = R\"x(TEST(Raw, Case))x\";\n"
            "TEST (Multi,\n      Line)\n{\n}\n"
        )
        py = (
            "import unittest\n\n\n"
            "class Ordinary:\n    def test_missing(self):\n        pass\n\n    def save(self):\n        pass\n\n\n"
            "class Base(unittest.TestCase):\n    pass\n\n\n"
            "class Derived(Base):\n    def test_real(self):\n        pass\n\n    def helper(self):\n        pass\n"
        )
        _commit(repo, {"tests/recto-tests/hostile_tests.cpp": cpp, "tests/scripts/test_sample.py": py}, "initial")
        _git(repo, "tag", "v2.0.0")
        cls.shas = {}
        for name, test_id in [
            ("Commented", "Commented.Case"), ("Block", "Block.Case"), ("Disabled", "Disabled.Case"),
            ("NotTaken", "NotTaken.Case"), ("Quoted", "Quoted.Case"), ("Raw", "Raw.Case"),
            ("Ordinary", "tests.scripts.test_sample.Ordinary.test_missing"),
            ("Save", "tests.scripts.test_sample.Ordinary.save"),
            ("Helper", "tests.scripts.test_sample.Derived.helper"),
            ("Enabled", "Enabled.Case"), ("Taken", "Taken.Case"), ("Multi", "Multi.Line"),
            ("Real", "tests.scripts.test_sample.Derived.test_real"),
        ]:
            cls.shas[name] = _commit(repo, {f"src/{name}.txt": name}, f"fix {name}\n\nFixes-User: #1\n"
                                                                     f"Regression-Test: {test_id}\n")
        # One physical trailer line holding a vertical tab: git reads it as one Fixes-User value.
        cls.shas["VerticalTab"] = _commit(
            repo, {"src/vt.txt": "vt"},
            "fix vt\n\nFixes-User: #2\x0bRegression-Test: Enabled.Case\n")
        # Separator characters in a subject must not break the report.
        cls.shas["Separators"] = _commit(repo, {"src/sep.txt": "sep"}, "odd \x1e subject \x1f here\n")
        cls.shas["ExcusedWithTest"] = _commit(
            repo, {"src/ex.txt": "ex"}, "fix ex\n\nFixes-User: #3\nRegression-Test: Enabled.Case\n")
        _git(repo, "tag", "v2.1.0")

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def violations_for(self, result, name):
        return [v for v in result["violations"] if v.startswith(self.shas[name][:7])]

    def test_only_compiled_test_declarations_count(self):
        result = release_notes.report(self.repo, "v2.0.0", "v2.1.0")
        for name in ("Commented", "Block", "Disabled", "NotTaken", "Quoted", "Raw", "Ordinary", "Save", "Helper"):
            with self.subTest(fake=name):
                self.assertTrue(any("does not exist" in v for v in self.violations_for(result, name)),
                                result["violations"])
        for name in ("Enabled", "Taken", "Multi", "Real"):
            with self.subTest(real=name):
                self.assertEqual(self.violations_for(result, name), [])

    def test_a_vertical_tab_does_not_split_a_trailer(self):
        result = release_notes.report(self.repo, "v2.0.0", "v2.1.0")
        self.assertTrue(any("without a Regression-Test" in v for v in self.violations_for(result, "VerticalTab")),
                        result["violations"])

    def test_separator_characters_in_a_subject_are_kept(self):
        code, out = 0, io.StringIO()
        with contextlib.redirect_stdout(out):
            code = release_notes.main(["v2.0.0", "v2.1.0", "--json"], repo=self.repo)
        self.assertEqual(code, 1)
        self.assertEqual(len(json.loads(out.getvalue())["fixes"]), 15)

    def test_an_allowance_never_hides_a_named_test(self):
        excuse = {self.shas["ExcusedWithTest"][:10]: "documentation only"}
        result = release_notes.report(self.repo, "v2.0.0", "v2.1.0", allowances=excuse)
        self.assertTrue(any("drop one" in v for v in self.violations_for(result, "ExcusedWithTest")))
        self.assertIn("Enabled.Case; excused: documentation only", release_notes.render(result))


class UserBugRuleDocsTest(unittest.TestCase):
    def test_rule_states_each_step(self):
        text = (REPO / "docs" / "support" / "user-bugs.md").read_text(encoding="utf-8")
        for heading in ("## 1. Reproduce first", "## 2. Write the test, and watch it fail",
                        "## 3. Fix it, with both trailers", "## 4. Label it `fixed-on-main`, and leave it open",
                        "## 5. Close it when the release ships"):
            self.assertIn(f"\n{heading}\n", text)
        for needle in ("tests/corpus/user-reports/", "tests/corpus/PROVENANCE.md", "Fixes-User: #",
                       "Regression-Test: ", "scripts/release-notes.py"):
            self.assertIn(needle, text)

    def test_template_and_agents_point_at_the_rule(self):
        template = (REPO / ".github" / "pull_request_template.md").read_text(encoding="utf-8")
        for needle in ("docs/support/user-bugs.md", "Fixes-User: #", "Regression-Test: ",
                       "Regression test: failing on parent, passing on fix"):
            self.assertIn(needle, template)
        self.assertIn("docs/support/user-bugs.md", (REPO / "AGENTS.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
