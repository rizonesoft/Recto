"""Run one scheduled automation, keep its log, and email the operator when it fails.

    python .conclave/automations/run.py NAME
    python .conclave/automations/run.py --check       (installers) print the folder, or refuse tasks.json
    python .conclave/automations/run.py --units DIR   (install.sh) write a systemd user unit pair per task

Windows Task Scheduler starts this through `pythonw.exe` (install.ps1 registers every task in tasks.json with it),
and on Linux and WSL a systemd user timer starts it (install.sh). It runs the task's command from the repository
root with the task's timeout, writes the combined output to `build/automations/<name>/<UTC>.log` (the newest 20
kept) and the outcome to `build/automations/<name>/last.json`, and on a non-zero exit, a timeout, or a command that
cannot start (126) sends the failure email through alert.py. Every process it starts, and every nested launch in
the tasks, carries CREATE_NO_WINDOW on Windows, so a scheduled run never opens a console on the desktop. This
wrapper is the one place a failure is reported on both schedulers. Its own exit is the command's (124 on a timeout,
or 3 when the alert could not be sent), so Task Scheduler's Last Run Result and a failed systemd unit show it too.
"""

from __future__ import annotations

import datetime
import importlib.util
import json
import os
import re
import signal
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
FOLDER = re.compile(r"^\\([A-Za-z0-9][A-Za-z0-9._-]*)\\$")
DAYS = ("Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday")
KEEP_LOGS = 20
NO_WINDOW = 0x08000000
MARKER = "# conclave-automations: "


class TaskError(ValueError):
    pass


def _whole(value) -> bool:
    """A positive whole number; JSON `true` is a Python int, and is refused."""
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


def load_manifest(path: str) -> dict:
    """tasks.json as {"folder", "prefix", "tasks" by name}, refused unless every entry is well formed.

    `folder` is the project's Task Scheduler folder (`\\Recto\\`), and every task name starts with the folder's
    name in lower case and a hyphen (`recto-`), so two projects never share a folder or a unit name. A fresh
    manifest has `folder: null` and no tasks until the project names its folder.
    """
    with open(path, encoding="utf-8") as fh:
        doc = json.load(fh)
    if not isinstance(doc, dict) or doc.get("schema_version") != 1 or not isinstance(doc.get("tasks"), list):
        raise TaskError("tasks.json needs schema_version 1 and a tasks list")
    folder = doc.get("folder")
    if folder is None:
        if doc["tasks"]:
            raise TaskError("tasks.json lists tasks but no folder: set folder to \\<Project>\\ first")
        return {"folder": None, "prefix": None, "tasks": {}}
    match = FOLDER.fullmatch(folder) if isinstance(folder, str) else None
    if not match:
        raise TaskError("tasks.json folder must be one Task Scheduler folder, like \\Recto\\")
    prefix = match.group(1).lower() + "-"
    name_rule = re.compile(re.escape(prefix) + r"[a-z0-9][a-z0-9-]*")
    out: dict[str, dict] = {}
    for task in doc["tasks"]:
        if not isinstance(task, dict):
            raise TaskError(f"every task must be an object, not {task!r}")
        name = task.get("name", "")
        if not isinstance(name, str) or not name_rule.fullmatch(name) or name in out:
            raise TaskError(f"task name {name!r} must be a unique {prefix}* name")
        if set(task) != {"name", "description", "command", "trigger", "timeout_minutes"}:
            raise TaskError(f"{name}: needs exactly name, description, command, trigger, timeout_minutes")
        description = task["description"]
        if not isinstance(description, str) or not description.strip() or "\n" in description or "\r" in description:
            raise TaskError(f"{name}: description must be one non-empty line")
        command = task["command"]
        if not isinstance(command, list) or not command or not all(isinstance(c, str) and c for c in command):
            raise TaskError(f"{name}: command must be a non-empty list of strings")
        if not _whole(task["timeout_minutes"]):
            raise TaskError(f"{name}: timeout_minutes must be a positive whole number")
        trigger = task["trigger"]
        if not isinstance(trigger, dict):
            raise TaskError(f"{name}: trigger must be at-logon-and-every or weekly-at")
        if set(trigger) == {"at_logon_delay_minutes", "every_minutes"}:
            if not all(_whole(trigger[k]) for k in trigger):
                raise TaskError(f"{name}: a logon trigger needs positive whole minutes")
        elif set(trigger) == {"weekly", "at"}:
            if trigger["weekly"] not in DAYS or not re.fullmatch(r"([01]\d|2[0-3]):[0-5]\d", str(trigger["at"])):
                raise TaskError(f"{name}: a weekly trigger needs a day name and an HH:MM time")
        else:
            raise TaskError(f"{name}: trigger must be at-logon-and-every or weekly-at")
        out[name] = task
    return {"folder": folder, "prefix": prefix, "tasks": out}


