#!/usr/bin/env python3
"""Alert when this machine's Visual Studio is behind the latest servicing release of the version Recto builds with.

    python scripts/check-visual-studio.py check     # exit 0 current, 1 behind or missing, 2 cannot check
    python scripts/check-visual-studio.py notify    # as check, but being behind is emailed once per release, exit 0

`PlatformToolset` in `Directory.Build.props` names the MSVC toolset (v145 is Visual Studio 2026, version 18). The
installed version comes from `vswhere.exe`; the latest servicing release comes from that version's stable release
channel manifest (`https://aka.ms/vs/<major>/stable/channel`). Visual Studio is installed per machine, not pinned in the
repository, so this check alerts and never changes anything: the Visual Studio Installer applies the update.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import pin_proposal  # noqa: E402
from pin_sources import Unreadable, fetch_json, version_key  # noqa: E402

TOOLSETS = {"v143": 17, "v145": 18}
CHANNEL = "https://aka.ms/vs/{major}/stable/channel"
VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"


def toolset_major(root=ROOT):
    text = (Path(root) / "Directory.Build.props").read_text(encoding="utf-8")
    found = set(re.findall(r"<PlatformToolset>\s*(v\d+)\s*</PlatformToolset>", text))
    if len(found) != 1 or next(iter(found)) not in TOOLSETS:
        raise ValueError(f"Directory.Build.props must name one known PlatformToolset ({', '.join(TOOLSETS)}): {found}")
    return TOOLSETS[next(iter(found))]


def installed(major, run=subprocess.run):
    """Installed (display version, product name) for that major, newest first; empty when none."""
    if not VSWHERE.is_file():
        raise Unreadable(f"{VSWHERE} not found")
    result = run([str(VSWHERE), "-all", "-format", "json", "-utf8", "-version", f"[{major}.0,{major + 1}.0)"],
                 capture_output=True, text=True, encoding="utf-8", creationflags=pin_proposal.NO_WINDOW)
    if result.returncode != 0:
        raise Unreadable(f"vswhere failed: {result.stderr.strip()}")
    rows = [(i.get("catalog", {}).get("productDisplayVersion", ""), i.get("displayName", ""))
            for i in json.loads(result.stdout or "[]") if not i.get("isPrerelease")]
    return sorted((r for r in rows if version_key(r[0])), key=lambda r: version_key(r[0]), reverse=True)


def latest(major, fetch=fetch_json):
    channel = fetch(CHANNEL.format(major=major))
    version = str((channel.get("info") or {}).get("productDisplayVersion", "")) if isinstance(channel, dict) else ""
    if version_key(version) is None:
        raise Unreadable(f"{CHANNEL.format(major=major)}: no productDisplayVersion")
    return version


def main(argv=None, fetch=fetch_json, root=ROOT, run=subprocess.run, mail=pin_proposal.notify):
    parser = argparse.ArgumentParser(description="Alert when Visual Studio is behind its latest servicing release.")
    parser.add_argument("mode", choices=("check", "notify"))
    args = parser.parse_args(argv)
    try:
        major = toolset_major(root)
        have = installed(major, run)
        newest = latest(major, fetch)
    except Unreadable as exc:
        print(f"check-visual-studio: could not read {exc}", file=sys.stderr)
        return 2
    except (OSError, ValueError) as exc:
        print(f"check-visual-studio: {exc}", file=sys.stderr)
        return 2
    if not have:
        print(f"no stable Visual Studio {major} is installed, but Directory.Build.props needs its toolset")
        return 1
    behind = [(v, name) for v, name in have if version_key(v) < version_key(newest)]
    if not behind:
        print(f"{have[0][1]} {have[0][0]} is the latest servicing release")
        return 0
    lines = [f"{name} {v} is behind {newest}" for v, name in behind]
    print("\n".join(lines))
    if args.mode == "check":
        return 1
    return pin_proposal.notice(root=root, key=f"vs-{newest}", subject=f"Recto: Visual Studio {newest} is available",
                               body="\n".join(lines + ["", "Update it from the Visual Studio Installer, then run build.cmd."]),
                               mail=mail)


if __name__ == "__main__":
    sys.exit(main())
