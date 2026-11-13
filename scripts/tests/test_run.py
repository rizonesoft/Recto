"""Tests of `scripts/unattended-run.py`: start, resume, status, attach and stop a run held in tmux.

They run real tmux on a private server (TMUX_TMPDIR in a temporary folder), never the operator's own sessions, a
real preflight against a seeded project, and a stub `claude` that records its arguments and starts a child.
"""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
import unittest.mock

SCRIPTS = Path(__file__).resolve().parents[1]
TEMPLATES = SCRIPTS.parent
IN_KIT = TEMPLATES.name == "templates" and TEMPLATES.parent.name == "unattended"
TODO = TEMPLATES.parent.parent / "todo" / "templates" if IN_KIT else TEMPLATES
HAS_TMUX = os.name == "posix" and shutil.which("tmux") is not None
needs_tmux = unittest.skipUnless(HAS_TMUX, "the run lives in tmux: Linux or WSL with tmux installed")

PROFILE = {"schema_version": 1, "name": "demo", "agent": "claude", "model": "claude-opus-5-5", "effort": "high",
           "prompt_file": "docs/unattended-prompt.md", "branch": "main", "skip_permissions": True, "tools": [],
           "min_disk_gb": 1, "min_ram_gb": 1, "tick_seconds": 300, "idle_minutes": 45, "stall_hours": 6,
           "preflight": []}
PROMPT = "Run the plan to the end.\n"
# The stub agent: records its arguments, starts a child that ignores SIGHUP (as a detached tool would, so only the
# process-group kill ends it), then waits. With STUB_RESUME_FAILS set, a --resume exits at once.
STUB = '''#!/usr/bin/env python3
import json, os, signal, subprocess, sys, time
NAME = os.path.basename(sys.argv[0])
if sys.argv[1:] == ["--version"]:
    print(os.environ.get("STUB_VERSION") or {"claude": "2.1.322 (Claude Code)", "codex": "codex-cli 0.166.0", "agy": "1.5.5"}[NAME])
    sys.exit(0)
with open(os.environ["STUB_LOG"], "a") as log:
    log.write(json.dumps({"cli": NAME, "argv": sys.argv[1:], "inside": os.environ.get("CONCLAVE_UNATTENDED_RUN"),
                          "tag": os.environ.get("CONCLAVE_UNATTENDED_TAG")}) + "\\n")
RESUMING = any(a in ("--resume", "resume") or a.startswith("--conversation=") for a in sys.argv[1:])
if os.environ.get("STUB_RESUME_FAILS") and RESUMING:
    sys.exit(1)
if os.environ.get("STUB_EXIT"):
    print("stub agent: cannot start", flush=True)
    sys.exit(3)
# The first child scrubs its environment, as some tools do, so it carries no run tag: only the process-group kill
# reaches it. The second leaves the group by starting its own session: only the tag reaches it.
child = subprocess.Popen([sys.executable, "-c",
                          "import signal, time; signal.signal(signal.SIGHUP, signal.SIG_IGN); time.sleep(300)"],
                         env={"PATH": os.environ.get("PATH", "")})
open(os.environ["STUB_CHILD"], "w").write(str(child.pid))
# A second child in its own session, as the review dispatcher detaches its reviewers: outside the agent's group.
detached = subprocess.Popen([sys.executable, "-c",
                             "import signal, time; signal.signal(signal.SIGHUP, signal.SIG_IGN); time.sleep(300)"],
                            start_new_session=True)
open(os.environ["STUB_CHILD"] + ".detached", "w").write(str(detached.pid))
signal.signal(signal.SIGHUP, signal.SIG_IGN)
if os.environ.get("STUB_ECHO"):
    for line in sys.stdin:
        with open(os.environ["STUB_ECHO"], "a") as echo:
            echo.write(line)
time.sleep(300)
'''


TABLE = ("# TODO-01 -- Demo\n\n## Implementation Order\n\n| Order | § | Deliverable | Depends On | Done |\n"
         "| ----- | - | ----------- | ---------- | ---- |\n| 1 | §2 | Second | none | {s2} |\n| 2 | §1 | First | none | {s1} |\n")


def windowless():
    """No console window of its own for any process a test starts on Windows (a console child of a parent with
    none opens one over the owner's work: 2026-11-12, Office's launcher tests flashed 5 to 10); 0 elsewhere."""
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=True, timeout=60,
                          creationflags=windowless())


