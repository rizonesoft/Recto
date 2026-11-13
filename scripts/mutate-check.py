#!/usr/bin/env python3
"""Run isolated single-occurrence mutation checks in a temporary copy outside the live working tree.

    python scripts/mutate-check.py --file <relpath> --old <text> --new <text> -- <test command...>

Copies only tracked + non-ignored untracked files (excluding `build/` and `.git/`) into a temporary directory, runs the
command unmutated once to verify a green baseline, applies the exact single-occurrence replacement to `--file`, runs the
command again, prints a structured summary (`KILLED`, `SURVIVED`, or `NOT_APPLIED`), and deletes the temporary copy.
The live repository is never modified, so read-only helpers (`gemini-4-hydrogen-high` and Claude `mutation-tester`) can
prove tests fail without touching uncommitted work. Standard library only.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def windowless() -> int:
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


def stage_tree(root: Path, dest: Path) -> None:
    listed = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        capture_output=True, timeout=60, creationflags=windowless(),
    )
    if listed.returncode != 0:
        raise RuntimeError(f"git ls-files failed: {listed.stderr.decode('utf-8', 'replace').strip()}")
    for raw in listed.stdout.split(b"\0"):
        if not raw:
            continue
        rel = raw.decode("utf-8", "surrogateescape")
        src = root / rel
        if not src.is_file() or src.is_symlink():
            continue
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)


def run_cmd(cmd: list[str], cwd: Path, timeout_s: int) -> tuple[int, str]:
    done = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=timeout_s, creationflags=windowless())
    combined = ((done.stdout or "") + "\n" + (done.stderr or "")).strip()
    tail = "\n".join(combined.splitlines()[-20:])
    return done.returncode, tail


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Verify a test catches a single-occurrence mutation in a temp copy.")
    parser.add_argument("--root", default=str(ROOT), help="repository root (default: Recto)")
    parser.add_argument("--file", required=True, help="repository-relative file to mutate inside the temp copy")
    parser.add_argument("--old", required=True, help="exact text to replace (must occur once)")
    parser.add_argument("--new", required=True, help="replacement text")
    parser.add_argument("--timeout", type=int, default=180, help="per-run command timeout in seconds")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="test command after --")
    args = parser.parse_args(argv)

    cmd = list(args.command)
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        print("mutate-check: provide a test command after '--'", file=sys.stderr)
        return 2

    root = Path(args.root).resolve()
    rel = Path(args.file)
    if rel.is_absolute() or ".." in rel.parts:
        print(f"mutate-check: --file must be a relative path inside the repository: {args.file}", file=sys.stderr)
        return 2
    live_target = root / rel
    if not live_target.is_file():
        print(f"mutate-check: target file does not exist: {args.file}", file=sys.stderr)
        return 2

    original = live_target.read_text(encoding="utf-8")
    count = original.count(args.old)
    if count != 1:
        print(f"OUTCOME: NOT_APPLIED (occurrences={count})")
        return 2

    with tempfile.TemporaryDirectory(prefix="recto-mutate-") as tmp:
        copy_root = Path(tmp) / "tree"
        copy_root.mkdir()
        stage_tree(root, copy_root)
        base_rc, base_tail = run_cmd(cmd, copy_root, args.timeout)
        if base_rc != 0:
            print(f"OUTCOME: BASELINE_RED (exit={base_rc})\n{base_tail}")
            return 1
        mutated = original.replace(args.old, args.new, 1)
        (copy_root / rel).write_text(mutated, encoding="utf-8", newline="\n")
        mut_rc, mut_tail = run_cmd(cmd, copy_root, args.timeout)
        if mut_rc != 0:
            print(f"OUTCOME: KILLED (exit={mut_rc})\n{mut_tail}")
            return 0
        print("OUTCOME: SURVIVED (exit=0)")
        return 1


if __name__ == "__main__":
    sys.exit(main())
