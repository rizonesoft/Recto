#!/usr/bin/env python3
"""Alert when the Python that CI or build/tools.lock.json pins falls behind the latest stable release (D4).

    build\\python.cmd scripts/check-python-pin.py check     # exit 0 current, 1 behind, 2 cannot check
    build\\python.cmd scripts/check-python-pin.py notify    # as check, but being behind is emailed once per finding, exit 0
    python scripts/check-python-pin.py issue --scope ci    # (GitHub pins workflow) one issue per CI finding

`--scope ci` checks only the workflow pins (what GitHub Actions `setup-python` uses), `--scope local` only the
repo-scoped CPython version in `build/tools.lock.json` (D00 T04 section 6), and the default `all` both.

Two pins are compared with the newest stable CPython on python.org (published, not a pre-release):
- every `python-version:` in `.github/workflows/*.yml` must name the newest stable minor (for example `3.15`);
- the `cpython.version` entry in `build/tools.lock.json` must be the newest stable minor, at its newest patch.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import pin_proposal  # noqa: E402
from pin_sources import Unreadable, fetch_json, version_key  # noqa: E402

RELEASES = "https://www.python.org/api/v2/downloads/release/?is_published=true&pre_release=false"
NAME = re.compile(r"^Python (\d+\.\d+\.\d+)$")
CI_PIN = re.compile(r"""python-version:\s*["']?(\d+\.\d+(?:\.\d+)?)["']?""")
LOCK_REL = "build/tools.lock.json"


def latest_releases(releases):
    """Newest stable version overall and the newest patch of each minor, as key tuples."""
    versions = [version_key(NAME.match(str(r.get("name", "")))[1]) for r in releases
                if isinstance(r, dict) and NAME.match(str(r.get("name", "")))]
    if not versions:
        raise Unreadable(f"{RELEASES}: no stable CPython releases")
    by_minor = {}
    for v in versions:
        by_minor[v[:2]] = max(by_minor.get(v[:2], v), v)
    return max(versions), by_minor


def ci_pins(root=ROOT):
    found = []
    for path in sorted((Path(root) / ".github" / "workflows").glob("*.y*ml")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = CI_PIN.search(line)
            if match:
                found.append((path.relative_to(root).as_posix(), number, match[1]))
    return found


def lock_pin(root=ROOT):
    """Read the pinned CPython version tuple from build/tools.lock.json."""
    lock_path = Path(root) / LOCK_REL
    try:
        doc = json.loads(lock_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise Unreadable(f"{LOCK_REL}: {exc}") from None
    cpython = doc.get("cpython") if isinstance(doc, dict) else None
    raw = cpython.get("version") if isinstance(cpython, dict) else None
    parsed = version_key(str(raw)) if isinstance(raw, str) else None
    if parsed is None or len(parsed) != 3:
        raise Unreadable(f"{LOCK_REL}: cpython.version is missing or not X.Y.Z")
    return parsed


def text(key):
    return ".".join(map(str, key))


def findings(newest, by_minor, pins, local):
    out = []
    for path, line, value in pins or []:
        if version_key(value)[:2] < newest[:2]:
            out.append(f"{path}:{line} python-version {value} is behind the newest stable minor {text(newest[:2])}")
    if local is None:
        return out
    if local[:2] < newest[:2]:
        out.append(f"{LOCK_REL} cpython {text(local)} is behind the newest stable minor {text(newest[:2])} "
                   f"(latest {text(newest)})")
    elif local < by_minor.get(local[:2], local):
        out.append(f"{LOCK_REL} cpython {text(local)} is behind its latest patch {text(by_minor[local[:2]])}")
    return out


def main(argv=None, fetch=fetch_json, root=ROOT, local=None, mail=pin_proposal.notify,
         run_gh=pin_proposal.gh):
    parser = argparse.ArgumentParser(description="Alert when CI or build/tools.lock.json Python falls behind the latest stable.")
    parser.add_argument("mode", choices=("check", "notify", "issue"))
    parser.add_argument("--scope", choices=("all", "ci", "local"), default="all")
    args = parser.parse_args(argv)
    try:
        releases = fetch(RELEASES)
        newest, by_minor = latest_releases(releases if isinstance(releases, list) else [])
        pins = ci_pins(root) if args.scope != "local" else []
        resolved_local = None
        if args.scope != "ci":
            resolved_local = tuple(local) if local is not None else lock_pin(root)
    except Unreadable as exc:
        print(f"check-python-pin: could not read {exc}", file=sys.stderr)
        return 2
    if not pins and args.scope != "local":
        print("check-python-pin: no python-version pin found in .github/workflows", file=sys.stderr)
        return 2
    found = findings(newest, by_minor, pins, resolved_local)
    if not found:
        print(f"the checked Python pins ({args.scope}) are the newest stable Python ({text(newest)})")
        if args.mode == "issue":
            return pin_proposal.resolve_issues(root=root, check="python", run_gh=run_gh,
                                               message=f"CI uses the newest stable Python {text(newest[:2])}.")
        return 0
    print("\n".join(found))
    if args.mode == "check":
        return 1
    if args.mode == "issue":
        return pin_proposal.file_issue(root=root, check="python", title=f"CI Python {text(newest[:2])} is available",
                                       body="\n".join(found + ["", "Update python-version in the workflows (Dependabot "
                                                                   "does not track it), then let CI prove it."]),
                                       run_gh=run_gh)
    key = "python-" + hashlib.sha256("\n".join(found).encode()).hexdigest()[:12]
    return pin_proposal.notice(root=root, key=key, subject="Recto: a newer Python is available",
                               body="\n".join(found + ["", "Update cpython in build/tools.lock.json, then run the tooling tests."
                                                       if args.scope == "local" else
                                                       "Update the workflow python-version or build/tools.lock.json, "
                                                       "then run the tooling tests."]),
                               mail=mail)


if __name__ == "__main__":
    sys.exit(main())
