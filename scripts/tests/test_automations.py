"""The automations kit: tasks.json, run.py, alert.py, the installers, and the README's task table."""
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

REPO = Path(__file__).resolve().parents[2]
AUTO = REPO / ".conclave" / "automations"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


run = load("conclave_automations_run", AUTO / "run.py")
alert = load("conclave_automations_alert", AUTO / "alert.py")


def readme_rows(text):
    """The first cell of every row in the README's What runs table."""
    section = text.split("## What runs", 1)[1].split("\n## ", 1)[0]
    rows = [line.split("|")[1].strip() for line in section.splitlines() if line.startswith("| `")]
    return [row.strip("`") for row in rows]


def alive(pid):
    """Whether a process still runs (a zombie awaiting its reaper counts as gone)."""
    if os.name == "nt":
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True,
                             creationflags=0x08000000).stdout
        return str(pid) in out
    try:
        with open(f"/proc/{pid}/stat", encoding="utf-8") as fh:
            return fh.read().split(")")[-1].split()[0] != "Z"
    except FileNotFoundError:
        return False


def fixture_task(name, code="print('ok')", trigger=None):
    return {"name": name, "description": f"fixture {name}", "command": [sys.executable, "-c", code],
            "trigger": trigger or {"weekly": "Monday", "at": "07:00"}, "timeout_minutes": 1}


class Manifest(unittest.TestCase):
    def test_the_manifest_loads_and_every_command_script_exists(self):
        tasks = run.load_tasks(str(AUTO / "tasks.json"))
        for name, task in tasks.items():
            with self.subTest(name=name):
                self.assertEqual(task["command"][0], "python")
                self.assertTrue((REPO / task["command"][1]).is_file(), task["command"][1])

    def test_the_readme_lists_exactly_the_manifest(self):
        rows = readme_rows((AUTO / "README.md").read_text(encoding="utf-8"))
        self.assertEqual(sorted(rows), sorted(run.load_tasks(str(AUTO / "tasks.json"))))

    def test_the_installers_read_the_manifest_and_name_no_task(self):
        prefix = run.load_manifest(str(AUTO / "tasks.json"))["prefix"]
        for installer in ("install.ps1", "install.sh"):
            text = (AUTO / installer).read_text(encoding="utf-8")
            with self.subTest(installer=installer):
                self.assertIn("run.py", text)
                if prefix:
                    self.assertNotIn(prefix, text)

    def check(self, doc):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "tasks.json"
            path.write_text(json.dumps(doc), encoding="utf-8")
            return run.load_manifest(str(path))

    def test_the_folder_sets_the_name_prefix(self):
        manifest = self.check({"schema_version": 1, "folder": "\\Example\\",
                               "tasks": [fixture_task("example-reviewers")]})
        self.assertEqual(manifest["prefix"], "example-")
        self.assertEqual(list(manifest["tasks"]), ["example-reviewers"])

    def test_a_fresh_kit_has_no_folder_and_no_tasks(self):
        self.assertEqual(self.check({"schema_version": 1, "folder": None, "tasks": []}),
                         {"folder": None, "prefix": None, "tasks": {}})
        with self.assertRaises(run.TaskError):
            self.check({"schema_version": 1, "folder": None, "tasks": [fixture_task("x-y")]})

    def test_a_malformed_manifest_is_refused(self):
        good = {"schema_version": 1, "folder": "\\Demo\\", "tasks": []}
        first = fixture_task("demo-check")
        cases = {
            "another project's name": {**first, "name": "rizonetech-x"},
            "bare prefix": {**first, "name": "demo-"},
            "extra key": {**first, "enabled": True},
            "empty command": {**first, "command": []},
            "zero timeout": {**first, "timeout_minutes": 0},
            "boolean timeout": {**first, "timeout_minutes": True},
            "boolean logon minutes": {**first, "trigger": {"at_logon_delay_minutes": True, "every_minutes": 5}},
            "name with a trailing newline": {**first, "name": "demo-check\n"},
            "two-line description": {**first, "description": "one\ntwo"},
            "bad day": {**first, "trigger": {"weekly": "Funday", "at": "07:00"}},
            "bad time": {**first, "trigger": {"weekly": "Monday", "at": "7am"}},
            "unknown trigger": {**first, "trigger": {"daily": "07:00"}},
        }
        for label, task in cases.items():
            with self.subTest(label), self.assertRaises(run.TaskError):
                self.check({**good, "tasks": [task]})
        for label, doc in {"duplicate": {**good, "tasks": [first, first]},
                           "nested folder": {**good, "folder": "\\Demo\\Sub\\"},
                           "no slashes": {**good, "folder": "Demo"},
                           "folder with a trailing newline": {**good, "folder": "\\Demo\\\n"},
                           "a task that is not an object": {**good, "tasks": [None]},
                           "a list, not an object": [],
                           "schema 2": {**good, "schema_version": 2}}.items():
            with self.subTest(label), self.assertRaises(run.TaskError):
                self.check(doc)


