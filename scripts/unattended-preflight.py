#!/usr/bin/env python3
"""Refuse an unattended run that cannot work: one PASS or FAIL line per precondition, exit 1 on any FAIL.

    python3 scripts/unattended-preflight.py [--root DIR] [--resume-session ID]

Reads the run profile (`.conclave/unattended.json`, through `scripts/lib/unattended.py`) and checks, in order: the
system (tmux runs on Linux and in WSL only), git (the profile's branch, a clean tree, not behind the upstream as last
fetched), the run (no campaign guard and no live launch claim, or only those of `--resume-session`, the run being
resumed after a crash), the tools on PATH, the agent's Stop hook (wired, its own self-test, and for Codex approved in Codex), disk
and memory headroom, then the profile's own `preflight` commands, each in its own process group with its own
timeout. A check that cannot run is a FAIL saying why; the checks that do not depend on it still run.
Standard library only.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import threading

HERE = Path(__file__).resolve().parent
# Loading the kit's own modules must not write __pycache__ into the project: that dirties the tree the preflight
# checks, and a launch would refuse itself.
sys.dont_write_bytecode = True
TIMED_OUT = 124  # the status `timeout(1)` reports, given to a check that ran past its limit
NOT_STARTED = 127  # the status a shell gives a program it could not start
AGENT_CLI = {"claude": "claude", "codex": "codex", "agy": "agy"}
# Each agent's Stop hook file; the markers and the parsing are the guard's own (`HOOK_FILES` in campaign_guard.py).
AGENT_HOOK_FILE = {"claude": ".claude/settings.json", "codex": ".codex/hooks.json", "agy": ".agents/hooks.json"}
GUARD = "build/claude-campaign-guard.json"
SELF_TEST_SECONDS = 300
TRUST_SECONDS = 120
TAIL_BYTES = 4096  # what a profile check's output costs at most: its last 4 KiB, held in memory only
MEMINFO = Path("/proc/meminfo")


def windowless() -> int:
    """No console window of its own for any process this starts on Windows; nothing elsewhere (lib/unattended.py)."""
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Report:
    def __init__(self, out=None):
        self.out = out or sys.stdout
        self.failed = False

    def line(self, ok: bool, name: str, detail: str) -> None:
        self.failed |= not ok
        print(f"{'PASS' if ok else 'FAIL'} {name}: {detail}", file=self.out, flush=True)


def _last_line(text: str) -> str:
    lines = text.strip().splitlines()
    return lines[-1][-200:] if lines else ""


def _git(root: Path, *args: str) -> subprocess.CompletedProcess:
    """One git query, never raising: a query that cannot run or runs past a minute reads as a failed query (exit -1)
    whose stderr says why, so each git precondition reports on its own."""
    try:
        return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, timeout=60,
                              creationflags=windowless())
    except (OSError, subprocess.SubprocessError) as exc:
        return subprocess.CompletedProcess(["git", *args], -1, stdout="", stderr=f"git {args[0]} could not run: {exc}")


def check_git(root: Path, profile: dict, report: Report, resuming: bool = False) -> None:
    """The branch, a clean tree and the upstream. A resume owns the crashed run's unfinished edits, so a tree it left
    changed passes then, saying how much was kept (D02 T02 §7)."""
    if shutil.which("git") is None:
        report.line(False, "git", "git is not on PATH, so the branch, the tree and the upstream cannot be checked")
        return
    branch = _git(root, "rev-parse", "--abbrev-ref", "HEAD")
    name = branch.stdout.strip() if branch.returncode == 0 else None
    if name is None:
        report.line(False, "git-branch", f"not a git repository with a commit ({_last_line(branch.stderr)})")
    else:
        report.line(name == profile["branch"], "git-branch",
                    f"on {name}" + ("" if name == profile["branch"] else f", the profile runs on {profile['branch']}"))
    status = _git(root, "status", "--porcelain", "--untracked-files=all")
    dirty = [line for line in status.stdout.splitlines() if line.strip()]
    if status.returncode != 0:
        report.line(False, "git-clean", f"git status failed ({_last_line(status.stderr)})")
    elif dirty and resuming:
        report.line(True, "git-clean", f"{len(dirty)} changed or untracked path(s) kept for the resumed run, first "
                                         f"{dirty[0][3:]}")
    else:
        report.line(not dirty, "git-clean",
                    "clean tree" if not dirty else f"{len(dirty)} changed or untracked path(s), first {dirty[0][3:]}")
    if name is None:
        report.line(False, "git-upstream", "cannot be checked: the branch is not known (see git-branch)")
        return
    configured = _git(root, "config", "--get", f"branch.{name}.merge")
    if configured.returncode == 1:  # git's exit for a key that is not set: the branch tracks nothing
        report.line(True, "git-upstream", "no upstream set")
        return
    upstream = _git(root, "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
    if configured.returncode != 0 or upstream.returncode != 0:
        report.line(False, "git-upstream", f"{name} tracks {configured.stdout.strip() or 'an upstream'}, which "
                                           f"cannot be resolved ({_last_line(upstream.stderr) or 'git config failed'})")
        return
    behind = _git(root, "rev-list", "--count", "HEAD..@{u}")
    if behind.returncode != 0 or not behind.stdout.strip().isdigit():
        report.line(False, "git-upstream", f"could not compare with {upstream.stdout.strip()} "
                                           f"({_last_line(behind.stderr)})")
        return
    count = int(behind.stdout.strip())
    report.line(count == 0, "git-upstream", f"{upstream.stdout.strip()} as last fetched: "
                + ("up to date" if count == 0 else f"{count} commit(s) behind; pull first"))


def check_run(root: Path, report: Report, resume_session: str | None = None) -> None:
    guard = root / GUARD
    if not guard.exists():
        report.line(True, "campaign-guard", "no run guard held")
        return
    try:
        held = json.loads(guard.read_text(encoding="utf-8"))
        session, tag = str(held.get("session_id", "")), str(held.get("launch_tag", ""))
    except (OSError, ValueError, AttributeError) as exc:
        report.line(False, "campaign-guard", f"{GUARD} is held and cannot be read ({exc}); another run may be live")
        return
    # The run being resumed owns the guard by its agent session or, for an agent that chose its own id (Codex,
    # Antigravity), by the run's tag the guard recorded (D02 T02 §6).
    if resume_session and resume_session in (session, tag):
        report.line(True, "campaign-guard", f"{GUARD} is held by session {session}, the run being resumed")
    else:
        report.line(False, "campaign-guard", f"{GUARD} is held by session {session or 'unknown'}: another run is "
                                             "live (resume it with --resume-session, or end it)")


def check_claim(root: Path, report: Report, lib, resume_session: str | None = None, session_exists=None) -> None:
    """No other live launch claim (`build/unattended-launch.json`, through `lib.read_claim`): a stale claim passes
    with its reason, the resumed run's own live claim passes, and a claim whose liveness tmux cannot tell fails."""
    claim, live, reason = lib.read_claim(root, session_exists or lib.tmux_session_exists)
    if live is None:
        report.line(False, "launch-claim", reason)
    elif live and not (resume_session and claim.get("session") == resume_session):
        report.line(False, "launch-claim", f"{reason}: another run is live (resume it with --resume-session)")
    else:
        report.line(True, "launch-claim", reason + (", the run being resumed" if live else ""))


