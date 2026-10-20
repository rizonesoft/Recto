#!/usr/bin/env python3
"""Keep the vcpkg baseline on the latest vcpkg release (foundation plan D4; test-only dependencies, D14).

    python scripts/check-vcpkg-baseline.py check              # exit 1 when a newer vcpkg release exists
    python scripts/check-vcpkg-baseline.py update             # propose it on branch pins/vcpkg-<tag>, then email
    python scripts/check-vcpkg-baseline.py update --dry-run   # say what update would do; change and send nothing
    python scripts/check-vcpkg-baseline.py pr                 # (GitHub pins workflow) open a PR; CI builds it

The pin is `default-registry.baseline` in `vcpkg-configuration.json`, a microsoft/vcpkg commit. The target is the
commit of the latest microsoft/vcpkg GitHub release (`/releases/latest`, never a prerelease), not the moving master
branch. A baseline that is already that commit, or descends from it, is current. `update` rewrites only the baseline
on a branch from main and requires `build.cmd` (which rebuilds the vcpkg test dependencies such as gtest and runs the
tests) to pass before it commits and emails the operator, through `scripts/lib/pin_proposal.py`.
Exit 0 current or proposed, 1 behind or a proposal failed, 2 a source could not be read or the pin is malformed.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import pin_proposal  # noqa: E402
from pin_sources import Unreadable, fetch_json  # noqa: E402

CONFIG = "vcpkg-configuration.json"
REPO = "microsoft/vcpkg"
LATEST = f"https://api.github.com/repos/{REPO}/releases/latest"
COMMIT = f"https://api.github.com/repos/{REPO}/commits/{{ref}}"
COMPARE = f"https://api.github.com/repos/{REPO}/compare/{{base}}...{{head}}"
SHA = re.compile(r"^[0-9a-f]{40}$")


def baseline(text):
    data = json.loads(text)
    value = (data.get("default-registry") or {}).get("baseline") if isinstance(data, dict) else None
    if not isinstance(value, str) or not SHA.match(value):
        raise ValueError(f"{CONFIG}: default-registry.baseline must be a 40-character commit id")
    return value


def latest_release(fetch=fetch_json):
    """(tag, commit) of the latest vcpkg release."""
    release = fetch(LATEST)
    tag = str(release.get("tag_name", "")) if isinstance(release, dict) else ""
    if not tag:
        raise Unreadable(f"{LATEST}: no tag_name")
    commit = fetch(COMMIT.format(ref=tag))
    sha = str(commit.get("sha", "")) if isinstance(commit, dict) else ""
    if not SHA.match(sha):
        raise Unreadable(f"{REPO}@{tag}: no commit sha")
    return tag, sha


def is_behind(pinned, release_sha, fetch=fetch_json):
    """True when the release commit is ahead of the pinned baseline."""
    if pinned == release_sha:
        return False
    status = fetch(COMPARE.format(base=pinned, head=release_sha))
    state = status.get("status") if isinstance(status, dict) else None
    if state not in ("ahead", "behind", "identical", "diverged"):
        raise Unreadable(f"{REPO} compare {pinned[:12]}...{release_sha[:12]}: no status")
    # "behind" means the pin descends from the release (newer than it); "diverged" means the pin is off the release
    # line, which a release baseline should never be, so it counts as stale.
    return state in ("ahead", "diverged")


def rewrite_text(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"{CONFIG}: the baseline {old} must appear exactly once")
    return text.replace(old, new)


def main(argv=None, fetch=fetch_json, root=ROOT, build=None, mail=pin_proposal.notify, run_gh=None):
    parser = argparse.ArgumentParser(description="Keep the vcpkg baseline on the latest vcpkg release.")
    parser.add_argument("mode", choices=("check", "update", "pr"))
    parser.add_argument("--dry-run", action="store_true", help="say what update would do; change and send nothing")
    args = parser.parse_args(argv)
    root = Path(root)
    try:
        pinned = baseline((root / CONFIG).read_text(encoding="utf-8"))
        tag, sha = latest_release(fetch)
        behind = is_behind(pinned, sha, fetch)
    except Unreadable as exc:
        print(f"check-vcpkg-baseline: could not read {exc}", file=sys.stderr)
        return 2
    except (OSError, ValueError) as exc:
        print(f"check-vcpkg-baseline: {exc}", file=sys.stderr)
        return 2
    if not behind:
        print(f"the vcpkg baseline {pinned[:12]} is at or past the latest release {tag}")
        return 0
    print(f"vcpkg release {tag} ({sha[:12]}) is newer than the baseline {pinned[:12]}")
    if args.mode == "check":
        return 1
    key = f"vcpkg-{tag}"

    def rewrite(tree):
        path = tree / CONFIG
        current = baseline(path.read_text(encoding="utf-8"))
        if current == sha:
            return []
        path.write_bytes(rewrite_text(path.read_bytes().decode("utf-8"), current, sha).encode("utf-8"))
        return [CONFIG]

    def named(tree):
        return [] if baseline((tree / CONFIG).read_text(encoding="utf-8")) == sha else [f"{CONFIG} does not name {sha}"]

    def checks(tree):
        return named(tree) + (pin_proposal.build_passes(tree) if build is None else pin_proposal.build_passes(tree, build))

    if args.mode == "pr":
        # On GitHub the dispatched build.yml on the branch rebuilds the vcpkg test dependencies, so no local build.
        extra = {} if run_gh is None else {"run_gh": run_gh}
        return pin_proposal.propose_pr(
            root=root, branch=f"pins/{key}", rewrite=rewrite, checks=named,
            title=f"deps: move the vcpkg baseline to release {tag}",
            body="\n".join([f"vcpkg release {tag} ({sha}); the baseline was {pinned}.", "",
                            "Opened by the pins workflow (foundation plan D4). CI rebuilds the test dependencies and "
                            "runs the tests on this branch."]), **extra)

    return pin_proposal.propose(
        root=root, key=key, branch=f"pins/{key}", rewrite=rewrite, checks=checks, dry_run=args.dry_run, mail=mail,
        message=[f"deps: move the vcpkg baseline to release {tag}",
                 f"Baseline {sha}; was {pinned}. build.cmd passed. Proposed by scripts/check-vcpkg-baseline.py update."],
        subject=f"Recto: vcpkg {tag} is ready to merge",
        body=lambda commit: "\n".join([f"vcpkg release {tag} is out ({sha[:12]}).", "",
                                       f"Branch pins/{key} (commit {commit[:12]}) moves the baseline from "
                                       f"{pinned[:12]}; build.cmd passed there.", "",
                                       "Merge it with:", f"    git merge pins/{key}"]))


if __name__ == "__main__":
    sys.exit(main())