class Runner(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / ".conclave" / "automations").mkdir(parents=True)
        self.tasks = {"schema_version": 1, "folder": "\\Demo\\", "tasks": [
            fixture_task("demo-pass", "print('all good')"),
            fixture_task("demo-fail", "import sys; print('line one'); print('it broke'); sys.exit(3)"),
        ]}
        self.write()
        self.alerts = []

    def tearDown(self):
        self._tmp.cleanup()

    def write(self):
        (self.root / ".conclave" / "automations" / "tasks.json").write_text(json.dumps(self.tasks), encoding="utf-8")

    def main(self, *argv, **kw):
        kw.setdefault("alerter", lambda *a: self.alerts.append(a))
        return run.main(list(argv), root=str(self.root), **kw)

    def last(self, name):
        return json.loads((self.root / "build" / "automations" / name / "last.json").read_text(encoding="utf-8"))

    def test_a_passing_run_keeps_its_log_and_sends_nothing(self):
        self.assertEqual(self.main("demo-pass"), 0)
        outcome = self.last("demo-pass")
        self.assertEqual(outcome["exit"], 0)
        self.assertIn("all good", (self.root / outcome["log"]).read_text(encoding="utf-8"))
        self.assertEqual(self.alerts, [])

    def test_a_failing_run_alerts_with_its_log_tail_and_folder(self):
        self.assertEqual(self.main("demo-fail"), 3)
        (name, task, outcome, tail, folder), = self.alerts
        self.assertEqual((name, outcome["exit"], task["description"], folder),
                         ("demo-fail", 3, "fixture demo-fail", "\\Demo\\"))
        self.assertIn("it broke", tail)
        self.assertEqual(self.last("demo-fail")["alert"], "sent")
        self.assertNotIn("alert", self.main("demo-pass") or self.last("demo-pass"))

    def test_a_timeout_stops_the_run_and_alerts(self):
        killed = []

        class Hung:
            pid = 0

            def __init__(self, *a, **kw):
                pass

            def wait(self, timeout=None):
                raise subprocess.TimeoutExpired("hung", timeout)

        self.assertEqual(self.main("demo-pass", popen=Hung, killer=killed.append), 124)
        self.assertEqual(len(killed), 1)
        self.assertTrue(self.last("demo-pass")["timed_out"])
        self.assertEqual(len(self.alerts), 1)

    def test_an_unsent_alert_exits_3_and_says_so_in_the_log(self):
        def refuse(*_a):
            raise alert.Refused("vault field credential is unavailable")

        self.tasks["tasks"][1]["command"][-1] = "import sys; sys.exit(5)"
        self.write()
        self.assertEqual(self.main("demo-fail", alerter=refuse), 3)
        log = (self.root / self.last("demo-fail")["log"]).read_text(encoding="utf-8")
        self.assertIn("alert not sent: Refused: vault field credential is unavailable", log)
        self.assertEqual((self.last("demo-fail")["alert"], self.last("demo-fail")["exit"]), ("not sent", 5))

    def test_only_the_newest_logs_are_kept(self):
        folder = self.root / "build" / "automations" / "demo-pass"
        folder.mkdir(parents=True)
        for i in range(run.KEEP_LOGS + 5):
            (folder / f"20000101T0000{i:02d}Z.log").write_text("old", encoding="utf-8")
        self.main("demo-pass")
        self.assertEqual(len(list(folder.glob("*.log"))), run.KEEP_LOGS)
        self.assertTrue((self.root / self.last("demo-pass")["log"]).exists())

    def test_a_command_that_cannot_start_is_recorded_and_alerted(self):
        def cannot_start(*_a, **_kw):
            raise PermissionError(13, "Access is denied")

        self.assertEqual(self.main("demo-pass", popen=cannot_start), 126)
        outcome = self.last("demo-pass")
        self.assertEqual(outcome["exit"], 126)
        self.assertIn("could not start", (self.root / outcome["log"]).read_text(encoding="utf-8"))
        (_name, _task, alerted, tail, _folder), = self.alerts
        self.assertEqual(alerted["exit"], 126)
        self.assertIn("Access is denied", tail)

    def test_every_nested_launch_opens_no_console(self):
        for path in (AUTO / "run.py", AUTO / "alert.py"):
            text = path.read_text(encoding="utf-8")
            calls = text.count("subprocess.run([") + text.count("subprocess.Popen(") + text.count("run(command")
            with self.subTest(path=path.name):
                self.assertGreaterEqual(text.count("creationflags="), calls, path.name)
        self.assertIn("**_spawn_options()", (AUTO / "run.py").read_text(encoding="utf-8"))
        self.assertEqual(run._spawn_options(), {"creationflags": run.NO_WINDOW} if os.name == "nt"
                         else {"start_new_session": True})

    def test_a_timeout_kill_stops_what_the_task_started(self):
        # The parent starts a child that writes its pid, then both hang; the kill must end the child too.
        pid_file = self.root / "child.pid"
        child = f"import os, time; open({str(pid_file)!r}, 'w').write(str(os.getpid())); time.sleep(120)"
        parent = f"import subprocess, sys, time; subprocess.Popen([sys.executable, '-c', {child!r}]); time.sleep(120)"
        proc = subprocess.Popen([sys.executable, "-c", parent], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                **run._spawn_options())
        try:
            for _ in range(200):
                if pid_file.exists() and pid_file.read_text(encoding="utf-8"):
                    break
                time.sleep(0.05)
            child_pid = int(pid_file.read_text(encoding="utf-8"))
        finally:
            run._kill_tree(proc)
        self.assertIsNotNone(proc.returncode)
        for _ in range(100):
            if not alive(child_pid):
                break
            time.sleep(0.05)
        self.assertFalse(alive(child_pid), "the task's child survived the timeout kill")

    def test_an_unknown_task_or_bad_arguments_are_refused(self):
        self.assertEqual(self.main("demo-missing"), 2)
        self.assertEqual(self.main(), 2)
        self.assertEqual(self.main("--units"), 2)

    def test_check_prints_the_folder_or_refuses_a_fresh_kit(self):
        out = io.StringIO()
        sys.stdout, saved = out, sys.stdout
        try:
            self.assertEqual(self.main("--check"), 0)
        finally:
            sys.stdout = saved
        self.assertEqual(out.getvalue().strip(), "\\Demo\\")
        self.tasks = {"schema_version": 1, "folder": None, "tasks": []}
        self.write()
        err = io.StringIO()
        sys.stderr, saved = err, sys.stderr
        try:
            self.assertEqual(self.main("--check"), 2)
        finally:
            sys.stderr = saved
        self.assertIn("no folder yet", err.getvalue())