# The CLI versions whose flags the launcher's commands were built from (kits/unattended/README.md); older ones may lack
# a flag, so they fail the preflight (D02 T02 §6).
MIN_VERSIONS = {"claude": "2.1.322", "codex": "0.166.0", "agy": "1.5.5"}


def _version(text: str) -> tuple[int, ...] | None:
    found = re.search(r"\b(\d+(?:\.\d+)+)\b", text or "")
    return tuple(int(part) for part in found.group(1).split(".")) if found else None


def check_agent_version(profile: dict, report: Report) -> None:
    cli, minimum = AGENT_CLI[profile["agent"]], MIN_VERSIONS[profile["agent"]]
    try:
        done = subprocess.run([cli, "--version"], capture_output=True, text=True, timeout=30,
                              creationflags=windowless())
    except (OSError, subprocess.SubprocessError) as exc:
        report.line(False, f"version {cli}", f"'{cli} --version' could not run ({exc}); {minimum} or newer is needed")
        return
    said = (done.stdout or done.stderr).strip()
    found = _version(said) if done.returncode == 0 else None
    if found is None:
        report.line(False, f"version {cli}", f"'{cli} --version' gave no version ({_last_line(said) or 'no output'}, "
                                             f"exit {done.returncode}); {minimum} or newer is needed")
        return
    shown = ".".join(map(str, found))
    ok = found >= _version(minimum)
    report.line(ok, f"version {cli}", f"{shown}" + ("" if ok else f", older than {minimum}, whose flags the launcher "
                                                              "uses: update it"))


