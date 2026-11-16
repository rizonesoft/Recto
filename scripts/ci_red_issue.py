#!/usr/bin/env python3
"""Keep one GitHub issue open while main is red, from GitHub itself (D00 T04 section 13).

    python scripts/ci_red_issue.py reconcile --workflow build.yml

`.github/workflows/ci-red.yml` runs this when a run of a watched workflow (`build.yml`, `plan.yml`) ends on main, with
the workflow's own token. It never reads the run that woke it: it reads main's current verdict for that workflow by
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


def state_of(entry):
    """('red', run, jobs), ('cancelled', run, []), ('green', run, []) or ('none', {}, []) from a workflow_status."""
    if entry.get("verdict") == "red":
        return "red", entry.get("verdict_run") or {}, entry.get("failed_jobs") or []
    if entry.get("latest_completed_state") == "cancelled":
        return "cancelled", entry.get("latest_completed") or {}, []
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
        lines.append(f"- Failed job: {job.get('name')} ({job.get('conclusion')}) {job.get('url') or ''}".rstrip())
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


def main(argv=None, runner=ci_status.run_gh, out=print):
    parser = argparse.ArgumentParser(description="Keep one GitHub issue open while main is red (D00 T04 section 13).")
    sub = parser.add_subparsers(dest="command", required=True)
    rec = sub.add_parser("reconcile", help="make the workflow's ci-red issue match main's verdict")
    rec.add_argument("--workflow", required=True, help="the watched workflow's file, as build.yml or plan.yml")
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        return 2 if exc.code else 0
    # GitHub's workflow_run event names the file with its folder (.github/workflows/build.yml).
    workflow = Path(args.workflow).name
    try:
        reconcile(workflow, runner=runner, out=out)
    except Usage as exc:
        out(f"ci-red: {exc}")
        return 2
    except ci_status.Unavailable as exc:
        out(f"ci-red: unknown, nothing changed: {exc}")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
