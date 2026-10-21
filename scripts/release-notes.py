#!/usr/bin/env python3
"""List the user-reported bugs a release fixes, and refuse fixes that carry no regression test (D07 T03 §4).

    python scripts/release-notes.py v1.0.0 v1.1.0                      # text: issues, closing comments
    python scripts/release-notes.py v1.0.0 v1.1.0 --json               # the same as JSON (D07 T03 §7 embeds it)
    python scripts/release-notes.py v1.0.0 v1.1.0 --issues issues.json # group by the issues' kind labels
    python scripts/release-notes.py root v1.0.0                        # the first release

A fix for a user-reported bug carries two commit trailers (docs/support/user-bugs.md):

    Fixes-User: #123
    Regression-Test: PdfXref.EmptyTable                  (a gtest Suite.Case)
    Regression-Test: tests.scripts.test_x.XTest.test_y   (a Python module.Class.test_name)

Every `Fixes-User:` commit in `<from>..<to>` must name at least one regression test, or be excused with
`--allow-no-test <sha>=<reason>` (documentation-only fixes; the excuse is printed with the entry). Each named test is
checked exactly:

- a Python id must be a test method of a `unittest.TestCase` subclass at the fix commit, as unittest's own loader
  resolves it in a temporary worktree of that commit (the module is imported; no test runs);
- a gtest id must be in the fix commit's own test list: CI records every `main` build's
  `recto-tests.exe --gtest_list_tests` as a git note on the commit (`refs/notes/gtest-list`, D07 T03 §9), the
  compiler's word for what was a test at that commit. A fix commit with no note reports its gtest ids unverified.

`--issues` reads `gh issue list --state all --json number,title,labels` so issues group by their kind label.

Exit 0 when every fix carries a verified regression test, 1 when any does not (the report still prints), 2 when the
range or an argument cannot be read.
"""
import argparse
import json
from pathlib import Path
import re
import secrets
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REPO_URL = "https://github.com/rizonesoft/Recto"
# The kind label an issue is grouped under: the most severe one it carries (docs/support/triage.md).
GROUPS = ("security", "data-loss", "crash", "regression", "bug")
UNLABELLED = "unlabelled"
# GitHub issue numbers fit in ten digits; a longer run of digits is not an issue number (and never reaches int()).
ISSUE = re.compile(r"#([0-9]{1,10})\b")
GTEST_ID = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$")
PYTHON_ID = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*){2,}$")
TRAILER = re.compile(r"^([A-Za-z0-9-]+):\s*(.*)$", re.DOTALL)
# Where CI records each main build's `--gtest_list_tests` output, one note per commit (D07 T03 §9).
NOTES_REF = "refs/notes/gtest-list"
GTEST_MAIN_BANNER = re.compile(r"^Running main\(\) from .+$")
PROBE_TIMEOUT = 120  # seconds for one Python test module to import in its worktree
# Run in a worktree of the fix commit: resolve module.Class.method the way unittest does, without running it. The
# verdict is a nonce read from stdin before the module is imported and printed only after every check passed, so a
# module that exits during import (even with status 0) is never read as a verified test.
PYTHON_PROBE = """
import importlib, inspect, pathlib, sys, types, unittest
nonce = sys.stdin.readline().strip()
sys.stdin.close()
here = pathlib.Path.cwd().resolve()
sys.path.insert(0, str(here))
name = sys.argv[1]
*module, cls_name, method = name.split(".")
mod = importlib.import_module(".".join(module))
cls = getattr(mod, cls_name, None)
if not (isinstance(cls, type) and issubclass(cls, unittest.TestCase)
        and method in unittest.defaultTestLoader.getTestCaseNames(cls)):
    sys.exit(3)
def inside(obj):
    try:
        return pathlib.Path(inspect.getfile(obj)).resolve().is_relative_to(here)
    except (TypeError, OSError):
        return False
# The module, the class and the class that defines the method must all come from the fix commit's tree, not from
# the standard library or an installed package.
owner = next((klass for klass in cls.__mro__ if method in vars(klass)), None)
if not (inside(mod) and inside(cls) and owner is not None and inside(owner)):
    sys.exit(3)
# A regression test is a plain instance method: for one, unittest's loader builds cls(method), which is what this
# probe does below. A classmethod, staticmethod or other descriptor takes the loader's callable path instead, which
# calls it, so it is refused here without being run.
if not isinstance(vars(owner)[method], types.FunctionType):
    sys.exit(3)
# The method body too, layer by layer from the one unittest runs: a repository class can assign a function defined
# elsewhere (test_x = Other.test_y), or wrap its test in a decorator from outside the tree that never calls it.
layer, seen = vars(owner)[method], set()
while layer is not None and id(layer) not in seen:
    seen.add(id(layer))
    code = getattr(layer, "__code__", None)
    if (not isinstance(layer, types.FunctionType) or code is None or not inside(layer)
            or not pathlib.Path(code.co_filename).resolve().is_relative_to(here)):
        sys.exit(3)
    layer = getattr(layer, "__wrapped__", None)
# Build the case the way unittest's loader does for a plain TestCase method, without calling anything: cls(method)
# checks the method exists and refuses, for example, an abstract class.
try:
    case = cls(method)
except Exception:
    sys.exit(3)
if not (isinstance(case, cls) and isinstance(case, unittest.TestCase) and callable(case)
        and case._testMethodName == method):
    sys.exit(3)
# unittest's loadTestsFromName then looks the name up on the instance, and the case runs whatever that returns. It
# must be the verified method itself, bound to this case: anything else (a function, a built-in method or None set
# in __init__) either sends the loader down its callable path or runs code other than the fix commit's test.
bound = getattr(case, method, None)
if not (isinstance(bound, types.MethodType) and bound.__self__ is case and bound.__func__ is vars(owner)[method]):
    sys.exit(3)
print(nonce)
"""


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
    found = []
    for sha in git(repo, "rev-list", "--reverse", revision).stdout.split():
        # NUL separates the fields and the trailers: git cannot store NUL in a commit message, so no subject or
        # trailer value can split or merge a field (a vertical tab or U+001E stays inside its value).
        out = git(repo, "show", "-s", "--format=%h%x00%s%x00%(trailers:unfold,only,separator=%x00)", sha).stdout
        short, subject, *trailers = out.rstrip("\n").split("\x00")
        pairs = [TRAILER.match(trailer) for trailer in trailers if trailer.strip()]
        found.append({"sha": sha, "short": short, "subject": subject,
                      "trailers": [(m.group(1).lower(), m.group(2).strip()) for m in pairs if m]})
    return found


