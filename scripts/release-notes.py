#!/usr/bin/env python3
"""List the user-reported bugs a release fixes, and refuse fixes that carry no regression test (D07 T03 §4).

    python scripts/release-notes.py v1.0.0 v1.1.0                      # text: issues by label, closing comments
    python scripts/release-notes.py v1.0.0 v1.1.0 --json               # the same as JSON (D07 T03 §7 embeds it)
    python scripts/release-notes.py v1.0.0 v1.1.0 --issues issues.json # group by the issues' kind labels
    python scripts/release-notes.py root v1.0.0                        # the first release: every commit up to it

A fix for a user-reported bug carries two commit trailers (docs/support/user-bugs.md):

    Fixes-User: #123
    Regression-Test: PdfXref.EmptyTable            (a gtest Suite.Case)
    Regression-Test: tests.scripts.test_x.XTest.test_y   (a Python module.Class.test_name)

Every `Fixes-User:` commit in `<from>..<to>` must name at least one regression test that exists at that commit, or be
excused with `--allow-no-test <sha>=<reason>` (documentation-only fixes); the excuse is printed with the entry.
`--issues` reads `gh issue list --state all --json number,title,labels` so issues group by their kind label.

Exit 0 when every fix carries an existing regression test, 1 when any does not (the report still prints), 2 when the
range or an argument cannot be read.
"""
import argparse
import ast
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REPO_URL = "https://github.com/rizonesoft/Recto"
# The kind label an issue is grouped under: the most severe one it carries (docs/support/triage.md).
GROUPS = ("security", "data-loss", "crash", "regression", "bug")
UNLABELLED = "unlabelled"
ISSUE = re.compile(r"#([0-9]+)\b")
GTEST_ID = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$")
PYTHON_ID = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*){2,}$")
TRAILER = re.compile(r"^([A-Za-z0-9-]+):\s*(.*)$")
# Field and record separators for `git log`: control characters a commit subject or trailer cannot hold.
FIELD, RECORD = "\x1f", "\x1e"


class Unreadable(Exception):
    """The range, the repository or an argument cannot be read."""


def git(repo, *args, check=True):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if check and result.returncode != 0:
        raise Unreadable(f"git {' '.join(args)}: {result.stderr.strip() or 'failed'}")
    return result


def commits(repo, start, end):
    """Each commit in start..end (oldest first) with its subject and trailers; start `root` means from the first."""
    revision = end if start == "root" else f"{start}..{end}"
    for ref in ([end] if start == "root" else [start, end]):
        git(repo, "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}")
    out = git(repo, "log", "--reverse", f"--format=%H{FIELD}%h{FIELD}%s{FIELD}%(trailers:unfold,only){RECORD}",
              revision).stdout
    found = []
    for record in out.split(RECORD):
        record = record.strip("\n")
        if not record:
            continue
        sha, short, subject, trailers = record.split(FIELD)
        pairs = [TRAILER.match(line) for line in trailers.splitlines() if line.strip()]
        found.append({"sha": sha, "short": short, "subject": subject,
                      "trailers": [(m.group(1).lower(), m.group(2).strip()) for m in pairs if m]})
    return found


def gtest_exists(repo, sha, test_id):
    suite, case = GTEST_ID.match(test_id).groups()
    pattern = rf"TEST(_F|_P)?\(\s*{suite}\s*,\s*{case}\s*\)"
    result = git(repo, "grep", "-q", "-E", pattern, sha, "--", "*.cpp", "*.cc", "*.h", "*.hpp", check=False)
    return result.returncode == 0


def python_test_exists(repo, sha, test_id):
    """True when module.Class.test_name names a method of a class in that module's file at the commit."""
    parts = test_id.split(".")
    module, cls, method = parts[:-2], parts[-2], parts[-1]
    shown = git(repo, "show", f"{sha}:{'/'.join(module)}.py", check=False)
    if shown.returncode != 0:
        return False
    try:
        tree = ast.parse(shown.stdout)
    except SyntaxError:
        return False
    return any(isinstance(node, ast.ClassDef) and node.name == cls and any(
        isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef)) and item.name == method for item in node.body)
        for node in tree.body)


def test_exists(repo, sha, test_id):
    if GTEST_ID.match(test_id):
        return gtest_exists(repo, sha, test_id)
    if PYTHON_ID.match(test_id):
        return python_test_exists(repo, sha, test_id)
    return False


def read_issues(path):
    """Issue number to its label names, from a `gh issue list --json number,title,labels` export."""
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise Unreadable(f"--issues {path}: {exc}") from None
    if not isinstance(data, list):
        raise Unreadable(f"--issues {path}: expected the list `gh issue list --json` writes")
    labels = {}
    for issue in data:
        if not isinstance(issue, dict) or not isinstance(issue.get("number"), int):
            raise Unreadable(f"--issues {path}: an entry has no integer number")
        labels[issue["number"]] = [label.get("name") for label in issue.get("labels") or []
                                   if isinstance(label, dict) and isinstance(label.get("name"), str)]
    return labels


