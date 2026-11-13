#!/usr/bin/env python3
"""Start, show, attach to and stop an unattended agent run held in tmux.

    python3 scripts/unattended-run.py start [--ships N] [--no-heartbeat]   take the launch lock, run the preflight,
                                                 start (or resume) the run, and its heartbeat unless --no-heartbeat;
                                                 --ships N ends the run once it has shipped N sections
    python3 scripts/unattended-run.py status     whether the run is live, its session, start, guard, health and stall
    python3 scripts/unattended-run.py attach [--watch]   attach to the run's tmux session (--watch: read-only)
    python3 scripts/unattended-run.py stop       end the guard, the tmux sessions and the whole process group
    python3 scripts/unattended-run.py heartbeat [--once]   nudge an idle run; record a dead one as ended (start runs it)

The run lives in the detached tmux session `<name>-run` (the profile's `name`), so it outlives the terminal that
started it. `build/unattended-run.json` (the run marker) names the run: its agent session, its tmux sessions and its
process group. A run never restarts itself (owner decision 2026-11-12): only the operator's `start` starts one, and
`stop`, a crash or a reboot ends it. The heartbeat nudges a live run and records a dead one as ended, stopping all it
started. An operator's `start` that finds a marker left without a heartbeat continues that agent session.
The heartbeat also counts the sections the run ships (rows turned `[x]`, commit by commit), writes
`build/unattended-health.json` every tick, ends a bounded run at its limit, and writes
`build/unattended-needs-operator.md` when nothing has shipped for `stall_hours`.
`status` and `stop` follow the marker, so a profile changed during a run cannot strand it. Linux and WSL only (tmux).
Standard library only.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
import time
import uuid

HERE = Path(__file__).resolve().parent
# Loading the kit's own modules must not write __pycache__ into the project: that dirties the tree the preflight
# checks, and a launch would refuse itself.
sys.dont_write_bytecode = True
MARKER = "build/unattended-run.json"
# `session` is the run's tag (CONCLAVE_UNATTENDED_TAG, the claim's session, and Claude Code's own --session-id);
# `agent_session` is the agent's own session id: the tag for Claude Code, and for Codex and Antigravity, which choose
# their own, the id the run's guard records (None until then).
# `start_commit` and `run_started` are the run's baseline (HEAD and the time of its fresh start, both kept by a resume,
# where `started` is the resume's own time) and `ship_limit` its bound (`start --ships N`, None when unbounded).
MARKER_KEYS = ("session", "agent_session", "tmux_session", "heartbeat_session", "agent", "pgid", "started", "resumed",
               "start_commit", "run_started", "ship_limit")
# Keys a marker written before them lacks, read as unknown: the agent session before D02 T02 §6, the baseline and the
# bound before D02 T02 §8 (such a run is unbounded, and the heartbeat's first look is its baseline).
LEGACY_KEYS = ("agent_session", "start_commit", "run_started", "ship_limit")
GUARD = "build/claude-campaign-guard.json"
HEALTH = "build/unattended-health.json"  # written by the heartbeat on every tick
NEEDS_OPERATOR = "build/unattended-needs-operator.md"  # the stall notice: nothing has shipped for stall_hours
HEARTBEAT_STATE = "build/unattended-heartbeat.json"
ENDED = "build/claude-campaign-ended.json"  # written by campaign_guard.py end: how the last run ended
FINISHED = 10  # the heartbeat's exit status when the run is over: stopped, ended by its agent, or dead
# What one attempt to end a run came to: over; not yet (kept for the next tick); or the marker now names the run the
# operator started or resumed since, which the heartbeat goes on supervising.
OVER, RETRY, REPLACED = "over", "retry", "replaced"
FALLBACK_TICK_SECONDS = 300  # the template's tick, used while the profile cannot be read mid-run
NUDGE_PROMPT = ("The unattended run looks idle. Continue with process-plan: pick up the open section in the run file "
                "and keep going. Never ask the operator a question.")
HISTORY_LIMIT = 50000
INSIDE = "CONCLAVE_UNATTENDED_RUN"  # set inside the run, so a run cannot stop itself
# Every process of a run carries this in its environment (inherited even by one that starts its own session, such as
# a reviewer the dispatcher detaches), so a stop or a resume finds and ends the whole run, not only its group.
TAG = "CONCLAVE_UNATTENDED_TAG"
# The preflight's own checks (git, the guard self-test, Codex trust), on top of the profile's checks' own timeouts.
PREFLIGHT_SLACK_SECONDS = 900
STOP_GRACE_SECONDS = 10
# How long a process or group sent SIGKILL is waited for: a process dies only once it leaves uninterruptible I/O, and
# a killed group leader stays in its group until tmux reaps it. (The resume refused on 2026-11-12 was the second: the
# group was not waited for at all; see end_run_processes.)
KILL_WAIT_SECONDS = 30
# How long `stop` tries to record the run's end (state, health, notice) itself before handing it to the heartbeat.
STOP_RECORD_SECONDS = 10
# The run `stop` ended, kept in the heartbeat's tmux session environment (memory, no disk): the heartbeat records
# that end when `stop` could not, and keeps trying until the disk takes it.
STOPPED = "CONCLAVE_UNATTENDED_STOPPED"
STOP_REASON = "stopped by the operator"
# How long a started or resumed agent must stay up to count as started.
STARTUP_SECONDS = float(os.environ.get("CONCLAVE_STARTUP_GRACE") or 10)
CLAUDE_DISALLOWED = "AskUserQuestion,EnterPlanMode,ExitPlanMode"
RESUME_PROMPT = ("Resume the unattended run where it stopped: run process-plan, pick up the open section in the run "
                 "file, and keep going. Never ask the operator a question.")


class RunError(RuntimeError):
    """A launcher step that cannot go ahead, said in one line."""


class UsageError(RunError):
    """A start the operator asked for in a form that cannot be run (exit 2), said with what to do instead."""


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


lib = _load("unattended_lib", HERE / "lib" / "unattended.py")


def tmux(*args: str, check: bool = False) -> subprocess.CompletedProcess:
    try:
        done = subprocess.run(["tmux", *args], capture_output=True, text=True, timeout=30,
                              creationflags=lib.windowless())
    except (OSError, subprocess.SubprocessError) as exc:
        if check:
            raise RunError(f"tmux {args[0]} could not run: {exc}") from None
        return subprocess.CompletedProcess(["tmux", *args], -1, stdout="", stderr=str(exc))
    if check and done.returncode != 0:
        raise RunError(f"tmux {args[0]} failed: {(done.stderr or done.stdout).strip()[-300:]}")
    return done


def names(profile: dict) -> tuple[str, str]:
    """The run's tmux session and the heartbeat's (D02 T02 §7), from the profile."""
    return f"{profile['name']}-run", f"{profile['name']}-heartbeat"


def read_json(path: Path) -> dict | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return data if isinstance(data, dict) else None


def read_marker(root: Path) -> tuple[dict | None, str | None]:
    """(marker, problem): the run marker checked against its schema; no marker is (None, None), and one that cannot
    be read or does not hold the schema is (None, why)."""
    path = root / MARKER
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return None, None
    except (OSError, ValueError) as exc:
        return None, f"{MARKER} cannot be read ({exc})"
    if isinstance(data, dict) and set(MARKER_KEYS) - set(LEGACY_KEYS) <= set(data) <= set(MARKER_KEYS):
        if "agent_session" not in data:
            # Before D02 T02 §6 the tag was Claude Code's session and the only agent was Claude.
            data = {**data, "agent_session": data.get("session") if data.get("agent") == "claude" else None}
        data = {**{key: None for key in LEGACY_KEYS}, **data}
    if not isinstance(data, dict) or set(data) != set(MARKER_KEYS):
        return None, f"{MARKER} does not hold {', '.join(MARKER_KEYS)}"
    if data["start_commit"] is not None and not (isinstance(data["start_commit"], str)
                                                 and re.fullmatch(r"[0-9a-f]{40}", data["start_commit"])):
        return None, f"{MARKER} holds a start commit that is not a commit id"
    if data["run_started"] is not None and not isinstance(data["run_started"], str):
        return None, f"{MARKER} holds a run start that is not a time"
    limit = data["ship_limit"]
    if limit is not None and (isinstance(limit, bool) or not isinstance(limit, int) or limit <= 0):
        return None, f"{MARKER} holds a ship limit that is not a whole number above 0"
    if not all(isinstance(data[k], str) and data[k] for k in ("session", "tmux_session", "heartbeat_session")):
        return None, f"{MARKER} names no session"
    if not isinstance(data["pgid"], int) or isinstance(data["pgid"], bool) or data["pgid"] <= 1:
        return None, f"{MARKER} holds no process group"
    if data["agent_session"] is not None and not (isinstance(data["agent_session"], str) and data["agent_session"]):
        return None, f"{MARKER} holds an agent session that is not a session id"
    return data, None


def owned_guard(root: Path, marker: dict | None) -> dict | None:
    """The guard when it is this run's: it carries the run's tag, or names the run's agent session."""
    guard = read_json(root / GUARD)
    if not guard or not marker:
        return None
    if guard.get("launch_tag") == marker["session"]:
        return guard
    if marker.get("agent_session") and str(guard.get("session_id")) == marker["agent_session"]:
        return guard
    return None


