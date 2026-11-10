"""scripts/start-unattended-run.ps1, driven with a fake `op` and a fake `claude` first on PATH: the session gets the
1Password token in its environment and nowhere else, cannot ask questions, and is told the clone it runs in."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "scripts" / "start-unattended-run.ps1"
POWERSHELL = shutil.which("pwsh")
TOKEN = "fake-token-0123456789"

FAKE_CLAUDE = """
$record = @{
    args = @($args | Select-Object -SkipLast 1)
    goal = [string]$args[-1]
    token_matches = ($env:CLAUDE_CODE_OAUTH_TOKEN -ceq $env:RECTO_TEST_EXPECTED_TOKEN)
    token_set = [bool]$env:CLAUDE_CODE_OAUTH_TOKEN
}
$record | ConvertTo-Json | Set-Content -Encoding utf8 $env:RECTO_TEST_RECORD
exit 0
"""


@unittest.skipUnless(POWERSHELL, "needs PowerShell 7")
class Launcher(unittest.TestCase):
    def run_launcher(self, *switches: str, op_exit: int = 0) -> tuple[subprocess.CompletedProcess, dict | None]:
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "claude.ps1").write_text(FAKE_CLAUDE, encoding="utf-8")
            (tmp / "op.ps1").write_text(f"if ({op_exit} -eq 0) {{ Write-Output '{TOKEN}' }}\nexit {op_exit}\n",
                                        encoding="utf-8")
            record = tmp / "record.json"
            env = dict(os.environ, PATH=str(tmp) + os.pathsep + os.environ["PATH"], RECTO_TEST_RECORD=str(record),
                       RECTO_TEST_EXPECTED_TOKEN=TOKEN)
            env.pop("CLAUDE_CODE_OAUTH_TOKEN", None)
            run = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(LAUNCHER),
                                  *switches], capture_output=True, text=True, timeout=180, env=env)
            seen = json.loads(record.read_text(encoding="utf-8-sig")) if record.exists() else None
            return run, seen

    def test_the_session_gets_the_token_in_its_environment_and_never_on_screen_or_in_arguments(self):
        run, seen = self.run_launcher()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertTrue(seen["token_matches"])
        self.assertNotIn(TOKEN, run.stdout + run.stderr)
        self.assertNotIn(TOKEN, json.dumps(seen["args"]) + seen["goal"])
        self.assertIn("Sign-in: the one-year token from 1Password.", run.stdout)

    def test_the_session_runs_unattended_on_the_writer_model_and_cannot_ask(self):
        run, seen = self.run_launcher()
        self.assertEqual(seen["args"], ["--dangerously-skip-permissions", "--disallowedTools", "AskUserQuestion",
                                        "--model", "claude-fable-5-2"])
        self.assertTrue(seen["goal"].startswith("/goal "))

    def test_the_goal_names_the_clone_the_launcher_is_in(self):
        run, seen = self.run_launcher()
        self.assertIn(f"todo/implementation-plan.md in {ROOT})", seen["goal"])
        self.assertNotIn("{ROOT}", seen["goal"])
        self.assertLessEqual(len(seen["goal"]), 4000)

    def test_a_failed_1password_read_is_said_and_the_run_starts_on_the_stored_login(self):
        run, seen = self.run_launcher(op_exit=1)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertFalse(seen["token_set"])
        self.assertIn("1Password did not give the token", run.stdout + run.stderr)

    def test_print_only_starts_nothing_and_prints_no_token(self):
        run, seen = self.run_launcher("-PrintOnly")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIsNone(seen)
        self.assertNotIn(TOKEN, run.stdout + run.stderr)
        self.assertIn("--disallowedTools AskUserQuestion", run.stdout)


if __name__ == "__main__":
    unittest.main()