def load_tasks(path: str) -> dict[str, dict]:
    """The tasks in tasks.json by name, refused unless every entry is well formed."""
    return load_manifest(path)["tasks"]


def resolve(command: list[str]) -> list[str]:
    """`python` is the console interpreter beside this one (Task Scheduler starts pythonw.exe)."""
    if command[0] != "python":
        return command
    here = sys.executable
    console = os.path.join(os.path.dirname(here), "python.exe")
    return [console if os.name == "nt" and os.path.isfile(console) else here] + command[1:]


def _utc(stamp: bool = False) -> str:
    now = datetime.datetime.now(datetime.timezone.utc)
    return now.strftime("%Y%m%dT%H%M%SZ" if stamp else "%Y-%m-%dT%H:%M:%SZ")


def _spawn_options() -> dict:
    """How every task starts: with no console on Windows, and in a process group of its own elsewhere, so a timeout
    stops what the task started too."""
    return {"creationflags": NO_WINDOW} if os.name == "nt" else {"start_new_session": True}


def _kill_tree(proc) -> None:
    """Stop the task and everything it started: taskkill's tree on Windows, the task's process group elsewhere."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True, timeout=60,
                       creationflags=NO_WINDOW)
    else:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:  # the group already exited between the timeout and this kill
            pass
    proc.wait(timeout=60)


def execute(name: str, task: dict, root: str, popen=subprocess.Popen, killer=_kill_tree) -> dict:
    """Run the task's command into a fresh log; returns the outcome recorded in last.json."""
    folder = os.path.join(root, "build", "automations", name)
    os.makedirs(folder, exist_ok=True)
    log = os.path.join(folder, _utc(stamp=True) + ".log")
    outcome = {"name": name, "started": _utc(), "log": os.path.relpath(log, root).replace("\\", "/"),
               "timed_out": False}
    env = {**os.environ, "PYTHONIOENCODING": "utf-8"}
    with open(log, "wb") as out:
        try:
            proc = popen(resolve(task["command"]), cwd=root, stdout=out, stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, env=env, **_spawn_options())
        except OSError as error:  # the command never started: a failed run like any other, so it is alerted
            out.write(f"run.py: could not start {task['command'][0]}: {error}\n".encode("utf-8"))
            proc = None
            outcome["exit"] = 126
        if proc is not None:
            try:
                outcome["exit"] = proc.wait(timeout=task["timeout_minutes"] * 60)
            except subprocess.TimeoutExpired:
                killer(proc)
                outcome.update(exit=124, timed_out=True)
    outcome["finished"] = _utc()
    logs = sorted(f for f in os.listdir(folder) if f.endswith(".log"))
    for old in logs[:-KEEP_LOGS]:
        os.unlink(os.path.join(folder, old))
    tmp = os.path.join(folder, "last.json.tmp")
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(outcome, fh, indent=2)
    os.replace(tmp, os.path.join(folder, "last.json"))
    return outcome


def _quote(arg: str) -> str:
    """One systemd ExecStart argument: quoted, with `%` specifiers, backslashes and quotes escaped."""
    return '"' + arg.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%") + '"'


