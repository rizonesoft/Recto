#!/usr/bin/env python3
"""Keep one GitHub issue open while main is red, from GitHub itself (D00 T04 section 13).

    python scripts/ci_red_issue.py reconcile --workflow build.yml
    python scripts/ci_red_issue.py reconcile --all [--wait-for <sha>]

`.github/workflows/ci-red.yml` runs `reconcile --all` with the workflow's own token when a run of a watched workflow
(`build.yml`, `plan.yml`) ends on main, and on a push to main that changes either file: GitHub names a run of a
workflow file it cannot parse after the file's path and wakes no `workflow_run` workflow for it, and such a file can
only arrive by a push that changes it, so that push run waits (`--wait-for`, at most WAIT_SECONDS) until the commit's
watched runs are listed and reconciles with them. It never reads the run that woke it: it reads main's current verdict for that workflow by
the rule `scripts/ci_status.py` applies (the newest completed run that is not a rebuild of an older commit; a
cancelled run carries no verdict, a timed-out one is red), so the order runs finish in cannot change the outcome, and
makes the issue `main is red: <workflow>` (label `ci-red`, assigned to the repository owner) match it:

- red, or a newest run cancelled with no verdict after it: open the issue when none is open, else add one comment
  naming the run when the issue does not name it yet;
- green: close the open issue with a comment naming the green run;
- no completed run: nothing.

GitHub then notifies the owner as the assignee; whether that arrives as email follows the owner's own notification
settings. The deliberate red that proves this alert (`plan.yml` with `fail_on_purpose`) counts here and nowhere else.

Exit 0 when the issue matches main's verdict, 2 for bad usage or a workflow that is not watched, 3 when `gh` fails or
answers nonsense (the ci-red run is then red, so the failure shows in the Actions tab). Stdlib only; `gh` is
injectable for tests.
"""
import argparse
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ci_status  # noqa: E402  (the verdict rule this alert shares with the local watch)

LABEL = "ci-red"
LABEL_COLOR = "B60205"
LABEL_TEXT = "main is red: opened and closed by .github/workflows/ci-red.yml"
BRANCH = "main"
WAIT_SECONDS = 45  # the longest a push reconcile waits for the commit's watched runs to be listed
POLL_SECONDS = 5


class Usage(Exception):
    """Bad usage (exit 2)."""


def title_for(workflow):
    return f"main is red: {workflow}"


def marker(run):
    """The tag that says an issue or comment already names this run's attempt."""
    return f"<!-- ci-red run={run.get('databaseId')}/{run.get('attempt') or 1} -->"


def gh_text(runner, args):
    """Run a gh command whose output is not JSON; Unavailable when it fails."""
    try:
        code, out, err = runner(args)
    except FileNotFoundError:
        raise ci_status.Unavailable("gh is not installed or not on PATH") from None
    if code != 0:
        reason = (err or out or "").strip().splitlines()
        raise ci_status.Unavailable(f"gh {' '.join(args[:2])} exited {code}: "
                                    f"{(reason[0] if reason else 'no output')[:ci_status.MAX_MESSAGE]}")
    return out


def gh_with_body(runner, args, body):
    """Run a gh issue command with its Markdown body in a file (`--body-file`): a body of several lines passed as one
    argument is cut at its first line break by a Windows command-line launcher (gh.cmd and the like)."""
    handle, path = tempfile.mkstemp(prefix="ci-red-", suffix=".md")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            stream.write(body)
        return gh_text(runner, [*args, "--body-file", path])
    finally:
        os.unlink(path)


