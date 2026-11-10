#!/usr/bin/env python3
"""Report the CI state of main: the latest completed run of each watched workflow, and runs still in progress.

    python scripts/ci_status.py                  # build.yml and plan.yml on main
    python scripts/ci_status.py --json           # the same report as one JSON object
    python scripts/ci_status.py --workflow build.yml --branch main

For each workflow (by default `build.yml` and `plan.yml`, the ones in `.github/workflows/` whose results gate main) it
reads the last 10 runs with `gh run list` and reports the latest completed run (green, red or cancelled), its verdict,
and how many newer runs are still queued or in progress. A cancelled run carries no verdict (a newer push usually
superseded it), so the verdict is the latest completed run that was not cancelled; with none in the last 10 runs the
verdict is `none`. A job GitHub stopped at its `timeout-minutes` is reported as cancelled, with the reason only in its
check-run annotation: such a run is red here (the work did not finish), in this report and in `--wait`. For a red verdict it names the failed jobs from `gh run view <id> --json jobs`.

Exit 0: no watched workflow is red. Exit 1: at least one is red (the workflow, the commit, the run's URL and its
failed jobs are printed). Exit 2: bad usage. Exit 3: `gh` is missing, not authenticated, or failed, so the state is
unknown; that is never treated as green. The red-first policy that consumes this is in the `process-phase` skill.
Stdlib only; the `gh` runner is injectable for tests.

    python scripts/ci_status.py --wait <sha>     # follow one pushed commit's runs to their end (D00 T04 §9)
    python scripts/ci_status.py --gate [--brief] # may a new row be claimed, or a section stamp?

`--wait` reads which watched workflows the push starts from each workflow's own `push` filter (`branches`, `paths`,
`paths-ignore`) and the files the push brought: from `--since` (the branch's old head), else from the earlier watch of
the same commit, else from GitHub's own record of the branch's pushes. When none of the three gives the range, the
commit's own files are only a lower bound, and every watched workflow gets 10 minutes to show a run before it is taken
as not started. It follows
each through `gh run list --commit` every `--poll` seconds (default 10), and writes its state to
`build/ci-watch/<sha>.json` as it goes. It prints what it expects when it starts and every change of state with the
seconds since then. The verdict over the commit's runs: a red exits 1 the poll it is seen (every failed job named, and
the workflows still running named too, since a red is acted on at once), else an expected run not seen within 10
minutes exits 3 (unknown, never green), else a cancelled run exits 5 (main builds are never cancelled by a newer push,
so this is a manual or infrastructure stop), else running past `--timeout` exits 4, else 0. A read of one workflow that
fails is waited out and never hides another's red; one that keeps failing for 10 minutes makes that workflow unknown.
Its last line names the next action with its command. A watch started again for the same commit keeps the first one's
`--since`, counts itself (`watches`), and is refused with exit 6 while the first is still alive.

`--gate` asks whether a new row may be claimed or a section stamped: 0 when the watch of main's head ended green and
GitHub still lists every watched run of that commit as completed and green (a rerun in progress, or one that turned
red, is not a green); 1 when it is red; 6 for anything else, each with what it found and the next command: no watch, a
watch still running or gone, a head on GitHub this clone has not fetched (`git ls-remote`), a cancelled, unknown or
timed-out end. `--brief` prints the answer on one line, failed jobs included, and exits 0, for the session's start hook.

`--scheduled` (the `recto-ci-watch` task in `.conclave/automations/`) exits 1 for every red run in each workflow's last
10 runs that has not been reported, so the runner's failure email names it, and once when `gh` has been unreachable
for an hour. A run counts as reported only when the runner's `build/automations/recto-ci-watch/last.json` says that
email was sent; until then every poll raises it again. Its memory is `build/automations/ci-watch-reported.json`.

A state file that cannot be written (a full disk, a locked file, a refused folder) is exit 3, unknown, with the path
and what to do: never exit 1, which means red. The file it could not replace stays as it was.
"""
import argparse
import calendar
import ctypes
import fnmatch
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ("build.yml", "plan.yml")
FIELDS = "databaseId,headSha,status,conclusion,url,createdAt,event,attempt"
LIMIT = 10
GREEN = {"success", "neutral", "skipped"}
# A run that was stopped before it judged the commit: no verdict either way.
NO_VERDICT = {"cancelled", "stale"}
MAX_JOBS = 20
MAX_MESSAGE = 300


class Unavailable(Exception):
    """gh is missing, unauthenticated or failed: the CI state is unknown (exit 3)."""


