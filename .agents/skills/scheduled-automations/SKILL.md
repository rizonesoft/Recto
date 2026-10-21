---
name: scheduled-automations
description: Add, change, remove, install or debug a scheduled job in .conclave/automations/ (tasks.json, run.py, the failure email, install.ps1 for Windows Task Scheduler and install.sh for systemd user timers). Use when a check should run on a schedule, when an "Automation failed" email arrives, or when the operator asks what runs and when.
---

# Scheduled automations

Every scheduled job lives in `.conclave/automations/tasks.json`. The installers register each task with the machine's scheduler, the scheduler starts `run.py <name>`, and `run.py` logs the run under `build/automations/<name>/` and emails the operator when it fails. Read `.conclave/automations/README.md` for the full behaviour.

## Add or change a task

1. If `tasks.json` still has `"folder": null`, set it to the project's folder (`"\\Isotone\\"`). Every task name then starts with the folder name in lower case and a hyphen (`isotone-`).
2. Write the check as a script in this repository that exits 0 when all is well and non-zero, with one line saying why, when it is not. Give it tests. A check that cannot reach its source exits 2 (unverified), never 0.
3. Add the entry: `name`, a one-line `description`, `command` (`["python", "scripts/<check>.py", ...]`), `trigger` (`{"weekly": "Monday", "at": "07:00"}` or `{"at_logon_delay_minutes": 3, "every_minutes": 60}`) and `timeout_minutes`.
4. Add its row to the README's "What runs" table: task, when, what it runs, and when it fails.
5. Run `python scripts/tests/test_automations.py`, run the task once with `python .conclave/automations/run.py <name>`, then re-run the installer: `pwsh .conclave/automations/install.ps1` on Windows, `bash .conclave/automations/install.sh` on Linux and WSL.

To remove a task, delete its entry and its README row, then re-run the installer: it unregisters what the file no longer lists.

## When a failure email arrives

1. Read the run's log under `build/automations/<name>/` (`last.json` names it) and reproduce with `python .conclave/automations/run.py <name>`.
2. Fix the cause where it is owned: the code, setting or pin the check reports on. Never loosen the check or its timeout to make the email stop.
3. Exit 3 means the task failed and the alert could not be sent; the reason is at the end of the log.

## Rules

- `tasks.json` is the only list. Never register a task by hand or name a task in an installer.
- A scheduled command never needs a person: no prompts, no browser, nothing that writes to a live system without the operator's approval.
- Never put a secret in `tasks.json`, a command line or a log. The alert reads its key from the vault at send time.
- Change `run.py`, `alert.py` or the installers only to fix the runner itself, never to add a task: a task is an entry in `tasks.json`, and `scripts/tests/test_automations.py` covers the runner.
