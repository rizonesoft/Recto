"""scripts/review-prompt.py: fills the four prompt templates in docs/, refuses what it cannot fill, and writes
atomically. Every test runs the script from a scratch project that holds copies of it and of the templates."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
import unittest.mock

# The scratch repository, the dispatcher and the fake reviewer CLIs; support both package and discover imports.
try:
    from scripts.tests.test_review_dispatch import Harness
except ModuleNotFoundError:
    from test_review_dispatch import Harness

SCRIPTS = Path(__file__).resolve().parents[1]
DOCS = SCRIPTS.parent / "docs"
TEMPLATES = ("plan-review-prompt.md", "review-round-prompt.md", "completion-review-prompt.md",
             "reconciliation-review-prompt.md")
KINDS = ("plan", "round", "completion", "reconciliation")
CAND = "0123456789abcdef0123456789abcdef01234567"
BASE = "89abcdef0123456789abcdef0123456789abcdef"
FAILED = "fedcba9876543210fedcba9876543210fedcba98"
LAST = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
REF = "D00 T01 §7"
TODO = "todo/00-reviewer/TODO-01-three-pass-review.md"
PLACEHOLDER = re.compile(r"`<[^`\n]*>`")
# The final verdict each kind's reply format must still carry after filling.
VERDICT = {"plan": ["PLAN: complete", "OVERALL: approve"], "round": ["OVERALL: approve"],
           "completion": ["COMPLETION: complete"], "reconciliation": ["RECONCILIATION: complete"]}
# The values every kind needs besides the candidate.
COMMON = {"plan": ["--governing", "docs/plan.md"],
          "round": ["--purpose", "Fill review prompts.", "--governing", "docs/plan.md"],
          "completion": ["--purpose", "Fill review prompts.", "--governing", "docs/plan.md"],
          "reconciliation": ["--failed", FAILED, "--findings", "docs/reviews/00-reviewer/D00-T01-s7.md",
                             "--governing", "docs/plan.md"]}


def install(root: Path, graph=None):
    """A scratch project: scripts/review-prompt.py beside docs/<templates>, and a stand-in todo-graph.py if given."""
    (root / "scripts").mkdir(parents=True, exist_ok=True)
    (root / "docs").mkdir(exist_ok=True)
    shutil.copy(SCRIPTS / "review-prompt.py", root / "scripts" / "review-prompt.py")
    shutil.copy(SCRIPTS / "review_layout.py", root / "scripts" / "review_layout.py")
    for name in TEMPLATES:
        shutil.copy(DOCS / name, root / "docs" / name)
    if graph is not None:
        (root / "scripts" / "todo-graph.py").write_text(graph, encoding="utf-8")


class Filler(unittest.TestCase):
    graph = None

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name) / "project"
        install(self.root, self.graph)
        self.out = Path(self._tmp.name) / "out" / "prompt.md"
        self.out.parent.mkdir()
        self.gates = Path(self._tmp.name) / "gates.txt"
        self.gates.write_text("python -m unittest: OK (12 tests)\n", encoding="utf-8")

    def tearDown(self):
        self._tmp.cleanup()

    def run_filler(self, args):
        return subprocess.run([sys.executable, "-B", str(self.root / "scripts/review-prompt.py"), *args],
                              capture_output=True, text=True, timeout=60, cwd=self.root, encoding="utf-8",
                              env={**os.environ, "PYTHONIOENCODING": "utf-8"})

    def fill(self, kind, *extra, out=None, common=True):
        args = ["--kind", kind, "--candidate", CAND, "--out", str(out or self.out)]
        if common:
            args += COMMON[kind]
            if kind == "reconciliation":
                args += ["--gates", str(self.gates)]
        return self.run_filler(args + list(extra))

    def ok(self, kind, *extra, **kwargs):
        done = self.fill(kind, *extra, **kwargs)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(Path(done.stdout.strip()), self.out)
        return self.out.read_text(encoding="utf-8")

    def refused(self, kind, needle, *extra, **kwargs):
        """The run exits 2 naming `needle`, prints nothing, and leaves an earlier output byte for byte."""
        self.out.write_bytes(b"EARLIER OUTPUT\n")
        done = self.fill(kind, *extra, **kwargs)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn(needle, done.stderr)
        self.assertEqual(done.stdout, "")
        self.assertEqual(self.out.read_bytes(), b"EARLIER OUTPUT\n", "a refusal must leave the earlier output alone")
        self.assertEqual([p.name for p in self.out.parent.iterdir()], [self.out.name], "no temp file left behind")
        return done.stderr


class FillEachKind(Filler):
    def test_every_kind_fills_with_no_placeholder_left(self):
        for kind in KINDS:
            with self.subTest(kind):
                extra = ["--ref", REF] + (["--todo", TODO] if kind in ("plan", "completion") else [])
                text = self.ok(kind, *extra)
                self.assertEqual(PLACEHOLDER.findall(text), [])
                self.assertNotIn("<DNN TNN", text)
                self.assertTrue(text.startswith("You are an independent"), "only the prompt after `---` is written")
                self.assertIn("## Reply format", text)
                for verdict in VERDICT[kind]:
                    self.assertIn(verdict, text)
                self.assertTrue(text.endswith("\n") and not text.endswith("\n\n"))

    def test_completion_and_reconciliation_prompts_say_the_stamp_ticks_the_commit_item(self):
        # Without it a reviewer reads the open Commit item of an unstamped section as an undelivered promise.
        # The opening clause is part of the rule: it says why the item is open (the stamp comes after the review).
        rule = ("When the review is of a TODO section, that section is stamped after its review: "
                "`build\\python.cmd scripts/todo-graph.py stamp` ticks its `- [ ] Commit:` item and its Implementation Order row "
                "(`todo/README.md`, Stamps and receipts), so on an unstamped section both are open by design and are "
                "not a gap.")
        closing = ("A gap that only reports that open item or row is closed by this rule: mark it `closed`, citing the "
                   "rule as its evidence; it needs no checkbox edit and no `- Implemented:` line.")
        for kind in ("completion", "reconciliation"):
            with self.subTest(kind):
                extra = ["--ref", REF] + (["--todo", TODO] if kind == "completion" else [])
                self.ok(kind, *extra)
                written = self.out.read_text(encoding="utf-8")  # the file the filler wrote, not its stdout
                self.assertIn(rule, written)
                if kind == "reconciliation":
                    self.assertIn(closing, written)
                else:
                    self.assertNotIn(closing, written)

    def test_saved_file_is_the_prompt_and_values_appear(self):
        text = self.ok("completion", "--ref", REF, "--todo", TODO, "--fidelity", "design/main-window.png")
        self.assertEqual(self.out.read_text(encoding="utf-8"), text)
        self.assertIn(f"- Commit or range: `{CAND}` (inspect", text)
        self.assertIn(f"- TODO section: `{REF}` (its contract: `build\\python.cmd scripts/todo-graph.py resolve \"{REF}\"`)", text)
        self.assertIn(f"`{TODO}`", text)
        self.assertIn("- Fidelity: `design/main-window.png`.", text)
        self.assertNotIn("none (Surface: no)", text)
        self.assertIn("- What the feature is for: Fill review prompts.", text)
        self.assertIn("`AGENTS.md` and `docs/plan.md`", text)

    def test_fidelity_defaults_to_no_surface(self):
        self.assertIn("- Fidelity: `none (Surface: no)`.", self.ok("plan", "--ref", REF, "--todo", TODO))
        self.assertIn("- Fidelity: `none (Surface: no)`.", self.ok("round", "--ref", REF))
        self.assertIn("- Fidelity: `design/SPEC.md`.", self.ok("round", "--ref", REF, "--fidelity", "design/SPEC.md"))

    def test_every_kind_takes_fidelity_and_names_it_on_its_candidate(self):
        # The reconciliation prompt had no Fidelity line, so a repair of a surface was reviewed with no design source.
        for kind in KINDS:
            with self.subTest(kind):
                extra = ["--ref", REF] + (["--todo", TODO] if kind in ("plan", "completion") else [])
                text = self.ok(kind, *extra, "--fidelity", "docs/design/contract.md")
                candidate = text.partition("## Candidate")[2].partition("\n## ")[0]
                self.assertIn("- Fidelity: `docs/design/contract.md`.", candidate)
                self.assertNotIn("none (Surface: no)", text)
                self.assertIn("- Fidelity: `none (Surface: no)`.", self.ok(kind, *extra))

    def test_single_commit_versus_range(self):
        single = self.ok("round", "--ref", REF)
        self.assertIn(f"- Commit or range: `{CAND}` (inspect", single)
        self.assertNotIn(BASE, single)
        ranged = self.ok("round", "--ref", REF, "--base", BASE)
        self.assertIn(f"- Commit or range: `{BASE}..{CAND}`, tip `{CAND}` (inspect", ranged)

    def test_reconciliation_names_failed_and_last_in_their_places(self):
        same = self.ok("reconciliation", "--ref", REF)
        self.assertIn(f"- The failed completion's candidate: `{FAILED}`.", same)
        self.assertIn(f"(the failed completion's, or the last reconciliation's): `{FAILED}`.", same)
        self.assertIn(f"The repair is `{FAILED}..{CAND}`, read all of it, with `{FAILED}..{CAND}` the newest part",
                      same)
        second = self.ok("reconciliation", "--ref", REF, "--last", LAST)
        self.assertIn(f"- The failed completion's candidate: `{FAILED}`.", second)
        self.assertIn(f"or the last reconciliation's): `{LAST}`.", second)
        self.assertIn(f"- The repaired candidate: `{CAND}`. The repair is `{FAILED}..{CAND}`", second)
        self.assertIn(f"with `{LAST}..{CAND}` the newest part", second)
        self.assertIn("`docs/reviews/00-reviewer/D00-T01-s7.md`", second)
        self.assertIn("- Gate results on the repaired candidate: `python -m unittest: OK (12 tests)`.", second)
        self.assertEqual(PLACEHOLDER.findall(second), [])

    def test_multiline_gate_output_goes_in_a_fence_and_is_not_rescanned(self):
        self.gates.write_text("ran 3 checks\nOK `<not a placeholder>`\n", encoding="utf-8")
        text = self.ok("reconciliation", "--ref", REF)
        self.assertIn("- Gate results on the repaired candidate:\n\n```\nran 3 checks\nOK `<not a placeholder>`\n```\n\n"
                      "- Governing", text)

    def test_supplied_text_is_never_taken_for_the_gate_placeholder(self):
        # Round of D00 T01 §7: a purpose holding the filler's own gate marker crashed it, and with gate output present
        # the supplied text was replaced by that output.
        text = self.ok("round", "--purpose", "Handle @@GATES@@ in supplied text", common=False,
                       *(["--governing", "docs/plan.md"]))
        self.assertIn("Handle @@GATES@@ in supplied text", text)
        self.gates.write_text("true -> exit 0\n", encoding="utf-8")
        text = self.ok("reconciliation", "--ref", REF, "--governing", "docs/@@GATES@@.md", common=False,
                       *(["--failed", CAND, "--findings", "docs/reviews/x.md", "--gates", str(self.gates)]))
        self.assertIn("docs/@@GATES@@.md", text)
        self.assertEqual(text.count("true -> exit 0"), 1)
        self.assertNotIn("\0", text)

    def test_lens_verdict_text_is_left_alone(self):
        self.assertIn("`LENS <name>: clean`", self.ok("reconciliation", "--ref", REF))

    def test_round_without_ref_drops_the_todo_line_and_nothing_else(self):
        with_ref = self.ok("round", "--ref", REF).splitlines()
        text = self.ok("round")
        self.assertNotIn("- TODO section", text)
        self.assertNotIn("todo-graph", text)
        self.assertEqual(PLACEHOLDER.findall(text), [])
        self.assertEqual([line for line in with_ref if "todo-graph" not in line], text.splitlines())

    def test_plan_and_completion_without_ref_drop_the_section_and_file_lines(self):
        for kind in ("plan", "completion"):
            with self.subTest(kind):
                text = self.ok(kind)
                self.assertNotIn("- TODO section", text)
                self.assertNotIn("- The TODO file", text)
                self.assertEqual(PLACEHOLDER.findall(text), [])
                self.assertIn("- Fidelity:", text)

    def test_todo_without_ref_keeps_the_file_line(self):
        text = self.ok("plan", "--todo", TODO)
        self.assertIn(f"`{TODO}`", text)
        self.assertNotIn("- TODO section", text)


class Refusals(Filler):
    def test_missing_value_names_the_placeholder_and_its_option(self):
        cases = [("round", ["--governing", "x"], "`<one or two sentences>`", "--purpose"),
                 ("round", ["--purpose", "x"], "`<the spec, plan or design file that applies>`", "--governing"),
                 ("completion", ["--governing", "x"], "`<one or two sentences: the user", "--purpose"),
                 ("plan", [], "`<the spec or plan that applies>`", "--governing"),
                 ("reconciliation", ["--governing", "x"], "`<docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md>`",
                  "--findings"),
                 ("reconciliation", ["--governing", "x", "--findings", "f.md"], "`<COMPLETION_SHA>`", "--failed"),
                 ("reconciliation", ["--governing", "x", "--findings", "f.md", "--failed", FAILED],
                  "`<quoted output>`", "--gates")]
        for kind, args, placeholder, flag in cases:
            with self.subTest(kind=kind, placeholder=placeholder):
                message = self.refused(kind, placeholder, *args, common=False)
                self.assertIn(f"(give {flag})", message)
                self.assertIn("no value for", message)

    def test_a_value_the_kind_has_no_place_for_is_refused(self):
        self.refused("plan", "no place for --purpose", "--purpose", "x")
        self.refused("plan", "no place for --base", "--base", BASE)
        self.refused("round", "no place for --gates", "--gates", str(self.gates))
        self.refused("reconciliation", "no place for --purpose", "--ref", REF, "--purpose", "x")

    def test_missing_template(self):
        (self.root / "docs" / "review-round-prompt.md").unlink()
        message = self.refused("round", "review-round-prompt.md")
        self.assertIn("cannot be read", message)

    def test_template_without_its_rule_line(self):
        (self.root / "docs" / "review-round-prompt.md").write_text("no rule here\n", encoding="utf-8")
        self.refused("round", "no `---` line")

    def test_bad_shas(self):
        for bad in ("abc123", CAND.upper(), CAND + "0", "g" * 40, ""):
            with self.subTest(bad=bad):
                done = self.run_filler(["--kind", "plan", "--candidate", bad, "--out", str(self.out),
                                        "--governing", "x"])
                self.assertEqual(done.returncode, 2, done.stderr)
                self.assertIn("40-character", done.stderr)
                self.assertFalse(self.out.exists())
        self.refused("round", "--base must be a full 40-character", "--base", "abc")
        self.refused("reconciliation", "--failed must be a full 40-character", "--failed", "abc")
        self.refused("round", "--ref must look like", "--ref", "section seven")

    def test_a_commit_name_git_resolves_is_used_as_its_full_sha(self):
        # todo/findings.md, 2026-11-12: an abbreviated SHA was refused and cost a rerun in D02 T01 §3.
        for args in (["init", "-q"], ["config", "user.email", "t@t"], ["config", "user.name", "t"],
                     ["config", "commit.gpgsign", "false"], ["commit", "-q", "--allow-empty", "-m", "base"],
                     ["commit", "-q", "--allow-empty", "-m", "tip"]):
            subprocess.run(["git", *args], cwd=self.root, capture_output=True, check=True, timeout=60)
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=self.root, capture_output=True, text=True,
                              check=True, timeout=60).stdout.strip()
        base = subprocess.run(["git", "rev-parse", "HEAD~1"], cwd=self.root, capture_output=True, text=True,
                              check=True, timeout=60).stdout.strip()
        for name in (head[:7], "HEAD"):
            with self.subTest(name=name):
                done = self.run_filler(["--kind", "round", "--candidate", name, "--base", base[:9],
                                        "--purpose", "x", "--governing", "x", "--out", str(self.out)])
                self.assertEqual(done.returncode, 0, done.stderr)
                text = self.out.read_text(encoding="utf-8")
                self.assertIn(f"`{base}..{head}`", text)
                self.assertNotIn(f"`{head[:7]}`", text)
        self.refused("round", "--base must be a full 40-character commit SHA or a name git resolves", "--base",
                     "no-such-branch")
        self.refused("round", "--base must be a full 40-character", "--base=--all")  # never read as a git option
    def test_unreadable_or_empty_gates_file(self):
        missing = self.gates.parent / "nope.txt"
        message = self.refused("reconciliation", "cannot be read", "--failed", FAILED, "--findings", "f.md",
                               "--governing", "x", "--gates", str(missing), common=False)
        self.assertIn("nope.txt", message)
        self.gates.write_text("\n", encoding="utf-8")
        self.refused("reconciliation", "is empty", "--ref", REF)

    def test_an_unknown_placeholder_in_a_template_is_refused_by_name(self):
        # Completion review of D00 T01 §7: only known placeholders with missing values were tested.
        template = self.root / "docs" / "review-round-prompt.md"
        template.write_text(template.read_text(encoding="utf-8") + "\n- A new field: `<SOMETHING_NEW>`.\n",
                            encoding="utf-8")
        self.refused("round", "<SOMETHING_NEW>")

    def test_a_failed_temporary_write_or_replacement_leaves_the_earlier_output(self):
        # Completion review of D00 T01 §7: the earlier tests never reached the write or the replace of an existing
        # prompt. The script is loaded in this process so that one call of its own can be made to fail.
        spec = importlib.util.spec_from_file_location("review_prompt_under_test", self.root / "scripts/review-prompt.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        args = ["--kind", "round", "--candidate", CAND, "--out", str(self.out), *COMMON["round"]]
        real_fdopen = os.fdopen

        class FullDisk:
            """The temporary file opened for real, whose write stores a little and then fails."""
            def __init__(self, handle):
                self.handle = handle

            def __enter__(self):
                return self

            def __exit__(self, *exc):
                self.handle.close()
                return False

            def write(self, text):
                self.handle.write(text[:20])
                self.handle.flush()
                raise full()

        def full():
            return OSError(28, "No space left on device")

        for label, call, effect in (("opening the temporary file", "fdopen", full()),
                                    ("writing the temporary file", "fdopen",
                                     lambda *a, **k: FullDisk(real_fdopen(*a, **k))),
                                    ("the replacement", "replace", full())):
            with self.subTest(label):
                self.out.write_bytes(b"EARLIER OUTPUT\n")
                errors, printed = io.StringIO(), io.StringIO()
                with unittest.mock.patch.object(module.os, call, side_effect=effect), \
                        contextlib.redirect_stderr(errors), contextlib.redirect_stdout(printed), \
                        unittest.mock.patch.object(module.os, "getcwd", return_value=str(self.root)):
                    code = module.main(args)
                self.assertEqual(code, 2, errors.getvalue())
                self.assertIn("cannot be written", errors.getvalue())
                self.assertIn("No space left on device", errors.getvalue())
                self.assertEqual(printed.getvalue(), "")
                self.assertEqual(self.out.read_bytes(), b"EARLIER OUTPUT\n")
                self.assertEqual([p.name for p in self.out.parent.iterdir()], [self.out.name], "a temp file was left")
        # With nothing failing the same call writes the prompt.
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module.main(args), 0)
        self.assertIn(CAND, self.out.read_text(encoding="utf-8"))

    def test_output_whose_parent_is_a_file(self):
        blocker = self.out.parent / "blocker"
        blocker.write_text("a file", encoding="utf-8")
        done = self.fill("round", "--ref", REF, out=blocker / "prompt.md")
        self.assertEqual(done.returncode, 2, done.stderr)
        self.assertIn("cannot be written", done.stderr)
        self.assertEqual(blocker.read_text(encoding="utf-8"), "a file")
        self.assertEqual([p.name for p in self.out.parent.iterdir()], ["blocker"])

    def test_output_that_is_a_directory(self):
        folder = self.out.parent / "folder"
        folder.mkdir()
        done = self.fill("round", "--ref", REF, out=folder)
        self.assertEqual(done.returncode, 2, done.stderr)
        self.assertIn("cannot be written", done.stderr)
        self.assertTrue(folder.is_dir())

    def test_a_refusal_leaves_an_earlier_prompt_untouched(self):
        self.ok("round", "--ref", REF)
        earlier = self.out.read_bytes()
        done = self.fill("round", "--governing", "x", common=False)
        self.assertEqual(done.returncode, 2)
        self.assertEqual(self.out.read_bytes(), earlier)


class TodoLookup(Filler):
    graph = textwrap.dedent('''\
        import json, sys
        if sys.argv[1:] == ["resolve", "D00 T01 §7"]:
            print(json.dumps({"ref": "D00 T01 §7", "path": "todo/00-x/TODO-01-y.md",
                              "body": "## 7. Example\\n\\n**Surface:** no\\n", "exit": 3}))
            sys.exit(3)
        print(json.dumps({"error": "no such section"}))
        sys.exit(1)
        ''')

    def test_ref_without_todo_resolves_the_file_even_on_exit_3(self):
        text = self.ok("plan", "--ref", REF)
        self.assertIn("`todo/00-x/TODO-01-y.md`", text)
        self.assertEqual(PLACEHOLDER.findall(text), [])

    def test_explicit_todo_wins(self):
        self.assertIn("`other/TODO.md`", self.ok("plan", "--ref", REF, "--todo", "other/TODO.md"))

    def reconcile(self, *extra):
        return ["--failed", FAILED, "--gates", str(self.gates), "--governing", "docs/plan.md", "--ref", REF, *extra]

    def test_ref_without_findings_names_the_section_file_in_the_configured_folder(self):
        # D03 T01 §4: the findings folder is todo/plan.json's layout.receipts, docs/reviews without it.
        (self.root / "todo/00-x").mkdir(parents=True)
        self.assertIn("`docs/reviews/00-x/D00-T01-s7.md`", self.ok("reconciliation", *self.reconcile(), common=False))
        (self.root / "todo/plan.json").write_text('{"layout": {"receipts": "reviews"}}', encoding="utf-8")
        text = self.ok("reconciliation", *self.reconcile(), common=False)
        self.assertIn("`reviews/00-x/D00-T01-s7.md`", text)
        self.assertNotIn("docs/reviews/00-x", text)
        (self.root / "todo/plan.json").write_text('{"layout": {"receipts": "reviews", "plan": "docs/plan.md"}}',
                                                  encoding="utf-8")  # D03 T01 §7: the todo kit's plan key passes
        self.assertIn("`reviews/00-x/D00-T01-s7.md`", self.ok("reconciliation", *self.reconcile(), common=False))
        self.assertIn("`other/f.md`", self.ok("reconciliation", *self.reconcile("--findings", "other/f.md"),
                                              common=False), "an explicit --findings still wins")

    def test_a_layout_that_cannot_be_read_is_refused(self):
        (self.root / "todo/00-x").mkdir(parents=True)
        (self.root / "todo/plan.json").write_text('{"layout": {"receipts": "../out"}}', encoding="utf-8")
        self.refused("reconciliation", "todo/plan.json: layout receipts must be a folder inside the repository",
                     *self.reconcile(), common=False)

    def test_unresolvable_section_asks_for_todo(self):
        message = self.refused("plan", "--todo is needed", "--ref", "D09 T09 §9")
        self.assertIn("named no file", message)


class FidelityLookup(Filler):
    """`--ref` without `--fidelity` reads the section's own Surface and Fidelity through todo-graph.py resolve."""
    graph = textwrap.dedent('''\
        import json, sys
        BODIES = {
            "D00 T01 §7": "## 7. A surface\\n\\n**Surface:** yes\\n**Fidelity:** `docs/design/contract.md` section 4\\n",
            "D00 T01 §8": "## 8. No surface\\n\\n**Surface:** no\\n",
            "D00 T01 §9": "## 9. A surface with no source\\n\\n**Surface:** yes\\n**Job:** Open a file.\\n",
            "D00 T02 §1": "## 1. Quoted fields\\n\\n> **Surface:** yes\\n> **Fidelity:** docs/design/mockup.md\\n",
            "D00 T02 §2": "## 2. Missing surface\\n\\n**Job:** No surface field.\\n",
            "D00 T02 §3": "## 3. Ambiguous surface\\n\\n**Surface:** yes\\n**Surface:** no\\n",
            "D00 T02 §4": "## 4. Empty duplicate surface\\n\\n**Surface:**\\n**Surface:** no\\n",
            "D00 T02 §5": "## 5. Empty duplicate fidelity\\n\\n**Surface:** yes\\n**Fidelity:**\\n**Fidelity:** docs/x.md\\n",
        }
        ref = sys.argv[2] if sys.argv[1:2] == ["resolve"] and len(sys.argv) == 3 else None
        if ref == "D00 T03 §1":
            print(json.dumps({"ref": ref, "path": "todo/00-x/TODO-03-z.md", "exit": 0}))
            sys.exit(0)
        if ref not in BODIES:
            print("TODO refused: no such section: " + str(ref), file=sys.stderr)
            sys.exit(1)
        print(json.dumps({"ref": ref, "path": "todo/00-x/TODO-01-y.md", "body": BODIES[ref], "exit": 3}))
        sys.exit(3)
        ''')

    def extra(self, kind, ref):
        return ["--ref", ref] + (["--findings", "f.md"] if kind == "reconciliation" else [])

    def test_a_surface_sections_fidelity_is_read_on_every_kind(self):
        for kind in KINDS:
            with self.subTest(kind):
                text = self.ok(kind, *self.extra(kind, REF))
                self.assertIn("- Fidelity: `docs/design/contract.md` section 4.", text)
                self.assertNotIn("none (Surface: no)", text)
                self.assertEqual(PLACEHOLDER.findall(text), [])
                quoted = self.ok(kind, *self.extra(kind, "D00 T02 §1"))
                self.assertIn("- Fidelity: `docs/design/mockup.md`.", quoted)

    def test_a_section_without_a_surface_keeps_the_default(self):
        for kind in KINDS:
            with self.subTest(kind):
                self.assertIn("- Fidelity: `none (Surface: no)`.", self.ok(kind, *self.extra(kind, "D00 T01 §8")))

    def test_a_surface_section_without_a_fidelity_is_refused_never_sent_as_no_surface(self):
        for kind in KINDS:
            with self.subTest(kind):
                message = self.refused(kind, "--fidelity is needed", *self.extra(kind, "D00 T01 §9"))
                self.assertIn("Surface: yes", message)
                self.assertIn("D00 T01 §9", message)

    def test_an_explicit_fidelity_wins_and_needs_no_lookup(self):
        for ref in (REF, "D00 T01 §9"):
            with self.subTest(ref):
                text = self.ok("round", "--ref", ref, "--fidelity", "design/other.md")
                self.assertIn("- Fidelity: `design/other.md`.", text)

    def test_a_section_the_graph_cannot_resolve_is_refused(self):
        message = self.refused("round", "--fidelity is needed", "--ref", "D09 T09 §9")
        self.assertIn("looking up D09 T09 §9 failed", message)

    def test_a_graph_that_prints_no_body_is_refused(self):
        message = self.refused("round", "--fidelity is needed", "--ref", "D00 T03 §1")
        self.assertIn("printed no section body", message)

    def test_blank_fidelity_override_is_refused(self):
        for blank in ("", "   "):
            with self.subTest(blank=blank):
                message = self.refused("round", "--fidelity must not be blank", "--ref", REF, "--fidelity", blank)
                self.assertIn("--fidelity must not be blank", message)

    def test_missing_or_ambiguous_surface_field_is_refused(self):
        for ref in ("D00 T02 §2", "D00 T02 §3", "D00 T02 §4"):
            with self.subTest(ref=ref):
                message = self.refused("round", "--fidelity is needed", "--ref", ref)
                self.assertIn("unambiguous Surface", message)
        # Empty duplicate Fidelity on Surface: yes is also refused (Round 1 finding 4).
        message = self.refused("round", "--fidelity is needed", "--ref", "D00 T02 §5")
        self.assertIn("names no one Fidelity", message)

    def test_candidate_commit_fidelity_wins_over_working_tree(self):
        import subprocess
        git = lambda *args: subprocess.run(
            ["git", "-C", str(self.root), *args], check=True, capture_output=True, text=True
        )
        git("init", "-q")
        git("config", "user.email", "test@example.com")
        git("config", "user.name", "Test")
        todo_file = self.root / "todo/00-x/TODO-01-y.md"
        todo_file.parent.mkdir(parents=True, exist_ok=True)
        todo_file.write_text(
            "## 7. A surface\n\n**Surface:** yes\n**Fidelity:** `docs/design/v1.md` section 1\n"
            "##\tAppendix\n**Surface:** no\n",
            encoding="utf-8",
        )
        git("add", "todo/00-x/TODO-01-y.md")
        git("commit", "-q", "-m", "v1 fidelity")
        cand = git("rev-parse", "HEAD").stdout.strip()
        # Mutate working tree to a different Fidelity (and stub graph returns contract.md section 4).
        todo_file.write_text(
            "## 7. A surface\n\n**Surface:** no\n**Fidelity:** `docs/design/v2.md` section 2\n",
            encoding="utf-8",
        )
        out = self.root / "cand-prompt.md"
        done = self.run_filler([
            "--kind", "round", "--candidate", cand, "--ref", REF,
            "--purpose", "Verify candidate Fidelity.", "--governing", "docs/plan.md", "--out", str(out),
        ])
        self.assertEqual(done.returncode, 0, done.stderr)
        text = out.read_text(encoding="utf-8")
        self.assertIn("- Fidelity: `docs/design/v1.md` section 1.", text)
        self.assertNotIn("docs/design/v2.md", text)
        self.assertNotIn("docs/design/contract.md", text)

        # Round 1 finding 1: unavailable full SHA inside a git repo refuses instead of falling back to checkout.
        missing_sha = "f" * 40
        bad = self.run_filler([
            "--kind", "round", "--candidate", missing_sha, "--ref", REF,
            "--purpose", "Verify unavailable SHA refusal.", "--governing", "docs/plan.md", "--out", str(out),
        ])
        self.assertEqual(bad.returncode, 2)
        self.assertIn("not an available commit", bad.stderr)

        # Round 1 finding 3: missing graph body still refuses even on a real git candidate commit.
        todo3 = self.root / "todo/00-x/TODO-03-z.md"
        todo3.write_text("## 1. No body\n\n**Surface:** no\n", encoding="utf-8")
        git("add", "todo/00-x/TODO-03-z.md")
        git("commit", "-q", "-m", "add todo3")
        cand3 = git("rev-parse", "HEAD").stdout.strip()
        no_body = self.run_filler([
            "--kind", "round", "--candidate", cand3, "--ref", "D00 T03 §1",
            "--purpose", "Verify missing body refusal on real commit.", "--governing", "docs/plan.md", "--out", str(out),
        ])
        self.assertEqual(no_body.returncode, 2)
        self.assertIn("printed no section body", no_body.stderr)

        # Round 1 finding 2: renaming the TODO file after candidate still resolves candidate's file via DNN TNN.
        renamed = self.root / "todo/00-x/TODO-01-renamed.md"
        todo_file.rename(renamed)
        (self.root / "scripts/todo-graph.py").write_text(
            self.graph.replace("todo/00-x/TODO-01-y.md", "todo/00-x/TODO-01-renamed.md"),
            encoding="utf-8",
        )
        done_renamed = self.run_filler([
            "--kind", "round", "--candidate", cand, "--ref", REF,
            "--purpose", "Verify renamed TODO lookup.", "--governing", "docs/plan.md", "--out", str(out),
        ])
        self.assertEqual(done_renamed.returncode, 0, done_renamed.stderr)
        self.assertIn("- Fidelity: `docs/design/v1.md` section 1.", out.read_text(encoding="utf-8"))

        # Round 1 finding 5: ##<tab> heading ends section 7 so Surface in Appendix is not consumed.
        todo_tab = self.root / "todo/00-x/TODO-01-y.md"
        todo_tab.write_text("## 7. Heading only\n##\tAppendix\n**Surface:** no\n", encoding="utf-8")
        git("add", "todo/00-x/TODO-01-y.md")
        git("commit", "-q", "-m", "tab heading boundary")
        cand_tab = git("rev-parse", "HEAD").stdout.strip()
        tab_res = self.run_filler([
            "--kind", "round", "--candidate", cand_tab, "--ref", REF,
            "--purpose", "Verify tab heading stop.", "--governing", "docs/plan.md", "--out", str(out),
        ])
        self.assertEqual(tab_res.returncode, 2)
        self.assertIn("unambiguous Surface", tab_res.stderr)

    def test_plan_prompt_calls_todo_graph_resolve_only_once(self):
        # Round 1 finding 8: reusing resolve_section() avoids running todo-graph.py resolve twice per prompt.
        counter = self.root / "resolve-calls.txt"
        logged_graph = (
            f"from pathlib import Path\n"
            f"with open({str(counter)!r}, 'a', encoding='utf-8') as f: f.write('1\\n')\n"
            + self.graph
        )
        (self.root / "scripts/todo-graph.py").write_text(logged_graph, encoding="utf-8")
        text = self.ok("plan", "--ref", REF)
        self.assertIn("`todo/00-x/TODO-01-y.md`", text)
        self.assertIn("- Fidelity: `docs/design/contract.md` section 4.", text)
        self.assertEqual(len(counter.read_text(encoding="utf-8").splitlines()), 1)

    def test_git_repository_access_error_refuses_instead_of_checkout_fallback(self):
        # Completion review Gap 1: exit-128 repository-access failure (e.g. dubious ownership) must refuse rather than
        # falling back to working-tree Fidelity, while non-git directories still work.
        import importlib.util
        import unittest.mock
        spec = importlib.util.spec_from_file_location("review_prompt_under_test", self.root / "scripts/review-prompt.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        fake_128 = subprocess.CompletedProcess(
            ["git"], 128, stdout="", stderr="fatal: detected dubious ownership in repository at '/repo'\n"
        )
        with unittest.mock.patch.object(mod.subprocess, "run", return_value=fake_128):
            with self.assertRaises(mod.Refused) as ctx:
                mod._section_body_at_commit(self.root, CAND, "todo/00-x/TODO-01-y.md", REF)
        self.assertIn("git repository check failed", str(ctx.exception))
        self.assertIn("dubious ownership", str(ctx.exception))
        # Outside a git repo ("not a git repository (or any ...)" with no GIT_DIR and no ancestor .git), standalone
        # fixtures still return None, and the git probe runs with C-locale env vars (Reconciliation 1 finding 3).
        fake_not_git = subprocess.CompletedProcess(
            ["git"], 128, stdout="", stderr="fatal: not a git repository (or any of the parent directories): .git\n"
        )
        seen_env = {}
        def capture_run(*args, **kwargs):
            seen_env.update(kwargs.get("env") or {})
            return fake_not_git
        with unittest.mock.patch.dict(os.environ, {"LC_ALL": "fr_FR.UTF-8"}, clear=False), \
                unittest.mock.patch.object(mod.subprocess, "run", side_effect=capture_run):
            if "GIT_DIR" in os.environ:
                os.environ.pop("GIT_DIR")
            self.assertIsNone(mod._section_body_at_commit(self.root, CAND, "todo/00-x/TODO-01-y.md", REF))
        self.assertEqual(seen_env.get("LC_ALL"), "C")
        self.assertEqual(seen_env.get("LANG"), "C")
        self.assertEqual(seen_env.get("LANGUAGE"), "C")

        # Standalone non-git directory with GIT_INDEX_FILE still works (Reconciliation 4 finding 6).
        with unittest.mock.patch.dict(os.environ, {"GIT_INDEX_FILE": "alternate-index"}, clear=False):
            for var in mod._GIT_STORE_ENV_VARS:
                os.environ.pop(var, None)
            self.assertIsNone(mod._section_body_at_commit(self.root, CAND, "todo/00-x/TODO-01-y.md", REF))

        # Real git checkout: GIT_WORK_TREE pointing elsewhere still reads candidate commit (Reconciliation 4 finding 1);
        # stale GIT_OBJECT_DIRECTORY and stale GIT_DIR refuse; corrupt .git refuses; GIT_CEILING_DIRECTORIES allows
        # standalone nested dir without crossing Git's discovery boundary (Reconciliation 4 finding 7).
        outer = Path(self._tmp.name) / "outer-repo"
        nested = outer / "standalone-sub"
        nested.mkdir(parents=True)
        other_wt = Path(self._tmp.name) / "elsewhere-wt"
        other_wt.mkdir()
        git = lambda *args: subprocess.run(
            ["git", "-C", str(outer), *args], check=True, capture_output=True, text=True
        )
        git("init", "-q")
        git("config", "user.email", "test@example.com")
        git("config", "user.name", "Test")
        todo_outer = outer / "todo/00-x/TODO-01-y.md"
        todo_outer.parent.mkdir(parents=True, exist_ok=True)
        todo_outer.write_text("## 7. A surface\n\n**Surface:** yes\n**Fidelity:** `docs/design/wt.md`\n",
                              encoding="utf-8")
        git("add", "todo/00-x/TODO-01-y.md")
        git("commit", "-q", "-m", "outer commit")
        outer_cand = git("rev-parse", "HEAD").stdout.strip()

        with unittest.mock.patch.dict(os.environ, {"GIT_WORK_TREE": str(other_wt)}, clear=False):
            body = mod._section_body_at_commit(outer, outer_cand, "todo/00-x/TODO-01-y.md", REF)
        self.assertIsNotNone(body)
        self.assertIn("docs/design/wt.md", body)

        with unittest.mock.patch.dict(os.environ, {"GIT_OBJECT_DIRECTORY": str(outer / "missing-objects")},
                                      clear=False):
            with self.assertRaises(mod.Refused) as ctx:
                mod._section_body_at_commit(outer, outer_cand, "todo/00-x/TODO-01-y.md", REF)
        self.assertIn("git repository check failed", str(ctx.exception))

        with unittest.mock.patch.dict(os.environ, {"GIT_DIR": str(outer / "missing-git-dir")}, clear=False):
            with self.assertRaises(mod.Refused) as ctx:
                mod._section_body_at_commit(outer, outer_cand, "todo/00-x/TODO-01-y.md", REF)
        self.assertIn("git repository check failed", str(ctx.exception))

        corrupt = Path(self._tmp.name) / "corrupt-repo"
        corrupt.mkdir()
        (corrupt / ".git").write_text("gitdir: missing\n", encoding="utf-8")
        with unittest.mock.patch.dict(os.environ, {}, clear=False):
            for var in mod._GIT_STORE_ENV_VARS:
                os.environ.pop(var, None)
            with self.assertRaises(mod.Refused) as ctx:
                mod._section_body_at_commit(corrupt, CAND, "todo/00-x/TODO-01-y.md", REF)
        self.assertIn("git repository check failed", str(ctx.exception))

        with unittest.mock.patch.dict(os.environ, {"GIT_CEILING_DIRECTORIES": str(outer)}, clear=False):
            for var in mod._GIT_STORE_ENV_VARS:
                os.environ.pop(var, None)
            self.assertIsNone(mod._section_body_at_commit(nested, CAND, "todo/00-x/TODO-01-y.md", REF))

        # Reconciliation 5 finding 1: existing repo whose .git/objects is inaccessible causes Git to emit the
        # discovery-only diagnostic; _has_git_marker_within_discovery_boundary catches it and refuses.
        with unittest.mock.patch.dict(os.environ, {}, clear=False), \
                unittest.mock.patch.object(mod.subprocess, "run", return_value=fake_not_git):
            for var in mod._GIT_STORE_ENV_VARS:
                os.environ.pop(var, None)
            with self.assertRaises(mod.Refused) as ctx:
                mod._section_body_at_commit(outer, outer_cand, "todo/00-x/TODO-01-y.md", REF)
        self.assertIn("git repository check failed", str(ctx.exception))

        # Filesystem mount boundary stops the marker scan unless GIT_DISCOVERY_ACROSS_FILESYSTEM is set.
        real_stat = os.stat
        outer_real = os.path.normcase(os.path.realpath(outer))

        class _FakeStat:
            def __init__(self, dev):
                self.st_dev = dev

        def mount_boundary_stat(p, *a, **kw):
            st = real_stat(p, *a, **kw)
            if os.path.normcase(os.path.realpath(p)) == outer_real:
                return _FakeStat(st.st_dev + 1)
            return st

        with unittest.mock.patch.dict(os.environ, {}, clear=False), \
                unittest.mock.patch.object(mod.os, "stat", side_effect=mount_boundary_stat), \
                unittest.mock.patch.object(mod.subprocess, "run", return_value=fake_not_git):
            for var in mod._GIT_STORE_ENV_VARS:
                os.environ.pop(var, None)
            os.environ.pop("GIT_DISCOVERY_ACROSS_FILESYSTEM", None)
            self.assertIsNone(mod._section_body_at_commit(nested, CAND, "todo/00-x/TODO-01-y.md", REF))

        # Reconciliation 6/7 finding 9: Git's own bool parser handles '2', '01', and '1k' as truthy.
        real_run = subprocess.run
        def fake_rev_parse_real_config(cmd, *a, **kw):
            if len(cmd) >= 2 and cmd[1] == "-c":
                return real_run(cmd, *a, **kw)
            return fake_not_git

        for truthy in ("2", "01", "1k"):
            with self.subTest(across_fs=truthy):
                with unittest.mock.patch.dict(os.environ, {"GIT_DISCOVERY_ACROSS_FILESYSTEM": truthy}, clear=False), \
                        unittest.mock.patch.object(mod.os, "stat", side_effect=mount_boundary_stat), \
                        unittest.mock.patch.object(mod.subprocess, "run", side_effect=fake_rev_parse_real_config):
                    for var in mod._GIT_STORE_ENV_VARS:
                        os.environ.pop(var, None)
                    with self.assertRaises(mod.Refused) as ctx:
                        mod._section_body_at_commit(nested, CAND, "todo/00-x/TODO-01-y.md", REF)
                self.assertIn("git repository check failed", str(ctx.exception))

        # D00 T03 §2 (filed P3 from D00 T03 §1 Rec 8): inherited GIT_CONFIG, GIT_CONFIG_PARAMETERS, and
        # GIT_CONFIG_COUNT must not disrupt _git_across_fs()'s Git --type=bool parse. Use independently
        # disruptive values so removing any one key from _git_across_fs()'s filter makes git config fail.
        for label, extra_env in (
            ("GIT_CONFIG", {"GIT_CONFIG": os.devnull}),
            ("GIT_CONFIG_PARAMETERS", {"GIT_CONFIG_PARAMETERS": "malformed"}),
            ("GIT_CONFIG_COUNT", {"GIT_CONFIG_COUNT": "1"}),
        ):
            with self.subTest(config_override=label):
                test_env = {**os.environ, "GIT_DISCOVERY_ACROSS_FILESYSTEM": "1", **extra_env}
                test_env.pop("GIT_CONFIG_KEY_0", None)
                c_env = {**test_env, "LC_ALL": "C", "LANG": "C", "LANGUAGE": "C"}
                self.assertTrue(mod._git_across_fs(test_env, c_env))
                with unittest.mock.patch.dict(os.environ, {"GIT_DISCOVERY_ACROSS_FILESYSTEM": "1", **extra_env},
                                              clear=False), \
                        unittest.mock.patch.object(mod.os, "stat", side_effect=mount_boundary_stat), \
                        unittest.mock.patch.object(mod.subprocess, "run", side_effect=fake_rev_parse_real_config):
                    os.environ.pop("GIT_CONFIG_KEY_0", None)
                    for var in mod._GIT_STORE_ENV_VARS:
                        os.environ.pop(var, None)
                    with self.assertRaises(mod.Refused) as ctx:
                        mod._section_body_at_commit(nested, CAND, "todo/00-x/TODO-01-y.md", REF)
                self.assertIn("git repository check failed", str(ctx.exception))

        # Reconciliation 7 finding 1: EIO/EACCES on lstat(".git") must refuse instead of returning False via lexists().
        import errno
        real_lstat = os.lstat
        def fail_git_lstat(p, *a, **kw):
            if Path(p).name == ".git":
                raise OSError(errno.EIO, "simulated I/O error on .git")
            return real_lstat(p, *a, **kw)

        with unittest.mock.patch.dict(os.environ, {}, clear=False), \
                unittest.mock.patch.object(mod.os, "lstat", side_effect=fail_git_lstat), \
                unittest.mock.patch.object(mod.subprocess, "run", return_value=fake_not_git):
            for var in mod._GIT_STORE_ENV_VARS:
                os.environ.pop(var, None)
            with self.assertRaises(mod.Refused) as ctx:
                mod._section_body_at_commit(self.root, CAND, "todo/00-x/TODO-01-y.md", REF)
        self.assertIn("git repository check failed", str(ctx.exception))

        # Reconciliation 6 finding 1: dangling .git symlink refuses instead of returning None via Path.exists().
        dangling = Path(self._tmp.name) / "dangling-repo"
        dangling.mkdir()
        dangling_git = dangling / ".git"
        try:
            os.symlink(dangling / "offline-volume-target", dangling_git)
            dangling_made = True
        except OSError as exc:
            if os.name == "nt" and getattr(exc, "winerror", None) == 1314:
                dangling_made = False
            else:
                raise
        if dangling_made:
            with unittest.mock.patch.dict(os.environ, {}, clear=False), \
                    unittest.mock.patch.object(mod.subprocess, "run", return_value=fake_not_git):
                for var in mod._GIT_STORE_ENV_VARS:
                    os.environ.pop(var, None)
                with self.assertRaises(mod.Refused) as ctx:
                    mod._section_body_at_commit(dangling, CAND, "todo/00-x/TODO-01-y.md", REF)
            self.assertIn("git repository check failed", str(ctx.exception))