def run_gh(args):
    """Run gh with args; returns (exit code, stdout, stderr). Raises Unavailable when gh is not installed."""
    # An explicit path: on Windows a bare which() also searches the working directory, where a planted gh could sit.
    executable = shutil.which("gh", path=os.environ.get("PATH", ""))
    if executable is None:
        raise Unavailable("gh is not installed or not on PATH")
    try:
        done = subprocess.run([executable, *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", timeout=120)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise Unavailable(f"gh could not run: {exc}") from None
    return done.returncode, done.stdout, done.stderr


def gh_json(runner, args):
    try:
        code, out, err = runner(args)
    except FileNotFoundError:
        raise Unavailable("gh is not installed or not on PATH") from None
    if code != 0:
        reason = (err or out or "").strip().splitlines()
        raise Unavailable(f"gh {' '.join(args[:2])} exited {code}: {(reason[0] if reason else 'no output')[:MAX_MESSAGE]}")
    try:
        return json.loads(out)
    except ValueError:
        raise Unavailable(f"gh {' '.join(args[:2])} did not return JSON") from None


def verdict_of(run):
    if run is None:
        return "none"
    conclusion = (run.get("conclusion") or "").lower()
    if conclusion in GREEN:
        return "green"
    if conclusion in NO_VERDICT:
        return "cancelled"
    return "red"


TIMEOUT_MARK = "exceeded the maximum execution time"


def timed_out_jobs(runner, run):
    """The jobs of a cancelled run that GitHub stopped at their `timeout-minutes`, as failed jobs. GitHub reports a job
    timeout as `cancelled` and says why only in the job's check-run annotation; a timeout is a red build (the work did
    not finish), never a cancel to rerun or skip. Unavailable when the jobs or annotations cannot be read."""
    view = gh_json(runner, ["run", "view", str(run.get("databaseId")), "--json", "jobs"])
    jobs = view.get("jobs") if isinstance(view, dict) else None
    if not isinstance(jobs, list):
        raise Unavailable("gh run view returned no job list")
    found = []
    for job in jobs:
        if not isinstance(job, dict) or (job.get("conclusion") or "").lower() != "cancelled":
            continue
        notes = gh_json(runner, ["api", f"repos/{{owner}}/{{repo}}/check-runs/{job.get('databaseId')}/annotations"])
        messages = [n.get("message") or "" for n in notes if isinstance(n, dict)] if isinstance(notes, list) else []
        timeout = next((m for m in messages if TIMEOUT_MARK in m), None)
        if timeout:
            found.append({"name": job.get("name"), "conclusion": f"timed out ({timeout.strip()})", "url": job.get("url")})
    return found[:MAX_JOBS]


def settle(runner, run):
    """A completed run's verdict and failed jobs, a timed-out run counted red."""
    verdict = verdict_of(run)
    if verdict == "cancelled":
        timed_out = timed_out_jobs(runner, run)
        if timed_out:
            return "red", timed_out
    if verdict == "red":
        return "red", failed_jobs(runner, run.get("databaseId"))
    return verdict, []


def brief(run):
    return None if run is None else {k: run.get(k) for k in ("databaseId", "headSha", "status", "conclusion", "url",
                                                              "createdAt", "event")}


def workflow_status(runner, workflow, branch):
    runs = gh_json(runner, ["run", "list", "--branch", branch, "--workflow", workflow, "--limit", str(LIMIT),
                            "--json", FIELDS])
    if not isinstance(runs, list) or not all(isinstance(r, dict) for r in runs):
        raise Unavailable(f"gh run list for {workflow} did not return a list of runs")
    runs = sorted(runs, key=lambda r: str(r.get("createdAt") or ""), reverse=True)
    completed = [r for r in runs if r.get("status") == "completed"]
    latest = completed[0] if completed else None
    # The decisive run is the latest completed one that was not cancelled; a timed-out run is red, not cancelled.
    decisive, verdict, jobs = None, "none", []
    latest_state = verdict_of(latest)
    for candidate in completed:
        verdict, jobs = settle(runner, candidate)
        if candidate is latest:
            latest_state = verdict
        if verdict != "cancelled":
            decisive = candidate
            break
    else:
        verdict, jobs = "none", []
    newer = [r for r in runs if r.get("status") != "completed"
             and (latest is None or str(r.get("createdAt") or "") > str(latest.get("createdAt") or ""))]
    entry = {"workflow": workflow, "verdict": verdict, "latest_completed": brief(latest),
             "latest_completed_state": latest_state, "verdict_run": brief(decisive), "in_progress": len(newer)}
    if verdict == "red":
        entry["failed_jobs"] = jobs
    return entry


def failed_jobs(runner, run_id):
    """The failed jobs of one run, or a one-line note when they cannot be read (the run is still red)."""
    try:
        view = gh_json(runner, ["run", "view", str(run_id), "--json", "jobs"])
    except Unavailable as exc:
        return [{"name": f"jobs unavailable: {exc}", "conclusion": None, "url": None}]
    jobs = view.get("jobs") if isinstance(view, dict) else None
    if not isinstance(jobs, list):
        return [{"name": "jobs unavailable: gh run view returned no job list", "conclusion": None, "url": None}]
    failed = [j for j in jobs if isinstance(j, dict) and verdict_of(j) == "red" and j.get("status") == "completed"]
    return [{"name": j.get("name"), "conclusion": j.get("conclusion"), "url": j.get("url")} for j in failed[:MAX_JOBS]]


def report(runner, workflows, branch):
    """The report dict and its exit code; Unavailable propagates (exit 3)."""
    entries = [workflow_status(runner, w, branch) for w in workflows]
    code = 1 if any(e["verdict"] == "red" for e in entries) else 0
    return {"branch": branch, "workflows": entries, "exit": code}, code


def describe(entry):
    run = entry["verdict_run"]
    where = f" at {run['headSha'][:12]} ({run['url']})" if run and run.get("headSha") and run.get("url") else ""
    lines = [f"{entry['workflow']}: {entry['verdict']}{where}"]
    if entry["latest_completed_state"] == "cancelled":
        lines.append(f"  latest completed run was cancelled: {entry['latest_completed']['url']}")
    if entry["in_progress"]:
        lines.append(f"  {entry['in_progress']} newer run(s) still queued or in progress")
    for job in entry.get("failed_jobs", []):
        lines.append(f"  failed job: {job['name']} ({job['conclusion']}) {job['url'] or ''}".rstrip())
    return lines


# --- Following one push (D00 T04 §9). ------------------------------------------------------------------------------

WATCH_DIR = Path("build") / "ci-watch"  # one state file per watched commit, so a newer push's watch never overwrites an older one


def watch_file(root, sha):
    return Path(root) / WATCH_DIR / f"{sha}.json"
APPEAR_SECONDS = 600       # an expected run not seen by then is unknown, not green
WAIT_FIELDS = "databaseId,headSha,status,conclusion,url,createdAt,event,workflowName,attempt"


def push_filter(workflow_file):
    """The `on: push:` block of a workflow, as {"branches": [...], "paths": [...], "paths-ignore": [...]} (empty lists for
    keys it does not have), or None when the workflow is not started by a push. A small reader for the plain block
    style these workflows use; anything it cannot read counts as "starts on every push"."""
    lines = Path(workflow_file).read_text(encoding="utf-8").splitlines()
    result = {"branches": [], "paths": [], "paths-ignore": []}
    in_on = in_push = False
    key = None
    for raw in lines:
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip())
        text = raw.strip()
        if indent == 0:
            in_on = text.startswith("on:")
            in_push = False
            continue
        if not in_on:
            continue
        if indent == 2:
            in_push = text.startswith("push:")
            key = None
            continue
        if not in_push:
            continue
        match = re.match(r"^([a-z-]+):\s*(.*)$", text)
        if indent == 4 and match:
            key = match.group(1) if match.group(1) in result else None
            inline = match.group(2).strip()
            if key and inline.startswith("[") and inline.endswith("]"):
                result[key] = [v.strip().strip("'\"") for v in inline[1:-1].split(",") if v.strip()]
            continue
        if key and text.startswith("- "):
            result[key].append(text[2:].strip().strip("'\""))
    text = "\n".join(lines)
    if not re.search(r"(?m)^on:\s*$", text) or not re.search(r"(?m)^  push:", text):
        return None
    return result


def glob_match(pattern, path):
    """GitHub's path globs: `**` crosses folders, `*` and `?` do not."""
    regex = ""
    i = 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            regex += "(?:.*/)?"
            i += 3
        elif pattern.startswith("**", i):
            regex += ".*"
            i += 2
        elif pattern[i] == "*":
            regex += "[^/]*"
            i += 1
        elif pattern[i] == "?":
            regex += "[^/]"
            i += 1
        else:
            regex += re.escape(pattern[i])
            i += 1
    return re.fullmatch(regex, path) is not None


def starts_on(filter_, branch, changed):
    """Whether a push of `changed` files to `branch` starts a workflow with this push filter."""
    if filter_ is None:
        return False
    if filter_["branches"] and not any(fnmatch.fnmatchcase(branch, b) for b in filter_["branches"]):
        return False
    if filter_["paths"]:
        return any(glob_match(p, f) for f in changed for p in filter_["paths"])
    if filter_["paths-ignore"]:
        return not all(any(glob_match(p, f) for p in filter_["paths-ignore"]) for f in changed)
    return True


def run_git(args):
    try:
        done = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", timeout=60)
    except (OSError, subprocess.TimeoutExpired) as exc:
        # A git that cannot run or does not answer (ls-remote with the network down) is unknown, never a red.
        raise Unavailable(f"git {' '.join(args[:2])} could not run: {exc}") from None
    if done.returncode != 0:
        raise Unavailable(f"git {' '.join(args[:2])} exited {done.returncode}: {done.stderr.strip()[:MAX_MESSAGE]}")
    return done.stdout


def process_alive(pid):
    """Whether process `pid` is still running (Windows: OpenProcess and GetExitCodeProcess)."""
    if not isinstance(pid, int) or pid <= 0:
        return False
    if os.name != "nt":
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        return bool(kernel32.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259  # STILL_ACTIVE
    finally:
        kernel32.CloseHandle(handle)


def process_born(pid):
    """When process `pid` was created, as a number that differs for any other process that later gets the same pid
    (Windows: its creation FILETIME; elsewhere: its start time in clock ticks from /proc), or None when it cannot be
    read (no such process, or no access)."""
    if not isinstance(pid, int) or pid <= 0:
        return None
    if os.name != "nt":
        try:
            fields = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8").rsplit(")", 1)[1].split()
            return int(fields[19])
        except (OSError, ValueError, IndexError):
            return None
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return None
    try:
        created, ended, kernel, user = (ctypes.c_ulonglong() for _ in range(4))
        ok = kernel32.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(ended), ctypes.byref(kernel),
                                      ctypes.byref(user))
        return int(created.value) if ok else None
    finally:
        kernel32.CloseHandle(handle)