def parse_allowances(values):
    allowed = {}
    for value in values:
        sha, sep, reason = value.partition("=")
        if not sep or not re.fullmatch(r"[0-9a-f]{7,40}", sha) or not reason.strip():
            raise Unreadable(f"--allow-no-test takes <sha>=<reason>, not {value!r}")
        allowed[sha] = reason.strip()
    return allowed


def report(repo, start, end, issue_labels=None, allowances=None, repo_url=REPO_URL):
    """The release's user fixes, issues and closing comments, and every fix that breaks the regression-test rule."""
    allowances = dict(allowances or {})
    used = set()
    release_url = f"{repo_url}/releases/tag/{end}"
    fixes, violations, by_issue = [], [], {}
    for commit in commits(repo, start, end):
        fixes_user = [value for key, value in commit["trailers"] if key == "fixes-user"]
        if not fixes_user:
            continue
        numbers = sorted({int(n) for value in fixes_user for n in ISSUE.findall(value)})
        tests = [value for key, value in commit["trailers"] if key == "regression-test"]
        excuse = next((reason for sha, reason in allowances.items() if commit["sha"].startswith(sha)), None)
        if excuse is not None:
            used.update(sha for sha in allowances if commit["sha"].startswith(sha))
        where = f"{commit['short']} {commit['subject']}"
        if not numbers:
            violations.append(f"{where}: Fixes-User names no issue number ({'; '.join(fixes_user)})")
        if excuse is None:
            if not tests:
                violations.append(f"{where}: Fixes-User without a Regression-Test trailer")
            for test_id in tests:
                if not (GTEST_ID.match(test_id) or PYTHON_ID.match(test_id)):
                    violations.append(f"{where}: Regression-Test {test_id!r} is neither Suite.Case nor "
                                      f"module.Class.test_name")
                elif not test_exists(repo, commit["sha"], test_id):
                    violations.append(f"{where}: Regression-Test {test_id} does not exist at {commit['short']}")
        fix = {"sha": commit["sha"], "short": commit["short"], "subject": commit["subject"], "issues": numbers,
               "tests": tests, "allowed_no_test": excuse}
        fixes.append(fix)
        for number in numbers:
            by_issue.setdefault(number, []).append(fix)
    for sha in sorted(set(allowances) - used):
        violations.append(f"--allow-no-test {sha}: no Fixes-User commit in {start}..{end} matches it")
    issues = []
    for number, issue_fixes in sorted(by_issue.items()):
        labels = (issue_labels or {}).get(number, [])
        group = next((name for name in GROUPS if name in labels), UNLABELLED)
        shorts = ", ".join(fix["short"] for fix in issue_fixes)
        issues.append({"number": number, "group": group, "labels": labels,
                       "commits": [fix["sha"] for fix in issue_fixes],
                       "comment": f"Fixed in {end} ({shorts}, {release_url})"})
    return {"from": start, "to": end, "release_url": release_url, "fixes": fixes, "issues": issues,
            "violations": violations}


def render(result):
    lines = [f"User-reported fixes in {result['to']} ({result['from']}..{result['to']})"]
    for group in (*GROUPS, UNLABELLED):
        members = [issue for issue in result["issues"] if issue["group"] == group]
        if not members:
            continue
        lines += ["", f"{group}:"]
        for issue in members:
            for fix in (f for f in result["fixes"] if f["sha"] in issue["commits"]):
                proof = ", ".join(fix["tests"]) or f"no test: {fix['allowed_no_test']}"
                lines.append(f"  #{issue['number']}  {fix['short']}  {fix['subject']}  ({proof})")
    if not result["issues"]:
        lines.append("  none")
    lines += ["", "Closing comments:"]
    lines += [f"  #{issue['number']}: {issue['comment']}" for issue in result["issues"]] or ["  none"]
    if result["violations"]:
        lines += ["", "Violations:"] + [f"  {violation}" for violation in result["violations"]]
    return "\n".join(lines)


def main(argv=None, repo=ROOT):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("start", metavar="from-tag", help="the previous release tag, or root for the first release")
    parser.add_argument("end", metavar="to-tag", help="the release tag being published")
    parser.add_argument("--json", action="store_true", help="print JSON instead of text")
    parser.add_argument("--issues", help="gh issue list --state all --json number,title,labels output")
    parser.add_argument("--allow-no-test", action="append", default=[], metavar="SHA=REASON",
                        help="excuse one documentation-only fix from the regression-test rule")
    parser.add_argument("--repo-url", default=REPO_URL, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        allowances = parse_allowances(args.allow_no_test)
        labels = read_issues(args.issues) if args.issues else None
        result = report(repo, args.start, args.end, labels, allowances, args.repo_url)
    except Unreadable as exc:
        print(f"release-notes: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2) if args.json else render(result))
    return 1 if result["violations"] else 0


if __name__ == "__main__":
    sys.exit(main())
