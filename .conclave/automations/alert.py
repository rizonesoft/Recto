"""Email the operator when a scheduled automation fails.

    python .conclave/automations/alert.py NAME            send the failure email for NAME's last run
    python .conclave/automations/alert.py NAME --dry-run  print that email instead, reading no key

run.py calls `send_failure` when a task exits non-zero or times out, so a failed run is reported by the
wrapper that saw it fail, whatever the command printed. The email carries the task, its exit, its start and
finish, the tail of that run's log, and how to run it again on this machine's scheduler.

It goes through SMTP2GO's `/v3/email/send` from the Rizonetech website sender, the verified sender and key every
Conclave project's automations use (Rizonesoft is a brand of Rizonetech). The key is read at send time from the
Conclave vault item "Rizonetech SMTP2GO (website mail)" through `conclave-op` (in WSL when this runs on Windows),
for this run only: never written to disk, an argument, or the output. SMTP2GO answers a refused send with HTTP 200
and `succeeded: 0`, so anything but one success and no failures is a refusal.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
ITEM = "op://Conclave/jq2q47sqbpdwacivbjead5rrku"
ENDPOINT = "https://api.smtp2go.com/v3/email/send"
SENDER = "website@rizonetech.com"
TO = "rizonetech@gmail.com"
LOG_LINES = 60


class Refused(Exception):
    pass


def key(run=subprocess.run, windows: bool = os.name == "nt") -> str:
    """The API key, refused unless it has SMTP2GO's shape ("api-" and 32 letters or digits); never echoed."""
    read = f'conclave-op read "{ITEM}/credential"'
    command = ["wsl.exe", "-e", "bash", "-lc", read] if windows else ["bash", "-lc", read]
    result = run(command, capture_output=True, text=True, timeout=90,
                 creationflags=0x08000000 if windows else 0)  # CREATE_NO_WINDOW: run.py has no console
    value = (result.stdout or "").strip()
    if result.returncode != 0 or not value:
        raise Refused("vault field credential is unavailable")
    if len(value) != 36 or not value.startswith("api-") or not value[4:].isalnum() or not value.isascii():
        raise Refused("vault field credential does not have the SMTP2GO key shape")
    return value


def sender(folder: str) -> str:
    """`\\Isotone\\` sends as "Isotone automations <website@rizonetech.com>"."""
    return f"{folder.strip(chr(92))} automations <{SENDER}>"


def rerun(name: str, folder: str, windows: bool) -> list[str]:
    """How to start the task again, on the scheduler this machine uses."""
    if windows:
        return [f"Run again: Start-ScheduledTask -TaskPath '{folder}' -TaskName {name}",
                f"Or directly: python .conclave/automations/run.py {name}"]
    return [f"Run again: systemctl --user start {name}.service",
            f"Or directly: python3 .conclave/automations/run.py {name}"]


def message(name: str, task: dict, outcome: dict, tail: str, host: str, folder: str,
            windows: bool = os.name == "nt") -> tuple[str, str]:
    """The subject and plain-text body of the failure email."""
    exit_text = "timed out" if outcome.get("timed_out") else f"exit {outcome.get('exit')}"
    body = "\n".join([
        f"{name} failed on {host}: {exit_text}.",
        "",
        f"What it does: {task.get('description') or '(no description)'}",
        f"Command: {' '.join(task.get('command', []))}",
        f"Started: {outcome.get('started') or 'unknown'}",
        f"Finished: {outcome.get('finished') or 'unknown'}",
        "",
        f"Last {LOG_LINES} lines of this run's log ({outcome.get('log') or 'no log'}):",
        "",
        tail or "(the run wrote no output)",
        "",
        *rerun(name, folder, windows),
    ])
    return f"Automation failed: {name} on {host}", body


def send(api_key: str, subject: str, body: str, from_: str, opener=urllib.request.urlopen) -> str:
    """Post the email; refuse anything but one success and no failures."""
    payload = json.dumps({"sender": from_, "to": [TO], "subject": subject, "text_body": body}).encode()
    request = urllib.request.Request(ENDPOINT, data=payload, method="POST", headers={
        "Content-Type": "application/json", "Accept": "application/json", "X-Smtp2go-Api-Key": api_key})
    with opener(request, timeout=60) as response:
        data = json.loads(response.read().decode() or "{}").get("data", {})
    if int(data.get("succeeded", 0)) < 1 or int(data.get("failed", 0)) > 0:
        raise Refused(f"SMTP2GO refused the send: {data.get('failures') or data.get('error') or data}")
    return data.get("email_id", "")


def send_failure(name: str, task: dict, outcome: dict, tail: str, folder: str, run=subprocess.run,
                 opener=urllib.request.urlopen, host: str | None = None) -> str:
    subject, body = message(name, task, outcome, tail, host or socket.gethostname(), folder)
    return send(key(run), subject, body, sender(folder), opener)


def tail_of(path: str, lines: int = LOG_LINES) -> str:
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return "".join(fh.readlines()[-lines:]).rstrip()
    except OSError:
        return ""


def main(argv=None, run=subprocess.run, opener=urllib.request.urlopen, host=None, root=REPO) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("name", help="the task that failed, e.g. isotone-dotnet-ga")
    p.add_argument("--dry-run", action="store_true", help="print the email; read no key and send nothing")
    args = p.parse_args(argv)
    with open(os.path.join(root, ".conclave", "automations", "tasks.json"), encoding="utf-8") as fh:
        manifest = json.load(fh)
    task = {t.get("name"): t for t in manifest.get("tasks", [])}.get(args.name)
    if task is None or not manifest.get("folder"):
        print(f"alert: no task named {args.name}", file=sys.stderr)
        return 2
    folder = manifest["folder"]
    try:
        with open(os.path.join(root, "build", "automations", args.name, "last.json"), encoding="utf-8") as fh:
            outcome = json.load(fh)
    except (OSError, ValueError):
        outcome = {}
    tail = tail_of(os.path.join(root, outcome["log"])) if outcome.get("log") else ""
    if args.dry_run:
        subject, body = message(args.name, task, outcome, tail, host or socket.gethostname(), folder)
        print(f"From: {sender(folder)}\nTo: {TO}\nSubject: {subject}\n\n{body}")
        return 0
    try:
        email_id = send_failure(args.name, task, outcome, tail, folder, run, opener, host)
    except (Refused, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"alert not sent: {error}", file=sys.stderr)
        return 1
    print(f"alert sent to {TO} for {args.name} (SMTP2GO email {email_id})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
