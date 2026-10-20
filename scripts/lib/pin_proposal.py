"""How the pin checks act and tell the operator, shared by every `scripts/check-*.py` pin script.

On GitHub (the daily `.github/workflows/pins.yml`): `propose_pr` opens a pull request for a repository pin bump and
dispatches CI on its branch; `file_issue` opens one deduplicated issue for an owner decision, and `resolve_issues`
closes it once the finding is gone. GitHub's own notifications tell the operator.

On this machine (`.conclave/automations`, for checks only this PC can see): `notice` emails an "available" notice once
per new version, and `notify` sends through the automations' alert sender. `propose` is the local equivalent of a PR:
it adds a temporary git worktree on a new branch from main, applies the rewrite there, runs the caller's checks,
commits, removes the worktree and emails the operator which branch to merge; a proposal is finished only when the ref
`refs/recto-pin/<key>` exists. It serves the manual `update` modes.

Stdlib only.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0  # CREATE_NO_WINDOW: a scheduled run opens no console


def git(*args, cwd, check=True):
    result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                            errors="replace", creationflags=NO_WINDOW)
    if check and result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {(result.stderr or result.stdout).strip()}")
    return result


def notify(root, subject, body, dry_run=False, alert_module=None):
    """Email the operator through the automations' sender (the same vault key, SMTP2GO path and "<Folder> automations"
    From line as the failure alerts), to the address `.conclave/automations/alert.py` names."""
    if dry_run:
        print(f"would email: {subject}\n{body}")
        return
    automations = Path(root) / ".conclave" / "automations"
    if alert_module is None:
        sys.path.insert(0, str(automations))
        import alert as alert_module
    folder = json.loads((automations / "tasks.json").read_text(encoding="utf-8"))["folder"]
    alert_module.send(alert_module.key(), subject, body, alert_module.sender(folder))


def notice(*, root, key, subject, body, dry_run=False, mail=notify):
    """Email an "update available" notice once per key; return 0, or 1 when it could not be sent.

    The key names the new version, so each new release is announced exactly once. The record is the ref
    `refs/recto-notice/<key>` on main's current commit, written only after the email went."""
    root = Path(root)
    done = f"refs/recto-notice/{key}"
    if git("rev-parse", "--verify", "--quiet", done, cwd=root, check=False).returncode == 0:
        print(f"{key} was announced before ({done})")
        return 0
    try:
        mail(root, subject, body, dry_run)
    except Exception as exc:  # any send failure: the operator was not told, so the next run tries again
        print(f"could not email the notice ({type(exc).__name__}: {exc}); the next run will try again", file=sys.stderr)
        return 1
    if dry_run:
        return 0
    marked = git("update-ref", done, "main", cwd=root, check=False)
    if marked.returncode != 0:
        print(f"emailed {key}, but could not record {done}: {(marked.stderr or marked.stdout).strip()}", file=sys.stderr)
        return 1
    print(f"announced {key} to the operator")
    return 0


BOT = ("github-actions[bot]", "41898282+github-actions[bot]@users.noreply.github.com")
LABELS = {"dependencies": "0366d6", "pins": "5319e7"}
CI_WORKFLOWS = ("build.yml", "plan.yml")


def gh(*args, cwd):
    """Run the GitHub CLI (authenticated by GH_TOKEN in the workflow); raise with its message on failure."""
    result = subprocess.run(["gh", *args], cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                            errors="replace", creationflags=NO_WINDOW)
    if result.returncode != 0:
        raise RuntimeError(f"gh {' '.join(args[:3])}: {(result.stderr or result.stdout).strip()}")
    return result.stdout


def ensure_labels(names, root, run_gh=gh):
    for name in names:
        run_gh("label", "create", name, "--color", LABELS.get(name, "bfd4f2"), "--force", cwd=root)