def parse_gtest_list(text, where):
    """`Suite.Case` names from `--gtest_list_tests` output. A typed suite (gtest marks it `# TypeParam = ...`) ends in
    its type name or index, which is dropped: `Suite/0.` or `Suite/int.` (TYPED_TEST) and `Prefix/Suite/0.`
    (TYPED_TEST_P) are `Suite`. Otherwise the suite is the last part: `Prefix/Suite.` (TEST_P) is `Suite`. A
    parameterised case `Case/0` is `Case`. `where` names the source in a refusal."""
    names, suite = set(), None
    for line in text.splitlines():
        entry, _, comment = line.partition("#")
        entry = entry.rstrip()
        if not entry:
            continue
        if suite is None and GTEST_MAIN_BANNER.match(entry):
            continue  # gtest_main.cc prints this before the list
        if not entry[0].isspace():
            if not entry.endswith("."):
                raise Unreadable(f"{where}: {line!r} is not --gtest_list_tests output")
            parts = entry[:-1].split("/")
            if comment.strip().startswith("TypeParam") and len(parts) > 1:
                parts = parts[:-1]
            suite = parts[-1]
        elif suite is None:
            raise Unreadable(f"{where}: a test case comes before any suite")
        else:
            names.add(f"{suite}.{entry.strip().split('/')[0]}")
    if not names:
        raise Unreadable(f"{where}: no tests listed")
    return names


class GtestNotes:
    """The test list CI recorded for a commit, from `refs/notes/gtest-list` (fetched from origin once when asked)."""

    def __init__(self, repo, fetch):
        """With `fetch`, only CI's notes count: the ref is fetched from origin and replaces any local one; when origin
        has no notes ref yet, no commit has a list; when origin cannot be read, the check stops (Unreadable). Without
        `fetch` (tests), the local ref is read as is."""
        self.repo, self.lists, self.none = repo, {}, False
        if fetch:
            present = git(repo, "ls-remote", "--exit-code", "origin", NOTES_REF, check=False).returncode
            if present == 0:
                git(repo, "fetch", "--quiet", "origin", f"+{NOTES_REF}:{NOTES_REF}")
            elif present == 2:
                self.none = True
            else:
                raise Unreadable(f"cannot read {NOTES_REF} from origin (git ls-remote exit {present})")

    def tests_at(self, sha):
        """The `Suite.Case` names in the commit's note, None when the commit has no note."""
        if self.none:
            return None
        if sha not in self.lists:
            note = git(self.repo, "notes", f"--ref={NOTES_REF}", "show", sha, check=False)
            if note.returncode != 0:
                self.lists[sha] = None
            else:
                try:
                    self.lists[sha] = parse_gtest_list(note.stdout, f"{NOTES_REF} note on {sha[:7]}")
                except Unreadable:
                    self.lists[sha] = set()
        return self.lists[sha]


