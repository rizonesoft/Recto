#!/usr/bin/env python3
"""Keep the WinUI 3 pin on the latest stable release (owner rule, 2026-10-20).

    python scripts/check-winui-pin.py check              # exit 1 when a newer stable release exists
    python scripts/check-winui-pin.py update             # propose it: branch winui/<version>, then email
    python scripts/check-winui-pin.py update --dry-run   # say what update would do; change and send nothing

The pin is the `Microsoft.WindowsAppSDK` PackageReference version in `src/app/Recto.vcxproj` (foundation plan D4). A stable release is a GitHub release
of the microsoft-ui-xaml repository tagged `winui3/release/<major>.<minor>.<patch>`, not marked a prerelease and
without "experimental" in its tag or name. The newest such release must also be published on NuGet under the same version.

`update` never touches the operator's checkout or main. It adds a temporary git worktree on a new branch
`winui/<version>` from main, rewrites only the pinned version there, runs the checks (the file parses, NuGet has the
version, and `build.cmd` (x64 and ARM64, Debug and Release, then the x64 tests) passes), commits, removes the worktree and emails the operator
which branch to merge. A failed check removes the branch and exits 1, so the scheduled run reports the failure
through the automations alert. A release whose proposal finished before is not proposed again.
main, not the checkout, decides: when main already pins the release, nothing is proposed. A proposal is finished only
when the ref refs/winui-pin/<version> exists, written after the checks passed, the commit landed and the email went;
a winui/<version> branch without it is an unfinished attempt that the next run discards and redoes, and a branch that
cannot be discarded fails the run. The pin on main must also be published on NuGet. A pin that is not an exact stable version, or is ahead of the newest stable
release, fails the check.
Exit 0 current or proposed, 1 newer release pending or a check failed, 2 the releases or NuGet could not be read.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PIN_FILE = "src/app/Recto.vcxproj"
PROJECT = ROOT / PIN_FILE
PACKAGE = "Microsoft.WindowsAppSDK"
REPOSITORY = "microsoft/microsoft-ui-xaml"
RELEASES = f"https://api.github.com/repos/{REPOSITORY}/releases?per_page=100"
NUGET = f"https://api.nuget.org/v3-flatcontainer/{PACKAGE.lower()}/index.json"
TAG = re.compile(r"^winui3/release/(\d+)\.(\d+)\.(\d+)$")
STABLE = re.compile(r"^\d+\.\d+\.\d+$")
BRANCH = "winui/{version}"
# Written only after the proposal's checks passed, it was committed and the operator was emailed. A branch without it
# is an unfinished attempt, never a proposal.
DONE = "refs/winui-pin/{version}"
NO_WINDOW = 0x08000000 if sys.platform == "win32" else 0  # CREATE_NO_WINDOW: a scheduled run opens no console


class Unreadable(Exception):
    pass


def fetch_json(url, opener=urllib.request.urlopen):
    request = urllib.request.Request(url, headers={"Accept": "application/json", "User-Agent": "recto-winui-pin"})
    try:
        with opener(request, timeout=60) as response:
            return json.loads(response.read().decode("utf-8"))
    except Exception as exc:  # any network or decode failure means the source could not be read
        raise Unreadable(f"{url.split('?')[0]}: {type(exc).__name__}: {exc}") from None


def latest_stable(releases):
    """The newest stable release as (version text, release), or None. Experimental and prerelease tags never count."""
    best = None
    for release in releases:
        if not isinstance(release, dict):
            continue
        tag = str(release.get("tag_name", ""))
        match = TAG.match(tag)
        name = str(release.get("name") or "")
        # The tag pattern already refuses a "-experimental" suffix; the name catches a release labelled experimental
        # under a plain version tag.
        if not match or release.get("prerelease") or release.get("draft") or "experimental" in name.lower():
            continue
        key = tuple(int(n) for n in match.groups())
        if best is None or key > best[0]:
            best = (key, release)
    return (".".join(map(str, best[0])), best[1]) if best else None


def pinned_text(text, where=PIN_FILE):
    root = ET.fromstring(text)
    # MSBuild elements carry the msbuild/2003 namespace, so match the local name.
    hits = [e for e in root.iter() if e.tag.rsplit("}", 1)[-1] == "PackageReference" and e.get("Include") == PACKAGE]
    if len(hits) != 1 or not hits[0].get("Version"):
        raise ValueError(f"{where}: needs exactly one PackageReference for {PACKAGE} with a Version")
    return hits[0].get("Version")


def pinned(project_path=PROJECT):
    return pinned_text(Path(project_path).read_text(encoding="utf-8"), project_path)


def main_pin(root=ROOT):
    """The pin on main: main is what a proposal changes, so main, never the checkout, is the baseline."""
    shown = git("show", f"main:{PIN_FILE}", cwd=root, check=False)
    if shown.returncode != 0:
        raise ValueError(f"cannot read {PIN_FILE} on main: {(shown.stderr or shown.stdout).strip()}")
    return pinned_text(shown.stdout, f"main:{PIN_FILE}")


def repinned(text, old, new):
    """The project text with only the pinned version changed, so formatting and comments survive.

    It rewrites the reference pinned_text reads: the one live (uncommented) PackageReference whose Include names the
    package, in either attribute order or quote style."""
    comments = [m.span() for m in re.finditer(r"<!--.*?-->", text, re.S)]
    include = re.compile(r"\bInclude\s*=\s*([\"'])" + re.escape(PACKAGE) + r"\1")
    tags = [m for m in re.finditer(r"<PackageReference\b[^>]*>", text)
            if include.search(m.group(0)) and not any(a <= m.start() < b for a, b in comments)]
    if len(tags) != 1:
        raise ValueError(f"needs exactly one live PackageReference for {PACKAGE}, found {len(tags)}")
    tag = tags[0]
    version = re.compile(r"(\bVersion\s*=\s*([\"']))" + re.escape(old) + r"\2")
    changed, count = version.subn(lambda m: m.group(1) + new + m.group(2), tag.group(0))
    if count != 1:
        raise ValueError(f"the {PACKAGE} pin {old} was not found on its PackageReference")
    return text[:tag.start()] + changed + text[tag.end():]


def newer(version, than):
    return tuple(int(n) for n in version.split(".")) > tuple(int(n) for n in than.split("."))


def git(*args, cwd=ROOT, check=True):
    result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                            creationflags=NO_WINDOW)
    if check and result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)}: {(result.stderr or result.stdout).strip()}")
    return result


def checks(tree, version, nuget_versions, build=subprocess.run):
    """Problems with the proposed pin; an empty list means it may be committed."""
    problems = []
    try:
        if pinned(tree / PIN_FILE) != version:
            problems.append("the rewritten file does not pin the new version")
    except (ET.ParseError, ValueError) as exc:
        problems.append(f"{PIN_FILE}: {exc}")
    if version not in nuget_versions:
        problems.append(f"NuGet has no {PACKAGE} {version}")
    if not (tree / "build.cmd").is_file():
        problems.append("main has no build.cmd, so the proposal cannot be built")
    else:
        result = build(["cmd", "/c", "build.cmd"], cwd=tree, capture_output=True, text=True, creationflags=NO_WINDOW)
        if result.returncode != 0:
            problems.append(f"build.cmd failed: {(result.stdout or '')[-400:]}")
    return problems


def notify(subject, body, dry_run=False):
    """Email the operator through the automations' sender (the same vault key and SMTP2GO path as the alerts)."""
    if dry_run:
        print(f"would email: {subject}\n{body}")
        return
    sys.path.insert(0, str(ROOT / ".conclave" / "automations"))
    import alert
    alert.send(alert.key(), subject, body)


