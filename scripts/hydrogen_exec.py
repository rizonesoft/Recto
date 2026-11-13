#!/usr/bin/env python3
"""Execution-layer read-only sandbox and strict command allowlist for Gemini 4 Hydrogen helpers.

`scripts/dispatch_hook.py` (wired on `PreToolUse` with `matcher: "*"`) validates every Hydrogen `run_command` against
`parse_hydrogen_command` and rewrites `CommandLine` via Antigravity's `overwrite` field to:

    & python "<repo>/scripts/hydrogen_exec.py" --payload <hex-encoded-utf8-json-argv>

At execution time this module:
1. Decodes `<hex>` and re-verifies `validate_hydrogen_argv(tokens)` before running anything (defense in depth).
2. Executes the child binary directly with `shell=False` (`CreateProcess` / `execve`), so PowerShell and POSIX shells
   never evaluate expressions, `[System.IO.File]::WriteAllText(...)`, subshells, pipes or redirections.
3. Runs `git` with `--no-optional-locks -c core.fsmonitor=false -c diff.external=` so even `.git/index` is untouched.
4. Stages tracked + non-ignored untracked files into an isolated temporary repository outside the live checkout
   (sharing `.git/objects` read-only via `objects/info/alternates`) before running Python/GTest suites or scripts, so
   no helper execution can modify the writer's checkout.
"""
from __future__ import annotations

import argparse
from collections.abc import Iterator, Mapping
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
import lib.proc_group as proc_group

GIT_SAFE_CONFIG_KEYS = {
    "color.ui", "color.diff", "color.status", "color.branch",
    "core.quotepath", "core.autocrlf", "diff.renames", "diff.algorithm",
    "log.date", "log.decorate",
}
GIT_GLOBAL_BOOL_FLAGS = {
    "--no-pager", "-p", "--paginate", "-P", "--no-replace-objects",
    "--literal-pathspecs", "--glob-pathspecs", "--noglob-pathspecs", "--icase-pathspecs", "--no-optional-locks",
}

# Strict positive exact-spelling option allowlists per read-only Git subcommand: `(bool_flags, value_flags, eq_prefixes, allow_count_short)`.
_LOG_BOOL_FLAGS = {
    "--oneline", "--graph", "--decorate", "--no-decorate", "--all", "--branches", "--tags", "--remotes",
    "--stat", "--numstat", "--shortstat", "--name-only", "--name-status", "--summary", "-p", "-u", "--patch",
    "-s", "--no-patch", "--raw", "--first-parent", "--no-merges", "--merges", "--reverse", "--follow", "-z",
    "--abbrev-commit", "--no-abbrev-commit", "--full-history", "--simplify-by-decoration", "--date-order",
    "--author-date-order", "--topo-order", "--no-color", "--color", "-w", "--ignore-all-space", "-b",
    "--ignore-space-change", "-M", "-C",
}
_LOG_VAL_FLAGS = {
    "-n", "--max-count", "--skip", "--since", "--after", "--until", "--before",
    "--author", "--committer", "--grep", "-S", "-G", "-L", "--format", "--pretty", "--date", "--diff-filter",
}
_LOG_EQ_PREFIXES = (
    "--max-count=", "--skip=", "--since=", "--after=", "--until=", "--before=",
    "--author=", "--committer=", "--grep=", "--format=", "--pretty=", "--date=",
    "--diff-filter=", "--decorate=", "--color=", "--abbrev=",
)

