#!/usr/bin/env python3
"""Merge dependency pull requests (Dependabot and the pins workflow) when it is safe, one at a time.

    python scripts/merge_prs.py              # merge what is eligible now
    python scripts/merge_prs.py --dry-run    # say what would merge, change nothing
    python scripts/merge_prs.py --scheduled  # the recto-pr-merge task: stand aside while a campaign runs

Safe to run: the working tree is clean, HEAD is origin/main after a fetch (nothing waits to be pushed or pulled), no
TODO section is in progress (open, started and ready), and, when scheduled, no campaign run guard is held.

Eligible: opened by Dependabot or the pins workflow, labelled `dependencies` or `pins`, every check green, GitHub says
mergeable, and its branch is not on EXCLUDED_BRANCHES (pins that are section work, merged by that section instead).

Each merge: `gh pr merge <n> --rebase --match-head-commit <sha>` (linear history, and only the commit that was
checked), then `git pull --ff-only`, then `todo-graph.py validate`. The first failure stops the run and leaves the
remaining pull requests for the operator.

Exit 0: merged, or nothing eligible, or not safe right now (said why). Exit 1: a merge or its follow-up failed.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import todo_graph  # noqa: E402

AUTHORS = {"app/dependabot", "dependabot[bot]", "app/github-actions", "github-actions[bot]"}
LABELS = {"dependencies", "pins"}
# Branch prefixes of pin pull requests that belong to TODO section work and are merged there, never here. Recto has
# none today: the Windows SDK line moves through an issue and the operator's decision, not a pull request.
EXCLUDED_BRANCHES: tuple[str, ...] = ()
GREEN = {"SUCCESS", "NEUTRAL", "SKIPPED"}
GUARD = Path("build") / "claude-campaign-guard.json"


class Unsafe(Exception):
    """Not safe to merge right now; nothing was changed."""


def run_cmd(args, root):
    return subprocess.run(args, cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=600)


def check(run, root, args, what):
    result = run(args, root)
    if result.returncode != 0:
        raise RuntimeError(f"{what} failed (exit {result.returncode}): {(result.stderr or result.stdout).strip()[:400]}")
    return result.stdout


def section_in_progress(root):
    """An open section with a Started line that is ready to work: someone is building it now. A started section that
    later became blocked is not in progress."""
    rows = todo_graph.graph(root)
    for row in todo_graph.ordered(rows):
        if (not row.done and re.search(r"^> \*\*Started:\*\*", row.body, re.M)
                and todo_graph.disposition(row, rows) == 0):
            return row.ref
    return None


def ensure_safe(run, root, scheduled):
    if scheduled and (Path(root) / GUARD).exists():
        raise Unsafe("a campaign run holds the guard; the loop merges at its own section boundaries")
    if check(run, root, ["git", "status", "--porcelain"], "git status").strip():
        raise Unsafe("the working tree has changes")
    check(run, root, ["git", "fetch", "--quiet", "origin", "main"], "git fetch")
    head = check(run, root, ["git", "rev-parse", "HEAD"], "git rev-parse HEAD").strip()
    upstream = check(run, root, ["git", "rev-parse", "origin/main"], "git rev-parse origin/main").strip()
    if head != upstream:
        raise Unsafe(f"HEAD {head[:7]} is not origin/main {upstream[:7]}")
    ref = section_in_progress(root)
    if ref:
        raise Unsafe(f"section {ref} is in progress")


def checks_green(pr):
    rollup = pr.get("statusCheckRollup") or []
    if not rollup:
        return False  # no checks ran, so nothing says the change builds
    for item in rollup:
        if item.get("__typename") == "StatusContext":
            if item.get("state") != "SUCCESS":
                return False
        elif item.get("status") != "COMPLETED" or item.get("conclusion") not in GREEN:
            return False
    return True


def why_not(pr):
    """Why a pull request is not eligible, or None when it is."""
    if (pr.get("author") or {}).get("login") not in AUTHORS:
        return "not from Dependabot or the pins workflow"
    if not LABELS & {label["name"] for label in pr.get("labels") or []}:
        return "not labelled dependencies or pins"
    if EXCLUDED_BRANCHES and pr.get("headRefName", "").startswith(EXCLUDED_BRANCHES):
        return "a pin that is section work"
    if pr.get("mergeable") != "MERGEABLE":
        return f"GitHub says {pr.get('mergeable')}"
    if not checks_green(pr):
        return "its checks are not all green"
    return None


def open_prs(run, root):
    fields = "number,title,author,labels,headRefName,headRefOid,mergeable,statusCheckRollup"
    out = check(run, root, ["gh", "pr", "list", "--state", "open", "--limit", "100", "--json", fields], "gh pr list")
    return sorted(json.loads(out), key=lambda pr: pr["number"])


def main(argv=None, run=run_cmd, root=ROOT, out=print):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--scheduled", action="store_true")
    args = parser.parse_args(argv)
    try:
        ensure_safe(run, root, args.scheduled)
        prs = open_prs(run, root)
    except Unsafe as e:
        out(f"merge-prs: not now: {e}")
        return 0
    except RuntimeError as e:
        out(f"merge-prs: {e}")
        return 1
    merged = 0
    for pr in prs:
        reason = why_not(pr)
        if reason:
            out(f"merge-prs: #{pr['number']} skipped: {reason} ({pr['title']})")
            continue
        if args.dry_run:
            out(f"merge-prs: #{pr['number']} would merge at {pr['headRefOid'][:7]} ({pr['title']})")
            continue
        try:
            check(run, root, ["gh", "pr", "merge", str(pr["number"]), "--rebase",
                              "--match-head-commit", pr["headRefOid"]], f"merging #{pr['number']}")
            check(run, root, ["git", "pull", "--ff-only", "--quiet", "origin", "main"], "git pull --ff-only")
            check(run, root, [sys.executable, str(Path(root) / "scripts" / "todo-graph.py"), "validate"],
                  "todo-graph validate")
        except RuntimeError as e:
            out(f"merge-prs: stopped at #{pr['number']}: {e}; it and any later pull request are left for you")
            return 1
        merged += 1
        out(f"merge-prs: merged #{pr['number']} ({pr['title']})")
    out(f"merge-prs: {merged} merged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