def drop(branch, root):
    """Delete a branch that is not a finished proposal; the problems, empty when it is gone."""
    result = git("branch", "-D", branch, cwd=root, check=False)
    return [] if result.returncode == 0 else [f"could not delete {branch}: {(result.stderr or result.stdout).strip()}"]


def update(version, release, nuget_versions, root=ROOT, dry_run=False, build=subprocess.run, mail=notify):
    branch, done = BRANCH.format(version=version), DONE.format(version=version)
    if git("rev-parse", "--verify", "--quiet", done, cwd=root, check=False).returncode == 0:
        print(f"release {version} was proposed and emailed before ({done})")
        return 0
    leftover = git("rev-parse", "--verify", "--quiet", f"refs/heads/{branch}", cwd=root, check=False).returncode == 0
    if dry_run:
        print((f"would discard the unfinished {branch}, then " if leftover else "would ")
              + f"create {branch} from main with {PACKAGE} {version}, check it, commit, and email the operator")
        return 1
    if leftover:
        left = drop(branch, root)
        if left:
            print("an unfinished proposal is in the way; repair by hand:\n- " + "\n- ".join(left), file=sys.stderr)
            return 1
        print(f"discarded the unfinished {branch} from an earlier run")
    outcome, old, commit, cleanup = None, None, None, []
    with tempfile.TemporaryDirectory(prefix="winui-pin-", dir=root / "build" if (root / "build").is_dir() else None) as tmp:
        tree = Path(tmp) / "tree"
        git("worktree", "add", "-b", branch, str(tree), "main", cwd=root)
        try:
            project = tree / PIN_FILE
            old = pinned(project)
            if not STABLE.match(old):
                print(f"main pins {PACKAGE} {old}, which is not an exact stable version", file=sys.stderr)
                outcome = 1
            elif not newer(version, old):
                print(f"main already pins {PACKAGE} {old}; release {version} needs no proposal")
                outcome = 0
            else:
                project.write_text(repinned(project.read_text(encoding="utf-8"), old, version), encoding="utf-8", newline="")
                problems = checks(tree, version, nuget_versions, build)
                if problems:
                    print("checks failed, nothing proposed:\n- " + "\n- ".join(problems), file=sys.stderr)
                    outcome = 1
                else:
                    git("add", PIN_FILE, cwd=tree)
                    git("commit", "-q", "-m", f"deps: pin WinUI 3 (Windows App SDK) {version}",
                        "-m", f"Newest stable release {release.get('tag_name')} published "
                              f"{str(release.get('published_at', ''))[:10]}; was {old}. Proposed by the recto-winui-pin task.",
                        cwd=tree)
                    commit = git("rev-parse", "HEAD", cwd=tree).stdout.strip()
        finally:
            removed = git("worktree", "remove", "--force", str(tree), cwd=root, check=False)
            if removed.returncode != 0:
                cleanup.append(f"could not remove the worktree {tree}: {(removed.stderr or removed.stdout).strip()}")
            if commit is None:
                cleanup += drop(branch, root)
    if cleanup:
        # A leftover branch would read as a finished proposal on the next run, so this is a failure to repair.
        print("cleanup failed; repair by hand before the next run:\n- " + "\n- ".join(cleanup), file=sys.stderr)
        return 1
    if outcome is not None:
        return outcome
    try:
        mail(f"Recto: WinUI 3 {version} is ready to merge",
             "\n".join([f"A newer stable WinUI 3 release is out: {release.get('tag_name')} "
                        f"(published {str(release.get('published_at', ''))[:10]}).",
                        "", f"Branch {branch} (commit {commit[:12]}) moves the pin from {old} to {version}; its checks passed.",
                        "", "Merge it in the repository with:", f"    git merge {branch}",
                        "", f"Release notes: {release.get('html_url', '')}"]), dry_run)
    except Exception as exc:  # any send failure: the operator was not told, so the proposal is not finished
        left = drop(branch, root)
        print(f"could not email the proposal ({type(exc).__name__}: {exc}); "
              + (f"{branch} was removed so the next run proposes it again" if not left else "; ".join(left)),
              file=sys.stderr)
        return 1
    marked = git("update-ref", done, commit, cwd=root, check=False)
    if marked.returncode != 0:
        print(f"proposed and emailed {version}, but could not record {done}: {(marked.stderr or marked.stdout).strip()}; "
              "the next run will propose it again", file=sys.stderr)
        return 1
    print(f"proposed {version} on {branch} ({commit[:12]}) and emailed the operator")
    return 0