GIT_SUBCOMMAND_SPECS: dict[str, tuple[set[str], set[str], tuple[str, ...], bool]] = {
    "status": (
        {"-s", "--short", "-b", "--branch", "-sb", "-bs", "--porcelain", "-u", "-uno", "-unormal", "-uall",
         "--ignored", "-z", "--long", "--ahead-behind", "--no-ahead-behind", "--renames", "--no-renames"},
        set(),
        ("--porcelain=", "--untracked-files=", "--ignored="),
        False,
    ),
    "diff": (
        {"--stat", "--numstat", "--shortstat", "--name-only", "--name-status", "--summary", "--patch", "-p", "-u",
         "--raw", "--cached", "--staged", "--check", "--minimal", "--patience", "--histogram", "-w",
         "--ignore-all-space", "-b", "--ignore-space-change", "--ignore-space-at-eol", "--ignore-cr-at-eol",
         "-M", "-C", "-B", "-D", "-R", "-z", "--no-renames", "--no-color", "--color", "--word-diff",
         "--exit-code", "--quiet"},
        {"-U", "--unified", "--diff-filter", "--relative"},
        ("--unified=", "--diff-filter=", "--word-diff=", "--stat=", "--find-renames=", "--find-copies=",
         "--color=", "--relative=", "--abbrev="),
        False,
    ),
    "log": (_LOG_BOOL_FLAGS, _LOG_VAL_FLAGS, _LOG_EQ_PREFIXES, True),
    "show": (_LOG_BOOL_FLAGS, _LOG_VAL_FLAGS, _LOG_EQ_PREFIXES, True),
    "shortlog": (
        {"-s", "--summary", "-n", "--numbered", "-e", "--email", "--all", "--no-merges"},
        {"--group", "--format"},
        ("--group=", "--format="),
        False,
    ),
    "grep": (
        {"-n", "--line-number", "-i", "--ignore-case", "-w", "--word-regexp", "-v", "--invert-match",
         "-l", "--files-with-matches", "-L", "--files-without-match", "-c", "--count", "-h", "-H", "-I",
         "-E", "--extended-regexp", "-F", "--fixed-strings", "-P", "--perl-regexp", "-G", "--basic-regexp",
         "-z", "--null", "--untracked", "--cached", "--recurse-submodules", "--heading", "--break",
         "--full-name", "-q", "--quiet", "--all-match", "--and", "--or", "--not", "--no-color", "--color"},
        {"-e", "-A", "-B", "-C", "--max-depth", "--threads"},
        ("--max-depth=", "--threads=", "--color="),
        False,
    ),
    "blame": (
        {"-l", "-t", "-s", "-p", "--porcelain", "--line-porcelain", "-w", "-M", "-C", "-n", "-f", "-e",
         "--show-email", "--show-name", "--show-number", "--root", "--incremental"},
        {"-L", "--date"},
        ("--date=", "--abbrev="),
        False,
    ),
    "ls-files": (
        {"-c", "--cached", "-d", "--deleted", "-m", "--modified", "-o", "--others", "-i", "--ignored",
         "-s", "--stage", "-u", "--unmerged", "-k", "--killed", "-z", "--exclude-standard", "--full-name",
         "--error-unmatch", "--recurse-submodules"},
        {"-x", "--exclude"},
        ("--exclude=",),
        False,
    ),
    "ls-tree": (
        {"-d", "-r", "-t", "-l", "--long", "-z", "--name-only", "--name-status", "--object-only",
         "--full-name", "--full-tree"},
        {"--abbrev", "--format"},
        ("--abbrev=", "--format="),
        False,
    ),
    "rev-parse": (
        {"--verify", "-q", "--quiet", "--short", "--symbolic", "--symbolic-full-name", "--abbrev-ref",
         "--all", "--branches", "--tags", "--remotes", "--show-toplevel", "--show-prefix", "--show-cdup",
         "--git-dir", "--git-common-dir", "--is-inside-work-tree", "--is-inside-git-dir",
         "--is-bare-repository", "--is-shallow-repository"},
        set(),
        ("--short=", "--abbrev-ref=", "--branches=", "--tags=", "--remotes="),
        False,
    ),
    "rev-list": (
        {"--count", "--objects", "--all", "--branches", "--tags", "--remotes", "--first-parent",
         "--no-merges", "--merges", "--reverse", "--topo-order", "--date-order", "--header", "--oneline", "--quiet"},
        {"-n", "--max-count", "--skip", "--since", "--after", "--until", "--before", "--format", "--pretty"},
        ("--max-count=", "--skip=", "--format=", "--pretty="),
        True,
    ),
    "cat-file": (
        {"-t", "-s", "-e", "-p", "--batch-check", "--batch-all-objects"},
        set(),
        ("--batch-check=",),
        False,
    ),
    "describe": (
        {"--dirty", "--all", "--tags", "--contains", "--exact-match", "--always", "--long", "--first-parent"},
        {"--abbrev", "--candidates", "--match", "--exclude"},
        ("--dirty=", "--abbrev=", "--candidates=", "--match=", "--exclude="),
        False,
    ),
    "for-each-ref": (
        {"--ignore-case"},
        {"--count", "--sort", "--format", "--points-at", "--merged", "--no-merged", "--contains", "--no-contains"},
        ("--count=", "--sort=", "--format=", "--points-at=", "--merged=", "--no-merged=",
         "--contains=", "--no-contains="),
        False,
    ),
    "name-rev": (
        {"--tags", "--all", "--always", "--name-only", "--no-undefined"},
        {"--refs", "--exclude"},
        ("--refs=", "--exclude="),
        False,
    ),
    "merge-base": (
        {"--all", "-a", "--octopus", "--independent", "--is-ancestor", "--fork-point"},
        set(),
        (),
        False,
    ),
    "check-attr": ({"-a", "--all", "--cached", "-z"}, set(), (), False),
    "check-ignore": ({"-q", "--quiet", "-v", "--verbose", "-n", "--non-matching", "--no-index", "-z"}, set(), (), False),
    "verify-commit": ({"-v", "--verbose", "--raw"}, set(), (), False),
    "verify-tag": ({"-v", "--verbose", "--raw"}, set(), (), False),
    "version": (set(), set(), (), False),
}
PYTHON_SAFE_FLAGS = {"-B", "-u", "-S", "-E", "-I"}
UNITTEST_BOOL_FLAGS = {
    "-v", "--verbose", "-q", "--quiet", "-f", "--failfast", "-b", "--buffer", "-c", "--catch", "--locals",
}
UNITTEST_TARGET_RE = re.compile(r"^(?:tests|scripts\.tests)(?:\.[A-Za-z_][A-Za-z0-9_]*)+$")
CHECK_ONLY_SCRIPTS = {
    "scripts/check-windows-sdk-line.py",
    "scripts/check-actions-pins.py",
    "scripts/check-vcpkg-baseline.py",
    "scripts/check-python-pin.py",
    "scripts/check-visual-studio.py",
}
DASH_CHECK_SCRIPTS = {
    "scripts/gen-compile-commands.py",
    "scripts/clangd-lsp.py",
    "scripts/python-lsp.py",
}
ALLOWED_GTEST_BINS = {
    "build/bin/x64/Debug/recto-tests/recto-tests.exe",
    "build/bin/x64/Release/recto-tests/recto-tests.exe",
    "build/bin-asan/x64/Debug/recto-tests/recto-tests.exe",
    "build/bin-asan/x64/Release/recto-tests/recto-tests.exe",
}
FORBIDDEN_UNQUOTED = set(";|&\n\r`$(){}[]<>")


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def _is_safe_relpath(raw: str) -> bool:
    if not raw or "\x00" in raw:
        return False
    norm = raw.replace("\\", "/")
    if norm.startswith("/") or re.match(r"^[A-Za-z]:", norm):
        return False
    parts = [p for p in norm.split("/") if p not in ("", ".")]
    return ".." not in parts