class Units(unittest.TestCase):
    MANIFEST = {"folder": "\\Demo\\", "prefix": "demo-", "tasks": {
        "demo-weekly": fixture_task("demo-weekly", trigger={"weekly": "Wednesday", "at": "07:15"}),
        "demo-logon": fixture_task("demo-logon", trigger={"at_logon_delay_minutes": 3, "every_minutes": 60}),
    }}

    def units(self, root="/home/op/My 100% repo"):
        return run.systemd_units(self.MANIFEST, root, "/usr/bin/python3")

    def test_a_service_and_a_timer_per_task_each_marked_with_this_tasks_json(self):
        units = self.units()
        self.assertEqual(sorted(units), ["demo-logon.service", "demo-logon.timer", "demo-weekly.service",
                                         "demo-weekly.timer"])
        marker = run.MARKER + os.path.join("/home/op/My 100% repo", ".conclave", "automations", "tasks.json")
        for name, text in units.items():
            with self.subTest(name=name):
                self.assertEqual(text.splitlines()[0], marker)

    def test_a_specifier_in_a_description_stays_literal_in_both_units(self):
        task = {**fixture_task("demo-pct"), "description": "Disk at 90% on %n"}
        units = run.systemd_units({"folder": "\\Demo\\", "prefix": "demo-", "tasks": {"demo-pct": task}},
                                  "/home/op/repo", "/usr/bin/python3")
        for name in ("demo-pct.service", "demo-pct.timer"):
            with self.subTest(name=name):
                self.assertIn("Description=Disk at 90%% on %%n\n", units[name])

    def test_the_service_runs_this_runner_with_the_name_and_escapes_specifiers(self):
        service = self.units()["demo-weekly.service"]
        self.assertIn("Type=oneshot", service)
        self.assertIn("WorkingDirectory=/home/op/My 100%% repo", service)
        self.assertIn('ExecStart="/usr/bin/python3" "', service)
        self.assertIn('100%% repo', service.split("ExecStart=", 1)[1].splitlines()[0])
        self.assertTrue(service.split("ExecStart=", 1)[1].splitlines()[0].endswith('run.py" "demo-weekly"'))
        self.assertIn("TimeoutStartSec=6min", service)
        self.assertNotIn("[Install]", service)

    def test_a_weekly_timer_is_persistent_and_a_logon_timer_repeats(self):
        weekly, logon = self.units()["demo-weekly.timer"], self.units()["demo-logon.timer"]
        for line in ("OnCalendar=Wed *-*-* 07:15:00", "Persistent=true", "RandomizedDelaySec=300",
                     "WantedBy=timers.target"):
            self.assertIn(line, weekly)
        for line in ("OnStartupSec=3min", "OnUnitActiveSec=60min", "WantedBy=timers.target"):
            self.assertIn(line, logon)

    def test_write_units_refuses_a_fresh_kit_and_writes_the_rest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, out = Path(tmp) / "repo", Path(tmp) / "out"
            (root / ".conclave" / "automations").mkdir(parents=True)
            out.mkdir()
            manifest = root / ".conclave" / "automations" / "tasks.json"
            manifest.write_text(json.dumps({"schema_version": 1, "folder": None, "tasks": []}), encoding="utf-8")
            with self.assertRaises(run.TaskError):
                run.write_units(str(out), str(root))
            manifest.write_text(json.dumps({"schema_version": 1, "folder": "\\Demo\\",
                                            "tasks": [fixture_task("demo-one")]}), encoding="utf-8")
            marker = run.write_units(str(out), str(root), "/usr/bin/python3")
            self.assertEqual(sorted(p.name for p in out.iterdir()), ["demo-one.service", "demo-one.timer"])
            self.assertEqual((out / "demo-one.timer").read_text(encoding="utf-8").splitlines()[0], marker)


class Alert(unittest.TestCase):
    TASK = {"description": "Check the packages", "command": ["python", "scripts/check-packages.py"]}
    OUTCOME = {"exit": 1, "started": "2026-10-19T05:15:00Z", "finished": "2026-10-19T05:16:00Z",
               "log": "build/automations/demo-packages/x.log", "timed_out": False}

    def test_the_message_names_the_task_its_exit_its_log_tail_and_the_rerun(self):
        subject, body = alert.message("demo-packages", self.TASK, self.OUTCOME, "pkg 1.0 High", "PC", "\\Demo\\",
                                      windows=True)
        self.assertEqual(subject, "Automation failed: demo-packages on PC")
        for text in ("exit 1", "Check the packages", "pkg 1.0 High",
                     "Start-ScheduledTask -TaskPath '\\Demo\\' -TaskName demo-packages",
                     r"Or directly: build\python.cmd .conclave/automations/run.py demo-packages"):
            self.assertIn(text, body)
        _, linux = alert.message("demo-packages", self.TASK, self.OUTCOME, "", "PC", "\\Demo\\", windows=False)
        self.assertIn("systemctl --user start demo-packages.service", linux)
        self.assertIn("Or directly: ./scripts/run-python.sh .conclave/automations/run.py demo-packages", linux)
        _, timed = alert.message("x", self.TASK, {**self.OUTCOME, "timed_out": True}, "", "PC", "\\Demo\\")
        self.assertIn("timed out", timed)

    def test_the_sender_is_named_for_the_folder(self):
        self.assertEqual(alert.sender("\\Isotone\\"), "Isotone automations <website@rizonetech.com>")

    def test_a_send_smtp2go_refused_is_a_refusal(self):
        class Response(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        sent = []

        def opener(payload):
            return lambda request, **kw: sent.append(json.loads(request.data)) or Response(payload)

        refused = json.dumps({"data": {"succeeded": 0, "failed": 1, "failures": ["bad sender"]}}).encode()
        with self.assertRaises(alert.Refused):
            alert.send("api-" + "a" * 32, "s", "b", "Demo automations <x@y>", opener=opener(refused))
        ok = json.dumps({"data": {"succeeded": 1, "failed": 0, "email_id": "E1"}}).encode()
        self.assertEqual(alert.send("api-" + "a" * 32, "s", "b", "Demo automations <x@y>", opener=opener(ok)), "E1")
        self.assertEqual(sent[-1]["sender"], "Demo automations <x@y>")

    def test_a_key_without_the_smtp2go_shape_is_refused(self):
        def vault(value, code=0):
            return lambda *a, **kw: subprocess.CompletedProcess(a, code, stdout=value, stderr="")

        self.assertEqual(alert.key(vault("api-" + "B" * 32 + "\n")), "api-" + "B" * 32)
        for bad, code in (("", 0), ("api-short", 0), ("x" * 36, 0), ("api-" + "B" * 32, 1)):
            with self.subTest(bad=bad[:8], code=code), self.assertRaises(alert.Refused):
                alert.key(vault(bad, code))

    def test_the_key_is_read_through_wsl_on_windows_and_directly_elsewhere(self):
        seen = []

        def vault(command, **kw):
            seen.append((command[0], kw["creationflags"]))
            return subprocess.CompletedProcess(command, 0, stdout="api-" + "C" * 32, stderr="")

        alert.key(vault, windows=True)
        alert.key(vault, windows=False)
        self.assertEqual(seen, [("wsl.exe", 0x08000000), ("bash", 0)])

    def test_a_dry_run_reads_no_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / ".conclave" / "automations").mkdir(parents=True)
            (root / ".conclave" / "automations" / "tasks.json").write_text(json.dumps(
                {"schema_version": 1, "folder": "\\Demo\\", "tasks": [fixture_task("demo-ga")]}), encoding="utf-8")

            def no_vault(*_a, **_kw):
                raise AssertionError("a dry run must not read the vault")

            out = io.StringIO()
            sys.stdout, saved = out, sys.stdout
            try:
                code = alert.main(["demo-ga", "--dry-run"], run=no_vault, host="PC", root=str(root))
            finally:
                sys.stdout = saved
            self.assertEqual(code, 0)
            self.assertIn("From: Demo automations <website@rizonetech.com>", out.getvalue())
            self.assertIn("Subject: Automation failed: demo-ga on PC", out.getvalue())


