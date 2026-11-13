#!/usr/bin/env python3
"""Check that Ninja, CMake, sccache and CPython in build/tools.lock.json are on their latest stable releases (D4).

    build\\python.cmd scripts/check-tool-pins.py check    # exit 0 current, 1 behind or invalid, 2 cannot check
    build\\python.cmd scripts/check-tool-pins.py notify   # email once per newer release set, exit 0
    python scripts/check-tool-pins.py pr                 # (GitHub pins workflow) open one PR updating lock + SHA-256

Policy (foundation plan D4, D00 T04 section 6): every entry in build/tools.lock.json pins an exact stable release URL,
its published 64-hex SHA-256, and its relative executable path. `check` (or no mode argument) compares all four entries
against each project's official release metadata. `notify` (the local `recto-tool-pins` scheduled task) announces each
newer release set once via `refs/recto-notice/tools-...`. `pr` (the daily `.github/workflows/pins.yml` workflow) fetches
the new release archive over HTTPS, computes its SHA-256 (cross-checking GitHub's asset digest when published), refuses
with exit 2 without touching `build/tools.lock.json` if any lookup or checksum fails, and opens a single `pins/tools-...`
pull request with `build.yml` and `plan.yml` dispatched on its branch.

Stdlib only.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
if str(ROOT / "scripts" / "lib") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts" / "lib"))
import lib.pin_proposal as pin_proposal  # noqa: E402
from lib.pin_sources import Unreadable, fetch_json, version_key  # noqa: E402

LOCK_REL = "build/tools.lock.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
CPYTHON_NAME_RE = re.compile(r"^Python (\d+\.\d+\.\d+)$")
CPYTHON_RELEASES_URL = "https://www.python.org/api/v2/downloads/release/?is_published=true&pre_release=false"

TOOL_SPECS = {
    "ninja": {
        "kind": "github",
        "repo": "ninja-build/ninja",
        "asset": lambda ver, tag: "ninja-win.zip",
        "url": lambda ver, tag: f"https://github.com/ninja-build/ninja/releases/download/{tag}/ninja-win.zip",
        "executable": lambda ver, tag: "ninja.exe",
    },
    "cmake": {
        "kind": "github",
        "repo": "Kitware/CMake",
        "asset": lambda ver, tag: f"cmake-{ver}-windows-x86_64.zip",
        "url": lambda ver, tag: f"https://github.com/Kitware/CMake/releases/download/{tag}/cmake-{ver}-windows-x86_64.zip",
        "executable": lambda ver, tag: f"cmake-{ver}-windows-x86_64/bin/cmake.exe",
    },
    "sccache": {
        "kind": "github",
        "repo": "mozilla/sccache",
        "asset": lambda ver, tag: f"sccache-{tag}-x86_64-pc-windows-msvc.zip",
        "url": lambda ver, tag: f"https://github.com/mozilla/sccache/releases/download/{tag}/sccache-{tag}-x86_64-pc-windows-msvc.zip",
        "executable": lambda ver, tag: f"sccache-{tag}-x86_64-pc-windows-msvc/sccache.exe",
    },
    "cpython": {
        "kind": "cpython",
        "url": lambda ver, tag: f"https://www.python.org/ftp/python/{ver}/python-{ver}-embed-amd64.zip",
        "executable": lambda ver, tag: "python.exe",
    },
}


class CannotCheck(Exception):
    """Raised when build/tools.lock.json or an upstream release source cannot be read or verified."""


def is_stable(version: str) -> bool:
    return isinstance(version, str) and re.fullmatch(r"\d+(?:\.\d+)+", version.strip()) is not None


def fetch_bytes(url: str, opener=urllib.request.urlopen) -> bytes:
    if not url.startswith("https://"):
        raise CannotCheck(f"refusing non-HTTPS download URL: {url}")
    headers = {"User-Agent": "recto-pins"}
    token = os.environ.get("GITHUB_TOKEN")
    if token and url.startswith("https://api.github.com/"):
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    try:
        with opener(request, timeout=120) as response:
            data = response.read()
    except Exception as exc:
        raise CannotCheck(f"{url}: {type(exc).__name__}: {exc}") from None
    if not data:
        raise CannotCheck(f"{url}: empty response body")
    return data


def read_lock(root: Path = ROOT) -> dict[str, dict[str, str]]:
    lock_path = Path(root) / LOCK_REL
    try:
        raw = lock_path.read_text(encoding="utf-8")
        doc = json.loads(raw)
    except (OSError, ValueError) as exc:
        raise CannotCheck(f"{LOCK_REL} cannot be read: {exc}") from None
    if not isinstance(doc, dict):
        raise CannotCheck(f"{LOCK_REL} must hold a JSON object")
    missing = [name for name in TOOL_SPECS if name not in doc]
    extra = [name for name in doc if name not in TOOL_SPECS]
    if missing:
        raise CannotCheck(f"{LOCK_REL} is missing required tool entries: {', '.join(missing)}")
    if extra:
        raise CannotCheck(f"{LOCK_REL} holds unexpected tool entries: {', '.join(extra)}")
    entries: dict[str, dict[str, str]] = {}
    for name in TOOL_SPECS:
        item = doc[name]
        if not isinstance(item, dict):
            raise CannotCheck(f"{LOCK_REL} entry '{name}' must be an object")
        for key_name in ("version", "url", "sha256", "executable"):
            val = item.get(key_name)
            if not isinstance(val, str) or not val.strip():
                raise CannotCheck(f"{LOCK_REL} entry '{name}' is missing non-empty '{key_name}'")
        entries[name] = {k: str(item[k]).strip() for k in ("version", "url", "sha256", "executable")}
    return entries


def _latest_github(repo: str, spec: dict, fetch) -> tuple[str, str, str, str | None]:
    api_url = f"https://api.github.com/repos/{repo}/releases/latest"
    try:
        payload = fetch(api_url)
    except Unreadable as exc:
        raise CannotCheck(str(exc)) from None
    candidates = [payload] if isinstance(payload, dict) else (payload if isinstance(payload, list) else [])
    stable: list[tuple[tuple[int, ...], str, str, dict]] = []
    for rel in candidates:
        if not isinstance(rel, dict) or rel.get("draft") or rel.get("prerelease"):
            continue
        tag = str(rel.get("tag_name") or "").strip()
        ver = tag[1:] if tag.startswith("v") else tag
        vkey = version_key(ver)
        if vkey is not None and is_stable(ver):
            stable.append((vkey, ver, tag, rel))
    if not stable:
        raise CannotCheck(f"{api_url}: no stable release found")
    _, ver, tag, rel = max(stable, key=lambda row: row[0])
    want_asset = spec["asset"](ver, tag)
    download_url = spec["url"](ver, tag)
    published_sha: str | None = None
    assets = rel.get("assets")
    if isinstance(assets, list) and assets:
        matched = [a for a in assets if isinstance(a, dict) and a.get("name") == want_asset]
        if not matched:
            raise CannotCheck(f"{repo} {tag} has no release asset named {want_asset}")
        asset_obj = matched[0]
        if isinstance(asset_obj.get("browser_download_url"), str) and asset_obj["browser_download_url"].startswith("https://"):
            download_url = asset_obj["browser_download_url"]
        digest = asset_obj.get("digest")
        if isinstance(digest, str) and digest.startswith("sha256:"):
            cand_sha = digest.split(":", 1)[1].strip().lower()
            if not SHA256_RE.fullmatch(cand_sha):
                raise CannotCheck(f"{repo} {tag} asset {want_asset} has malformed digest {digest!r}")
            published_sha = cand_sha
    return ver, tag, download_url, published_sha


def _latest_cpython(spec: dict, fetch) -> tuple[str, str, str, str | None]:
    try:
        releases = fetch(CPYTHON_RELEASES_URL)
    except Unreadable as exc:
        raise CannotCheck(str(exc)) from None
    if not isinstance(releases, list):
        raise CannotCheck(f"{CPYTHON_RELEASES_URL}: expected a JSON list")
    versions: list[tuple[tuple[int, ...], str]] = []
    for rel in releases:
        if not isinstance(rel, dict):
            continue
        m = CPYTHON_NAME_RE.match(str(rel.get("name") or ""))
        if m and is_stable(m.group(1)):
            vkey = version_key(m.group(1))
            if vkey is not None:
                versions.append((vkey, m.group(1)))
    if not versions:
        raise CannotCheck(f"{CPYTHON_RELEASES_URL}: no stable CPython releases")
    _, ver = max(versions, key=lambda row: row[0])
    return ver, ver, spec["url"](ver, ver), None


def latest_for_tool(tool: str, fetch=fetch_json) -> dict[str, str | None]:
    spec = TOOL_SPECS[tool]
    if spec["kind"] == "github":
        ver, tag, url, published_sha = _latest_github(spec["repo"], spec, fetch)
    else:
        ver, tag, url, published_sha = _latest_cpython(spec, fetch)
    return {
        "version": ver,
        "tag": tag,
        "url": url,
        "sha256": published_sha,
        "executable": spec["executable"](ver, tag),
    }


def check_entries(entries: dict[str, dict[str, str]], fetch=fetch_json) -> list[tuple[str, str, str | None, dict | None, str]]:
    faults: list[tuple[str, str, str | None, dict | None, str]] = []
    for tool in TOOL_SPECS:
        entry = entries[tool]
        pinned = entry["version"]
        if not is_stable(pinned):
            faults.append((tool, pinned, None, None, "is not an exact stable version"))
            continue
        if not entry["url"].startswith("https://"):
            faults.append((tool, pinned, None, None, "url must use https://"))
            continue
        if not SHA256_RE.fullmatch(entry["sha256"].lower()):
            faults.append((tool, pinned, None, None, "sha256 is not 64 lowercase hex digits"))
            continue
        if ".." in entry["executable"] or entry["executable"].startswith(("/", "\\")):
            faults.append((tool, pinned, None, None, "executable must be a safe relative path"))
            continue
        latest = latest_for_tool(tool, fetch=fetch)
        latest_ver = str(latest["version"])
        if version_key(pinned) != version_key(latest_ver):
            faults.append((tool, pinned, latest_ver, latest, f"latest stable is {latest_ver}"))
    return faults


def resolve_update_checksum(tool: str, latest: dict[str, str | None], download=fetch_bytes) -> dict[str, str]:
    url = str(latest["url"])
    archive = download(url)
    computed = hashlib.sha256(archive).hexdigest().lower()
    published = latest.get("sha256")
    if published and published != computed:
        raise CannotCheck(f"{tool} {latest['version']}: published digest {published} != downloaded SHA-256 {computed}")
    return {
        "version": str(latest["version"]),
        "url": url,
        "sha256": computed,
        "executable": str(latest["executable"]),
    }


def open_pr(updates: list[tuple[str, str, dict[str, str]]], root: Path = ROOT, run_gh=None) -> int:
    def rewrite(tree):
        lock_path = Path(tree) / LOCK_REL
        doc = json.loads(lock_path.read_text(encoding="utf-8"))
        changed = False
        for tool, _, target in updates:
            if doc.get(tool) != target:
                doc[tool] = dict(target)
                changed = True
        if not changed:
            return []
        lock_path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8", newline="\n")
        return [LOCK_REL]

    def checks(tree):
        current = read_lock(Path(tree))
        problems = []
        for tool, _, target in updates:
            for field in ("version", "url", "sha256", "executable"):
                if current[tool][field] != target[field]:
                    problems.append(f"{tool}.{field} is {current[tool][field]!r}, expected {target[field]!r}")
        return problems

    summary = ", ".join(f"{tool} {target['version']}" for tool, _, target in updates)
    branch = "pins/tools-" + "-".join(f"{tool}-{target['version']}" for tool, _, target in updates)
    body = "\n".join(
        [f"{tool}: {pinned} -> {target['version']} ({target['sha256'][:12]})" for tool, pinned, target in updates]
        + ["", "Opened by the pins workflow (foundation plan D4, D00 T04 section 6). CI verifies .tools bootstrap, x64/ARM64 builds, and Python gates on this branch."]
    )
    extra = {} if run_gh is None else {"run_gh": run_gh}
    return pin_proposal.propose_pr(
        root=root,
        branch=branch,
        rewrite=rewrite,
        checks=checks,
        title=f"deps: update {summary}",
        body=body,
        **extra,
    )


def main(argv=None, *, fetch=fetch_json, download=fetch_bytes, root: Path = ROOT,
         mail=pin_proposal.notify, run_gh=None) -> int:
    argv = sys.argv[1:] if argv is None else list(argv)
    if argv == []:
        mode = "check"
    elif len(argv) == 1 and argv[0] in ("check", "notify", "pr"):
        mode = argv[0]
    else:
        print("usage: check-tool-pins.py [check | notify | pr]", file=sys.stderr)
        return 2

    try:
        entries = read_lock(root)
        faults = check_entries(entries, fetch=fetch)
    except CannotCheck as exc:
        print(f"check-tool-pins: {exc}", file=sys.stderr)
        return 2

    for tool, pinned, _, _, problem in faults:
        print(f"{tool} {pinned}: {problem}")
    if not faults:
        print(f"all {len(TOOL_SPECS)} pins in {LOCK_REL} are the latest stable releases")
        return 0

    newer = [(tool, pinned, latest_ver, latest) for tool, pinned, latest_ver, latest, _ in faults
             if latest_ver is not None and latest is not None]
    if mode == "check" or len(newer) != len(faults):
        return 1

    if mode == "pr":
        try:
            resolved = [(tool, pinned, resolve_update_checksum(tool, latest, download=download))
                        for tool, pinned, _, latest in newer]
        except CannotCheck as exc:
            print(f"check-tool-pins: {exc}", file=sys.stderr)
            return 2
        return open_pr(resolved, root=root, run_gh=run_gh)

    lines = [f"{tool} {pinned} -> {latest_ver}" for tool, pinned, latest_ver, _ in newer]
    key = "tools-" + "-".join(f"{tool}-{latest_ver}" for tool, _, latest_ver, _ in newer)
    return pin_proposal.notice(
        root=root,
        key=key,
        subject="Recto: newer .tools/ build tool releases",
        body="\n".join(
            [f"Newer stable releases for {LOCK_REL}:"]
            + lines
            + ["", "Update build/tools.lock.json and verify with build.cmd (foundation plan D4)."]
        ),
        mail=mail,
    )


if __name__ == "__main__":
    sys.exit(main())
