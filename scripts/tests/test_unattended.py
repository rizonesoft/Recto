"""Tests of the unattended kit's shared module (`scripts/lib/unattended.py`): the run profile loader."""
from __future__ import annotations

import ast
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

SCRIPTS = Path(__file__).resolve().parents[1]
TEMPLATES = SCRIPTS.parent
_spec = importlib.util.spec_from_file_location("unattended_lib", SCRIPTS / "lib" / "unattended.py")
assert _spec is not None and _spec.loader is not None
unattended = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(unattended)

TEMPLATE = TEMPLATES / ".conclave" / "unattended.json"
PROMPT = TEMPLATES / "docs" / "unattended-prompt.md"
# In the SDK these files are the kit's templates; installed in a project they are the project's own, which it edits.
IN_KIT = TEMPLATES.name == "templates" and TEMPLATES.parent.name == "unattended"
in_kit_only = unittest.skipUnless(IN_KIT, "checks the shipped template; a project's profile is its own")
# The loader tests use their own profile and prompt, so they hold whatever a project does with its copies.
FIXTURE = {"schema_version": 1, "name": "impossible-os", "agent": "claude", "model": "claude-opus-5-5", "effort": "high",
           "prompt_file": "docs/run-prompt.md", "branch": "main", "skip_permissions": True,
           "tools": ["git", "python3", "tmux"], "min_disk_gb": 20, "min_ram_gb": 2, "tick_seconds": 300,
           "idle_minutes": 45, "stall_hours": 6, "preflight": []}


def windowless():
    """No console window of its own for any process a test starts on Windows (a console child of a parent with
    none opens one over the owner's work: 2026-11-12, Office's launcher tests flashed 5 to 10); 0 elsewhere."""
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


class LoadProfileTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        (self.root / ".conclave").mkdir()
        (self.root / "docs").mkdir()
        (self.root / "docs" / "run-prompt.md").write_text("Run the plan.\n", encoding="utf-8")
        self.profile = json.loads(json.dumps(FIXTURE))

    def write(self, data):
        (self.root / ".conclave" / "unattended.json").write_text(
            data if isinstance(data, str) else json.dumps(data), encoding="utf-8")

    def refused(self, key, data=None):
        self.write(self.profile if data is None else data)
        with self.assertRaises(unattended.ProfileError) as caught:
            unattended.load_profile(self.root)
        message = str(caught.exception)
        self.assertTrue(message.startswith(".conclave/unattended.json: "), message)
        self.assertIn(key, message)
        return message

    def with_(self, **changes):
        return {**self.profile, **changes}

    @in_kit_only
    def test_the_template_set_for_a_project_loads(self):
        shutil.copyfile(PROMPT, self.root / "docs" / "unattended-prompt.md")
        configured = {**json.loads(TEMPLATE.read_text(encoding="utf-8")),
                      "name": "impossible-os", "model": "claude-opus-5-5", "effort": "high"}
        self.write(configured)
        loaded = unattended.load_profile(self.root)
        self.assertEqual(loaded, configured)
        self.assertEqual((loaded["agent"], loaded["branch"], loaded["min_disk_gb"], loaded["min_ram_gb"],
                          loaded["tick_seconds"], loaded["idle_minutes"], loaded["stall_hours"]),
                         ("claude", "main", 20, 2, 300, 45, 6))
        self.assertEqual(loaded["prompt_file"], "docs/unattended-prompt.md")

    @in_kit_only
    def test_the_shipped_template_unchanged_names_the_three_to_set(self):
        self.write(TEMPLATE.read_text(encoding="utf-8"))
        message = self.refused("name", json.loads(TEMPLATE.read_text(encoding="utf-8")))
        for key in ("name", "model", "effort"):
            self.assertIn(key, message)
        self.assertIn("set", message)

    def test_each_null_is_named(self):
        for key in ("name", "model", "effort"):
            with self.subTest(key=key):
                self.refused(key, self.with_(**{key: None}))

    def test_a_missing_profile_or_unreadable_json_is_refused(self):
        with self.assertRaises(unattended.ProfileError) as caught:
            unattended.load_profile(self.root)
        self.assertIn("missing", str(caught.exception))
        self.refused("JSON", "{not json")
        self.refused("object", "[1, 2]")

    def test_an_unknown_or_missing_key_is_refused(self):
        self.refused("agnet", self.with_(agnet="claude"))
        data = dict(self.profile)
        del data["tick_seconds"]
        self.assertIn("is required", self.refused("tick_seconds", data))

    def test_the_schema_version_must_be_1(self):
        for bad in (2, "1", True, None):
            with self.subTest(bad=bad):
                self.refused("schema_version", self.with_(schema_version=bad))

    def test_an_unknown_agent_is_refused(self):
        for bad in ("gemini", "Claude", "", None, 1):
            with self.subTest(bad=bad):
                self.refused("agent", self.with_(agent=bad))
        for good in ("claude", "codex", "agy"):
            self.write(self.with_(agent=good))
            self.assertEqual(unattended.load_profile(self.root)["agent"], good)

    def test_the_name_must_be_a_session_name(self):
        for bad in ("a", "Impossible", "-run", "has space", "x" * 42, "dots.no", 7):
            with self.subTest(bad=bad):
                self.refused("name", self.with_(name=bad))

    def test_strings_must_be_strings(self):
        for key in ("model", "effort", "branch"):
            for bad in ("", "  ", 3, ["x"]):
                with self.subTest(key=key, bad=bad):
                    self.refused(key, self.with_(**{key: bad}))
        self.refused("skip_permissions", self.with_(skip_permissions="yes"))

    def test_numbers_must_be_whole_and_positive(self):
        for key in unattended.WHOLE:
            for bad in (0, -1, 2.5, "20", True, None):
                with self.subTest(key=key, bad=bad):
                    self.refused(key, self.with_(**{key: bad}))

    def test_tools_must_be_command_names(self):
        for bad in ("git", ["git", ""], ["git", 3], None):
            with self.subTest(bad=bad):
                self.refused("tools", self.with_(tools=bad))

    def test_the_prompt_file_must_exist_inside_the_repository(self):
        outside = Path(self._tmp.name).parent / "outside-prompt.md"
        for bad in ("../outside-prompt.md", "docs/../../x.md", str(outside), "/etc/passwd", "C:/x.md", "", 4):
            with self.subTest(bad=bad):
                self.refused("prompt_file", self.with_(prompt_file=bad))
        self.assertIn("does not exist", self.refused("prompt_file", self.with_(prompt_file="docs/nope.md")))

    def test_preflight_checks_are_name_command_and_timeout(self):
        good = {"name": "suite", "command": ["python3", "-m", "unittest"], "timeout_seconds": 600}
        self.write(self.with_(preflight=[good]))
        self.assertEqual(unattended.load_profile(self.root)["preflight"], [good])
        bad_cases = [
            ("preflight", {"preflight": {"name": "x"}}),
            ("preflight[0]", {"preflight": ["python3 -m unittest"]}),
            ("preflight[0]", {"preflight": [{**good, "extra": 1}]}),
            ("preflight[0]", {"preflight": [{"name": "x", "command": ["y"]}]}),
            ("preflight[0].name", {"preflight": [{**good, "name": ""}]}),
            ("preflight[0].command", {"preflight": [{**good, "command": "python3 -m unittest"}]}),
            ("preflight[0].command", {"preflight": [{**good, "command": []}]}),
            ("preflight[0].command", {"preflight": [{**good, "command": ["python3", 1]}]}),
            ("preflight[1].timeout_seconds", {"preflight": [good, {**good, "timeout_seconds": 0}]}),
            ("preflight[0].timeout_seconds", {"preflight": [{**good, "timeout_seconds": True}]}),
        ]
        for key, change in bad_cases:
            with self.subTest(key=key, change=change):
                self.refused(key, self.with_(**change))

    def test_an_edited_profile_is_read_back_on_reload(self):
        self.write(self.profile)
        self.assertEqual(unattended.load_profile(self.root)["idle_minutes"], self.profile["idle_minutes"])
        self.write(self.with_(idle_minutes=self.profile["idle_minutes"] + 45, branch="trunk", agent="codex", effort="xhigh"))
        loaded = unattended.load_profile(self.root)
        self.assertEqual((loaded["idle_minutes"], loaded["branch"], loaded["agent"], loaded["effort"]),
                         (self.profile["idle_minutes"] + 45, "trunk", "codex", "xhigh"))

    def test_the_effort_must_be_one_the_agent_takes(self):
        self.assertIn("for claude", self.refused("effort", self.with_(effort="minimal")))
        self.refused("effort", self.with_(agent="codex", effort="max"))
        self.refused("effort", self.with_(agent="agy", effort="minimal"))  # agy --help: low|medium|high|xhigh|max
        for agent, effort in (("claude", "max"), ("codex", "minimal"), ("agy", "xhigh")):
            self.write(self.with_(agent=agent, effort=effort))
            self.assertEqual(unattended.load_profile(self.root)["effort"], effort)

    def test_a_preflight_command_must_start_with_a_program(self):
        good = {"name": "suite", "command": ["python3"], "timeout_seconds": 60}
        for bad in ([""], ["  ", "-m"]):
            with self.subTest(bad=bad):
                self.refused("preflight[0].command", self.with_(preflight=[{**good, "command": bad}]))

    def test_a_nul_in_an_argument_or_a_path_is_refused(self):
        good = {"name": "check", "command": ["python3"], "timeout_seconds": 60}
        self.refused("preflight[0].command", self.with_(preflight=[{**good, "command": ["python3", "\0"]}]))
        self.refused("tools", self.with_(tools=["git\0"]))
        for key in ("model", "effort", "branch", "prompt_file"):
            with self.subTest(key=key):
                self.assertIn("NUL", self.refused(key, self.with_(**{key: self.profile[key] + "\0x"})))

    def denied(self, name):
        """Path.read_bytes raising PermissionError for one file, as an unreadable file does on any system."""
        real = Path.read_bytes

        def read_bytes(path):
            if path.name == name:
                raise PermissionError(13, "Permission denied", str(path))
            return real(path)
        return unittest.mock.patch.object(Path, "read_bytes", read_bytes)

    def test_a_profile_that_cannot_be_read_is_refused(self):
        self.write(self.profile)
        with self.denied("unattended.json"):
            with self.assertRaises(unattended.ProfileError) as caught:
                unattended.load_profile(self.root)
        message = str(caught.exception)
        self.assertTrue(message.startswith(".conclave/unattended.json: not readable"), message)
        self.assertIn("Permission denied", message)

    def test_a_prompt_that_cannot_be_read_is_refused(self):
        self.write(self.profile)
        with self.denied("run-prompt.md"):
            message = self.refused("prompt_file")
        self.assertIn("Permission denied", message)

    def test_the_prompt_file_must_be_a_readable_text_file(self):
        (self.root / "docs" / "folder.md").mkdir()
        self.assertIn("not a file", self.refused("prompt_file", self.with_(prompt_file="docs/folder.md")))
        (self.root / "docs" / "binary.md").write_bytes(b"\xff\xfe\x00bad")
        self.assertIn("UTF-8", self.refused("prompt_file", self.with_(prompt_file="docs/binary.md")))

    def test_a_prompt_link_leading_outside_the_repository_is_refused(self):
        outside = Path(self._tmp.name + "-outside.md")
        outside.write_text("outside", encoding="utf-8")
        self.addCleanup(outside.unlink)
        link = self.root / "docs" / "link.md"
        try:
            link.symlink_to(outside)
        except OSError as exc:  # Windows without the symlink privilege or developer mode
            self.skipTest(f"cannot make a symlink here: {exc}")
        self.assertIn("outside the repository", self.refused("prompt_file", self.with_(prompt_file="docs/link.md")))

    @in_kit_only
    def test_the_seed_prompt_names_no_project_and_says_what_a_run_must_do(self):
        text = PROMPT.read_text(encoding="utf-8")
        for phrase in ("process-plan", "Never ask the operator", "run file", "docs/phase-runs/", "PARKED"):
            self.assertIn(phrase, text)
        for name in ("RasterFold", "Impossible OS", "Conclave-SDK"):
            self.assertNotIn(name, text)


