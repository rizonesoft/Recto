"""scripts/start-unattended-run.ps1, driven with a fake `op`, a fake `claude` and a fake `agy` first on PATH: the
Claude Code session gets the 1Password token in its environment and nowhere else, cannot ask questions, and is told
the clone it runs in; the Antigravity session runs Gemini 4 Carbon and never gets that token; and both Desktop
shortcuts are written with their runner's badged icon."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "scripts" / "start-unattended-run.ps1"
POWERSHELL = shutil.which("pwsh")
TOKEN = "fake-token-0123456789"

FAKE_CLAUDE = """
$record = @{
    program = [System.IO.Path]::GetFileNameWithoutExtension($PSCommandPath)
    args = @($args | Select-Object -SkipLast 1)
    goal = [string]$args[-1]
    token_matches = ($env:CLAUDE_CODE_OAUTH_TOKEN -ceq $env:RECTO_TEST_EXPECTED_TOKEN)
    token_set = [bool]$env:CLAUDE_CODE_OAUTH_TOKEN
}
$record | ConvertTo-Json | Set-Content -Encoding utf8 $env:RECTO_TEST_RECORD
exit 0
"""


def no_window() -> int:
    """The creation flags for every process this file starts: no console window on Windows, where a parent without a
    console of its own would otherwise open a visible one for each child; nothing elsewhere."""
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


@unittest.skipUnless(POWERSHELL, "needs PowerShell 7")
class Launcher(unittest.TestCase):
    def run_launcher(self, *switches: str, op_exit: int = 0, op_hangs: bool = False,
                     before: str = "", after: str = "",
                     inherited_token: str = "") -> tuple[subprocess.CompletedProcess, dict | None]:
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            for runner in ("claude", "agy"):  # both runners take the same fake, which records its own name
                (tmp / f"{runner}.ps1").write_text(FAKE_CLAUDE, encoding="utf-8")
            # op is started as a process, so the fake is a batch file; a hanging one stands for an unanswered prompt.
            wait = "ping -n 60 127.0.0.1 >nul\n" if op_hangs else ""
            say = f"echo {TOKEN}\n" if op_exit == 0 else ""
            (tmp / "op.cmd").write_text(f"@echo off\n{wait}{say}exit /b {op_exit}\n", encoding="utf-8", newline="\r\n")
            record = tmp / "record.json"
            env = dict(os.environ, PATH=str(tmp) + os.pathsep + os.environ["PATH"], RECTO_TEST_RECORD=str(record),
                       RECTO_TEST_EXPECTED_TOKEN=TOKEN)
            env.pop("CLAUDE_CODE_OAUTH_TOKEN", None)
            if inherited_token:
                env["CLAUDE_CODE_OAUTH_TOKEN"] = inherited_token
            command = f"{before}; & '{LAUNCHER}' {' '.join(switches)}; {after}"
            run = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command],
                                 capture_output=True, text=True, timeout=180, env=env, creationflags=no_window())
            seen = json.loads(record.read_text(encoding="utf-8-sig")) if record.exists() else None
            return run, seen

    def test_the_session_gets_the_token_in_its_environment_and_never_on_screen_or_in_arguments(self):
        run, seen = self.run_launcher()
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertTrue(seen["token_matches"])
        self.assertNotIn(TOKEN, run.stdout + run.stderr)
        self.assertNotIn(TOKEN, json.dumps(seen["args"]) + seen["goal"])
        self.assertIn("Sign-in: the one-year token from 1Password.", run.stdout)

    def test_the_session_runs_unattended_on_the_newest_opus_and_cannot_ask(self):
        run, seen = self.run_launcher()
        self.assertEqual(seen["args"], ["--dangerously-skip-permissions", "--disallowedTools", "AskUserQuestion",
                                        "--model", "opus"])
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

    def test_the_shell_left_open_after_the_session_does_not_keep_the_token(self):
        run, seen = self.run_launcher(after="Write-Output \"TOKEN_LEFT=$([bool]$env:CLAUDE_CODE_OAUTH_TOKEN)\"")
        self.assertTrue(seen["token_matches"])
        self.assertIn("TOKEN_LEFT=False", run.stdout)

    def test_an_unanswered_1password_prompt_is_stopped_and_the_run_starts(self):
        run, seen = self.run_launcher("-TokenTimeoutSeconds", "2", op_hangs=True)
        self.assertFalse(seen["token_set"])
        self.assertIn("did not answer within 2 seconds", run.stdout + run.stderr)
        self.assertIn("starting on the stored Claude Code login", run.stdout + run.stderr)

    def test_print_only_starts_nothing_and_prints_no_token(self):
        run, seen = self.run_launcher("-PrintOnly")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIsNone(seen)
        self.assertNotIn(TOKEN, run.stdout + run.stderr)
        self.assertIn("--disallowedTools AskUserQuestion", run.stdout)

    def test_the_carbon_runner_starts_antigravity_on_gemini_4_carbon_without_claudes_token(self):
        run, seen = self.run_launcher("-Runner", "agy", inherited_token="inherited-token-abc")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(seen["program"], "agy")
        self.assertEqual(seen["args"], ["--model", "gemini-4-carbon-medium", "--dangerously-skip-permissions", "-i"])
        self.assertTrue(seen["goal"].startswith("/goal "))
        self.assertFalse(seen["token_set"])
        self.assertNotIn("1Password", run.stdout + run.stderr)
        self.assertIn("own account on this PC", run.stdout)

    def test_the_claude_runner_is_the_default(self):
        run, seen = self.run_launcher()
        self.assertEqual(seen["program"], "claude")

    def test_install_shortcut_writes_one_badged_shortcut_for_each_runner_and_removes_the_old_one(self):
        folder = "(Split-Path $env:RECTO_TEST_RECORD)"
        before = (f"$desk = Join-Path {folder} 'desk'; $icons = Join-Path {folder} 'icons'; "
                  "New-Item -ItemType Directory $desk | Out-Null; "
                  "Set-Content (Join-Path $desk 'Recto - Process Plan (Unattended).lnk') 'old'")
        after = ("$shell = New-Object -ComObject WScript.Shell; "
                 "$found = @(Get-ChildItem $desk | Sort-Object Name | ForEach-Object { "
                 "$s = $shell.CreateShortcut($_.FullName); @{ name = $_.Name; target = $s.TargetPath; "
                 "arguments = $s.Arguments; folder = $s.WorkingDirectory; icon = $s.IconLocation; "
                 "icon_exists = (Test-Path -LiteralPath ($s.IconLocation -replace ',0$', '')); "
                 "description = $s.Description; style = $s.WindowStyle } }); "
                 "'FOUND=' + (ConvertTo-Json -Compress -InputObject $found)")
        run, seen = self.run_launcher("-InstallShortcut", "-ShortcutFolder", "$desk", "-IconFolder", "$icons",
                                      before=before, after=after)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIsNone(seen)  # installing the shortcuts starts no session
        found = json.loads(next(ln for ln in run.stdout.splitlines() if ln.startswith("FOUND="))[len("FOUND="):])
        self.assertEqual([s["name"] for s in found], ["Recto Carbon campaign.lnk", "Recto Claude campaign.lnk"])
        plain = str(ROOT / "resources" / "ico" / "recto.ico")
        for shortcut, runner, badge, where in zip(found, ("agy", "claude"), ("carbon", "claude"),
                                                  ("Google Antigravity on Gemini 4 Carbon", "Claude Code")):
            self.assertEqual(Path(shortcut["target"]).stem.lower(), "pwsh")
            self.assertEqual(shortcut["arguments"],
                             f'-NoLogo -NoExit -NoProfile -ExecutionPolicy Bypass -File "{LAUNCHER}" -Runner {runner}')
            self.assertEqual(Path(shortcut["folder"]), ROOT)
            self.assertEqual(shortcut["description"], f"Start the unattended Recto campaign in {where}")
            self.assertEqual(shortcut["style"], 1)
            self.assertTrue(shortcut["icon_exists"], shortcut["icon"])
            # The badged icon where this machine can render it (Pillow), else Recto's plain one, said in a warning.
            if "could not be rendered" in run.stdout + run.stderr:
                self.assertEqual(shortcut["icon"], f"{plain},0")
            else:
                self.assertTrue(shortcut["icon"].endswith(f"recto-campaign-{badge}.ico,0"), shortcut["icon"])

    def test_every_process_this_file_starts_opens_no_window(self):
        self.assertEqual(no_window(), 0x08000000 if os.name == "nt" else 0)
        with mock.patch.object(subprocess, "run", wraps=subprocess.run) as run, \
                mock.patch.object(subprocess, "Popen", wraps=subprocess.Popen) as popen:
            self.run_launcher("-PrintOnly")
        # subprocess.run starts its process through Popen, so Popen sees every start, however it was asked for.
        self.assertTrue(run.call_args_list)
        # The message names the program only: the call carries the whole environment, which is never printed.
        for call in run.call_args_list + popen.call_args_list:
            self.assertEqual(call.kwargs.get("creationflags"), no_window(), Path(call.args[0][0]).name)


try:
    from PIL import Image
except ImportError:
    Image = None


@unittest.skipUnless(Image, "needs Pillow")
class CampaignIcon(unittest.TestCase):
    def test_the_icon_keeps_every_size_and_carries_the_badge_bottom_right(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "badged.ico"
            run = subprocess.run([sys.executable, str(ROOT / "scripts" / "campaign-icon.py"), str(target), "#D97757"],
                                 capture_output=True, text=True, timeout=120, creationflags=no_window())
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(os.listdir(tmp), ["badged.ico"])  # no partial file is left beside it
            with Image.open(ROOT / "resources" / "ico" / "recto.ico") as plain, Image.open(target) as badged:
                self.assertEqual(sorted(badged.ico.sizes()), sorted(plain.ico.sizes()))
                for size in ((16, 16), (256, 256)):
                    at = round(size[0] * 0.75)  # the badge's centre
                    # At 16 pixels the white outline blends a few levels into the centre; the colour is still the fill.
                    for got, want in zip(badged.ico.getimage(size).convert("RGBA").getpixel((at, at)),
                                         (217, 119, 87, 255)):
                        self.assertAlmostEqual(got, want, delta=0 if size[0] == 256 else 6)
                    self.assertEqual(badged.ico.getimage(size).convert("RGBA").getpixel((1, 1)),
                                     plain.ico.getimage(size).convert("RGBA").getpixel((1, 1)))

    def test_a_bad_colour_is_refused_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = subprocess.run([sys.executable, str(ROOT / "scripts" / "campaign-icon.py"),
                                  str(Path(tmp) / "badged.ico"), "orange"],
                                 capture_output=True, text=True, timeout=120, creationflags=no_window())
            self.assertEqual(run.returncode, 2)
            self.assertEqual(os.listdir(tmp), [])


if __name__ == "__main__":
    unittest.main()
