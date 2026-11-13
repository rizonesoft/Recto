"""scripts/dispatch_hook.py: a review runs only through the shell tool's background option (ported from Is It Legit)."""
import json
import subprocess
import sys
import unittest
from pathlib import Path

HOOK = Path(__file__).resolve().parents[2] / "scripts" / "dispatch_hook.py"
REVIEW = "python scripts/review-dispatch.py --writer claude --prompt R:/scratch/p.md --record docs/reviews/01-engine/D01-T02-s1.md"


def run(payload) -> subprocess.CompletedProcess:
    data = payload if isinstance(payload, str) else json.dumps(payload)
    return subprocess.run([sys.executable, str(HOOK)], input=data, capture_output=True, text=True, timeout=30)


def call(tool: str, command: str, background=None) -> dict:
    tool_input = {"command": command}
    if background is not None:
        tool_input["run_in_background"] = background
    return {"tool_name": tool, "tool_input": tool_input}


def agy_call(name: str, args: dict, conversation_id: str = "conv-1", workspace: str = "", cwd: str = "",
             model_name: str = "gemini-4-carbon-medium") -> dict:
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
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subdir = root / "src"
            subdir.mkdir()
            # No guard file: attended Antigravity session may ask questions when genuinely needed.
            res = run(agy_call("ask_question", {"questions": []}, conversation_id="conv-camp",
                               workspace=str(root), cwd=str(subdir)))
            self.assertEqual(json.loads(res.stdout), {"decision": "allow"})
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

    def test_hydrogen_swarm_spawn_requires_high_effort_and_inherit_workspace(self):
        for good_model in ("gemini-4-hydrogen-high", "gemini-4-hydrogen-xhigh"):
            res = run(agy_call("swarm", {"action": "add", "name": "test-runner", "model": good_model,
                                         "workspace": "inherit", "brief": "run tests"}))
            self.assertEqual(json.loads(res.stdout), {"decision": "allow"}, good_model)
        for bad_model in ("gemini-4-hydrogen", "gemini-4-hydrogen-low", "gemini-4-hydrogen-medium"):
            res = run(agy_call("swarm", {"action": "add", "name": "test-runner", "model": bad_model,
                                         "workspace": "inherit", "brief": "run tests"}))
            body = json.loads(res.stdout)
            self.assertEqual(body["decision"], "deny", bad_model)
            self.assertIn("hydrogen-effort", body["reason"])
        for bad_ws in ("branch", "share"):
            res = run(agy_call("swarm", {"action": "add", "name": "test-runner", "model": "gemini-4-hydrogen-high",
                                         "workspace": bad_ws, "brief": "run tests"}))
            body = json.loads(res.stdout)
            self.assertEqual(body["decision"], "deny", bad_ws)
            self.assertIn("hydrogen-readonly", body["reason"])

    def test_hydrogen_worker_is_strictly_read_only(self):
        repo = HOOK.parents[1]
        hooks_cfg = json.loads((repo / ".agents" / "hooks.json").read_text(encoding="utf-8"))
        self.assertEqual(hooks_cfg["recto-campaign"]["PreToolUse"][0]["matcher"], "*")

        for denied_tool, args in (
            ("write_file", {"Path": "D:/Recto/x.txt", "Content": "x"}),
            ("edit_file", {"Path": "D:/Recto/x.txt", "Target": "a", "Replacement": "b"}),
            ("ask_question", {"questions": []}),
            ("invoke_subagent", {"TypeName": "self", "Role": "r", "Prompt": "p"}),
            ("define_subagent", {"Name": "x", "Description": "d", "SystemPrompt": "s"}),
            ("manage_task", {"Action": "kill", "TaskId": "t"}),
            ("schedule", {"Prompt": "p", "DurationSeconds": 10}),
            ("call_mcp_tool", {"ServerName": "data-agent-kit-plugin_dak-notebook", "ToolName": "insert_cell", "Arguments": {}}),
            ("swarm", {"action": "add", "name": "x", "brief": "b"}),
        ):
            with self.subTest(tool=denied_tool):
                res = run(agy_call(denied_tool, args, model_name="gemini-4-hydrogen-high"))
                body = json.loads(res.stdout)
                self.assertEqual(body["decision"], "deny")
                self.assertIn("hydrogen-readonly", body["reason"])

        status_ok = run(agy_call("swarm", {"action": "status"}, model_name="gemini-4-hydrogen-high"))
        self.assertEqual(json.loads(status_ok.stdout), {"decision": "allow"})
        lsp_ok = run(agy_call("call_mcp_tool",
                              {"ServerName": "recto-lsp", "ToolName": "lsp_symbols", "Arguments": {"file_or_query": "main"}},
                              model_name="gemini-4-hydrogen-high"))
        self.assertEqual(json.loads(lsp_ok.stdout), {"decision": "allow"})

        for bad_cmd in (
            '[System.IO.File]::WriteAllText("AGENTS.md", "replacement")',
            '[IO.File]::Delete("AGENTS.md")',
            'pwsh -NoProfile -Command "Set-Content AGENTS.md x"',
            'cmd /c del AGENTS.md',
            "git commit -m 'bad'",
            "git checkout -b feat",
            "git reset --hard",
            "git -C . add src/pdf/object.h",
            "git -c user.name=x commit -m 'bypass'",
            "git -c diff.external=evil diff",
            "git -C ../outside status",
            "git --git-dir=.git --work-tree=. reset --hard",
            "git -C. checkout -- src/pdf/object.h",
            "git branch new-feature",
            "git tag v9.9.9",
            "git config user.email x@y.z",
            "git log -n 1 --output=out.txt",
            "Write-Host $(git -C . add src/pdf/object.h)",
            "Set-Content foo.txt 'bar'",
            "Remove-Item foo.txt",
            "echo hi > foo.txt",
            "python scripts/todo-graph.py stamp 'D00 T01 s1' --sha abc",
            "python scripts/todo-graph.py plan --sync",
            "python -c \"from pathlib import Path; Path('a').write_text('b')\"",
            r".\build.cmd -Quick",
            "build/bin/x64/Debug/recto-tests/recto-tests.exe --gtest_output=xml:AGENTS.md",
            "python scripts/mutate-check.py --file a.py --old x --new y -- pwsh -c evil",
        ):
            with self.subTest(bad_cmd=bad_cmd):
                res = run(agy_call("run_command", {"CommandLine": bad_cmd, "WaitMsBeforeAsync": 5000},
                                   model_name="gemini-4-hydrogen-high"))
                body = json.loads(res.stdout)
                self.assertEqual(body["decision"], "deny", bad_cmd)
                self.assertIn("hydrogen-readonly", body["reason"])

        for ok_cmd in (
            "git status -sb",
            "git -C . status -sb",
            "git -c color.ui=false log -n 5 --format=\"%h (%s)\"",
            "git --no-pager branch --show-current",
            "git config --get core.hooksPath",
            "git log -n 5 --oneline",
            "git diff HEAD -- scripts/",
            "python scripts/todo-graph.py validate",
            "python scripts/run-tests.py --quick",
            "python -m unittest tests.scripts.test_dispatch_hook 2>&1",
            "build/bin/x64/Debug/recto-tests/recto-tests.exe --gtest_filter=PdfObject.*",
            "python scripts/mutate-check.py --file scripts/ci_status.py --old a --new b -- python -m unittest tests.scripts.test_ci_status",
        ):
            with self.subTest(ok_cmd=ok_cmd):
                res = run(agy_call("run_command", {"CommandLine": ok_cmd, "WaitMsBeforeAsync": 5000},
                                   model_name="gemini-4-hydrogen-high"))
                body = json.loads(res.stdout)
                self.assertEqual(body["decision"], "allow", ok_cmd)
                self.assertIn("overwrite", body)
                rewritten = body["overwrite"]["CommandLine"]
                self.assertIn("hydrogen_exec.py", rewritten)
                self.assertIn("--payload ", rewritten)

    def test_hydrogen_exec_isolates_checkout_and_rejects_forged_payloads_at_execution_time(self):
        import importlib.util
        import tempfile

        spec = importlib.util.spec_from_file_location("hydrogen_exec", HOOK.parent / "hydrogen_exec.py")
        assert spec is not None and spec.loader is not None
        hexec = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(hexec)

        # Forged payload with a PowerShell expression or python -c is rejected at execution time (exit 2).
        forged_hex = json.dumps(['[System.IO.File]::WriteAllText("AGENTS.md", "x")']).encode("utf-8").hex()
        forged = subprocess.run(
            [sys.executable, str(HOOK.parent / "hydrogen_exec.py"), "--payload", forged_hex],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(forged.returncode, 2)
        self.assertIn("hydrogen-readonly", forged.stderr)

        # Allowed unittest runs execute inside a staged temp directory so writes inside the test never touch `repo`.
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            (repo / "AGENTS.md").write_text("canonical-agents-rules\n", encoding="utf-8")
            (repo / "tests").mkdir()
            (repo / "tests" / "__init__.py").write_text("", encoding="utf-8")
            (repo / "tests" / "test_probe.py").write_text(
                "import pathlib, unittest\n"
                "class Probe(unittest.TestCase):\n"
                "    def test_attempt_write(self):\n"
                "        root = pathlib.Path(__file__).resolve().parents[1]\n"
                "        (root / 'AGENTS.md').write_text('clobbered', encoding='utf-8')\n"
                "        (root / 'NEW_FILE.txt').write_text('created', encoding='utf-8')\n",
                encoding="utf-8",
            )
            for git_args in (
                ["init", "-q"],
                ["config", "user.email", "t@t"],
                ["config", "user.name", "t"],
                ["config", "commit.gpgsign", "false"],
                ["add", "-A"],
                ["commit", "-qm", "init"],
            ):
                subprocess.run(["git", "-C", str(repo), *git_args], check=True, capture_output=True)

            rc = hexec.execute_hydrogen_argv(["python", "-m", "unittest", "tests.test_probe"], root=repo)
            self.assertEqual(rc, 0)
            self.assertEqual((repo / "AGENTS.md").read_text(encoding="utf-8"), "canonical-agents-rules\n")
            self.assertFalse((repo / "NEW_FILE.txt").exists())

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
                "import pathlib, subprocess, sys, time\n"
                f"child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])\n"
                f"pathlib.Path({str(pid_file)!r}).write_text(str(child.pid), encoding='utf-8')\n"
                "time.sleep(30)\n",
                encoding="utf-8",
            )
            code, tail = mutate_mod.run_cmd([sys.executable, str(spawner)], tmp_path, timeout_s=0.6)
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
