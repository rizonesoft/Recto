#!/usr/bin/env python3
"""Claude Code PreToolUse hook: a review runs through the shell tool's own background option, never detached.

A review (`scripts/review-dispatch.py ... --prompt ...`) takes minutes. Started with the tool's `run_in_background`, the
session is told the moment it finishes and acts on it; started detached (`&`, `nohup`, `disown`, `setsid`, `Start-Job`,
`Start-Process`), nothing tells the session, and the finished review waits for the next heartbeat. This hook refuses the
detached form and a foreground form alike, naming the fix. Calls that run no review (`--check-pin`, `--help`) pass, and
so does anything it cannot read: a broken hook must never block the session's other work.

Wired in .claude/settings.json as a PreToolUse hook on Bash and PowerShell; exit 2 refuses the call and shows the message
to Claude. Ported from Is It Legit's `scripts/dispatch_hook.py` (2026-11-06), with PowerShell added because it is Recto's
primary shell on Windows.
"""
import json
import re
import sys

# The dispatcher run as a command: after `python`, `python3` or `python.exe` and any interpreter options (`-u`, `-X dev`),
# its path quoted or not, or in command position (the start, after `;`, `&`, `|`, `(`, a newline, `nohup`, `setsid` or
# `exec`), unquoted. Text that only mentions it (a heredoc, a commit message) is not a run.
REVIEW = re.compile(r"(?:\bpython(?:3(?:\.\d+)?)?(?:\.exe)?(?:\s+(?:-[XW]\s*\S+|-[A-Za-z]+))*\s+['\"]?"
                    r"|(?:^|[\n;&|(]|\bnohup\b|\bsetsid\b|\bexec\b)\s*)"
                    r"(?:[\w.~/\\:-]*[/\\])?review-dispatch\.py\b")
# Bash: a single `&` that backgrounds a command, not `&&`, `&>`, `>&` or `2>&1`.
BASH_DETACH = re.compile(r"(?<![&>])&(?![&>])|\bnohup\b|\bdisown\b|\bsetsid\b")
# PowerShell: `&` is the call operator before a command, so only one that ends a statement backgrounds it.
POWERSHELL_DETACH = re.compile(r"(?<![&>])&\s*(?:$|[;\n])|\bStart-(?:Job|ThreadJob|Process)\b",
                               re.IGNORECASE | re.MULTILINE)
DETACH = {"Bash": BASH_DETACH, "PowerShell": POWERSHELL_DETACH}


def verdict(payload: dict) -> str | None:
    """Why this tool call is refused, or None to let it run."""
    detach = DETACH.get(payload.get("tool_name"))
    tool_input = payload.get("tool_input") or {}
    command = tool_input.get("command") if isinstance(tool_input, dict) else None
    if detach is None or not isinstance(command, str) or not REVIEW.search(command) or "--prompt" not in command:
        return None
    if detach.search(command):
        return ("review-dispatch: run the review with the shell tool's run_in_background option, not detached "
                "(&, nohup, disown, setsid, Start-Job, Start-Process): only the tool's option tells the session when "
                "it finishes")
    if tool_input.get("run_in_background") is not True:
        return ("review-dispatch: a review takes minutes; run it with the shell tool's run_in_background option, so "
                "the session is told when it finishes")
    return None


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (ValueError, OSError):
        return 0
    if not isinstance(payload, dict):
        return 0
    why = verdict(payload)
    if why:
        print(why, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