def tokenize_command(command: str) -> tuple[list[str] | None, str | None]:
    """Tokenize a single literal command while rejecting every shell metacharacter, pipeline, redirect and expression."""
    cmd = command.strip()
    if not cmd:
        return None, "empty command is forbidden"
    cmd = re.sub(r"\s+2>&1\s*$", "", cmd).strip()
    if cmd.startswith("&") and (len(cmd) == 1 or cmd[1].isspace()):
        cmd = cmd[1:].lstrip()
    if not cmd:
        return None, "empty command is forbidden"

    tokens: list[str] = []
    buf: list[str] = []
    in_tok = False
    quote: str | None = None
    i = 0
    while i < len(cmd):
        ch = cmd[i]
        if ord(ch) < 32:
            return None, "multiline or control characters are forbidden"
        if quote is None:
            if ch.isspace():
                if in_tok:
                    tokens.append("".join(buf))
                    buf = []
                    in_tok = False
            elif ch in ("'", '"'):
                quote = ch
                in_tok = True
            elif ch in FORBIDDEN_UNQUOTED:
                return None, f"shell operator or expression character {ch!r} is forbidden"
            else:
                buf.append(ch)
                in_tok = True
        elif quote == "'":
            if ch == "'":
                if i + 1 < len(cmd) and cmd[i + 1] == "'":
                    buf.append("'")
                    i += 1
                else:
                    quote = None
            else:
                buf.append(ch)
        elif quote == '"':
            if ch in ("$", "`"):
                return None, f"PowerShell interpolation character {ch!r} inside double quotes is forbidden"
            if ch == "\\" and i + 1 < len(cmd) and cmd[i + 1] == '"':
                buf.append('"')
                i += 1
            elif ch == '"':
                quote = None
            else:
                buf.append(ch)
        i += 1

    if quote is not None:
        return None, "unclosed quote is forbidden"
    if in_tok:
        tokens.append("".join(buf))
    if not tokens:
        return None, "empty command is forbidden"
    return tokens, None


