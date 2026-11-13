"""Lifetime-bound subprocess group / Win32 Job Object execution for Recto's test and mutation runners.

On Windows, `taskkill /T /PID <parent>` cannot find surviving grandchildren once `<parent>` has already exited, and any
surviving descendant that inherited `stdout`/`stderr` pipes keeps `Popen.communicate()` blocked indefinitely. Starting
the child `CREATE_SUSPENDED`, assigning it to a `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` Win32 Job Object before resuming
its initial thread, and terminating that Job Object on timeout, cancellation, or completion guarantees every descendant
is reaped even after parent exit. On POSIX (Linux, WSL, macOS), descendants that call `setsid()` (`start_new_session`)
or reparent to `init` escape a single `os.killpg(proc.pid, SIGKILL)`: we tag the child tree's environment with a unique
`_RECTO_MANAGED_GROUP` token, freeze (`SIGSTOP`) and walk `/proc` (both `ppid` edges and `/proc/<pid>/environ`, with a
`ps` fallback where `/proc` is absent), re-scan while frozen, and `SIGKILL` every discovered process group and PID.
Standard library only.
"""
from __future__ import annotations

import atexit
from collections.abc import Iterator
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
from typing import Any
import uuid
import weakref

CREATE_NEW_PROCESS_GROUP = 0x00000200
CREATE_SUSPENDED = 0x00000004
CREATE_NO_WINDOW = 0x08000000
JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS = 9
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
_GROUP_ENV_KEY = "_RECTO_MANAGED_GROUP"


class _IO_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("ReadOperationCount", ctypes.c_uint64),
        ("WriteOperationCount", ctypes.c_uint64),
        ("OtherOperationCount", ctypes.c_uint64),
        ("ReadTransferCount", ctypes.c_uint64),
        ("WriteTransferCount", ctypes.c_uint64),
        ("OtherTransferCount", ctypes.c_uint64),
    ]


class _JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_int64),
        ("PerJobUserTimeLimit", ctypes.c_int64),
        ("LimitFlags", ctypes.c_uint32),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", ctypes.c_uint32),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", ctypes.c_uint32),
        ("SchedulingClass", ctypes.c_uint32),
    ]


class _JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", _JOBOBJECT_BASIC_LIMIT_INFORMATION),
        ("IoInfo", _IO_COUNTERS),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


_WIN32_APIS: tuple[ctypes.WinDLL, ctypes.WinDLL] | None = None
_WIN32_LOCK = threading.Lock()


def windowless() -> int:
    return CREATE_NO_WINDOW if os.name == "nt" else 0


def _win32_apis() -> tuple[ctypes.WinDLL, ctypes.WinDLL]:
    global _WIN32_APIS
    with _WIN32_LOCK:
        if _WIN32_APIS is None:
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
            kernel32.CreateJobObjectW.restype = ctypes.c_void_p
            kernel32.SetInformationJobObject.argtypes = [
                ctypes.c_void_p,
                ctypes.c_int,
                ctypes.c_void_p,
                ctypes.c_uint32,
            ]
            kernel32.SetInformationJobObject.restype = ctypes.c_int
            kernel32.AssignProcessToJobObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
            kernel32.AssignProcessToJobObject.restype = ctypes.c_int
            kernel32.TerminateJobObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            kernel32.TerminateJobObject.restype = ctypes.c_int
            kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            kernel32.CloseHandle.restype = ctypes.c_int
            ntdll = ctypes.WinDLL("ntdll")
            ntdll.NtResumeProcess.argtypes = [ctypes.c_void_p]
            ntdll.NtResumeProcess.restype = ctypes.c_int32
            _WIN32_APIS = (kernel32, ntdll)
        return _WIN32_APIS


def _create_configured_job() -> int:
    kernel32, _ = _win32_apis()
    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())
    info = _JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    if not kernel32.SetInformationJobObject(
        job, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS, ctypes.byref(info), ctypes.sizeof(info)
    ):
        err = ctypes.get_last_error()
        kernel32.CloseHandle(job)
        raise ctypes.WinError(err)
    return int(job)


_LIVE_MANAGED: weakref.WeakSet[ManagedProcess] = weakref.WeakSet()
_LIVE_LOCK = threading.Lock()

