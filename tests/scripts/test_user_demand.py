"""Tests for scripts/check-user-demand.py and the committed ledger (D07 T03 §6)."""
import contextlib
from datetime import date
import importlib.util
import io
import pathlib
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("check_user_demand", REPO / "scripts" / "check-user-demand.py")
demand = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(demand)

HEADER = ("| Request | Evidence | Status | Owner | Cost of reversing | Last reviewed |\n"
          "| --- | --- | --- | --- | --- | --- |\n")
PARITY = ("| Acrobat Pro tool | Status | Recto sections | Recto keeps |\n| --- | --- | --- | --- |\n"
          "| Web app | Excluded | | Desktop only |\n"
          "| Split view | Planned | D02 T02 §23-§24 | Tab tear-off stays recorded in `docs/user-demand.md` |\n"
          "| Merge | Planned | D05 T01 §1 | |\n")
PLAN = "| D15 | Target OS | Windows 11 | | |\n| D17 | Layout | One shell | | |\n"
SECTIONS = {"D02 T02 §23", "D02 T02 §24", "D05 T01 §1"}
VALID = {
    "web": "| A web version | parity: Web app | not planned | D15: desktop only | A second front end | 2026-10-21 |",
    "split": "| Side by side | parity: Split view | planned | D02 T02 §23-§24 | None: planned | 2026-10-21 |",
}


def ledger(*rows):
    return "# Ledger\n\n" + HEADER + "\n".join(rows) + "\n"


TODAY = date(2026, 10, 21)


def problems(*rows, release=(None, None, True), parity=PARITY, **kwargs):
    return demand.problems(ledger(*rows), parity, PLAN, SECTIONS, release, today=TODAY, **kwargs)


class LedgerRulesTest(unittest.TestCase):
    def test_a_sound_ledger_has_no_problems(self):
        self.assertEqual(problems(*VALID.values()), [])

    def test_each_rule_refuses_its_fault(self):
        web, split = VALID["web"], VALID["split"]
        cases = {
            "status": (web.replace("not planned", "maybe"), "is not planned, decided or not planned"),
            "owner": (web.replace("D15: desktop only", "someone"), "names no TODO section and no decision"),
            "section": (split.replace("§23-§24", "§23-§25"), "D02 T02 §25 does not resolve"),
            "decision": (web.replace("D15", "D99"), "decision D99 is not in the foundation plan"),
            "cost": (web.replace("A second front end", ""), "needs its cost of reversing"),
            "parity name": (web.replace("parity: Web app", "parity: Web ap"), "is not in docs/acrobat-parity.md"),
            "evidence": (web.replace("parity: Web app", "asked often"), "evidence names no issue and no parity row"),
            "date": (web.replace("2026-10-21", "21 Oct"), "is not a YYYY-MM-DD date"),
            "cells": (web.replace(" | 2026-10-21 |", " |"), "has 5 cells"),
            "request ref": (web.replace("A web version", "A web version (D99 T99 §999)"), "D99 T99 §999 does not resolve"),
            "backwards": (split.replace("§23-§24", "§24-§23"), "runs backwards"),
            "file only": (web.replace("D15: desktop only", "D15: desktop only, see D02 T02"), "without a section"),
            "future": (web.replace("2026-10-21", "9999-12-31"), "is in the future"),
            "request decision": (web.replace("A web version", "A web version (D99)"), "decision D99 is not in"),
            "planned decision only": (split.replace("D02 T02 §23-§24", "D17"), "needs a TODO section that builds it"),
            "unparsed section": (split.replace("§23-§24", "§23 and §999"), "is not part of a `DNN TNN §N` reference"),
        }
        for name, (row, message) in cases.items():
            with self.subTest(rule=name):
                found = problems(row, split)
                self.assertTrue(any(message in line for line in found), found)

    def test_every_unowned_parity_row_needs_a_ledger_row(self):
        found = problems(VALID["split"])
        self.assertIn("parity row 'Web app' has no owning TODO section and no ledger row", found)
        # A Planned row that points at the ledger needs a row too.
        found = problems(VALID["web"])
        self.assertIn("parity row 'Split view' has no owning TODO section and no ledger row", found)

    def test_a_parity_row_without_a_section_needs_a_ledger_row_whatever_its_status(self):
        parity = PARITY + "| New request | Planned | | No owner yet |\n"
        self.assertIn("parity row 'New request' has no owning TODO section and no ledger row",
                      problems(*VALID.values(), parity=parity))

    def test_rows_older_than_the_last_release_are_refused(self):
        found = problems(*VALID.values(), release=("v1.0.0", date(2026, 11, 1), True))
        self.assertTrue(any("is before the v1.0.0 release" in line for line in found), found)
        self.assertEqual(problems(*VALID.values(), release=("v1.0.0", date(2026, 10, 21), True)), [])

    def test_a_lightweight_release_tag_is_refused(self):
        found = problems(*VALID.values(), release=("v1.0.0", date(2026, 10, 1), False))
        self.assertTrue(any("is lightweight" in line for line in found), found)

    def test_release_mode_needs_a_recent_review(self):
        stale = [row.replace("2026-10-21", "2026-09-30") for row in VALID.values()]
        self.assertEqual(problems(*stale), [])
        found = problems(*stale, release_window=demand.RELEASE_WINDOW)
        self.assertEqual(len([line for line in found if "more than 14 days ago" in line]), 2, found)

    def test_wrong_columns_are_refused(self):
        text = "| Request | Status | Owner |\n| --- | --- | --- |\n| A | planned | D02 T02 §23 |\n"
        self.assertEqual(demand.problems(text, PARITY, PLAN, SECTIONS),
                         [f"the ledger table must have the columns {' | '.join(demand.COLUMNS)}"])

    def test_section_ranges_and_lists_expand(self):
        self.assertEqual(demand.section_refs("D02 T01 §3, §13-§15, D04 T04 §8"),
                         (["D02 T01 §3", "D02 T01 §13", "D02 T01 §14", "D02 T01 §15", "D04 T04 §8"], []))


class CommittedLedgerTest(unittest.TestCase):
    def test_the_committed_ledger_is_sound(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(demand.main([], root=REPO), 0, out.getvalue())

    def test_scope_page_covers_its_topics_and_is_linked(self):
        scope = (REPO / "docs" / "scope.md").read_text(encoding="utf-8")
        for heading in ("## Which Windows versions and processors does Recto support?",
                        "## What does Recto never do?", "## How does Recto handle XFA and dynamic forms?",
                        "## Where do I ask a question or report a problem?"):
            self.assertIn(f"\n{heading}\n", scope)
        self.assertIn("(docs/scope.md)", (REPO / "README.md").read_text(encoding="utf-8"))
        self.assertIn("docs/user-demand.md", (REPO / "docs" / "support" / "triage.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
