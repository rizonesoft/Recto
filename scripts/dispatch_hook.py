#!/usr/bin/env python3
"""PreToolUse hook for Claude Code and Google Antigravity: enforce background reviews, campaign question-free runs,
and read-only high-effort Gemini 4 Hydrogen helpers.

A review (`scripts/review-dispatch.py ... --prompt ...`) takes minutes. Started through the runner's tracked background
option (`run_in_background: true` in Claude Code, `WaitMsBeforeAsync: 500` with `IsDaemon: false` in Antigravity's
`run_command`), the session is notified the moment it finishes and acts on it; started detached (`&`, `nohup`, `disown`,
`setsid`, `Start-Job`, `Start-Process`) or as an indefinite daemon, nothing notifies the session, and a foreground call
blocks or times out. This hook refuses detached, daemon and foreground review launches alike, naming the fix.

In Antigravity it also enforces two campaign invariants:
1. Refuses `ask_question` while `build/claude-campaign-guard.json` is live for the session's `conversationId`, matching
   Claude Code's `--disallowedTools AskUserQuestion` in unattended campaigns while leaving attended sessions free to
   confirm destructive or credential actions.
2. Enforces Gemini 4 Hydrogen (`gemini-4-hydrogen-high`) read-only helper boundaries: spawning a Hydrogen swarm worker
   requires `high` (or `xhigh`) effort and `workspace: "inherit"`, and inside any Hydrogen worker turn all file writes,
   edits, subagent spawns, operator prompts and mutating shell/git/todo commands are denied so 100% of writing,
   architecture, diagnosis and verdicts remain in the Gemini 4 Carbon writer session.

Wired in `.claude/settings.json` on `Bash|PowerShell` (exit 2 + stderr message refuses) and in `.agents/hooks.json` on
`run_command|ask_question|write_file|edit_file|swarm|invoke_subagent` (stdout JSON `{"decision": "deny", "reason": ...}`
refuses, `{"decision": "allow"}` passes).
"""
import json
import os
import re
import shlex
import sys

# The dispatcher run as a command: after `python`, `python3`, `python.exe` or the `py` launcher, quoted or not, and any
# interpreter options (`-u`, `-X dev`, `-3.14`), its path quoted or not, or in command position (the start, after `;`, `&`, `|`, `(`, a
# newline, `nohup`, `setsid` or `exec`), unquoted. Text that only mentions it (a heredoc, a commit message) is not a run.
REVIEW = re.compile(r"(?:\bpy(?:thon)?(?:3(?:\.\d+)?)?(?:\.exe)?['\"]?(?:\s+(?:-[XW]\s*\S+|-[\w.]+))*\s+['\"]?"
                    r"|(?:^|[\n;&|(]|\bnohup\b|\bsetsid\b|\bexec\b)\s*)"
                    r"(?:[\w.~/\\:-]*[/\\])?review-dispatch\.py\b")
# Bash: a single `&` that backgrounds a command, not `&&`, `&>`, `>&` or `2>&1`.
BASH_DETACH = re.compile(r"(?<![&>])&(?![&>])|\bnohup\b|\bdisown\b|\bsetsid\b")
# PowerShell's launchers detach whatever they start, and name the script as an argument of their own, so naming the
# dispatcher beside one is a detached review even when REVIEW cannot read it as a command.
POWERSHELL_LAUNCHER = re.compile(r"\bStart-(?:Job|ThreadJob|Process)\b", re.IGNORECASE)
# PowerShell: `&` is the call operator before a command, so only one that ends a statement backgrounds it.
POWERSHELL_DETACH = re.compile(r"(?<![&>])&\s*(?:$|[;\n])|" + POWERSHELL_LAUNCHER.pattern, re.IGNORECASE | re.MULTILINE)
# Antigravity runs pwsh on Windows and sh elsewhere; leading `&` is PowerShell's call operator, while trailing `&` and
# POSIX/PowerShell detach keywords background the child outside `run_command`'s task tracker.
AGY_DETACH = re.compile(POWERSHELL_DETACH.pattern + r"|\bnohup\b|\bdisown\b|\bsetsid\b", re.IGNORECASE | re.MULTILINE)
DETACH = {"Bash": BASH_DETACH, "PowerShell": POWERSHELL_DETACH}
AGY_MAX_WAIT_MS = 1000

HYDROGEN_ALLOWED_MODELS = {
    "gemini-4-hydrogen-high",
    "gemini-4-hydrogen-xhigh",
    "gemini 4 hydrogen (high)",
    "gemini 4 hydrogen (xhigh)",
}