def check_tools(profile: dict, report: Report) -> None:
    names = ["git", "python3", "tmux", AGENT_CLI[profile["agent"]], *profile["tools"]]
    for name in dict.fromkeys(names):
        found = shutil.which(name)
        report.line(bool(found), f"tool {name}", found or "not on PATH")


def check_hook(root: Path, profile: dict, report: Report, self_test_seconds: int = SELF_TEST_SECONDS) -> None:
    guard_script = root / "scripts" / "campaign_guard.py"
    if not guard_script.is_file():
        report.line(False, "stop-hook", "scripts/campaign_guard.py is missing: install the todo kit")
        return
    try:
        guard = _load("project_campaign_guard", guard_script)
        markers = {f: m for f, _, m in guard.HOOK_FILES}
    except Exception as exc:  # the project's own script: any failure to load it is the finding, reported here
        report.line(False, "stop-hook", f"scripts/campaign_guard.py does not load ({type(exc).__name__}: {exc})")
        return
    hook_file = AGENT_HOOK_FILE[profile["agent"]]
    path = root / hook_file
    if not path.is_file():
        report.line(False, "stop-hook", f"{hook_file} is missing: install the todo kit for {profile['agent']}")
        return
    try:
        guard._campaign_command(str(path), hook_file, markers[hook_file])
        report.line(True, "stop-hook", f"{hook_file} runs the campaign Stop hook")
    except guard.GuardError as exc:
        report.line(False, "stop-hook", f"{hook_file}: {exc}")
        return
    try:
        test = subprocess.run([sys.executable, str(guard_script), "--self-test", "--hook-file", hook_file], cwd=root,
                              capture_output=True, text=True, timeout=self_test_seconds, creationflags=windowless())
        report.line(test.returncode == 0, "guard-self-test",
                    f"exit {test.returncode}: {_last_line(test.stdout) or _last_line(test.stderr) or 'no output'}")
    except subprocess.TimeoutExpired:
        report.line(False, "guard-self-test", f"ran past {self_test_seconds}s")
    except (OSError, subprocess.SubprocessError) as exc:
        report.line(False, "guard-self-test", f"could not run ({exc})")
    if profile["agent"] == "codex":
        check_codex_trust(root, report)


def check_codex_trust(root: Path, report: Report) -> None:
    """Codex runs a project hook only once it is approved; the todo kit's trust script approves it through Codex's
    own app-server when it is not (the same step `forge.py` takes at install), and this reports what it did."""
    script = root / "scripts" / "codex-hook-trust.py"
    if not script.is_file():
        report.line(False, "codex-hook-trust", "scripts/codex-hook-trust.py is missing: install the todo kit")
        return
    try:
        done = subprocess.run([sys.executable, str(script)], cwd=root, capture_output=True, text=True,
                              timeout=TRUST_SECONDS, creationflags=windowless())
    except subprocess.TimeoutExpired:
        report.line(False, "codex-hook-trust", f"ran past {TRUST_SECONDS}s")
        return
    except (OSError, subprocess.SubprocessError) as exc:
        report.line(False, "codex-hook-trust", f"could not run ({exc})")
        return
    said = _last_line(done.stdout) if done.returncode == 0 else (_last_line(done.stderr) or _last_line(done.stdout))
    report.line(done.returncode == 0, "codex-hook-trust", f"exit {done.returncode}: {said or 'no output'}")


def disk_free_gb(root: Path) -> float:
    return shutil.disk_usage(root).free / 1024 ** 3


def available_gb(meminfo: Path | None = None) -> float | None:
    try:
        for line in (meminfo or MEMINFO).read_text(encoding="ascii").splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) / (1024 * 1024)
    except (OSError, ValueError, IndexError):
        return None
    return None


