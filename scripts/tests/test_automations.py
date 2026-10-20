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
        manifest = self.check({"schema_version": 1, "folder": "\\Conclave-SDK\\",
                               "tasks": [fixture_task("conclave-sdk-reviewers")]})
        self.assertEqual(manifest["prefix"], "conclave-sdk-")
        self.assertEqual(list(manifest["tasks"]), ["conclave-sdk-reviewers"])

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
                     "Start-ScheduledTask -TaskPath '\\Demo\\' -TaskName demo-packages"):
            self.assertIn(text, body)
        _, linux = alert.message("demo-packages", self.TASK, self.OUTCOME, "", "PC", "\\Demo\\", windows=False)
        self.assertIn("systemctl --user start demo-packages.service", linux)
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
        for label, text in (("unmarked", "[Unit]\nDescription=hand-written\n"),
                            ("another checkout", run.MARKER + "/elsewhere/.conclave/automations/tasks.json\n[Unit]\n")):
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

    def test_a_refused_manifest_installs_nothing(self):
        result = self.install(tasks=[fixture_task("other-name")])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.config / "systemd" / "user").exists())


if __name__ == "__main__":
    unittest.main()
