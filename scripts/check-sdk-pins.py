#!/usr/bin/env python3
"""Check that the C++/WinRT and Windows SDK build tools pins in src/app/Recto.vcxproj are the latest stable releases.

    python scripts/check-sdk-pins.py          # exit 0 current, 1 a pin is behind or not stable, 2 cannot check
    python scripts/check-sdk-pins.py notify   # as above, but a newer release is emailed once per version, exit 0
    python scripts/check-sdk-pins.py pr       # (GitHub pins workflow) open one PR bumping every newer release

Policy (foundation plan D4): every pin is an exact version, set to the latest stable release on
nuget.org, never a floating range and never a preview or experimental build. These two are the only
NuGet packages Recto.exe restores. The CMake build (D00 T04 section 8) fetches the same two packages through
build/bootstrap-tools.ps1 from build/tools.lock.json, whose entries for them carry "owner": "check-sdk-pins": this check
fails when a lock entry and its PackageReference name different versions, and `pr` moves both files in one change,
downloading the new package to pin its SHA-256, so one update keeps both builds on one version. The Windows SDK build tools track the Windows SDK named by
WindowsTargetPlatformVersion, so that pin is compared only against releases in its own 10.0.<build> line; moving to a new Windows
SDK is its own decision (`scripts/check-windows-sdk-line.py` files an issue for a new line). The daily
`.github/workflows/pins.yml` runs `pr`: the newer releases become one pull request with CI dispatched on its branch,
through `scripts/lib/pin_proposal.py`; `notify` emails once per version for local use. A pin that is not an exact
stable version, or a source that cannot be read, fails the run.

Stdlib only.
"""
import hashlib
import json
import os
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
LOCK = ROOT / "build" / "tools.lock.json"
PACKAGE_URL = "https://www.nuget.org/api/v2/package/{id}/{version}"
OWNER = "check-sdk-pins"
# Package id -> its entry in build/tools.lock.json.
LOCK_ENTRIES = {
    "Microsoft.Windows.CppWinRT": "cppwinrt",
    "Microsoft.Windows.SDK.BuildTools": "sdk-buildtools",
}

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


def read_lock(lock_text):
    """The versions build/tools.lock.json pins for the tracked packages, each entry checked to be this check's own
    (owner), at the nuget.org URL of its version, with a SHA-256."""
    try:
        doc = json.loads(lock_text)
    except ValueError as exc:
        raise CannotCheck(f"{LOCK.name} does not parse: {exc}") from None
    versions = {}
    for package, name in LOCK_ENTRIES.items():
        entry = doc.get(name) if isinstance(doc, dict) else None
        if not isinstance(entry, dict):
            raise CannotCheck(f"{LOCK.name} has no {name} entry for {package}")
        version = entry.get("version")
        if entry.get("owner") != OWNER or not isinstance(version, str) or not version:
            raise CannotCheck(f"{LOCK.name} entry {name} needs a version and \"owner\": \"{OWNER}\"")
        if entry.get("url") != PACKAGE_URL.format(id=package, version=version):
            raise CannotCheck(f"{LOCK.name} entry {name} does not name the nuget.org package of version {version}")
        if not re.fullmatch(r"[0-9a-f]{64}", str(entry.get("sha256", ""))):
            raise CannotCheck(f"{LOCK.name} entry {name} has no SHA-256")
        versions[package] = version
    return versions


def lock_faults(pins, lock_versions):
    """A fault for every package whose lock entry and PackageReference name different versions: the two builds would
    compile against different releases. Not proposable (a person settles which is right)."""
    return [(package, lock_versions[package], None,
             f"{LOCK.name} pins {lock_versions[package]}, not the {pins[package]} {PROJECT.name} pins")
            for package in TRACKED if package in pins and lock_versions.get(package) != pins[package]]


def fetch_package(package, version):
    url = PACKAGE_URL.format(id=package, version=version)
    try:
        with urllib.request.urlopen(url, timeout=120) as response:
            return response.read()
    except OSError as exc:
        raise CannotCheck(f"cannot download {url}: {exc}") from exc


def set_lock_versions(lock_text, versions, fetch=fetch_package):
    """The lock text with each package in `versions` moved to that version: its URL and the SHA-256 of the package
    downloaded from it. Key order and the two-space layout are kept, so the diff is the three changed values."""
    doc = json.loads(lock_text)
    for package, version in versions.items():
        entry = doc[LOCK_ENTRIES[package]]
        entry["version"] = version
        entry["url"] = PACKAGE_URL.format(id=package, version=version)
        entry["sha256"] = hashlib.sha256(fetch(package, version)).hexdigest()
    return json.dumps(doc, indent=2) + "\n"


def write_atomically(path, text):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8", newline="")
    os.replace(temporary, path)


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


def rewrite_tree(tree, newer, fetch=fetch_package):
    """Move every newer release in Recto.vcxproj and build/tools.lock.json under `tree`, together; return the
    repository-relative paths changed."""
    relative = PROJECT.relative_to(ROOT).as_posix()
    lock_relative = LOCK.relative_to(ROOT).as_posix()
    path, lock_path = Path(tree) / relative, Path(tree) / lock_relative
    text, lock_text = path.read_text(encoding="utf-8"), lock_path.read_text(encoding="utf-8")
    current, _ = read_project(text)
    locked = read_lock(lock_text)
    changed = []
    new_text = text
    for package, _, latest in newer:
        if current[package] != latest:
            new_text = set_version(new_text, package, current[package], latest)
    moves = {package: latest for package, _, latest in newer if locked[package] != latest}
    new_lock = set_lock_versions(lock_text, moves, fetch) if moves else lock_text
    if new_text != text:
        write_atomically(path, new_text)
        changed.append(relative)
    if new_lock != lock_text:
        write_atomically(lock_path, new_lock)
        changed.append(lock_relative)
    return changed


def tree_faults(tree, newer):
    """What still differs from the proposal under `tree`: a pin in either file not at its new version."""
    current, _ = read_project((Path(tree) / PROJECT.relative_to(ROOT)).read_text(encoding="utf-8"))
    locked = read_lock((Path(tree) / LOCK.relative_to(ROOT)).read_text(encoding="utf-8"))
    return ([f"{package} is {current[package]}, not {latest}" for package, _, latest in newer if current[package] != latest]
            + [f"{LOCK.name} pins {package} {locked[package]}, not {latest}" for package, _, latest in newer
               if locked[package] != latest])


def open_pr(newer, run_gh=None):
    """Propose every newer release as one pull request from the GitHub pins workflow; CI on the branch builds it."""

    def rewrite(tree):
        return rewrite_tree(tree, newer)

    def checks(tree):
        return tree_faults(tree, newer)

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
        pins, sdk_line = read_project(PROJECT.read_text(encoding="utf-8"))
        mismatched = lock_faults(pins, read_lock(LOCK.read_text(encoding="utf-8")))
        faults = mismatched + check(pins, sdk_line)
    except CannotCheck as exc:
        print(f"check-sdk-pins: {exc}", file=sys.stderr)
        return 2
    for package, pinned, _, problem in faults:
        print(f"{package} {pinned}: {problem}")
    if not faults:
        print(f"all {len(TRACKED)} pins in {PROJECT.relative_to(ROOT).as_posix()} are the latest stable releases, "
              f"and {LOCK.relative_to(ROOT).as_posix()} pins the same")
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