# Kernel-EOF lifeline reaper for POSIX: blocks at 0% CPU on stdin (`life_r`) until either `terminate_and_close()`
# closes `life_w` or the parent runner dies from `SIGKILL`, then reaps every descendant across `setsid()` sessions.
_POSIX_LIFELINE_REAPER = """
import os, pathlib, signal, subprocess, sys

root_pid = int(sys.argv[1])
token = sys.argv[2]
needle = ("_RECTO_MANAGED_GROUP=" + token).encode("ascii") if token else b""
try:
    while sys.stdin.buffer.read(4096):
        pass
except BaseException:
    pass

def discover():
    me = os.getpid()
    my_pg = os.getpgrp() if hasattr(os, "getpgrp") else -1
    children = {}
    pgids = {}
    seeds = {root_pid} if root_pid > 1 else set()
    proc_root = pathlib.Path("/proc")
    if proc_root.is_dir():
        for entry in proc_root.iterdir():
            if not entry.name.isdigit():
                continue
            pid = int(entry.name)
            if pid == me:
                continue
            try:
                raw = (entry / "stat").read_bytes()
                rp = raw.rfind(b")")
                if rp > 0:
                    f = raw[rp + 2:].split()
                    if len(f) >= 3:
                        ppid, pgid = int(f[1]), int(f[2])
                        children.setdefault(ppid, set()).add(pid)
                        pgids[pid] = pgid
            except OSError:
                continue
            if needle:
                try:
                    env_raw = (entry / "environ").read_bytes()
                    if needle in env_raw.split(b"\\0"):
                        seeds.add(pid)
                except OSError:
                    pass
    else:
        try:
            ps = subprocess.run(["ps", "-axo", "pid=,ppid=,pgid="], capture_output=True, text=True, timeout=2.0)
            for row in ps.stdout.splitlines():
                parts = row.split()
                if len(parts) >= 3 and all(p.lstrip("-").isdigit() for p in parts[:3]):
                    pid, ppid, pgid = int(parts[0]), int(parts[1]), int(parts[2])
                    if pid != me:
                        children.setdefault(ppid, set()).add(pid)
                        pgids[pid] = pgid
        except Exception:
            pass
    seen = set()
    queue = list(seeds)
    while queue:
        cur = queue.pop()
        if cur in seen:
            continue
        seen.add(cur)
        queue.extend(children.get(cur, ()))
    t_pids = {p for p in seen if p > 1 and p != me}
    t_pgs = ({root_pid} if root_pid > 1 and root_pid != my_pg else set()) | {
        pgids[p] for p in t_pids if pgids.get(p, 0) > 1 and pgids[p] != my_pg
    }
    return t_pids, t_pgs

sigstop = getattr(signal, "SIGSTOP", None)
sigkill = getattr(signal, "SIGKILL", signal.SIGTERM)
killpg = getattr(os, "killpg", None)
p1, g1 = discover()
if sigstop is not None:
    for g in g1:
        try:
            if killpg: killpg(g, sigstop)
        except OSError:
            pass
    for p in p1:
        try:
            os.kill(p, sigstop)
        except OSError:
            pass
p2, g2 = discover()
for g in (g1 | g2):
    try:
        if killpg: killpg(g, sigkill)
    except OSError:
        pass
for p in (p1 | p2):
    try:
        os.kill(p, sigkill)
    except OSError:
        pass
"""


def _discover_posix_targets(
    root_pid: int,
    group_token: str | None,
    *,
    proc_root: Path = Path("/proc"),
) -> tuple[set[int], set[int]]:
    """Find every descendant PID and PGID of `root_pid` across `setsid()` boundaries and `init` reparenting."""
    self_pid = os.getpid()
    getpgrp = getattr(os, "getpgrp", None)
    raw_pgid = getpgrp() if callable(getpgrp) else -1
    self_pgid = raw_pgid if isinstance(raw_pgid, int) else -1
    children_by_ppid: dict[int, set[int]] = {}
    pgid_by_pid: dict[int, int] = {}
    seeds: set[int] = {root_pid} if root_pid > 1 else set()
    needle = f"{_GROUP_ENV_KEY}={group_token}".encode("ascii") if group_token else b""

    if proc_root.is_dir():
        try:
            entries = list(proc_root.iterdir())
        except OSError:
            entries = []
        for entry in entries:
            if not entry.name.isdigit():
                continue
            pid = int(entry.name)
            if pid == self_pid:
                continue
            try:
                raw = (entry / "stat").read_bytes()
                rparen = raw.rfind(b")")
                if rparen > 0:
                    fields = raw[rparen + 2:].split()
                    if len(fields) >= 3:
                        ppid = int(fields[1])
                        pgid = int(fields[2])
                        children_by_ppid.setdefault(ppid, set()).add(pid)
                        pgid_by_pid[pid] = pgid
            except (OSError, ValueError):
                continue
            if needle:
                try:
                    env_raw = (entry / "environ").read_bytes()
                    if needle in env_raw.split(b"\0"):
                        seeds.add(pid)
                except OSError:
                    pass
    else:
        try:
            ps = subprocess.run(
                ["ps", "-axo", "pid=,ppid=,pgid="],
                capture_output=True,
                text=True,
                timeout=2.0,
            )
            for row in ps.stdout.splitlines():
                parts = row.split()
                if len(parts) >= 3 and all(p.lstrip("-").isdigit() for p in parts[:3]):
                    pid, ppid, pgid = int(parts[0]), int(parts[1]), int(parts[2])
                    if pid != self_pid:
                        children_by_ppid.setdefault(ppid, set()).add(pid)
                        pgid_by_pid[pid] = pgid
        except (OSError, subprocess.SubprocessError, ValueError):
            pass

    visited: set[int] = set()
    queue = list(seeds)
    while queue:
        cur = queue.pop()
        if cur in visited:
            continue
        visited.add(cur)
        queue.extend(children_by_ppid.get(cur, ()))

    target_pids = {p for p in visited if p > 1 and p != self_pid}
    target_pgids: set[int] = set()
    if root_pid > 1 and root_pid != self_pgid:
        target_pgids.add(root_pid)
    for pid in target_pids:
        pgid = pgid_by_pid.get(pid, 0)
        if pgid > 1 and pgid != self_pgid:
            target_pgids.add(pgid)
    return target_pids, target_pgids