def watch_alive(state, alive=process_alive, born=process_born):
    """Whether the process that wrote this watch file is still running: its pid is alive and was created when the file
    says. A pid alone is not enough: after a restart or a kill Windows gives the number to another process, and a
    watch that waited on that one would wait for ever. A file from before the creation time was recorded has none, and
    is judged by its pid."""
    pid = state.get("pid")
    if not alive(pid):
        return False
    return state.get("born") is None or born(pid) == state.get("born")


def write_state(path, state):
    """Replace `path` with `state`, whole or not at all. A full disk, a locked file or a refused write is Unavailable
    (the CI state is then unknown, never red or green), naming the path and what to do; the old file stays as it was."""
    path = Path(path)
    temporary = path.with_name(f"{path.name}.{os.getpid()}.tmp")  # its own, so two writers never share one
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary.write_text(json.dumps(state, indent=2), encoding="utf-8")
        os.replace(temporary, path)
    except OSError as exc:
        try:
            temporary.unlink()  # the half-written copy; the error that matters is raised below
        except OSError:
            pass
        raise Unavailable(f"cannot write {path}: {exc}; free disk space or close the program holding the file, "
                          f"then run the same command again") from None


def read_state(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def verdict_of_commit(states):
    """The exit for a commit from its workflows' states: a red anywhere wins over a cancel or an absence."""
    values = [s["state"] for s in states.values()]
    if "red" in values:
        return 1
    if "unknown" in values:
        return 3
    if "cancelled" in values:
        return 5
    if "pending" in values:
        return 4
    return 0


def runs_of_commit(runner, workflow, sha):
    """The push runs of one workflow on `sha`, oldest first (a rerun keeps its run id and raises its attempt)."""
    runs = gh_json(runner, ["run", "list", "--workflow", workflow, "--commit", sha, "--limit", "20", "--json", WAIT_FIELDS])
    runs = [r for r in runs if isinstance(r, dict) and r.get("headSha") == sha and r.get("event") == "push"] \
        if isinstance(runs, list) else []
    return sorted(runs, key=lambda r: (str(r.get("createdAt") or ""), r.get("attempt") or 0))


def say(line):
    """Print now: a watch runs in the background, and a line held in a buffer until it ends tells nobody anything."""
    print(line, flush=True)


def pushed_from(runner, sha, branch):
    """The branch's head before the push that made `sha` its head, read from GitHub's record of the branch's pushes,
    or None when `sha` was never a pushed head there or GitHub cannot say. This is the range GitHub itself filtered
    the push's workflows by."""
    try:
        pushes = gh_json(runner, ["api", f"repos/{{owner}}/{{repo}}/activity?ref=refs/heads/{branch}"
                                         f"&activity_type=push&per_page=100"])
    except Unavailable:
        return None
    for push in pushes if isinstance(pushes, list) else []:
        if isinstance(push, dict) and push.get("after") == sha and re.fullmatch(r"[0-9a-f]{40}", str(push.get("before"))):
            return None if set(push["before"]) == {"0"} else push["before"]
    return None


def claim(lock, pid, created, alive=process_alive, born=process_born, sleep=time.sleep):
    """Take the watch of one commit for process `pid`: create `lock` if no live watch holds it. Returns None when the
    claim is this process's, or the holder's record when another live watch has it. Creating the file is the claim
    (it fails if the file exists), so of two watches started together exactly one goes on. A lock whose holder is gone
    (its pid dead, or given to another process) is removed and claimed."""
    lock = Path(lock)
    for _ in range(3):
        try:
            lock.parent.mkdir(parents=True, exist_ok=True)
            handle = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            holder = read_state(lock)
            if holder is None:
                sleep(0.5)  # created this instant by another watch that has not written its name yet
                holder = read_state(lock)
            if isinstance(holder, dict) and holder.get("pid") != pid and watch_alive(holder, alive, born):
                return holder
            try:
                lock.unlink()  # its holder is gone; the next turn of the loop claims it, or loses to whoever did
            except OSError:
                pass
            continue
        except OSError as exc:
            raise Unavailable(f"cannot write {lock}: {exc}; free disk space or close the program holding the file, "
                              f"then run the same command again") from None
        with os.fdopen(handle, "w", encoding="utf-8") as out:
            json.dump({"pid": pid, "born": created}, out)
        return None
    raise Unavailable(f"cannot claim {lock}: it keeps being taken; run the same command again")


def wait(sha, *, pid=None, alive=process_alive, born=process_born, root=ROOT, out=say, sleep=time.sleep, **options):
    """Follows the watched workflows' runs on `sha` (see `follow`), as the only watch of that commit: it claims
    `build/ci-watch/<sha>.lock` before anything else and holds it until it ends, so a watch started beside it (a second
    session recovering, a heartbeat) exits 6 at once instead of following the same runs and writing the same file."""
    me = pid if pid is not None else os.getpid()
    lock = watch_file(root, sha).with_suffix(".lock")
    holder = claim(lock, me, born(me), alive, born, sleep)
    if holder is not None:
        out(f"ci watch: {sha[:12]} is already watched by process {holder.get('pid')}; no second watch is started. "
            f"next: wait for that watch to end")
        return 6
    try:
        return follow(sha, pid=me, alive=alive, born=born, root=root, out=out, sleep=sleep, **options)
    finally:
        try:
            lock.unlink()
        except OSError:
            pass  # already gone; a lock left behind is released by the next watch, which sees its holder is dead


def follow(sha, *, since=None, runner=run_gh, git=run_git, root=ROOT, workflows=WORKFLOWS, branch="main", timeout=3600,
           poll=10, clock=time.monotonic, sleep=time.sleep, out=say, pid=None, alive=process_alive, born=process_born):
    """Follows the watched workflows' runs on `sha` and returns the exit code the moment the verdict is known: 1 at the
    first red seen (the other runs are named as still running), else when every run has ended. See the module
    docstring. Every change of state is printed with the seconds since the watch began."""
    directory = Path(root) / ".github" / "workflows"
    state_path = watch_file(root, sha)
    me = pid if pid is not None else os.getpid()
    previous = read_state(state_path)
    previous = previous if isinstance(previous, dict) and previous.get("sha") == sha else {}
    if previous.get("exit") is None and previous.get("pid") not in (None, me) and watch_alive(previous, alive, born):
        out(f"ci watch: {sha[:12]} is already watched by process {previous['pid']}; no second watch is started. "
            f"next: wait for that watch to end")
        return 6
    # A restarted watch keeps the push range the first one was given: GitHub filters a push by everything it brought
    # (the branch's old head to its new one), not by its last commit.
    since = since or previous.get("since") or pushed_from(runner, sha, branch)
    # With no range from the caller, an earlier watch or GitHub, the commit's own files are only a lower bound: a
    # workflow they do not start is then given the same ten minutes to show a run before it is taken as not started.
    ranged = since is not None
    changed = [line.strip() for line in git(["diff", "--name-only", since or f"{sha}^", sha]).splitlines() if line.strip()]
    expected = {w for w in workflows if (directory / w).is_file() and starts_on(push_filter(directory / w), branch, changed)}
    started = clock()
    states = {w: {"state": "pending", "run": None, "failed_jobs": []} for w in expected}
    record = {"sha": sha, "since": since, "pid": me, "born": born(me), "watches": int(previous.get("watches") or 0) + 1,
              "expected": sorted(expected), "workflows": states, "exit": None}
    write_state(state_path, record)  # nothing is known yet, so a failed write here is Unavailable (exit 3)
    out(f"ci watch: {sha[:12]} expects {', '.join(sorted(expected)) or 'no watched workflow'} "
        f"({len(changed)} changed file(s)); reading every {poll} s for up to {timeout // 60} min")
    if not ranged:
        out(f"ci watch: the push that brought {sha[:12]} is not known (no --since, no earlier watch, not in GitHub's "
            f"push record), so every watched workflow gets {APPEAR_SECONDS // 60} minutes to show a run")
    said, outage, write_warned = {}, {}, False
    while True:
        for workflow in workflows:
            if not (directory / workflow).is_file():
                continue
            try:
                runs = runs_of_commit(runner, workflow, sha)
                if not runs:
                    if workflow not in expected:
                        if ranged or clock() - started >= APPEAR_SECONDS:
                            states.pop(workflow, None)  # read, and it has no run on this commit: nothing to wait for
                        else:
                            states[workflow] = {"state": "pending", "run": None, "failed_jobs": [],
                                                "note": "the push range is not known: waiting to see whether it starts"}
                    elif clock() - started >= APPEAR_SECONDS:
                        states[workflow] = {"state": "unknown", "run": None, "failed_jobs": [],
                                            "note": f"expected for this push, not seen in {APPEAR_SECONDS // 60} minutes"}
                    outage.pop(workflow, None)
                    continue
                latest = runs[-1]
                entry = {"run": brief(latest), "failed_jobs": []}
                if latest.get("status") != "completed":
                    entry["state"] = "pending"
                else:
                    verdict, jobs = settle(runner, latest)
                    entry["state"] = {"green": "green", "cancelled": "cancelled"}.get(verdict, "red")
                    entry["failed_jobs"] = jobs if entry["state"] == "red" else []
                states[workflow] = entry
                outage.pop(workflow, None)
                if entry["state"] == "red":
                    break  # reported now: no other workflow's read, which can take minutes on a bad network, delays it
            except Unavailable as exc:
                # One workflow's failed read never hides what is already known (a red seen stays red), and a short
                # outage is waited out; only one that lasts as long as a missing run makes the workflow unknown.
                first = outage.setdefault(workflow, clock())
                # A workflow the push was not expected to start may have a run all the same (the range was not
                # given, or its filter changed): until it has been read once, the watch does not call the push green.
                known = states.setdefault(workflow, {"state": "pending", "run": None, "failed_jobs": [],
                                                     "note": f"gh could not read it: {exc}"})
                if known["state"] in ("green", "cancelled"):
                    # A verdict read earlier is not the verdict now (the run may have been started again): it
                    # stands only while it can be read back, so it waits for the next read that answers.
                    known = states[workflow] = {**known, "state": "pending", "note": f"gh could not read it again: {exc}"}
                if known["state"] == "pending" and clock() - first >= APPEAR_SECONDS:
                    states[workflow] = {**known, "state": "unknown",
                                        "note": f"gh could not read it for {APPEAR_SECONDS // 60} minutes: {exc}"}
        for workflow, entry in sorted(states.items()):
            run = entry.get("run") or {}
            waiting = "waiting for GitHub to register the run" if entry["state"] == "pending" and not run else ""
            waiting = entry.get("note") or waiting if workflow not in expected else waiting
            line = f"{workflow}: {entry['state']} {run.get('status') or ''} {run.get('url') or entry.get('note') or waiting}"
            if run and entry.get("note") and entry["state"] in ("pending", "unknown"):
                line += f" ({entry['note']})"  # why a run that was read before is being waited on again
            line = " ".join(line.split())
            if said.get(workflow) != line:
                said[workflow] = line
                out(f"[{int(clock() - started):>5} s] {line}")
        red = any(entry["state"] == "red" for entry in states.values())
        finished = all(entry["state"] != "pending" for entry in states.values())
        if red or finished or clock() - started >= timeout:
            code, unsaved = verdict_of_commit(states), False
            record["exit"] = code
            try:
                write_state(state_path, record)
            except Unavailable as exc:
                # The file still says the watch has not ended, so the gate would wait on it for ever: a red seen is
                # still reported as red, and anything else is unknown (3), with the reason, never a green nobody
                # can read back.
                out(f"ci watch: {exc}")
                unsaved = True
                if code != 1:
                    record["exit"] = code = 3
            for workflow, entry in sorted(states.items()):
                run = entry.get("run") or {}
                out(f"{workflow}: {entry['state']} {run.get('url') or entry.get('note') or ''}".rstrip())
                for job in entry.get("failed_jobs", []):
                    out(f"  failed job: {job['name']} ({job['conclusion']}) {job['url'] or ''}".rstrip())
            if not states:
                out(f"no watched workflow starts on {sha[:12]} (its files match no push filter)")
            still = sorted(w for w, entry in states.items() if entry["state"] == "pending")
            if red and still:
                out(f"red is reported at once; still running on this commit: {', '.join(still)} "
                    f"(read them with `python scripts/ci_status.py`)")
            if unsaved and code != 1:
                out(f"ci watch: exit {code} for {sha[:12]}. next: the watch's state could not be saved: free disk "
                    f"space or close the program holding {state_path}, then start "
                    f"`python scripts/ci_status.py --wait {sha}` again")
            else:
                out(f"ci watch: exit {code} for {sha[:12]}. {next_step(record)}")
            return code
        try:
            write_state(state_path, record)
        except Unavailable as exc:
            if not write_warned:
                out(f"ci watch: {exc}")
                write_warned = True
        sleep(poll)


def next_step(state):
    """The one next action for a watch's recorded end, as `process-phase` step 3 decides it, with its command."""
    sha, code = state.get("sha") or "", state.get("exit")
    again = f"`python scripts/ci_status.py --wait {sha}`"
    workflows = state.get("workflows") or {}
    if code == 0:
        return "next: nothing, CI is green for this push"
    if code == 1:
        ids = [str((e.get("run") or {}).get("databaseId")) for _, e in sorted(workflows.items())
               if e.get("state") == "red" and (e.get("run") or {}).get("databaseId")]
        return ("next: red comes first, before any new row or stamp: read "
                + (" and ".join(f"`gh run view {i} --log-failed`" for i in ids) or "the failed run's log")
                + ", fix it at its cause, push, and watch the fix")
    if code == 5:
        ids = [str((e.get("run") or {}).get("databaseId")) for _, e in sorted(workflows.items())
               if e.get("state") == "cancelled" and (e.get("run") or {}).get("databaseId")]
        return ("next: a cancelled run is not green: "
                + (" and ".join(f"`gh run rerun {i}`" for i in ids) or "rerun the cancelled run") + f", then {again}")
    kind = {3: "unknown", 4: "still running at the watch's time limit"}.get(code, "not ended")
    if int(state.get("watches") or 1) >= 2 and code in (3, 4):
        return (f"next: the second watch also ended {kind}: record a Critical event in the run file naming the "
                f"workflow, never treat it as green, and start {again} once GitHub or gh answers again")
    return f"next: {kind} is not green: start a second watch, {again}"


def full_sha(git, value):
    """The full id of the commit `value` names; Unavailable when this clone has no such commit."""
    resolved = git(["rev-parse", "--verify", f"{value}^{{commit}}"]).strip()
    if not re.fullmatch(r"[0-9a-f]{40}", resolved):
        raise Unavailable(f"{value} does not name a commit in this clone")
    return resolved


def gate(*, git=run_git, runner=run_gh, root=ROOT, brief_line=False, out=print, alive=process_alive,
         watched=WORKFLOWS, born=process_born):
    """0 when the watch of main's head ended green and that is still true on GitHub, 1 red, 6 anything else (no watch,
    a watch not ended, a remote head this clone has not fetched, a rerun in progress, a cancelled, unknown or timed-out
    end). Every refusal says what it found and the next command. Unavailable when git or gh cannot answer."""
    head = git(["rev-parse", "origin/main"]).strip()
    remote = git(["ls-remote", "origin", "refs/heads/main"]).split()
    if not remote:
        raise Unavailable("git ls-remote returned no head for main")
    if remote[0] != head:
        out(f"ci gate: main moved on GitHub to {remote[0][:12]} and this clone still has {head[:12]}, so no watch here "
            f"judged it. next: `git pull --ff-only`, then `python scripts/ci_status.py --wait {remote[0]} --since {head}`")
        return 6
    again = f"`python scripts/ci_status.py --wait {head}`"
    state = read_state(watch_file(root, head))
    if not isinstance(state, dict) or state.get("sha") != head:
        out(f"ci gate: no watch for origin/main {head[:12]}. next: start {again}")
        return 6
    code = state.get("exit")
    workflows = state.get("workflows")
    workflows = workflows if isinstance(workflows, dict) else {}
    if code is None:
        if watch_alive(state, alive, born):
            out(f"ci gate: the watch of {head[:12]} is still running (process {state.get('pid')}). "
                f"next: wait for its notification")
        else:
            out(f"ci gate: the watch of {head[:12]} is gone before it ended; start it again. next: start {again}")
        return 6
    if code == 0:
        # A green recorded earlier is admitted only while it is still the truth: a rerun of the same commit, or a run
        # that started after the watch ended, is read from GitHub now.
        # Every watched workflow is read, not only the ones the watch recorded: a watch restarted without its push
        # range can have expected too few, and a run it never saw must not pass on its green.
        directory = Path(root) / ".github" / "workflows"
        red, running, unread = [], [], []
        for workflow in sorted(set(workflows) | {w for w in watched if (directory / w).is_file()}):
            # Every workflow is read before the gate answers, so a red is never hidden behind another workflow that
            # is still running or cannot be read.
            try:
                runs = runs_of_commit(runner, workflow, head)
                latest = runs[-1] if runs else None
                if latest is None and workflow not in workflows:
                    continue  # not started by this push, and the watch did not expect it either
                if latest is None or latest.get("status") != "completed":
                    running.append(f"{workflow} is running again ({(latest or {}).get('url') or 'no run listed'})")
                    continue
                verdict, jobs = settle(runner, latest)
            except Unavailable as exc:
                unread.append(f"{workflow}: {exc}")
                continue
            if verdict == "red":
                names = ", ".join(str(job["name"]) for job in jobs)
                red.append(f"{workflow} is red ({latest.get('url')})" + (f"; failed jobs: {names}" if names else ""))
            elif verdict != "green":
                running.append(f"{workflow} is {verdict} ({latest.get('url')})")
        if red:
            out(f"ci gate: red at {head[:12]} after a recorded green: " + "; ".join(red + running)
                + f". next: red comes first: fix it at its cause, push, and watch the fix; this commit's watch is {again}")
            return 1
        if unread:
            raise Unavailable("; ".join(unread))
        if running:
            out(f"ci gate: the recorded green of {head[:12]} is stale: " + "; ".join(running) + f". next: start {again}")
            return 6
        out(f"ci gate: green at {head[:12]}")
        return 0
    parts = []
    for workflow, entry in sorted(workflows.items()):
        if entry.get("state") == "green":
            continue
        names = ", ".join(str(job.get("name")) for job in entry.get("failed_jobs") or [])
        parts.append(f"{workflow} {entry.get('state')} {(entry.get('run') or {}).get('url') or entry.get('note') or ''}".strip()
                     + (f" (failed jobs: {names})" if names else ""))
    label = {1: "red", 3: "unknown", 4: "not ended at the watch's time limit", 5: "cancelled"}.get(code, f"exit {code}")
    out(f"ci gate: {label} at {head[:12]}: " + "; ".join(parts) + f". {next_step(state)}")
    if not brief_line:
        for workflow, entry in sorted(workflows.items()):
            for job in entry.get("failed_jobs") or []:
                out(f"  {workflow} failed job: {job['name']} {job['url'] or ''}".rstrip())
    return 1 if code == 1 else 6


REPORTED_FILE = Path("build") / "automations" / "ci-watch-reported.json"
TASK = "recto-ci-watch"  # its runner writes build/automations/<TASK>/last.json, with whether the alert was sent
UNKNOWN_ALERT_SECONDS = 3600
# The runner's email carries the last 60 lines of the task's log (`alert.tail_of` in `.conclave/automations/`). One
# poll therefore names at most this many pending items, last in its output, and only those are acknowledged by that
# email; the rest stay pending for the next one.
ALERT_BATCH = 40


def _epoch(stamp):
    try:
        return calendar.timegm(time.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ"))
    except (TypeError, ValueError):
        return None


def alert_delivered(root, raised_at):
    """Whether the runner (`.conclave/automations/run.py`) sent the failure email for the run of this task that raised
    at `raised_at`: its `last.json` covers that moment, exited 1 and says the alert was sent."""
    last = read_state(Path(root) / "build" / "automations" / TASK / "last.json")
    if not isinstance(last, dict) or last.get("alert") != "sent" or last.get("exit") != 1:
        return False
    began, ended = _epoch(last.get("started")), _epoch(last.get("finished"))
    return began is not None and ended is not None and began - 1 <= raised_at <= ended + 1


def scheduled(*, runner=run_gh, root=ROOT, workflows=WORKFLOWS, branch="main", now=time.time, out=print):
    """The recto-ci-watch task. One rule: everything it finds worth an email (a red run, or `gh` unreachable for an
    hour) is kept in `pending` and makes it exit 1 on every poll until the runner records that poll's email as sent;
    only then is it forgotten, and never raised again. So a red found is never dropped by a later failed read, by
    another workflow's outage or by an email that did not leave.

    Every completed run in the last 10 of each workflow is judged, per attempt ("<run id>/<attempt>": a rerun keeps
    its id, and a cancelled or green attempt that is rerun and fails is a new red), so two reds between two polls are
    both reported and a later green hides neither. On the task's first run only the standing red is news."""
    path = Path(root) / REPORTED_FILE
    memory = read_state(path)
    memory = memory if isinstance(memory, dict) else {}
    # Until one poll has read everything, the task has no baseline: an older memory file with `reported` has one.
    first_time = not memory.get("baselined", "reported" in memory)
    # A bare run id from an older memory file stands for every attempt of that run.
    reported = [r for r in memory.get("reported", []) if isinstance(r, (int, str))]
    passed = [r for r in memory.get("passed", []) if isinstance(r, str)]
    pending = [p for p in memory.get("pending", []) if isinstance(p, dict) and isinstance(p.get("key"), str)]
    if pending and isinstance(memory.get("raised_at"), (int, float)) and alert_delivered(root, memory["raised_at"]):
        # Only what that email named is acknowledged by it. A memory file from before the names were kept names
        # none, and its pending items are simply raised once more.
        named = [k for k in memory.get("raised_keys", []) if isinstance(k, str)]
        reported += [p["key"] for p in pending if p["key"] in named]
        pending = [p for p in pending if p["key"] not in named]
    entries, unread, baseline = [], [], []
    for workflow in workflows:
        try:
            runs = gh_json(runner, ["run", "list", "--branch", branch, "--workflow", workflow, "--limit", str(LIMIT),
                                    "--json", FIELDS])
        except Unavailable as exc:
            unread.append(f"{workflow}: {exc}")
            continue
        try:
            entries.append(workflow_status(runner, workflow, branch))  # the summary lines for the log, nothing more
        except Unavailable as exc:
            unread.append(f"{workflow} summary: {exc}")
        # Every listed run is judged on its own, oldest first: one whose verdict cannot be read (a cancelled run's
        # jobs, for instance) is left for a later poll and takes nothing else with it.
        # standing is the newest run with a verdict when it is red: on the task's first run it alone is news.
        reds, standing = [], None
        for run in sorted((r for r in runs if isinstance(r, dict)), key=lambda r: str(r.get("createdAt") or "")):
            run_id = run.get("databaseId")
            key = f"{run_id}/{run.get('attempt') or 1}"
            if run.get("status") != "completed" or not isinstance(run_id, int):
                continue
            if key in passed or key in reported or run_id in reported or any(p["key"] == key for p in pending):
                continue  # judged on an earlier poll
            try:
                verdict, jobs = settle(runner, run)
            except Unavailable as exc:
                unread.append(f"{workflow} run {run_id}: {exc}")
                continue
            if verdict != "red":
                passed.append(key)
                standing = None if verdict == "green" else standing
                continue
            standing = key
            reds.append({"key": key, "workflow": workflow, "url": run.get("url"),
                         "sha": str(run.get("headSha") or "")[:12],
                         "failed_jobs": [str(job.get("name")) for job in jobs]})
        for item in reds:
            if first_time and item["key"] != standing:
                baseline.append(item["key"])  # history from before this task first ran: only the standing red is news
            else:
                pending.append(item)
    if unread:
        first = memory.get("unknown_since") or now()
        memory["unknown_since"] = first
        out(f"ci-status unknown for {int((now() - first) // 60)} minutes: {'; '.join(unread)}")
        outage = f"outage/{int(first)}"
        if (now() - first >= UNKNOWN_ALERT_SECONDS and outage not in reported
                and not any(p["key"] == outage for p in pending)):
            pending.append({"key": outage, "workflow": "gh", "url": None, "sha": None, "failed_jobs": [],
                            "note": f"GitHub could not be read for over {UNKNOWN_ALERT_SECONDS // 60} minutes"})
    else:
        memory.pop("unknown_since", None)
    memory.pop("unknown_reported", None)  # an older memory file's flag; the outage is a pending item now
    # The first run's baseline (the history it skips, and the runs it found not red) is kept only once everything was
    # read; until then the next poll judges main from the start again, so history is skipped once, and whole.
    whole = not (first_time and unread)
    if whole:
        reported += baseline
    memory.update(reported=reported[-400:], passed=passed[-400:] if whole else [], pending=pending, baselined=whole)
    shown = pending[:ALERT_BATCH]
    if pending:
        memory.update(raised_at=now(), raised_keys=[p["key"] for p in shown])
    else:
        memory.pop("raised_at", None)
        memory.pop("raised_keys", None)
    write_state(path, memory)
    for entry in entries:
        out("\n".join(describe(entry)))
    # What the email must carry comes last, one line each, so it is inside the part of the log the runner sends.
    for item in shown:
        if item.get("note"):
            out(f"not yet reported by email: {item['note']}")
            continue
        jobs = f"; failed jobs: {', '.join(item['failed_jobs'])}" if item.get("failed_jobs") else ""
        out(f"red run on main: {item['workflow']} at {item.get('sha')} {item.get('url')}{jobs}")
    reds = [p for p in pending if not p.get("note")]
    if reds:
        later = f"; {len(pending) - len(shown)} more follow in the next email" if len(pending) > len(shown) else ""
        out(f"main has {len(reds)} red run(s) not yet reported by email: "
            f"{', '.join(sorted({p['workflow'] for p in reds}))} (each is reported until its email is sent, then never "
            f"again{later})")
    return 1 if pending else 0


def main(argv=None, runner=run_gh, root=ROOT, git=run_git):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", action="store_true", help="print the report as one JSON object")
    parser.add_argument("--branch", default="main", help="the branch whose runs are read (default main)")
    parser.add_argument("--workflow", action="append", dest="workflows",
                        help="a workflow file in .github/workflows/ (repeatable; default build.yml and plan.yml)")
    parser.add_argument("--wait", metavar="SHA", help="follow this pushed commit's runs to their end (exit 0, 1, 3, 4 or 5)")
    parser.add_argument("--since", metavar="SHA", help="with --wait, the branch's head before the push (default the commit's parent)")
    parser.add_argument("--timeout", type=int, default=60, help="minutes --wait follows the runs (default 60)")
    parser.add_argument("--poll", type=int, default=10, help="seconds between --wait's reads (default 10)")
    parser.add_argument("--gate", action="store_true", help="may a new row be claimed: the watch of origin/main's head")
    parser.add_argument("--brief", action="store_true", help="with --gate, one line")
    parser.add_argument("--scheduled", action="store_true", help="the recto-ci-watch task: exit 1 once per new red run")
    args = parser.parse_args(argv)
    if args.gate:
        try:
            code = gate(git=git, runner=runner, root=root, brief_line=args.brief)
        except Unavailable as exc:
            print(f"ci gate: unknown: {exc}")
            code = 3
        # One line for a session's start (the SessionStart hook): it informs, so it never fails the hook.
        return 0 if args.brief else code
    if args.scheduled:
        directory = Path(root) / ".github" / "workflows"
        try:
            return scheduled(runner=runner, root=root, workflows=[w for w in WORKFLOWS if (directory / w).is_file()],
                             branch=args.branch)
        except Unavailable as exc:
            # Its own memory could not be written: unknown (3), which the runner emails like any failed run.
            print(f"ci-status unknown: {exc}")
            return 3
    if args.wait:
        if (not re.fullmatch(r"[0-9a-f]{7,40}", args.wait) or (args.since and not re.fullmatch(r"[0-9a-f]{7,40}", args.since))
                or args.timeout < 1 or args.poll < 1):
            parser.error("--wait takes a commit sha, and --timeout and --poll positive numbers")
        try:
            # An abbreviated sha is accepted, and GitHub and the state files speak in full ones.
            sha = full_sha(git, args.wait)
            since = full_sha(git, args.since) if args.since else None
            return wait(sha, since=since, runner=runner, git=git, root=root, branch=args.branch,
                        timeout=args.timeout * 60, poll=args.poll)
        except Unavailable as exc:
            print(f"ci-status unknown: {exc}")
            return 3
    directory = Path(root) / ".github" / "workflows"
    workflows = args.workflows or [w for w in WORKFLOWS if (directory / w).is_file()]
    missing = [w for w in workflows if "/" in w or "\\" in w or not (directory / w).is_file()]
    if missing or not workflows or not args.branch.strip():
        parser.error(f"no such workflow in .github/workflows/: {', '.join(missing)}" if missing
                     else "no workflow to watch, or an empty --branch")
    try:
        result, code = report(runner, workflows, args.branch)
    except Unavailable as exc:
        if args.json:
            print(json.dumps({"branch": args.branch, "error": str(exc), "exit": 3}))
        else:
            print(f"ci-status unknown: {exc}")
        return 3
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for entry in result["workflows"]:
            print("\n".join(describe(entry)))
    return code


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