def main(argv=None, fetch=fetch_json, root=ROOT, build=subprocess.run, mail=notify):
    parser = argparse.ArgumentParser(description="Keep the WinUI 3 pin on the latest stable release.")
    parser.add_argument("mode", choices=("check", "update"))
    parser.add_argument("--dry-run", action="store_true", help="say what update would do; change and send nothing")
    args = parser.parse_args(argv)
    try:
        releases = fetch(RELEASES)
        latest = latest_stable(releases if isinstance(releases, list) else [])
        if latest is None:
            raise Unreadable("no stable winui3/release tag was found")
        version, release = latest
        current = main_pin(Path(root))
        feed = fetch(NUGET)
        nuget = feed.get("versions") if isinstance(feed, dict) else None
        if not isinstance(nuget, list) or not all(isinstance(v, str) for v in nuget):
            raise Unreadable(f"{NUGET}: no list of version strings")
        if not STABLE.match(current):
            print(f"{PACKAGE} {current} is not an exact stable version; pin a stable release (foundation plan D4)")
            return 1
        if newer(current, version):
            print(f"{PACKAGE} {current} is ahead of the newest stable WinUI 3 release {version}; pin a published release")
            return 1
        if current not in nuget:
            print(f"{PACKAGE} {current} on main is not published on NuGet")
            return 1
        if not newer(version, current):
            print(f"{PACKAGE} {current} is the latest stable WinUI 3 release ({release.get('tag_name')})")
            return 0
        if args.mode == "check":
            print(f"newer stable WinUI 3 release {version} ({release.get('tag_name')}); the pin is {current}")
            return 1
    except Unreadable as exc:
        print(f"check-winui-pin: could not read {exc}", file=sys.stderr)
        return 2
    except (ET.ParseError, ValueError) as exc:
        print(f"check-winui-pin: {exc}", file=sys.stderr)
        return 1
    return update(version, release, set(nuget), Path(root), args.dry_run, build, mail)


if __name__ == "__main__":
    sys.exit(main())