def agent_command(root: Path, profile: dict, tag: str, resume_id: str | None) -> list[str]:
    """The agent's command line, built from the profile and the flags each installed CLI's own help confirms
    (kits/unattended/README.md). A fresh start gets the seed prompt; a resume (`resume_id`, the agent's own session)
    gets a short resume prompt."""
    agent, model, effort, skip = profile["agent"], profile["model"], profile["effort"], profile["skip_permissions"]
    prompt = RESUME_PROMPT if resume_id else (root / profile["prompt_file"]).read_text(encoding="utf-8")
    if agent == "claude":
        # --disallowedTools takes a list, which would swallow the prompt: the options end with `--`.
        argv = ["claude", "--model", model, "--effort", effort]
        argv += ["--resume", resume_id] if resume_id else ["--session-id", tag]
        argv += ["--disallowedTools", CLAUDE_DISALLOWED]
        if skip:
            argv.append("--dangerously-skip-permissions")
        return argv + ["--", prompt]
    if agent == "codex":
        # Codex has no --effort: -c overrides config.toml's model_reasoning_effort (a TOML string). It chooses its own
        # session id, so a fresh start names none; `codex resume [SESSION_ID] [PROMPT]` continues one.
        options = ["-m", model, "-c", f'model_reasoning_effort="{effort}"']
        if skip:
            options.append("--dangerously-bypass-approvals-and-sandbox")
        if resume_id:
            return ["codex", "resume", *options, "--", resume_id, prompt]
        return ["codex", *options, "--", prompt]
    if agent == "agy":
        # Antigravity's flags take `--name=value`; --prompt-interactive runs the prompt and keeps the session open.
        argv = ["agy", f"--model={model}", f"--effort={effort}"]
        if resume_id:
            argv.append(f"--conversation={resume_id}")
        if skip:
            argv.append("--dangerously-skip-permissions")
        return argv + [f"--prompt-interactive={prompt}"]
    raise RunError(f"agent {agent!r} has no launch command")


def run_preflight(root: Path, resume_session: str | None, profile: dict) -> tuple[int, str]:
    """Run the preflight in its own process group, given as long as its profile checks may take plus its own checks,
    and end the whole group when it overruns or the launcher is interrupted."""
    args = [sys.executable, str(HERE / "unattended-preflight.py"), "--root", str(root)]
    if resume_session:
        args += ["--resume-session", resume_session]
    limit = PREFLIGHT_SLACK_SECONDS + sum(check["timeout_seconds"] for check in profile["preflight"])
    # Its profile checks run in sessions of their own, outside its group: the tag reaches them all.
    tag = f"preflight-{uuid.uuid4()}"
    proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            start_new_session=True, env={**os.environ, TAG: tag}, creationflags=lib.windowless())
    try:
        report, _ = proc.communicate(timeout=limit)
    except subprocess.TimeoutExpired:
        end_run_processes(tag, proc.pid, grace=0)
        report, _ = proc.communicate()
        return 1, (report or "").rstrip() + f"\nFAIL preflight: ran past {limit}s; it and its checks were ended"
    except BaseException:
        end_run_processes(tag, proc.pid, grace=0)
        proc.communicate()
        raise
    return proc.returncode, (report or "").rstrip()


def _alive(name: str) -> bool | None:
    return lib.tmux_session_exists(name)


def _group_alive(pgid: int) -> bool:
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def tagged(session: str) -> list[int]:
    """The processes whose environment carries this run's tag, read from /proc (empty where /proc cannot say)."""
    needle = f"{TAG}={session}".encode()
    found = []
    try:
        entries = os.listdir("/proc")
    except OSError:
        return found
    for entry in entries:
        if not entry.isdigit() or int(entry) == os.getpid():
            continue
        try:
            with open(f"/proc/{entry}/environ", "rb") as handle:
                if needle in handle.read().split(b"\0"):
                    found.append(int(entry))
        except OSError:
            continue  # it ended while being read, or is not this user's
    return found


