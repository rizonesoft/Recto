#!/usr/bin/env python3
"""Alert when a newer Windows SDK line ships than the one Recto targets (foundation plan D4).

    python scripts/check-windows-sdk-line.py check     # exit 0 current, 1 a newer line exists or pins disagree, 2 cannot check
    python scripts/check-windows-sdk-line.py notify    # as check, but a newer line is emailed once and exits 0
    python scripts/check-windows-sdk-line.py issue     # (GitHub pins workflow) one issue per new line, closed when resolved

Recto targets one Windows SDK, named by `WindowsTargetPlatformVersion` (10.0.<build>.0) in `Directory.Build.props` and
in every project that sets it. `scripts/check-sdk-pins.py` keeps the `Microsoft.Windows.SDK.BuildTools` pin current
within that line; this check looks one level up and reports when nuget.org publishes stable build tools for a newer
10.0.<build> line. Moving lines changes the target platform, so it is never automatic: the alert names the new line
and the owner decides (a bump updates `WindowsTargetPlatformVersion` everywhere and the build tools pin together).
`issue` (the daily `.github/workflows/pins.yml`) files one issue per new line and closes it once Recto targets the
newest line; `notify` emails "Windows SDK <line> is available" once per line for local use. Both go through
`scripts/lib/pin_proposal.py` and succeed, so a failure is reserved for a check that could not run.
"""
import argparse
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import pin_proposal  # noqa: E402
from pin_sources import Unreadable, fetch_json, version_key  # noqa: E402

FEED = "https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.buildtools/index.json"
FILES = ("Directory.Build.props", "src/app/Recto.vcxproj")


def targets(root=ROOT):
    """Every WindowsTargetPlatformVersion value as (file, value)."""
    found = []
    for name in FILES:
        tree = ET.fromstring((Path(root) / name).read_text(encoding="utf-8"))
        found += [(name, (e.text or "").strip()) for e in tree.iter() if e.tag.rsplit("}", 1)[-1] == "WindowsTargetPlatformVersion"]
    return found


def newest_line(versions):
    stable = [version_key(v) for v in versions if re.fullmatch(r"\d+(\.\d+)*", v)]
    if not stable:
        raise Unreadable(f"{FEED}: no stable versions")
    return max(stable)[:3]


def main(argv=None, fetch=fetch_json, root=ROOT, mail=pin_proposal.notify, run_gh=pin_proposal.gh):
    parser = argparse.ArgumentParser(description="Alert when a newer Windows SDK line ships.")
    parser.add_argument("mode", choices=("check", "notify", "issue"))
    args = parser.parse_args(argv)
    try:
        values = targets(root)
        feed = fetch(FEED)
        versions = feed.get("versions") if isinstance(feed, dict) else None
        if not isinstance(versions, list):
            raise Unreadable(f"{FEED}: no versions list")
        newest = newest_line(versions)
    except Unreadable as exc:
        print(f"check-windows-sdk-line: could not read {exc}", file=sys.stderr)
        return 2
    except (OSError, ET.ParseError) as exc:
        print(f"check-windows-sdk-line: {exc}", file=sys.stderr)
        return 2
    lines = {value for _, value in values}
    if not values or len(lines) != 1 or version_key(next(iter(lines))) is None:
        print("WindowsTargetPlatformVersion must be one numeric value everywhere: "
              + ", ".join(f"{name}={value}" for name, value in values))
        return 1
    current = version_key(next(iter(lines)))[:3]
    if newest > current:
        line, target = ".".join(map(str, newest)), ".".join(map(str, current))
        advice = (f"Windows SDK {line} is available; Recto targets {target}. Moving is an owner decision: update "
                  f"WindowsTargetPlatformVersion in {' and '.join(FILES)} and the build tools pin together, then run build.cmd.")
        print(advice)
        if args.mode == "check":
            return 1
        if args.mode == "issue":
            return pin_proposal.file_issue(root=root, check="windows-sdk-line", title=f"Windows SDK {line} is available",
                                           body=advice, run_gh=run_gh)
        return pin_proposal.notice(root=root, key=f"windows-sdk-{line}", subject=f"Recto: Windows SDK {line} is available",
                                   body=advice, mail=mail)
    print(f"Recto targets the newest Windows SDK line {'.'.join(map(str, current))}")
    if args.mode == "issue":
        return pin_proposal.resolve_issues(root=root, check="windows-sdk-line", run_gh=run_gh,
                                           message=f"Recto targets the newest Windows SDK line {'.'.join(map(str, current))}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