class PythonTests:
    """Answers whether module.Class.test_name is a unittest test at a commit, one worktree per commit."""

    def __init__(self, repo):
        self.repo, self.answers, self.tmp = repo, {}, None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self.tmp is not None:
            for path in Path(self.tmp.name).iterdir():
                git(self.repo, "worktree", "remove", "--force", str(path), check=False)
            git(self.repo, "worktree", "prune", check=False)
            self.tmp.cleanup()

    def exists(self, sha, test_id):
        if (sha, test_id) not in self.answers:
            if self.tmp is None:
                self.tmp = tempfile.TemporaryDirectory(prefix="release-notes-")
            tree = Path(self.tmp.name) / sha[:12]
            if not tree.exists():
                git(self.repo, "worktree", "add", "--detach", "--quiet", str(tree), sha)
            nonce = secrets.token_hex(16)
            try:
                # Bytes, not text: an imported module may print anything, and only the ASCII nonce is compared.
                probe = subprocess.run([sys.executable, "-I", "-c", PYTHON_PROBE, test_id], cwd=tree,
                                       input=(nonce + "\n").encode("ascii"), capture_output=True,
                                       timeout=PROBE_TIMEOUT)
            except subprocess.TimeoutExpired:
                # An import that never finishes proves nothing: the test is unverified, and the report still prints.
                self.answers[(sha, test_id)] = False
                return False
            lines = probe.stdout.strip().splitlines()
            self.answers[(sha, test_id)] = (probe.returncode == 0 and bool(lines)
                                            and lines[-1] == nonce.encode("ascii"))
        return self.answers[(sha, test_id)]


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
        if issue.get("labels") is not None and not isinstance(issue["labels"], list):
            raise Unreadable(f"--issues {path}: issue #{issue['number']} has labels that are not a list")
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


def test_problem(python_tests, gtest_notes, commit, test_id):
    """Why a named regression test does not count, or None when it does."""
    if GTEST_ID.match(test_id):
        listed = gtest_notes.tests_at(commit["sha"])
        if listed is None:
            return (f"Regression-Test {test_id} is unverified: {commit['short']} has no CI test list ({NOTES_REF}); "
                    f"record one with `gh workflow run build.yml --ref main -f sha={commit['sha']}`")
        if test_id not in listed:
            return f"Regression-Test {test_id} is not in {commit['short']}'s CI test list"
        return None
    if PYTHON_ID.match(test_id):
        if not python_tests.exists(commit["sha"], test_id):
            return f"Regression-Test {test_id} is not a unittest test at {commit['short']}"
        return None
    return f"Regression-Test {test_id!r} is neither Suite.Case nor module.Class.test_name"


def report(repo, start, end, issue_labels=None, allowances=None, repo_url=REPO_URL, fetch_notes=False):
    """The release's user fixes, issues and closing comments, and every fix that breaks the regression-test rule."""
    gtest_notes = GtestNotes(repo, fetch_notes)
    allowances = dict(allowances or {})
    used = set()
    release_url = f"{repo_url}/releases/tag/{end}"
    fixes, violations, by_issue = [], [], {}
    with PythonTests(repo) as python_tests:
        for commit in commits(repo, start, end):
            fixes_user = [value for key, value in commit["trailers"] if key == "fixes-user"]
            if not fixes_user:
                continue
            numbers = sorted({int(n) for value in fixes_user for n in ISSUE.findall(value)})
            tests = [value for key, value in commit["trailers"] if key == "regression-test"]
            matched = {sha for sha in allowances if commit["sha"].startswith(sha)}
            used |= matched
            excuse = allowances[min(matched)] if matched else None
            where = f"{commit['short']} {commit['subject']}"
            if not numbers:
                violations.append(f"{where}: Fixes-User names no issue number ({'; '.join(fixes_user)})")
            # An allowance excuses only a missing Regression-Test trailer; a named test is always checked.
            if not tests and excuse is None:
                violations.append(f"{where}: Fixes-User without a Regression-Test trailer")
            if tests and excuse is not None:
                violations.append(f"{where}: --allow-no-test excuses a commit that names Regression-Test; drop one")
            for test_id in tests:
                problem = test_problem(python_tests, gtest_notes, commit, test_id)
                if problem:
                    violations.append(f"{where}: {problem}")
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
                proof = ", ".join(fix["tests"]) or "no test"
                if fix["allowed_no_test"] is not None:
                    proof += f"; excused: {fix['allowed_no_test']}"
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
        result = report(repo, args.start, args.end, labels, allowances, args.repo_url, fetch_notes=True)
    except Unreadable as exc:
        print(f"release-notes: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2) if args.json else render(result))
    return 1 if result["violations"] else 0


if __name__ == "__main__":
    sys.exit(main())