def describe(pids: list[int]) -> str:
    """Each pid with its command, kernel state and parent, read from /proc, so a process that outlives SIGKILL is
    named, not only numbered (a CI runner showed one, 2026-11-13)."""
    parts = []
    for pid in pids:
        try:
            command = Path(f"/proc/{pid}/cmdline").read_bytes().replace(b"\0", b" ").decode("utf-8", "replace")
            fields = dict(line.split(":", 1) for line in Path(f"/proc/{pid}/status").read_text().splitlines()
                          if ":" in line)
            parts.append(f"{pid} ({command.strip()[:120] or '?'}; state {fields.get('State', '?').strip()}; "
                         f"parent {fields.get('PPid', '?').strip()})")
        except OSError:
            parts.append(f"{pid} (ended since)")
    return ", ".join(parts)


def end_run_processes(session: str, pgid: int | None, grace: float = STOP_GRACE_SECONDS) -> list[int]:
    """End the run's process group and every process tagged with its session: wait up to `grace` for them to go
    (tmux has already sent its hangup), then SIGKILL what is left. Returns the pids still alive afterwards."""
    deadline = time.monotonic() + grace
    while (tagged(session) or (pgid and _group_alive(pgid))) and time.monotonic() < deadline:
        time.sleep(0.2)
    if pgid and _group_alive(pgid):
        try:
            os.killpg(pgid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # the group ended at the last moment
    for pid in tagged(session):
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # it ended at the last moment
    # SIGKILL lands only once a process leaves uninterruptible I/O (slow under load, as on WSL's /mnt), so the wait
    # after it is KILL_WAIT_SECONDS whatever the polite wait was; a killed process cannot do more work meanwhile.
    deadline = time.monotonic() + max(grace, KILL_WAIT_SECONDS)
    # The group too: a killed leader stays in it until its parent (tmux) reaps it, a moment after the kill; read as
    # alive at once, it made a clean stop or resume report a survivor (CI and WSL, 2026-11-13).
    while (tagged(session) or (pgid and _group_alive(pgid))) and time.monotonic() < deadline:
        time.sleep(0.2)
    return tagged(session) + ([pgid] if pgid and _group_alive(pgid) else [])


def _launch(root: Path, run_session: str, argv: list[str], session: str) -> int:
    """Start `argv` as the only window of the detached tmux session `run_session`, with a 50,000-line history (tmux
    applies history-limit to new windows only, so a placeholder window is replaced), inside the repository and with
    the inside-the-run variable set, and see that it stays up for the startup check. Returns the agent's pid, which
    leads its own process group. On any failure the session and the group are ended and RunError says why."""
    tmux("new-session", "-d", "-s", run_session, "-n", "boot", "-c", str(root), "sleep 30", check=True)
    pid = None
    try:
        # A session target ending in ":" (the exact name, then its current window): set-option and new-window
        # resolve -t through a pane, and a bare "=name" names no pane in tmux 3.6.
        tmux("set-option", "-t", f"={run_session}:", "history-limit", str(HISTORY_LIMIT), check=True)
        # The agent's window opens on a placeholder, keeps its pane on exit, and only then is the agent respawned into
        # it: an agent that exits at once is still there to read, not a window that vanished before the option was set.
        tmux("new-window", "-d", "-t", f"={run_session}:", "-n", "agent", "-c", str(root), "sleep 30", check=True)
        target = f"={run_session}:agent"
        tmux("set-option", "-w", "-t", target, "remain-on-exit", "on", check=True)  # for the startup check only
        tmux("respawn-pane", "-k", "-t", target, "-c", str(root), "-e", f"{INSIDE}=1", "-e", f"{TAG}={session}",
             "exec " + shlex.join(argv), check=True)
        tmux("kill-window", "-t", f"={run_session}:boot", check=True)
        pid = int(tmux("display-message", "-p", "-t", target, "#{pane_pid}", check=True).stdout.strip())
        deadline = time.monotonic() + STARTUP_SECONDS
        while time.monotonic() < deadline:
            dead = tmux("display-message", "-p", "-t", target, "#{pane_dead} #{pane_dead_status}").stdout.split()
            if dead and dead[0] == "1":
                # The agent's own last words, from the pane's history (a dead pane's screen holds only tmux's own
                # `Pane is dead (...)` line, which is left out).
                history = tmux("capture-pane", "-p", "-J", "-S", "-200", "-t", target).stdout
                shown = [line for line in history.strip().splitlines()
                         if line.strip() and not line.startswith("Pane is dead")]
                last = shown[-1][-200:] if shown else "no output"
                raise RunError(f"the agent exited {dead[1] if len(dead) > 1 else '?'} within "
                               f"{STARTUP_SECONDS:g}s of starting: {last}")
            time.sleep(0.2)
        tmux("set-option", "-w", "-t", target, "remain-on-exit", "off", check=True)
        return pid
    except BaseException:
        tmux("kill-session", "-t", f"={run_session}")
        end_run_processes(session, pid, grace=2)
        raise


def start(root: Path, out=None, supervise: bool = True, ships: int | None = None) -> int:
    """Start the run, as the operator asked. `supervise` launches the heartbeat in its own tmux session when it is not
    running. `ships` bounds a fresh run to that many sections shipped; a resume keeps the run's own bound. Nothing
    calls this but the operator: no heartbeat, unit or task restarts a run."""
    out = out or sys.stdout
    profile = lib.load_profile(root)
    run_session, heartbeat = names(profile)
    with lib.launch_lock(root):
        marker, problem = read_marker(root)
        if problem:
            raise RunError(f"{problem}: stop the run (or remove {MARKER}) before starting another")
        if marker:
            run_session, heartbeat = marker["tmux_session"], marker["heartbeat_session"]
        live = _alive(run_session)
        if live is None:
            raise RunError(f"whether tmux session {run_session} is running cannot be told: tmux could not be asked")
        if live:
            raise RunError(f"tmux session {run_session} is already running: attach with "
                           "'python3 scripts/unattended-run.py attach', or stop it first")
        old = marker
        # The bound belongs to the run: a resume keeps it, and only the heartbeat enforces it (run.sh:91).
        kept = old["ship_limit"] if old else None
        if old and ships is not None and ships != kept:
            own = f"is bounded to {kept} section(s)" if kept else "has no ship limit"
            raise UsageError(f"run {old['session']} {own}; a resume keeps the run's own bound: stop the run to start "
                             "one with another")
        limit = kept if old else ships
        if limit is not None and not supervise:
            raise UsageError(f"a run bounded to {limit} section(s) needs its heartbeat, which enforces the bound: "
                             "start it without --no-heartbeat")
        if marker:
            # The tmux session is gone, but that does not prove its processes are: a crashed run's leftovers are
            # ended before anything starts in its place.
            left = end_run_processes(marker["session"], marker["pgid"], grace=2)
            if left:
                raise RunError(f"the previous run's processes {describe(left)} are still running after SIGKILL")
        # The agent session to resume: Claude Code's is the tag; Codex's and Antigravity's is what the run's guard
        # records, and without one (the agent died before it took the guard) the run starts fresh.
        guard = owned_guard(root, old)
        resume = (old["agent_session"] or (str(guard["session_id"]) if guard else None)) if old else None
        if old and old["agent"] != profile["agent"]:
            # The profile now names another agent: a session of one cannot be resumed by the other.
            print(f"start: the crashed run was {old['agent']}, the profile now names {profile['agent']}; its "
                  f"session is not resumed, and a fresh {profile['agent']} session starts", file=out)
            resume = None
        code, report = run_preflight(root, old["session"] if old else None, profile)
        print(report, file=out)
        if code != 0:
            print("start: the preflight refused the run (FAIL lines above); nothing was started", file=out)
            return 1
        started = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        try:
            head = lib.head_commit(root)
        except lib.ShipCountError as exc:
            raise RunError(f"the run's start commit cannot be read ({exc})") from None
        session = agent_session = pid = None
        if old and not resume:
            _end_old_guard(root, old, out)  # nothing to resume: the crashed run's guard is ended before a fresh start
        if resume:
            try:
                pid = _launch(root, run_session, agent_command(root, profile, old["session"], resume), old["session"])
                session, agent_session = old["session"], resume
            except RunError as exc:
                print(f"start: the resume of session {resume} did not stay up ({exc}); starting a fresh session",
                      file=out)
                _end_old_guard(root, old, out)
        if session is None:
            session = str(uuid.uuid4())
            agent_session = session if profile["agent"] == "claude" else None
            pid = _launch(root, run_session, agent_command(root, profile, session, None), session)
        resumed = session == (old["session"] if old else None)
        marker = {"session": session, "agent_session": agent_session, "tmux_session": run_session,
                  "heartbeat_session": heartbeat, "agent": profile["agent"], "pgid": pid, "started": started,
                  "resumed": resumed,
                  # A resume keeps the run's baseline (a run marked before D02 T02 §8 keeps having none); a fresh
                  # session, a fallback included, starts its own.
                  "start_commit": old["start_commit"] if resumed else head,
                  "run_started": old["run_started"] if resumed else started, "ship_limit": limit}
        try:
            lib.write_atomic(root / MARKER, json.dumps(marker, indent=2) + "\n")
            lib.write_claim(root, session, pid, profile["agent"], run_session, started=started)
            if read_marker(root)[0] != marker:
                raise OSError(f"{MARKER} did not read back as written")
        except OSError as exc:
            tmux("kill-session", "-t", f"={run_session}")
            end_run_processes(session, pid, grace=2)
            (root / MARKER).unlink(missing_ok=True)
            lib.clear_claim(root, session, lock_held=True)
            raise RunError(f"the run's state could not be written ({exc}); the run was ended") from None
        if not resumed:
            _clear_stall_notice(root, out, reason="a fresh run started")
    print(f"start: {'resumed' if marker['resumed'] else 'started'} session {session} in tmux session "
          f"{run_session} (attach: python3 scripts/unattended-run.py attach)", file=out)
    if supervise:
        _launch_heartbeat(root, heartbeat, out)
    return 0


def _launch_heartbeat(root: Path, heartbeat: str, out, stopped: str | None = None) -> None:
    """The heartbeat runs in its own detached tmux session, one per run: a second start leaves a live one alone.
    `stopped` names a run `stop` ended without recording it, for the heartbeat to record."""
    if _alive(heartbeat) is not False:
        return
    command = shlex.join([sys.executable, str(HERE / "unattended-run.py"), "--root", str(root), "heartbeat"])
    extra = ["-e", f"{STOPPED}={stopped}"] if stopped else []
    tmux("new-session", "-d", "-s", heartbeat, *extra, "-c", str(root), "exec " + command, check=True)
    print(f"{'stop' if stopped else 'start'}: heartbeat running in tmux session {heartbeat}", file=out)


def handoff_pending(root: Path) -> str | None:
    """The run `stop` handed its unrecorded end to this heartbeat, while its state does not yet hold that end."""
    stopped = stopped_run()
    last = read_json(root / HEARTBEAT_STATE) or {}
    if not stopped or (last.get("session") == stopped and (last.get("ended") or {}).get("reason") == STOP_REASON):
        return None
    return stopped


def stopped_run() -> str | None:
    """The run `stop` ended and left for this heartbeat to record, from its own tmux session's environment."""
    if not os.environ.get("TMUX"):
        return None
    shown = tmux("show-environment", STOPPED)
    value = shown.stdout.strip().partition("=")[2] if shown.returncode == 0 else ""
    return value or None


def read_state(root: Path, tag: str | None) -> dict:
    """The heartbeat's state for the run tagged `tag`; a state left by another run is not carried over."""
    state = read_json(root / HEARTBEAT_STATE) or {}
    if state.get("session") != tag:
        state = {}
    return {"session": tag, **{key: state.get(key) for key in STATE_KEYS}}


# The heartbeat's state for its run: the nudge throttle and the last error (§7); the one ship count the health file,
# the limit and the stall clock all read (`base`, the commit it counts from; `ships`; `counted`, the last commit
# counted; `last_ship`), the run's start and bound as last seen, the stall notice's throttle and whether it was alive.
STATE_KEYS = ("last_nudge", "last_error", "ended", "base", "ships", "counted", "last_ship", "run_started",
              "ship_limit", "stall_notice_at", "alive")
STALL_PROMPT = ("Nothing has shipped for hours. Before more section work, find out why from git log, the gate log and "
                "the test artifacts. Fix the cause in the layer that owns it, or record it and move to another "
                "section. Never ask the operator a question.")


def _iso(epoch: float) -> str:
    return datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _epoch(stamp: str | None) -> float | None:
    try:
        return datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc).timestamp() if stamp \
            else None
    except (TypeError, ValueError):
        return None