def _scan_exact_git_opts(
    sub: str,
    rest: list[str],
    bool_flags: set[str],
    val_flags: set[str],
    eq_prefixes: tuple[str, ...],
    *,
    allow_count_short: bool = False,
) -> tuple[list[str] | None, str | None]:
    """Validate `rest` against exact option spellings so Git's option-prefix abbreviations cannot smuggle flags."""
    positionals: list[str] = []
    j = 0
    while j < len(rest):
        arg = rest[j]
        if arg == "--":
            positionals.extend(rest[j + 1:])
            break
        if arg.startswith("-") and arg != "-":
            if arg in bool_flags:
                j += 1
                continue
            if arg in val_flags:
                if j + 1 >= len(rest):
                    return None, f"git {sub} option {arg!r} requires a value"
                j += 2
                continue
            if any(arg.startswith(pfx) and len(arg) > len(pfx) for pfx in eq_prefixes):
                j += 1
                continue
            if allow_count_short and re.fullmatch(r"-(?:n)?\d+", arg):
                j += 1
                continue
            if sub == "diff" and re.fullmatch(r"-U\d+", arg):
                j += 1
                continue
            if sub == "grep" and re.fullmatch(r"-[ABC]\d+", arg):
                j += 1
                continue
            return None, f"git {sub} option {arg!r} is not in the Hydrogen exact-spelling read-only allowlist"
        positionals.append(arg)
        j += 1
    return positionals, None


def _validate_git_argv(tokens: list[str]) -> str | None:
    if len(tokens) == 2 and tokens[1] in ("--version", "--help", "-h"):
        return None
    i = 1
    while i < len(tokens):
        tok = tokens[i]
        if tok == "-C":
            if i + 1 >= len(tokens) or (not _is_safe_relpath(tokens[i + 1]) and tokens[i + 1] != "."):
                return "git -C must name '.' or a repository-relative path"
            i += 2
            continue
        if tok.startswith("-C") and len(tok) > 2:
            rel = tok[2:]
            if rel != "." and not _is_safe_relpath(rel):
                return "git -C must name '.' or a repository-relative path"
            i += 1
            continue
        if tok == "-c":
            if i + 1 >= len(tokens) or "=" not in tokens[i + 1]:
                return "git -c requires key=value"
            key = tokens[i + 1].split("=", 1)[0].strip().lower()
            if key not in GIT_SAFE_CONFIG_KEYS:
                return f"git -c {key!r} is not in the safe read-only config allowlist"
            i += 2
            continue
        if tok.startswith("-c") and len(tok) > 2:
            kv = tok[2:]
            key = kv.split("=", 1)[0].strip().lower() if "=" in kv else ""
            if key not in GIT_SAFE_CONFIG_KEYS:
                return f"git -c {key!r} is not in the safe read-only config allowlist"
            i += 1
            continue
        if tok in GIT_GLOBAL_BOOL_FLAGS:
            i += 1
            continue
        if tok.startswith("-"):
            return f"git global option {tok!r} is forbidden"
        break

    if i >= len(tokens):
        return "missing git subcommand"
    sub = tokens[i].lower()
    rest = tokens[i + 1:]

    spec = GIT_SUBCOMMAND_SPECS.get(sub)
    if spec is not None:
        bool_f, val_f, eq_p, allow_cnt = spec
        pos, err = _scan_exact_git_opts(sub, rest, bool_f, val_f, eq_p, allow_count_short=allow_cnt)
        if err:
            return err
        if sub == "version" and pos:
            return "git version takes no positional arguments"
        return None

    if sub == "branch":
        if not rest or rest == ["--show-current"]:
            return None
        pos, err = _scan_exact_git_opts(
            "branch",
            rest,
            {"-a", "--all", "-r", "--remotes", "-v", "-vv", "--verbose", "--list", "-l", "--show-current", "--no-color", "--color"},
            {"--contains", "--no-contains", "--merged", "--no-merged", "--sort", "--format", "--points-at"},
            ("--contains=", "--no-contains=", "--merged=", "--no-merged=", "--sort=", "--format=", "--points-at=", "--color="),
        )
        if err:
            return err
        has_query_mode = any(
            r in ("--list", "-l", "--contains", "--no-contains", "--merged", "--no-merged")
            or r.startswith(("--contains=", "--no-contains=", "--merged=", "--no-merged="))
            for r in rest
        )
        if pos and not has_query_mode:
            return "creating or mutating git branches is forbidden"
        return None

    if sub == "tag":
        if not rest:
            return None
        pos, err = _scan_exact_git_opts(
            "tag",
            rest,
            {"-l", "--list", "-n", "--ignore-case"},
            {"--sort", "--format", "--contains", "--no-contains", "--merged", "--no-merged", "--points-at"},
            ("--sort=", "--format=", "--contains=", "--no-contains=", "--merged=", "--no-merged=", "--points-at="),
        )
        if err:
            return err
        if "-l" not in rest and "--list" not in rest:
            return "creating or deleting git tags is forbidden"
        return None

    if sub == "config":
        pos, err = _scan_exact_git_opts(
            "config",
            rest,
            {"--get", "--get-all", "--get-regexp", "--list", "-l", "--local", "--worktree",
             "--show-origin", "--show-scope", "-z", "--null", "--name-only"},
            set(),
            (),
        )
        if err or pos is None:
            return err or "invalid git config arguments"
        is_list = "-l" in rest or "--list" in rest
        is_get = any(r in ("--get", "--get-all", "--get-regexp") for r in rest)
        if is_list and not is_get and len(pos) == 0:
            return None
        if is_get and not is_list and 1 <= len(pos) <= 2:
            return None
        return "mutating git config commands are forbidden"

    return f"git subcommand {sub!r} is not in the Hydrogen read-only allowlist"