def problem_jobs(runner, run):
    """Every job of the run that did not pass, whatever the run as a whole concluded: failed, cancelled, and cancelled
    by its `timeout-minutes` (said in the job's check-run annotation, as ci_status reads it), each with its own state.
    A red build can hold a cancelled sibling and a cancelled one a timed-out job; the issue names them all."""
    view = ci_status.gh_json(runner, ci_status.view_jobs(run.get("databaseId"), run.get("attempt")))
    jobs = view.get("jobs") if isinstance(view, dict) else None
    if not isinstance(jobs, list):
        raise ci_status.Unavailable("gh run view returned no job list")
    timed_out = {j.get("name"): j for j in ci_status.timed_out_jobs(runner, run)}
    found = []
    for job in jobs:
        if not isinstance(job, dict) or job.get("status") not in (None, "completed"):
            continue
        conclusion = (job.get("conclusion") or "").lower()
        if job.get("name") in timed_out and conclusion == "cancelled":
            found.append(timed_out[job.get("name")])
        elif conclusion in ("failure", "cancelled", "timed_out", "startup_failure", "action_required"):
            found.append({"name": job.get("name"), "conclusion": job.get("conclusion"), "url": job.get("url")})
    return found[:ci_status.MAX_JOBS]


def state_of(entry):
    """('red', run, jobs), ('cancelled', run, []), ('green', run, []) or ('none', {}, []) from a workflow_status. The
    newest completed run decides first: one cancelled with nothing after it is named even behind an older red, since
    main's newest commit then has no verdict; a timed-out one is already red in `latest_completed_state`."""
    if entry.get("latest_completed_state") == "cancelled":
        return "cancelled", entry.get("latest_completed") or {}, []
    if entry.get("verdict") == "red":
        return "red", entry.get("verdict_run") or {}, entry.get("failed_jobs") or []
    if entry.get("verdict") == "green":
        return "green", entry.get("verdict_run") or {}, []
    return "none", {}, []


def describe(workflow, state, run, jobs):
    sha = str(run.get("headSha") or "")
    lines = [marker(run),
             f"`{workflow}` on `{BRANCH}` is {'red' if state == 'red' else 'cancelled, with no verdict after it'}.",
             "",
             f"- Commit: `{sha[:12]}` ({sha})",
             f"- Run: {run.get('url')} (attempt {run.get('attempt') or 1}, {run.get('event')})"]
    for job in jobs:
        conclusion = (job.get("conclusion") or "").lower()
        kind = ("Cancelled job" if conclusion == "cancelled" else
                "Timed-out job" if conclusion.startswith("timed") else "Failed job")
        lines.append(f"- {kind}: {job.get('name')} ({job.get('conclusion')}) {job.get('url') or ''}".rstrip())
    lines += ["", "This issue closes itself on the next green run of this workflow on main."]
    return "\n".join(lines)


def open_issue(runner, workflow):
    issues = ci_status.gh_json(runner, ["issue", "list", "--state", "open", "--label", LABEL, "--limit", "50",
                                        "--json", "number,title"])
    if not isinstance(issues, list):
        raise ci_status.Unavailable("gh issue list did not return a list")
    mine = sorted((i for i in issues if isinstance(i, dict) and i.get("title") == title_for(workflow)),
                  key=lambda i: i.get("number") or 0)
    return mine[0] if mine else None


def names_run(runner, number, run):
    view = ci_status.gh_json(runner, ["issue", "view", str(number), "--json", "body,comments"])
    if not isinstance(view, dict):
        raise ci_status.Unavailable("gh issue view did not return an issue")
    texts = [view.get("body") or ""] + [c.get("body") or "" for c in view.get("comments") or [] if isinstance(c, dict)]
    return any(marker(run) in text for text in texts)


def owner(runner):
    repo = ci_status.gh_json(runner, ["repo", "view", "--json", "owner"])
    login = (repo.get("owner") or {}).get("login") if isinstance(repo, dict) else None
    if not login:
        raise ci_status.Unavailable("gh repo view named no owner")
    return login