def propose_pr(*, root, branch, rewrite, checks, title, body, run_gh=gh):
    """Open a pull request for a pin bump from the GitHub pins workflow; return the exit code.

    The checkout is main. A branch that already has a pull request (open, merged or closed) is never proposed again,
    so closing a PR declines that version. The bump is committed as github-actions[bot] on `branch`, pushed, opened as
    a PR, and the CI workflows are dispatched on the branch, because a PR opened with the workflow token does not start
    other workflows by itself (workflow_dispatch is the documented exception)."""
    root = Path(root)
    existing = json.loads(run_gh("pr", "list", "--head", branch, "--state", "all", "--json", "number,state", cwd=root) or "[]")
    if existing:
        print(f"{branch} was proposed before as PR #{existing[0]['number']} ({existing[0]['state'].lower()})")
        return 0
    if git("ls-remote", "--exit-code", "--heads", "origin", branch, cwd=root, check=False).returncode == 0:
        git("push", "origin", "--delete", branch, cwd=root)
        print(f"discarded the unfinished remote {branch} from an earlier run")
    git("switch", "-c", branch, cwd=root)
    try:
        changed = rewrite(root)
        if not changed:
            print(f"main already holds the target for {branch}; nothing to propose")
            return 0
        problems = checks(root)
        if problems:
            print("checks failed, nothing proposed:\n- " + "\n- ".join(problems), file=sys.stderr)
            return 1
        git("add", *changed, cwd=root)
        git("-c", f"user.name={BOT[0]}", "-c", f"user.email={BOT[1]}", "commit", "-q", "-m", title, "-m", body, cwd=root)
        git("push", "-u", "origin", branch, cwd=root)
        ensure_labels(("dependencies", "pins"), root, run_gh)
        url = run_gh("pr", "create", "--base", "main", "--head", branch, "--title", title, "--body", body,
                     "--label", "dependencies", "--label", "pins", cwd=root).strip()
        for workflow in CI_WORKFLOWS:
            run_gh("workflow", "run", workflow, "--ref", branch, cwd=root)
        print(f"opened {url} and started {', '.join(CI_WORKFLOWS)} on {branch}")
        return 0
    finally:
        git("switch", "main", cwd=root, check=False)
        git("branch", "-D", branch, cwd=root, check=False)


def file_issue(*, root, check, title, body, run_gh=gh):
    """Open one issue per finding for an owner decision; return 0. Deduplicated by title within the check's label:
    an open issue with this title is left alone, a closed one means the owner dismissed it, and older open issues of the
    same check are closed as superseded."""
    label = f"pins:{check}"
    ensure_labels(("pins", label), root, run_gh)
    issues = json.loads(run_gh("issue", "list", "--label", label, "--state", "all", "--limit", "100",
                               "--json", "number,title,state", cwd=root) or "[]")
    if any(i["title"] == title for i in issues):
        print(f"issue already filed: {title}")
        return 0
    for issue in issues:
        if issue["state"] == "OPEN":
            run_gh("issue", "close", str(issue["number"]), "--comment", f"Superseded by a newer finding: {title}", cwd=root)
    url = run_gh("issue", "create", "--title", title, "--body", body, "--label", "pins", "--label", label, cwd=root).strip()
    print(f"filed {url}")
    return 0


def resolve_issues(*, root, check, message, run_gh=gh):
    """Close every open issue of the check once its finding is gone; return 0."""
    label = f"pins:{check}"
    issues = json.loads(run_gh("issue", "list", "--label", label, "--state", "open", "--json", "number", cwd=root) or "[]")
    for issue in issues:
        run_gh("issue", "close", str(issue["number"]), "--comment", f"Resolved: {message}", cwd=root)
        print(f"closed #{issue['number']}: {message}")
    return 0