def gone(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


def load_run_module():
    spec = importlib.util.spec_from_file_location("unattended_run", SCRIPTS / "unattended-run.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AttachCommandTest(unittest.TestCase):
    def test_each_form(self):
        run = load_run_module()
        self.assertEqual(run.attach_command("demo-run", False), ["tmux", "attach-session", "-t", "=demo-run"])
        self.assertEqual(run.attach_command("demo-run", True), ["tmux", "attach-session", "-r", "-t", "=demo-run"])


class CommandTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        (self.root / "docs").mkdir()
        (self.root / "docs" / "unattended-prompt.md").write_text(PROMPT, encoding="utf-8")

    def test_the_claude_command_from_the_profile(self):
        run = load_run_module()
        for skip in (True, False):
            with self.subTest(skip_permissions=skip):
                argv = run.agent_command(self.root, {**PROFILE, "skip_permissions": skip}, "sid-1", None)
                expected = ["claude", "--model", "claude-opus-5-5", "--effort", "high", "--session-id", "sid-1",
                            "--disallowedTools", "AskUserQuestion,EnterPlanMode,ExitPlanMode",
                            *(["--dangerously-skip-permissions"] if skip else []), "--", PROMPT]
                self.assertEqual(argv, expected)
        resumed = run.agent_command(self.root, PROFILE, "sid-1", "sid-1")
        self.assertEqual(resumed[:7], ["claude", "--model", "claude-opus-5-5", "--effort", "high", "--resume",
                                       "sid-1"])
        self.assertEqual(resumed[-2:], ["--", run.RESUME_PROMPT])
        self.assertNotIn("--session-id", resumed)

    CLAUDE_ONLY = ("--session-id", "--resume", "--disallowedTools", "--effort", "--dangerously-skip-permissions")

    def test_the_codex_command_from_the_profile(self):
        run = load_run_module()
        codex = {**PROFILE, "agent": "codex", "model": "gpt-6-astra", "effort": "xhigh"}
        for skip in (True, False):
            with self.subTest(skip_permissions=skip):
                profile = {**codex, "skip_permissions": skip}
                bypass = ["--dangerously-bypass-approvals-and-sandbox"] if skip else []
                options = ["-m", "gpt-6-astra", "-c", 'model_reasoning_effort="xhigh"', *bypass]
                self.assertEqual(run.agent_command(self.root, profile, "tag-1", None), ["codex", *options, "--", PROMPT])
                self.assertEqual(run.agent_command(self.root, profile, "tag-1", "thread-9"),
                                 ["codex", "resume", *options, "--", "thread-9", run.RESUME_PROMPT])
                for argv in (run.agent_command(self.root, profile, "tag-1", None),
                             run.agent_command(self.root, profile, "tag-1", "thread-9")):
                    self.assertFalse([a for a in argv if a in self.CLAUDE_ONLY], argv)
                    self.assertNotIn("tag-1", argv, "Codex chooses its own session id")

    def test_the_antigravity_command_from_the_profile(self):
        run = load_run_module()
        agy = {**PROFILE, "agent": "agy", "model": "gemini-4-argon", "effort": "max"}
        for skip in (True, False):
            with self.subTest(skip_permissions=skip):
                profile = {**agy, "skip_permissions": skip}
                bypass = ["--dangerously-skip-permissions"] if skip else []
                self.assertEqual(run.agent_command(self.root, profile, "tag-1", None),
                                 ["agy", "--model=gemini-4-argon", "--effort=max", *bypass,
                                  f"--prompt-interactive={PROMPT}"])
                self.assertEqual(run.agent_command(self.root, profile, "tag-1", "conv-7"),
                                 ["agy", "--model=gemini-4-argon", "--effort=max", "--conversation=conv-7", *bypass,
                                  f"--prompt-interactive={run.RESUME_PROMPT}"])
                argv = run.agent_command(self.root, profile, "tag-1", None)
                self.assertFalse([a for a in argv if a in ("--session-id", "--resume", "--disallowedTools")], argv)


@needs_tmux
class RunTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        base = Path(self._tmp.name)
        self.root = base / "repo"
        self.bin = base / "bin"
        self.bin.mkdir()
        for cli in ("claude", "codex", "agy"):
            stub = self.bin / cli
            stub.write_text(STUB, encoding="utf-8")
            stub.chmod(0o755)
        self.log = base / "stub.jsonl"
        self.child_file = base / "child.pid"
        tmux_dir = base / "tmux"
        tmux_dir.mkdir()
        self.env = {**{k: v for k, v in os.environ.items() if k != "TMUX"},
                    "PATH": f"{self.bin}{os.pathsep}{os.environ['PATH']}", "TMUX_TMPDIR": str(tmux_dir),
                    "STUB_LOG": str(self.log), "STUB_CHILD": str(self.child_file), "CONCLAVE_STARTUP_GRACE": "2"}
        patch = unittest.mock.patch.dict(os.environ, self.env, clear=True)
        patch.start()
        self.addCleanup(patch.stop)
        self.addCleanup(self.end_everything)
        for rel in ("scripts/lib/unattended.py", "scripts/unattended-preflight.py", "scripts/unattended-run.py"):
            (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(TEMPLATES / rel, self.root / rel)
        for rel in ("scripts/campaign_guard.py", "scripts/codex-campaign-hook.py", "scripts/codex-hook-trust.py",
                    "scripts/lib/todo_graph.py", ".claude/settings.json"):
            if (TODO / rel).is_file():
                (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(TODO / rel, self.root / rel)
        (self.root / ".conclave").mkdir()
        self.write_profile()
        (self.root / "docs").mkdir()
        (self.root / "docs" / "unattended-prompt.md").write_text(PROMPT, encoding="utf-8")
        (self.root / ".gitignore").write_text("build/\n", encoding="utf-8")
        self.todo = self.root / "todo" / "02-demo" / "TODO-01-demo.md"
        self.todo.parent.mkdir(parents=True)
        self.todo.write_text(TABLE.format(s1="[ ]", s2="[ ]"), encoding="utf-8")
        for args in (["init", "-q", "-b", "main"], ["config", "user.email", "t@t"], ["config", "user.name", "t"],
                     ["config", "commit.gpgsign", "false"], ["add", "-A"], ["commit", "-qm", "fixture"]):
            git(self.root, *args)

    def write_profile(self, **changes):
        (self.root / ".conclave" / "unattended.json").write_text(json.dumps({**PROFILE, **changes}),
                                                                 encoding="utf-8")

    def end_everything(self):
        subprocess.run(["tmux", "kill-server"], capture_output=True, timeout=30, creationflags=windowless())
        for path in (self.child_file, Path(str(self.child_file) + ".detached")):
            if path.exists():
                try:
                    os.kill(int(path.read_text()), signal.SIGKILL)
                except (ProcessLookupError, ValueError):
                    pass  # already gone

    def launcher(self, *args, env=None, heartbeat=False):
        # These tests drive one start at a time; the heartbeat's own tests (HeartbeatRunTest) leave it running.
        if args[:1] == ("start",) and not heartbeat:
            args = (*args, "--no-heartbeat")
        return subprocess.run([sys.executable, str(self.root / "scripts" / "unattended-run.py"), *args],
                              capture_output=True, text=True, timeout=600, env=env or self.env,
                              creationflags=windowless())

    def tmux_has(self, name):
        return subprocess.run(["tmux", "has-session", "-t", f"={name}"], capture_output=True,
                              timeout=30, creationflags=windowless()).returncode == 0

    def calls(self, at_least=1):
        """The stub's recorded calls, once it has made `at_least` of them: the agent starts after the launcher returns."""
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            lines = self.log.read_text(encoding="utf-8").splitlines() if self.log.exists() else []
            if len(lines) >= at_least:
                break
            time.sleep(0.1)
        return [json.loads(line) for line in lines]

    def acquire(self, session, tag=None):
        (self.root / "docs" / "phase-runs").mkdir(parents=True, exist_ok=True)
        run_file = self.root / "docs" / "phase-runs" / "run.md"
        if not run_file.exists():  # a run file is committed, as process-plan commits it
            run_file.write_text("# Run\n", encoding="utf-8")
            git(self.root, "add", "-A")
            git(self.root, "commit", "-qm", "run file")
        done = subprocess.run([sys.executable, str(self.root / "scripts" / "campaign_guard.py"), "acquire",
                               "--session", session, "--phase", "2", "--run-file", "docs/phase-runs/run.md",
                               "--cron-id", "job-1", "--generation", "g1", "--root", str(self.root)],
                              capture_output=True, text=True, timeout=60,
                              env={**self.env, **({"CONCLAVE_UNATTENDED_TAG": tag} if tag else {})},
                              creationflags=windowless())
        self.assertEqual(done.returncode, 0, done.stderr)

    def guard(self):
        path = self.root / "build" / "claude-campaign-guard.json"
        return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None

    def run_module(self):
        spec = importlib.util.spec_from_file_location("installed_run", self.root / "scripts" / "unattended-run.py")
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def marker(self):
        return json.loads((self.root / "build" / "unattended-run.json").read_text(encoding="utf-8"))

    def wait_child(self, detached=False):
        path = Path(str(self.child_file) + ".detached") if detached else self.child_file
        deadline = time.monotonic() + 15
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        return int(path.read_text())

    def wait_gone(self, pid, seconds=15):
        deadline = time.monotonic() + seconds
        while not gone(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        return gone(pid)

    def test_start_runs_the_agent_in_tmux_that_outlives_the_launcher(self):
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("PASS profile", done.stdout)
        # The launcher has exited; the run is still there.
        self.assertTrue(self.tmux_has("demo-run"))
        marker = self.marker()
        claim = json.loads((self.root / "build" / "unattended-launch.json").read_text(encoding="utf-8"))
        self.assertEqual((marker["tmux_session"], marker["agent"], marker["resumed"]), ("demo-run", "claude", False))
        self.assertEqual((claim["session"], claim["tmux_session"], claim["pid"]),
                         (marker["session"], "demo-run", marker["pgid"]))
        call = self.calls()[0]
        self.assertEqual(call["argv"][call["argv"].index("--session-id") + 1], marker["session"])
        self.assertEqual(call["argv"][-1], PROMPT)
        self.assertEqual(call["inside"], "1")
        limit = subprocess.run(["tmux", "show-options", "-v", "-t", "=demo-run:", "history-limit"],
                               capture_output=True, text=True, timeout=30, creationflags=windowless()).stdout.strip()
        self.assertEqual(limit, "50000")
        again = self.launcher("start")
        self.assertEqual(again.returncode, 1)
        self.assertIn("already running", again.stderr)

    def test_an_agent_cli_older_than_the_recorded_flags_starts_nothing(self):
        # Completion review of D02 T02 §6: the version refusal was tested in the preflight alone.
        self.use_codex()
        done = self.launcher("start", env={**self.env, "STUB_VERSION": "codex-cli 0.150.1"})
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("FAIL version codex: 0.150.1, older than 0.166.0", done.stdout)
        self.assertIn("nothing was started", done.stdout)
        self.assertFalse(self.log.exists(), "the agent was started")
        self.assertFalse(self.tmux_has("demo-run"))
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_a_failed_preflight_starts_nothing(self):
        (self.root / "dirty.txt").write_text("x", encoding="utf-8")
        done = self.launcher("start")
        self.assertEqual(done.returncode, 1)
        self.assertIn("FAIL git-clean", done.stdout)
        self.assertIn("nothing was started", done.stdout)
        self.assertFalse(self.tmux_has("demo-run"))
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_a_dead_run_is_resumed_and_a_resume_that_fails_starts_fresh(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        first = self.marker()["session"]
        self.calls(1)  # the agent has started before its session is killed
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual((self.marker()["session"], self.marker()["resumed"]), (first, True))
        resumed = self.calls(2)[-1]["argv"]
        self.assertEqual(resumed[resumed.index("--resume") + 1], first)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        env = {**self.env, "STUB_RESUME_FAILS": "1"}
        done = self.launcher("start", env=env)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("did not stay up (the agent exited 1", done.stdout)
        self.assertIn("starting a fresh session", done.stdout)
        fresh = self.marker()
        self.assertNotEqual(fresh["session"], first)
        self.assertFalse(fresh["resumed"])
        calls = self.calls(4)  # the first start, the resume, the failed resume and the fresh start
        self.assertEqual(len(calls), 4, [c["argv"][4:6] for c in calls])
        self.assertIn("--resume", calls[2]["argv"])
        self.assertEqual(calls[3]["argv"][calls[3]["argv"].index("--session-id") + 1], fresh["session"])

    def test_status_for_a_live_and_a_stopped_run(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        lines = dict(line.split(": ", 1) for line in self.launcher("status").stdout.splitlines())
        self.assertTrue(lines["run"].startswith("live"))
        self.assertEqual(lines["session"], self.marker()["session"])
        self.assertEqual(lines["guard"], "none")
        self.assertEqual(lines["health"], "none")
        self.assertEqual(self.launcher("stop").returncode, 0)
        lines = dict(line.split(": ", 1) for line in self.launcher("status").stdout.splitlines())
        self.assertTrue(lines["run"].startswith("stopped"))
        self.assertEqual(lines["session"], "none")

    def test_stop_ends_the_guard_the_sessions_and_the_whole_group(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        session = self.marker()["session"]
        child = self.wait_child()
        detached = self.wait_child(detached=True)
        self.acquire(session)
        subprocess.run(["tmux", "new-session", "-d", "-s", "demo-heartbeat", "sleep 300"], check=True, timeout=30,
                       creationflags=windowless())
        done = self.launcher("stop")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("guard ended", done.stdout)
        self.assertFalse(self.tmux_has("demo-run"))
        self.assertFalse(self.tmux_has("demo-heartbeat"))
        deadline = time.monotonic() + 15
        while not gone(child) and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertTrue(gone(child), "the agent's child outlived the stop")
        self.assertTrue(self.wait_gone(detached), "a child in its own session outlived the stop")
        for rel in ("build/claude-campaign-guard.json", "build/unattended-run.json", "build/unattended-launch.json"):
            self.assertFalse((self.root / rel).exists(), rel)

    def test_a_resume_ends_the_crashed_runs_leftovers_first(self):
        # Round 1 of D02 T02 §5: the old processes survived a lost tmux session and ran beside the resumed agent.
        self.assertEqual(self.launcher("start").returncode, 0)
        old_child, old_detached = self.wait_child(), self.wait_child(detached=True)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        time.sleep(0.5)
        self.assertFalse(gone(old_child), "the stub's child should outlive the tmux session (it ignores SIGHUP)")
        self.child_file.unlink()
        Path(str(self.child_file) + ".detached").unlink()
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(self.wait_gone(old_child), "the crashed run's child ran on beside the resumed run")
        self.assertTrue(self.wait_gone(old_detached), "the crashed run's detached child ran on")

    def test_a_guard_that_does_not_end_keeps_the_runs_state(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        self.acquire(self.marker()["session"])
        run = self.run_module()
        refused = subprocess.CompletedProcess([], 2, stdout="", stderr="campaign-guard: the guard lock timed out")
        out = __import__("io").StringIO()
        with unittest.mock.patch.object(run, "_end_guard", return_value=refused):
            self.assertEqual(run.stop(self.root, out), 1)
        self.assertIn("guard not ended", out.getvalue())
        self.assertTrue((self.root / "build" / "unattended-run.json").exists(), "the marker was dropped")
        self.assertTrue((self.root / "build" / "unattended-launch.json").exists(), "the claim was dropped")
        done = self.launcher("stop")  # the second stop finds the session again and ends the guard
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIsNone(self.guard())

    def test_a_preflight_that_overruns_is_ended_with_its_children(self):
        # Round 1 of D02 T02 §5: a fixed 900 s limit killed the preflight alone and raised TimeoutExpired.
        run = self.run_module()
        fake = Path(self._tmp.name) / "fake-scripts"
        fake.mkdir()
        pid_file = Path(self._tmp.name) / "preflight-child.pid"
        (fake / "unattended-preflight.py").write_text(
            "import subprocess, sys, time\n"
            "c = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)'])\n"
            f"open({str(pid_file)!r}, 'w').write(str(c.pid))\n"
            "print('PASS profile: slow', flush=True)\n"
            "time.sleep(120)\n", encoding="utf-8")
        with unittest.mock.patch.object(run, "HERE", fake), \
                unittest.mock.patch.object(run, "PREFLIGHT_SLACK_SECONDS", 2):
            code, report = run.run_preflight(self.root, None, PROFILE)
        self.assertEqual(code, 1)
        self.assertIn("PASS profile: slow", report)
        self.assertIn("FAIL preflight: ran past 2s", report)
        self.assertTrue(self.wait_gone(int(pid_file.read_text())), "the preflight's child outlived it")
        limit_profile = {**PROFILE, "preflight": [{"name": "build", "command": ["true"], "timeout_seconds": 1800}]}
        with unittest.mock.patch.object(run.subprocess, "Popen", side_effect=RuntimeError("stop here")) as popen:
            with self.assertRaises(RuntimeError):
                run.run_preflight(self.root, None, limit_profile)
        self.assertEqual(popen.call_count, 1)

    def test_a_fallback_whose_old_guard_cannot_be_ended_starts_nothing(self):
        # Completion review of D02 T02 §5: the fresh session started anyway and the marker lost its owner.
        self.assertEqual(self.launcher("start").returncode, 0)
        first = self.marker()["session"]
        self.calls(1)
        self.acquire(first)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        run = self.run_module()
        refused = subprocess.CompletedProcess([], 2, stdout="", stderr="campaign-guard: the guard lock timed out")
        with unittest.mock.patch.dict(os.environ, {"STUB_RESUME_FAILS": "1"}), \
                unittest.mock.patch.object(run, "_end_guard", return_value=refused):
            with self.assertRaises(run.RunError) as caught:
                run.start(self.root, out=open(os.devnull, "w"), supervise=False)
        self.assertIn("could not be ended", str(caught.exception))
        self.assertFalse(self.tmux_has("demo-run"), "a fresh session started beside the crashed run's guard")
        self.assertEqual(self.marker()["session"], first, "the marker lost the crashed session")
        self.assertEqual(self.guard()["session_id"], first)

    def test_a_cancelled_preflight_ends_its_detached_profile_check(self):
        # Completion review of D02 T02 §5: profile checks run in sessions of their own, outside the preflight's group.
        pids = Path(self._tmp.name) / "check.pids"
        check = ("import os, subprocess, sys, time; "
                 "c = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(300)']); "
                 f"open({str(pids)!r}, 'w').write(f'{{os.getpid()}} {{c.pid}}'); time.sleep(300)")
        profile = {**PROFILE, "preflight": [{"name": "build", "command": [sys.executable, "-c", check],
                                             "timeout_seconds": 60}]}
        self.write_profile(**profile)
        git(self.root, "commit", "-qam", "a slow check")
        run = self.run_module()
        with unittest.mock.patch.object(run, "PREFLIGHT_SLACK_SECONDS", -50):  # a 10 s limit, under the check's 60
            code, report = run.run_preflight(self.root, None, profile)
        self.assertEqual(code, 1)
        self.assertIn("FAIL preflight: ran past 10s", report)
        check_pid, child_pid = (int(p) for p in pids.read_text().split())
        self.assertTrue(self.wait_gone(check_pid), "the detached profile check outlived its preflight")
        self.assertTrue(self.wait_gone(child_pid), "the profile check's child outlived its preflight")
    def test_an_agent_that_exits_at_once_is_reported_and_leaves_nothing(self):
        done = self.launcher("start", env={**self.env, "STUB_EXIT": "1"})
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("the agent exited 3", done.stderr)
        self.assertIn("stub agent: cannot start", done.stderr)
        self.assertFalse(self.tmux_has("demo-run"))
        for rel in ("build/unattended-run.json", "build/unattended-launch.json"):
            self.assertFalse((self.root / rel).exists(), rel)

    def test_a_state_write_that_fails_ends_the_run_it_started(self):
        run = self.run_module()
        with unittest.mock.patch.object(run.lib, "write_claim", side_effect=OSError(28, "No space left on device")):
            with self.assertRaises(run.RunError) as caught:
                run.start(self.root, out=open(os.devnull, "w"), supervise=False)
        self.assertIn("could not be written", str(caught.exception))
        self.assertFalse(self.tmux_has("demo-run"))
        self.assertTrue(gone(self.wait_child()), "the started agent's child outlived the failed start")
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_a_tmux_failure_leaves_no_session(self):
        run = self.run_module()
        real = run.tmux

        def failing(*args, check=False):
            if args[0] == "new-window":
                raise run.RunError("tmux new-window failed: injected")
            return real(*args, check=check)
        with unittest.mock.patch.object(run, "tmux", failing):
            with self.assertRaises(run.RunError):
                run.start(self.root, out=open(os.devnull, "w"), supervise=False)
        self.assertFalse(self.tmux_has("demo-run"))
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_a_resume_keeps_the_crashed_runs_guard_and_a_fresh_start_ends_it(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        first = self.marker()["session"]
        self.calls(1)
        self.acquire(first)  # the crashed run held its guard
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("PASS campaign-guard", done.stdout)
        self.assertEqual(self.guard()["session_id"], first, "the resumed run keeps its guard")
        self.calls(2)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start", env={**self.env, "STUB_RESUME_FAILS": "1"})
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("the crashed run's guard ended", done.stdout)
        self.assertIsNone(self.guard(), "a fresh session cannot take the crashed one's guard, so it was ended")
        self.assertNotEqual(self.marker()["session"], first)

    def test_stop_under_the_lock_once_then_a_no_op(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        holder = subprocess.Popen([sys.executable, "-c", (
            "import importlib.util, time\n"
            f"spec = importlib.util.spec_from_file_location('u', {str(self.root / 'scripts/lib/unattended.py')!r})\n"
            "u = importlib.util.module_from_spec(spec); spec.loader.exec_module(u)\n"
            f"with u.launch_lock({str(self.root)!r}):\n"
            "    print('held', flush=True); time.sleep(60)\n")], stdout=subprocess.PIPE, text=True,
            creationflags=windowless())
        self.assertEqual(holder.stdout.readline().strip(), "held")
        refused = self.launcher("stop")
        holder.kill()
        holder.wait()
        holder.stdout.close()
        self.assertEqual(refused.returncode, 1)
        self.assertIn("unattended-launch.lock", refused.stderr)
        self.assertTrue(self.tmux_has("demo-run"), "a stop beside a launch must change nothing")
        self.assertEqual(self.launcher("stop").returncode, 0)
        again = self.launcher("stop")
        self.assertEqual(again.returncode, 0, again.stdout + again.stderr)
        self.assertIn("with no marker", again.stdout)

    def test_stop_keeps_another_sessions_guard_and_follows_the_marker_over_a_renamed_profile(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        self.acquire("someone-else")
        self.write_profile(name="renamed")
        git(self.root, "commit", "-qam", "rename")
        done = self.launcher("stop")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("belongs to session someone-else", done.stdout)
        self.assertEqual(self.guard()["session_id"], "someone-else")
        self.assertFalse(self.tmux_has("demo-run"), "stop followed the profile's new name, not the run's")

    def test_status_shows_the_recorded_values_and_an_unknown_state(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        marker = self.marker()
        self.acquire(marker["session"])
        lines = dict(line.split(": ", 1) for line in self.launcher("status").stdout.splitlines())
        self.assertEqual(lines["started"], marker["started"])
        guard = self.guard()
        self.assertEqual(lines["guard"], f"phase 2, run {guard['run_id']}, session {marker['session']}")
        run = self.run_module()
        out = __import__("io").StringIO()
        with unittest.mock.patch.object(run.lib, "tmux_session_exists", return_value=None):
            run.status(self.root, out)
        self.assertIn("run: unknown (tmux could not be asked)", out.getvalue())

    def test_attach_to_an_absent_session_is_refused(self):
        done = self.launcher("attach", "--watch")
        self.assertEqual(done.returncode, 1)
        self.assertIn("is not running", done.stderr)

    def use_codex(self):
        self.write_profile(agent="codex", model="gpt-6-astra", effort="xhigh")
        (self.root / ".codex").mkdir(exist_ok=True)
        shutil.copyfile(TODO / ".codex/hooks.json", self.root / ".codex/hooks.json")
        # Codex's approval of the hook goes through its app-server, which test_codex_hook.py drives against a stub
        # server; here the trust script reports the hook trusted, as it does once Codex has approved it.
        (self.root / "scripts" / "codex-hook-trust.py").write_text(
            "print('codex-hook-trust: trusted: .codex/hooks.json:stop:0:0')\n", encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "a codex run")

    def test_a_codex_run_is_tagged_resumed_by_its_guards_session_and_stopped(self):
        # D02 T02 §6: Codex chooses its own session id; the guard it acquires carries the run's tag.
        self.use_codex()
        self.assertEqual(self.launcher("start").returncode, 0)
        marker = self.marker()
        self.assertIsNone(marker["agent_session"])
        first = self.calls(1)[0]
        self.assertEqual((first["cli"], first["tag"]), ("codex", marker["session"]))
        self.acquire("codex-thread-1", tag=marker["session"])  # the hook inside the run, with Codex's own id
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        resumed = self.calls(2)[-1]["argv"]
        self.assertEqual(resumed[:2], ["resume", "-m"])
        self.assertEqual(resumed[resumed.index("--") + 1], "codex-thread-1")
        self.assertEqual((self.marker()["session"], self.marker()["agent_session"]), (marker["session"],
                                                                                       "codex-thread-1"))
        done = self.launcher("stop")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("guard ended", done.stdout)
        self.assertIsNone(self.guard())

    def test_a_codex_run_without_a_guard_starts_fresh_and_leaves_another_runs_guard(self):
        self.use_codex()
        self.assertEqual(self.launcher("start").returncode, 0)
        tag = self.marker()["session"]
        self.calls(1)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start")  # the agent died before it took a guard: nothing to resume
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        fresh = self.calls(2)[-1]["argv"]
        self.assertNotEqual(fresh[0], "resume")
        self.assertNotEqual(self.marker()["session"], tag)
        self.acquire("someone-else", tag="another-run")
        done = self.launcher("stop")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.guard()["session_id"], "someone-else", "stop ended another run's guard")

    def test_a_marker_from_before_agent_session_still_stops_its_run(self):
        # Round 1 of D02 T02 §6: a §5 marker (no agent_session) became unreadable, and stop lost the run's group.
        self.assertEqual(self.launcher("start").returncode, 0)
        child = self.wait_child()
        path = self.root / "build" / "unattended-run.json"
        old = json.loads(path.read_text(encoding="utf-8"))
        del old["agent_session"]
        path.write_text(json.dumps(old), encoding="utf-8")
        marker, problem = self.run_module().read_marker(self.root)
        self.assertIsNone(problem)
        self.assertEqual(marker["agent_session"], old["session"])
        done = self.launcher("stop")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(self.wait_gone(child), "stop lost the old marker's process group")

    def test_a_recovery_that_fails_is_not_read_as_a_run_that_ended(self):
        # Round 1 of D02 T02 §7: the launcher's own guard end left an end record, and the heartbeat then stopped.
        self.assertEqual(self.launcher("start").returncode, 0)
        first = self.marker()["session"]
        self.calls(1)
        self.acquire(first)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start", env={**self.env, "STUB_RESUME_FAILS": "1", "STUB_EXIT": "1"})
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)  # the resume and the fresh start both fail
        self.assertIn("the crashed run's guard ended", done.stdout)
        run = self.run_module()
        marker, _ = run.read_marker(self.root)
        self.assertEqual(marker["session"], first, "the failed recovery lost the run")
        self.assertFalse(run.run_finished(self.root, marker), "a failed recovery reads as a run that ended")
    def test_a_run_whose_profile_changed_agent_starts_that_agent_fresh(self):
        # Round 1 of D02 T02 §6: a crashed Claude Code run was resumed as `codex resume <Claude's session id>`.
        self.assertEqual(self.launcher("start").returncode, 0)
        first = self.marker()["session"]
        self.calls(1)
        self.acquire(first)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        self.use_codex()
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("the profile now names codex", done.stdout)
        self.assertIn("the crashed run's guard ended", done.stdout)
        call = self.calls(2)[-1]
        self.assertEqual(call["cli"], "codex")
        self.assertNotIn("resume", call["argv"])
        self.assertNotIn(first, call["argv"])
        self.assertIsNone(self.guard())
    def test_an_antigravity_run_starts_with_its_prompt(self):
        self.write_profile(agent="agy", model="gemini-4-argon", effort="high")
        git(self.root, "commit", "-qam", "an antigravity run")
        (self.root / ".agents").mkdir(exist_ok=True)
        shutil.copyfile(TODO / ".agents/hooks.json", self.root / ".agents/hooks.json")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "the antigravity hook")
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        call = self.calls(1)[0]
        self.assertEqual(call["cli"], "agy")
        self.assertEqual(call["argv"][-1], f"--prompt-interactive={PROMPT}")



    def test_a_crashed_runs_unfinished_edit_survives_its_resume(self):
        # Plan review of D02 T02 §7: the clean-tree check refused to resume a run killed mid-edit.
        self.assertEqual(self.launcher("start").returncode, 0)
        self.calls(1)
        edited = self.root / "docs" / "unattended-prompt.md"
        edited.write_text(PROMPT + "An edit the run had not committed.\n", encoding="utf-8")
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("kept for the resumed run", done.stdout)
        self.assertTrue(self.marker()["resumed"])
        self.assertIn("An edit the run had not committed.", edited.read_text(encoding="utf-8"))

    def heartbeat_cli(self, *args):
        return subprocess.run([sys.executable, str(self.root / "scripts" / "unattended-run.py"), "heartbeat", *args],
                              capture_output=True, text=True, timeout=300, env=self.env, creationflags=windowless())


    def wait_session_gone(self, name, seconds=30):
        deadline = time.monotonic() + seconds
        while self.tmux_has(name) and time.monotonic() < deadline:
            time.sleep(0.2)
        return not self.tmux_has(name)





    def idle_tmux(self, run, idle, now_of):
        """tmux as the run sees it, except that its window reports `idle` seconds without activity."""
        real = run.tmux

        def tmux(*args, check=False):
            if args[0] == "display-message" and "#{window_activity}" in args:
                return subprocess.CompletedProcess(args, 0, stdout=f"{int(now_of() - idle)}\n", stderr="")
            return real(*args, check=check)
        return tmux

    def test_an_ordinary_start_runs_one_heartbeat_and_stop_removes_it(self):
        done = self.launcher("start", heartbeat=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("heartbeat running in tmux session demo-heartbeat", done.stdout)
        self.assertEqual(self.launcher("start", heartbeat=True).returncode, 1)  # already running: no second one
        sessions = subprocess.run(["tmux", "ls", "-F", "#{session_name}"], capture_output=True, text=True,
                                  timeout=30, creationflags=windowless()).stdout.split()
        self.assertEqual(sorted(sessions), ["demo-heartbeat", "demo-run"])
        self.assertEqual(self.launcher("stop").returncode, 0)
        self.assertFalse(self.tmux_has("demo-heartbeat"))

    def test_a_killed_run_is_recorded_as_ended_and_never_restarted(self):
        # Owner decision 2026-11-12: a crash ends a run; the heartbeat stops what it started and restarts nothing.
        self.write_profile(tick_seconds=1)
        git(self.root, "commit", "-qam", "a one-second tick")
        self.assertEqual(self.launcher("start", heartbeat=True).returncode, 0)
        marker = self.marker()
        child, detached = self.wait_child(), self.wait_child(detached=True)
        self.acquire(marker["session"], tag=marker["session"])
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        self.assertTrue(self.wait_session_gone("demo-heartbeat"), "the heartbeat did not end with its dead run")
        time.sleep(3)
        self.assertEqual(len(self.calls(1)), 1, "the dead run was started again")
        self.assertFalse(self.tmux_has("demo-run"))
        self.assertTrue(self.wait_gone(child) and self.wait_gone(detached), "the dead run's processes kept running")
        self.assertIsNone(self.guard(), "the dead run's guard was left held")
        for rel in ("build/unattended-run.json", "build/unattended-launch.json"):
            self.assertFalse((self.root / rel).exists(), rel)
        state = json.loads((self.root / "build" / "unattended-heartbeat.json").read_text(encoding="utf-8"))
        self.assertEqual(state["ended"]["reason"], "its session is gone")
        done = self.launcher("start")  # only the operator starts a run, and this one starts fresh
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertNotEqual(self.marker()["session"], marker["session"])
        self.assertIn("--session-id", self.calls(2)[-1]["argv"])

    def test_nothing_resumes_a_dead_session_or_a_crashed_agent_over_twenty_ticks(self):
        # D02 T02 §9: a run never restarts itself (owner decision 2026-11-12).
        for how in ("its tmux session is killed", "its agent crashes"):
            with self.subTest(how):
                self.end_everything()
                self.log.unlink(missing_ok=True)
                self.assertEqual(self.launcher("start").returncode, 0)
                marker = self.marker()
                self.calls(1)
                if how == "its agent crashes":
                    os.killpg(marker["pgid"], signal.SIGKILL)  # the pane closes with it, and so does the session
                else:
                    subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
                self.assertTrue(self.wait_session_gone("demo-run"))
                run = self.run_module()
                codes = []
                for i in range(20):
                    state = run.read_state(self.root, marker["session"])
                    codes.append(run.tick(self.root, 1_000_000.0 + i * 300, state, __import__("io").StringIO()))
                time.sleep(2)
                self.assertEqual(len(self.calls(1)), 1, "the dead run was started again")
                self.assertEqual(codes, [run.FINISHED] * 20)
                self.assertFalse(self.tmux_has("demo-run"))
                self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_heartbeat_once_on_a_killed_run_records_it_ended_with_10(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        self.calls(1)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        once = self.heartbeat_cli("--once")
        self.assertEqual(once.returncode, 10, once.stdout + once.stderr)
        self.assertIn("ended (its session is gone); nothing restarts it", once.stdout)
        self.assertEqual(len(self.calls(1)), 1)
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_the_heartbeat_ends_with_10_when_the_marker_goes_and_its_session_closes(self):
        self.write_profile(tick_seconds=1)
        git(self.root, "commit", "-qam", "a one-second tick")
        self.assertEqual(self.launcher("start", heartbeat=True).returncode, 0)
        (self.root / "build" / "unattended-run.json").unlink()
        self.assertTrue(self.wait_session_gone("demo-heartbeat"), "the heartbeat outlived the run's marker")
        once = self.heartbeat_cli("--once")
        self.assertEqual(once.returncode, 10, once.stdout + once.stderr)
        self.assertIn("no run marker; the heartbeat stops", once.stdout)

    def test_a_run_that_ends_itself_is_stopped_with_everything_it_started(self):
        self.write_profile(tick_seconds=1)
        git(self.root, "commit", "-qam", "a one-second tick")
        self.assertEqual(self.launcher("start", heartbeat=True).returncode, 0)
        marker = self.marker()
        child, detached = self.wait_child(), self.wait_child(detached=True)
        self.acquire(marker["session"], tag=marker["session"])
        rid = self.guard()["run_id"]
        run_file = self.root / "docs" / "phase-runs" / "run.md"
        run_file.write_text(f"# Run\n\nPARKED 2026-11-12T00:00:00Z run={rid} nothing runnable\n", encoding="utf-8")
        ended = subprocess.run([sys.executable, str(self.root / "scripts" / "campaign_guard.py"), "end", "--session",
                                marker["session"], "--reason", "park", "--root", str(self.root)],
                               capture_output=True, text=True, timeout=60, env=self.env, creationflags=windowless())
        self.assertEqual(ended.returncode, 0, ended.stderr)  # the run's own agent ends it
        self.assertTrue(self.wait_session_gone("demo-heartbeat"), "the heartbeat outlived the run's own end")
        self.assertFalse(self.tmux_has("demo-run"), "the finished run's agent kept running")
        self.assertTrue(self.wait_gone(child) and self.wait_gone(detached), "the finished run's processes kept running")
        for rel in ("build/unattended-run.json", "build/unattended-launch.json"):
            self.assertFalse((self.root / rel).exists(), rel)

    def test_a_cleanup_never_touches_the_operators_next_run(self):
        # Plan review of D02 T02 §7 (re-plan): an operator's start between the heartbeat's look and its cleanup.
        self.assertEqual(self.launcher("start").returncode, 0)
        old = self.marker()
        self.calls(1)
        self.assertEqual(self.launcher("stop").returncode, 0)
        self.assertEqual(self.launcher("start").returncode, 0)
        new = self.marker()
        self.acquire(new["session"], tag=new["session"])
        run = self.run_module()
        out, state = __import__("io").StringIO(), run.read_state(self.root, old["session"])
        self.assertEqual(run.end_run(self.root, old, "its session is gone", state, 0.0, out, gone=True), run.REPLACED)
        self.assertIn("is no longer the marked run", out.getvalue())
        self.assertTrue(self.tmux_has("demo-run"))
        self.assertEqual(self.marker()["session"], new["session"])
        self.assertTrue((self.root / "build" / "unattended-launch.json").exists())
        self.assertEqual(self.guard()["launch_tag"], new["session"])

    def test_a_cleanup_never_touches_the_operators_resume_of_the_same_run(self):
        # Round 1 of D02 T02 §7: a resume keeps the run's tag, so the tag alone named the resumed run as the dead one.
        self.assertEqual(self.launcher("start").returncode, 0)
        self.calls(1)
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30,
                       creationflags=windowless())  # the agent crashes
        dead = self.marker()  # what the heartbeat saw before the operator's start
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        resumed = self.marker()
        self.assertEqual((resumed["session"], resumed["resumed"]), (dead["session"], True))
        self.assertNotEqual(resumed["pgid"], dead["pgid"])
        run = self.run_module()
        out, state = __import__("io").StringIO(), run.read_state(self.root, dead["session"])
        self.assertEqual(run.end_run(self.root, dead, "its session is gone", state, 0.0, out, gone=True), run.REPLACED)
        self.assertTrue(self.tmux_has("demo-run"), "the resumed run's session was ended")
        self.assertTrue(run._group_alive(resumed["pgid"]), "the resumed run's process group was ended")
        self.assertEqual(self.marker(), resumed)
        self.assertTrue((self.root / "build" / "unattended-launch.json").exists())

    def test_a_session_seen_dead_but_not_confirmed_gone_under_the_lock_is_not_ended(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        marker = self.marker()
        run = self.run_module()
        for answer in (True, None):  # back again, or tmux cannot say
            with self.subTest(alive=answer):
                out, state = __import__("io").StringIO(), run.read_state(self.root, marker["session"])
                with unittest.mock.patch.object(run, "_alive", return_value=answer):
                    self.assertEqual(run.end_run(self.root, marker, "its session is gone", state, 0.0, out, gone=True),
                                     run.RETRY)
                self.assertIn("not confirmed gone", state["last_error"])
                self.assertTrue(self.tmux_has("demo-run"))
                self.assertEqual(self.marker(), marker)

    def test_a_cleanup_that_fails_keeps_the_run_recorded_for_the_next_tick(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        marker = self.marker()
        self.acquire(marker["session"], tag=marker["session"])
        run = self.run_module()
        refused = subprocess.CompletedProcess([], 2, stdout="", stderr="campaign-guard: the guard lock timed out")
        # In this order: the guard is still held for the first three, and the fourth really ends it. The third is an
        # operator's start holding the launch lock (round 1, P2).
        cases = ((unittest.mock.patch.object(run, "_end_guard", return_value=refused), "its guard did not end", True),
                 (unittest.mock.patch.object(run, "_end_guard", side_effect=subprocess.TimeoutExpired("end", 120)),
                  "TimeoutExpired", True),
                 (run.lib.launch_lock(self.root), "is held: another launch is under way", True),
                 (unittest.mock.patch.object(run, "end_run_processes", return_value=[4242]), "outlived SIGKILL", True),
                 # The claim cannot be cleared (round 1, P2): the marker stays, so the next tick finds the run again.
                 (unittest.mock.patch.object(run.lib, "clear_claim",
                                             side_effect=run.lib.LaunchError("tmux could not be asked")),
                  "tmux could not be asked", True))
        for patch, why, claim_kept in cases:
            with self.subTest(why=why):
                out, state = __import__("io").StringIO(), run.read_state(self.root, marker["session"])
                with patch:
                    self.assertEqual(run.end_run(self.root, marker, "its session is gone", state, 0.0, out), run.RETRY)
                self.assertIn(why, state["last_error"])
                self.assertIsNone(state["ended"])
                self.assertTrue((self.root / "build" / "unattended-run.json").exists(), "the marker was dropped")
                self.assertEqual((self.root / "build" / "unattended-launch.json").exists(), claim_kept)
        out, state = __import__("io").StringIO(), run.read_state(self.root, marker["session"])
        self.assertEqual(run.end_run(self.root, marker, "its session is gone", state, 0.0, out), run.OVER)  # retried
        self.assertIsNone(self.guard())
        for rel in ("build/unattended-run.json", "build/unattended-launch.json"):
            self.assertFalse((self.root / rel).exists(), rel)

    def test_a_nudge_reaches_the_runs_pane(self):
        echo = Path(self._tmp.name) / "typed.txt"
        self.assertEqual(self.launcher("start", env={**self.env, "STUB_ECHO": str(echo)}).returncode, 0)
        self.calls(1)
        time.sleep(1)
        run = self.run_module()
        now = 1_000_000.0
        state = run.read_state(self.root, self.marker()["session"])
        with unittest.mock.patch.object(run, "tmux", self.idle_tmux(run, PROFILE["idle_minutes"] * 60, lambda: now)):
            self.assertEqual(run.tick(self.root, now, state, __import__("io").StringIO()), 0)
        self.assertEqual(state["last_nudge"], now)
        deadline = time.monotonic() + 15
        while not (echo.exists() and run.NUDGE_PROMPT in echo.read_text(encoding="utf-8")) and time.monotonic() < deadline:
            time.sleep(0.2)
        self.assertIn(run.NUDGE_PROMPT, echo.read_text(encoding="utf-8"), "the nudge never reached the pane")

    def test_fifty_ticks_with_nudges_write_only_the_heartbeat_state(self):
        # The run's own state files exist first, as in a real run (the Stop hook writes the guard's state file).
        self.assertEqual(self.launcher("start").returncode, 0)
        marker = self.marker()
        self.calls(1)
        self.acquire(marker["session"], tag=marker["session"])
        (self.root / "build" / "claude-campaign-state.json").write_text("{}", encoding="utf-8")
        before = {p.name for p in (self.root / "build").iterdir()}
        run = self.run_module()
        idle = PROFILE["idle_minutes"] * 60
        clock = {"now": 1_000_000.0}
        out = __import__("io").StringIO()
        with unittest.mock.patch.object(run, "tmux", self.idle_tmux(run, idle, lambda: clock["now"])):
            for i in range(50):
                state = run.read_state(self.root, marker["session"])
                self.assertEqual(run.tick(self.root, clock["now"], state, out), 0, out.getvalue()[-1500:])
                run.lib.write_atomic(self.root / "build" / "unattended-heartbeat.json", json.dumps(state))
                clock["now"] += idle if i % 5 == 4 else 1  # a nudge every fifth tick: under the stall limit
        self.assertGreaterEqual(out.getvalue().count("heartbeat: nudged"), 8)
        self.assertIn("heartbeat-still", out.getvalue())
        after = {p.name for p in (self.root / "build").iterdir()}
        self.assertEqual(after - before, {"unattended-heartbeat.json"}, "the heartbeat wrote more than its state")

    def test_an_idle_run_that_never_progresses_ends_as_a_stall(self):
        # The guard's stall breaker counts each nudge (heartbeat-still); with no change to the tree its second trip
        # ends the run as a stall, and the heartbeat then stops the run and itself.
        self.assertEqual(self.launcher("start").returncode, 0)
        marker = self.marker()
        self.calls(1)
        self.acquire(marker["session"], tag=marker["session"])
        run = self.run_module()
        idle = PROFILE["idle_minutes"] * 60
        clock = {"now": 1_000_000.0}
        out, codes = __import__("io").StringIO(), []
        state = run.read_state(self.root, marker["session"])
        with unittest.mock.patch.object(run, "tmux", self.idle_tmux(run, idle, lambda: clock["now"])):
            for _ in range(20):
                codes.append(run.tick(self.root, clock["now"], state, out))
                if codes[-1] == run.FINISHED:
                    break
                clock["now"] += idle
        self.assertEqual(codes[-1], run.FINISHED, out.getvalue()[-1500:])
        ended = json.loads((self.root / "build" / "claude-campaign-ended.json").read_text(encoding="utf-8"))
        self.assertEqual((ended["reason"], ended["launch_tag"]), ("stall", marker["session"]))
        self.assertFalse(self.tmux_has("demo-run"), "the stalled run kept running")
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def stamp_row(self, message="stamp §1"):
        self.todo.write_text(TABLE.format(s1="[x]", s2="[ ]"), encoding="utf-8")
        git(self.root, "commit", "-qam", message)

    def test_a_fresh_start_records_its_baseline(self):
        head = git(self.root, "rev-parse", "HEAD").stdout.strip()
        self.assertEqual(self.launcher("start").returncode, 0)
        marker = self.marker()
        self.assertEqual((marker["start_commit"], marker["run_started"], marker["ship_limit"]),
                         (head, marker["started"], None))

    def test_a_bounded_run_ends_when_it_reaches_its_limit(self):
        self.write_profile(tick_seconds=1)
        git(self.root, "commit", "-qam", "a one-second tick")
        done = self.launcher("start", "--ships", "1", heartbeat=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(self.marker()["ship_limit"], 1)
        self.calls(1)
        self.stamp_row()
        self.assertTrue(self.wait_session_gone("demo-heartbeat"), "the heartbeat outlived the run's limit")
        self.assertFalse(self.tmux_has("demo-run"))
        for rel in ("build/unattended-run.json", "build/unattended-launch.json"):
            self.assertFalse((self.root / rel).exists(), rel)
        health = json.loads((self.root / "build" / "unattended-health.json").read_text(encoding="utf-8"))
        self.assertEqual((health["alive"], health["ships"], health["ship_limit"]), (False, 1, 1))
        state = json.loads((self.root / "build" / "unattended-heartbeat.json").read_text(encoding="utf-8"))
        self.assertEqual(state["ended"]["reason"], "the ship limit was reached")

    def test_ship_limits_that_cannot_be_run_are_refused(self):
        for value in ("0", "-1", "x", "1.5"):
            with self.subTest(ships=value):
                done = self.launcher("start", "--ships", value, heartbeat=True)
                self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
                self.assertIn("whole number above 0", done.stderr)
        done = self.launcher("start", "--ships", "1")  # the helper adds --no-heartbeat
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("needs its heartbeat", done.stderr)
        self.assertFalse(self.log.exists(), "an agent started for a run that could not be bounded")
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_a_resume_keeps_the_runs_bound_and_baseline(self):
        self.assertEqual(self.launcher("start", "--ships", "2", heartbeat=True).returncode, 0)
        self.calls(1)
        before = self.marker()
        subprocess.run(["tmux", "kill-session", "-t", "=demo-heartbeat"], timeout=30, creationflags=windowless())
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        self.stamp_row()  # HEAD moves: the resume still counts from the run's own start
        done = self.launcher("start")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("needs its heartbeat", done.stderr)
        done = self.launcher("start", "--ships", "3", heartbeat=True)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("is bounded to 2 section(s)", done.stderr)
        self.assertEqual(len(self.calls(1)), 1, "a refused resume started an agent")
        done = self.launcher("start", heartbeat=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        after = self.marker()
        self.assertTrue(after["resumed"])
        self.assertEqual({k: after[k] for k in ("start_commit", "run_started", "ship_limit")},
                         {k: before[k] for k in ("start_commit", "run_started", "ship_limit")})

    def test_a_fresh_start_removes_another_runs_stall_notice_and_a_resume_keeps_it(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        self.calls(1)
        notice = self.root / "build" / "unattended-needs-operator.md"
        notice.write_text(f"# Nothing has shipped\n\nRun: {self.marker()['session']}\n", encoding="utf-8")
        subprocess.run(["tmux", "kill-session", "-t", "=demo-run"], timeout=30, creationflags=windowless())
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertTrue(self.marker()["resumed"])
        self.assertTrue(notice.exists(), "a resume removed its own run's notice")
        old = self.marker()["session"]
        self.assertEqual(self.launcher("stop").returncode, 0)
        self.assertFalse(notice.exists(), "stop left the stopped run's notice")
        # A notice a failed removal left behind, as from a run that ended while the disk was failing.
        notice.write_text(f"# Nothing has shipped\n\nRun: {old}\n", encoding="utf-8")
        done = self.launcher("start")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertFalse(notice.exists(), "a fresh run kept the previous run's notice")
        self.assertIn("stall notice is removed (a fresh run started)", done.stdout)

    def test_stop_records_the_end_where_the_heartbeat_would_have(self):
        # Round 1 of D02 T02 §8: stop ended the heartbeat first, so the health file said alive indefinitely.
        self.assertEqual(self.launcher("start").returncode, 0)
        tag = self.marker()["session"]
        build = self.root / "build"
        (build / "unattended-health.json").write_text(json.dumps({"alive": True, "run": tag}), encoding="utf-8")
        (build / "unattended-needs-operator.md").write_text(f"# Nothing has shipped\n\nRun: {tag}\n", encoding="utf-8")
        done = self.launcher("stop")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        health = json.loads((build / "unattended-health.json").read_text(encoding="utf-8"))
        self.assertEqual((health["alive"], health["run"]), (False, tag))
        state = json.loads((build / "unattended-heartbeat.json").read_text(encoding="utf-8"))
        self.assertEqual(state["ended"]["reason"], "stopped by the operator")
        self.assertFalse((build / "unattended-needs-operator.md").exists(), "the stopped run's notice was left")

    def test_a_stop_that_cannot_record_its_end_hands_it_to_the_heartbeat(self):
        # Completion review and reconciliations of D02 T02 §8: a stop on a full disk left health saying alive, and
        # only another command could fix it. The heartbeat now owns the end: it records it once the disk takes it.
        self.write_profile(tick_seconds=1)  # a running heartbeat looks again on its next tick
        git(self.root, "commit", "-qam", "a one-second tick")
        for running in (True, False):
            with self.subTest(heartbeat_running=running):
                self.end_everything()
                self.log.unlink(missing_ok=True)
                build = self.root / "build"
                for name in ("unattended-heartbeat.json", "unattended-health.json"):
                    (build / name).unlink(missing_ok=True)
                self.assertEqual(self.launcher("start", heartbeat=running).returncode, 0)
                tag = self.marker()["session"]
                (build / "unattended-health.json").write_text(json.dumps({"alive": True, "run": tag}), encoding="utf-8")
                run = self.run_module()
                real = run.lib.write_atomic

                def health_fails(path, text):
                    if path.name == "unattended-health.json":
                        raise OSError(28, "No space left on device")
                    return real(path, text)
                out = __import__("io").StringIO()
                # Only this process's writes fail; the heartbeat's own process writes once it gets the end.
                with unittest.mock.patch.object(run.lib, "write_atomic", health_fails), \
                        unittest.mock.patch.object(run, "STOP_RECORD_SECONDS", 1):
                    self.assertEqual(run.stop(self.root, out), 0, out.getvalue())
                self.assertIn("records it once the disk takes it", out.getvalue())
                self.assertFalse((build / "unattended-run.json").exists(), "a later start would resume a stopped run")
                self.assertFalse(self.tmux_has("demo-run"))
                self.assertTrue(self.wait_session_gone("demo-heartbeat"), "the heartbeat never finished the end")
                health = json.loads((build / "unattended-health.json").read_text(encoding="utf-8"))
                self.assertEqual((health["alive"], health["run"]), (False, tag))
                state = json.loads((build / "unattended-heartbeat.json").read_text(encoding="utf-8"))
                self.assertEqual(state["ended"]["reason"], "stopped by the operator")

    def test_a_stop_records_its_end_once_the_disk_takes_it(self):
        # Reconciliation 1 of D02 T02 §8: recovery needed a second stop; stop now retries its own record.
        self.assertEqual(self.launcher("start").returncode, 0)
        run = self.run_module()
        real, failures = run.lib.write_atomic, []

        def full_twice(path, text):
            if path.name == "unattended-health.json" and len(failures) < 2:
                failures.append(path)
                raise OSError(28, "No space left on device")
            return real(path, text)
        out = __import__("io").StringIO()
        with unittest.mock.patch.object(run.lib, "write_atomic", full_twice):
            self.assertEqual(run.stop(self.root, out), 0, out.getvalue())
        self.assertEqual(len(failures), 2)
        health = json.loads((self.root / "build" / "unattended-health.json").read_text(encoding="utf-8"))
        self.assertIs(health["alive"], False)
        self.assertFalse((self.root / "build" / "unattended-run.json").exists())

    def test_the_stall_nudge_reaches_the_runs_pane(self):
        echo = Path(self._tmp.name) / "typed.txt"
        self.assertEqual(self.launcher("start", env={**self.env, "STUB_ECHO": str(echo)}).returncode, 0)
        self.calls(1)
        run = self.run_module()
        now = time.time()
        marker = {**self.marker(), "run_started": run._iso(now - 7 * 3600)}  # nothing shipped for seven hours
        (self.root / "build" / "unattended-run.json").write_text(json.dumps(marker), encoding="utf-8")
        state = run.read_state(self.root, marker["session"])
        with unittest.mock.patch.object(run, "tmux", self.idle_tmux(run, PROFILE["idle_minutes"] * 60, lambda: now)):
            self.assertEqual(run.tick(self.root, now, state, __import__("io").StringIO()), 0)
        self.assertTrue((self.root / "build" / "unattended-needs-operator.md").exists())
        deadline = time.monotonic() + 15
        while not (echo.exists() and run.STALL_PROMPT in echo.read_text(encoding="utf-8")) and time.monotonic() < deadline:
            time.sleep(0.2)
        self.assertIn(run.STALL_PROMPT, echo.read_text(encoding="utf-8"), "the stall nudge never reached the pane")

    def test_a_run_cannot_stop_itself(self):
        self.assertEqual(self.launcher("start").returncode, 0)
        done = self.launcher("stop", env={**self.env, "CONCLAVE_UNATTENDED_RUN": "1"})
        self.assertEqual(done.returncode, 2)
        self.assertIn("refused inside the run", done.stdout)
        self.assertTrue(self.tmux_has("demo-run"))
        self.assertTrue((self.root / "build" / "unattended-run.json").exists())


class HeartbeatTickTest(unittest.TestCase):
    """The heartbeat's decisions, with an injected clock and tmux answers, on any system."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        (self.root / ".conclave").mkdir()
        (self.root / "docs").mkdir()
        (self.root / "docs" / "unattended-prompt.md").write_text(PROMPT, encoding="utf-8")
        (self.root / ".conclave" / "unattended.json").write_text(json.dumps(PROFILE), encoding="utf-8")
        self.run = load_run_module()
        self.marker = {"session": "tag-1", "agent_session": "tag-1", "tmux_session": "demo-run",
                       "heartbeat_session": "demo-heartbeat", "agent": "claude", "pgid": 4242,
                       "started": "2026-11-12T00:00:00Z", "resumed": False}
        (self.root / "build").mkdir()
        (self.root / "build" / "unattended-run.json").write_text(json.dumps(self.marker), encoding="utf-8")
        self.alive = True
        self.ended = []
        self.out = __import__("io").StringIO()

        def end_run(root, observed, reason, state, now, out, gone=False):
            # What the real end_run leaves behind once a run is over: its end in the state, no marker.
            self.ended.append(reason)
            state["ended"] = {"reason": reason, "at": "2026-11-12T00:00:00Z"}
            (root / "build" / "unattended-run.json").unlink()
            return self.run.OVER
        # No repository here: the ship count has tests of its own (HeartbeatShipsTest).
        for name, value in (("_alive", lambda name: self.alive), ("end_run", end_run),
                            ("count_ships", lambda root, marker, state, now: None)):
            patch = unittest.mock.patch.object(self.run, name, value)
            patch.start()
            self.addCleanup(patch.stop)

    def tick(self, now, state):
        return self.run.tick(self.root, now, state, self.out)

    def state(self):
        return self.run.read_state(self.root, "tag-1")

    def test_a_dead_run_is_ended_not_restarted(self):
        self.alive = False
        with unittest.mock.patch.object(self.run, "start", side_effect=AssertionError("the run was restarted")):
            self.assertEqual(self.tick(0.0, self.state()), self.run.FINISHED)
        self.assertEqual(self.ended, ["its session is gone"])

    def test_a_cleanup_that_fails_is_tried_again_on_the_next_tick(self):
        self.alive = False
        for outcome in (self.run.RETRY, self.run.REPLACED):  # a replaced run is supervised on, not finished
            with self.subTest(outcome=outcome), unittest.mock.patch.object(self.run, "end_run", return_value=outcome):
                self.assertEqual(self.tick(0.0, self.state()), 0)

    def test_a_tick_that_fails_is_recorded_and_supervision_goes_on(self):
        # Round 1 of D02 T02 §7: a held launch lock or a guard timeout ended the heartbeat with the run still marked.
        failures = [self.run.lib.LaunchError("build/unattended-launch.lock is held"),
                    subprocess.TimeoutExpired("campaign_guard.py", 120), OSError(5, "Input/output error")]
        sleeps = []

        def stop_after(seconds):
            sleeps.append(seconds)
            if len(sleeps) == len(failures):
                raise StopIteration
        with unittest.mock.patch.object(self.run, "tick", side_effect=failures):
            with self.assertRaises(StopIteration):
                self.run.heartbeat(self.root, clock=lambda: 0.0, sleep=stop_after, out=self.out)
        self.assertEqual(self.out.getvalue().count("the tick failed"), 3)
        self.assertIn("Input/output error", self.state()["last_error"])
        with unittest.mock.patch.object(self.run, "tick", side_effect=failures[0]):
            self.assertEqual(self.run.heartbeat(self.root, once=True, out=self.out), 1)

    def test_a_profile_broken_mid_run_keeps_the_last_tick(self):
        sleeps = []

        def stop_after_two(seconds):
            sleeps.append(seconds)
            (self.root / ".conclave" / "unattended.json").write_text("{not json", encoding="utf-8")
            if len(sleeps) == 2:
                raise StopIteration
        with unittest.mock.patch.object(self.run, "_nudge"):
            with self.assertRaises(StopIteration):
                self.run.heartbeat(self.root, clock=lambda: 0.0, sleep=stop_after_two, out=self.out)
        self.assertEqual(sleeps, [PROFILE["tick_seconds"]] * 2)
        self.assertIn("the last tick of", self.out.getvalue())

    def flaky_writes(self, failures):
        """write_atomic failing its first `failures` calls with a full disk, then writing."""
        real, calls = self.run.lib.write_atomic, []

        def write(path, text):
            calls.append(path)
            if len(calls) <= failures:
                raise OSError(28, "No space left on device")
            return real(path, text)
        return unittest.mock.patch.object(self.run.lib, "write_atomic", write)

    def saved(self):
        return json.loads((self.root / "build" / "unattended-heartbeat.json").read_text(encoding="utf-8"))

    def test_an_ended_record_whose_write_fails_is_written_again_before_the_heartbeat_stops(self):
        # Completion review of D02 T02 §7: the loop exited 10 with the record only in memory, and lost it.
        self.alive = False
        sleeps = []
        with self.flaky_writes(2):
            code = self.run.heartbeat(self.root, clock=lambda: 0.0, sleep=sleeps.append, out=self.out)
        self.assertEqual((code, len(sleeps), self.ended), (self.run.FINISHED, 2, ["its session is gone"]))
        self.assertEqual(self.saved()["ended"]["reason"], "its session is gone")
        self.assertIn("written again next tick", self.out.getvalue())

    def test_a_run_started_while_an_ended_record_waits_is_supervised(self):
        self.alive = False
        sleeps = []

        def operator_starts(seconds):
            sleeps.append(seconds)
            if len(sleeps) == 1:  # the operator starts a new run while the old record is unwritten
                self.alive = True
                (self.root / "build" / "unattended-run.json").write_text(
                    json.dumps({**self.marker, "session": "tag-2", "agent_session": "tag-2", "pgid": 4343}),
                    encoding="utf-8")
            else:
                raise StopIteration
        with self.flaky_writes(1), unittest.mock.patch.object(self.run, "_nudge") as nudged:
            with self.assertRaises(StopIteration):
                self.run.heartbeat(self.root, clock=lambda: 0.0, sleep=operator_starts, out=self.out)
        self.assertIn("the operator's new run is supervised now", self.out.getvalue())
        self.assertEqual((nudged.call_count, self.saved()["session"]), (1, "tag-2"))

    def test_an_unreadable_marker_while_an_ended_record_waits_keeps_the_record(self):
        # Reconciliation 1 of D02 T02 §7: an I/O error on the marker dropped the record still to be written.
        self.alive = False
        marker, sleeps = self.root / "build" / "unattended-run.json", []

        def flaky_marker(seconds):
            sleeps.append(seconds)
            if len(sleeps) == 1:
                marker.write_text('{"session": ', encoding="utf-8")  # cannot be read for a moment
            elif len(sleeps) == 2:
                marker.unlink()  # reads again: no run
            else:
                raise StopIteration
        with self.flaky_writes(2):
            code = self.run.heartbeat(self.root, clock=lambda: 0.0, sleep=flaky_marker, out=self.out)
        self.assertEqual((code, len(sleeps)), (self.run.FINISHED, 2))
        self.assertEqual(self.saved()["ended"]["reason"], "its session is gone")
        self.assertNotIn("the operator's new run is supervised now", self.out.getvalue())

    def test_heartbeat_once_writes_an_ended_record_again_until_it_lands(self):
        self.alive = False
        sleeps = []
        with self.flaky_writes(1):
            self.assertEqual(self.run.heartbeat(self.root, once=True, clock=lambda: 0.0, sleep=sleeps.append,
                                                out=self.out), self.run.FINISHED)
        self.assertEqual((len(sleeps), self.saved()["ended"]["reason"]), (1, "its session is gone"))

    def test_with_no_run_the_last_runs_record_is_left_as_it_was(self):
        record = self.root / "build" / "unattended-heartbeat.json"
        record.write_text(json.dumps({"session": "tag-1", "last_nudge": None, "last_error": None,
                                      "ended": {"reason": "its session is gone", "at": "2026-11-12T00:00:00Z"}}),
                          encoding="utf-8")
        before = record.read_bytes()
        (self.root / "build" / "unattended-run.json").unlink()
        self.assertEqual(self.run.heartbeat(self.root, once=True, out=self.out), self.run.FINISHED)
        self.assertEqual(record.read_bytes(), before, "a heartbeat with no run overwrote the last run's record")

    def test_an_unreadable_marker_is_looked_at_again_and_the_runs_state_kept(self):
        # Completion review of D02 T02 §7: a marker that could not be read ended supervision like an absent one.
        marker = self.root / "build" / "unattended-run.json"
        good = marker.read_text(encoding="utf-8")
        (self.root / "build" / "unattended-heartbeat.json").write_text(
            json.dumps({"session": "tag-1", "last_nudge": 500.0, "last_error": None, "ended": None}), encoding="utf-8")
        marker.write_text('{"session": "tag-1", "half": ', encoding="utf-8")  # a write in flight
        with unittest.mock.patch.object(self.run, "_nudge", side_effect=AssertionError("nudged an unread run")):
            self.assertEqual(self.tick(0.0, self.state()), 0)
        self.assertEqual(self.ended, [])
        sleeps = []

        def readable_again(seconds):
            sleeps.append(seconds)
            marker.write_text(good, encoding="utf-8")
            if len(sleeps) == 2:
                raise StopIteration
        marker.write_text('{"session": "tag-1", "half": ', encoding="utf-8")
        with unittest.mock.patch.object(self.run, "_nudge") as nudged:
            with self.assertRaises(StopIteration):
                self.run.heartbeat(self.root, clock=lambda: 0.0, sleep=readable_again, out=self.out)
        self.assertIn("the run is not touched, looked at again next tick", self.out.getvalue())
        self.assertEqual(nudged.call_count, 1, "the run was not supervised once its marker read again")
        self.assertEqual((self.saved()["session"], self.saved()["last_nudge"]), ("tag-1", 500.0))

    def test_a_run_its_agent_ended_is_finished_and_another_runs_end_is_not(self):
        (self.root / "build" / "claude-campaign-ended.json").write_text(
            json.dumps({"launch_tag": "other-run", "session_id": "other", "reason": "park"}), encoding="utf-8")
        with unittest.mock.patch.object(self.run, "_nudge"):
            self.assertEqual(self.tick(0.0, self.state()), 0, "another run's end ended this one")
        (self.root / "build" / "claude-campaign-ended.json").write_text(
            json.dumps({"launch_tag": "tag-1", "session_id": "tag-1", "reason": "closeout"}), encoding="utf-8")
        self.assertEqual(self.tick(1.0, self.state()), self.run.FINISHED)
        self.assertEqual(self.ended, ["the run ended itself"])

    def test_a_run_that_never_took_its_guard_is_not_finished(self):
        with unittest.mock.patch.object(self.run, "_nudge"):
            self.assertEqual(self.tick(0.0, self.state()), 0)
        self.assertEqual(self.ended, [])

    def test_no_marker_ends_the_heartbeat(self):
        (self.root / "build" / "unattended-run.json").unlink()
        self.assertEqual(self.tick(0.0, self.state()), self.run.FINISHED)

    def test_the_state_is_the_runs_own_and_is_read_back(self):
        saved = {"session": "tag-1", "last_nudge": 500.0, "last_error": None, "ended": None}
        (self.root / "build" / "unattended-heartbeat.json").write_text(json.dumps(saved), encoding="utf-8")
        self.assertEqual(self.run.read_state(self.root, "tag-1")["last_nudge"], 500.0)
        fresh = self.run.read_state(self.root, "tag-2")
        self.assertEqual((fresh["session"], fresh["last_nudge"]), ("tag-2", None), "a new run inherited the state")

    def test_a_state_write_that_fails_keeps_supervising_with_the_state_in_memory(self):
        clock = iter([0.0, 100.0, 200.0])
        sleeps, calls = [], []
        real = self.run.lib.write_atomic

        def flaky(path, text):
            calls.append(1)
            if len(calls) <= 2:
                raise OSError(28, "No space left on device")
            return real(path, text)

        def stop_after_three(_seconds):
            sleeps.append(1)
            if len(sleeps) == 3:
                raise StopIteration
        with unittest.mock.patch.object(self.run.lib, "write_atomic", flaky), \
                unittest.mock.patch.object(self.run, "_nudge"):
            with self.assertRaises(StopIteration):
                self.run.heartbeat(self.root, clock=lambda: next(clock), sleep=stop_after_three, out=self.out)
        self.assertIn("could not be written (No space left on device); kept in memory", self.out.getvalue())
        self.assertTrue((self.root / "build" / "unattended-heartbeat.json").exists(), "no write ever succeeded")

    def nudge(self, activity_age, clients, state=None, now=10_000.0, fail=None):
        sent = []

        def tmux(*args, check=False):
            ok = 1 if fail == args[0] else 0
            if args[0] == "display-message":
                return subprocess.CompletedProcess(args, ok, stdout=f"{int(now - activity_age)}\n", stderr="")
            if args[0] == "list-clients":
                return subprocess.CompletedProcess(args, ok, stdout="".join(f"{c}\n" for c in clients), stderr="")
            if args[0] == "send-keys":
                sent.append(args)
                return subprocess.CompletedProcess(args, ok, stdout="", stderr="no such pane" if ok else "")
            return subprocess.CompletedProcess(args, 0, stdout="", stderr="")
        state = state if state is not None else self.state()
        with unittest.mock.patch.object(self.run, "tmux", tmux), \
                unittest.mock.patch.object(self.run, "note_resumption") as noted:
            self.tick(now, state)
        return sent, state, noted.call_count

    def test_an_idle_run_is_nudged_and_counted_and_an_active_one_is_not(self):
        idle = PROFILE["idle_minutes"] * 60
        sent, state, noted = self.nudge(idle, [])
        self.assertEqual(sent[0][-1], self.run.NUDGE_PROMPT)
        self.assertEqual((state["last_nudge"], noted), (10_000.0, 1))
        self.assertEqual(self.nudge(idle - 1, [])[0], [])

    def test_an_operator_typing_suppresses_the_nudge_and_a_watcher_does_not(self):
        idle = PROFILE["idle_minutes"] * 60
        self.assertEqual(self.nudge(idle, ["0"])[0], [], "a nudge went to a run an operator is attached to")
        self.assertTrue(self.nudge(idle, ["1"])[0], "a read-only watcher suppressed the nudge")

    def test_a_nudge_is_sent_at_most_once_per_idle_minutes_across_a_reload(self):
        idle = PROFILE["idle_minutes"] * 60
        _sent, state, _ = self.nudge(idle, [], now=10_000.0)
        (self.root / "build" / "unattended-heartbeat.json").write_text(json.dumps(state), encoding="utf-8")
        reloaded = self.state()
        self.assertEqual(self.nudge(idle * 3, [], state=reloaded, now=10_000.0 + idle - 1)[0], [])
        self.assertTrue(self.nudge(idle * 3, [], state=reloaded, now=10_000.0 + idle)[0])

    def test_a_failed_tmux_query_or_send_is_not_counted_as_a_nudge(self):
        idle = PROFILE["idle_minutes"] * 60
        for failing in ("display-message", "list-clients", "send-keys"):
            with self.subTest(failing=failing):
                _sent, state, noted = self.nudge(idle, [], fail=failing)
                self.assertIsNone(state["last_nudge"])
                self.assertEqual(noted, 0)
                self.assertTrue(state["last_error"])


T0 = 1_700_000_000  # the fixture's first commit time; the tests' clocks count from it


class HeartbeatShipsTest(unittest.TestCase):
    """The heartbeat's one ship count, its health file, the ship limit and the stall notice (D02 T02 §8), over a real
    repository with injected clocks and tmux answers, on any system."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "repo"
        for rel, source in (("scripts/unattended-run.py", TEMPLATES / "scripts" / "unattended-run.py"),
                            ("scripts/lib/unattended.py", TEMPLATES / "scripts" / "lib" / "unattended.py"),
                            ("scripts/lib/todo_graph.py", TODO / "scripts" / "lib" / "todo_graph.py")):
            (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, self.root / rel)
        (self.root / ".conclave").mkdir()
        self.write_profile()
        (self.root / "docs").mkdir()
        (self.root / "docs" / "unattended-prompt.md").write_text(PROMPT, encoding="utf-8")
        (self.root / ".gitignore").write_text("build/\n", encoding="utf-8")
        self.todo = self.root / "todo" / "02-demo" / "TODO-01-demo.md"
        self.todo.parent.mkdir(parents=True)
        for args in (["init", "-q", "-b", "main"], ["config", "user.email", "t@t"], ["config", "user.name", "t"],
                     ["config", "commit.gpgsign", "false"]):
            git(self.root, *args)
        self.base = self.commit("[ ]", "[ ]", "fixture", T0)
        spec = importlib.util.spec_from_file_location("ships_run", self.root / "scripts" / "unattended-run.py")
        self.run = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.run)
        self.marker = {"session": "tag-1", "agent_session": "tag-1", "tmux_session": "demo-run",
                       "heartbeat_session": "demo-heartbeat", "agent": "claude", "pgid": 4242,
                       "started": self.run._iso(T0), "resumed": False, "start_commit": self.base,
                       "run_started": self.run._iso(T0), "ship_limit": None}
        self.write_marker()
        self.alive, self.ended, self.out = True, [], __import__("io").StringIO()

        def end_run(root, observed, reason, state, now, out, gone=False):
            self.ended.append(reason)
            state["ended"] = {"reason": reason, "at": self.run._iso(now)}
            (root / "build" / "unattended-run.json").unlink()
            return self.run.OVER
        self.nudge = unittest.mock.MagicMock()
        for name, value in (("_alive", lambda name: self.alive), ("end_run", end_run), ("_nudge", self.nudge)):
            patch = unittest.mock.patch.object(self.run, name, value)
            patch.start()
            self.addCleanup(patch.stop)

    def write_profile(self, **changes):
        (self.root / ".conclave" / "unattended.json").write_text(json.dumps({**PROFILE, **changes}), encoding="utf-8")

    def write_marker(self, **changes):
        (self.root / "build").mkdir(exist_ok=True)
        (self.root / "build" / "unattended-run.json").write_text(json.dumps({**self.marker, **changes}),
                                                                 encoding="utf-8")

    def commit(self, s1, s2, message, when):
        self.todo.write_text(TABLE.format(s1=s1, s2=s2), encoding="utf-8")
        git(self.root, "add", "-A")
        env = {**os.environ, "GIT_COMMITTER_DATE": f"@{when} +0000", "GIT_AUTHOR_DATE": f"@{when} +0000"}
        subprocess.run(["git", "commit", "-qm", message], cwd=self.root, check=True, capture_output=True, timeout=60,
                       env=env, creationflags=windowless())
        return git(self.root, "rev-parse", "HEAD").stdout.strip()

    def state(self, tag="tag-1"):
        return self.run.read_state(self.root, tag)

    def tick(self, now, state):
        return self.run.tick(self.root, now, state, self.out)

    def file(self, rel):
        path = self.root / rel
        return json.loads(path.read_text(encoding="utf-8")) if path.exists() else None

    def test_the_count_advances_by_new_commits_only_and_survives_a_reload(self):
        state = self.state()
        self.tick(T0 + 10, state)
        self.assertEqual((state["ships"], state["counted"], state["last_ship"]), (0, self.base, None))
        self.commit("[x]", "[ ]", "stamp §1", T0 + 100)
        second = self.commit("[x]", "[x]", "stamp §2", T0 + 200)  # two ships between two ticks
        self.tick(T0 + 300, state)
        self.assertEqual((state["ships"], state["counted"]), (2, second))
        self.assertEqual(state["last_ship"], {"commit": second, "at": self.run._iso(T0 + 200)})
        self.run.lib.write_atomic(self.root / "build" / "unattended-heartbeat.json", json.dumps(state))
        reloaded = self.state()
        with unittest.mock.patch.object(self.run.lib, "ships_along", wraps=self.run.lib.ships_along) as along:
            self.tick(T0 + 400, reloaded)
        self.assertEqual((reloaded["ships"], along.call_count), (2, 0), "a reload counted a commit again")
        (self.root / "build" / "unattended-heartbeat.json").unlink()
        fresh = self.state()
        self.tick(T0 + 500, fresh)
        self.assertEqual(fresh["ships"], 2, "the count was not rebuilt from the run's start commit")

    def test_a_git_read_that_fails_is_counted_again_not_read_as_absent(self):
        # Reconciliation 1 of D02 T02 §8: a failed `git show` read as a missing file, and the ship was passed for good.
        state = self.state()
        self.tick(T0 + 10, state)
        shipped = self.commit("[x]", "[ ]", "stamp §1", T0 + 100)
        real, failed = self.run.lib._git, []

        def flaky(root, *args, **kwargs):
            if args[:1] == ("show",) and not failed:
                failed.append(args)
                raise self.run.lib.ShipCountError("git show failed: error: unable to read (I/O error)")
            return real(root, *args, **kwargs)
        with unittest.mock.patch.object(self.run.lib, "_git", flaky):
            self.tick(T0 + 200, state)
        self.assertEqual((state["ships"], state["counted"]), (0, self.base), "the unread commit was counted past")
        self.assertIn("ships could not be counted", state["last_error"])
        self.tick(T0 + 300, state)
        self.assertEqual((state["ships"], state["counted"]), (1, shipped))

    def test_a_failed_state_write_counts_no_commit_twice(self):
        real, calls = self.run.lib.write_atomic, []

        def flaky(path, text):
            calls.append(path.name)
            if calls.count("unattended-heartbeat.json") == 1:
                raise OSError(28, "No space left on device")
            return real(path, text)
        sleeps = []

        def ship_then_stop(seconds):
            sleeps.append(seconds)
            if len(sleeps) == 1:
                self.commit("[x]", "[x]", "stamp §2", T0 + 200)
            else:
                raise StopIteration
        self.commit("[x]", "[ ]", "stamp §1", T0 + 100)
        with unittest.mock.patch.object(self.run.lib, "write_atomic", flaky):
            with self.assertRaises(StopIteration):
                self.run.heartbeat(self.root, clock=lambda: T0 + 300, sleep=ship_then_stop, out=self.out)
        self.assertEqual(self.file("build/unattended-heartbeat.json")["ships"], 2)
        self.assertEqual(self.file("build/unattended-health.json")["ships"], 2)

    def test_the_health_file_holds_exact_values_before_and_after_a_ship(self):
        self.assertEqual(self.run.heartbeat(self.root, once=True, clock=lambda: T0 + 3600, out=self.out), 0)
        self.assertEqual(self.file("build/unattended-health.json"),
                         {"alive": True, "run": "tag-1", "ships": 0, "ship_limit": None, "last_ship": None,
                          "hours_since_ship": 1.0, "last_nudge": None, "updated": self.run._iso(T0 + 3600)})
        shipped = self.commit("[x]", "[ ]", "stamp §1", T0 + 7200)
        self.run.heartbeat(self.root, once=True, clock=lambda: T0 + 9000, out=self.out)
        health = self.file("build/unattended-health.json")
        self.assertEqual((health["ships"], health["hours_since_ship"]), (1, 0.5))
        self.assertEqual(health["last_ship"], {"commit": shipped, "at": self.run._iso(T0 + 7200)})

    def test_a_resume_before_the_first_ship_keeps_the_stall_clock(self):
        self.write_marker(started=self.run._iso(T0 + 5000), resumed=True)  # `started` is the resume's own time
        self.run.heartbeat(self.root, once=True, clock=lambda: T0 + 7200, out=self.out)
        self.assertEqual(self.file("build/unattended-health.json")["hours_since_ship"], 2.0)

    def test_the_ended_runs_health_says_not_alive_after_a_failed_write(self):
        self.alive = False
        real, failed = self.run.lib.write_atomic, []

        def health_fails_once(path, text):
            if path.name == "unattended-health.json" and not failed:
                failed.append(1)
                raise OSError(28, "No space left on device")
            return real(path, text)
        sleeps = []
        with unittest.mock.patch.object(self.run.lib, "write_atomic", health_fails_once):
            code = self.run.heartbeat(self.root, clock=lambda: T0 + 60, sleep=sleeps.append, out=self.out)
        self.assertEqual((code, len(sleeps)), (self.run.FINISHED, 1))
        self.assertIs(self.file("build/unattended-health.json")["alive"], False)

    def test_a_legacy_marker_is_counted_from_the_heartbeats_first_look(self):
        legacy = {k: v for k, v in self.marker.items() if k not in ("start_commit", "run_started", "ship_limit")}
        (self.root / "build" / "unattended-run.json").write_text(json.dumps(legacy), encoding="utf-8")
        before = self.commit("[x]", "[ ]", "stamped before the heartbeat looked", T0 + 100)
        state = self.state()
        self.tick(T0 + 200, state)
        self.assertEqual((state["base"], state["ships"], state["run_started"]), (before, 0, self.run._iso(T0)))
        self.commit("[x]", "[x]", "stamp §2", T0 + 300)
        self.tick(T0 + 400, state)
        self.assertEqual(state["ships"], 1)
        self.assertEqual(self.run.read_marker(self.root)[0]["ship_limit"], None, "a legacy run is unbounded")

    def test_a_bounded_run_is_ended_when_it_reaches_its_limit(self):
        self.write_marker(ship_limit=2)
        self.commit("[x]", "[ ]", "stamp §1", T0 + 100)
        state = self.state()
        self.assertEqual(self.tick(T0 + 200, state), 0)
        self.commit("[x]", "[x]", "stamp §2", T0 + 300)
        self.assertEqual(self.tick(T0 + 400, state), self.run.FINISHED)
        self.assertEqual(self.ended, ["the ship limit was reached"])
        self.assertIs(state["alive"], False)

    def notice(self):
        path = self.root / "build" / "unattended-needs-operator.md"
        return path.read_text(encoding="utf-8") if path.exists() else None

    def test_a_run_ended_at_its_limit_leaves_no_stall_notice(self):
        # Round 1 of D02 T02 §8: the limit's end returned before the notice could go, and status kept showing it.
        self.write_marker(ship_limit=1)
        state = self.state()
        self.tick(T0 + 6 * 3600, state)
        self.assertIsNotNone(self.notice())
        self.commit("[x]", "[ ]", "stamp §1", T0 + 6 * 3600 + 30)
        self.assertEqual(self.tick(T0 + 6 * 3600 + 60, state), self.run.FINISHED)
        self.assertIsNone(self.notice(), "the ended run's stall notice was left")

    def test_the_stall_notice_is_written_once_per_stall_hours_and_the_stall_nudge_sent(self):
        state = self.state()
        self.tick(T0 + 6 * 3600 - 60, state)
        self.assertIsNone(self.notice())
        self.assertEqual(self.nudge.call_args.args[-1], self.run.NUDGE_PROMPT)
        self.tick(T0 + 6 * 3600, state)
        first = self.notice()
        self.assertIn("Run: tag-1\n", first)
        self.assertIn("nothing has shipped since the run started", first)
        self.assertEqual(self.nudge.call_args.args[-1], self.run.STALL_PROMPT)
        self.run.lib.write_atomic(self.root / "build" / "unattended-heartbeat.json", json.dumps(state))
        reloaded = self.state()
        with unittest.mock.patch.object(self.run.lib, "write_atomic", wraps=self.run.lib.write_atomic) as writes:
            self.tick(T0 + 7 * 3600, reloaded)
        self.assertEqual([c.args[0].name for c in writes.call_args_list], [], "the notice was written again")
        self.assertEqual(self.nudge.call_args.args[-1], self.run.STALL_PROMPT)
        shipped = self.commit("[x]", "[ ]", "stamp §1", T0 + 8 * 3600)
        self.tick(T0 + 8 * 3600 + 60, reloaded)
        self.assertIsNone(self.notice(), "the notice outlived the next ship")
        self.assertEqual(reloaded["last_ship"]["commit"], shipped)

    def test_an_ended_runs_notice_whose_removal_fails_is_removed_on_the_next_tick(self):
        # Completion review of D02 T02 §8: the removal failed once and nothing tried it again.
        self.write_marker(ship_limit=1)
        state = self.state()
        self.tick(T0 + 6 * 3600, state)
        self.run.lib.write_atomic(self.root / "build" / "unattended-heartbeat.json", json.dumps(state))
        self.commit("[x]", "[ ]", "stamp §1", T0 + 6 * 3600 + 30)
        real, failures = Path.unlink, []

        def unlink(path, *args, **kwargs):
            if path.name == "unattended-needs-operator.md" and len(failures) < 2:
                failures.append(path)
                raise OSError(5, "Input/output error")
            return real(path, *args, **kwargs)
        sleeps = []
        with unittest.mock.patch.object(Path, "unlink", unlink):
            code = self.run.heartbeat(self.root, clock=lambda: T0 + 6 * 3600 + 60, sleep=sleeps.append, out=self.out)
        self.assertEqual((code, len(sleeps), len(failures)), (self.run.FINISHED, 1, 2))
        self.assertIsNone(self.notice(), "the ended run's notice outlived the retry")
        self.assertIs(self.file("build/unattended-health.json")["alive"], False)

    def test_a_tick_that_saw_the_run_alive_never_writes_over_a_stop(self):
        # Reconciliation 3 of D02 T02 §8: a heartbeat that observed the live run wrote its snapshot after `stop` had
        # recorded the end, leaving health alive and no end record, with nothing left to retry.
        real_tick, sleeps = self.run.tick, []

        def tick_then_stop(root, now, state, out=None):
            code = real_tick(root, now, state, out)  # the heartbeat sees the run alive
            self.run.record_stop(root, "tag-1", now, self.out)  # the operator's stop lands before its write
            (root / "build" / "unattended-run.json").unlink()
            return code

        def stop_after_one(seconds):
            sleeps.append(seconds)
            raise StopIteration
        with unittest.mock.patch.object(self.run, "tick", tick_then_stop):
            with self.assertRaises(StopIteration):
                self.run.heartbeat(self.root, clock=lambda: T0 + 60, sleep=stop_after_one, out=self.out)
        self.assertIs(self.file("build/unattended-health.json")["alive"], False, "a stale tick overwrote the stop")
        self.assertEqual(self.file("build/unattended-heartbeat.json")["ended"]["reason"], "stopped by the operator")

    def test_a_stop_handed_over_while_a_tick_ran_is_recorded_before_the_heartbeat_stops(self):
        # Reconciliation 4 of D02 T02 §8: the loop read the marker, stop removed it and handed its end over, the tick
        # found no marker, and the heartbeat exited without the record.
        real_tick, handed = self.run.tick, []

        def stop_hands_over_mid_tick(root, now, state, out=None):
            if not handed:
                handed.append(1)
                (root / "build" / "unattended-run.json").unlink()  # the record failed; the marker went anyway
            return real_tick(root, now, state, out)
        sleeps = []
        with unittest.mock.patch.object(self.run, "tick", stop_hands_over_mid_tick), \
                unittest.mock.patch.object(self.run, "stopped_run", lambda: "tag-1"):
            code = self.run.heartbeat(self.root, clock=lambda: T0 + 60, sleep=sleeps.append, out=self.out)
        self.assertEqual((code, len(sleeps)), (self.run.FINISHED, 1))
        self.assertIs(self.file("build/unattended-health.json")["alive"], False)
        self.assertEqual(self.file("build/unattended-heartbeat.json")["ended"]["reason"], "stopped by the operator")

    def test_a_held_launch_lock_keeps_an_ended_runs_record_pending(self):
        self.alive = False
        held = self.run.lib.launch_lock(self.root)
        sleeps = []

        def release_after_one(seconds):
            sleeps.append(seconds)
            held.__exit__(None, None, None)
        held.__enter__()
        code = self.run.heartbeat(self.root, once=True, clock=lambda: T0 + 60, sleep=release_after_one, out=self.out)
        self.assertEqual((code, len(sleeps)), (self.run.FINISHED, 1))
        self.assertEqual(self.file("build/unattended-heartbeat.json")["ended"]["reason"], "its session is gone")

    def test_a_shorter_stall_hours_and_a_failed_notice_write(self):
        self.write_profile(stall_hours=3)
        state = self.state()
        real, failed = self.run.lib.write_atomic, []

        def notice_fails_once(path, text):
            if path.name == "unattended-needs-operator.md" and not failed:
                failed.append(1)
                raise OSError(28, "No space left on device")
            return real(path, text)
        with unittest.mock.patch.object(self.run.lib, "write_atomic", notice_fails_once):
            self.tick(T0 + 3 * 3600, state)
        self.assertIsNone(self.notice())
        self.assertIn("the stall notice could not be written", state["last_error"])
        self.assertIsNone(state["stall_notice_at"], "a failed write moved the throttle")
        self.tick(T0 + 3 * 3600 + 60, state)
        self.assertIn("stall_hours is 3", self.notice())

    def test_another_runs_notice_is_not_removed_by_this_run(self):
        self.run.lib.write_atomic(self.root / "build" / "unattended-needs-operator.md", "# x\n\nRun: tag-0\n")
        self.tick(T0 + 60, self.state())
        self.assertIn("Run: tag-0", self.notice())

    def test_status_shows_the_stall_notice(self):
        state = self.state()
        self.tick(T0 + 6 * 3600, state)
        out = __import__("io").StringIO()
        self.run.status(self.root, out)
        self.assertIn("needs operator:\n# Nothing has shipped for 6.0 hours", out.getvalue())


class KillWaitTest(unittest.TestCase):
    @unittest.skipUnless(hasattr(signal, "SIGKILL"), "SIGKILL is POSIX")
    def test_a_killed_process_slow_to_die_is_waited_for(self):
        # 2026-11-12, WSL under load: a crashed run's child took over 10 s to die after SIGKILL (its I/O on /mnt had
        # to finish first), and the operator's resume was refused. A simulated clock: it dies 20 s after the kill.
        run = load_run_module()
        clock, killed = {"now": 0.0}, {}

        def tagged(_session):
            return [] if "at" in killed and clock["now"] - killed["at"] >= 20 else [4242]

        def sleep(seconds):
            clock["now"] += seconds
        with unittest.mock.patch.object(run, "tagged", tagged), \
                unittest.mock.patch.object(run.os, "kill", lambda pid, sig: killed.setdefault("at", clock["now"])), \
                unittest.mock.patch.object(run.time, "monotonic", lambda: clock["now"]), \
                unittest.mock.patch.object(run.time, "sleep", sleep):
            self.assertEqual(run.end_run_processes("tag-1", None, grace=2), [])
        self.assertIn("at", killed, "the process was never sent SIGKILL")

    @unittest.skipUnless(hasattr(signal, "SIGKILL"), "SIGKILL is POSIX")
    def test_a_killed_group_leader_not_yet_reaped_is_waited_for(self):
        # 2026-11-13, CI and WSL: the group was read once, right after SIGKILL, while its killed leader waited to be
        # reaped, so a clean stop or resume reported a survivor. Simulated clock: the group goes 2 s after the kill.
        run = load_run_module()
        clock, killed = {"now": 0.0}, {}

        def group_alive(_pgid):
            return not ("at" in killed and clock["now"] - killed["at"] >= 2)

        def sleep(seconds):
            clock["now"] += seconds
        with unittest.mock.patch.object(run, "tagged", lambda _session: []), \
                unittest.mock.patch.object(run, "_group_alive", group_alive), \
                unittest.mock.patch.object(run.os, "killpg", lambda pgid, sig: killed.setdefault("at", clock["now"])), \
                unittest.mock.patch.object(run.time, "monotonic", lambda: clock["now"]), \
                unittest.mock.patch.object(run.time, "sleep", sleep):
            self.assertEqual(run.end_run_processes("tag-1", 4242, grace=2), [])


class DescribeTest(unittest.TestCase):
    @unittest.skipUnless(os.path.isdir("/proc/self"), "/proc is Linux's")
    def test_a_surviving_process_is_named_with_its_command_state_and_parent(self):
        run = load_run_module()
        said = run.describe([os.getpid(), 2 ** 22 + 1])
        self.assertRegex(said, rf"^{os.getpid()} \(.*python.*; state \S+ .*; parent {os.getppid()}\), ")
        self.assertTrue(said.endswith(f"{2 ** 22 + 1} (ended since)"))


class WindowsTest(unittest.TestCase):
    def test_windows_is_told_to_use_wsl(self):
        run = load_run_module()
        with unittest.mock.patch.object(run.os, "name", "nt"), \
                unittest.mock.patch.object(run.sys, "stderr", new_callable=lambda: __import__("io").StringIO()) as err:
            self.assertEqual(run.main(["status"]), 1)
        self.assertIn("run this in WSL", err.getvalue())


if __name__ == "__main__":
    unittest.main()