posix_only = unittest.skipUnless(os.name == "posix", "the launch lock is fcntl.flock: Linux and WSL")


@posix_only
class LaunchLockTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def test_a_second_launch_is_refused_while_the_first_holds_the_lock(self):
        holder = subprocess.Popen([sys.executable, "-c", (
            "import importlib.util, sys, time\n"
            f"spec = importlib.util.spec_from_file_location('u', {str(SCRIPTS / 'lib' / 'unattended.py')!r})\n"
            "u = importlib.util.module_from_spec(spec); spec.loader.exec_module(u)\n"
            f"with u.launch_lock({str(self.root)!r}):\n"
            "    print('held', flush=True); time.sleep(60)\n")], stdout=subprocess.PIPE, text=True,
            creationflags=windowless())
        self.addCleanup(lambda: (holder.kill(), holder.wait(), holder.stdout.close()))
        self.assertEqual(holder.stdout.readline().strip(), "held")
        with self.assertRaises(unattended.LaunchError) as caught:
            with unattended.launch_lock(self.root):
                self.fail("a second launch took the lock")
        self.assertIn("build/unattended-launch.lock", str(caught.exception))
        holder.kill()
        holder.wait()
        with unattended.launch_lock(self.root) as path:  # the kernel let go when the holder died
            self.assertEqual(path, self.root / "build" / "unattended-launch.lock")

    def test_the_lock_is_released_when_the_block_ends(self):
        with unattended.launch_lock(self.root):
            pass
        with unattended.launch_lock(self.root):
            pass


class ClaimTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        self.live_sessions = set()
        self.unknown = False

    def exists(self, name):
        return None if self.unknown else name in self.live_sessions

    def claim_path(self):
        return self.root / "build" / "unattended-launch.json"

    def claim(self, session="session-a", tmux="demo-run"):
        return unattended.write_claim(self.root, session, 4242, "claude", tmux)

    def test_a_written_claim_reads_back(self):
        written = unattended.write_claim(self.root, "session-a", 4242, "claude", "demo-run",
                                         started="2026-11-12T10:00:00Z")
        self.assertEqual(json.loads(self.claim_path().read_text(encoding="utf-8")), written)
        self.assertEqual(written, {"session": "session-a", "tmux_session": "demo-run", "pid": 4242,
                                   "agent": "claude", "started": "2026-11-12T10:00:00Z"})
        self.assertEqual([p.name for p in self.claim_path().parent.iterdir()], ["unattended-launch.json"],
                         "the temporary file was left behind")

    def test_a_failed_write_leaves_the_old_claim_whole(self):
        self.claim()
        before = self.claim_path().read_bytes()
        with unittest.mock.patch.object(unattended.os, "replace", side_effect=OSError(28, "No space left")):
            with self.assertRaises(OSError):
                unattended.write_claim(self.root, "session-b", 2, "codex", "other-run")
        self.assertEqual(self.claim_path().read_bytes(), before)
        self.assertEqual(len(list(self.claim_path().parent.iterdir())), 1)

    def test_no_live_gone_unknown_and_malformed_claims(self):
        self.assertEqual(unattended.read_claim(self.root, self.exists), (None, False, "no launch claim"))
        self.claim()
        self.live_sessions.add("demo-run")
        claim, live, reason = unattended.read_claim(self.root, self.exists)
        self.assertIs(live, True, reason)
        self.assertEqual(claim["session"], "session-a")
        self.unknown = True
        _claim, live, reason = unattended.read_claim(self.root, self.exists)
        self.assertIsNone(live)
        self.assertIn("cannot be told", reason)
        self.unknown = False
        self.live_sessions.clear()
        _claim, live, reason = unattended.read_claim(self.root, self.exists)
        self.assertIs(live, False)
        self.assertIn("is gone: stale", reason)
        for text, why in (("{not json", "not readable JSON"), ("[1]", "not a JSON object"),
                          (json.dumps({"session": "s"}), "lacks tmux_session")):
            with self.subTest(text=text):
                self.claim_path().write_text(text, encoding="utf-8")
                self.live_sessions.add("demo-run")
                _claim, live, reason = unattended.read_claim(self.root, self.exists)
                self.assertIs(live, False)
                self.assertIn(why, reason)
                self.assertIn("stale", reason)

    @posix_only
    def test_a_live_claim_of_another_session_is_never_cleared(self):
        self.claim()
        self.live_sessions.add("demo-run")
        with self.assertRaises(unattended.LaunchError) as caught:
            unattended.clear_claim(self.root, "session-b", self.exists)
        self.assertIn("session-a", str(caught.exception))
        self.assertTrue(self.claim_path().exists())

    @posix_only
    def test_the_owners_own_live_claim_is_cleared(self):
        self.claim()
        self.live_sessions.add("demo-run")
        self.assertIn("stopped", unattended.clear_claim(self.root, "session-a", self.exists))
        self.assertFalse(self.claim_path().exists())
        # Only the launch lock stays: flock locks the open file, so the file itself is kept for the next launch.
        self.assertEqual([p.name for p in self.claim_path().parent.iterdir()], ["unattended-launch.lock"])

    @posix_only
    def test_a_stale_claim_is_cleared_by_anyone_and_a_second_clear_is_a_no_op(self):
        self.claim()
        self.assertIn("stale", unattended.clear_claim(self.root, "session-b", self.exists))
        self.assertFalse(self.claim_path().exists())
        self.assertEqual(unattended.clear_claim(self.root, "session-b", self.exists), "no launch claim")

    @posix_only
    def test_a_claim_whose_liveness_is_unknown_is_never_cleared(self):
        self.claim()
        self.unknown = True
        with self.assertRaises(unattended.LaunchError) as caught:
            unattended.clear_claim(self.root, "session-b", self.exists)
        self.assertIn("cannot be told", str(caught.exception))
        self.assertTrue(self.claim_path().exists())

    @posix_only
    def test_a_claim_replaced_after_it_was_read_is_left_in_place(self):
        self.claim()
        real = unattended.read_claim

        def read_then_replace(root, session_exists):
            result = real(root, session_exists)
            unattended.write_claim(self.root, "session-new", 7, "codex", "new-run")  # a new launch, just now
            return result
        with unittest.mock.patch.object(unattended, "read_claim", read_then_replace):
            with self.assertRaises(unattended.LaunchError) as caught:
                unattended.clear_claim(self.root, "session-b", self.exists)
        self.assertIn("changed after it was read", str(caught.exception))
        self.assertEqual(json.loads(self.claim_path().read_text(encoding="utf-8"))["session"], "session-new")
        names = sorted(p.name for p in self.claim_path().parent.iterdir())
        self.assertEqual(names, ["unattended-launch.json", "unattended-launch.lock"], "the claim was ever moved")

    def test_a_claim_that_exists_but_cannot_be_read_is_unknown(self):
        # Round 1 of D02 T02 §4: a permission or I/O error read as stale, which let a launch in.
        self.claim()
        real = Path.read_bytes

        def denied(p):
            if p.name == "unattended-launch.json":
                raise PermissionError(13, "Permission denied", str(p))
            return real(p)
        with unittest.mock.patch.object(Path, "read_bytes", denied):
            claim, live, reason = unattended.read_claim(self.root, self.exists)
        self.assertIsNone(live)
        self.assertIn("cannot be read", reason)
        self.assertIn("Permission denied", reason)

    @posix_only
    def test_a_clear_is_refused_while_a_launch_holds_the_lock(self):
        # Round 1 of D02 T02 §4: a clear that ran beside a launch could hide the claim it was writing.
        self.claim()
        holder = subprocess.Popen([sys.executable, "-c", (
            "import importlib.util, time\n"
            f"spec = importlib.util.spec_from_file_location('u', {str(SCRIPTS / 'lib' / 'unattended.py')!r})\n"
            "u = importlib.util.module_from_spec(spec); spec.loader.exec_module(u)\n"
            f"with u.launch_lock({str(self.root)!r}):\n"
            "    print('held', flush=True); time.sleep(60)\n")], stdout=subprocess.PIPE, text=True,
            creationflags=windowless())
        self.addCleanup(lambda: (holder.kill(), holder.wait(), holder.stdout.close()))
        self.assertEqual(holder.stdout.readline().strip(), "held")
        with self.assertRaises(unattended.LaunchError) as caught:
            unattended.clear_claim(self.root, "session-a", self.exists)
        self.assertIn("unattended-launch.lock", str(caught.exception))
        self.assertTrue(self.claim_path().exists())

    @posix_only
    def test_a_launcher_holding_the_lock_clears_with_lock_held(self):
        self.claim()
        with unattended.launch_lock(self.root):
            self.assertIn("stale", unattended.clear_claim(self.root, "session-b", self.exists, lock_held=True))
        self.assertFalse(self.claim_path().exists())

    def test_tmux_lookup_failures_are_unknown_and_its_own_answers_are_not(self):
        def answer(code, stderr):
            return unittest.mock.patch.object(unattended.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], code, stdout="", stderr=stderr))
        for code, stderr, expected in ((0, "", True), (1, "can't find session: =demo-run\n", False),
                                       (1, "no server running on /tmp/tmux-1000/default\n", False),
                                       (1, "error connecting to /tmp/tmux-1000/default (No such file or directory)\n",
                                        False),
                                       (1, "error connecting to /tmp/tmux-1000/default (Permission denied)\n", None)):
            with self.subTest(stderr=stderr), answer(code, stderr):
                self.assertIs(unattended.tmux_session_exists("demo-run"), expected)
        for error in (FileNotFoundError(2, "No such file", "tmux"), subprocess.TimeoutExpired(["tmux"], 30)):
            with self.subTest(error=error), unittest.mock.patch.object(unattended.subprocess, "run",
                                                                       side_effect=error):
                self.assertIsNone(unattended.tmux_session_exists("demo-run"))

    @unittest.skipUnless(shutil.which("tmux"), "tmux is not installed here (the WSL run has it)")
    def test_a_real_tmux_session_is_live_and_a_prefix_is_not(self):
        name = f"claim-test-{os.getpid()}"
        subprocess.run(["tmux", "new-session", "-d", "-s", name, "sleep 60"], check=True, timeout=30,
                       creationflags=windowless())
        self.addCleanup(subprocess.run, ["tmux", "kill-session", "-t", f"={name}"], capture_output=True, timeout=30,
                        creationflags=windowless())
        self.assertIs(unattended.tmux_session_exists(name), True)
        self.assertIs(unattended.tmux_session_exists(name[:-1]), False)


