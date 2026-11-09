#!/usr/bin/env python3
"""Report the CI state of main: the latest completed run of each watched workflow, and runs still in progress.

    python scripts/ci_status.py                  # build.yml and plan.yml on main
    python scripts/ci_status.py --json           # the same report as one JSON object
    python scripts/ci_status.py --workflow build.yml --branch main

For each workflow (by default `build.yml` and `plan.yml`, the ones in `.github/workflows/` whose results gate main) it
reads the last 10 runs with `gh run list` and reports the latest completed run (green, red or cancelled), its verdict,
and how many newer runs are still queued or in progress. A cancelled run carries no verdict (a newer push usually
superseded it), so the verdict is the latest completed run that was not cancelled; with none in the last 10 runs the
verdict is `none`. For a red verdict it names the failed jobs from `gh run view <id> --json jobs`.

Exit 0: no watched workflow is red. Exit 1: at least one is red (the workflow, the commit, the run's URL and its
failed jobs are printed). Exit 2: bad usage. Exit 3: `gh` is missing, not authenticated, or failed, so the state is
unknown; that is never treated as green. The red-first policy that consumes this is in the `process-phase` skill.
Stdlib only; the `gh` runner is injectable for tests.

    python scripts/ci_status.py --wait <sha>     # follow one pushed commit's runs to their end (D00 T04 §9)
    python scripts/ci_status.py --gate [--brief] # may a new row be claimed, or a section stamp?

`--wait` reads which watched workflows the push starts from each workflow's own `push` filter (`branches`, `paths`,
`paths-ignore`) and the files the push brought (`--since`, the branch's old head; else the commit's parent), follows
each through `gh run list --commit`, and writes its state to `build/ci-watch/<sha>.json` as it goes. The verdict over the commit's runs: any red exits 1 (every failed job named), else an
expected run not seen within 10 minutes exits 3 (unknown, never green), else a cancelled run exits 5 (main builds are
never cancelled by a newer push, so this is a manual or infrastructure stop), else running past `--timeout` exits 4,
else 0. `--gate` reads the file of the head of `origin/main`: 0 when its watch ended green, 1 when it ended red, 6
when it has not ended or there is none (start `--wait`); `--brief` prints the answer on one line and exits 0, for the
session's start hook. `--scheduled` (the `recto-ci-watch` task in `.conclave/automations/`) exits 1 once for each red
run not reported before, and once when `gh` has been unreachable for an hour, so the runner's failure email names it
once; it remembers what it reported in `build/automations/ci-watch-reported.json`.
"""
import argparse
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
FIELDS = "databaseId,headSha,status,conclusion,url,createdAt,event"
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
    decisive = next((r for r in completed if verdict_of(r) != "cancelled"), None)
    newer = [r for r in runs if r.get("status") != "completed"
             and (latest is None or str(r.get("createdAt") or "") > str(latest.get("createdAt") or ""))]
    entry = {"workflow": workflow, "verdict": verdict_of(decisive), "latest_completed": brief(latest),
             "latest_completed_state": verdict_of(latest), "verdict_run": brief(decisive), "in_progress": len(newer)}
    if entry["verdict"] == "red":
        entry["failed_jobs"] = failed_jobs(runner, decisive.get("databaseId"))
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
    done = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=60)
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


def write_state(path, state):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(state, indent=2), encoding="utf-8")
    os.replace(temporary, path)


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


def wait(sha, *, since=None, runner=run_gh, git=run_git, root=ROOT, workflows=WORKFLOWS, branch="main", timeout=3600,
         poll=60, clock=time.monotonic, sleep=time.sleep, out=print, pid=None):
    """Follows the watched workflows' runs on `sha` to their end; returns the exit code. See the module docstring."""
    directory = Path(root) / ".github" / "workflows"
    state_path = watch_file(root, sha)
    # GitHub filters a push by everything it brought (the branch's old head to its new one), not by its last commit.
    changed = [line.strip() for line in git(["diff", "--name-only", since or f"{sha}^", sha]).splitlines() if line.strip()]
    expected = {w for w in workflows if (directory / w).is_file() and starts_on(push_filter(directory / w), branch, changed)}
    started = clock()
    states = {w: {"state": "pending", "run": None, "failed_jobs": []} for w in expected}
    record = {"sha": sha, "since": since, "pid": pid if pid is not None else os.getpid(), "expected": sorted(expected), "workflows": states,
              "exit": None}
    write_state(state_path, record)
    while True:
        for workflow in workflows:
            if not (directory / workflow).is_file():
                continue
            runs = gh_json(runner, ["run", "list", "--workflow", workflow, "--commit", sha, "--limit", "20", "--json",
                                    WAIT_FIELDS])
            runs = [r for r in runs if isinstance(r, dict) and r.get("headSha") == sha and r.get("event") == "push"]
            if not runs:
                if workflow in expected and clock() - started >= APPEAR_SECONDS:
                    states[workflow] = {"state": "unknown", "run": None, "failed_jobs": [],
                                        "note": f"expected for this push, not seen in {APPEAR_SECONDS // 60} minutes"}
                continue
            latest = sorted(runs, key=lambda r: str(r.get("createdAt") or ""))[-1]
            entry = {"run": brief(latest), "failed_jobs": []}
            if latest.get("status") != "completed":
                entry["state"] = "pending"
            else:
                entry["state"] = {"green": "green", "cancelled": "cancelled"}.get(verdict_of(latest), "red")
                if entry["state"] == "red":
                    entry["failed_jobs"] = failed_jobs(runner, latest.get("databaseId"))
            states[workflow] = entry
        finished = all(s["state"] != "pending" for s in states.values())
        if finished or clock() - started >= timeout:
            code = verdict_of_commit(states)
            record["exit"] = code
            write_state(state_path, record)
            for workflow, entry in sorted(states.items()):
                run = entry.get("run") or {}
                out(f"{workflow}: {entry['state']} {run.get('url') or entry.get('note') or ''}".rstrip())
                for job in entry.get("failed_jobs", []):
                    out(f"  failed job: {job['name']} ({job['conclusion']}) {job['url'] or ''}".rstrip())
            if not states:
                out(f"no watched workflow starts on {sha[:12]} (its files match no push filter)")
            return code
        write_state(state_path, record)
        sleep(poll)