GIT_GLOBAL_VALUE_FLAGS = {
    "-C", "-c", "--git-dir", "--work-tree", "--namespace", "--super-prefix", "--exec-path", "--config-env",
}
GIT_GLOBAL_BOOL_FLAGS = {
    "--no-pager", "-p", "--paginate", "-P", "--no-replace-objects", "--bare",
    "--literal-pathspecs", "--glob-pathspecs", "--noglob-pathspecs", "--icase-pathspecs", "--no-optional-locks",
}
GIT_READONLY_SUBCOMMANDS = {
    "status", "diff", "log", "show", "blame", "grep", "ls-files", "ls-tree", "rev-parse", "rev-list",
    "cat-file", "describe", "shortlog", "for-each-ref", "name-rev", "merge-base", "check-attr",
    "check-ignore", "verify-commit", "verify-tag", "version", "help",
}
GIT_BRANCH_READONLY_FLAGS = {
    "-a", "--all", "-r", "--remotes", "-v", "-vv", "--verbose", "--list", "-l",
    "--show-current", "--contains", "--no-contains", "--merged", "--no-merged", "--sort", "--format",
}
HYDROGEN_TOOL_MUTATE = re.compile(
    r"todo-graph\.py\b[^\n;|&]*\b(?:stamp|unstamp)\b"
    r"|todo-graph\.py\b[^\n;|&]*\bplan\b[^\n;|&]*--sync\b"
    r"|campaign_guard\.py\b[^\n;|&]*\b(?:acquire|end|park)\b"
    r"|review-dispatch\.py\b[^\n;|&]*(?:--prompt|--record)\b"
    r"|merge_prs\.py\b(?![^\n;|&]*--dry-run\b)",
    re.IGNORECASE,
)
HYDROGEN_SHELL_MUTATE = re.compile(
    r"\b(?:Set-Content|Add-Content|Out-File|Clear-Content|Remove-Item|Move-Item|Copy-Item|New-Item|Rename-Item|Tee-Object)\b"
    r"|(?:^|[\n;|&(]\s*)(?:rm|del|erase|rmdir|rd|mv|cp|ni|ri|mi|cpi|rni|sc|ac|clc|mkdir|md|touch|truncate)\s+\S"
    r"|(?<![-\w>=&])1?>>?(?![>&])\s*(?!(?:\$null|/dev/null|nul)\b)\S"
    r"|\.(?:write_text|write_bytes)\s*\("
    r"|\bopen\s*\([^)]*,\s*['\"][wa+x]"
    r"|\bos\.(?:remove|unlink|replace|rename|makedirs|mkdir|rmdir)\b"
    r"|\bshutil\.(?:rmtree|move|copy\w*)\b",
    re.IGNORECASE | re.MULTILINE,
)


def _git_tokens_violation(tokens: list[str], start: int) -> str | None:
    """Validate one `git ...` invocation starting at `tokens[start]` against a strict read-only allowlist."""
    i = start + 1
    while i < len(tokens):
        tok = tokens[i]
        if tok in ("--version", "--help", "-h"):
            return None
        if tok in GIT_GLOBAL_VALUE_FLAGS:
            i += 2
            continue
        if any(tok.startswith(flag + "=") for flag in GIT_GLOBAL_VALUE_FLAGS if flag.startswith("--")):
            i += 1
            continue
        if (tok.startswith("-C") or tok.startswith("-c")) and len(tok) > 2:
            i += 1
            continue
        if tok in GIT_GLOBAL_BOOL_FLAGS:
            i += 1
            continue
        if tok.startswith("-"):
            return f"unsupported or mutating git global option {tok!r} is forbidden"
        break
    if i >= len(tokens):
        return None
    sub = tokens[i].lower()
    rest = tokens[i + 1:]
    if any(r == "-o" or r == "--output" or r.startswith("--output=") or r.startswith("-o") and len(r) > 2
           for r in rest if isinstance(r, str)):
        return f"git {sub} with --output/-o file write is forbidden"
    if sub in GIT_READONLY_SUBCOMMANDS:
        return None
    if sub == "branch":
        if not rest:
            return None
        if "--list" in rest or "-l" in rest or "--show-current" in rest:
            bad = [r for r in rest if r in ("-d", "-D", "-m", "-M", "-c", "-C", "-f", "--force", "--delete", "--move", "--copy")]
            return None if not bad else "mutating git branch options are forbidden"
        for r in rest:
            if r.startswith("--sort=") or r.startswith("--format="):
                continue
            if r not in GIT_BRANCH_READONLY_FLAGS:
                return "creating or mutating git branches is forbidden"
        return None
    if sub == "tag":
        if not rest or ("-l" in rest or "--list" in rest):
            bad = [r for r in rest if r in ("-d", "--delete", "-a", "--annotate", "-s", "--sign", "-u", "-f", "--force")]
            return None if not bad else "creating or deleting git tags is forbidden"
        return "creating or mutating git tags is forbidden"
    if sub == "config":
        has_read = any(r in ("--get", "--get-all", "--get-regexp", "--list", "-l") for r in rest)
        has_write = any(r in ("--unset", "--unset-all", "--replace-all", "--add", "--rename-section",
                              "--remove-section", "--edit", "-e") for r in rest)
        if has_read and not has_write:
            return None
        return "mutating git config commands are forbidden"
    return f"mutating git subcommand {sub!r} is forbidden"