# The todo kit's graph library, which owns what an Implementation Order row is: installed beside lib/unattended.py
# in a project, and in the todo kit's own templates when these tests run from the SDK.
TODO_GRAPH = next(p for p in (SCRIPTS / "lib" / "todo_graph.py",
                              TEMPLATES.parent.parent / "todo" / "templates" / "scripts" / "lib" / "todo_graph.py")
                  if p.is_file())
TABLE = ("# TODO-01 -- Demo\n\n## Implementation Order\n\n| Order | § | Deliverable | Depends On | Done |\n"
         "| ----- | - | ----------- | ---------- | ---- |\n| 1 | §2 | Second | none | {s2} |\n| 2 | §1 | First | none | {s1} |\n")


def run_git(cwd, *args, when=None):
    env = {**os.environ, **({"GIT_COMMITTER_DATE": f"@{when} +0000", "GIT_AUTHOR_DATE": f"@{when} +0000"} if when
                           else {})}
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=True, timeout=60, env=env,
                          creationflags=windowless()).stdout.strip()


class ShipCountTest(unittest.TestCase):
    """Sections shipped, counted commit by commit from git with the todo kit's row pattern (D02 T02 §8)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name) / "repo"
        lib_dir = self.root / "scripts" / "lib"
        lib_dir.mkdir(parents=True)
        shutil.copyfile(SCRIPTS / "lib" / "unattended.py", lib_dir / "unattended.py")
        shutil.copyfile(TODO_GRAPH, lib_dir / "todo_graph.py")
        spec = importlib.util.spec_from_file_location("ships_lib", lib_dir / "unattended.py")
        self.lib = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.lib)
        self.todo = self.root / "todo" / "02-demo" / "TODO-01-demo.md"
        self.todo.parent.mkdir(parents=True)
        for args in (["init", "-q", "-b", "main"], ["config", "user.email", "t@t"], ["config", "user.name", "t"],
                     ["config", "commit.gpgsign", "false"]):
            run_git(self.root, *args)
        self.base = self.commit("[ ]", "[ ]", "fixture", when=1_700_000_000)

    def commit(self, s1, s2, message, when=None, extra=""):
        self.todo.write_text(TABLE.format(s1=s1, s2=s2) + extra, encoding="utf-8")
        run_git(self.root, "add", "-A")
        run_git(self.root, "commit", "-qm", message, when=when)
        return run_git(self.root, "rev-parse", "HEAD")

    def test_two_rows_stamped_in_two_commits_are_two_ships_with_their_times(self):
        first = self.commit("[x]", "[ ]", "stamp §1", when=1_700_000_100)
        second = self.commit("[x]", "[x]", "stamp §2", when=1_700_000_200)
        self.assertEqual(self.lib.ships_between(self.root, self.base, second), 2)
        self.assertEqual(self.lib.ships_along(self.root, self.base, second),
                         [(first, 1_700_000_100, 1), (second, 1_700_000_200, 1)])
        self.assertEqual(self.lib.ships_between(self.root, first, second), 1)

    def test_a_row_reopened_and_stamped_again_counts_both_ships(self):
        # The endpoints alone read one: `[ ]` at the base, `[x]` at the tip (ship_count.py:51).
        self.commit("[x]", "[ ]", "stamp §1")
        self.commit("[ ]", "[ ]", "unstamp §1")
        tip = self.commit("[x]", "[ ]", "stamp §1 again")
        self.assertEqual(self.lib.ships_between(self.root, self.base, tip), 2)

    def test_an_unstamp_or_an_edit_of_a_stamped_row_is_no_ship(self):
        stamped = self.commit("[x]", "[ ]", "stamp §1")
        self.todo.write_text(TABLE.format(s1="[x]", s2="[ ]").replace("| First |", "| First, renamed |"),
                             encoding="utf-8")
        run_git(self.root, "commit", "-qam", "edit a stamped row")
        edited = run_git(self.root, "rev-parse", "HEAD")
        unstamped = self.commit("[ ]", "[ ]", "unstamp §1")
        self.assertEqual(self.lib.ships_between(self.root, stamped, edited), 0)
        self.assertEqual(self.lib.ships_between(self.root, edited, unstamped), 0)

    def test_the_working_tree_is_never_read(self):
        self.todo.write_text(TABLE.format(s1="[x]", s2="[x]"), encoding="utf-8")  # uncommitted
        self.assertEqual(self.lib.ships_between(self.root, self.base, run_git(self.root, "rev-parse", "HEAD")), 0)

    def test_a_line_that_starts_with_plus_signs_inside_a_hunk_is_content(self):
        # `++ note` added in the hunk reads as `+++ note`, which a header parser would take for a file name.
        tip = self.commit("[x]", "[ ]", "stamp §1 with a note", extra="\n++ note\n")
        self.assertEqual(self.lib.ships_between(self.root, self.base, tip), 1)

    def test_a_todo_file_whose_path_git_would_quote_is_counted(self):
        # Round 1 of D02 T02 §8: git quotes a non-ASCII path (`+++ "b/todo/02-caf\303\251/..."`), and it counted 0.
        self.todo = self.root / "todo" / "02-café" / "TODO-01-demo.md"
        self.todo.parent.mkdir(parents=True)
        base = self.commit("[ ]", "[ ]", "a domain with a non-ASCII name")
        tip = self.commit("[x]", "[ ]", "stamp its §1")
        self.assertEqual(self.lib.ships_between(self.root, base, tip), 1)
        # git keeps quoting a name with a control character even with core.quotePath off.
        self.assertEqual(self.lib._unquote('"todo/02-caf\\303\\251/TODO-01-demo.md"'), "todo/02-café/TODO-01-demo.md")

    def test_rows_outside_a_todo_files_order_table_ship_nothing(self):
        # Round 1 of D02 T02 §8: an example row in todo/README.md, or in a section's body, read as a ship.
        readme = self.root / "todo" / "README.md"
        example = "\n## 1. First\n\nAn example row: \n\n| 3 | §3 | Example | none | {s} |\n"
        readme.write_text("# TODO system\n\n| 1 | §1 | Example | none | [ ] |\n", encoding="utf-8")
        base = self.commit("[ ]", "[ ]", "a README and an example", extra=example.format(s="[ ]"))
        readme.write_text("# TODO system\n\n| 1 | §1 | Example | none | [x] |\n", encoding="utf-8")
        tip = self.commit("[ ]", "[ ]", "tick the examples", extra=example.format(s="[x]"))
        self.assertEqual(self.lib.ships_between(self.root, base, tip), 0)
        shipped = self.commit("[x]", "[ ]", "a real stamp", extra=example.format(s="[x]"))
        self.assertEqual(self.lib.ships_between(self.root, tip, shipped), 1)

    def test_an_example_of_a_section_already_shipped_is_no_ship(self):
        # Completion review of D02 T02 §8: the example names §1, whose table row is already [x], and read as a ship.
        example = "\n## 2. First\n\nAn example row:\n\n| 2 | §1 | First | none | {s} |\n"
        base = self.commit("[x]", "[ ]", "§1 shipped, with an example", extra=example.format(s="[ ]"))
        tip = self.commit("[x]", "[ ]", "tick the example", extra=example.format(s="[x]"))
        self.assertEqual(self.lib.ships_between(self.root, base, tip), 0)

    def test_a_real_ship_beside_a_removed_example_of_the_same_section_counts(self):
        example = "\n## 2. First\n\nAn example row:\n\n| 2 | §1 | First | none | [x] |\n"
        base = self.commit("[ ]", "[ ]", "an example of §1, already ticked", extra=example)
        tip = self.commit("[x]", "[ ]", "ship §1 and drop the example")
        self.assertEqual(self.lib.ships_between(self.root, base, tip), 1)

    def test_a_project_without_the_todo_kit_is_refused_by_name(self):
        (self.root / "scripts" / "lib" / "todo_graph.py").unlink()
        spec = importlib.util.spec_from_file_location("ships_lib_bare", self.root / "scripts" / "lib" / "unattended.py")
        bare = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bare)
        with self.assertRaises(bare.ShipCountError) as caught:
            bare.ships_between(self.root, self.base, self.base + "~0")
        self.assertIn("needs the todo kit", str(caught.exception))

    def test_a_commit_git_cannot_resolve_is_a_ship_count_error(self):
        with self.assertRaises(self.lib.ShipCountError):
            self.lib.ships_between(self.root, "0" * 40, "HEAD")


class WindowlessTest(unittest.TestCase):
    """Every process the kit starts, from its scripts and from its tests, opens no console window on Windows (the
    owner's rule of 2026-11-12: a launcher test never opens a window)."""

    def starts(self, tree):
        """Each start through subprocess.run or subprocess.Popen, with the call whose keywords configure it: the call
        itself, or the callback registration it is handed to (`addCleanup(subprocess.run, ...)`), or None for an alias
        (`start = subprocess.run`), which hides its starts. A type annotation, an attribute of the class
        (`subprocess.Popen.wait`) and a mock's target (`patch.object(subprocess.Popen, ...)`) start nothing."""
        parents = {child: node for node in ast.walk(tree) for child in ast.iter_child_nodes(node)}
        for node in ast.walk(tree):
            if not (isinstance(node, ast.Attribute) and node.attr in ("run", "Popen")
                    and isinstance(node.value, ast.Name) and node.value.id == "subprocess"):
                continue
            parent = parents.get(node)
            if isinstance(parent, ast.Call) and parent.func is node:
                yield node.lineno, parent
            elif isinstance(parent, ast.Call) and node in parent.args:
                if getattr(parent.func, "attr", "") in ("addCleanup", "callback", "submit"):
                    yield node.lineno, parent
            elif isinstance(parent, (ast.Assign, ast.AnnAssign, ast.keyword, ast.Return)):
                yield node.lineno, None

    # The kit's own Python files: in an installed project scripts/ also holds the other kits' files, which are theirs.
    KIT_FILES = ("campaign-icon.py", "lib/unattended.py", "tests/test_campaign_icon.py", "tests/test_preflight.py",
                 "tests/test_run.py", "tests/test_unattended.py", "unattended-preflight.py", "unattended-run.py")

    def test_the_kit_file_list_is_complete_in_the_kit_itself(self):
        if SCRIPTS.parent.name != "templates":
            self.skipTest("an installed copy: scripts/ holds other kits' files too")
        found = sorted(p.relative_to(SCRIPTS).as_posix() for p in SCRIPTS.rglob("*.py") if "__pycache__" not in p.parts)
        self.assertEqual(found, sorted(self.KIT_FILES), "a kit file is missing from WindowlessTest.KIT_FILES")

    def test_every_process_the_kit_starts_gets_no_window(self):
        files = [SCRIPTS / name for name in self.KIT_FILES]
        seen = 0
        for path in files:
            text = path.read_text(encoding="utf-8")
            lines = text.splitlines()
            for line, call in self.starts(ast.parse(text)):
                if "window-exempt:" in lines[line - 1]:
                    continue  # the reason is on the line, as the owner's launcher-test hook asks
                seen += 1
                with self.subTest(file=path.name, line=line):
                    flags = [k.value for k in (call.keywords if call else []) if k.arg == "creationflags"]
                    named = flags and isinstance(flags[0], ast.Call) and (
                        getattr(flags[0].func, "id", "") == "windowless" or getattr(flags[0].func, "attr", "") ==
                        "windowless")
                    self.assertTrue(named, f"{path.name}:{line} starts a process without creationflags=windowless()")
        self.assertGreaterEqual(seen, 40)

    def test_the_check_refuses_a_start_without_the_flag(self):
        for source in ("subprocess.run(['tmux'], timeout=30)\n", "x.addCleanup(subprocess.run, ['tmux'])\n",
                       "subprocess.Popen(['tmux'], creationflags=0)\n", "start = subprocess.run\n"):
            with self.subTest(source=source):
                found = list(self.starts(ast.parse(source)))
                self.assertTrue(found)
                call = found[0][1]
                self.assertFalse(call and any(k.arg == "creationflags" and isinstance(k.value, ast.Call)
                                              for k in call.keywords))

    def test_the_flag_is_create_no_window_on_windows_and_none_elsewhere(self):
        expected = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self.assertEqual((unattended.windowless(), windowless()), (expected, expected))


if __name__ == "__main__":
    unittest.main()