class NoGraph(Filler):
    def test_no_lookup_script_asks_for_todo(self):
        message = self.refused("plan", "--todo is needed", "--ref", REF)
        self.assertIn("todo-graph.py is not beside", message)

    def test_round_needs_no_lookup(self):
        self.assertIn(f"`{REF}`", self.ok("round", "--ref", REF))

    def test_all_four_prompts_point_at_appended_rubric_with_no_inline_definitions_or_suppression(self):
        inline_def = re.compile(r"P[123]`?\s*\(")
        for kind in KINDS:
            extra = ["--ref", REF]
            if kind in ("plan", "completion"):
                extra += ["--todo", "todo/00-x/TODO-01-y.md"]
            if kind == "reconciliation":
                extra += ["--findings", "docs/reviews/00-x/D00-T01-s7.md"]
            text = self.ok(kind, *extra)
            with self.subTest(kind=kind):
                self.assertIsNone(inline_def.search(text), f"{kind} still defines P1/P2/P3 inline")
                for forbidden in ("list only", "report only", "Do not list a need", "Proportionality:"):
                    self.assertNotIn(forbidden, text, f"{kind} still contains suppression phrase {forbidden!r}")
                self.assertIn("likelihood-weighted severity rubric", text)
                self.assertIn("scripts/review-dispatch.py", text)
                if kind == "reconciliation":
                    self.assertIn("## Writer note:", text)
                    self.assertIn("contests on its grade", text)
                    self.assertIn("revised grade contributes to `HIGHEST:`", text)


class ThroughTheDispatcher(Harness):
    def test_a_filled_round_prompt_reaches_the_reviewer(self):
        install(self.repo)
        filled = self.scratch / "round.md"
        done = subprocess.run([sys.executable, "-B", str(self.repo / "scripts/review-prompt.py"), "--kind", "round",
                               "--candidate", CAND, "--purpose", "Fill review prompts.", "--governing", "docs/plan.md",
                               "--out", str(filled)], capture_output=True, text=True, timeout=60, cwd=self.repo)
        self.assertEqual(done.returncode, 0, done.stderr)
        result, summary = self.dispatch("--writer", "claude", "--prompt", str(filled), cwd=self.repo)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIsNotNone(summary)
        sent = self.calls()[-1]["stdin"]
        self.assertIn(CAND, sent)
        self.assertEqual(sent, filled.read_text(encoding="utf-8").rstrip("\n") + self.module().review_pin.SEVERITY_RUBRIC)
        self.assertEqual(PLACEHOLDER.findall(sent), [])


if __name__ == "__main__":
    unittest.main()
