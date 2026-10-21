#!/usr/bin/env python3
"""Check docs/user-demand.md, Recto's user demand ledger (D07 T03 §6).

    python scripts/check-user-demand.py            # exit 0 sound, 1 a row is wrong, 2 the ledger cannot be read

Every row must have a known status (planned, decided, not planned), an owner naming at least one TODO section or
foundation-plan decision, and a cost of reversing unless it is planned. Every TODO reference must resolve in the
TODO graph (the same graph `scripts/todo-graph.py resolve` reads), every decision `D<n>` must exist in the foundation
plan's decision table, and every `parity: <tool>` must name a row of docs/acrobat-parity.md. Every parity row that
is Excluded or Partial, or that points at this ledger, must have a ledger row. TODO references are checked in every
cell, and a backwards range or a file named without a section is a fault. No row may be dated in the future or
before the newest release, whose date is its annotated `v*` tag's own date (a lightweight release tag is a fault).

    python scripts/check-user-demand.py --release  # also: every row reviewed in the last 14 days (release runbook)
"""
import argparse
from datetime import date, timedelta
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
from todo_graph import Refused, graph  # noqa: E402

LEDGER = "docs/user-demand.md"
PARITY = "docs/acrobat-parity.md"
PLAN = "docs/plans/2026-10-13-recto-foundation.md"
COLUMNS = ["Request", "Evidence", "Status", "Owner", "Cost of reversing", "Last reviewed"]
STATUSES = {"planned", "decided", "not planned"}
# `D02 T06 §1-§15`, `D02 T01 §3, §13-§16`: a file reference, then its sections and ranges up to the next file.
SECTION_RUN = re.compile(r"\b(D\d\d T\d\d)((?:\s*,?\s*§\d+(?:\s*-\s*§?\d+)?)+)")
SECTION = re.compile(r"§(\d+)(?:\s*-\s*§?(\d+))?")
FILE_ONLY = re.compile(r"\bD\d\d T\d\d\b(?!\s*,?\s*§)")
RELEASE_WINDOW = timedelta(days=14)
# A decision is D<n> without a leading zero and not followed by a TODO file number (any length, so D100 is checked).
DECISION = re.compile(r"\bD([1-9]\d*)\b(?!\s*T\d)")
PARITY_REF = re.compile(r"parity: ([^;|]+)")


DIVIDER = re.compile(r"\|?(\s*:?-+:?\s*\|)+\s*:?-*:?\s*")
UNESCAPED_PIPE = re.compile(r"(?<!\\)\|")


def split_cells(line):
    """The cells of one `| a | b |` row: one leading and one trailing pipe dropped, `\\|` kept as a literal pipe."""
    inner = line.strip()[1:]
    inner = inner[:-1] if inner.endswith("|") and not inner.endswith("\\|") else inner
    return [cell.strip().replace("\\|", "|") for cell in UNESCAPED_PIPE.split(inner)]


def tables(text):
    """Every Markdown table as (header cells, rows, faults). A table is a header row and a divider, then every line up
    to the next blank line. Each of those lines must be written as a full `| ... |` row; any other line (a row without
    its leading pipe, stray text) is a fault rather than a row the checks never see."""
    lines = [line.strip() for line in text.splitlines()]
    found, i = [], 0
    while i < len(lines) - 1:
        if lines[i].startswith("|") and DIVIDER.fullmatch(lines[i + 1]):
            header, rows, faults = split_cells(lines[i]), [], []
            i += 2
            while i < len(lines) and lines[i]:
                if lines[i].startswith("|") and lines[i].endswith("|"):
                    rows.append(split_cells(lines[i]))
                else:
                    faults.append(f"{lines[i][:40]!r} is inside a table but is not a `| ... |` row")
                i += 1
            found.append((header, rows, faults))
        else:
            i += 1
    return found


def section_refs(text):
    """The TODO sections a cell names, with ranges and lists expanded, and the faults in how it names them: a file
    without a section, a backwards range, or a `§` the reference syntax does not reach (`§23 and §99`)."""
    refs, faults = [], [f"{m.group(0)} names a TODO file without a section" for m in FILE_ONLY.finditer(text)]
    consumed = set()
    for match in SECTION_RUN.finditer(text):
        consumed.update(range(*match.span(2)))
        prefix = match.group(1)
        for first, last in SECTION.findall(match.group(2)):
            if last and int(last) < int(first):
                faults.append(f"{prefix} §{first}-§{last} runs backwards")
                continue
            refs += [f"{prefix} §{n}" for n in range(int(first), int(last or first) + 1)]
    for mark in re.finditer("§", text):
        if mark.start() not in consumed:
            faults.append(f"{text[mark.start():mark.start() + 8].strip()} is not part of a `DNN TNN §N` reference")
    return refs, faults


def parity_names(text, sections):
    """Every tool name in the parity ledger, and the ones that must have a ledger row: no resolving TODO section in the
    table's `Recto sections` column (a missing, unknown or malformed reference counts as none; a short row's missing
    cells are empty, as Markdown renders them), Excluded or Partial, or naming this ledger."""
    names, needed = set(), set()
    for header, rows, _ in tables(text):
        if "Recto sections" not in header:
            continue
        sections_at = header.index("Recto sections")
        for cells in rows:
            cells = cells + [""] * (len(header) - len(cells))
            name, owner = cells[0], cells[sections_at]
            names.add(name)
            refs, faults = section_refs(owner)
            # A parity owner may name a whole TODO file (`D04 T08`), which counts when the file has sections.
            files = [m.group(0) for m in FILE_ONLY.finditer(owner)]
            faults = [fault for fault in faults if not fault.endswith("names a TODO file without a section")]
            owned = (bool(refs) or bool(files)) and not faults and all(ref in sections for ref in refs) and all(
                any(section.startswith(f"{file} §") for section in sections) for file in files)
            if not owned or "Excluded" in cells or "Partial" in cells or "user-demand.md" in " | ".join(cells):
                needed.add(name)
    return names, needed


