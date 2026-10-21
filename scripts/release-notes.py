#!/usr/bin/env python3
"""List the user-reported bugs a release fixes, and refuse fixes that carry no regression test (D07 T03 §4).

    python scripts/release-notes.py v1.0.0 v1.1.0 --gtest-list tests.txt     # text: issues, closing comments
    python scripts/release-notes.py v1.0.0 v1.1.0 --gtest-list tests.txt --json   # JSON (D07 T03 §7 embeds it)
    python scripts/release-notes.py v1.0.0 v1.1.0 --issues issues.json       # group by the issues' kind labels
    python scripts/release-notes.py root v1.0.0                              # the first release

A fix for a user-reported bug carries two commit trailers (docs/support/user-bugs.md):

    Fixes-User: #123
    Regression-Test: PdfXref.EmptyTable                  (a gtest Suite.Case)
    Regression-Test: tests.scripts.test_x.XTest.test_y   (a Python module.Class.test_name)

Every `Fixes-User:` commit in `<from>..<to>` must name at least one regression test, or be excused with
`--allow-no-test <sha>=<reason>` (documentation-only fixes; the excuse is printed with the entry). Each named test is
checked exactly:

- a Python id must be a test method of a `unittest.TestCase` subclass at the fix commit, as unittest's own loader
  resolves it in a temporary worktree of that commit (the module is imported; no test runs);
- a gtest id must be declared in C++ source at the fix commit, and must be in `--gtest-list`, the
  `recto-tests.exe --gtest_list_tests` output of the release build, which is the compiler's word that it is a test.
  Without `--gtest-list` a gtest id is reported as unverified.

`--issues` reads `gh issue list --state all --json number,title,labels` so issues group by their kind label.

Exit 0 when every fix carries a verified regression test, 1 when any does not (the report still prints), 2 when the
range or an argument cannot be read.
"""
import argparse
import bisect
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
CPP_FILES = ("*.cpp", "*.cc", "*.cxx", "*.h", "*.hpp")
RAW_STRING = re.compile(r'R"([^ ()\\\t\v\f\n]{0,16})\(')
# `%:` is the standard alternative spelling of `#` (C++ [lex.digraph]).
DIRECTIVE = re.compile(r"^\s*(?:#|%:)\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
GTEST_MAIN_BANNER = re.compile(r"^Running main\(\) from .+$")
PROBE_TIMEOUT = 120  # seconds for one Python test module to import in its worktree
# Run in a worktree of the fix commit: resolve module.Class.method the way unittest does, without running it. The
# verdict is a nonce read from stdin before the module is imported and printed only after every check passed, so a
# module that exits during import (even with status 0) is never read as a verified test.
PYTHON_PROBE = """
import importlib, inspect, pathlib, sys, unittest
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
# The method body too: a repository class can assign a function defined elsewhere (test_x = Other.test_y).
func = vars(owner)[method]
func = inspect.unwrap(getattr(func, "__func__", func))
code = getattr(func, "__code__", None)
if code is None or not inside(func) or not pathlib.Path(code.co_filename).resolve().is_relative_to(here):
    sys.exit(3)
# Build the case the way unittest's loader does for a TestCase method, without calling anything: cls(method) checks
# the method exists and refuses, for example, an abstract class. (loadTestsFromName would call a classmethod.)
try:
    case = cls(method)
except Exception:
    sys.exit(3)
if not (isinstance(case, cls) and isinstance(case, unittest.TestCase) and callable(case)
        and case._testMethodName == method):
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


def _in_number(text, i):
    """True when the apostrophe at i is a digit separator (1'000, 0xFF'FF): the token it sits in starts with a digit.
    After an identifier or a literal prefix (L'x', u8'x') it opens a character literal instead."""
    start = i
    while start > 0 and (text[start - 1].isalnum() or text[start - 1] in "_'."):
        start -= 1
    # A number starts with a digit, or with a dot and a digit (.1'0).
    return start < i and (text[start].isdigit() or (text[start] == "." and text[start + 1].isdigit()))


def _separator_follows(spliced_tail):
    """True when an apostrophe after this already-spliced code would be a digit separator."""
    return _in_number(spliced_tail + "'", len(spliced_tail))


def _spliced(text):
    """The text with every backslash-newline removed (translation phase 2), and for each kept character its index in
    the original text, so a raw string literal can be read back from the unspliced characters."""
    chars, origin, i, n = [], [], 0, len(text)
    while i < n:
        if text.startswith("\\\n", i):
            i += 2
            continue
        chars.append(text[i])
        origin.append(i)
        i += 1
    return "".join(chars), origin


def cpp_code(text):
    """C++ source with lines spliced, comments and literals blanked, and `#if 0` branches removed. As the compiler
    does, every splice is removed before tokens are read, and only a raw string literal's content is taken from the
    original characters (C++ reverts splices between its delimiters)."""
    # Translation phase 1: a leading byte-order mark is not source, and CRLF or a lone CR ends a line as LF does.
    original = text.removeprefix("﻿").replace("\r\n", "\n").replace("\r", "\n")
    text, origin = _spliced(original)
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            out.append(" ")
        elif (raw := RAW_STRING.match(text, i)) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")
                                                     or text[i - 1] in "8uUL"):
            # The closing `)delim"` is found in the original text, after the opening parenthesis.
            close = original.find(f'){raw.group(1)}"', origin[raw.end() - 1] + 1)
            i = n if close < 0 else bisect.bisect_left(origin, close + len(raw.group(1)) + 2)
            out.append('""')
        elif text[i] in "\"'" and not (text[i] == "'" and _separator_follows("".join(out[-64:]))):
            quote, i = text[i], i + 1
            while i < n and text[i] not in (quote, "\n"):
                i += 2 if text[i] == "\\" else 1
            i += 1
            out.append('""')
        else:
            out.append(text[i])
            i += 1
    kept, frames = [], []
    for line in "".join(out).split("\n"):
        directive = DIRECTIVE.match(line)
        if not directive:
            if all(frame["active"] for frame in frames):
                kept.append(line)
            continue
        word, condition = directive.group(1), re.sub(r"[\s()]", "", directive.group(2))
        known = word in ("if", "elif") and condition in ("0", "1")
        if word in ("if", "ifdef", "ifndef"):
            frames.append({"known": known, "active": not known or condition == "1",
                           "taken": known and condition == "1"})
        elif word == "elif" and frames:
            frame = frames[-1]
            frame["active"] = not frame["taken"] and (not known or condition == "1")
            frame["taken"] = frame["taken"] or (known and condition == "1")
            frame["known"] = frame["known"] and known
        elif word == "else" and frames:
            frame = frames[-1]
            frame["active"] = not frame["taken"] or not frame["known"]
            frame["taken"] = True
        elif word == "endif" and frames:
            frames.pop()
    return "\n".join(kept)


_CPP_SOURCES = {}


def cpp_sources(repo, sha):
    """Every C++ file at the commit as compiled code (`cpp_code`), read in one `git cat-file --batch` and cached.
    Every file is read, never a text-search shortlist: a line splice can break any identifier across lines."""
    key = (str(repo), sha)
    if key not in _CPP_SOURCES:
        names = [name for name in git(repo, "ls-tree", "-r", "-z", "--name-only", sha).stdout.split("\0")
                 if name and any(Path(name).match(pattern) for pattern in CPP_FILES)]
        request = "".join(f"{sha}:{name}\n" for name in names).encode("utf-8")
        out = subprocess.run(["git", "-C", str(repo), "cat-file", "--batch"], input=request, capture_output=True,
                             check=False).stdout
        sources, at = [], 0
        for _ in names:
            header_end = out.index(b"\n", at)
            header = out[at:header_end].split()
            at = header_end + 1
            if len(header) == 3 and header[1] == b"blob":
                size = int(header[2])
                sources.append(cpp_code(out[at:at + size].decode("utf-8", errors="replace")))
                at += size + 1
        _CPP_SOURCES[key] = sources
    return _CPP_SOURCES[key]


def gtest_declared(repo, sha, test_id):
    """True when TEST, TEST_F, TEST_P, TYPED_TEST or TYPED_TEST_P declares Suite.Case in C++ source at the commit
    (comments, literals and `#if 0` branches excluded). Whether it compiles into a test is `--gtest-list`'s job."""
    suite, case = GTEST_ID.match(test_id).groups()
    declaration = re.compile(rf"\b(?:TEST(?:_F|_P)?|TYPED_TEST(?:_P)?)\s*\(\s*{suite}\s*,\s*{case}\s*\)")
    return any(declaration.search(source) for source in cpp_sources(repo, sha))


def read_gtest_list(path):
    """`Suite.Case` names from `--gtest_list_tests` output. A typed suite (gtest marks it `# TypeParam = ...`) ends in
    its type name or index, which is dropped: `Suite/0.` or `Suite/int.` (TYPED_TEST) and `Prefix/Suite/0.`
    (TYPED_TEST_P) are `Suite`. Otherwise the suite is the last part: `Prefix/Suite.` (TEST_P) is `Suite`. A
    parameterised case `Case/0` is `Case`."""
    try:
        text = Path(path).read_text(encoding="utf-8-sig")
    except (OSError, UnicodeDecodeError) as exc:
        raise Unreadable(f"--gtest-list {path}: {exc}") from None
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
                raise Unreadable(f"--gtest-list {path}: {line!r} is not --gtest_list_tests output")
            parts = entry[:-1].split("/")
            if comment.strip().startswith("TypeParam") and len(parts) > 1:
                parts = parts[:-1]
            suite = parts[-1]
        elif suite is None:
            raise Unreadable(f"--gtest-list {path}: a test case comes before any suite")
        else:
            names.add(f"{suite}.{entry.strip().split('/')[0]}")
    if not names:
        raise Unreadable(f"--gtest-list {path}: no tests listed")
    return names


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


def test_problem(repo, python_tests, gtest_list, commit, test_id):
    """Why a named regression test does not count, or None when it does."""
    if GTEST_ID.match(test_id):
        if not gtest_declared(repo, commit["sha"], test_id):
            return f"Regression-Test {test_id} is not declared at {commit['short']}"
        if gtest_list is None:
            return f"Regression-Test {test_id} is unverified: pass --gtest-list from the release build"
        if test_id not in gtest_list:
            return f"Regression-Test {test_id} is not in the release build's test list"
        return None
    if PYTHON_ID.match(test_id):
        if not python_tests.exists(commit["sha"], test_id):
            return f"Regression-Test {test_id} is not a unittest test at {commit['short']}"
        return None
    return f"Regression-Test {test_id!r} is neither Suite.Case nor module.Class.test_name"


def report(repo, start, end, issue_labels=None, allowances=None, gtest_list=None, repo_url=REPO_URL):
    """The release's user fixes, issues and closing comments, and every fix that breaks the regression-test rule."""
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
                problem = test_problem(repo, python_tests, gtest_list, commit, test_id)
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
    parser.add_argument("--gtest-list", help="recto-tests.exe --gtest_list_tests output from the release build")
    parser.add_argument("--allow-no-test", action="append", default=[], metavar="SHA=REASON",
                        help="excuse one documentation-only fix from the regression-test rule")
    parser.add_argument("--repo-url", default=REPO_URL, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        allowances = parse_allowances(args.allow_no_test)
        labels = read_issues(args.issues) if args.issues else None
        gtest_list = read_gtest_list(args.gtest_list) if args.gtest_list else None
        result = report(repo, args.start, args.end, labels, allowances, gtest_list, args.repo_url)
    except Unreadable as exc:
        print(f"release-notes: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2) if args.json else render(result))
    return 1 if result["violations"] else 0


if __name__ == "__main__":
    sys.exit(main())