def count_ships(root: Path, marker: dict, state: dict, now: float) -> None:
    """Advance the run's one ship count to HEAD, counting only the commits after the last one counted. A run marked
    before D02 T02 §8 has no start commit: the heartbeat's first look (HEAD then, and the marker's `started`) is its
    baseline. The count starts again from the baseline when the state is missing or history was rewritten under it."""
    head = lib.head_commit(root)
    if marker["start_commit"]:
        base, run_started = marker["start_commit"], marker["run_started"]
    else:
        base = state.get("base") or head
        run_started = state.get("run_started") or marker["started"]
    state["run_started"], state["ship_limit"] = run_started, marker["ship_limit"]
    if state.get("base") != base or not state.get("counted") or not lib.is_ancestor(root, state["counted"], head):
        state.update(base=base, ships=0, counted=base, last_ship=None)
    if state["counted"] == head:
        return
    for sha, when, shipped in lib.ships_along(root, state["counted"], head):
        state["ships"] += shipped
        state["last_ship"] = {"commit": sha, "at": _iso(when)}
    state["counted"] = head


def health_of(state: dict, now: float) -> dict:
    """The health file's contents, from the run's state alone, so the ended run's last write needs no marker."""
    since = _epoch((state.get("last_ship") or {}).get("at")) or _epoch(state.get("run_started"))
    return {"alive": bool(state.get("alive")), "run": state.get("session"), "ships": state.get("ships") or 0,
            "ship_limit": state.get("ship_limit"), "last_ship": state.get("last_ship"),
            "hours_since_ship": round((now - since) / 3600, 2) if since is not None else None,
            "last_nudge": _iso(state["last_nudge"]) if state.get("last_nudge") else None, "updated": _iso(now)}