def last_release(root):
    """The newest `v*` release tag, its date and whether it is annotated. An annotated tag's date is when the release
    was tagged; a lightweight tag only has its commit's date, which can be far older, so it is reported as a fault."""
    out = subprocess.run(["git", "-C", str(root), "for-each-ref", "refs/tags/v*", "--sort=-creatordate",
                          "--format=%(objecttype) %(refname:short) %(creatordate:short)"],
                         capture_output=True, text=True, check=True).stdout.split("\n")[0].split()
    if len(out) != 3:
        return None, None, True
    return out[1], date.fromisoformat(out[2]), out[0] == "tag"


def problems(ledger_text, parity_text, plan_text, sections, release=(None, None, True), today=None,
             release_window=None):
    """Every fault in the ledger, as readable lines. `release_window` (the --release mode) requires every row to have
    been reviewed within that many days of `today`."""
    today = today or date.today()
    found = []
    ledger = [table for table in tables(ledger_text) if table[0] == COLUMNS]
    if len(ledger) != 1:
        return [f"the ledger must hold exactly one table with the columns {' | '.join(COLUMNS)}"]
    _, rows, found = ledger[0]
    found = list(found)
    if not rows:
        return ["the ledger has no rows"]
    decisions = {int(n) for n in re.findall(r"^\| D(\d+) \|", plan_text, re.M)}
    parity, needed = parity_names(parity_text, sections)
    covered = set()
    tag, released, annotated = release
    if tag and not annotated:
        found.append(f"release tag {tag} is lightweight; tag releases with `git tag -a` so their date is the release's")
    for number, cells in enumerate(rows, 1):
        where = f"row {number}"
        if len(cells) != len(COLUMNS):
            found.append(f"{where}: has {len(cells)} cells, expected {len(COLUMNS)}")
            continue
        request, evidence, status, owner, cost, reviewed = cells
        where = f"row {number} ({request})"
        if not request:
            found.append(f"{where}: no request")
        if status not in STATUSES:
            found.append(f"{where}: status {status!r} is not planned, decided or not planned")
        refs, faults = section_refs(owner)
        owned_decisions = [int(n) for n in DECISION.findall(owner)]
        if not refs and not owned_decisions:
            found.append(f"{where}: the owner names no TODO section and no decision")
        if status == "planned" and not refs:
            found.append(f"{where}: a planned row needs a TODO section that builds it")
        cited_decisions = list(owned_decisions)
        for cell in (request, evidence, cost):
            more, more_faults = section_refs(cell)
            refs, faults = refs + more, faults + more_faults
            cited_decisions += [int(n) for n in DECISION.findall(cell)]
        found += [f"{where}: {fault}" for fault in faults]
        for ref in dict.fromkeys(refs):
            if ref not in sections:
                found.append(f"{where}: {ref} does not resolve")
        for decision in dict.fromkeys(cited_decisions):
            if decision not in decisions:
                found.append(f"{where}: decision D{decision} is not in the foundation plan")
        if status != "planned" and not cost:
            found.append(f"{where}: a {status} row needs its cost of reversing")
        names = [name.strip() for name in PARITY_REF.findall(evidence)]
        if not names and not re.search(r"#\d+", evidence):
            found.append(f"{where}: evidence names no issue and no parity row")
        for name in names:
            if name not in parity:
                found.append(f"{where}: parity row {name!r} is not in {PARITY}")
            covered.add(name)
        try:
            when = date.fromisoformat(reviewed)
        except ValueError:
            found.append(f"{where}: last reviewed {reviewed!r} is not a YYYY-MM-DD date")
            continue
        if when > today:
            found.append(f"{where}: last reviewed {reviewed} is in the future")
        if released and when < released:
            found.append(f"{where}: last reviewed {reviewed} is before the {tag} release ({released}); review it")
        if release_window and when < today - release_window:
            found.append(f"{where}: last reviewed {reviewed} is more than {release_window.days} days ago; review it "
                         f"before the release")
    for name in sorted(needed - covered):
        found.append(f"parity row {name!r} has no owning TODO section and no ledger row")
    return found


def main(argv=None, root=ROOT):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--release", action="store_true", help="also require every row reviewed in the last 14 days")
    args = parser.parse_args(argv)
    try:
        texts = [(Path(root) / name).read_text(encoding="utf-8") for name in (LEDGER, PARITY, PLAN)]
        sections = set(graph(root))
    except (OSError, UnicodeDecodeError, Refused) as exc:
        print(f"check-user-demand: cannot read the ledger or the TODO graph: {exc}", file=sys.stderr)
        return 2
    found = problems(*texts, sections, last_release(root), release_window=RELEASE_WINDOW if args.release else None)
    if found:
        print("\n".join(found))
        return 1
    print(f"{LEDGER}: every row resolves and every unowned parity row has an answer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
