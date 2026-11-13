"""Shared code of the unattended kit: the run profile loader, the launch lock and claim, and the window flag every
process the kit starts carries; the ship counter comes with D02 T02 §8.

The preflight (`scripts/unattended-preflight.py`) and the launcher (`scripts/unattended-run.py`) import it.
It holds no project name: the project's own settings live in `.conclave/unattended.json` (the run profile).
"""
from __future__ import annotations

import contextlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

SCHEMA_VERSION = 1
PROFILE = ".conclave/unattended.json"
AGENTS = ("claude", "codex", "agy")
# Each agent's own effort values: Claude Code `--effort`, Codex `model_reasoning_effort`, Antigravity `--effort`
# (`agy --help`, 1.5.5: low|medium|high|xhigh|max; D02 T02 §6).
EFFORTS = {"claude": ("low", "medium", "high", "xhigh", "max"), "codex": ("minimal", "low", "medium", "high", "xhigh"),
           "agy": ("low", "medium", "high", "xhigh", "max")}
NAME = re.compile(r"[a-z0-9][a-z0-9-]{1,40}")
WHOLE = ("min_disk_gb", "min_ram_gb", "tick_seconds", "idle_minutes", "stall_hours")
KEYS = {"schema_version", "name", "agent", "model", "effort", "prompt_file", "branch", "skip_permissions", "tools",
        "preflight", *WHOLE}
MUST_SET = ("name", "model", "effort")


def windowless() -> int:
    """The creation flag every process the kit starts carries: no console window of its own on Windows (a console
    child of a parent with none opens one over the operator's work), and nothing elsewhere."""
    return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0


class ProfileError(ValueError):
    """A run profile the launcher cannot run, named with the file and the key."""


def load_profile(root) -> dict:
    """The run profile, every key checked: a fault names `.conclave/unattended.json` and the key, never a default."""
    root = Path(root)
    path = root / PROFILE

    def fault(key: str, what: str) -> ProfileError:
        return ProfileError(f"{PROFILE}: {key}: {what}")

    try:
        data = json.loads(path.read_bytes().decode("utf-8"))
    except FileNotFoundError:
        raise ProfileError(f"{PROFILE}: missing; install the unattended kit or write the run profile") from None
    except (OSError, ValueError, RecursionError) as exc:
        raise ProfileError(f"{PROFILE}: not readable as UTF-8 JSON ({exc})") from None
    if not isinstance(data, dict):
        raise ProfileError(f"{PROFILE}: the profile must be a JSON object")
    unknown = sorted(set(data) - KEYS)
    if unknown:
        raise fault(unknown[0], "is not a run profile key")
    missing = sorted(KEYS - set(data))
    if missing:
        raise fault(missing[0], "is required")
    if data["schema_version"] != SCHEMA_VERSION or isinstance(data["schema_version"], bool):
        raise fault("schema_version", f"must be {SCHEMA_VERSION}")
    unset = [key for key in MUST_SET if data[key] is None]
    if unset:
        raise fault(unset[0], f"is null in the template; set {', '.join(unset)} for this project")
    # A NUL cannot reach a program's arguments or a file name: refuse it here, not as a ValueError at launch.
    for key in ("model", "effort", "branch", "prompt_file"):
        if isinstance(data[key], str) and "\0" in data[key]:
            raise fault(key, "must not contain a NUL character")
    if not isinstance(data["name"], str) or not NAME.fullmatch(data["name"]):
        raise fault("name", "must match [a-z0-9][a-z0-9-]{1,40}")
    if data["agent"] not in AGENTS:
        raise fault("agent", f"must be one of {', '.join(AGENTS)}")
    for key in ("model", "effort", "branch"):
        if not isinstance(data[key], str) or not data[key].strip():
            raise fault(key, "must be a non-empty string")
    efforts = EFFORTS[data["agent"]]
    if data["effort"] not in efforts:
        raise fault("effort", f"must be one of {', '.join(efforts)} for {data['agent']}")
    if not isinstance(data["skip_permissions"], bool):
        raise fault("skip_permissions", "must be true or false")
    for key in WHOLE:
        value = data[key]
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise fault(key, "must be a whole number above 0")
    tools = data["tools"]
    if not isinstance(tools, list) or not all(isinstance(t, str) and t and "\0" not in t for t in tools):
        raise fault("tools", "must be a list of command names")
    prompt = data["prompt_file"]
    parts = prompt.replace("\\", "/").split("/") if isinstance(prompt, str) else []
    if (not isinstance(prompt, str) or not prompt or os.path.isabs(prompt) or prompt.startswith(("/", "\\"))
            or re.match(r"^[A-Za-z]:", prompt) or ".." in parts):
        raise fault("prompt_file", "must be a path inside the repository")
    target = (root / prompt).resolve()
    if not target.is_relative_to(root.resolve()):
        raise fault("prompt_file", f"{prompt} leads outside the repository")
    if not target.is_file():
        raise fault("prompt_file", f"{prompt} does not exist or is not a file")
    try:
        target.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise fault("prompt_file", f"{prompt} is not readable as UTF-8 ({exc})") from None
    if not isinstance(data["preflight"], list):
        raise fault("preflight", "must be a list of checks")
    for index, check in enumerate(data["preflight"]):
        where = f"preflight[{index}]"
        if not isinstance(check, dict) or set(check) != {"name", "command", "timeout_seconds"}:
            raise fault(where, "needs exactly name, command and timeout_seconds")
        if not isinstance(check["name"], str) or not check["name"]:
            raise fault(f"{where}.name", "must be a non-empty string")
        command = check["command"]
        if (not isinstance(command, list) or not command or not all(isinstance(c, str) for c in command)
                or not command[0].strip() or any("\0" in c for c in command)):
            raise fault(f"{where}.command", "must be a list of strings that starts with a program")
        timeout = check["timeout_seconds"]
        if isinstance(timeout, bool) or not isinstance(timeout, int) or timeout <= 0:
            raise fault(f"{where}.timeout_seconds", "must be a whole number above 0")
    return data