def _stall(root: Path, state: dict, profile: dict, now: float, out) -> bool:
    """Whether the run has shipped nothing for stall_hours. A stalled run gets the stall notice, at most once per
    stall_hours (its throttle in the run's own state, moved only by a notice written); a run that is not stalled has
    its own notice removed. A write or removal that fails is recorded and tried again next tick."""
    since = _epoch((state.get("last_ship") or {}).get("at")) or _epoch(state.get("run_started"))
    if since is None:
        return False
    hours = (now - since) / 3600
    limit = profile["stall_hours"]
    if hours < limit:
        notice = root / NEEDS_OPERATOR
        try:
            if notice.exists() and f"Run: {state['session']}\n" in notice.read_text(encoding="utf-8"):
                notice.unlink()
                print(f"heartbeat: run {state['session']} shipped; the stall notice is removed", file=out)
            state["stall_notice_at"] = None
        except OSError as exc:
            state["last_error"] = f"the stall notice could not be removed ({exc}); tried again next tick"
        return False
    if state.get("stall_notice_at") and now - state["stall_notice_at"] < limit * 3600:
        return True
    last = state.get("last_ship")
    shipped = f"the last ship was {last['commit']} at {last['at']}" if last else \
        f"nothing has shipped since the run started at {state.get('run_started')}"
    text = (f"# Nothing has shipped for {hours:.1f} hours\n\nRun: {state['session']}\n\n"
            "The unattended run is alive, but "
            f"no section has shipped for {hours:.1f} hours (stall_hours is {limit}): {shipped}. Read the run file, "
            "git log and the gate logs, then attach (`python3 scripts/unattended-run.py attach`) or stop the run.\n")
    try:
        lib.write_atomic(root / NEEDS_OPERATOR, text)
        state["stall_notice_at"] = now
        print(f"heartbeat: nothing has shipped for {hours:.1f} hours; {NEEDS_OPERATOR} written", file=out)
    except OSError as exc:
        state["last_error"] = f"the stall notice could not be written ({exc}); tried again next tick"
        print(f"heartbeat: {state['last_error']}", file=out)
    return True


def _clear_stall_notice(root: Path, out, reason: str) -> None:
    notice = root / NEEDS_OPERATOR
    try:
        if notice.exists():
            notice.unlink()
            print(f"start: the previous run's stall notice is removed ({reason})", file=out)
    except OSError as exc:
        print(f"start: the previous run's stall notice could not be removed ({exc})", file=out)


def run_finished(root: Path, marker: dict) -> bool:
    """Whether the run's own agent ended it: the guard's end record names the run's tag or its agent session. A
    guard that is merely absent proves nothing (the agent may not have taken it yet)."""
    ended = read_json(root / ENDED)
    if not ended:
        return False
    return ended.get("launch_tag") == marker["session"] or bool(
        marker.get("agent_session") and ended.get("session_id") == marker["agent_session"])


def _nudge(root: Path, run_session: str, profile: dict, state: dict, now: float, out,
           prompt: str = NUDGE_PROMPT) -> None:
    """Prompt a run whose window has shown nothing for idle_minutes, at most once per idle_minutes, unless an
    operator is attached with a client that can type (a read-only watcher does not count). A nudge counts only when
    tmux answered the client query and accepted the prompt. A stalled run gets the stall prompt (§8)."""
    idle = profile["idle_minutes"] * 60
    target = f"={run_session}:agent"
    shown = tmux("display-message", "-p", "-t", target, "#{window_activity}")
    activity = shown.stdout.strip()
    if shown.returncode != 0 or not activity.isdigit():
        state["last_error"] = f"tmux could not say when {run_session} was last active"
        return
    if now - int(activity) < idle or (state.get("last_nudge") and now - state["last_nudge"] < idle):
        return
    clients = tmux("list-clients", "-t", f"={run_session}", "-F", "#{client_readonly}")
    if clients.returncode != 0:
        state["last_error"] = f"tmux could not list the clients of {run_session}; no nudge sent"
        return
    if "0" in clients.stdout.split():
        return  # an operator is attached and may be typing
    typed = tmux("send-keys", "-t", target, "-l", prompt)
    entered = tmux("send-keys", "-t", target, "Enter") if typed.returncode == 0 else typed
    if entered.returncode != 0:
        state["last_error"] = f"tmux did not take the nudge for {run_session}: {(entered.stderr or '').strip()}"
        return
    state["last_nudge"], state["last_error"] = now, None
    print(f"heartbeat: nudged {run_session} after {int(now - int(activity))}s without activity", file=out)
    note_resumption(root, out)


def note_resumption(root: Path, out) -> None:
    """A nudge is a heartbeat resumption: the guard's stall breaker counts it (`heartbeat-still`), and ends the run
    as a stall after enough of them with no change to the tree (process-plan's heartbeat contract)."""
    marker, _ = read_marker(root)
    guard = owned_guard(root, marker)
    if not guard:
        return
    done = subprocess.run([sys.executable, str(HERE / "campaign_guard.py"), "heartbeat-still", "--session",
                           str(guard.get("session_id")), "--root", str(root)], capture_output=True, text=True,
                          timeout=120, creationflags=lib.windowless())
    said = (done.stdout or done.stderr).strip().splitlines()
    print(f"heartbeat: {said[-1][-200:] if said else f'heartbeat-still exited {done.returncode}'}", file=out)