def reconcile(workflow, runner=ci_status.run_gh, out=print):
    """Make the workflow's ci-red issue match main's verdict; returns what it did."""
    if workflow not in ci_status.WORKFLOWS:
        raise Usage(f"{workflow} is not a watched workflow ({', '.join(ci_status.WORKFLOWS)})")
    entry = ci_status.workflow_status(runner, workflow, BRANCH, include_proof=True)
    state, run, jobs = state_of(entry)
    if state in ("red", "cancelled"):
        jobs = problem_jobs(runner, run)
    issue = open_issue(runner, workflow)
    if state in ("red", "cancelled"):
        body = describe(workflow, state, run, jobs)
        if issue is None:
            gh_text(runner, ["label", "create", LABEL, "--force", "--color", LABEL_COLOR, "--description", LABEL_TEXT])
            gh_with_body(runner, ["issue", "create", "--title", title_for(workflow), "--label", LABEL,
                                  "--assignee", owner(runner)], body)
            done = f"opened: {title_for(workflow)} for {run.get('url')}"
        elif names_run(runner, issue["number"], run):
            done = f"unchanged: issue #{issue['number']} already names {run.get('url')}"
        else:
            gh_with_body(runner, ["issue", "comment", str(issue["number"])], body)
            done = f"commented: issue #{issue['number']} on {run.get('url')}"
    elif state == "green" and issue is not None:
        gh_text(runner, ["issue", "close", str(issue["number"]), "--comment",
                         f"Green again: {run.get('url')} at `{str(run.get('headSha') or '')[:12]}`."])
        done = f"closed: issue #{issue['number']} by {run.get('url')}"
    else:
        done = f"nothing to do: {workflow} is {state} and " + ("no issue is open" if issue is None else
                                                                 f"issue #{issue['number']} is open")
    out(f"ci-red: {done}")
    return done


def wait_for(sha, runner, *, sleep, clock, out=print):
    """Wait, at most WAIT_SECONDS, until every watched workflow lists a run of `sha` (any state): a run of a file
    GitHub cannot parse is listed, already failed, within seconds of the push. A workflow the push did not start never
    lists one, so the wait ends at the limit and the reconcile goes on with what is listed."""
    began = clock()
    while True:
        missing = []
        for workflow in ci_status.WORKFLOWS:
            runs = ci_status.gh_json(runner, ["run", "list", "--workflow", workflow, "--commit", sha, "--limit", "5",
                                              "--json", "databaseId"])
            if not (isinstance(runs, list) and runs):
                missing.append(workflow)
        if not missing:
            return
        if clock() - began >= WAIT_SECONDS:
            out(f"ci-red: after {WAIT_SECONDS} s no run of {sha[:12]} is listed for {', '.join(missing)}; "
                f"reconciling with what is")
            return
        sleep(POLL_SECONDS)


def main(argv=None, runner=ci_status.run_gh, out=print, sleep=None, clock=None):
    import time
    parser = argparse.ArgumentParser(description="Keep one GitHub issue open while main is red (D00 T04 section 13).")
    sub = parser.add_subparsers(dest="command", required=True)
    rec = sub.add_parser("reconcile", help="make the ci-red issues match main's verdict")
    which = rec.add_mutually_exclusive_group(required=True)
    which.add_argument("--workflow", help="one watched workflow's file, as build.yml or plan.yml")
    which.add_argument("--all", action="store_true", help="every watched workflow")
    rec.add_argument("--wait-for", default="", help="a pushed commit whose watched runs to wait for first")
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        return 2 if exc.code else 0
    # GitHub's workflow_run event names the file with its folder (.github/workflows/build.yml).
    workflows = list(ci_status.WORKFLOWS) if args.all else [Path(args.workflow).name]
    unknown = []
    try:
        if args.wait_for:
            wait_for(args.wait_for, runner, sleep=sleep or time.sleep, clock=clock or time.monotonic, out=out)
    except ci_status.Unavailable as exc:
        unknown.append(f"waiting for {args.wait_for[:12]}: {exc}")
    for workflow in workflows:
        try:
            reconcile(workflow, runner=runner, out=out)
        except Usage as exc:
            out(f"ci-red: {exc}")
            return 2
        except ci_status.Unavailable as exc:  # each workflow on its own: one outage never hides the other's red
            unknown.append(f"{workflow}: {exc}")
    if unknown:
        out(f"ci-red: unknown, nothing changed for {'; '.join(unknown)}")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
