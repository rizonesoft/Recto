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


if __name__ == "__main__":
    unittest.main()