def _validate_python_argv(tokens: list[str]) -> str | None:
    base = tokens[0].lower()
    i = 1
    while i < len(tokens):
        tok = tokens[i]
        if tok in PYTHON_SAFE_FLAGS:
            i += 1
            continue
        if base in ("py", "py.exe") and re.match(r"^-3(?:\.\d+)?$", tok):
            i += 1
            continue
        break
    if i >= len(tokens):
        return "interactive python invocation without a read-only target is forbidden"

    head = tokens[i]
    rest = tokens[i + 1:]
    if head == "-m":
        if not rest or rest[0] != "unittest":
            return "only '-m unittest' is permitted under python -m"
        u_args = rest[1:]
        targets = 0
        j = 0
        while j < len(u_args):
            arg = u_args[j]
            if arg in UNITTEST_BOOL_FLAGS:
                j += 1
                continue
            if arg == "-k" and j + 1 < len(u_args):
                j += 2
                continue
            if UNITTEST_TARGET_RE.match(arg):
                targets += 1
                j += 1
                continue
            return f"unittest argument {arg!r} is not an allowlisted tests.* or scripts.tests.* target"
        return None if targets >= 1 else "python -m unittest requires at least one explicit test module target"

    if head.startswith("-"):
        return f"python option {head!r} (such as -c) is forbidden"

    script = head.replace("\\", "/").lstrip("./")
    if script == "scripts/todo-graph.py":
        if rest in (["validate"], ["render"], ["plan", "--check"]):
            return None
        if (
            len(rest) == 2
            and rest[0] == "query"
            and rest[1] in ("ready", "blocked", "stats", "deferred", "adjacency", "plan", "rounds")
        ):
            return None
        if rest and rest[0] == "resolve":
            r_tail = rest[1:]
            if len(r_tail) == 1 and not r_tail[0].startswith("-"):
                return None
            if len(r_tail) == 2 and "--context" in r_tail and any(not x.startswith("-") for x in r_tail):
                return None
        if len(rest) >= 2 and rest[0] == "classify" and all(not a.startswith("-") for a in rest[1:]):
            return None
        return "only exact 'todo-graph.py validate|render|query|resolve|classify|plan --check' options are permitted"
    if script == "scripts/ci_status.py":
        j = 0
        while j < len(rest):
            if rest[j] == "--json":
                j += 1
            elif rest[j] == "--branch" and j + 1 < len(rest) and not rest[j + 1].startswith("-"):
                j += 2
            else:
                return f"ci_status.py option {rest[j]!r} is forbidden in Hydrogen helpers"
        return None
    if script == "scripts/run-tests.py":
        j = 0
        while j < len(rest):
            if rest[j] in ("--quick", "--full", "--list"):
                j += 1
            elif rest[j] in ("-j", "--jobs") and j + 1 < len(rest) and rest[j + 1].isdigit():
                j += 2
            elif rest[j] == "--timeout" and j + 1 < len(rest) and re.match(r"^(?:[1-9]\d*(?:\.\d+)?|0\.\d*[1-9]\d*)$", rest[j + 1]):
                j += 2
            elif rest[j] in ("-k", "--filter") and j + 1 < len(rest):
                j += 2
            else:
                return f"run-tests.py argument {rest[j]!r} is forbidden in Hydrogen helpers"
        return None
    if script == "scripts/review_pin.py":
        if "--check" in rest and all(a in ("--check", "--all", "--json") for a in rest):
            return None
        return "only 'review_pin.py --check [--all] [--json]' is permitted"
    if script == "scripts/review-dispatch.py":
        if any(a in ("--check-pin", "--list-rounds") for a in rest) and all(
            a in ("--check-pin", "--list-rounds", "--json") for a in rest
        ):
            return None
        return "only 'review-dispatch.py --check-pin|--list-rounds [--json]' is permitted"
    if script in DASH_CHECK_SCRIPTS:
        return None if rest == ["--check"] else f"only '{script} --check' is permitted"
    if script == "scripts/check-sdk-pins.py":
        return None if not rest else "check-sdk-pins.py takes no arguments in Hydrogen helpers"
    if script in CHECK_ONLY_SCRIPTS:
        return None if rest == ["check"] else f"only '{script} check' is permitted"
    if script == "scripts/mutate-check.py":
        return ("mutate-check.py executes caller-supplied code replacements and may only run in the Gemini 4 Carbon "
                "writer session, never in read-only Hydrogen helpers")
    return f"python script {script!r} is not in the Hydrogen read-only allowlist"