def systemd_units(manifest: dict, root: str, python: str) -> dict[str, str]:
    """A `<name>.service` and `<name>.timer` per task, each carrying the marker that names this tasks.json.

    The service is a oneshot that runs this file with the task's name (so the failure email comes from here, as on
    Windows) and is stopped five minutes past the task's timeout. A weekly timer is `Persistent=true` with a
    five-minute random delay, as Task Scheduler's start-when-available and random delay are; a logon timer fires
    the delay after the user manager starts and then on the interval.
    """
    marker = MARKER + os.path.join(root, ".conclave", "automations", "tasks.json")
    units: dict[str, str] = {}
    for name, task in manifest["tasks"].items():
        units[f"{name}.service"] = "\n".join([
            marker,
            "[Unit]",
            f"Description={task['description'].replace('%', '%%')}",
            "",
            "[Service]",
            "Type=oneshot",
            f"WorkingDirectory={root.replace('%', '%%')}",
            "ExecStart=" + " ".join(_quote(a) for a in (python, os.path.join(root, ".conclave", "automations", "run.py"),
                                                         name)),
            f"TimeoutStartSec={task['timeout_minutes'] + 5}min",
            "",
        ])
        trigger = task["trigger"]
        if "weekly" in trigger:
            when = [f"OnCalendar={trigger['weekly'][:3]} *-*-* {trigger['at']}:00", "Persistent=true",
                    "RandomizedDelaySec=300"]
        else:
            when = [f"OnStartupSec={trigger['at_logon_delay_minutes']}min",
                    f"OnUnitActiveSec={trigger['every_minutes']}min"]
        units[f"{name}.timer"] = "\n".join([
            marker, "[Unit]", f"Description={task['description'].replace('%', '%%')}", "", "[Timer]", *when, "",
            "[Install]", "WantedBy=timers.target", "",
        ])
    return units


def write_units(out_dir: str, root: str = REPO, python: str | None = None) -> str:
    """Write every task's units into out_dir (refused without a folder); returns the marker line."""
    installable_folder(root)
    manifest = load_manifest(os.path.join(root, ".conclave", "automations", "tasks.json"))
    for filename, text in systemd_units(manifest, root, python or sys.executable).items():
        tmp = os.path.join(out_dir, filename + ".tmp")
        with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
        os.replace(tmp, os.path.join(out_dir, filename))
    return MARKER + os.path.join(root, ".conclave", "automations", "tasks.json")


def _alert_module():
    spec = importlib.util.spec_from_file_location("conclave_automations_alert", os.path.join(HERE, "alert.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def installable_folder(root: str = REPO) -> str:
    """The folder an installer registers into, refused when tasks.json is malformed or names no folder yet."""
    manifest = load_manifest(os.path.join(root, ".conclave", "automations", "tasks.json"))
    if manifest["folder"] is None:
        raise TaskError("tasks.json has no folder yet: set it to \\<Project>\\ before installing")
    return manifest["folder"]


def main(argv: list[str], root: str = REPO, popen=subprocess.Popen, alerter=None, killer=_kill_tree) -> int:
    if argv == ["--check"] or (len(argv) == 2 and argv[0] == "--units"):
        try:
            print(installable_folder(root) if argv[0] == "--check" else write_units(argv[1], root))
        except (TaskError, OSError, ValueError) as error:
            print(f"run: {error}", file=sys.stderr)
            return 2
        return 0
    if len(argv) != 1 or argv[0].startswith("-"):
        print(__doc__.split("\n\n")[0], file=sys.stderr)
        return 2
    manifest = load_manifest(os.path.join(root, ".conclave", "automations", "tasks.json"))
    task = manifest["tasks"].get(argv[0])
    if task is None:
        print(f"run: no task named {argv[0]} in tasks.json", file=sys.stderr)
        return 2
    outcome = execute(argv[0], task, root, popen, killer)
    if outcome["exit"] == 0:
        return 0
    alert = _alert_module()
    try:
        (alerter or alert.send_failure)(argv[0], task, outcome, alert.tail_of(os.path.join(root, outcome["log"])),
                                        manifest["folder"])
    except Exception as error:  # the run already failed; an unsent alert must still show, as exit 3
        with open(os.path.join(root, outcome["log"]), "a", encoding="utf-8") as fh:
            fh.write(f"\nrun.py: alert not sent: {type(error).__name__}: {error}\n")
        return 3
    return outcome["exit"]


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