# The ship counter (D02 T02 §8). A section ships when its Implementation Order row turns `[x]`, which only the todo
# kit's stamp does; what a row is belongs to the todo kit (`ROW` in scripts/lib/todo_graph.py), so it is read from
# there, not copied.
TODO_GRAPH = "todo_graph.py"  # beside this file: the todo kit installs it in scripts/lib/


class ShipCountError(RuntimeError):
    """Ships that cannot be counted: git could not answer, or the todo kit's row pattern is not installed."""


_ROW = None


def _row_pattern():
    global _ROW
    if _ROW is None:
        import importlib.util
        import sys

        path = Path(__file__).resolve().parent / TODO_GRAPH
        name = f"unattended_todo_graph_{abs(hash(str(path)))}"
        spec = importlib.util.spec_from_file_location(name, path)
        if not path.is_file() or spec is None or spec.loader is None:
            raise ShipCountError(f"scripts/lib/{TODO_GRAPH} is missing: the unattended kit needs the todo kit")
        module = importlib.util.module_from_spec(spec)
        # Its dataclasses look their module up by name while it loads, so it is registered first (the importlib
        # recipe), and taken out again if it fails.
        sys.modules[name] = module
        try:
            spec.loader.exec_module(module)
            _ROW = module.ROW
        except Exception as exc:  # any failure to load it is a count that cannot be made, named
            sys.modules.pop(name, None)
            raise ShipCountError(f"scripts/lib/{TODO_GRAPH} could not be loaded ({type(exc).__name__}: {exc})") \
                from None
    return _ROW


# A TODO file, by the todo kit's tree shape (todo/README.md, "Tree shape"): `todo/<NN-domain>/TODO-NN-<name>.md`. Its
# README, index, plan and templates hold rows of the same shape that ship nothing.
TODO_FILE = re.compile(r"todo/[0-9]{2}-[^/]+/TODO-[0-9]{2}-[^/]+\.md")
ORDER_TABLE = re.compile(r"^## Implementation Order[ \t]*\r?\n(.*?)(?=^## |\Z)", re.M | re.S)


def _unquote(name: str) -> str | None:
    """A path as git prints it, unquoting git's C-style quoting (`"todo/02-caf\303\251/..."`), which it keeps for a
    name with a control character even with core.quotePath off."""
    if not (name.startswith('"') and name.endswith('"')):
        return name
    import codecs

    try:
        return codecs.escape_decode(name[1:-1].encode("ascii", "backslashreplace"))[0].decode("utf-8")
    except (UnicodeDecodeError, ValueError):
        return None


def order_table(text: str) -> dict[int, str]:
    """The rows of the file's own Implementation Order table, section number to status (`" "` or `"x"`). A row of
    the same shape anywhere else (a section's body, an example) is not in it."""
    table = ORDER_TABLE.search(text or "")
    row = _row_pattern()
    found = {}
    for line in table.group(1).splitlines() if table else ():
        match = row.match(line.rstrip("\r"))
        if match:
            found[int(match.group(2))] = match.group(5)
    return found


def ships_in_commit(old_text: str, new_text: str) -> int:
    """Rows a commit turns from `[ ]` to `[x]` in one TODO file's Implementation Order table, the table read whole
    before and after the commit (Impossible OS `ship_count.py`, which read the diff: a diff pairs lines, not rows, so
    an example line that becomes the table row hides the ship). A row that only appears, already `[x]` (a file added
    or moved), is not a ship."""
    before, after = order_table(old_text), order_table(new_text)
    return sum(1 for number, status in after.items() if status == "x" and before.get(number, "x") != "x")