def _split_shell_segments(command: str) -> list[str]:
    """Split a shell/PowerShell command on unquoted separators and subshell boundaries (`;`, `\\n`, `|`, `&`, `(`, `)`, `` ` ``)."""
    segments: list[str] = []
    buf: list[str] = []
    quote: str | None = None
    for ch in command:
        if quote is not None:
            buf.append(ch)
            if ch == quote:
                quote = None
        elif ch in ("'", '"'):
            quote = ch
            buf.append(ch)
        elif ch in ";\n|&()`":
            segments.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    if buf:
        segments.append("".join(buf))
    return segments


def _hydrogen_git_violation(command: str) -> str | None:
    if not re.search(r"\bgit(?:\.exe)?\b", command, re.IGNORECASE):
        return None
    for segment in _split_shell_segments(command):
        try:
            raw_tokens = shlex.split(segment, posix=False)
        except ValueError:
            return "unparseable quoted git command is forbidden"
        tokens = [t.strip("\"'") for t in raw_tokens if t.strip("\"'") not in ("&", "")]
        for idx, tok in enumerate(tokens):
            base = os.path.basename(tok.replace("\\", "/")).lower()
            if base in ("git", "git.exe"):
                violation = _git_tokens_violation(tokens, idx)
                if violation:
                    return violation
    return None


def _is_agy(payload: dict) -> bool:
    return isinstance(payload.get("toolCall"), dict) or payload.get("hookEventName") == "PreToolUse"


def _is_hydrogen_worker(payload: dict) -> bool:
    model = str(payload.get("modelName") or payload.get("model") or "").strip().lower()
    return "hydrogen" in model


def _hydrogen_mutating_command(command: str) -> str | None:
    git_why = _hydrogen_git_violation(command)
    if git_why:
        return git_why
    if HYDROGEN_TOOL_MUTATE.search(command):
        return "mutating todo-graph, campaign_guard, review-dispatch or merge_prs commands are forbidden"
    if HYDROGEN_SHELL_MUTATE.search(command):
        return "writing, copying, moving or deleting files from the shell is forbidden"
    return None


def _campaign_owns_agy_session(payload: dict) -> bool:
    if os.environ.get("CONCLAVE_UNATTENDED_RUN") == "1":
        return True
    session = str(payload.get("conversationId") or "")
    if not session:
        return False
    raw_ws = payload.get("workspacePaths")
    workspaces = raw_ws if isinstance(raw_ws, list) else []
    ws_root = next((w for w in workspaces if isinstance(w, str) and w), "")
    root = str(os.environ.get("CLAUDE_PROJECT_DIR") or ws_root or payload.get("cwd") or "")
    if not root:
        return False
    guard_path = os.path.join(root, "build", "claude-campaign-guard.json")
    try:
        with open(guard_path, encoding="utf-8-sig") as fh:
            guard = json.load(fh)
    except (OSError, ValueError):
        return False
    return isinstance(guard, dict) and str(guard.get("session_id") or "") == session