def validate_hydrogen_argv(tokens: list[str]) -> str | None:
    """Return None when `tokens` is an explicitly allowlisted read-only command, or a refusal reason."""
    if not tokens:
        return "empty command is forbidden"
    prog_norm = tokens[0].replace("\\", "/")
    base = os.path.basename(prog_norm).lower()

    if prog_norm.lower() == base and base in ("git", "git.exe"):
        return _validate_git_argv(tokens)
    if prog_norm.lower() == base and base in ("python", "python3", "python.exe", "python3.exe", "py", "py.exe"):
        return _validate_python_argv(tokens)
    if prog_norm.lower() == base and base in ("node", "node.exe"):
        if len(tokens) == 2 and tokens[1].replace("\\", "/").lstrip("./") == "scripts/verify-agent-tiers.mjs":
            return None
        return "only 'node scripts/verify-agent-tiers.mjs' is permitted"
    rel_bin = prog_norm.lstrip("./")
    if rel_bin in ALLOWED_GTEST_BINS:
        for arg in tokens[1:]:
            if arg in ("--gtest_list_tests", "--gtest_also_run_disabled_tests"):
                continue
            if arg.startswith(("--gtest_filter=", "--gtest_brief=", "--gtest_color=")):
                continue
            return f"recto-tests option {arg!r} (such as --gtest_output) is forbidden in Hydrogen helpers"
        return None
    return f"executable {tokens[0]!r} is not in the Hydrogen read-only allowlist"


def parse_hydrogen_command(command: str) -> tuple[list[str] | None, str | None]:
    tokens, tok_err = tokenize_command(command)
    if tok_err or tokens is None:
        return None, tok_err or "invalid command"
    val_err = validate_hydrogen_argv(tokens)
    if val_err:
        return None, val_err
    return tokens, None


def build_overwrite_command(tokens: list[str], script_path: Path | None = None) -> str:
    target = (script_path or Path(__file__)).resolve().as_posix().replace("'", "''")
    payload_hex = json.dumps(tokens, separators=(",", ":")).encode("utf-8").hex()
    return f"& python '{target}' --payload {payload_hex}"


_FORBIDDEN_ENV_KEYS = frozenset({
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
    "GIT_COMMON_DIR",
    "GIT_CONFIG_COUNT",
    "GIT_CONFIG_PARAMETERS",
    "GIT_DIR",
    "GIT_EXTERNAL_DIFF",
    "GIT_INDEX_FILE",
    "GIT_OBJECT_DIRECTORY",
    "GIT_WORK_TREE",
    "LLVM_PROFILE_FILE",
    "NODE_OPTIONS",
    "PYTHONHOME",
    "PYTHONINSPECT",
    "PYTHONPATH",
    "PYTHONSTARTUP",
    "RECTO_CORPUS_DIR",
    "RECTO_UPDATE_GOLDENS",
    "TEST_OUTPUT_FILE",
    "XML_OUTPUT_FILE",
})
_FORBIDDEN_ENV_PREFIXES = (
    "GIT_CONFIG_KEY_",
    "GIT_CONFIG_VALUE_",
    "GTEST_",
    "RECTO_UPDATE_",
)
_SKIPPED_GTEST_DEBUG_EXTS = frozenset({
    ".exp", ".idb", ".ilk", ".iobj", ".ipdb", ".lastbuildstate", ".lib", ".obj", ".pdb", ".tlog",
})


def _sanitized_hydrogen_env(base_env: Mapping[str, str] | None = None) -> dict[str, str]:
    """Return an environment stripped of mutation, output-redirection, and repo-relocation variables."""
    src = os.environ if base_env is None else base_env
    clean: dict[str, str] = {}
    for key, val in src.items():
        upper = key.upper()
        if upper in _FORBIDDEN_ENV_KEYS or upper.startswith(_FORBIDDEN_ENV_PREFIXES):
            continue
        clean[key] = val
    clean["PYTHONDONTWRITEBYTECODE"] = "1"
    clean["GIT_OPTIONAL_LOCKS"] = "0"
    return clean