def _git(root, *args: str, timeout: int = 60) -> str:
    try:
        done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, timeout=timeout,
                              encoding="utf-8", errors="replace", creationflags=windowless())
    except (OSError, subprocess.SubprocessError) as exc:
        raise ShipCountError(f"git {args[0]} could not run ({exc})") from None
    if done.returncode != 0:
        raise ShipCountError(f"git {args[0]} failed: {(done.stderr or '').strip()[:200]}")
    return done.stdout


def _file_at(root, commit: str | None, path: str) -> str:
    """The file's text at `commit`, or empty when it is not in that commit (a file the commit added or removed, or no
    commit: the parent of a root commit). A read that fails raises ShipCountError, so the count waits for the next
    tick instead of reading the file as absent and moving past its ship."""
    if commit is None:
        return ""
    if not _git(root, "ls-tree", "--name-only", "-z", commit, "--", path).strip("\0"):
        return ""
    return _git(root, "show", f"{commit}:{path}")


def ships_along(root, old: str, new: str) -> list[tuple[str, int, int]]:
    """(commit, committer time, rows shipped) for each first-parent commit from `old` (excluded) to `new` that ships
    a row, in order: each TODO file the commit changed, its table at the commit against its table at the first
    parent. Never the working tree, and never the two endpoints alone, which miss a section reopened and shipped
    again inside the range."""
    _row_pattern()  # a project without the todo kit is refused even over a range with nothing in it
    # core.quotePath off: a non-ASCII TODO path arrives as itself (a path git still quotes is unquoted).
    out = _git(root, "-c", "core.quotePath=false", "log", "--first-parent", "--reverse", "--diff-merges=first-parent",
               "--name-only", "--format=%x00%H %ct %P", f"{old}..{new}", "--", "todo")
    found = []
    for chunk in out.split("\0")[1:]:
        head, _, names = chunk.partition("\n")
        sha, when, *parents = head.split()
        shipped = 0
        for name in filter(None, names.splitlines()):
            path = _unquote(name)
            if path and TODO_FILE.fullmatch(path):
                shipped += ships_in_commit(_file_at(root, parents[0] if parents else None, path),
                                           _file_at(root, sha, path))
        if shipped:
            found.append((sha, int(when), shipped))
    return found


def ships_between(root, old: str, new: str) -> int:
    """The sections shipped from `old` to `new`, commit by commit (`ships_along`)."""
    return sum(count for _sha, _when, count in ships_along(root, old, new))

def head_commit(root) -> str:
    return _git(root, "rev-parse", "--verify", "HEAD^{commit}").strip()


def is_ancestor(root, older: str, newer: str) -> bool:
    try:
        done = subprocess.run(["git", "-C", str(root), "merge-base", "--is-ancestor", older, newer],
                              capture_output=True, timeout=60, creationflags=windowless())
    except (OSError, subprocess.SubprocessError) as exc:
        raise ShipCountError(f"git merge-base could not run ({exc})") from None
    if done.returncode not in (0, 1):
        raise ShipCountError(f"git merge-base failed: {done.stderr.decode('utf-8', 'replace').strip()[:200]}")
    return done.returncode == 0


# The launch lock and claim (D02 T02 §4). Two launches at once would start two agents on one tree: the lock is held
# for the launch, and the claim names the run it started, so a later launch can tell a live run from a dead one.
LAUNCH_LOCK = "build/unattended-launch.lock"
CLAIM = "build/unattended-launch.json"
CLAIM_KEYS = ("session", "tmux_session", "pid", "agent", "started")
# What tmux says when it can confirm a session does not exist; anything else it says is a lookup that failed.
# A missing server socket (`error connecting to ... (No such file or directory)`) means no server, so no session; a
# socket that cannot be reached for another reason (permission) stays unknown.
TMUX_ABSENT = ("can't find session", "no server running", "session not found", "(no such file or directory)")


class LaunchError(RuntimeError):
    """A launch or clear this run must not make, named with the file that says why."""


@contextlib.contextmanager
def launch_lock(root):
    """Hold `build/unattended-launch.lock` exclusively for the launch, without waiting: a second launch while the
    first holds it is refused naming the lock. The kernel releases it when the launcher exits, however it exits.
    The run lives in tmux, on Linux and in WSL (flock); on Windows, where the kit's tick tests run, its first byte is
    locked instead (msvcrt.locking)."""
    path = Path(root) / LAUNCH_LOCK
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = open(path, "a+b")
    locked = False
    try:
        try:
            if os.name == "nt":
                import msvcrt

                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except (BlockingIOError, PermissionError, OSError) as exc:
            if isinstance(exc, (BlockingIOError, PermissionError)) or getattr(exc, "errno", None) in (13, 36):
                raise LaunchError(f"{LAUNCH_LOCK} is held: another launch is under way") from None
            raise
        yield path
    finally:
        if locked and os.name == "nt":
            import msvcrt

            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
        handle.close()