def _kill_posix_tree(
    root_pid: int,
    group_token: str | None,
    *,
    proc_root: Path = Path("/proc"),
) -> None:
    """Freeze (`SIGSTOP`), re-scan, and `SIGKILL` every descendant PGID and PID across `setsid()` boundaries."""
    killpg = getattr(os, "killpg", None)
    sigstop = getattr(signal, "SIGSTOP", None)
    sigkill = getattr(signal, "SIGKILL", signal.SIGTERM)

    pids1, pgids1 = _discover_posix_targets(root_pid, group_token, proc_root=proc_root)
    if sigstop is not None:
        if callable(killpg):
            for pgid in pgids1:
                try:
                    killpg(pgid, sigstop)
                except OSError:
                    pass
        for pid in pids1:
            try:
                os.kill(pid, sigstop)
            except OSError:
                pass

    pids2, pgids2 = _discover_posix_targets(root_pid, group_token, proc_root=proc_root)
    if callable(killpg):
        for pgid in pgids1 | pgids2:
            try:
                killpg(pgid, sigkill)
            except OSError:
                pass
    for pid in pids1 | pids2:
        try:
            os.kill(pid, sigkill)
        except OSError:
            pass


class ManagedProcess:
    """Subprocess bound to a Win32 Job Object (Windows) or lifeline-watched tree (POSIX) with idempotent cleanup."""

    def __init__(
        self,
        proc: subprocess.Popen,
        job: int | None = None,
        *,
        group_token: str | None = None,
        lifeline_fd: int | None = None,
        reaper_proc: subprocess.Popen | None = None,
    ):
        self.proc = proc
        self._job = job
        self._group_token = group_token
        self._lifeline_fd = lifeline_fd
        self._reaper_proc = reaper_proc
        self._lock = threading.Lock()
        self._closed = False
        with _LIVE_LOCK:
            _LIVE_MANAGED.add(self)

    def terminate_and_close(self) -> None:
        """Terminate every surviving process in the job/tree and release OS handles at most once."""
        with self._lock:
            if self._closed:
                return
            self._closed = True
            job = self._job
            self._job = None
            lifeline_fd = self._lifeline_fd
            self._lifeline_fd = None
            reaper_proc = self._reaper_proc
            self._reaper_proc = None
        with _LIVE_LOCK:
            _LIVE_MANAGED.discard(self)

        if os.name == "nt":
            if job is not None:
                kernel32, _ = _win32_apis()
                kernel32.TerminateJobObject(job, 1)
                kernel32.CloseHandle(job)
        else:
            _kill_posix_tree(self.proc.pid, self._group_token)
            if lifeline_fd is not None:
                try:
                    os.close(lifeline_fd)
                except OSError:
                    pass
            if reaper_proc is not None:
                try:
                    reaper_proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    try:
                        reaper_proc.kill()
                        reaper_proc.wait(timeout=1.0)
                    except (OSError, subprocess.TimeoutExpired):
                        pass
        try:
            self.proc.kill()
        except OSError:
            pass

    def drain_after_terminate(self, drain_timeout: float = 2.0) -> tuple[str, str]:
        """Terminate the job/group and drain pipes with a bounded timeout so `communicate()` can never hang."""
        self.terminate_and_close()
        try:
            out, err = self.proc.communicate(timeout=drain_timeout)
            return out or "", err or ""
        except subprocess.TimeoutExpired:
            for stream in (self.proc.stdout, self.proc.stderr):
                if stream is not None:
                    try:
                        stream.close()
                    except OSError:
                        pass
            try:
                self.proc.wait(timeout=drain_timeout)
            except subprocess.TimeoutExpired:
                pass
            return "", ""


