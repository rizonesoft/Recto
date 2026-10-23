#!/usr/bin/env python3
"""Check that the C++/WinRT and Windows SDK build tools pins in src/app/Recto.vcxproj are the latest stable releases.

    python scripts/check-sdk-pins.py          # exit 0 current, 1 a pin is behind or not stable, 2 cannot check
    python scripts/check-sdk-pins.py notify   # as above, but a newer release is emailed once per version, exit 0
    python scripts/check-sdk-pins.py pr       # (GitHub pins workflow) open one PR bumping every newer release

Policy (foundation plan D4): every pin is an exact version, set to the latest stable release on
nuget.org, never a floating range and never a preview or experimental build. These two are the only
NuGet packages Recto.exe restores. The Windows SDK build tools track the Windows SDK named by
WindowsTargetPlatformVersion, so that pin is compared only against releases in its own 10.0.<build> line; moving to a new Windows
SDK is its own decision (`scripts/check-windows-sdk-line.py` files an issue for a new line). The daily
`.github/workflows/pins.yml` runs `pr`: the newer releases become one pull request with CI dispatched on its branch,
through `scripts/lib/pin_proposal.py`; `notify` emails once per version for local use. A pin that is not an exact
stable version, or a source that cannot be read, fails the run.

Stdlib only.
"""
import json
from pathlib import Path
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import pin_proposal  # noqa: E402
PROJECT = ROOT / "src" / "app" / "Recto.vcxproj"
FEED = "https://api.nuget.org/v3-flatcontainer/{id}/index.json"

# Package id -> True when only releases in the pinned version's first three components count.
TRACKED = {
    "Microsoft.Windows.CppWinRT": False,
    "Microsoft.Windows.SDK.BuildTools": True,
}


class CannotCheck(Exception):
    pass


def local(tag):
    # MSBuild elements carry the msbuild/2003 namespace, so match the local name.
    return tag.rsplit("}", 1)[-1]


def read_project(project_text):
    """The tracked pins and the Windows SDK line (10, 0, <build>) that WindowsTargetPlatformVersion names."""
    try:
        root = ET.fromstring(project_text)
    except ET.ParseError as exc:
        raise CannotCheck(f"{PROJECT.name} does not parse: {exc}") from None
    pins = {}
    for element in root.iter():
        package = element.get("Include")
        if local(element.tag) != "PackageReference" or package not in TRACKED:
            continue
        if package in pins:
            raise CannotCheck(f"{package} is referenced more than once in {PROJECT.name}")
        if not element.get("Version"):
            raise CannotCheck(f"{package} has no Version in {PROJECT.name}")
        pins[package] = element.get("Version")
    missing = [p for p in TRACKED if p not in pins]
    if missing:
        raise CannotCheck(f"no PackageReference pin for {', '.join(missing)} in {PROJECT.name}")
    targets = {(e.text or "").strip() for e in root.iter() if local(e.tag) == "WindowsTargetPlatformVersion"}
    if len(targets) != 1 or not is_stable(next(iter(targets))):
        raise CannotCheck(f"{PROJECT.name} needs exactly one numeric WindowsTargetPlatformVersion")
    return pins, key(next(iter(targets)))[:3]


def is_stable(version):
    return re.fullmatch(r"\d+(\.\d+)*", version) is not None


def key(version):
    return tuple(int(part) for part in version.split("."))


def latest_stable(versions, line=None):
    stable = [v for v in versions if is_stable(v)]
    if line is not None:
        stable = [v for v in stable if key(v)[:len(line)] == line]
    if not stable:
        raise CannotCheck("no stable release found" + (f" in line {'.'.join(map(str, line))}" if line else ""))
    return max(stable, key=key)


def fetch_versions(package):
    url = FEED.format(id=package.lower())
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            feed = json.load(response)
    except (OSError, ValueError) as exc:
        raise CannotCheck(f"cannot read {url}: {exc}") from exc
    versions = feed.get("versions") if isinstance(feed, dict) else None
    if not isinstance(versions, list) or not all(isinstance(v, str) for v in versions):
        raise CannotCheck(f"{url} holds no list of version strings")
    return versions