def agy_verdict(payload: dict) -> str | None:
    """Why an Antigravity PreToolUse call is refused, or None to allow it."""
    tool_call = payload.get("toolCall")
    if not isinstance(tool_call, dict):
        return None
    name = tool_call.get("name")
    raw_args = tool_call.get("args")
    args = raw_args if isinstance(raw_args, dict) else {}

    # 1. Enforce read-only boundaries inside any Gemini 4 Hydrogen worker turn.
    if _is_hydrogen_worker(payload):
        if name in ("write_file", "edit_file"):
            return ("hydrogen-readonly: Gemini 4 Hydrogen helpers are strictly read-only; return findings with "
                    "file:line citations to the Gemini 4 Carbon writer session, which owns all edits")
        if name == "ask_question":
            return ("hydrogen-readonly: Gemini 4 Hydrogen helpers never ask the operator questions; report back to "
                    "the Gemini 4 Carbon coordinator")
        if name == "invoke_subagent":
            return ("hydrogen-readonly: Gemini 4 Hydrogen helpers do not spawn subagents; return findings directly "
                    "to the Gemini 4 Carbon coordinator")
        if name == "swarm" and args.get("action") not in ("status", "models"):
            return ("hydrogen-readonly: Gemini 4 Hydrogen helpers may only read swarm status, never add, re-model "
                    "or kill workers")
        if name == "run_command":
            cmd = args.get("CommandLine")
            if isinstance(cmd, str):
                why_mut = _hydrogen_mutating_command(cmd)
                if why_mut:
                    return (f"hydrogen-readonly: {why_mut} in Gemini 4 Hydrogen helpers; return conclusions to the "
                            "Gemini 4 Carbon writer session")

    # 2. Enforce high-effort + inherit-workspace when spawning or switching a Gemini 4 Hydrogen worker.
    if name == "swarm" and args.get("action") in ("add", "model"):
        target_model = str(args.get("model") or "").strip().lower()
        if "hydrogen" in target_model:
            if target_model not in HYDROGEN_ALLOWED_MODELS:
                return ("hydrogen-effort: Gemini 4 Hydrogen helpers must run at high effort "
                        "(model: 'gemini-4-hydrogen-high') so mechanical evidence retains full accuracy")
            ws = args.get("workspace")
            if args.get("action") == "add" and ws not in (None, "", "inherit"):
                return ("hydrogen-readonly: Gemini 4 Hydrogen helpers are read-only and must use "
                        "workspace: 'inherit' (never branch or share)")

    if name == "ask_question":
        if _campaign_owns_agy_session(payload):
            return ("campaign_guard: an unattended campaign never asks the operator a question; decide from the "
                    "sources or take the industry-standard default, record it with its cost of reversing, or file a "
                    "waiting Need and carry on")
        return None
    if name != "run_command":
        return None
    command = args.get("CommandLine")
    if not isinstance(command, str) or "--prompt" not in command:
        return None
    launched = "review-dispatch.py" in command and POWERSHELL_LAUNCHER.search(command)
    if not launched and not REVIEW.search(command):
        return None
    if launched or AGY_DETACH.search(command):
        return ("review-dispatch: run the review through run_command with WaitMsBeforeAsync: 500 and IsDaemon: false, "
                "not detached (&, nohup, disown, setsid, Start-Job, Start-Process): only a tracked background task "
                "notifies the session when it finishes")
    if args.get("IsDaemon") is True:
        return ("review-dispatch: run the review with IsDaemon: false and WaitMsBeforeAsync: 500, not IsDaemon: true: "
                "a review terminates and must notify the session when it finishes")
    wait_ms = args.get("WaitMsBeforeAsync")
    if isinstance(wait_ms, bool) or not isinstance(wait_ms, (int, float)) or wait_ms < 0 or wait_ms > AGY_MAX_WAIT_MS:
        return ("review-dispatch: a review takes minutes; run it with WaitMsBeforeAsync: 500 so run_command "
                "backgrounds it immediately and notifies the session when it finishes")
    return None


def verdict(payload: dict) -> str | None:
    """Why this tool call is refused, or None to let it run. This reads shell text, so it is an early warning for the
    launch forms a session really uses, not a proof that no spelling gets past it."""
    if _is_agy(payload):
        return agy_verdict(payload)
    tool = str(payload.get("tool_name") or "")
    detach = DETACH.get(tool)
    raw_input = payload.get("tool_input")
    tool_input = raw_input if isinstance(raw_input, dict) else {}
    command = tool_input.get("command")
    if detach is None or not isinstance(command, str) or "--prompt" not in command:
        return None
    launched = tool == "PowerShell" and "review-dispatch.py" in command and POWERSHELL_LAUNCHER.search(command)
    if not launched and not REVIEW.search(command):
        return None
    if launched or detach.search(command):
        return ("review-dispatch: run the review with the shell tool's run_in_background option, not detached "
                "(&, nohup, disown, setsid, Start-Job, Start-Process): only the tool's option tells the session when "
                "it finishes")
    if tool_input.get("run_in_background") is not True:
        return ("review-dispatch: a review takes minutes; run it with the shell tool's run_in_background option, so "
                "the session is told when it finishes")
    return None


def main() -> int:
    raw = sys.stdin.read()
    try:
        payload = json.loads(raw)
    except ValueError:
        return 0
    if not isinstance(payload, dict):
        return 0
    try:
        why = verdict(payload)
    except Exception:
        why = None
    if _is_agy(payload):
        print(json.dumps({"decision": "deny", "reason": why} if why else {"decision": "allow"}))
        return 0
    if why:
        print(why, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
