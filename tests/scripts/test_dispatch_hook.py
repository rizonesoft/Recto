"""scripts/dispatch_hook.py: a review runs only through the shell tool's background option (ported from Is It Legit)."""
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

HOOK = Path(__file__).resolve().parents[2] / "scripts" / "dispatch_hook.py"
REVIEW = "python scripts/review-dispatch.py --writer claude --prompt R:/scratch/p.md --record docs/reviews/01-engine/D01-T02-s1.md"


def run(payload, extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    data = payload if isinstance(payload, str) else json.dumps(payload)
    env = {k: v for k, v in os.environ.items() if k not in ("CONCLAVE_UNATTENDED_RUN", "CLAUDE_PROJECT_DIR")}
    if extra_env:
        env.update(extra_env)
    return subprocess.run([sys.executable, str(HOOK)], input=data, capture_output=True, text=True, env=env, timeout=30)


def call(tool: str, command: str, background=None) -> dict:
    tool_input = {"command": command}
    if background is not None:
        tool_input["run_in_background"] = background
    return {"tool_name": tool, "tool_input": tool_input}


def agy_call(name: str, args: dict, conversation_id: str = "conv-1", workspace: str = "", cwd: str = "",
             model_name: str = "Orchestrator (Carbon)") -> dict:
    payload = {"hookEventName": "PreToolUse", "conversationId": conversation_id, "modelName": model_name,
               "toolCall": {"id": "call-1", "name": name, "args": args}}
    if workspace:
        payload["workspacePaths"] = [workspace]
    if cwd:
        payload["cwd"] = cwd
    return payload


class DispatchHook(unittest.TestCase):
    def test_a_review_through_the_background_option_runs(self):
        for tool in ("Bash", "PowerShell"):
            for command in (REVIEW, f"{REVIEW} > R:/scratch/log 2>&1; echo done"):
                result = run(call(tool, command, background=True))
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_detached_bash_review_is_refused_even_with_the_option(self):
        for command in (f"{REVIEW} > /tmp/log 2>&1 &", f"nohup {REVIEW} > /tmp/log 2>&1 &",
                        f"({REVIEW}; echo done) & disown", f"setsid {REVIEW}", f"{REVIEW} & sleep 3"):
            for background in (True, False, None):
                result = run(call("Bash", command, background=background))
                self.assertEqual(result.returncode, 2, f"{command} {background}")
                self.assertIn("run_in_background", result.stderr)
                self.assertIn("not detached", result.stderr)

    def test_a_detached_powershell_review_is_refused_even_with_the_option(self):
        for command in (f"Start-Job {{ {REVIEW} }}", f"start-threadjob {{ {REVIEW} }}",
                        f"Start-Process python -ArgumentList 'x'; {REVIEW}", f"{REVIEW} &", f"{REVIEW} &; echo next"):
            for background in (True, False, None):
                result = run(call("PowerShell", command, background=background))
                self.assertEqual(result.returncode, 2, f"{command} {background}")
                self.assertIn("not detached", result.stderr)

    def test_a_foreground_review_is_refused_in_either_shell(self):
        for tool in ("Bash", "PowerShell"):
            for background in (False, None):
                result = run(call(tool, REVIEW, background=background))
                self.assertEqual(result.returncode, 2)
                self.assertIn("a review takes minutes", result.stderr)

    def test_start_process_with_split_arguments_is_refused(self):
        # Review round 1: PowerShell's own launch form names the interpreter and the script as separate arguments.
        command = ("Start-Process -WindowStyle Hidden -FilePath python -ArgumentList "
                   "@('scripts/review-dispatch.py', '--writer', 'claude', '--prompt', 'R:/scratch/p.md')")
        for background in (True, False, None):
            result = run(call("PowerShell", command, background=background))
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn("not detached", result.stderr)

    def test_the_py_launcher_is_a_run(self):
        script = REVIEW.split("python ")[1]
        for command in (f"py {script}", f"py -3.14 {script}", f"py.exe -3 -u {script}"):
            result = run(call("PowerShell", command))
            self.assertEqual(result.returncode, 2, command)
            self.assertIn("a review takes minutes", result.stderr)
            self.assertEqual(run(call("PowerShell", command, background=True)).returncode, 0, command)

    def test_a_quoted_interpreter_is_a_run(self):
        # Review round 2: PowerShell's call operator takes a quoted executable, often an absolute path.
        script = REVIEW.split("python ")[1]
        for command in (f'& "python" {script}', f"& 'python.exe' {script}",
                        f'& "C:\\Program Files\\Python314\\python.exe" {script}'):
            result = run(call("PowerShell", command))
            self.assertEqual(result.returncode, 2, command)
            self.assertIn("a review takes minutes", result.stderr)
            self.assertEqual(run(call("PowerShell", command, background=True)).returncode, 0, command)

    def test_every_spelling_of_a_run_is_caught(self):
        script = REVIEW.split("python ")[1]
        for tool, command in (("Bash", f"./{script}"), ("Bash", f"cd /r; {script}"), ("Bash", f"exec {script}"),
                              ("Bash", f"python3 {script}"), ("Bash", f"python3.14 -u {script}"),
                              ("Bash", f"python '{script.replace(' --', chr(39) + ' --', 1)}"),
                              ("PowerShell", f"python.exe {script}"),
                              ("PowerShell", f"python scripts\\review-dispatch.py --prompt p.md"),
                              ("PowerShell", f"& python R:\\GitHub\\Recto\\{script}")):
            self.assertEqual(run(call(tool, command)).returncode, 2, command)

    def test_the_powershell_call_operator_is_not_taken_for_detaching(self):
        result = run(call("PowerShell", f"& python {REVIEW.split('python ')[1]}", background=True))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_calls_that_run_no_review_pass(self):
        for tool in ("Bash", "PowerShell"):
            for command in ("python scripts/review-dispatch.py --check-pin", "python scripts/review-dispatch.py --help",
                            "grep -n final scripts/review-dispatch.py", "sleep 3 && echo done 2>&1 &"):
                self.assertEqual(run(call(tool, command)).returncode, 0, command)
        self.assertEqual(run({"tool_name": "Read", "tool_input": {"command": REVIEW}}).returncode, 0)

    def test_text_that_only_mentions_the_dispatcher_passes(self):
        for command in (
            "python - <<'EOF'\ns = 'Run every review (`scripts/review-dispatch.py ... --prompt ...`) & so on'\nEOF",
            'git commit -m "docs: review-dispatch.py --prompt runs in the background & is watched"',
        ):
            self.assertEqual(run(call("Bash", command)).returncode, 0, command)

    def test_redirections_are_not_taken_for_detaching(self):
        result = run(call("Bash", f"{REVIEW} > /tmp/log 2>&1 && echo ok || echo failed &> /dev/null", background=True))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_an_unreadable_payload_lets_the_call_run(self):
        for payload in ("", "not json", "[]", json.dumps({"tool_name": "Bash", "tool_input": None})):
            self.assertEqual(run(payload).returncode, 0, payload)

    def test_antigravity_background_review_is_allowed_and_foreground_daemon_or_detached_is_denied(self):
        agy_review = REVIEW.replace("--writer claude", "--writer agy")
        ok = run(agy_call("run_command", {"CommandLine": agy_review, "WaitMsBeforeAsync": 500, "IsDaemon": False}))
        self.assertEqual((ok.returncode, json.loads(ok.stdout)), (0, {"decision": "allow"}))
        for bad_args, phrase in (
            ({"CommandLine": agy_review, "WaitMsBeforeAsync": 5000, "IsDaemon": False}, "WaitMsBeforeAsync: 500"),
            ({"CommandLine": agy_review}, "WaitMsBeforeAsync: 500"),
            ({"CommandLine": agy_review, "WaitMsBeforeAsync": True, "IsDaemon": False}, "WaitMsBeforeAsync: 500"),
            ({"CommandLine": agy_review, "WaitMsBeforeAsync": -1, "IsDaemon": False}, "WaitMsBeforeAsync: 500"),
            ({"CommandLine": agy_review, "WaitMsBeforeAsync": 500, "IsDaemon": True}, "IsDaemon: false"),
            ({"CommandLine": f"{agy_review} &", "WaitMsBeforeAsync": 500, "IsDaemon": False}, "not detached"),
            ({"CommandLine": f"Start-Job {{ {agy_review} }}", "WaitMsBeforeAsync": 500, "IsDaemon": False}, "not detached"),
            ({"CommandLine": f"nohup {agy_review}", "WaitMsBeforeAsync": 500, "IsDaemon": False}, "not detached"),
        ):
            with self.subTest(bad_args=bad_args):
                res = run(agy_call("run_command", bad_args))
                self.assertEqual(res.returncode, 0, res.stderr)
                body = json.loads(res.stdout)
                self.assertEqual(body["decision"], "deny", bad_args)
                self.assertIn(phrase, body["reason"])
        non_review = run(agy_call("run_command", {"CommandLine": "python scripts/review-dispatch.py --check-pin",
                                                  "WaitMsBeforeAsync": 5000}))
        self.assertEqual(json.loads(non_review.stdout), {"decision": "allow"})

    def test_antigravity_ask_question_is_denied_only_for_the_session_owning_a_live_campaign_guard(self):
        import tempfile
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subdir = root / "src"
            subdir.mkdir()
            # No guard file: attended Antigravity session may ask questions even if the test runner itself was
            # invoked inside an unattended campaign process carrying CONCLAVE_UNATTENDED_RUN=1.
            with mock.patch.dict(os.environ, {"CONCLAVE_UNATTENDED_RUN": "1", "CLAUDE_PROJECT_DIR": str(HOOK.parents[1])}):
                res = run(agy_call("ask_question", {"questions": []}, conversation_id="conv-camp",
                                   workspace=str(root), cwd=str(subdir)))
            self.assertEqual(json.loads(res.stdout), {"decision": "allow"})
            # Explicit CONCLAVE_UNATTENDED_RUN=1 in the hook's own environment denies ask_question even before acquire.
            forced = run(agy_call("ask_question", {"questions": []}, conversation_id="conv-camp",
                                  workspace=str(root), cwd=str(subdir)),
                         extra_env={"CONCLAVE_UNATTENDED_RUN": "1"})
            self.assertEqual(json.loads(forced.stdout)["decision"], "deny")
            (root / "build").mkdir()
            (root / "build" / "claude-campaign-guard.json").write_text(
                json.dumps({"runner": "agy", "session_id": "conv-camp"}), encoding="utf-8")
            # Another session on the same workspace is not blocked.
            other = run(agy_call("ask_question", {"questions": []}, conversation_id="conv-other",
                                 workspace=str(root), cwd=str(subdir)))
            self.assertEqual(json.loads(other.stdout), {"decision": "allow"})
            # Owning session is denied even when cwd is a subdirectory of workspacePaths[0].
            owned = run(agy_call("ask_question", {"questions": []}, conversation_id="conv-camp",
                                 workspace=str(root), cwd=str(subdir)))
            body = json.loads(owned.stdout)
            self.assertEqual(body["decision"], "deny")
            self.assertIn("unattended campaign never asks", body["reason"])

    def test_antigravity_configured_pre_tool_use_command_runs_from_agents_directory(self):
        import os
        repo = HOOK.parents[1]
        hooks_cfg = json.loads((repo / ".agents" / "hooks.json").read_text(encoding="utf-8"))
        command = hooks_cfg["recto-campaign"]["PreToolUse"][0]["hooks"][0]["command"]
        shell = ["cmd", "/c", command] if os.name == "nt" else ["sh", "-c", command]
        payload = json.dumps(agy_call("run_command", {"CommandLine": REVIEW, "WaitMsBeforeAsync": 5000}))
        proc = subprocess.run(shell, input=payload, capture_output=True, text=True,
                              cwd=str(repo / ".agents"), timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["decision"], "deny")

    def test_orchestrator_carbon_and_hydrogen_turns_allow_writes_and_commands_unmodified(self):
        self.assertFalse((HOOK.parent / "hydrogen_exec.py").exists())
        allowed_calls = (
            ("write_file", {"Path": "D:/Recto/src/x.cpp", "Content": "int x;\n"}),
            ("edit_file", {"Path": "D:/Recto/src/x.cpp", "Target": "int x;", "Replacement": "int y;"}),
            ("enter_plan_mode", {"reason": "hand back to planner"}),
            ("run_command", {"CommandLine": ".\\build.cmd -Quick", "WaitMsBeforeAsync": 5000}),
            ("run_command", {"CommandLine": "git status --short", "WaitMsBeforeAsync": 5000}),
        )
        for model in (
            "Orchestrator (Carbon)",
            "gemini-pro-orchestrator",
            "gemini-4-hydrogen-medium",
            "gemini-4-hydrogen-high",
            "gemini-4-carbon-medium",
        ):
            for tool_name, tool_args in allowed_calls:
                with self.subTest(model=model, tool=tool_name, args=tool_args):
                    res = run(agy_call(tool_name, tool_args, model_name=model))
                    self.assertEqual(res.returncode, 0, res.stderr)
                    self.assertEqual(json.loads(res.stdout), {"decision": "allow"})

    def test_mutate_check_runs_in_temp_copy_without_touching_live_tree(self):
        import tempfile
        mutate_script = HOOK.parent / "mutate-check.py"
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            (repo / "calc.py").write_text("def add(a, b):\n    return a + b\n", encoding="utf-8")
            (repo / "test_calc.py").write_text(
                "import unittest, calc\nclass T(unittest.TestCase):\n"
                "    def test_add(self): self.assertEqual(calc.add(2, 3), 5)\n",
                encoding="utf-8",
            )
            for git_args in (["init", "-q"], ["config", "core.autocrlf", "false"], ["add", "-A"]):
                subprocess.run(["git", "-C", str(repo), *git_args], check=True, capture_output=True)
            before = (repo / "calc.py").read_bytes()
            killed = subprocess.run(
                [sys.executable, str(mutate_script), "--root", str(repo), "--file", "calc.py",
                 "--old", "return a + b", "--new", "return a - b", "--",
                 sys.executable, "-m", "unittest", "test_calc"],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(killed.returncode, 0, killed.stdout + killed.stderr)
            self.assertIn("OUTCOME: KILLED", killed.stdout)
            self.assertEqual((repo / "calc.py").read_bytes(), before)

            # Mutating a native C++ source file with a pre-built binary (without recompiling) is refused up front.
            (repo / "sample.cpp").write_text("int f() { return 1; }\n", encoding="utf-8")
            refused_cpp = subprocess.run(
                [sys.executable, str(mutate_script), "--root", str(repo), "--file", "sample.cpp",
                 "--old", "return 1;", "--new", "return 0;", "--",
                 "build/bin/x64/Debug/recto-tests/recto-tests.exe", "--gtest_filter=PdfError.*"],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(refused_cpp.returncode, 2)
            self.assertIn("requires a build-and-test command", refused_cpp.stderr)

    def test_mutate_check_timeout_kills_descendant_process_tree(self):
        import ctypes
        import importlib.util
        import os
        import tempfile
        import time

        spec = importlib.util.spec_from_file_location("mutate_check", HOOK.parent / "mutate-check.py")
        assert spec is not None and spec.loader is not None
        mutate_mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mutate_mod)

        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            pid_file = tmp_path / "grandchild.pid"
            spawner = tmp_path / "spawner.py"
            spawner.write_text(
                "import os, pathlib, subprocess, sys, time\n"
                f"child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])\n"
                f"dst = pathlib.Path({str(pid_file)!r})\n"
                "tmp = dst.with_suffix('.tmp')\n"
                "tmp.write_text(str(child.pid), encoding='utf-8')\n"
                "os.replace(tmp, dst)\n"
                "time.sleep(30)\n",
                encoding="utf-8",
            )
            code, tail = mutate_mod.run_cmd([sys.executable, str(spawner)], tmp_path, timeout_s=2.0)
            self.assertIsNone(code)
            self.assertIn("timed out", tail)
            self.assertTrue(pid_file.is_file(), "spawner did not record grandchild PID before timeout")
            grandchild_pid = int(pid_file.read_text(encoding="utf-8").strip())
            if os.name == "nt":
                SYNCHRONIZE = 0x00100000
                handle = ctypes.windll.kernel32.OpenProcess(SYNCHRONIZE, False, grandchild_pid)
                if handle:
                    try:
                        wait_rc = ctypes.windll.kernel32.WaitForSingleObject(handle, 2000)
                        self.assertEqual(wait_rc, 0, f"grandchild PID {grandchild_pid} survived timeout")
                    finally:
                        ctypes.windll.kernel32.CloseHandle(handle)
            else:
                deadline = time.monotonic() + 2.0
                alive = True
                while time.monotonic() < deadline:
                    try:
                        os.kill(grandchild_pid, 0)
                        time.sleep(0.05)
                    except ProcessLookupError:
                        alive = False
                        break
                self.assertFalse(alive, f"grandchild PID {grandchild_pid} survived timeout")

if __name__ == "__main__":
    unittest.main()
