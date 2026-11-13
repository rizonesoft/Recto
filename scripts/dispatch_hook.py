#!/usr/bin/env python3
"""PreToolUse hook for Claude Code and Google Antigravity: enforce background reviews, campaign question-free runs,
and strictly read-only high-effort Gemini 4 Hydrogen helpers.

A review (`scripts/review-dispatch.py ... --prompt ...`) takes minutes. Started through the runner's tracked background
option (`run_in_background: true` in Claude Code, `WaitMsBeforeAsync: 500` with `IsDaemon: false` in Antigravity's
`run_command`), the session is notified the moment it finishes and acts on it; started detached (`&`, `nohup`, `disown`,
`setsid`, `Start-Job`, `Start-Process`) or as an indefinite daemon, nothing notifies the session, and a foreground call
blocks or times out. This hook refuses detached, daemon and foreground review launches alike, naming the fix.

In Antigravity (`.agents/hooks.json`, `matcher: "*"`) it also enforces two campaign invariants:
1. Refuses `ask_question` while `build/claude-campaign-guard.json` is live for the session's `conversationId`, matching
   Claude Code's `--disallowedTools AskUserQuestion` in unattended campaigns while leaving attended sessions free to
   confirm destructive or credential actions.
2. Enforces Gemini 4 Hydrogen (`gemini-4-hydrogen-high`) read-only helper boundaries at both `PreToolUse` and execution
   time: spawning a Hydrogen swarm worker requires `high` (or `xhigh`) effort and `workspace: "inherit"`; inside any
   Hydrogen turn only an explicit allowlist of read-only tools and literal read-only commands may run, and every allowed
   `run_command` is rewritten via `overwrite` to `scripts/hydrogen_exec.py --payload <hex>`, which re-validates the
   argv allowlist, runs `shell=False` without PowerShell evaluation, and executes test suites in an isolated temporary
   working copy outside the shared checkout.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import hydrogen_exec  # noqa: E402

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
HYDROGEN_PURE_READ_TOOLS = {
    "read_file", "view_file", "view_file_outline", "view_code_item",
    "grep_search", "find_by_name", "list_dir",
    "search_web", "read_url_content", "list_resources", "read_resource",
    "send_message", "post_message", "read_board", "swarm_wait",
}
HYDROGEN_READONLY_MCP_SERVERS = {"recto-lsp", "recto-lsp_recto-lsp"}


def _is_agy(payload: dict) -> bool:
    return isinstance(payload.get("toolCall"), dict) or payload.get("hookEventName") == "PreToolUse"


def _is_hydrogen_worker(payload: dict) -> bool:
    model = str(payload.get("modelName") or payload.get("model") or "").strip().lower()
    return "hydrogen" in model


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


def _hydrogen_tool_verdict(name: str, args: dict) -> str | None:
    if name in HYDROGEN_PURE_READ_TOOLS:
        return None
    if name.startswith(("mcp_recto-lsp_", "mcp_gemini-api_gemini-api-docs_")):
        return None
    if name == "call_mcp_tool":
        server = str(args.get("ServerName") or "")
        if server in HYDROGEN_READONLY_MCP_SERVERS:
            return None
        return (f"hydrogen-readonly: MCP server {server!r} is not permitted in Gemini 4 Hydrogen helpers; "
                "only read-only recto-lsp queries are allowed")
    if name == "swarm":
        if args.get("action") in ("status", "models"):
            return None
        return ("hydrogen-readonly: Gemini 4 Hydrogen helpers may only read swarm status, never add, re-model "
                "or kill workers")
    if name == "run_command":
        cmd = args.get("CommandLine")
        if not isinstance(cmd, str):
            return "hydrogen-readonly: run_command requires a literal string CommandLine"
        _, err = hydrogen_exec.parse_hydrogen_command(cmd)
        if err:
            return (f"hydrogen-readonly: {err} in Gemini 4 Hydrogen helpers; return findings to the "
                    "Gemini 4 Carbon writer session, which owns all edits and arbitrary commands")
        return None
    return (f"hydrogen-readonly: tool {name!r} is forbidden in Gemini 4 Hydrogen helpers; helpers are strictly "
            "read-only and return file:line citations to the Gemini 4 Carbon writer session")


def agy_verdict(payload: dict) -> str | None:
    """Why an Antigravity PreToolUse call is refused, or None to allow it."""
    tool_call = payload.get("toolCall")
    if not isinstance(tool_call, dict):
        return None
    name = str(tool_call.get("name") or "")
    raw_args = tool_call.get("args")
    args = raw_args if isinstance(raw_args, dict) else {}

    # 1. Enforce strict positive allowlist inside any Gemini 4 Hydrogen worker turn.
    if _is_hydrogen_worker(payload):
        why_h = _hydrogen_tool_verdict(name, args)
        if why_h:
            return why_h

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
    """Build the full Antigravity PreToolUse JSON response, rewriting allowed Hydrogen commands to hydrogen_exec.py."""
    why = verdict(payload)
    if why:
        return {"decision": "deny", "reason": why}
    if _is_hydrogen_worker(payload):
        tool_call = payload.get("toolCall")
        if isinstance(tool_call, dict) and tool_call.get("name") == "run_command":
            raw_args = tool_call.get("args")
            args = raw_args if isinstance(raw_args, dict) else {}
            cmd = args.get("CommandLine")
            if isinstance(cmd, str):
                tokens, err = hydrogen_exec.parse_hydrogen_command(cmd)
                if err or tokens is None:
                    return {"decision": "deny", "reason": f"hydrogen-readonly: {err or 'invalid command'}"}
                return {
                    "decision": "allow",
                    "overwrite": {"CommandLine": hydrogen_exec.build_overwrite_command(tokens)},
                }
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
        except Exception as exc:
            if _is_hydrogen_worker(payload):
                resp = {"decision": "deny", "reason": f"hydrogen-readonly: internal guard error ({exc})"}
            else:
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