def _stage_gtest_binary_tree(root: Path, exe: Path, staged_exe: Path) -> None:
    """Copy `<Plat>/<Cfg>` runtime outputs (including sibling `Recto/Recto.exe` and `recto-cli`) without debug/lib files."""
    def copy_dir(src_dir: Path, dst_dir: Path) -> None:
        if not src_dir.is_dir():
            return
        for item in src_dir.iterdir():
            if item.is_symlink():
                continue
            if item.is_dir():
                copy_dir(item, dst_dir / item.name)
            elif item.is_file() and item.suffix.lower() not in _SKIPPED_GTEST_DEBUG_EXTS:
                dst_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, dst_dir / item.name)

    cfg_dir = exe.parent.parent
    if cfg_dir != root and cfg_dir.is_relative_to(root):
        copy_dir(cfg_dir, staged_exe.parent.parent)
    else:
        copy_dir(exe.parent, staged_exe.parent)
    if not staged_exe.is_file():
        staged_exe.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(exe, staged_exe)


def stage_isolated_repo(root: Path, dest: Path) -> None:
    """Populate `dest` with `root`'s tracked/untracked files and read-only git object alternates."""
    env = _sanitized_hydrogen_env()
    listed = subprocess.run(
        ["git", "--no-optional-locks", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        capture_output=True, timeout=60, env=env, creationflags=windowless(),
    )
    if listed.returncode != 0:
        raise RuntimeError(f"git ls-files failed: {listed.stderr.decode('utf-8', 'replace').strip()}")
    for raw in listed.stdout.split(b"\0"):
        if not raw:
            continue
        rel = raw.decode("utf-8", "surrogateescape")
        src = root / rel
        if not src.is_file() or src.is_symlink():
            continue
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)

    git_dir_proc = subprocess.run(
        ["git", "--no-optional-locks", "-C", str(root), "rev-parse", "--git-dir"],
        capture_output=True, text=True, timeout=15, env=env, creationflags=windowless(),
    )
    common = subprocess.run(
        ["git", "--no-optional-locks", "-C", str(root), "rev-parse", "--git-common-dir"],
        capture_output=True, text=True, timeout=15, env=env, creationflags=windowless(),
    )
    if common.returncode == 0 and common.stdout.strip():
        git_common = (root / common.stdout.strip()).resolve()
        git_dir = (
            (root / git_dir_proc.stdout.strip()).resolve()
            if git_dir_proc.returncode == 0 and git_dir_proc.stdout.strip()
            else git_common
        )
        subprocess.run(
            ["git", "-C", str(dest), "init", "-q"],
            check=True, capture_output=True, env=env, creationflags=windowless(),
        )
        alt = dest / ".git" / "objects" / "info" / "alternates"
        alt.parent.mkdir(parents=True, exist_ok=True)
        alt.write_text((git_common / "objects").as_posix() + "\n", encoding="utf-8", newline="\n")
        for item in ("packed-refs", "config"):
            src_meta = git_common / item
            if src_meta.is_file():
                shutil.copy2(src_meta, dest / ".git" / item)
        for item in ("HEAD", "index"):
            src_local = git_dir / item
            if src_local.is_file():
                shutil.copy2(src_local, dest / ".git" / item)
        for shared_idx in git_dir.glob("sharedindex.*"):
            if shared_idx.is_file():
                shutil.copy2(shared_idx, dest / ".git" / shared_idx.name)
        src_refs = git_common / "refs"
        if src_refs.is_dir():
            shutil.copytree(src_refs, dest / ".git" / "refs", dirs_exist_ok=True)
        # Refresh stat cache in `dest/.git/index` so clean copied files are not misreported as modified/deleted.
        subprocess.run(
            ["git", "-c", "core.fsmonitor=false", "-C", str(dest), "update-index", "--refresh", "-q"],
            capture_output=True, timeout=30, env=env, creationflags=windowless(),
        )


@contextmanager
def _isolated_temp_dir(prefix: str) -> Iterator[Path]:
    """Create a temporary directory and retry removal briefly so terminated child file handles can release cleanly."""
    tmp_path = Path(tempfile.mkdtemp(prefix=prefix))
    try:
        yield tmp_path
    finally:
        for attempt in range(10):
            try:
                shutil.rmtree(tmp_path)
                break
            except FileNotFoundError:
                break
            except OSError:
                if attempt == 9:
                    shutil.rmtree(tmp_path, ignore_errors=True)
                    break
                time.sleep(0.05)