def gate(*, git=run_git, root=ROOT, brief_line=False, out=print):
    """0 when the watch of origin/main's head ended green, 1 red, 6 not ended or none."""
    head = git(["rev-parse", "origin/main"]).strip()
    state = read_state(watch_file(root, head))
    if not state or state.get("sha") != head:
        out(f"ci gate: no watch for origin/main {head[:12]}; start `python scripts/ci_status.py --wait {head}`")
        return 6
    code = state.get("exit")
    if code is None:
        alive = process_alive(state.get("pid"))
        out(f"ci gate: the watch of {head[:12]} is {'still running' if alive else 'gone before it ended; start it again'}")
        return 6
    if code == 0:
        out(f"ci gate: green at {head[:12]}")
        return 0
    reds = [f"{w} {e.get('state')} {(e.get('run') or {}).get('url') or ''}".strip()
            for w, e in sorted(state.get("workflows", {}).items()) if e.get("state") != "green"]
    out(f"ci gate: {'red' if code == 1 else 'not green'} at {head[:12]}: " + "; ".join(reds))
    if not brief_line:
        for w, e in sorted(state.get("workflows", {}).items()):
            for job in e.get("failed_jobs", []):
                out(f"  {w} failed job: {job['name']} {job['url'] or ''}".rstrip())
    return 1 if code == 1 else 6

REPORTED_FILE = Path("build") / "automations" / "ci-watch-reported.json"
UNKNOWN_ALERT_SECONDS = 3600


def scheduled(*, runner=run_gh, root=ROOT, workflows=WORKFLOWS, branch="main", now=time.time, out=print):
    """The recto-ci-watch task: exits 1 once for each red run not reported before (so the runner's failure email names
    it once), and once when gh has stayed unreachable for an hour; 0 otherwise."""
    path = Path(root) / REPORTED_FILE
    memory = read_state(path) or {}
    reported = [r for r in memory.get("reported", []) if isinstance(r, int)]
    try:
        result, code = report(runner, workflows, branch)
    except Unavailable as exc:
        first = memory.get("unknown_since") or now()
        memory["unknown_since"] = first
        alert = now() - first >= UNKNOWN_ALERT_SECONDS and not memory.get("unknown_reported")
        if alert:
            memory["unknown_reported"] = True
        write_state(path, memory)
        out(f"ci-status unknown for {int((now() - first) // 60)} minutes: {exc}")
        return 1 if alert else 0
    memory.pop("unknown_since", None)
    memory.pop("unknown_reported", None)
    new = []
    for entry in result["workflows"]:
        run = entry.get("verdict_run") or {}
        if entry["verdict"] == "red" and isinstance(run.get("databaseId"), int) and run["databaseId"] not in reported:
            new.append(entry)
            reported.append(run["databaseId"])
    memory["reported"] = reported[-200:]
    write_state(path, memory)
    for entry in result["workflows"]:
        out("\n".join(describe(entry)))
    if new:
        out(f"main is red: {', '.join(e['workflow'] for e in new)} (reported once)")
        return 1
    return 0


def main(argv=None, runner=run_gh, root=ROOT, git=run_git):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", action="store_true", help="print the report as one JSON object")
    parser.add_argument("--branch", default="main", help="the branch whose runs are read (default main)")
    parser.add_argument("--workflow", action="append", dest="workflows",
                        help="a workflow file in .github/workflows/ (repeatable; default build.yml and plan.yml)")
    parser.add_argument("--wait", metavar="SHA", help="follow this pushed commit's runs to their end (exit 0, 1, 3, 4 or 5)")
    parser.add_argument("--since", metavar="SHA", help="with --wait, the branch's head before the push (default the commit's parent)")
    parser.add_argument("--timeout", type=int, default=60, help="minutes --wait follows the runs (default 60)")
    parser.add_argument("--poll", type=int, default=60, help="seconds between --wait's reads (default 60)")
    parser.add_argument("--gate", action="store_true", help="may a new row be claimed: the watch of origin/main's head")
    parser.add_argument("--brief", action="store_true", help="with --gate, one line")
    parser.add_argument("--scheduled", action="store_true", help="the recto-ci-watch task: exit 1 once per new red run")
    args = parser.parse_args(argv)
    if args.gate:
        try:
            code = gate(git=git, root=root, brief_line=args.brief)
        except Unavailable as exc:
            print(f"ci gate: unknown: {exc}")
            code = 3
        # One line for a session's start (the SessionStart hook): it informs, so it never fails the hook.
        return 0 if args.brief else code
    if args.scheduled:
        directory = Path(root) / ".github" / "workflows"
        return scheduled(runner=runner, root=root, workflows=[w for w in WORKFLOWS if (directory / w).is_file()], branch=args.branch)
    if args.wait:
        if (not re.fullmatch(r"[0-9a-f]{7,40}", args.wait) or (args.since and not re.fullmatch(r"[0-9a-f]{7,40}", args.since))
                or args.timeout < 1 or args.poll < 1):
            parser.error("--wait takes a commit sha, and --timeout and --poll positive numbers")
        try:
            return wait(args.wait, since=args.since, runner=runner, git=git, root=root, branch=args.branch, timeout=args.timeout * 60,
                        poll=args.poll)
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