def check_headroom(root: Path, profile: dict, report: Report) -> None:
    try:
        free = disk_free_gb(root)
    except OSError as exc:
        report.line(False, "disk", f"free space on {root} cannot be read ({exc})")
    else:
        report.line(free >= profile["min_disk_gb"], "disk", f"{free:.1f} GB free, {profile['min_disk_gb']} GB needed")
    memory = available_gb()
    if memory is None:
        report.line(False, "memory", f"MemAvailable cannot be read from {MEMINFO}")
    else:
        report.line(memory >= profile["min_ram_gb"], "memory",
                    f"{memory:.1f} GB available, {profile['min_ram_gb']} GB needed")


def _end_group(proc: subprocess.Popen) -> None:
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:  # every process of the group has already ended: there is nothing left to stop
        pass
    proc.wait()


def _keep_tail(pipe, tail: bytearray) -> None:
    """Read `pipe` to its end, keeping only its last TAIL_BYTES: a check that writes without end costs no more."""
    while True:
        chunk = pipe.read1(65536)
        if not chunk:
            break
        tail.extend(chunk)
        del tail[:-TAIL_BYTES]
    pipe.close()


def run_check(check: dict, root: Path) -> tuple[int, str]:
    """Run one profile check from the repository root, its arguments as listed and no shell, in its own process
    group, and end the whole group when it finishes, times out or is interrupted (a child left behind does not
    outlive it): (status, last output line). A timeout reads as 124, a program that cannot start as 127."""
    try:
        proc = subprocess.Popen(check["command"], cwd=root, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True, creationflags=windowless())
    except OSError as exc:
        return NOT_STARTED, f"could not start {check['command'][0]}: {exc.strerror or exc}"
    tail = bytearray()
    reader = threading.Thread(target=_keep_tail, args=(proc.stdout, tail), daemon=True)
    reader.start()
    try:
        code = proc.wait(timeout=check["timeout_seconds"])
    except subprocess.TimeoutExpired:
        _end_group(proc)
        code = TIMED_OUT
    except BaseException:
        _end_group(proc)
        raise
    else:
        _end_group(proc)
    reader.join(timeout=10)
    return code, _last_line(bytes(tail).decode("utf-8", "replace"))


def check_profile_commands(root: Path, profile: dict, report: Report) -> None:
    for check in profile["preflight"]:
        code, last = run_check(check, root)
        detail = f"exit {code}" + (f" after {check['timeout_seconds']}s" if code == TIMED_OUT else "")
        report.line(code == 0, f"check {check['name']}", f"{detail}: {last or 'no output'}")


def preflight(root: Path, out=None, system: str | None = None, resume_session: str | None = None) -> int:
    report = Report(out)
    if (system or os.name) == "nt":
        report.line(False, "system", "the unattended run needs tmux, which runs on Linux and in WSL: run it in WSL")
        return 1
    try:
        unattended = _load("unattended_lib", HERE / "lib" / "unattended.py")
        profile = unattended.load_profile(root)
    except Exception as exc:  # a ProfileError, or the module itself missing or broken: every check needs the profile
        report.line(False, "profile", str(exc) or type(exc).__name__)
        return 1
    report.line(True, "profile", f"{unattended.PROFILE} loads: {profile['name']}, {profile['agent']}")
    checks = (("git", lambda: check_git(root, profile, report, resuming=bool(resume_session))),
              ("campaign-guard", lambda: check_run(root, report, resume_session)),
              ("launch-claim", lambda: check_claim(root, report, unattended, resume_session)),
              ("tools", lambda: check_tools(profile, report)),
              ("version", lambda: check_agent_version(profile, report)),
              ("stop-hook", lambda: check_hook(root, profile, report)),
              ("headroom", lambda: check_headroom(root, profile, report)),
              ("profile checks", lambda: check_profile_commands(root, profile, report)))
    for name, run in checks:
        try:
            run()
        except Exception as exc:  # a check that cannot run is a FAIL with the reason; the others still run
            report.line(False, name, f"could not run ({type(exc).__name__}: {exc})")
    return 1 if report.failed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Refuse an unattended run that cannot work.")
    parser.add_argument("--root", default=str(HERE.parent), help="the repository (default: this script's)")
    parser.add_argument("--resume-session", help="the session of the run being resumed, whose guard is allowed")
    args = parser.parse_args(argv)
    return preflight(Path(args.root).resolve(), resume_session=args.resume_session)


if __name__ == "__main__":
    sys.exit(main())
