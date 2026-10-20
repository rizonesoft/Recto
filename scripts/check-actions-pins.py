#!/usr/bin/env python3
"""Keep every GitHub Action used by the workflows on its latest stable major release (foundation plan D4).

    python scripts/check-actions-pins.py check              # exit 1 when an action is behind its latest major
    python scripts/check-actions-pins.py update             # propose the bump on branch pins/actions-<key>, then email
    python scripts/check-actions-pins.py update --dry-run   # say what update would do; change and send nothing

Workflows pin actions by major tag (`uses: actions/checkout@v7`). The latest stable major is the major version of the
action repository's latest GitHub release (`/releases/latest` never returns a prerelease or draft). A `uses:` that
names a full version, a branch or a commit instead of a major tag is reported, so the pin style stays uniform.
`update` changes only the `@v<N>` of stale lines on a branch from main (through `scripts/lib/pin_proposal.py`); the
workflows are proven by CI when the branch is pushed, so no local build runs. Local actions (`./...`) and Docker
actions are ignored.
Exit 0 current or proposed, 1 an action is behind or a proposal failed, 2 a release could not be read.
"""
import argparse
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import pin_proposal  # noqa: E402
from pin_sources import Unreadable, fetch_json, version_key  # noqa: E402

WORKFLOWS = ".github/workflows"
USES = re.compile(r"^(\s*-?\s*uses:\s*)([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)((?:/[^@\s]*)?)@(\S+)(.*)$")
MAJOR = re.compile(r"^v(\d+)$")
LATEST = "https://api.github.com/repos/{repo}/releases/latest"


def scan(root=ROOT):
    """Every action reference as (workflow path, line number, repo, ref)."""
    found = []
    for path in sorted((Path(root) / WORKFLOWS).glob("*.y*ml")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = USES.match(line)
            if match:
                found.append((path.relative_to(root).as_posix(), number, match[2], match[4]))
    return found


def latest_major(repo, fetch=fetch_json):
    release = fetch(LATEST.format(repo=repo))
    tag = str(release.get("tag_name", "")) if isinstance(release, dict) else ""
    key = version_key(tag)
    if key is None:
        raise Unreadable(f"{repo}: latest release tag {tag!r} is not a version")
    return key[0]


def faults(references, fetch=fetch_json):
    """(path, line, repo, ref, problem, target major or None) for every reference that breaks the policy."""
    majors, out = {}, []
    for path, line, repo, ref in references:
        match = MAJOR.match(ref)
        if not match:
            out.append((path, line, repo, ref, "is not pinned by a major tag such as @v4", None))
            continue
        if repo not in majors:
            majors[repo] = latest_major(repo, fetch)
        if int(match[1]) < majors[repo]:
            out.append((path, line, repo, ref, f"latest stable major is v{majors[repo]}", majors[repo]))
        elif int(match[1]) > majors[repo]:
            out.append((path, line, repo, ref, f"is ahead of the latest release v{majors[repo]}", None))
    return out


def rewrite_text(text, targets):
    """The workflow text with each stale `<repo>@v<old>` moved to its target major; formatting is kept."""
    lines = text.split("\n")
    for index, line in enumerate(lines):
        match = USES.match(line.rstrip("\r"))
        if match and match[2] in targets and MAJOR.match(match[4]) and int(MAJOR.match(match[4])[1]) < targets[match[2]]:
            ending = "\r" if line.endswith("\r") else ""
            lines[index] = f"{match[1]}{match[2]}{match[3]}@v{targets[match[2]]}{match[5]}{ending}"
    return "\n".join(lines)


def main(argv=None, fetch=fetch_json, root=ROOT, mail=pin_proposal.notify):
    parser = argparse.ArgumentParser(description="Keep GitHub Actions on their latest stable major.")
    parser.add_argument("mode", choices=("check", "update"))
    parser.add_argument("--dry-run", action="store_true", help="say what update would do; change and send nothing")
    args = parser.parse_args(argv)
    try:
        found = faults(scan(root), fetch)
    except Unreadable as exc:
        print(f"check-actions-pins: could not read {exc}", file=sys.stderr)
        return 2
    for path, line, repo, ref, problem, _ in found:
        print(f"{path}:{line} {repo}@{ref} {problem}")
    if not found:
        print("every GitHub Action in .github/workflows is on its latest stable major")
        return 0
    targets = {repo: major for _, _, repo, _, _, major in found if major is not None}
    if args.mode == "check" or not targets or len(targets) != len({f[2] for f in found}):
        return 1
    key = "actions-" + hashlib.sha256(",".join(f"{r}@v{m}" for r, m in sorted(targets.items())).encode()).hexdigest()[:12]
    summary = ", ".join(f"{r}@v{m}" for r, m in sorted(targets.items()))

    def rewrite(tree):
        changed = []
        for path in sorted((tree / WORKFLOWS).glob("*.y*ml")):
            text = path.read_bytes().decode("utf-8")
            new = rewrite_text(text, targets)
            if new != text:
                path.write_bytes(new.encode("utf-8"))
                changed.append(path.relative_to(tree).as_posix())
        return changed

    def checks(tree):
        # Every reference to a bumped action must now name exactly its target major.
        return [f"{path}:{line} still names {repo}@{ref}" for path, line, repo, ref in scan(tree)
                if repo in targets and ref != f"v{targets[repo]}"]

    return pin_proposal.propose(
        root=root, key=key, branch=f"pins/{key}", rewrite=rewrite, checks=checks, dry_run=args.dry_run, mail=mail,
        message=[f"ci: move GitHub Actions to their latest majors ({summary})",
                 "Proposed by the recto-actions-pins task; CI proves the workflows when the branch is pushed."],
        subject="Recto: GitHub Actions updates are ready to merge",
        body=lambda commit: "\n".join([f"Newer stable majors: {summary}.", "",
                                       f"Branch pins/{key} (commit {commit[:12]}) moves the workflows to them.", "",
                                       "Merge it and push so CI proves the workflows:", f"    git merge pins/{key}"]))


if __name__ == "__main__":
    sys.exit(main())
