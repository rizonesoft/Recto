"""Tests for scripts/lib/todo_graph.py and scripts/todo-graph.py: the graph refuses malformed work and stamps only real evidence."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts" / "lib"))
import todo_graph as tg  # noqa: E402

ADJACENCY = ("**Adjacency:** list=not-applicable (fixture); document=not-applicable (fixture); "
             "settings=not-applicable (fixture); reporting=not-applicable (fixture); "
             "notifications=not-applicable (fixture); permissions=not-applicable (fixture); "
             "audit=not-applicable (fixture); exchange=not-applicable (fixture); "
             "reverse=not-applicable (fixture)")


def section(n, title, phase, needs, extra=""):
    return (f"## {n}. {title}\n\n**Phase:** {phase}\n**Needs:** {needs}\n**Surface:** no\n\n**Build order:**\n\n"
            f"1. Do it. Done when: it shows.\n\n- [ ] Work.\n- [ ] Commit: `x: {title.lower()}`\n\n"
            f"**Test checkpoint:** `true` exits 0.{extra}\n")


def todo_file(rows, *, ident="alpha", domain="00-workspace", status="active", depends_on=None,
              table_override=None, inputs="- i"):
    """rows: (number, title, phase, needs, depends) tuples."""
    front = ["schema_version: 1", f"id: {ident}", f"domain: {domain}", f"status: {status}",
             'title: "TODO-01 -- A"']
    if depends_on is not None:
        front.append("depends_on: " + json.dumps(depends_on))
    table = table_override or "\n".join(
        f"| {i} | §{n} | {t} | {dep} | [ ] |" for i, (n, t, _, _, dep) in enumerate(rows, 1))
    bodies = "\n".join(section(n, t, ph, nd) for n, t, ph, nd, _ in rows)
    return ("---\n" + "\n".join(front) + "\n---\n\n# TODO-01 -- A\n\n> **Goal:** g.\n\n"
            "> **Current state:** c.\n\n## Inputs\n\n" + inputs + "\n\n## Outcome\n\n- o\n\n"
            + ADJACENCY + "\n\n"
            "## Implementation Order\n\n| Order | Section | Deliverable | Depends On | Status |\n"
            "| ----- | ------- | ----------- | ---------- | ------ |\n"
            + table + "\n\n" + bodies + "\n## Verification\n\n- v\n")


class Tree(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        (self.root / "todo/00-workspace").mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, text, name="TODO-01-a.md", domain="00-workspace", index=True):
        d = self.root / "todo" / domain
        d.mkdir(parents=True, exist_ok=True)
        (d / name).write_text(text, encoding="utf-8")
        if index:
            self.index(name, domain)

    def index(self, name, domain="00-workspace"):
        for path in (self.root / "todo" / domain / "INDEX.md", self.root / "todo/TODO-00-INDEX.md"):
            existing = path.read_text(encoding="utf-8") if path.exists() else "# Index\n"
            if name not in existing:
                path.write_text(existing + f"- {name}\n", encoding="utf-8")

    def refuses(self, fragment):
        with self.assertRaises(tg.Refused) as ctx:
            tg.graph(self.root)
        self.assertIn(fragment, str(ctx.exception))


class Graph(Tree):
    def test_valid_tree_orders_by_phase_and_dependency(self):
        self.write(todo_file([(1, "Late", 1, "none", "--"), (2, "Early", 0, "none", "--"),
                              (3, "After early", 0, "windows-host", "§2")]))
        rows = tg.graph(self.root)
        self.assertEqual([r.ref for r in tg.ordered(rows)],
                         ["D00 T01 §2", "D00 T01 §3", "D00 T01 §1"])
        self.assertEqual(tg.disposition(rows["D00 T01 §2"], rows), 0)
        self.assertEqual(tg.disposition(rows["D00 T01 §3"], rows), 4)

    def test_table_order_decides_among_ready_sections(self):
        table = "| 1 | §2 | B | -- | [ ] |\n| 2 | §1 | A | -- | [ ] |"
        self.write(todo_file([(1, "A", 0, "none", "--"), (2, "B", 0, "none", "--")],
                             table_override=table))
        self.assertEqual([r.ref for r in tg.ordered(tg.graph(self.root))],
                         ["D00 T01 §2", "D00 T01 §1"])

    def test_empty_tree_refuses(self):
        self.refuses("missing or empty")

    def test_malformed_row_is_not_empty_work(self):
        self.write(todo_file([(1, "A", 0, "none", "--")], table_override="| 1 | §1 | A | -- | [?] |"))
        self.refuses("malformed implementation row")

    def test_row_without_body_refuses(self):
        text = todo_file([(1, "A", 0, "none", "--")]).replace(
            "| 1 | §1 | A | -- | [ ] |", "| 1 | §1 | A | -- | [ ] |\n| 2 | §2 | B | -- | [ ] |")
        self.write(text)
        self.refuses("row/body mismatch")

    def test_cycle_refuses(self):
        self.write(todo_file([(1, "A", 0, "none", "§2"), (2, "B", 0, "none", "§1")]))
        self.refuses("dependency cycle")

    def test_prerequisite_in_a_later_phase_refuses(self):
        self.write(todo_file([(1, "A", 0, "none", "§2"), (2, "B", 1, "none", "--")]))
        self.refuses("prerequisite in later phase")

    def test_unknown_need_refuses(self):
        self.write(todo_file([(1, "A", 0, "novamira", "--")]))
        self.refuses("unknown/duplicate Needs")

    def test_commit_must_be_the_last_item(self):
        self.write(todo_file([(1, "A", 0, "none", "--")]).replace(
            "- [ ] Commit: `x: a`", "- [ ] Commit: `x: a`\n- [ ] After."))
        self.refuses("Commit last")

    def test_waiting_needs_are_not_ready(self):
        self.write(todo_file([(1, "A", 0, "owner-decision, windows-host", "--")]))
        rows = tg.graph(self.root)
        self.assertEqual(tg.disposition(rows["D00 T01 §1"], rows), 4)

    def test_operator_needs_are_not_ready(self):
        self.write(todo_file([(1, "A", 99, "operator", "--")]))
        rows = tg.graph(self.root)
        self.assertEqual(tg.disposition(rows["D00 T01 §1"], rows), 4)

    def test_draft_file_is_not_ready(self):
        self.write(todo_file([(1, "A", 0, "none", "--")], status="draft"))
        rows = tg.graph(self.root)
        self.assertEqual(tg.disposition(rows["D00 T01 §1"], rows), 4)

    def test_file_dependency_reaches_every_section(self):
        self.write(todo_file([(1, "A", 0, "none", "--")], ident="first"))
        self.write(todo_file([(1, "B", 1, "none", "--"), (2, "C", 1, "none", "--")],
                             ident="second", depends_on=["first"]), name="TODO-02-b.md")
        rows = tg.graph(self.root)
        self.assertEqual(rows["D00 T02 §2"].depends, ["D00 T01 §1"])

    def test_checked_row_without_stamp_refuses(self):
        self.write(todo_file([(1, "A", 0, "none", "--")]).replace("| [ ] |", "| [x] |"))
        self.refuses("checkbox/stamp/receipt mismatch")

    def test_missing_from_index_refuses(self):
        self.write(todo_file([(1, "A", 0, "none", "--")]), index=False)
        (self.root / "todo/TODO-00-INDEX.md").write_text("# Index\n", encoding="utf-8")
        (self.root / "todo/00-workspace/INDEX.md").write_text("# Index\n", encoding="utf-8")
        self.refuses("missing from domain index")

    def test_adjacency_missing_key_refuses(self):
        text = todo_file([(1, "A", 0, "none", "--")]).replace(
            "reverse=not-applicable (fixture)", "reverse=not-applicable")
        self.write(text)
        self.refuses("adjacency entry malformed")

    def test_adjacency_blanket_form_is_valid(self):
        text = todo_file([(1, "A", 0, "none", "--")]).replace(
            ADJACENCY, "**Adjacency:** all=not-applicable (fixture with no record of its own)")
        self.write(text)
        self.assertEqual(len(tg.graph(self.root)), 1)

    def test_adjacency_blanket_reason_may_contain_semicolons(self):
        text = todo_file([(1, "A", 0, "none", "--")]).replace(
            ADJACENCY, "**Adjacency:** all=not-applicable (tooling over files; its records are the "
                       "files themselves, owned by their own sections)")
        self.write(text)
        self.assertEqual(len(tg.graph(self.root)), 1)

    def test_one_sided_xref_refuses(self):
        self.write(todo_file([(1, "A", 0, "none", "--")], ident="first"))
        self.write(todo_file([(1, "B", 0, "none", "--")], ident="second",
                             inputs="- -> XREF: D00 T01 §1 -- consumes it"), name="TODO-02-b.md")
        self.refuses("one-sided XREF")

    def test_reciprocal_xref_is_valid(self):
        self.write(todo_file([(1, "A", 0, "none", "--")], ident="first",
                             inputs="- -> XREF: D00 T02 §1 -- feeds it"))
        self.write(todo_file([(1, "B", 0, "none", "--")], ident="second",
                             inputs="- -> XREF: D00 T01 §1 -- consumes it"), name="TODO-02-b.md")
        self.assertEqual(len(tg.graph(self.root)), 2)

    def test_surface_yes_without_fidelity_refuses(self):
        text = todo_file([(1, "A", 0, "none", "--")]).replace("**Surface:** no", "**Surface:** yes")
        self.write(text)
        self.refuses("missing Fidelity field")

    def test_freeze_check_without_frozen_refuses(self):
        text = todo_file([(1, "A", 0, "none", "--")]).replace(
            "**Test checkpoint:**", "**Freeze check:** fixtures hold.\n\n**Test checkpoint:**")
        self.write(text)
        self.refuses("Freeze check must match frozen")

    def test_deferral_without_owner_refuses(self):
        text = todo_file([(1, "A", 0, "none", "--")]).replace(
            "**Test checkpoint:**", "> **Deferred:** later, somehow\n\n**Test checkpoint:**")
        self.write(text)
        self.refuses("without a resolving owner")

    def test_projection_check_detects_drift(self):
        self.write(todo_file([(1, "A", 0, "none", "--")]))
        tg.projections(self.root, sync=True)
        tg.projections(self.root)
        path = self.root / "todo/00-workspace/TODO-01-a.md"
        path.write_text(path.read_text(encoding="utf-8").replace("**Phase:** 0", "**Phase:** 2"),
                        encoding="utf-8")
        with self.assertRaises(tg.Refused):
            tg.projections(self.root)


class Stamp(Tree):
    def setUp(self):
        super().setUp()
        self.write(todo_file([(1, "A", 0, "none", "--"), (2, "B", 0, "owner-decision", "--")]))
        for args in (["init", "-q"], ["config", "user.email", "t@example.com"],
                     ["config", "user.name", "T"], ["add", "-A"], ["commit", "-qm", "x"]):
            subprocess.run(["git", "-C", str(self.root), *args], check=True)
        self.sha = subprocess.run(["git", "-C", str(self.root), "rev-parse", "HEAD"],
                                  capture_output=True, text=True).stdout.strip()
        self.reviews = [{"round": 1, "report": "docs/reviews/00-workspace/D00-T01-s1.md",
                         "verdict": "clean"}]

    def tick_work(self):
        path = self.root / "todo/00-workspace/TODO-01-a.md"
        path.write_text(path.read_text(encoding="utf-8").replace("- [ ] Work.", "- [x] Work.", 1),
                        encoding="utf-8")

    def test_stamp_writes_receipt_and_validates(self):
        self.tick_work()
        receipt = tg.stamp(self.root, "D00 T01 §1", self.sha, self.reviews, ["true -> ok"], "2026-10-13")
        rows = tg.graph(self.root)
        self.assertTrue(rows["D00 T01 §1"].done)
        self.assertEqual(json.loads((self.root / receipt).read_text(encoding="utf-8"))
                         ["implementation_sha"], self.sha)

    def test_unticked_items_refuse(self):
        with self.assertRaises(tg.Refused) as ctx:
            tg.stamp(self.root, "D00 T01 §1", self.sha, self.reviews, ["true -> ok"], "2026-10-13")
        self.assertIn("unticked", str(ctx.exception))

    def test_waiting_section_cannot_be_stamped(self):
        with self.assertRaises(tg.Refused):
            tg.stamp(self.root, "D00 T01 §2", self.sha, self.reviews, ["true -> ok"], "2026-10-13")

    def test_unknown_commit_refuses(self):
        self.tick_work()
        with self.assertRaises(tg.Refused):
            tg.stamp(self.root, "D00 T01 §1", "0" * 40, self.reviews, ["true -> ok"], "2026-10-13")

    def test_no_review_round_refuses(self):
        self.tick_work()
        with self.assertRaises(tg.Refused):
            tg.stamp(self.root, "D00 T01 §1", self.sha, [], ["true -> ok"], "2026-10-13")

    def test_receipt_for_another_commit_refuses(self):
        self.tick_work()
        receipt = tg.stamp(self.root, "D00 T01 §1", self.sha, self.reviews, ["true -> ok"], "2026-10-13")
        data = json.loads((self.root / receipt).read_text(encoding="utf-8"))
        data["implementation_sha"] = "f" * 40
        (self.root / receipt).write_text(json.dumps(data), encoding="utf-8")
        self.refuses("receipt does not match its stamp")

    def snapshot(self):
        return {str(f.relative_to(self.root)): f.read_bytes()
                for f in self.root.rglob("*") if f.is_file() and ".git" not in f.parts}

    def test_bad_verdict_writes_nothing(self):
        self.tick_work()
        before = self.snapshot()
        bad = [{"round": 1, "report": "docs/reviews/x.md", "verdict": "cleen"}]
        with self.assertRaises(tg.Refused):
            tg.stamp(self.root, "D00 T01 §1", self.sha, bad, ["true -> ok"], "2026-10-13")
        self.assertEqual(self.snapshot(), before)
        tg.graph(self.root)

    def test_bad_date_writes_nothing(self):
        self.tick_work()
        before = self.snapshot()
        with self.assertRaises(tg.Refused):
            tg.stamp(self.root, "D00 T01 §1", self.sha, self.reviews, ["true -> ok"], "13 Oct")
        self.assertEqual(self.snapshot(), before)

    def test_stamping_the_last_section_marks_the_file_done(self):
        path = self.root / "todo/00-workspace/TODO-02-b.md"
        path.write_text(todo_file([(1, "Only", 0, "none", "--")], ident="bravo")
                        .replace("- [ ] Work.", "- [x] Work."), encoding="utf-8")
        self.index("TODO-02-b.md")
        tg.stamp(self.root, "D00 T02 §1", self.sha, self.reviews, ["true -> ok"], "2026-10-13")
        self.assertIn("status: done", path.read_text(encoding="utf-8"))
        self.assertTrue(tg.graph(self.root)["D00 T02 §1"].done)

    def test_malformed_receipt_refuses_instead_of_crashing(self):
        self.tick_work()
        receipt = tg.stamp(self.root, "D00 T01 §1", self.sha, self.reviews, ["true -> ok"], "2026-10-13")
        unhashable = json.dumps({"ref": "D00 T01 §1", "implementation_sha": self.sha,
                                 "reviews": [{"round": 1, "report": "r", "verdict": []}],
                                 "checks": ["x"], "date": "2026-10-13"})
        for bad in ("[]", json.dumps({"ref": "D00 T01 §1", "implementation_sha": self.sha,
                                      "reviews": [None], "checks": ["x"], "date": "2026-10-13"}),
                    "not json", unhashable):
            (self.root / receipt).write_text(bad, encoding="utf-8")
            self.refuses("receipt")

    def test_missing_receipt_refuses(self):
        self.tick_work()
        receipt = tg.stamp(self.root, "D00 T01 §1", self.sha, self.reviews, ["true -> ok"], "2026-10-13")
        (self.root / receipt).unlink()
        self.refuses("missing receipt")


class Repository(unittest.TestCase):
    def test_the_repository_graph_validates_and_its_plan_is_current(self):
        r = subprocess.run([sys.executable, str(REPO / "scripts/todo-graph.py"), "plan", "--check"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == "__main__":
    unittest.main()