@unittest.skipUnless(shutil.which("bash") and os.name != "nt", "install.sh runs under bash on Linux and WSL")
class InstallSh(unittest.TestCase):
    """install.sh against a fake systemctl: units land with the marker, stale marked units go, others stay."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        tmp = Path(self._tmp.name)
        self.root, self.config, self.bin = tmp / "repo", tmp / "config", tmp / "bin"
        auto = self.root / ".conclave" / "automations"
        auto.mkdir(parents=True)
        for name in ("run.py", "alert.py", "install.sh"):
            shutil.copy(AUTO / name, auto / name)
        self.manifest = auto / "tasks.json"
        self.bin.mkdir()
        self.calls = tmp / "systemctl.log"
        fake = self.bin / "systemctl"
        fake.write_text(f'#!/bin/sh\necho "$*" >> "{self.calls}"\n', encoding="utf-8")
        fake.chmod(0o755)

    def tearDown(self):
        self._tmp.cleanup()

    def install(self, *args, tasks):
        self.manifest.write_text(json.dumps({"schema_version": 1, "folder": "\\Demo\\", "tasks": tasks}),
                                 encoding="utf-8")
        env = {**os.environ, "XDG_CONFIG_HOME": str(self.config), "PATH": f"{self.bin}:{os.environ['PATH']}"}
        return subprocess.run(["bash", str(self.root / ".conclave/automations/install.sh"), *args], env=env,
                              capture_output=True, text=True, timeout=60)

    def test_install_replaces_only_this_repositorys_units(self):
        units = self.config / "systemd" / "user"
        units.mkdir(parents=True)
        (units / "demo-handmade.service").write_text("[Unit]\n", encoding="utf-8")
        result = self.install(tasks=[fixture_task("demo-one"), fixture_task("demo-two")])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((units / "demo-two.timer").is_file())
        result = self.install(tasks=[fixture_task("demo-one")])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in units.iterdir()),
                         ["demo-handmade.service", "demo-one.service", "demo-one.timer"])
        self.assertIn("disable --now -- demo-two.timer", self.calls.read_text(encoding="utf-8"))
        result = self.install("--uninstall", tasks=[fixture_task("demo-one")])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([p.name for p in units.iterdir()], ["demo-handmade.service"])

    def test_a_unit_this_repository_did_not_write_is_never_replaced(self):
        units = self.config / "systemd" / "user"
        units.mkdir(parents=True)
        live = Path(self._tmp.name) / "live-checkout" / ".conclave" / "automations"
        live.mkdir(parents=True)
        (live / "tasks.json").write_text("{}", encoding="utf-8")
        for label, text in (("unmarked", "[Unit]\nDescription=hand-written\n"),
                            ("another live checkout", run.MARKER + str(live / "tasks.json") + "\n[Unit]\n")):
            with self.subTest(label):
                (units / "demo-one.service").write_text(text, encoding="utf-8")
                result = self.install(tasks=[fixture_task("demo-one"), fixture_task("demo-two")])
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Refusing to replace", result.stderr)
                self.assertEqual((units / "demo-one.service").read_text(encoding="utf-8"), text)
                self.assertEqual(sorted(p.name for p in units.iterdir()), ["demo-one.service"])
                result = self.install("--uninstall", tasks=[])
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((units / "demo-one.service").read_text(encoding="utf-8"), text)

    def test_a_moved_clone_s_own_units_are_adopted(self):
        units = self.config / "systemd" / "user"
        units.mkdir(parents=True)
        moved = run.MARKER + "/home/op/old-place/.conclave/automations/tasks.json"
        (units / "demo-one.service").write_text(moved + "\n[Unit]\nDescription=old\n", encoding="utf-8")
        (units / "demo-one.timer").write_text(moved + "\n[Timer]\n", encoding="utf-8")
        result = self.install(tasks=[fixture_task("demo-one")])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(sorted(p.name for p in units.iterdir()), ["demo-one.service", "demo-one.timer"])
        for name in ("demo-one.service", "demo-one.timer"):
            self.assertEqual((units / name).read_text(encoding="utf-8").splitlines()[0],
                             run.MARKER + str(self.root / ".conclave" / "automations" / "tasks.json"))

    def test_check_reads_the_units_and_the_timers_through_systemctl(self):
        result = self.install(tasks=[fixture_task("demo-one")])
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.bin / "systemctl").write_text(
            f'#!/bin/sh\necho "$*" >> "{self.calls}"\n'
            'case "$2" in is-enabled) echo enabled ;; is-active) echo active ;; '
            'show) echo Result=success; echo ExecMainStatus=0 ;; esac\n', encoding="utf-8")
        env = {**os.environ, "XDG_CONFIG_HOME": str(self.config), "PATH": f"{self.bin}:{os.environ['PATH']}"}
        check = subprocess.run(["bash", str(self.root / ".conclave/automations/install.sh"), "--check"], env=env,
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
        self.assertIn("automations: 1 task(s) registered for this clone", check.stdout)

    def check_with(self, script):
        """install.sh --check with a systemctl first on PATH that answers by `script` (a sh case body)."""
        (self.bin / "systemctl").write_text(f'#!/bin/sh\necho "$*" >> "{self.calls}"\n{script}\n', encoding="utf-8")
        env = {**os.environ, "XDG_CONFIG_HOME": str(self.config), "PATH": f"{self.bin}:{os.environ['PATH']}"}
        return subprocess.run(["bash", str(self.root / ".conclave/automations/install.sh"), "--check"], env=env,
                              capture_output=True, text=True, timeout=60)

    def test_check_names_each_state_through_systemctl_on_path(self):
        healthy = ('case "$2" in is-enabled) echo enabled ;; is-active) echo active ;; '
                   'show) echo Result=success; echo ExecMainStatus=0 ;; esac')
        self.assertEqual(self.install(tasks=[fixture_task("demo-one")]).returncode, 0)
        units = self.config / "systemd" / "user"
        service = units / "demo-one.service"
        body = service.read_text(encoding="utf-8").split("\n", 1)[1]
        inactive = healthy.replace("is-active) echo active", "is-active) echo inactive")
        done = self.check_with(inactive)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("disabled: demo-one: its timer is not active", done.stdout)
        live = Path(self._tmp.name) / "live" / ".conclave" / "automations"
        live.mkdir(parents=True)
        (live / "tasks.json").write_text("{}", encoding="utf-8")
        for want, marker in (("other-clone", str(live / "tasks.json")),
                             ("stale", "/home/op/gone/.conclave/automations/tasks.json")):
            with self.subTest(want):
                service.write_text(run.MARKER + marker + "\n" + body, encoding="utf-8")
                done = self.check_with(healthy)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn(f"{want}: demo-one: registered for ", done.stdout)
                self.assertIn("next: bash .conclave/automations/install.sh", done.stdout)
        done = self.check_with('echo "Failed to connect to bus: No medium found" >&2; exit 1')
        self.assertEqual(done.returncode, 3, done.stdout + done.stderr)
        self.assertIn("unknown: ", done.stdout)
        self.assertIn("Failed to connect to bus", done.stdout)

    def test_a_refused_manifest_installs_nothing(self):
        result = self.install(tasks=[fixture_task("other-name")])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.config / "systemd" / "user").exists())



def demo_root(base, folder="\\Demo\\", names=("demo-one", "demo-two")):
    """A clone whose tasks.json lists `names` in `folder`, with the automation kit copied in."""
    auto = Path(base) / ".conclave" / "automations"
    auto.mkdir(parents=True)
    for name in ("run.py", "alert.py", "install.ps1", "install.sh"):
        shutil.copy(AUTO / name, auto / name)
    (auto / "tasks.json").write_text(json.dumps({"schema_version": 1, "folder": folder,
                                                 "tasks": [fixture_task(n) for n in names]}), encoding="utf-8")
    return str(Path(base).resolve())


class Registrations(unittest.TestCase):
    """registration_report and --check-installed, from the scheduler's records as data (D00 T04 section 14)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        base = Path(self._tmp.name)
        self.root = demo_root(base / "clone")
        self.other = demo_root(base / "other-clone")
        self.tools = base / "tools"
        (self.tools / "cpython" / "current").mkdir(parents=True)
        self.pythonw = str(self.tools / "cpython" / "current" / "pythonw.exe")
        Path(self.pythonw).write_text("", encoding="utf-8")
        self.old_python = str(base / "Python314" / "pythonw.exe")  # an interpreter that is no longer there
        self.env = mock_env(RECTO_TOOLS_DIR=str(self.tools))
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self._tmp.cleanup()

    def record(self, name, root=None, **overrides):
        root = root or self.root
        rec = {"name": name, "script": os.path.join(root, ".conclave", "automations", "run.py"), "workdir": root,
               "executable": self.pythonw, "enabled": True, "last_result": 0}
        rec.update(overrides)
        return rec

    def states(self, records, platform="windows"):
        tasks = {"demo-one": {}, "demo-two": {}}
        return {name: state for state, name, _ in run.registration_report(tasks, records, self.root, platform)}

    def test_each_state_from_data(self):
        gone = os.path.join(self._tmp.name, "moved-away")
        cases = {
            "ok": self.record("demo-one"),
            "stale": self.record("demo-one", root=gone),
            "other-clone": self.record("demo-one", root=self.other),
            "disabled": self.record("demo-one", enabled=False),
            "failing": self.record("demo-one", last_result=0x8007010B),
        }
        for want, rec in cases.items():
            with self.subTest(want):
                self.assertEqual(self.states([rec, self.record("demo-two")])["demo-one"], want)
        self.assertEqual(self.states([self.record("demo-two")])["demo-one"], "missing")

    def test_an_obsolete_executable_with_correct_paths_and_a_good_result_is_stale(self):
        self.assertEqual(self.states([self.record("demo-one", executable=self.old_python),
                                      self.record("demo-two")])["demo-one"], "stale")
        # The pinned interpreter named through RECTO_TOOLS_DIR must also exist.
        os.unlink(self.pythonw)
        self.assertEqual(self.states([self.record("demo-one"), self.record("demo-two")])["demo-one"], "stale")

    def test_scheduler_status_codes_and_the_runner_s_own_exits_are_ok(self):
        for result in (0, 0x41300, 0x41301, 0x41303, 0x41306, 1, 3, 124):
            with self.subTest(f"{result:#x}"):
                self.assertEqual(self.states([self.record("demo-one", last_result=result),
                                              self.record("demo-two")])["demo-one"], "ok")
        for result in (0x8007010B, 0x80070002, 0x800710E0, 0x80041323, 0xC000013A, -2147024629):
            with self.subTest(f"{result:#x}"):  # the last is 0x8007010B signed, as PowerShell may print it
                self.assertTrue(run.launch_failed(result))
        # A scheduler-native launch failure on an otherwise valid registration is failing, not ok.
        self.assertEqual(self.states([self.record("demo-one", last_result=0x80041323),
                                      self.record("demo-two")])["demo-one"], "failing")

    def test_a_systemd_timer_that_is_missing_disabled_or_inactive_is_disabled(self):
        for timer in ({"present": False, "enabled": False, "active": False},
                      {"present": True, "enabled": False, "active": True},
                      {"present": True, "enabled": True, "active": False}):
            with self.subTest(str(timer)):
                rec = self.record("demo-one", executable=sys.executable, timer=timer)
                rec.pop("enabled")
                rec.pop("last_result")
                two = self.record("demo-two", executable=sys.executable,
                                  timer={"present": True, "enabled": True, "active": True})
                self.assertEqual(self.states([rec, two], platform="systemd")["demo-one"], "disabled")

    def check(self, records=None, raises=None, *flags):
        def reader(tasks):
            if raises:
                raise run.SchedulerUnreadable(raises)
            return records
        out = io.StringIO()
        saved, sys.stdout = sys.stdout, out
        try:
            code = run.main(["--check-installed", *flags], root=self.root, reader=reader)
        finally:
            sys.stdout = saved
        return code, out.getvalue()

    def test_check_installed_says_each_line_and_exit(self):
        good = [self.record("demo-one"), self.record("demo-two")]
        with mock.patch.object(run, "_platform", return_value="windows"):
            self.assertEqual(self.check(good), (0, "automations: 2 task(s) registered for this clone\n"))
            code, said = self.check([self.record("demo-one", root=os.path.join(self._tmp.name, "gone"))])
            self.assertEqual(code, 1)
            self.assertIn("stale: demo-one: registered for ", said)
            self.assertIn("missing: demo-two: not registered", said)
            self.assertTrue(said.rstrip().endswith("next: pwsh .conclave/automations/install.ps1"))
            code, said = self.check(raises="Access is denied")
            self.assertEqual(code, 3)
            self.assertEqual(said, "unknown: Access is denied\nnext: pwsh .conclave/automations/install.ps1\n")
        with mock.patch.object(run, "_platform", return_value="systemd"):
            code, said = self.check(raises="systemctl --user is-enabled answered nothing")
            self.assertEqual(code, 3)
            self.assertIn("next: bash .conclave/automations/install.sh", said)

    def test_the_hook_modes_always_exit_0_and_agy_gets_inject_steps(self):
        with mock.patch.object(run, "_platform", return_value="windows"):
            code, said = self.check([], None, "--hook")
            self.assertEqual(code, 0)
            self.assertIn("missing: demo-one", said)
            code, said = self.check(None, "Access is denied", "--agy-hook")
            self.assertEqual(code, 0)
            message = json.loads(said)["injectSteps"][0]["systemMessage"]["systemMessage"]
            self.assertEqual(message, "unknown: Access is denied\nnext: pwsh .conclave/automations/install.ps1")