def end_run(root: Path, observed: dict, reason: str, state: dict, now: float, out, gone: bool = False) -> str:
    """Record the run `observed` as ended, under the launch lock and only while the marker still is `observed` (its
    whole record: a resume keeps the tag but starts a new process group, so the tag alone cannot tell the runs apart),
    and, for a run seen dead (`gone`), only while tmux still says its session is gone. Stop its tmux session, every
    process tagged with it and its group, end its own guard, then clear the run's claim and only then its marker.
    OVER when the run is over; REPLACED when the marker names the operator's newer run, which is left alone; RETRY
    when any step failed or the lock is held, keeping the marker for the next tick to try again."""
    try:
        with lib.launch_lock(root):
            marker, _ = read_marker(root)
            if marker != observed:
                print(f"heartbeat: run {observed['session']} (process group {observed['pgid']}) is no longer the "
                      "marked run; the operator's current run is not touched", file=out)
                return REPLACED
            if gone and _alive(marker["tmux_session"]) is not False:
                state["last_error"] = (f"tmux session {marker['tmux_session']} is not confirmed gone under the launch "
                                       "lock; nothing ended, looked at again next tick")
                print(f"heartbeat: {state['last_error']}", file=out)
                return RETRY
            tmux("kill-session", "-t", f"={marker['tmux_session']}")
            left = end_run_processes(marker["session"], marker["pgid"])
            guard = owned_guard(root, marker)
            failed = None
            if guard:
                done = _end_guard(root, str(guard.get("session_id")))
                if done.returncode != 0:
                    failed = f"its guard did not end ({(done.stdout or done.stderr).strip()[-200:]})"
            if left:
                failed = f"processes {describe(left)} outlived SIGKILL" + (f"; {failed}" if failed else "")
            if failed:
                raise RunError(failed)
            # The claim before the marker: while the marker stays, the next tick finds the run and finishes this.
            lib.clear_claim(root, marker["session"], lock_held=True)
            (root / MARKER).unlink(missing_ok=True)
    except (RunError, lib.LaunchError, OSError, subprocess.SubprocessError) as exc:
        why = str(exc) if isinstance(exc, RunError) else f"{type(exc).__name__}: {exc}"
        state["last_error"] = f"run {observed['session']} could not be ended: {why}; tried again next tick"
        print(f"heartbeat: {state['last_error']}", file=out)
        return RETRY
    state["ended"] = {"reason": reason, "at": datetime.fromtimestamp(now, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    state["last_error"] = None
    print(f"heartbeat: run {observed['session']} ended ({reason}); nothing restarts it, and the heartbeat stops",
          file=out)
    return OVER


def tick(root: Path, now: float, state: dict, out=None) -> int:
    """One heartbeat tick: FINISHED when the run is over, 0 otherwise. `state` is updated in place. A dead run is
    recorded as ended, never restarted (owner decision 2026-11-12)."""
    out = out or sys.stdout
    profile = lib.load_profile(root)
    marker, problem = read_marker(root)
    if problem:
        # A marker that is there but cannot be read (a failing disk, a write in flight) names a run this tick cannot
        # identify: nothing is nudged or ended, and the next tick looks again (completion review of D02 T02 §7).
        state["last_error"] = f"{problem}; the run is not touched, looked at again next tick"
        print(f"heartbeat: {state['last_error']}", file=out)
        return 0
    if marker is None:
        print("heartbeat: no run marker; the heartbeat stops", file=out)
        return FINISHED
    try:
        count_ships(root, marker, state, now)
    except lib.ShipCountError as exc:  # the count waits for the next tick; supervision does not
        state["last_error"] = f"ships could not be counted ({exc}); counted again next tick"
        print(f"heartbeat: {state['last_error']}", file=out)
    if run_finished(root, marker):
        return _ended(root, marker, "the run ended itself", state, now, out)
    if marker["ship_limit"] and (state.get("ships") or 0) >= marker["ship_limit"]:
        print(f"heartbeat: run {marker['session']} shipped {state['ships']} of its {marker['ship_limit']} section(s)",
              file=out)
        return _ended(root, marker, "the ship limit was reached", state, now, out)
    live = _alive(marker["tmux_session"])
    if live is None:
        state["last_error"] = "tmux could not be asked whether the run is alive"
        return 0
    if not live:
        reason = STOP_REASON if stopped_run() == marker["session"] else "its session is gone"
        return _ended(root, marker, reason, state, now, out, gone=True)
    state["alive"] = True
    stalled = _stall(root, state, profile, now, out)
    _nudge(root, marker["tmux_session"], profile, state, now, out, STALL_PROMPT if stalled else NUDGE_PROMPT)
    return 0


def _ended(root: Path, marker: dict, reason: str, state: dict, now: float, out, gone: bool = False) -> int:
    """FINISHED once the run is ended (its last health then says it is not alive, and its stall notice, which says it
    is alive, goes), 0 while the cleanup waits."""
    if end_run(root, marker, reason, state, now, out, gone=gone) != OVER:
        return 0
    state["alive"] = False
    remove_own_notice(root, marker["session"], out)
    return FINISHED


def remove_own_notice(root: Path, tag: str | None, out, strict: bool = False) -> None:
    """Remove the stall notice when it is this run's: an ended run is no longer the live run it describes. A notice
    of another run is left alone. A removal that fails is said, and with `strict` raised, so the caller keeps the
    run's end pending and tries again."""
    notice = root / NEEDS_OPERATOR
    try:
        if tag and notice.exists() and f"Run: {tag}\n" in notice.read_text(encoding="utf-8"):
            notice.unlink()
            print(f"unattended-run: run {tag} ended; its stall notice is removed", file=out)
    except OSError as exc:
        print(f"unattended-run: the stall notice of run {tag} could not be removed ({exc})", file=out)
        if strict:
            raise


def record_stop(root: Path, tag: str, now: float, out) -> None:
    """The operator's stop, recorded where the heartbeat would have: the run's state says it ended, its health says it
    is not alive and its stall notice goes (the heartbeat was stopped first, so it cannot). Raises OSError when any of
    it fails, so `stop` keeps the run's marker and a second stop records it."""
    state = read_state(root, tag)
    state.update(ended={"reason": STOP_REASON, "at": _iso(now)}, alive=False)
    # The state last: its end record says the whole record landed, which is what a heartbeat left to finish it reads.
    lib.write_atomic(root / HEALTH, json.dumps(health_of(state, now), indent=2) + "\n")
    remove_own_notice(root, tag, out, strict=True)
    lib.write_atomic(root / HEARTBEAT_STATE, json.dumps(state, indent=2) + "\n")


def heartbeat(root: Path, once: bool = False, clock=time.time, sleep=time.sleep, out=None) -> int:
    """Tick every tick_seconds until the run is over. The state is read back from its file on every tick (a heartbeat
    started again carries it on) and kept in memory while a write fails; it writes that file, the health file and the
    stall notice, and nothing else. A tick
    that fails (tmux, the guard, the disk, a profile edited mid-run) is recorded and tried again: supervision of a live
    run never ends on an error. `--once` exits 1 on such a failure. A run's ended record whose write fails is kept and
    written again every tick until it lands (under `--once` too), and the heartbeat stops only then and only once the
    marker reads as absent; a run the operator starts meanwhile is supervised instead. With no run to supervise, the
    heartbeat writes nothing, so the last run's record stays as it was."""
    out = out or sys.stdout
    unsaved, pending, seconds, tag = None, False, FALLBACK_TICK_SECONDS, None
    while True:
        marker, problem = read_marker(root)
        if pending and marker is not None:
            print(f"heartbeat: run {unsaved['session']} ended ({unsaved['ended']['reason']}) and its record could not "
                  "be written; the operator's new run is supervised now", file=out)
            unsaved, pending = None, False
        if marker is None and not problem and not pending:
            stopped = handoff_pending(root)
            if not stopped:
                print("heartbeat: no run marker; the heartbeat stops", file=out)
                return FINISHED
            # `stop` ended this run but could not record it: the heartbeat does, until it lands.
            unsaved, pending, tag = read_state(root, stopped), True, stopped
            unsaved.update(ended={"reason": STOP_REASON, "at": _iso(clock())}, alive=False)
            print(f"heartbeat: run {stopped} was stopped by the operator; recording its end", file=out)
        written = None
        if pending:
            # The run is over and its record is still in memory: only the write is left. While the marker cannot be
            # read, a run the operator started may be behind it, so the heartbeat does not stop yet.
            code = 0 if problem else FINISHED
        else:
            # An unreadable marker keeps the run's own state (the last tag seen, or the one the state file names): it
            # is the same run until the marker says otherwise.
            if marker:
                tag = marker["session"]
            elif problem:
                tag = tag or (read_json(root / HEARTBEAT_STATE) or {}).get("session")
            else:
                tag = None
            state = unsaved if unsaved and unsaved.get("session") == tag else read_state(root, tag)
            try:
                code = tick(root, clock(), state, out)
            except (RunError, lib.ProfileError, lib.LaunchError, OSError, subprocess.SubprocessError) as exc:
                code = 1 if once else 0
                state["last_error"] = f"the tick failed ({type(exc).__name__}: {exc}); tried again next tick"
                print(f"heartbeat: {state['last_error']}", file=out)
            unsaved = state
        try:
            # Under the launch lock, and only while the marker still names this state's run (or the state is the
            # heartbeat's own ended run): a tick that saw the run alive must not write over the record `stop` made
            # meanwhile (reconciliation 3 of D02 T02 §8).
            with lib.launch_lock(root):
                current, unreadable = read_marker(root)
                own = bool(unsaved.get("ended")) or bool(current and current["session"] == unsaved.get("session"))
                if unreadable and not own:
                    raise lib.LaunchError(f"{unreadable}; written next tick")
                if own:
                    lib.write_atomic(root / HEARTBEAT_STATE, json.dumps(unsaved, indent=2) + "\n")
                    written = HEALTH
                    lib.write_atomic(root / HEALTH, json.dumps(health_of(unsaved, clock()), indent=2) + "\n")
                    if code == FINISHED and unsaved.get("ended"):
                        written = NEEDS_OPERATOR
                        remove_own_notice(root, unsaved["session"], out, strict=True)  # the end is whole only then
                else:
                    print(f"heartbeat: run {unsaved.get('session')} is no longer the marked run; its records are "
                          "the operator's now, and this tick's are not written", file=out)
            unsaved, pending = None, False
        except lib.LaunchError as exc:  # another launch or a stop holds the lock: the state waits in memory
            pending = (pending or code == FINISHED) and bool(unsaved.get("ended"))
            print(f"heartbeat: {exc}; the state is written next tick", file=out)
        except OSError as exc:  # a full or failing disk must not end the supervision of the run, or lose its end
            failed = written or HEARTBEAT_STATE
            unsaved["last_error"] = f"{failed} could not be written or removed ({exc}); tried again next tick"
            pending = (pending or code == FINISHED) and bool(unsaved.get("ended"))
            print(f"heartbeat: {failed} could not be written ({exc.strerror or exc}); kept in memory"
                  + ("; written again next tick" if pending else ""), file=out)
        if not pending and (once or code == FINISHED):
            # A stop may have handed its end over while this tick ran (it found no marker): take it before stopping
            # (reconciliation 4 of D02 T02 §8).
            if code != FINISHED or not handoff_pending(root):
                return code
        try:
            seconds = lib.load_profile(root)["tick_seconds"]
        except lib.ProfileError as exc:
            print(f"heartbeat: {exc}; the last tick of {seconds}s is kept", file=out or sys.stdout)
        sleep(seconds)


def _end_old_guard(root: Path, old: dict, out) -> None:
    """A fresh session cannot take the crashed run's guard, so the launcher ends it, as the operator would, with the
    guard's own session id. If it cannot, nothing fresh starts: the marker still names the crashed run."""
    guard = owned_guard(root, old)
    if guard:
        owner = str(guard.get("session_id"))
        done = _end_guard(root, owner)
        said = (done.stdout or done.stderr).strip()[-300:]
        if done.returncode != 0:
            raise RunError(f"the crashed run's guard (session {owner}) could not be ended ({said}); nothing was "
                           f"started, and {MARKER} still names that run")
        # The guard's end record says how a run's own agent ended it; this end is the launcher's, so its record goes,
        # or a heartbeat would read a recovery that failed as a run that finished.
        ended = read_json(root / ENDED)
        if ended and ended.get("run_id") == guard.get("run_id") and ended.get("reason") == "operator-stop":
            (root / ENDED).unlink(missing_ok=True)
        print(f"start: the crashed run's guard ended: {said}", file=out)


def _end_guard(root: Path, session: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(HERE / "campaign_guard.py"), "end", "--session", session,
                           "--reason", "operator-stop", "--root", str(root)],
                          capture_output=True, text=True, timeout=120, creationflags=lib.windowless())


def status(root: Path, out=None) -> int:
    out = out or sys.stdout
    profile = lib.load_profile(root)
    marker, problem = read_marker(root)
    run_session, heartbeat = (marker["tmux_session"], marker["heartbeat_session"]) if marker else names(profile)

    def state(name):
        live = _alive(name)
        return "unknown (tmux could not be asked)" if live is None else "live" if live else "stopped"
    print(f"run: {state(run_session)} (tmux session {run_session})", file=out)
    print(f"marker: {problem}" if problem else f"marker: {'present' if marker else 'none'}", file=out)
    print(f"session: {marker['session'] if marker else 'none'}", file=out)
    print(f"started: {marker['started'] if marker else 'never'}", file=out)
    print(f"heartbeat: {state(heartbeat)} (tmux session {heartbeat})", file=out)
    guard = read_json(root / GUARD)
    print(f"guard: phase {guard.get('phase')}, run {guard.get('run_id')}, session {guard.get('session_id')}"
          if guard else "guard: none", file=out)
    beat = read_json(root / HEARTBEAT_STATE)
    print(f"heartbeat state: {json.dumps(beat, sort_keys=True)}" if beat else "heartbeat state: none", file=out)
    health = read_json(root / HEALTH)
    print(f"health: {json.dumps(health, sort_keys=True)}" if health else "health: none", file=out)
    try:
        notice = (root / NEEDS_OPERATOR).read_text(encoding="utf-8")
    except FileNotFoundError:
        notice = None
    except OSError as exc:
        notice = f"{NEEDS_OPERATOR} cannot be read ({exc})"
    print(f"needs operator:\n{notice.rstrip()}" if notice else "needs operator: no", file=out)
    return 0


def attach_command(run_session: str, watch: bool) -> list[str]:
    return ["tmux", "attach-session", *(["-r"] if watch else []), "-t", f"={run_session}"]


def attach(root: Path, watch: bool) -> int:
    marker, _ = read_marker(root)
    run_session = marker["tmux_session"] if marker else names(lib.load_profile(root))[0]
    if _alive(run_session) is not True:
        raise RunError(f"tmux session {run_session} is not running")
    argv = attach_command(run_session, watch)
    os.execvp(argv[0], argv)
    return 0  # not reached: the launcher becomes the tmux client


def stop(root: Path, out=None) -> int:
    out = out or sys.stdout
    if os.environ.get(INSIDE) == "1":
        print(f"stop: refused inside the run ({INSIDE}=1): a run does not stop itself; stop it from a terminal",
              file=out)
        return 2
    profile = lib.load_profile(root)
    with lib.launch_lock(root):
        marker, problem = read_marker(root)
        if problem:
            print(f"stop: {problem}; stopping the profile's sessions", file=out)
        run_session, heartbeat = (marker["tmux_session"], marker["heartbeat_session"]) if marker else names(profile)
        session = marker["session"] if marker else None
        guard = read_json(root / GUARD)
        mine = owned_guard(root, marker)
        guard_failed = False
        if mine:
            done = _end_guard(root, str(mine.get("session_id")))
            guard_failed = done.returncode != 0
            print(f"stop: guard {'not ended' if guard_failed else 'ended'}: "
                  f"{(done.stdout or done.stderr).strip()[-300:]}", file=out)
        elif guard:
            print(f"stop: the guard belongs to session {guard.get('session_id')}, not this run's; left in place",
                  file=out)
        if session and _alive(heartbeat):
            # Said first, in tmux's memory: if this stop cannot record the end, the heartbeat that stays records it.
            tmux("set-environment", "-t", f"={heartbeat}", STOPPED, session)
        tmux("kill-session", "-t", f"={run_session}")
        if not session:
            tmux("kill-session", "-t", f"={heartbeat}")
        left = end_run_processes(session, marker["pgid"]) if marker else []
        if left:
            print(f"stop: processes {describe(left)} are still running after SIGKILL; {MARKER} and the claim are kept",
                  file=out)
            return 1
        if guard_failed:
            print(f"stop: the run's guard was not ended; {MARKER} and the claim are kept, so a second stop can "
                  "end it", file=out)
            return 1
        recorded = True
        if session:
            deadline = time.monotonic() + STOP_RECORD_SECONDS
            while True:
                try:
                    record_stop(root, session, time.time(), out)
                    break
                except OSError as exc:
                    if time.monotonic() >= deadline:
                        recorded, failure = False, exc
                        break
                    time.sleep(1)
            print(f"stop: claim {lib.clear_claim(root, session, lock_held=True)}", file=out)
        # The marker goes either way: the run is stopped, and a later start must start fresh, not resume it.
        (root / MARKER).unlink(missing_ok=True)
        if recorded:
            tmux("kill-session", "-t", f"={heartbeat}")
        else:
            # The heartbeat owns the end it could not record: it keeps trying until the disk takes it, then stops.
            _launch_heartbeat(root, heartbeat, out, stopped=session)
            print(f"stop: the run's end could not be recorded in {STOP_RECORD_SECONDS}s ({failure}); the heartbeat "
                  f"(tmux session {heartbeat}) records it once the disk takes it", file=out)
    print(f"stop: run {session or 'with no marker'} stopped", file=out)
    return 0


def _ship_limit(text: str) -> int:
    """`--ships N`: a whole number above 0."""
    if not re.fullmatch(r"[0-9]+", text or "") or int(text) <= 0:
        raise argparse.ArgumentTypeError(f"must be a whole number above 0, got {text!r}")
    return int(text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Start, show, attach to and stop an unattended agent run.")
    parser.add_argument("--root", default=str(HERE.parent), help="the repository (default: this script's)")
    sub = parser.add_subparsers(dest="command", required=True)
    start_parser = sub.add_parser("start")
    start_parser.add_argument("--no-heartbeat", action="store_true", help="start the run without its heartbeat")
    start_parser.add_argument("--ships", type=_ship_limit, metavar="N",
                              help="end the run once it has shipped N sections (the heartbeat enforces it)")
    sub.add_parser("status")
    attach_parser = sub.add_parser("attach")
    attach_parser.add_argument("--watch", action="store_true", help="attach read-only")
    sub.add_parser("stop")
    beat_parser = sub.add_parser("heartbeat")
    beat_parser.add_argument("--once", action="store_true", help="one tick, then exit")
    args = parser.parse_args(argv)
    if os.name == "nt":
        print("unattended-run: the run lives in tmux, on Linux and in WSL: run this in WSL", file=sys.stderr)
        return 1
    root = Path(args.root).resolve()
    try:
        if args.command == "start":
            return start(root, supervise=not args.no_heartbeat, ships=args.ships)
        if args.command == "status":
            return status(root)
        if args.command == "attach":
            return attach(root, args.watch)
        if args.command == "heartbeat":
            return heartbeat(root, once=args.once)
        return stop(root)
    except UsageError as exc:
        print(f"unattended-run: {exc}", file=sys.stderr)
        return 2
    except (RunError, lib.ProfileError, lib.LaunchError) as exc:
        print(f"unattended-run: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
