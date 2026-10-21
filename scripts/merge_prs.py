#!/usr/bin/env python3
"""Merge dependency pull requests (Dependabot and the pins workflow) when it is safe, one at a time.

    python scripts/merge_prs.py              # merge what is eligible now
    python scripts/merge_prs.py --dry-run    # say what would merge, change nothing
    python scripts/merge_prs.py --scheduled  # the recto-pr-merge task: stand aside while a campaign runs

Safe to run: the working tree is clean, HEAD is origin/main after a fetch (nothing waits to be pushed or pulled), no
TODO section is in progress (open, started and ready), and, when scheduled, no campaign run guard is held.

Eligible: an open pull request into main, opened by Dependabot or the pins workflow, labelled `dependencies` or `pins`,
mergeable, its head commit's checks all finished and green with every REQUIRED_CHECKS job succeeded, and its branch not on
EXCLUDED_BRANCHES (pins that are section work, merged by that section instead). Each is re-read right before it is
judged. The scheduled run holds the campaign guard's lock for its whole window.

Each merge: `gh pr merge <n> --rebase --match-head-commit <sha>` (linear history, and only the commit that was
checked), then `git pull --ff-only`, then `todo-graph.py validate`. The first failure stops the run, saying whether that
pull request is still open (the merge failed) or merged (the local follow-up failed); later ones wait for the operator.

Exit 0: merged, or nothing eligible, or not safe right now (said why). Exit 1: a merge or its follow-up failed.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import todo_graph  # noqa: E402

sys.path.insert(0, str(ROOT / "scripts"))
import campaign_guard  # noqa: E402

AUTHORS = {"app/dependabot", "dependabot[bot]", "app/github-actions", "github-actions[bot]"}
LABELS = {"dependencies", "pins"}
# Branch prefixes of pin pull requests that belong to TODO section work and are merged there, never here. Recto has
# none today: the Windows SDK line moves through an issue and the operator's decision, not a pull request.
EXCLUDED_BRANCHES: tuple[str, ...] = ()
GREEN = {"success", "neutral", "skipped"}
# The jobs that prove a change builds and keeps the plan valid. They must have run on the pull request's head commit:
# the pins workflow dispatches them there, and GitHub does not list dispatched runs among a pull request's checks
# (whose own pull_request runs wait for approval), so checks are read from the commit, not the pull request.
REQUIRED_CHECKS = {"plan-gates", "Build x64 & ARM64 and run recto-tests",
                   "Build x64 Release with AddressSanitizer and run recto-tests"}
GUARD = Path("build") / "claude-campaign-guard.json"


class Unsafe(Exception):
    """Not safe to merge right now; nothing was changed."""


def run_cmd(args, root):
    return subprocess.run(args, cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=600)


def check(run, root, args, what):
    try:
        result = run(args, root)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise RuntimeError(f"{what} did not finish: {e}") from None
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
    if scheduled and (Path(root) / GUARD).exists():  # read under the guard lock (main), so no campaign starts after it
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


def origin_repo(run, root):
    """owner/name of the GitHub repository behind origin: every gh call names it, so an inherited GH_REPO or another
    default can never point the merges at a repository other than the one this clone checked."""
    url = check(run, root, ["git", "remote", "get-url", "origin"], "git remote get-url origin").strip()
    m = re.fullmatch(r"(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([\w.-]+/[\w.-]+?)(?:\.git)?/?", url)
    if m is None:
        raise RuntimeError(f"origin {url!r} is not a GitHub repository")
    return m.group(1)


def check_runs(run, root, sha):
    """Every check run on a commit: (name, status, conclusion)."""
    out = check(run, root, ["gh", "api", "--paginate", f"repos/{REPO[0]}/commits/{sha}/check-runs?per_page=100",
                            "--jq", ".check_runs[] | [.name, .status, .conclusion]"], f"reading checks of {sha[:7]}")
    return [tuple(json.loads(line)) for line in out.splitlines() if line.strip()]


def commit_statuses(run, root, sha):
    """Every legacy commit status on a commit: (context, state). CI can report through these as well as check runs."""
    # Every page: the endpoint returns 30 statuses a page by default, and a failing one may sit past the first.
    out = check(run, root, ["gh", "api", "--paginate", f"repos/{REPO[0]}/commits/{sha}/status?per_page=100",
                            "--jq", ".statuses[] | [.context, .state]"], f"reading statuses of {sha[:7]}")
    return [tuple(json.loads(line)) for line in out.splitlines() if line.strip()]


def checks_green(runs):
    """Every check run on the commit completed green, and each required job succeeded (no checks never passes)."""
    # The required jobs must have actually run and passed (a skipped build proves nothing); any other check may also be
    # neutral or skipped, but every check must have finished.
    if not REQUIRED_CHECKS <= {name for name, _, conclusion in runs if conclusion == "success"}:
        return False
    return all(status == "completed" and conclusion in GREEN for _, status, conclusion in runs)


def why_not(pr, runs, statuses=()):
    """Why a pull request is not eligible, or None when it is. runs and statuses: the check runs and the legacy commit
    statuses on its head commit."""
    if (pr.get("author") or {}).get("login") not in AUTHORS:
        return "not from Dependabot or the pins workflow"
    if not LABELS & {label["name"] for label in pr.get("labels") or []}:
        return "not labelled dependencies or pins"
    if pr.get("state") != "OPEN" or pr.get("baseRefName") != "main":
        return "not an open pull request into main"
    if EXCLUDED_BRANCHES and pr.get("headRefName", "").startswith(EXCLUDED_BRANCHES):
        return "a pin that is section work"
    if pr.get("mergeable") != "MERGEABLE":
        return f"GitHub says {pr.get('mergeable')}"
    if not checks_green(runs):
        return "its head commit's checks are not all green, or a required job did not run"
    if any(state != "success" for _, state in statuses):
        return "a commit status on its head commit is not success"
    return None


FIELDS = "number,title,author,labels,baseRefName,headRefName,headRefOid,mergeable,state"
REPO: list[str] = [""]  # owner/name, set from origin by merge_all before any gh call


SETTLE_TRIES, SETTLE_WAIT = 5, 3.0


def current(run, root, number):
    """A pull request as it is now. GitHub computes mergeability in the background and answers UNKNOWN meanwhile, so an
    UNKNOWN is asked again a few times; one still UNKNOWN is skipped this run."""
    for attempt in range(SETTLE_TRIES):
        pr = json.loads(check(run, root, ["gh", "pr", "view", str(number), "--repo", REPO[0], "--json", FIELDS],
                              f"gh pr view {number}"))
        if pr.get("mergeable") != "UNKNOWN" or attempt == SETTLE_TRIES - 1:
            return pr
        time.sleep(SETTLE_WAIT)


def open_prs(run, root):
    """Open pull requests into main, oldest first; each is re-read just before it is judged."""
    out = check(run, root, ["gh", "pr", "list", "--repo", REPO[0], "--state", "open", "--base", "main", "--limit",
                            "100", "--json", "number"], "gh pr list")
    return sorted(pr["number"] for pr in json.loads(out))


def merge_all(run, root, args, out):
    try:
        ensure_safe(run, root, args.scheduled)
        REPO[0] = origin_repo(run, root)
        numbers = open_prs(run, root)
    except Unsafe as e:
        out(f"merge-prs: not now: {e}")
        return 0
    except RuntimeError as e:
        out(f"merge-prs: {e}")
        return 1
    merged = 0
    for index, number in enumerate(numbers):
        if index:
            # Safe at the start is not safe for the whole batch: the operator may have started editing or a section
            # since the last merge, so each merge checks again.
            try:
                ensure_safe(run, root, args.scheduled)
            except Unsafe as e:
                out(f"merge-prs: stopped after {merged} merged: {e}; any later pull request waits")
                return 0
            except RuntimeError as e:
                out(f"merge-prs: {e}")
                return 1
        try:
            # Judged on its state right now, after any earlier merge: labels, base, mergeability and the checks of the
            # exact commit that will merge.
            pr = current(run, root, number)
            reason = why_not(pr, check_runs(run, root, pr["headRefOid"]), commit_statuses(run, root, pr["headRefOid"]))
        except RuntimeError as e:
            out(f"merge-prs: {e}")
            return 1
        if reason:
            out(f"merge-prs: #{number} skipped: {reason} ({pr['title']})")
            continue
        if args.dry_run:
            out(f"merge-prs: #{number} would merge at {pr['headRefOid'][:7]} ({pr['title']})")
            continue
        try:
            check(run, root, ["gh", "pr", "merge", str(number), "--repo", REPO[0], "--rebase", "--match-head-commit",
                              pr["headRefOid"]], f"merging #{number}")
        except RuntimeError as e:
            # A failed or timed-out merge may still have landed: say what GitHub reports, or that it is unknown.
            try:
                state = current(run, root, number).get("state", "UNKNOWN")
            except RuntimeError:
                state = "unknown (GitHub could not be read)"
            out(f"merge-prs: stopped at #{number}, whose state is now {state}: {e}; it and any later pull request "
                f"are left for you")
            return 1
        merged += 1
        out(f"merge-prs: merged #{number} ({pr['title']})")
        try:
            check(run, root, ["git", "pull", "--ff-only", "--quiet", "origin", "main"], "git pull --ff-only")
            check(run, root, [sys.executable, str(Path(root) / "scripts" / "todo-graph.py"), "validate"],
                  "todo-graph validate")
        except RuntimeError as e:
            out(f"merge-prs: #{number} merged on GitHub, but the local follow-up failed: {e}; stopped, any later pull "
                f"request is left for you")
            return 1
    out(f"merge-prs: {merged} merged")
    return 0


def main(argv=None, run=run_cmd, root=ROOT, out=print):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--scheduled", action="store_true")
    args = parser.parse_args(argv)
    if not args.scheduled:
        return merge_all(run, root, args, out)
    # The scheduled run holds the campaign guard's lock for its whole window: a campaign's acquire waits for it, so no
    # run can start between the guard check and the last merge.
    try:
        with campaign_guard._Lock(str(root)):
            return merge_all(run, root, args, out)
    except campaign_guard.GuardError as e:
        out(f"merge-prs: not now: {e}")
        return 0

if __name__ == "__main__":
    sys.exit(main())