def _run_managed_passthrough(cmd: list[str], *, cwd: Path, env: dict[str, str]) -> int:
    """Execute `cmd` inside a lifetime-bound job/tree so cancellation or early exit reaps every grandchild."""
    with proc_group.termination_signal_guard():
        managed = proc_group.spawn_managed_process(cmd, cwd=cwd, env=env, capture_output=False)
        try:
            return managed.proc.wait()
        except BaseException:
            managed.terminate_and_close()
            try:
                managed.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                pass
            raise
        finally:
            managed.terminate_and_close()
            try:
                managed.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                pass


def execute_hydrogen_argv(tokens: list[str], root: Path = ROOT) -> int:
    """Execute validated `tokens` with `shell=False` and temporary-directory checkout isolation."""
    err = validate_hydrogen_argv(tokens)
    if err:
        print(f"hydrogen-readonly: {err}", file=sys.stderr)
        return 2

    root = root.resolve()
    env = _sanitized_hydrogen_env()

    prog_norm = tokens[0].replace("\\", "/")
    base = os.path.basename(prog_norm).lower()

    if base in ("git", "git.exe"):
        with _isolated_temp_dir("recto-hydrogen-git-") as tmp_path:
            stage_isolated_repo(root, tmp_path)
            cmd = [
                "git", "--no-pager", "--no-optional-locks",
                "-c", "core.fsmonitor=false", "-c", "diff.external=", "-c", "core.pager=cat",
                *tokens[1:],
            ]
            return _run_managed_passthrough(cmd, cwd=tmp_path, env=env)

    if base in ("node", "node.exe"):
        with _isolated_temp_dir("recto-hydrogen-node-") as tmp_path:
            stage_isolated_repo(root, tmp_path)
            cmd = ["node", str(tmp_path / "scripts" / "verify-agent-tiers.mjs")]
            return _run_managed_passthrough(cmd, cwd=tmp_path, env=env)

    rel_bin = prog_norm.lstrip("./")
    if rel_bin in ALLOWED_GTEST_BINS:
        exe = (root / rel_bin).resolve()
        if not exe.is_relative_to(root) or not exe.is_file():
            print(f"hydrogen-readonly: missing test executable {rel_bin}", file=sys.stderr)
            return 2
        with _isolated_temp_dir("recto-hydrogen-gtest-") as tmp_path:
            stage_isolated_repo(root, tmp_path)
            staged_exe = tmp_path / rel_bin
            _stage_gtest_binary_tree(root, exe, staged_exe)
            return _run_managed_passthrough([str(staged_exe), *tokens[1:]], cwd=tmp_path, env=env)

    # Read-only `--check` gates inspect gitignored `build/compile_commands.json` whose paths match `root`.
    i = 1
    while i < len(tokens) and (tokens[i] in PYTHON_SAFE_FLAGS or re.match(r"^-3(?:\.\d+)?$", tokens[i])):
        i += 1
    py_flags = [f for f in tokens[1:i] if f in PYTHON_SAFE_FLAGS]
    target = tokens[i].replace("\\", "/").lstrip("./")
    rest = tokens[i + 1:]

    if tokens[i] != "-m" and target in DASH_CHECK_SCRIPTS and rest == ["--check"]:
        script_file = (root / target).resolve()
        if not script_file.is_relative_to(root) or not script_file.is_file():
            print(f"hydrogen-readonly: missing script {target}", file=sys.stderr)
            return 2
        cmd = [sys.executable, "-B", *py_flags, str(script_file), "--check"]
        return _run_managed_passthrough(cmd, cwd=root, env=env)

    # Every other Python command executes inside an isolated staged copy outside `root`.
    with _isolated_temp_dir("recto-hydrogen-py-") as tmp_path:
        stage_isolated_repo(root, tmp_path)
        if tokens[i] == "-m":
            cmd = [sys.executable, "-B", *py_flags, "-m", *rest]
        else:
            cmd = [sys.executable, "-B", *py_flags, str(tmp_path / target), *rest]
        return _run_managed_passthrough(cmd, cwd=tmp_path, env=env)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Execute an allowlisted Hydrogen read-only command without a shell.")
    parser.add_argument("--payload", required=True, help="hex-encoded UTF-8 JSON argv list")
    parser.add_argument("--root", default=str(ROOT), help="repository root")
    args = parser.parse_args(argv)
    try:
        tokens = json.loads(bytes.fromhex(args.payload).decode("utf-8"))
    except (ValueError, RecursionError):
        print("hydrogen-readonly: malformed hex payload", file=sys.stderr)
        return 2
    if not isinstance(tokens, list) or not all(isinstance(t, str) for t in tokens):
        print("hydrogen-readonly: payload must decode to list[str]", file=sys.stderr)
        return 2
    return execute_hydrogen_argv(tokens, Path(args.root))


if __name__ == "__main__":
    sys.exit(main())