def mock_env(**values):
    return mock.patch.dict(os.environ, values)


class SystemdReader(unittest.TestCase):
    """read_systemd: the unit folder and `systemctl --user`, through a fake systemctl."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = demo_root(Path(self._tmp.name) / "clone")
        self.units = Path(self._tmp.name) / "units"
        self.units.mkdir()
        manifest = run.load_manifest(os.path.join(self.root, ".conclave", "automations", "tasks.json"))
        for name, text in run.systemd_units(manifest, self.root, sys.executable).items():
            (self.units / name).write_text(text, encoding="utf-8")
        self.tasks = manifest["tasks"]

    def tearDown(self):
        self._tmp.cleanup()

    def systemctl(self, answers):
        def fake(args, **_):
            verb, unit = args[2], args[3]
            if verb == "show":
                said = answers.get((verb, unit), "Result=success\nExecMainStatus=0\nExecMainCode=exited")
                return subprocess.CompletedProcess(args, 0, said + "\n", "")
            said = answers.get((verb, unit), "enabled" if verb == "is-enabled" else "active")
            return subprocess.CompletedProcess(args, 0 if said in ("enabled", "active") else 1, said + "\n", "")
        return fake

    def states(self, answers=None):
        records = run.read_systemd(self.tasks, str(self.units), runner=self.systemctl(answers or {}))
        return {n: s for s, n, _ in run.registration_report(self.tasks, records, self.root, "systemd")}

    def test_healthy_units_are_ok_with_paths_read_back_from_the_unit(self):
        records = run.read_systemd(self.tasks, str(self.units), runner=self.systemctl({}))
        self.assertEqual(records[0]["script"], os.path.join(self.root, ".conclave", "automations", "run.py"))
        self.assertEqual(records[0]["executable"], sys.executable)
        self.assertEqual(self.states(), {"demo-one": "ok", "demo-two": "ok"})

    def test_an_inactive_timer_a_missing_timer_and_a_moved_or_other_clone(self):
        self.assertEqual(self.states({("is-active", "demo-one.timer"): "inactive"})["demo-one"], "disabled")
        (self.units / "demo-two.timer").unlink()
        self.assertEqual(self.states()["demo-two"], "disabled")
        moved = os.path.join(self._tmp.name, "moved")
        other = demo_root(Path(self._tmp.name) / "other")
        for where, want in ((moved, "stale"), (other, "other-clone")):
            with self.subTest(want):
                manifest = run.load_manifest(os.path.join(self.root, ".conclave", "automations", "tasks.json"))
                text = run.systemd_units(manifest, where, sys.executable)["demo-one.service"]
                (self.units / "demo-one.service").write_text(text, encoding="utf-8")
                self.assertEqual(self.states()["demo-one"], want)

    def test_a_service_that_systemd_could_not_start_is_failing_and_the_runner_s_own_exit_is_ok(self):
        noexec = ("show", "demo-one.service")
        self.assertEqual(self.states({noexec: "Result=exit-code\nExecMainStatus=203\nExecMainCode=exited"})["demo-one"],
                         "failing")
        self.assertEqual(self.states({noexec: "Result=start-limit-hit\nExecMainStatus=0"})["demo-one"], "failing")
        self.assertEqual(self.states({noexec: "Result=exit-code\nExecMainStatus=1\nExecMainCode=exited"})["demo-one"],
                         "ok")

    def test_an_unreadable_unit_is_unknown_and_the_hook_still_succeeds(self):
        real_open = open

        def refusing(path, *a, **k):
            if str(path).endswith("demo-one.service"):
                raise PermissionError(13, "Permission denied", str(path))
            return real_open(path, *a, **k)
        with mock.patch("builtins.open", refusing):
            with self.assertRaises(run.SchedulerUnreadable) as caught:
                run.read_systemd(self.tasks, str(self.units), runner=self.systemctl({}))
        self.assertIn("Permission denied", str(caught.exception))
        out = io.StringIO()
        saved, sys.stdout = sys.stdout, out
        try:
            with mock.patch.object(run, "_platform", return_value="systemd"), \
                    mock.patch.object(run, "read_systemd", side_effect=run.SchedulerUnreadable("unit cannot be read")):
                code = run.main(["--check-installed", "--agy-hook"], root=self.root)
        finally:
            sys.stdout = saved
        self.assertEqual(code, 0)
        self.assertIn("unknown: unit cannot be read", json.loads(out.getvalue())["injectSteps"][0]["systemMessage"]
                      ["systemMessage"])

    def test_a_slow_systemctl_spends_one_deadline_and_reads_unknown(self):
        now = [0.0]

        def slow(args, **kwargs):
            now[0] += 4  # every call answers, slowly
            self.assertLessEqual(kwargs["timeout"], run.DEADLINE_SECONDS)
            return self.systemctl({})(args)
        with self.assertRaises(run.SchedulerUnreadable) as caught:
            run.read_systemd(self.tasks, str(self.units), runner=slow, deadline=run.Deadline(clock=lambda: now[0]))
        self.assertIn(f"did not answer within {run.DEADLINE_SECONDS} seconds", str(caught.exception))
        self.assertLessEqual(now[0], run.DEADLINE_SECONDS + 4)

    def test_ownership_is_read_from_the_marker_as_install_sh_reads_it(self):
        text = (self.units / "demo-one.service").read_text(encoding="utf-8")
        body = text.split("\n", 1)[1]
        live = demo_root(Path(self._tmp.name) / "live")
        cases = {"other-clone": run.MARKER + os.path.join(live, ".conclave", "automations", "tasks.json") + "\n" + body,
                 "stale": run.MARKER + os.path.join(self._tmp.name, "gone", ".conclave", "automations", "tasks.json")
                          + "\n" + body,
                 "unowned": body}
        for want, unit in cases.items():
            with self.subTest(want):
                (self.units / "demo-one.service").write_text(unit, encoding="utf-8")
                self.assertEqual(self.states()["demo-one"], want)

    def test_a_user_bus_that_cannot_be_reached_is_unknown(self):
        def silent(args, **_):
            return subprocess.CompletedProcess(args, 1, "", "Failed to connect to bus: No medium found\n")
        with self.assertRaises(run.SchedulerUnreadable) as caught:
            run.read_systemd(self.tasks, str(self.units), runner=silent)
        self.assertIn("Failed to connect to bus", str(caught.exception))


PWSH = shutil.which("pwsh")


@unittest.skipUnless(os.name == "nt" and PWSH, "Task Scheduler and PowerShell 7 on Windows")
class TaskSchedulerAdapter(unittest.TestCase):
    """install.ps1 -Dump and -Check against this host's Task Scheduler, in a throwaway folder removed afterwards;
    then the repair (install.ps1) read back, and both writers' session hooks run as their files spell them."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        stamp = f"{os.getpid()}{int(time.time()) % 100000}"
        self.folder_name = f"RectoTest{stamp}"
        self.prefix = self.folder_name.lower() + "-"
        self.names = [self.prefix + "ok", self.prefix + "moved", self.prefix + "off", self.prefix + "absent"]
        self.root = demo_root(Path(self._tmp.name) / "clone", folder=f"\\{self.folder_name}\\", names=self.names)
        self.pythonw = str(REPO / ".tools" / "cpython" / "current" / "pythonw.exe")
        self.addCleanup(self.remove_folder)

    def tearDown(self):
        self._tmp.cleanup()

    def ps(self, script, env=None):
        return subprocess.run([str(PWSH), "-NoProfile", "-NonInteractive", "-Command", script], capture_output=True,
                              text=True, timeout=180, env=env, creationflags=0x08000000)

    def remove_folder(self):
        self.ps(f"Get-ScheduledTask -TaskPath '\\{self.folder_name}\\' -ErrorAction Ignore | "
                "Unregister-ScheduledTask -Confirm:$false; "
                "$s = New-Object -ComObject Schedule.Service; $s.Connect(); "
                f"try {{ $s.GetFolder('\\').DeleteFolder('{self.folder_name}', 0) }} catch {{}}")

    def register(self, name, root, enabled=True):
        runner = os.path.join(root, ".conclave", "automations", "run.py")
        disable = "" if enabled else f"; Disable-ScheduledTask -TaskPath '\\{self.folder_name}\\' -TaskName '{name}' | Out-Null"
        done = self.ps(f"$a = New-ScheduledTaskAction -Execute '{self.pythonw}' -Argument '\"{runner}\" {name}' "
                       f"-WorkingDirectory '{root}'; $t = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Monday -At 07:00; "
                       f"Register-ScheduledTask -TaskPath '\\{self.folder_name}\\' -TaskName '{name}' -Action $a "
                       f"-Trigger $t -Force | Out-Null{disable}")
        self.assertEqual(done.returncode, 0, done.stderr)

    def check(self, env=None):
        return subprocess.run([sys.executable, os.path.join(self.root, ".conclave", "automations", "run.py"),
                               "--check-installed"], capture_output=True, text=True, timeout=120, env=env,
                              creationflags=0x08000000)

    def test_the_real_scheduler_read_back_then_repaired(self):
        moved = os.path.join(self._tmp.name, "moved-away")
        self.register(self.prefix + "ok", self.root)
        self.register(self.prefix + "moved", moved)
        self.register(self.prefix + "off", self.root, enabled=False)
        env = {k: v for k, v in os.environ.items() if k != "RECTO_TOOLS_DIR"}
        done = self.check(env)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        lines = done.stdout.splitlines()
        self.assertIn(f"stale: {self.prefix}moved: registered for {moved}, this clone is {self.root}", lines)
        self.assertIn(f"disabled: {self.prefix}off: the task is disabled", lines)
        self.assertIn(f"missing: {self.prefix}absent: not registered", lines)
        self.assertNotIn(f"{self.prefix}ok:", done.stdout)
        self.assertEqual(lines[-1], "next: pwsh .conclave/automations/install.ps1")
        self.assertEqual(len(lines), 4)  # the three tasks to repair and the next line

        # Both writers' session hooks, run as their files spell them, show it and never fail.
        self.run_hooks(env)

        # The repair: install.ps1 over the stale, disabled and missing registrations, then the check again.
        env_tools = {**env, "RECTO_TOOLS_DIR": str(REPO / ".tools")}
        installed = self.ps(f"& '{os.path.join(self.root, '.conclave', 'automations', 'install.ps1')}'", env=env_tools)
        self.assertEqual(installed.returncode, 0, installed.stdout + installed.stderr)
        listed = self.ps(f"@(Get-ScheduledTask -TaskPath '\\{self.folder_name}\\').TaskName -join ','")
        self.assertEqual(sorted(listed.stdout.strip().split(",")), sorted(self.names))
        done = self.check(env_tools)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(done.stdout.strip(), f"automations: {len(self.names)} task(s) registered for this clone")

    def run_hooks(self, env):
        sys.path.insert(0, str(REPO / "scripts"))
        import campaign_guard  # the Git Bash Claude Code runs hooks through
        claude = json.loads((REPO / ".claude" / "settings.json").read_text(encoding="utf-8"))
        command = next(h["command"] for h in claude["hooks"]["SessionStart"][0]["hooks"]
                       if "--check-installed" in h["command"])
        done = subprocess.run([campaign_guard.git_bash(), "-c", command], capture_output=True, text=True, timeout=60,
                              env={**env, "CLAUDE_PROJECT_DIR": self.root.replace("\\", "/")},
                              creationflags=0x08000000)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn(f"stale: {self.prefix}moved:", done.stdout)
        self.assertIn("next: pwsh .conclave/automations/install.ps1", done.stdout)
        unreadable = {**env, "CLAUDE_PROJECT_DIR": self.root.replace("\\", "/"),
                      "CONCLAVE_SCHEDULER_RECORDS": os.path.join(self._tmp.name, "no-such-records.json")}
        done = subprocess.run([campaign_guard.git_bash(), "-c", command], capture_output=True, text=True, timeout=60,
                              env=unreadable, creationflags=0x08000000)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("unknown: CONCLAVE_SCHEDULER_RECORDS cannot be read", done.stdout)
        # Antigravity: the system shell from .agents/ in a git repository with scripts/run-python.sh.
        scripts = Path(self.root) / "scripts"
        scripts.mkdir()
        shutil.copy(REPO / "scripts" / "run-python.sh", scripts / "run-python.sh")
        (Path(self.root) / ".agents").mkdir()
        subprocess.run(["git", "init", "-q", self.root], check=True, timeout=60, creationflags=0x08000000)
        agy = json.loads((REPO / ".agents" / "hooks.json").read_text(encoding="utf-8"))
        command = next(h["command"] for h in agy["recto-campaign"]["SessionStart"] if "--check-installed" in h["command"])
        done = subprocess.run(command, shell=True, cwd=os.path.join(self.root, ".agents"), capture_output=True,
                              text=True, timeout=60, env=env, creationflags=0x08000000)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        message = json.loads(done.stdout)["injectSteps"][0]["systemMessage"]["systemMessage"]
        self.assertIn(f"stale: {self.prefix}moved:", message)
        self.assertIn("next: pwsh .conclave/automations/install.ps1", message)
        done = subprocess.run(command, shell=True, cwd=os.path.join(self.root, ".agents"), capture_output=True,
                              text=True, timeout=60, creationflags=0x08000000,
                              env={**env, "CONCLAVE_SCHEDULER_RECORDS": os.path.join(self._tmp.name, "none.json")})
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        message = json.loads(done.stdout)["injectSteps"][0]["systemMessage"]["systemMessage"]
        self.assertIn("unknown: CONCLAVE_SCHEDULER_RECORDS cannot be read", message)


if __name__ == "__main__":
    unittest.main()
