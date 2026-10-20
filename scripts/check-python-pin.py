#!/usr/bin/env python3
"""Alert when the Python that CI or the local scripts use falls behind the latest stable release (foundation plan D4).

    python scripts/check-python-pin.py check     # exit 0 current, 1 behind, 2 cannot check
    python scripts/check-python-pin.py notify    # as check, but being behind is emailed once per finding, exit 0

Two pins are compared with the newest stable CPython on python.org (published, not a pre-release):
- every `python-version:` in `.github/workflows/*.yml` must name the newest stable minor (for example `3.15`);
- the interpreter running this script (the one the automations and `scripts/` use on this machine) must be the newest
  stable minor, at its newest patch.
Tooling is stdlib-only, so a bump is a one-line workflow edit or a Python install; the tests in `tests/scripts` and
`scripts/tests` prove it. `notify` (the recto-python-pin task) sends one "Python update available" email for each new
set of findings through `scripts/lib/pin_proposal.py`.
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

RELEASES = "https://www.python.org/api/v2/downloads/release/?is_published=true&pre_release=false"
NAME = re.compile(r"^Python (\d+\.\d+\.\d+)$")
CI_PIN = re.compile(r"""python-version:\s*["']?(\d+\.\d+(?:\.\d+)?)["']?""")


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


def text(key):
    return ".".join(map(str, key))


def findings(newest, by_minor, pins, local):
    out = []
    for path, line, value in pins:
        if version_key(value)[:2] < newest[:2]:
            out.append(f"{path}:{line} python-version {value} is behind the newest stable minor {text(newest[:2])}")
    if local[:2] < newest[:2]:
        out.append(f"this machine's Python {text(local)} is behind the newest stable minor {text(newest[:2])} "
                   f"(latest {text(newest)})")
    elif local < by_minor.get(local[:2], local):
        out.append(f"this machine's Python {text(local)} is behind its latest patch {text(by_minor[local[:2]])}")
    return out


def main(argv=None, fetch=fetch_json, root=ROOT, local=tuple(sys.version_info[:3]), mail=pin_proposal.notify):
    parser = argparse.ArgumentParser(description="Alert when CI or local Python falls behind the latest stable.")
    parser.add_argument("mode", choices=("check", "notify"))
    args = parser.parse_args(argv)
    try:
        releases = fetch(RELEASES)
        newest, by_minor = latest_releases(releases if isinstance(releases, list) else [])
        pins = ci_pins(root)
    except Unreadable as exc:
        print(f"check-python-pin: could not read {exc}", file=sys.stderr)
        return 2
    if not pins:
        print("check-python-pin: no python-version pin found in .github/workflows", file=sys.stderr)
        return 2
    found = findings(newest, by_minor, pins, tuple(local))
    if not found:
        print(f"CI and this machine use the newest stable Python ({'.'.join(map(str, newest))})")
        return 0
    print("\n".join(found))
    if args.mode == "check":
        return 1
    key = "python-" + hashlib.sha256("\n".join(found).encode()).hexdigest()[:12]
    return pin_proposal.notice(root=root, key=key, subject="Recto: a newer Python is available",
                               body="\n".join(found + ["", "Update the workflow python-version, or install the newer "
                                                          "Python on this machine, then run the tooling tests."]),
                               mail=mail)


if __name__ == "__main__":
    sys.exit(main())
