#!/usr/bin/env python3
"""Run isolated single-occurrence mutation checks in a temporary copy outside the live working tree.

    python scripts/mutate-check.py --file <relpath> --old <text> --new <text> -- <test command...>

Copies only tracked + non-ignored untracked files (excluding `build/` and `.git/`) into a temporary directory, runs the
command unmutated once to verify a green baseline, applies the exact single-occurrence replacement to `--file`, runs the
command again, prints a structured summary (`KILLED`, `SURVIVED`, or `NOT_APPLIED`), and deletes the temporary copy.
The live repository is never modified, so the writer session (Gemini 4 Carbon or Claude) and Claude's `mutation-tester`
can prove tests fail without dirtying uncommitted work. Standard library only.
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
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
import lib.proc_group as proc_group  # noqa: E402


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


NATIVE_BUILD_EXTS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".ixx", ".cppm",
    ".rc", ".vcxproj", ".props", ".targets",
}
BUILD_DRIVERS = {"build.cmd", "build.ps1", "msbuild", "msbuild.exe", "cl", "cl.exe", "clang-cl", "clang-cl.exe"}


def _is_native_build_command(cmd: list[str]) -> bool:
    for tok in cmd[:4]:
        base = os.path.basename(tok.replace("\\", "/")).lower()
        if base in BUILD_DRIVERS:
            return True
    return False


def _resolve_cwd_command(cmd: list[str], cwd: Path) -> list[str]:
    head = cmd[0]
    cand = (cwd / head).resolve() if not Path(head).is_absolute() else Path(head)
    if cand.is_file() and cand.is_relative_to(cwd.resolve()):
        return [str(cand), *cmd[1:]]
    return cmd


def run_cmd(cmd: list[str], cwd: Path, timeout_s: float) -> tuple[int | None, str]:
    resolved_cmd = _resolve_cwd_command(cmd, cwd)
    with proc_group.termination_signal_guard():
        managed = proc_group.spawn_managed_process(resolved_cmd, cwd=cwd)
        proc = managed.proc
        try:
            out, err = proc.communicate(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            out, err = managed.drain_after_terminate()
            combined = ((out or "") + "\n" + (err or "")).strip()
            tail = "\n".join(combined.splitlines()[-20:])
            return None, (f"timed out after {timeout_s:g}s\n{tail}".strip())
        except BaseException:
            managed.drain_after_terminate()
            raise
        finally:
            managed.terminate_and_close()
    combined = ((out or "") + "\n" + (err or "")).strip()
    tail = "\n".join(combined.splitlines()[-20:])
    return proc.returncode, tail


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Verify a test catches a single-occurrence mutation in a temp copy.")
    parser.add_argument("--root", default=str(ROOT), help="repository root (default: Recto)")
    parser.add_argument("--file", required=True, help="repository-relative file to mutate inside the temp copy")
    parser.add_argument("--old", required=True, help="exact text to replace (must occur once)")
    parser.add_argument("--new", required=True, help="replacement text")
    parser.add_argument("--timeout", type=float, default=600.0, help="per-run command timeout in seconds")
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
    if rel.suffix.lower() in NATIVE_BUILD_EXTS and not _is_native_build_command(cmd):
        print(
            f"mutate-check: mutating native/MSBuild file {args.file!r} requires a build-and-test command "
            "(such as .\\build.cmd -Quick -Project <proj> -TestFilter <pat>) so the temporary copy recompiles; "
            "pre-built binaries in ignored build/ are absent from the staged tree and would not reflect the mutation",
            file=sys.stderr,
        )
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
        if base_rc is None:
            print(f"OUTCOME: BASELINE_TIMEOUT\n{base_tail}")
            return 1
        if base_rc != 0:
            print(f"OUTCOME: BASELINE_RED (exit={base_rc})\n{base_tail}")
            return 1
        mutated = original.replace(args.old, args.new, 1)
        (copy_root / rel).write_text(mutated, encoding="utf-8", newline="\n")
        mut_rc, mut_tail = run_cmd(cmd, copy_root, args.timeout)
        if mut_rc is None:
            print(f"OUTCOME: KILLED (timeout)\n{mut_tail}")
            return 0
        if mut_rc != 0:
            print(f"OUTCOME: KILLED (exit={mut_rc})\n{mut_tail}")
            return 0
        print("OUTCOME: SURVIVED (exit=0)")
        return 1


if __name__ == "__main__":
    sys.exit(main())