def build_passes(tree, build=subprocess.run):
    """The problems from running build.cmd in the worktree; empty when it passed."""
    if not (Path(tree) / "build.cmd").is_file():
        return ["main has no build.cmd, so the proposal cannot be built"]
    result = build(["cmd", "/c", "build.cmd"], cwd=tree, capture_output=True, text=True, creationflags=NO_WINDOW)
    return [] if result.returncode == 0 else [f"build.cmd failed: {(result.stdout or '')[-400:]}"]


def _drop(branch, root):
    result = git("branch", "-D", branch, cwd=root, check=False)
    return [] if result.returncode == 0 else [f"could not delete {branch}: {(result.stderr or result.stdout).strip()}"]


def propose(*, root, key, branch, rewrite, checks, message, subject, body, dry_run=False, mail=notify):
    """Propose a change; return the process exit code (0 proposed or already proposed, 1 failed).

    rewrite(tree) changes files in the worktree and returns the repository-relative paths it changed, or an empty
    list when main already holds the target (nothing to propose). checks(tree) returns problems; empty means commit.
    body is a function of the commit id, so the email can name it.
    """
    root = Path(root)
    done = f"refs/recto-pin/{key}"
    if git("rev-parse", "--verify", "--quiet", done, cwd=root, check=False).returncode == 0:
        print(f"{key} was proposed and emailed before ({done})")
        return 0
    leftover = git("rev-parse", "--verify", "--quiet", f"refs/heads/{branch}", cwd=root, check=False).returncode == 0
    if dry_run:
        print((f"would discard the unfinished {branch}, then " if leftover else "would ")
              + f"create {branch} from main, apply the change, check it, commit, and email the operator")
        return 1
    if leftover:
        left = _drop(branch, root)
        if left:
            print("an unfinished proposal is in the way; repair by hand:\n- " + "\n- ".join(left), file=sys.stderr)
            return 1
        print(f"discarded the unfinished {branch} from an earlier run")
    outcome, commit, cleanup = None, None, []
    scratch = root / "build" if (root / "build").is_dir() else None
    with tempfile.TemporaryDirectory(prefix="recto-pin-", dir=scratch) as tmp:
        tree = Path(tmp) / "tree"
        git("worktree", "add", "-b", branch, str(tree), "main", cwd=root)
        try:
            changed = rewrite(tree)
            if not changed:
                print(f"main already holds the target for {key}; nothing to propose")
                outcome = 0
            else:
                problems = checks(tree)
                if problems:
                    print("checks failed, nothing proposed:\n- " + "\n- ".join(problems), file=sys.stderr)
                    outcome = 1
                else:
                    git("add", *changed, cwd=tree)
                    git("commit", "-q", *sum((["-m", part] for part in message), []), cwd=tree)
                    commit = git("rev-parse", "HEAD", cwd=tree).stdout.strip()
        finally:
            removed = git("worktree", "remove", "--force", str(tree), cwd=root, check=False)
            if removed.returncode != 0:
                cleanup.append(f"could not remove the worktree {tree}: {(removed.stderr or removed.stdout).strip()}")
            if commit is None:
                cleanup += _drop(branch, root)
    if cleanup:
        print("cleanup failed; repair by hand before the next run:\n- " + "\n- ".join(cleanup), file=sys.stderr)
        return 1
    if outcome is not None:
        return outcome
    try:
        mail(root, subject, body(commit), dry_run)
    except Exception as exc:  # any send failure: the operator was not told, so the proposal is not finished
        left = _drop(branch, root)
        print(f"could not email the proposal ({type(exc).__name__}: {exc}); "
              + (f"{branch} was removed so the next run proposes it again" if not left else "; ".join(left)),
              file=sys.stderr)
        return 1
    marked = git("update-ref", done, commit, cwd=root, check=False)
    if marked.returncode != 0:
        print(f"proposed and emailed {key}, but could not record {done}: {(marked.stderr or marked.stdout).strip()}; "
              "the next run will propose it again", file=sys.stderr)
        return 1
    print(f"proposed {key} on {branch} ({commit[:12]}) and emailed the operator")
    return 0
