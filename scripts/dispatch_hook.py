#!/usr/bin/env python3
"""PreToolUse hook for Claude Code and Google Antigravity: enforce background reviews and question-free campaigns.

A review (`scripts/review-dispatch.py ... --prompt ...`) takes minutes. Started through the runner's tracked background
option (`run_in_background: true` in Claude Code, `WaitMsBeforeAsync: 500` with `IsDaemon: false` in Antigravity's
`run_command`), the session is notified the moment it finishes and acts on it; started detached (`&`, `nohup`, `disown`,
`setsid`, `Start-Job`, `Start-Process`) or as an indefinite daemon, nothing notifies the session, and a foreground call
blocks or times out. This hook refuses detached, daemon and foreground review launches alike, naming the fix.

In Antigravity (`.agents/hooks.json`, `matcher: "*"`) it also refuses `ask_question` while
`build/claude-campaign-guard.json` is live for the session's `conversationId` (or `CONCLAVE_UNATTENDED_RUN=1`), matching
Claude Code's `--disallowedTools AskUserQuestion` in unattended campaigns while leaving attended sessions free to
confirm destructive or credential actions. Under Orchestrator (Carbon) (`gemini-pro-orchestrator`), both planning and
execution turns remain unrestricted on normal editing, build and command tools.
"""
from __future__ import annotations

import json
import os
import re
import sys

# The dispatcher run as a command: after `python`, `python3`, `python.exe`, `build\python.cmd` or the `py` launcher, quoted or not, and any
# interpreter options (`-u`, `-X dev`, `-3.14`), its path quoted or not, or in command position (the start, after `;`, `&`, `|`, `(`, a
# newline, `nohup`, `setsid` or `exec`), unquoted. Text that only mentions it (a heredoc, a commit message) is not a run.
REVIEW = re.compile(r"(?:\bpy(?:thon)?(?:3(?:\.\d+)?)?(?:\.(?:exe|cmd))?['\"]?(?:\s+(?:-[XW]\s*\S+|-[\w.]+))*\s+['\"]?"
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


def _is_agy(payload: dict) -> bool:
    return isinstance(payload.get("toolCall"), dict) or payload.get("hookEventName") == "PreToolUse"


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
    name = str(tool_call.get("name") or "")
    raw_args = tool_call.get("args")
    args = raw_args if isinstance(raw_args, dict) else {}

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
    """Why this tool call is refused, or None to let it run."""
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


def agy_response(payload: dict) -> dict:
    """Build the Antigravity PreToolUse JSON decision response."""
    why = verdict(payload)
    if why:
        return {"decision": "deny", "reason": why}
    return {"decision": "allow"}


def main() -> int:
    raw = sys.stdin.read()
    try:
        payload = json.loads(raw)
    except ValueError:
        return 0
    if not isinstance(payload, dict):
        return 0
    if _is_agy(payload):
        try:
            resp = agy_response(payload)
        except Exception:
            resp = {"decision": "allow"}
        print(json.dumps(resp))
        return 0
    try:
        why = verdict(payload)
    except Exception:
        why = None
    if why:
        print(why, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
