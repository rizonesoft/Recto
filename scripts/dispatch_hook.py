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

HYDROGEN_GIT_MUTATE = re.compile(
    r"\bgit(?:\.exe)?\s+(?:-[^\s]+\s+)*"
    r"(?:commit|push|pull|fetch|reset|checkout|switch|restore|clean|merge|rebase|cherry-pick|revert|"
    r"stash|tag|am|apply|add|rm|mv|worktree|init|clone|gc|prune|notes|"
    r"branch\s+-[dDmMcC]|config\s+(?!--get\b|--list\b|-l\b))\b",
    re.IGNORECASE,
)
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


def _is_agy(payload: dict) -> bool:
    return isinstance(payload.get("toolCall"), dict) or payload.get("hookEventName") == "PreToolUse"


def _is_hydrogen_worker(payload: dict) -> bool:
    model = str(payload.get("modelName") or payload.get("model") or "").strip().lower()
    return "hydrogen" in model


def _hydrogen_mutating_command(command: str) -> str | None:
    if HYDROGEN_GIT_MUTATE.search(command):
        return "mutating git commands are forbidden"
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
    workspaces = payload.get("workspacePaths") if isinstance(payload.get("workspacePaths"), list) else []
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
    args = tool_call.get("args") if isinstance(tool_call.get("args"), dict) else {}

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
    tool = payload.get("tool_name")
    detach = DETACH.get(tool)
    tool_input = payload.get("tool_input") or {}
    command = tool_input.get("command") if isinstance(tool_input, dict) else None
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
