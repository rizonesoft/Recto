"""Tests of `scripts/unattended-preflight.py`: one PASS or FAIL line per precondition, exit 1 on any FAIL."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import unittest.mock

SCRIPTS = Path(__file__).resolve().parents[1]
TEMPLATES = SCRIPTS.parent
IN_KIT = TEMPLATES.name == "templates" and TEMPLATES.parent.name == "unattended"
# The todo kit's guard and hook files: the todo kit's templates in the SDK, the project's own once installed.
TODO = TEMPLATES.parent.parent / "todo" / "templates" if IN_KIT else TEMPLATES
_spec = importlib.util.spec_from_file_location("unattended_preflight", SCRIPTS / "unattended-preflight.py")
assert _spec is not None and _spec.loader is not None
pre = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pre)

PROFILE = {"schema_version": 1, "name": "fixture-run", "agent": "claude", "model": "claude-opus-5-5",
           "effort": "high", "prompt_file": "docs/run-prompt.md", "branch": "main", "skip_permissions": True,
           "tools": [], "min_disk_gb": 1, "min_ram_gb": 1, "tick_seconds": 300, "idle_minutes": 45,
           "stall_hours": 6, "preflight": []}
posix_only = unittest.skipUnless(os.name == "posix", "process groups and tmux: run in Linux or WSL")


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


def wait_gone(pid, seconds=10):
    deadline = time.monotonic() + seconds
    while not gone(pid) and time.monotonic() < deadline:
        time.sleep(0.05)
    return gone(pid)


class Fixture(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "repo"
        self.root.mkdir()
        (self.root / ".conclave").mkdir()
        (self.root / "docs").mkdir()
        (self.root / "docs" / "run-prompt.md").write_text("Run the plan.\n", encoding="utf-8")
        (self.root / ".gitignore").write_text("build/\n", encoding="utf-8")
        self.write_profile()
        for args in (["init", "-q", "-b", "main"], ["config", "user.email", "t@t"], ["config", "user.name", "t"],
                     ["config", "commit.gpgsign", "false"], ["add", "-A"], ["commit", "-qm", "fixture"]):
            git(self.root, *args)

    def write_profile(self, **changes):
        (self.root / ".conclave" / "unattended.json").write_text(json.dumps({**PROFILE, **changes}),
                                                                 encoding="utf-8")

    def seed(self, agent_file):
        (self.root / "scripts").mkdir(exist_ok=True)
        for script in ("campaign_guard.py", "codex-campaign-hook.py", "codex-hook-trust.py"):
            src = TODO / "scripts" / script
            if src.is_file():
                shutil.copyfile(src, self.root / "scripts" / script)
        target = self.root / agent_file
        target.parent.mkdir(parents=True, exist_ok=True)
        src_hook = TODO / agent_file
        if src_hook.is_file():
            shutil.copyfile(src_hook, target)
        elif agent_file == ".codex/hooks.json":
            target.write_text(json.dumps({"hooks": {"Stop": [{"hooks": [{"type": "command", "command": "python3 scripts/codex-campaign-hook.py"}]}]}}), encoding="utf-8")

    def report(self):
        return pre.Report(io.StringIO())

    def lines(self, report):
        return report.out.getvalue().splitlines()

    def fails(self, report):
        return [line for line in self.lines(report) if line.startswith("FAIL ")]

    def names(self, lines):
        return [line.split(":")[0] for line in lines]

    def run_preflight(self, **kwargs):
        out = io.StringIO()
        code = pre.preflight(self.root, out, system="posix", **kwargs)
        return code, out.getvalue().splitlines()


class GitChecksTest(Fixture):
    def check(self):
        report = self.report()
        pre.check_git(self.root, PROFILE, report)
        return report

    def test_a_ready_repository_passes_every_git_check(self):
        report = self.check()
        self.assertFalse(report.failed, self.lines(report))
        self.assertEqual(self.names(self.lines(report)), ["PASS git-branch", "PASS git-clean", "PASS git-upstream"])

    def test_another_branch_is_one_fail(self):
        git(self.root, "checkout", "-q", "-b", "feature")
        self.assertEqual(self.names(self.fails(self.check())), ["FAIL git-branch"])

    def test_a_dirty_tree_is_one_fail(self):
        (self.root / "new.txt").write_text("x", encoding="utf-8")
        fails = self.fails(self.check())
        self.assertEqual(self.names(fails), ["FAIL git-clean"])
        self.assertIn("new.txt", fails[0])

    def test_a_resume_keeps_the_crashed_runs_edits(self):
        (self.root / "new.txt").write_text("x", encoding="utf-8")
        report = self.report()
        pre.check_git(self.root, PROFILE, report, resuming=True)
        self.assertEqual(self.fails(report), [])
        self.assertIn("kept for the resumed run", report.out.getvalue())

    def test_behind_the_upstream_is_one_fail(self):
        remote = Path(self._tmp.name) / "remote.git"
        git(self._tmp.name, "init", "-q", "--bare", "-b", "main", str(remote))
        git(self.root, "remote", "add", "origin", str(remote))
        git(self.root, "push", "-q", "-u", "origin", "main")
        other = Path(self._tmp.name) / "other"
        git(self._tmp.name, "clone", "-q", str(remote), str(other))
        for args in (["config", "user.email", "t@t"], ["config", "user.name", "t"],
                     ["config", "commit.gpgsign", "false"], ["commit", "-q", "--allow-empty", "-m", "ahead"],
                     ["push", "-q", "origin", "main"]):
            git(other, *args)
        self.assertEqual(self.fails(self.check()), [])  # not fetched yet: compared with the last fetch
        git(self.root, "fetch", "-q")
        fails = self.fails(self.check())
        self.assertEqual(self.names(fails), ["FAIL git-upstream"])
        self.assertIn("1 commit(s) behind", fails[0])

    def test_an_upstream_that_no_longer_resolves_fails(self):
        # Round 1 of D02 T02 §3: a tracked branch deleted on the remote and pruned passed as "no upstream set".
        remote = Path(self._tmp.name) / "remote.git"
        git(self._tmp.name, "init", "-q", "--bare", "-b", "main", str(remote))
        git(self.root, "remote", "add", "origin", str(remote))
        git(self.root, "push", "-q", "-u", "origin", "main")
        git(remote, "update-ref", "-d", "refs/heads/main")
        git(self.root, "fetch", "-q", "--prune")
        fails = self.fails(self.check())
        self.assertEqual(self.names(fails), ["FAIL git-upstream"])
        self.assertIn("cannot be resolved", fails[0])
    def test_no_git_on_path_is_one_fail_saying_so(self):
        with unittest.mock.patch.object(pre.shutil, "which", return_value=None):
            fails = self.fails(self.check())
        self.assertEqual(fails, ["FAIL git: git is not on PATH, so the branch, the tree and the upstream cannot be "
                                 "checked"])

    def test_not_a_repository_fails_with_gits_reason(self):
        plain = Path(self._tmp.name) / "plain"
        plain.mkdir()
        report = self.report()
        pre.check_git(plain, PROFILE, report)
        fails = self.fails(report)
        # Each git precondition reports on its own (completion review of D02 T02 §3): all three fail, each saying why.
        self.assertEqual(self.names(fails), ["FAIL git-branch", "FAIL git-clean", "FAIL git-upstream"])
        self.assertIn("not a git repository", fails[0])
        self.assertIn("git status failed", fails[1])
        self.assertIn("the branch is not known", fails[2])


class RunCheckTest(Fixture):
    def hold(self, session):
        (self.root / "build").mkdir(exist_ok=True)
        (self.root / "build" / "claude-campaign-guard.json").write_text(json.dumps({"session_id": session}),
                                                                        encoding="utf-8")

    def check(self, resume=None):
        report = self.report()
        pre.check_run(self.root, report, resume)
        return report

    def test_no_guard_passes(self):
        self.assertEqual(self.lines(self.check()), ["PASS campaign-guard: no run guard held"])

    def test_a_guard_fails_naming_its_session(self):
        self.hold("session-a")
        fails = self.fails(self.check())
        self.assertEqual(len(fails), 1)
        self.assertIn("held by session session-a", fails[0])

    def test_a_guard_whose_tag_is_the_resumed_runs_passes(self):
        # D02 T02 §6: Codex and Antigravity choose their own session id, so the run owns its guard by tag.
        (self.root / "build").mkdir(exist_ok=True)
        (self.root / "build" / "claude-campaign-guard.json").write_text(
            json.dumps({"session_id": "codex-thread-1", "launch_tag": "tag-a"}), encoding="utf-8")
        self.assertEqual(self.fails(self.check("tag-a")), [])
        self.assertIn("held by session codex-thread-1", self.fails(self.check("tag-b"))[0])

    def test_the_resumed_runs_own_guard_passes_and_another_still_fails(self):
        self.hold("session-a")
        self.assertEqual(self.fails(self.check("session-a")), [])
        self.assertIn("the run being resumed", self.lines(self.check("session-a"))[0])
        self.assertIn("held by session session-a", self.fails(self.check("session-b"))[0])

    def test_an_unreadable_guard_fails(self):
        (self.root / "build").mkdir()
        (self.root / "build" / "claude-campaign-guard.json").write_text("{not json", encoding="utf-8")
        self.assertIn("cannot be read", self.fails(self.check("anything"))[0])


class VersionCheckTest(Fixture):
    def check(self, agent, stdout="", code=0, error=None):
        report = self.report()
        done = subprocess.CompletedProcess([], code, stdout=stdout, stderr="")
        patch = unittest.mock.patch.object(pre.subprocess, "run", side_effect=error) if error else \
            unittest.mock.patch.object(pre.subprocess, "run", return_value=done)
        with patch:
            pre.check_agent_version({**PROFILE, "agent": agent}, report)
        return self.lines(report)

    def test_the_recorded_version_and_a_newer_one_pass(self):
        self.assertEqual(self.check("codex", "codex-cli 0.166.0\n"), ["PASS version codex: 0.166.0"])
        self.assertEqual(self.check("codex", "codex-cli 0.170.2\n"), ["PASS version codex: 0.170.2"])
        self.assertEqual(self.check("agy", "1.5.5\n"), ["PASS version agy: 1.5.5"])
        self.assertEqual(self.check("claude", "2.1.322 (Claude Code)\n"), ["PASS version claude: 2.1.322"])

    def test_an_older_version_fails_naming_both(self):
        line = self.check("agy", "1.4.9\n")[0]
        self.assertTrue(line.startswith("FAIL version agy: 1.4.9, older than 1.5.5"), line)

    def test_no_version_fails(self):
        self.assertIn("gave no version (garbled", self.check("codex", "garbled\n")[0])
        self.assertIn("exit 2", self.check("codex", "", code=2)[0])
        self.assertIn("could not run", self.check("codex", error=FileNotFoundError(2, "No such file"))[0])
        self.assertTrue(all(line.startswith("FAIL") for line in self.check("codex", "", code=2)))


class ClaimCheckTest(Fixture):
    LIB = SCRIPTS / "lib" / "unattended.py"

    def lib(self):
        spec = importlib.util.spec_from_file_location("unattended_lib_for_claims", self.LIB)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def check(self, live_sessions, resume=None, unknown=False):
        lib = self.lib()
        report = self.report()
        pre.check_claim(self.root, report, lib, resume,
                        lambda name: None if unknown else name in live_sessions)
        return self.lines(report)

    def test_each_claim_state(self):
        self.assertEqual(self.check(set()), ["PASS launch-claim: no launch claim"])
        self.lib().write_claim(self.root, "session-a", 1, "claude", "demo-run")
        other = self.check({"demo-run"})
        self.assertEqual(len(other), 1)
        self.assertTrue(other[0].startswith("FAIL launch-claim: session session-a is live"), other)
        own = self.check({"demo-run"}, resume="session-a")
        self.assertTrue(own[0].startswith("PASS launch-claim:") and "the run being resumed" in own[0], own)
        stale = self.check(set())
        self.assertTrue(stale[0].startswith("PASS launch-claim: tmux session demo-run is gone: stale"), stale)
        real = Path.read_bytes
        with unittest.mock.patch.object(Path, "read_bytes", lambda p: (_ for _ in ()).throw(OSError(5, "I/O error"))
                                        if p.name == "unattended-launch.json" else real(p)):
            unreadable = self.check({"demo-run"})
        self.assertTrue(unreadable[0].startswith("FAIL launch-claim:") and "cannot be read" in unreadable[0], unreadable)
        unknown = self.check({"demo-run"}, unknown=True)
        self.assertTrue(unknown[0].startswith("FAIL launch-claim: whether tmux session demo-run"), unknown)

    def test_the_preflight_runs_the_claim_check(self):
        self.lib().write_claim(self.root, "session-a", 1, "claude", "demo-run")
        with unittest.mock.patch.object(pre, "check_claim", wraps=pre.check_claim) as called:
            self.run_preflight(resume_session="session-a")
        self.assertEqual(called.call_args.args[3], "session-a")

class ToolChecksTest(Fixture):
    def check(self, profile, missing):
        report = self.report()
        with unittest.mock.patch.object(pre.shutil, "which", lambda n: None if n == missing else f"/usr/bin/{n}"):
            pre.check_tools(profile, report)
        return report

    def test_each_required_tool_and_each_agents_cli_is_checked(self):
        for agent, cli in pre.AGENT_CLI.items():
            for missing in ("git", "python3", "tmux", cli):
                with self.subTest(agent=agent, missing=missing):
                    report = self.check({**PROFILE, "agent": agent}, missing)
                    self.assertEqual(self.fails(report), [f"FAIL tool {missing}: not on PATH"])

    def test_a_profile_tool_that_is_missing_fails_naming_it(self):
        report = self.check({**PROFILE, "agent": "codex", "tools": ["rg", "no-such-tool-xyz"]}, "no-such-tool-xyz")
        self.assertEqual(self.fails(report), ["FAIL tool no-such-tool-xyz: not on PATH"])
        self.assertEqual(self.names(self.lines(report)),
                         ["PASS tool git", "PASS tool python3", "PASS tool tmux", "PASS tool codex", "PASS tool rg",
                          "FAIL tool no-such-tool-xyz"])


class HookCheckTest(Fixture):
    def check(self, agent):
        report = self.report()
        pre.check_hook(self.root, {**PROFILE, "agent": agent}, report)
        return report

    def without_trust(self):
        return unittest.mock.patch.object(pre, "check_codex_trust", lambda root, report: None)

    def test_a_seeded_install_passes_for_each_agent(self):
        for agent, hook_file in pre.AGENT_HOOK_FILE.items():
            if not (TODO / hook_file).is_file():
                continue
            with self.subTest(agent=agent):
                self.seed(hook_file)
                with self.without_trust():
                    report = self.check(agent)
                self.assertEqual(self.fails(report), [], self.lines(report))
                self.assertIn(f"PASS stop-hook: {hook_file} runs the campaign Stop hook", self.lines(report))
                self.assertTrue(any(line.startswith("PASS guard-self-test: exit 0") for line in self.lines(report)))

    def test_another_agents_broken_hook_file_does_not_block_the_selected_one(self):
        self.seed(".claude/settings.json")
        (self.root / ".agents").mkdir()
        (self.root / ".agents" / "hooks.json").write_text("{not json", encoding="utf-8")
        report = self.check("claude")
        self.assertEqual(self.fails(report), [], self.lines(report))

    def test_a_hook_file_without_the_hook_fails(self):
        self.seed(".claude/settings.json")
        (self.root / ".claude" / "settings.json").write_text(json.dumps({"hooks": {}}), encoding="utf-8")
        fails = self.fails(self.check("claude"))
        self.assertTrue(fails and fails[0].startswith("FAIL stop-hook: .claude/settings.json"), fails)

    def test_a_disabled_or_malformed_hook_fails(self):
        self.seed(".agents/hooks.json")
        path = self.root / ".agents" / "hooks.json"
        doc = json.loads(path.read_text(encoding="utf-8"))
        for hook in doc.values():
            hook["enabled"] = False  # Antigravity disables a named hook, not one handler
        cases = (("disabled", json.dumps(doc)), ("malformed", "{not json"))
        for label, text in cases:
            with self.subTest(case=label):
                path.write_text(text, encoding="utf-8")
                fails = self.fails(self.check("agy"))
                self.assertTrue(fails and fails[0].startswith("FAIL stop-hook: .agents/hooks.json"), fails)

    def test_a_self_test_that_fails_is_a_fail(self):
        self.seed(".claude/settings.json")
        broken = subprocess.CompletedProcess([], 1, stdout="self-test: FAIL .claude/settings.json: exited 2\n",
                                             stderr="")
        real = subprocess.run  # window-exempt: a mock wrapper; the script under test passes its own flags

        def run(args, *a, **k):
            return broken if "--self-test" in args else real(args, *a, **k)
        with unittest.mock.patch.object(pre.subprocess, "run", run):
            fails = self.fails(self.check("claude"))
        self.assertEqual(fails, ["FAIL guard-self-test: exit 1: self-test: FAIL .claude/settings.json: exited 2"])

    def test_no_todo_kit_fails(self):
        self.assertEqual(self.fails(self.check("claude")),
                         ["FAIL stop-hook: scripts/campaign_guard.py is missing: install the todo kit"])

    def test_codex_runs_the_trust_script_and_reports_it(self):
        self.seed(".codex/hooks.json")
        fake = self.root / "scripts" / "codex-hook-trust.py"
        for exit_code, said, ok in ((0, "codex-hook-trust: approved: hooks.json:stop:0:0", True),
                                    (2, "codex-hook-trust: codex app-server did not start", False)):
            with self.subTest(exit_code=exit_code):
                stream = "stdout" if exit_code == 0 else "stderr"
                fake.write_text(f"import sys\nprint({said!r}, file=sys.{stream})\nsys.exit({exit_code})\n",
                                encoding="utf-8")
                report = self.check("codex")
                line = [x for x in self.lines(report) if "codex-hook-trust" in x.split(":")[0]]
                self.assertEqual(line, [f"{'PASS' if ok else 'FAIL'} codex-hook-trust: exit {exit_code}: {said}"])


class HeadroomCheckTest(Fixture):
    def check(self, disk, memory, **thresholds):
        report = self.report()
        with unittest.mock.patch.object(pre, "disk_free_gb", return_value=disk), \
                unittest.mock.patch.object(pre, "available_gb", return_value=memory):
            pre.check_headroom(self.root, {**PROFILE, **thresholds}, report)
        return self.lines(report)

    def test_below_at_and_above_each_threshold(self):
        for value, verdict in ((9.9, "FAIL"), (10.0, "PASS"), (10.1, "PASS")):
            with self.subTest(value=value):
                lines = self.check(value, value, min_disk_gb=10, min_ram_gb=10)
                self.assertEqual(lines, [f"{verdict} disk: {value:.1f} GB free, 10 GB needed",
                                         f"{verdict} memory: {value:.1f} GB available, 10 GB needed"])

    def test_the_profiles_thresholds_decide(self):
        self.assertTrue(all(x.startswith("PASS") for x in self.check(5.0, 5.0, min_disk_gb=4, min_ram_gb=4)))
        self.assertTrue(all(x.startswith("FAIL") for x in self.check(5.0, 5.0, min_disk_gb=6, min_ram_gb=6)))

    def test_unreadable_figures_fail_with_the_reason(self):
        report = self.report()
        with unittest.mock.patch.object(pre, "disk_free_gb", side_effect=OSError("no such volume")), \
                unittest.mock.patch.object(pre, "MEMINFO", Path(self._tmp.name) / "absent"):
            pre.check_headroom(self.root, PROFILE, report)
        self.assertEqual(self.names(self.fails(report)), ["FAIL disk", "FAIL memory"])
        self.assertIn("no such volume", self.fails(report)[0])

    def test_meminfo_is_read_in_gb(self):
        meminfo = Path(self._tmp.name) / "meminfo"
        meminfo.write_text("MemTotal:       16000000 kB\nMemAvailable:    2097152 kB\n", encoding="ascii")
        self.assertEqual(pre.available_gb(meminfo), 2.0)


class SystemAndProfileTest(Fixture):
    def test_windows_is_refused_with_one_line(self):
        out = io.StringIO()
        self.assertEqual(pre.preflight(self.root, out, system="nt"), 1)
        self.assertEqual(out.getvalue().splitlines(),
                         ["FAIL system: the unattended run needs tmux, which runs on Linux and in WSL: run it in WSL"])

    def test_a_profile_the_loader_refuses_is_one_fail(self):
        self.write_profile(agent="gemini")
        code, lines = self.run_preflight()
        self.assertEqual(code, 1)
        self.assertEqual(len(lines), 1)
        self.assertIn("FAIL profile: .conclave/unattended.json: agent", lines[0])

    def test_a_check_that_raises_is_a_fail_and_the_others_still_run(self):
        with unittest.mock.patch.object(pre, "check_git", side_effect=RuntimeError("git query broke")), \
                unittest.mock.patch.object(pre, "check_hook", side_effect=OSError("hook file unreadable")):
            code, lines = self.run_preflight()
        self.assertEqual(code, 1)
        self.assertIn("FAIL git: could not run (RuntimeError: git query broke)", lines)
        self.assertIn("FAIL stop-hook: could not run (OSError: hook file unreadable)", lines)
        self.assertTrue(any(line.startswith("PASS campaign-guard") for line in lines), lines)
        self.assertTrue(any(line.startswith(("PASS disk", "FAIL disk")) for line in lines), lines)


class EntryPointFailuresTest(Fixture):
    """Each operational failure the section lists, through `preflight()`: exit 1, its FAIL line, no traceback, and
    the checks that do not depend on it still reported (completion review of D02 T02 §3)."""

    def run_it(self, *patches):
        self.seed(".claude/settings.json")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "seed")
        with contextlib.ExitStack() as stack:
            for patch in patches:
                stack.enter_context(patch)
            code, lines = self.run_preflight()
        self.assertEqual(code, 1, lines)
        self.assertTrue(all(line.startswith(("PASS ", "FAIL ")) for line in lines), lines)
        names = self.names(lines)
        for independent in ("campaign-guard", "tool python3", "disk"):
            self.assertTrue(any(n.endswith(" " + independent) for n in names), (independent, lines))
        return lines

    def test_git_missing_from_path(self):
        which = shutil.which
        lines = self.run_it(unittest.mock.patch.object(pre.shutil, "which",
                                                       lambda n: None if n == "git" else which(n)))
        self.assertIn("FAIL git: git is not on PATH, so the branch, the tree and the upstream cannot be checked", lines)
        self.assertIn("FAIL tool git: not on PATH", lines)

    def test_a_git_query_that_times_out_fails_only_its_own_check(self):
        real = subprocess.run  # window-exempt: a mock wrapper; the script under test passes its own flags

        def run(args, *a, **k):
            if list(args[:2]) == ["git", "status"]:
                raise subprocess.TimeoutExpired(args, 60)
            return real(args, *a, **k)
        lines = self.run_it(unittest.mock.patch.object(pre.subprocess, "run", run))
        clean = [x for x in lines if x.startswith("FAIL git-clean")]
        self.assertEqual(len(clean), 1, lines)
        self.assertIn("could not run", clean[0])
        self.assertIn("PASS git-branch: on main", lines)
        self.assertIn("PASS git-upstream: no upstream set", lines)

    def test_a_guard_script_that_does_not_load(self):
        self.seed(".claude/settings.json")
        (self.root / "scripts" / "campaign_guard.py").write_text("raise SystemError('broken install')\n",
                                                                 encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "broken guard")
        code, lines = self.run_preflight()
        self.assertEqual(code, 1)
        self.assertTrue(any(x.startswith("FAIL stop-hook: scripts/campaign_guard.py does not load (SystemError")
                            for x in lines), lines)
        self.assertTrue(any(x.startswith(("PASS disk", "FAIL disk")) for x in lines), lines)

    def test_unreadable_disk_and_memory_figures(self):
        lines = self.run_it(unittest.mock.patch.object(pre, "disk_free_gb", side_effect=OSError("volume gone")),
                            unittest.mock.patch.object(pre, "MEMINFO", Path(self._tmp.name) / "absent"))
        self.assertTrue(any(x.startswith("FAIL disk: free space on") and "volume gone" in x for x in lines), lines)
        self.assertTrue(any(x.startswith("FAIL memory: MemAvailable cannot be read") for x in lines), lines)


@posix_only
class ProfileCommandsTest(Fixture):
    def command(self, code):
        return [sys.executable, "-c", code]

    def check(self, *checks):
        report = self.report()
        pre.check_profile_commands(self.root, {**PROFILE, "preflight": list(checks)}, report)
        return self.lines(report)

    def test_each_outcome_is_one_line(self):
        lines = self.check(
            {"name": "ok", "command": self.command("print('one'); print('all good')"), "timeout_seconds": 30},
            {"name": "bad", "command": self.command("import sys; print('broken'); sys.exit(3)"), "timeout_seconds": 30},
            {"name": "quiet", "command": self.command("pass"), "timeout_seconds": 30},
            {"name": "absent", "command": ["/no/such/program", "x"], "timeout_seconds": 30})
        self.assertEqual(lines[:3], ["PASS check ok: exit 0: all good", "FAIL check bad: exit 3: broken",
                                     "PASS check quiet: exit 0: no output"])
        self.assertTrue(lines[3].startswith("FAIL check absent: exit 127: could not start /no/such/program"), lines)

    def test_a_check_runs_from_the_root_with_its_arguments_as_listed(self):
        lines = self.check({"name": "where", "command": self.command(
            "import os, sys, json; print(json.dumps([os.getcwd(), sys.argv[1:]]))") + ["a b", "$HOME", "'q'"],
                            "timeout_seconds": 30})
        cwd, args = json.loads(lines[0].split(": ", 2)[2])
        self.assertEqual((os.path.realpath(cwd), args), (os.path.realpath(self.root), ["a b", "$HOME", "'q'"]))

    def test_a_check_past_its_timeout_reports_124_and_its_child_is_gone(self):
        pid_file = Path(self._tmp.name) / "child.pid"
        script = ("import subprocess, sys, time; "
                  "c = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)']); "
                  f"open({str(pid_file)!r}, 'w').write(str(c.pid)); time.sleep(120)")
        started = time.monotonic()
        code, _ = pre.run_check({"name": "slow", "command": self.command(script), "timeout_seconds": 2}, self.root)
        self.assertEqual(code, 124)
        self.assertLess(time.monotonic() - started, 30)
        self.assertTrue(wait_gone(int(pid_file.read_text())), "the timed-out check's child outlived it")

    def test_a_child_left_by_a_check_that_exited_is_ended(self):
        pid_file = Path(self._tmp.name) / "child.pid"
        script = ("import subprocess, sys; "
                  "c = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)'], "
                  "stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL); "
                  f"open({str(pid_file)!r}, 'w').write(str(c.pid)); print('done')")
        code, last = pre.run_check({"name": "leaves", "command": self.command(script), "timeout_seconds": 30},
                                   self.root)
        self.assertEqual((code, last), (0, "done"))
        self.assertTrue(wait_gone(int(pid_file.read_text())), "a child left behind outlived its check")

    def test_output_is_a_bounded_tail_held_in_memory(self):
        flood = "import sys\nfor _ in range(800):\n    sys.stdout.write('x' * 65536)\nsys.stdout.write('\\nlast line')\n"
        # A temp folder of the test's own, for the preflight and its check alike: the machine's shared one gains files
        # from every other process on it, which failed this test in WSL on 2026-11-12 with nothing of the preflight's.
        private = Path(self._tmp.name) / "private-tmp"
        private.mkdir()
        with unittest.mock.patch.object(tempfile, "tempdir", str(private)), \
                unittest.mock.patch.dict(os.environ, {"TMPDIR": str(private), "TEMP": str(private), "TMP": str(private)}):
            code, last = pre.run_check({"name": "flood", "command": self.command(flood), "timeout_seconds": 120},
                                       self.root)
        self.assertEqual((code, last), (0, "last line"))
        self.assertEqual(os.listdir(private), [], "the output went to disk")
        tail = bytearray()
        read, write = os.pipe()
        writer = threading.Thread(target=lambda: (os.write(write, b"y" * 100000), os.close(write)))
        writer.start()
        pre._keep_tail(os.fdopen(read, "rb"), tail)
        writer.join()
        self.assertEqual(len(tail), pre.TAIL_BYTES)  # one long unterminated line costs no more than the bound
        self.assertLessEqual(len(pre._last_line(bytes(tail).decode())), 200)

    def test_an_interrupt_ends_the_group_and_is_raised(self):
        pid_file = Path(self._tmp.name) / "child.pid"
        script = ("import subprocess, sys, time; "
                  "c = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)']); "
                  f"open({str(pid_file)!r}, 'w').write(str(c.pid)); time.sleep(120)")
        real_wait = subprocess.Popen.wait
        calls = []

        def wait(proc, timeout=None):
            if not calls:
                calls.append(1)
                deadline = time.monotonic() + 30
                while not pid_file.exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                raise KeyboardInterrupt
            return real_wait(proc, timeout=timeout)
        with unittest.mock.patch.object(subprocess.Popen, "wait", wait):
            with self.assertRaises(KeyboardInterrupt):
                pre.run_check({"name": "slow", "command": self.command(script), "timeout_seconds": 60}, self.root)
        self.assertTrue(wait_gone(int(pid_file.read_text())), "an interrupted check's child outlived it")

    def test_a_ready_fixture_prints_only_pass_lines_and_exits_0(self):
        self.seed(".claude/settings.json")
        self.write_profile(preflight=[{"name": "true", "command": ["true"], "timeout_seconds": 30}])
        git(self.root, "add", "-A")
        git(self.root, "commit", "-qm", "seed")
        version = lambda profile, report: report.line(True, "version claude", "checked by VersionCheckTest")
        with unittest.mock.patch.object(pre.shutil, "which", lambda name: f"/usr/bin/{name}"), \
                unittest.mock.patch.object(pre, "check_agent_version", version), \
                unittest.mock.patch.object(pre, "available_gb", return_value=64.0), \
                unittest.mock.patch.object(pre, "disk_free_gb", return_value=500.0):
            code, lines = self.run_preflight()
        self.assertEqual(code, 0, lines)
        self.assertTrue(lines and all(line.startswith("PASS ") for line in lines), lines)
        names = self.names(lines)
        for name in ("PASS profile", "PASS git-clean", "PASS campaign-guard", "PASS tool tmux", "PASS stop-hook",
                     "PASS guard-self-test", "PASS disk", "PASS memory", "PASS check true"):
            self.assertIn(name, names)


if __name__ == "__main__":
    unittest.main()