def check(pins, sdk_line, fetch=fetch_versions):
    """Return (package, pinned, latest, problem) for every pin that breaks the policy."""
    faults = []
    for package, same_line in TRACKED.items():
        pinned = pins[package]
        if not is_stable(pinned):
            faults.append((package, pinned, None, "is not an exact stable version"))
            continue
        if same_line and key(pinned)[:3] != sdk_line:
            faults.append((package, pinned, None, f"is not in the Windows SDK {'.'.join(map(str, sdk_line))} line "
                                                  "that WindowsTargetPlatformVersion names"))
            continue
        latest = latest_stable(fetch(package), sdk_line if same_line else None)
        if key(pinned) != key(latest):
            faults.append((package, pinned, latest, f"latest stable is {latest}"))
    return faults


def set_version(text, package, old, new):
    """The project text with only one live PackageReference's Version changed, so formatting and comments survive."""
    comments = [m.span() for m in re.finditer(r"<!--.*?-->", text, re.S)]
    include = re.compile(r"\bInclude\s*=\s*([\"'])" + re.escape(package) + r"\1")
    tags = [m for m in re.finditer(r"<PackageReference\b[^>]*>", text)
            if include.search(m.group(0)) and not any(a <= m.start() < b for a, b in comments)]
    if len(tags) != 1:
        raise CannotCheck(f"needs exactly one live PackageReference for {package}, found {len(tags)}")
    version = re.compile(r"(\bVersion\s*=\s*([\"']))" + re.escape(old) + r"\2")
    changed, count = version.subn(lambda m: m.group(1) + new + m.group(2), tags[0].group(0))
    if count != 1:
        raise CannotCheck(f"the {package} pin {old} was not found on its PackageReference")
    return text[:tags[0].start()] + changed + text[tags[0].end():]


def open_pr(newer, run_gh=None):
    """Propose every newer release as one pull request from the GitHub pins workflow; CI on the branch builds it."""
    relative = PROJECT.relative_to(ROOT).as_posix()

    def rewrite(tree):
        path = Path(tree) / relative
        text = path.read_text(encoding="utf-8")
        current, _ = read_project(text)
        for package, _, latest in newer:
            if current[package] != latest:
                text = set_version(text, package, current[package], latest)
        if text == path.read_text(encoding="utf-8"):
            return []
        path.write_text(text, encoding="utf-8", newline="")
        return [relative]

    def checks(tree):
        current, _ = read_project((Path(tree) / relative).read_text(encoding="utf-8"))
        return [f"{package} is {current[package]}, not {latest}" for package, _, latest in newer
                if current[package] != latest]

    summary = ", ".join(f"{package} {latest}" for package, _, latest in newer)
    extra = {} if run_gh is None else {"run_gh": run_gh}
    return pin_proposal.propose_pr(
        root=ROOT, branch="pins/toolchain-" + "-".join(f"{p.split('.')[-1].lower()}-{v}" for p, _, v in newer),
        rewrite=rewrite, checks=checks, title=f"deps: update {summary}",
        body="\n".join([f"{package}: {pinned} -> {latest}" for package, pinned, latest in newer]
                       + ["", "Opened by the pins workflow (foundation plan D4). CI builds x64 and ARM64 on this branch."]),
        **extra)


def main(argv=None, mail=pin_proposal.notify):
    argv = sys.argv[1:] if argv is None else argv
    if argv not in ([], ["notify"], ["pr"]):
        print("usage: check-sdk-pins.py [notify | pr]", file=sys.stderr)
        return 2
    try:
        faults = check(*read_project(PROJECT.read_text(encoding="utf-8")))
    except CannotCheck as exc:
        print(f"check-sdk-pins: {exc}", file=sys.stderr)
        return 2
    for package, pinned, _, problem in faults:
        print(f"{package} {pinned}: {problem}")
    if not faults:
        print(f"all {len(TRACKED)} pins in {PROJECT.relative_to(ROOT).as_posix()} are the latest stable releases")
        return 0
    newer = [(package, pinned, latest) for package, pinned, latest, _ in faults if latest is not None]
    if argv == [] or len(newer) != len(faults):
        return 1
    if argv == ["pr"]:
        return open_pr(newer)
    lines = [f"{package} {pinned} -> {latest}" for package, pinned, latest in newer]
    key = "toolchain-" + "-".join(f"{package}-{latest}" for package, _, latest in newer)
    return pin_proposal.notice(root=ROOT, key=key, subject="Recto: newer C++/WinRT or build tools releases",
                               body="\n".join(["Newer stable releases for the pins in src/app/Recto.vcxproj:"] + lines + [
                                   "", "Bump each in its own commit after a full build.cmd (foundation plan D4)."]),
                               mail=mail)


if __name__ == "__main__":
    sys.exit(main())