def write_atomic(path: Path, text: str) -> None:
    """Write `text` to `path` through a temporary file in its folder, flushed to disk, and os.replace."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass  # the replace already moved it
        raise


def write_claim(root, session: str, pid: int, agent: str, tmux_session: str, started: str | None = None) -> dict:
    """Write `build/unattended-launch.json` naming the run this launch started (the agent's session id, the tmux
    session the launcher started it in, `<name>-run` in §5, the pid, the agent and the UTC start), through a
    temporary file and os.replace, and return it. A launch writes it while holding `launch_lock`."""
    claim = {"session": session, "tmux_session": tmux_session, "pid": pid, "agent": agent,
             "started": started or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
    write_atomic(Path(root) / CLAIM, json.dumps(claim, indent=2) + "\n")
    return claim


def tmux_session_exists(name: str) -> bool | None:
    """True when tmux has a session called exactly `name`, False when tmux says it has none, and None when tmux
    could not be asked (not installed, timed out, a server it cannot reach): an unknown is never an absence."""
    try:
        done = subprocess.run(["tmux", "has-session", "-t", f"={name}"], capture_output=True, text=True, timeout=30,
                              creationflags=windowless())
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode == 0:
        return True
    said = (done.stderr or "").lower()
    return False if any(phrase in said for phrase in TMUX_ABSENT) else None


def read_claim(root, session_exists=tmux_session_exists) -> tuple[dict | None, bool | None, str]:
    """(claim, live, reason). Live is True when the claim's tmux session exists, False when it is stale (its session
    is gone, or the file is not JSON or lacks a key) and None when tmux could not say. No claim is (None, False)."""
    path = Path(root) / CLAIM
    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        return None, False, "no launch claim"
    except OSError as exc:  # it exists but cannot be read: a live run's claim may be behind it
        return None, None, f"{CLAIM} exists but cannot be read ({exc.strerror or exc}): whether its run is live " \
                           "cannot be told"
    try:
        claim = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as exc:
        return None, False, f"{CLAIM} is not readable JSON ({exc}): stale"
    if not isinstance(claim, dict):
        return None, False, f"{CLAIM} is not a JSON object: stale"
    missing = [key for key in CLAIM_KEYS if key not in claim]
    if missing:
        return claim, False, f"{CLAIM} lacks {', '.join(missing)}: stale"
    if not isinstance(claim["tmux_session"], str) or not claim["tmux_session"]:
        return claim, False, f"{CLAIM} names no tmux session: stale"
    exists = session_exists(claim["tmux_session"])
    if exists is None:
        return claim, None, (f"whether tmux session {claim['tmux_session']} of session {claim['session']} is live "
                             "cannot be told: tmux could not be asked")
    if exists:
        return claim, True, f"session {claim['session']} is live in tmux session {claim['tmux_session']}"
    return claim, False, f"tmux session {claim['tmux_session']} is gone: stale"


def clear_claim(root, session: str, session_exists=tmux_session_exists, lock_held: bool = False) -> str:
    """Remove the claim when it is stale or belongs to `session`, the run being stopped. It runs under the launch lock
    every launch holds while it writes its claim (`lock_held` when the caller already holds it), so no claim can be
    written while one is being cleared. A live claim of another session, one whose liveness is unknown, and one that
    changed after it was read are left in place and refused."""
    if not lock_held:
        with launch_lock(root):
            return clear_claim(root, session, session_exists, lock_held=True)
    path = Path(root) / CLAIM
    try:
        seen = path.read_bytes()
    except FileNotFoundError:
        return "no launch claim"
    except OSError as exc:
        raise LaunchError(f"{CLAIM} is left in place: it cannot be read ({exc.strerror or exc})") from None
    claim, live, reason = read_claim(root, session_exists)
    owner = claim.get("session") if isinstance(claim, dict) else None
    if live is None:
        raise LaunchError(f"{CLAIM} is left in place: {reason}")
    if live and owner != session:
        raise LaunchError(f"{CLAIM} belongs to live session {owner} (tmux session {claim.get('tmux_session')}); "
                          f"it is not {session}'s to clear")
    try:
        if path.read_bytes() != seen:
            raise LaunchError(f"{CLAIM} changed after it was read; the new claim is left in place")
        path.unlink()
    except FileNotFoundError:
        return "no launch claim"
    return f"cleared ({reason})" if not live else f"cleared (session {session} stopped)"