def _cleanup_all_live_managed() -> None:
    with _LIVE_LOCK:
        items = list(_LIVE_MANAGED)
    for item in items:
        try:
            item.terminate_and_close()
        except BaseException:
            pass


atexit.register(_cleanup_all_live_managed)


@contextmanager
def termination_signal_guard() -> Iterator[None]:
    """Convert `SIGTERM`/`SIGHUP` into `KeyboardInterrupt` on the main thread so `finally` blocks reap child trees."""
    if threading.current_thread() is not threading.main_thread():
        yield
        return

    def _raise_interrupt(_signum: int, _frame: object) -> None:
        raise KeyboardInterrupt()

    saved: list[tuple[signal.Signals, object]] = []
    for sig_name in ("SIGTERM", "SIGHUP"):
        sig = getattr(signal, sig_name, None)
        if sig is None:
            continue
        try:
            prev = signal.getsignal(sig)
            signal.signal(sig, _raise_interrupt)
            saved.append((sig, prev))
        except (OSError, ValueError):
            pass
    try:
        yield
    finally:
        for sig, prev in saved:
            try:
                signal.signal(sig, prev)  # type: ignore[arg-type]
            except (OSError, ValueError):
                pass


def _passthrough_stream(stream: Any) -> Any:
    """Return `stream` when backed by a real OS file descriptor so `STARTF_USESTDHANDLES` stays populated on Windows."""
    fileno = getattr(stream, "fileno", None)
    if callable(fileno):
        try:
            fileno()
            return stream
        except (OSError, ValueError):
            return None
    return None


def spawn_managed_process(
    cmd: list[str],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
    capture_output: bool = True,
    inherit_stdin: bool = False,
) -> ManagedProcess:
    """Start `cmd` inside a lifetime-bound Win32 Job Object or POSIX lifeline-watched tree."""
    stdin_arg: Any = _passthrough_stream(sys.stdin) if inherit_stdin else subprocess.DEVNULL
    stdout_arg: Any = subprocess.PIPE if capture_output else _passthrough_stream(sys.stdout)
    stderr_arg: Any = subprocess.PIPE if capture_output else _passthrough_stream(sys.stderr)
    text_kwargs: dict[str, Any] = (
        {"text": True, "encoding": "utf-8", "errors": "replace"} if capture_output else {}
    )

    if os.name == "nt":
        job = _create_configured_job()
        proc: subprocess.Popen | None = None
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=cwd,
                env=env,
                stdin=stdin_arg,
                stdout=stdout_arg,
                stderr=stderr_arg,
                creationflags=CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED,
                **text_kwargs,
            )
            assert proc is not None
            kernel32, ntdll = _win32_apis()
            raw_handle = ctypes.c_void_p(int(getattr(proc, "_handle")))
            if not kernel32.AssignProcessToJobObject(job, raw_handle):
                raise ctypes.WinError(ctypes.get_last_error())
            status = ntdll.NtResumeProcess(raw_handle)
            if status != 0:
                raise OSError(f"NtResumeProcess failed with NTSTATUS 0x{status & 0xFFFFFFFF:08x}")
            return ManagedProcess(proc, job)
        except BaseException:
            kernel32, _ = _win32_apis()
            if proc is not None:
                try:
                    proc.kill()
                except OSError:
                    pass
                for stream in (proc.stdout, proc.stderr):
                    if stream is not None:
                        try:
                            stream.close()
                        except OSError:
                            pass
                try:
                    proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    pass
            kernel32.CloseHandle(job)
            raise

    group_token = uuid.uuid4().hex
    child_env = dict(os.environ if env is None else env)
    child_env[_GROUP_ENV_KEY] = group_token
    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        env=child_env,
        stdin=stdin_arg,
        stdout=stdout_arg,
        stderr=stderr_arg,
        start_new_session=True,
        **text_kwargs,
    )
    life_w: int | None = None
    reaper: subprocess.Popen | None = None
    try:
        life_r, life_w = os.pipe()
        try:
            reaper_env = dict(os.environ)
            reaper_env.pop(_GROUP_ENV_KEY, None)
            reaper_env["PYTHONDONTWRITEBYTECODE"] = "1"
            reaper = subprocess.Popen(
                [sys.executable, "-B", "-c", _POSIX_LIFELINE_REAPER, str(proc.pid), group_token],
                stdin=life_r,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env=reaper_env,
                start_new_session=True,
            )
        finally:
            os.close(life_r)
    except OSError:
        if life_w is not None:
            try:
                os.close(life_w)
            except OSError:
                pass
            life_w = None
    return ManagedProcess(
        proc,
        None,
        group_token=group_token,
        lifeline_fd=life_w,
        reaper_proc=reaper,
    )
