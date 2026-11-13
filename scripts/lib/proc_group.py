"""Lifetime-bound subprocess group / Win32 Job Object execution for Recto's test and mutation runners.

On Windows, `taskkill /T /PID <parent>` cannot find surviving grandchildren once `<parent>` has already exited, and any
surviving descendant that inherited `stdout`/`stderr` pipes keeps `Popen.communicate()` blocked indefinitely. Starting
the child `CREATE_SUSPENDED`, assigning it to a `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` Win32 Job Object before resuming
its initial thread, and terminating that Job Object on timeout, cancellation, or completion guarantees every descendant
is reaped even after parent exit. On POSIX, `start_new_session=True` + `os.killpg(..., SIGKILL)` provides the matching
process-group lifetime guarantee. Standard library only.
"""
from __future__ import annotations

import ctypes
import os
from pathlib import Path
import signal
import subprocess
import threading

CREATE_NEW_PROCESS_GROUP = 0x00000200
CREATE_SUSPENDED = 0x00000004
CREATE_NO_WINDOW = 0x08000000
JOB_OBJECT_EXTENDED_LIMIT_INFORMATION_CLASS = 9
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000


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


class ManagedProcess:
    """Subprocess bound to a Win32 Job Object (Windows) or session PGID (POSIX) with idempotent tree termination."""

    def __init__(self, proc: subprocess.Popen, job: int | None = None):
        self.proc = proc
        self._job = job
        self._lock = threading.Lock()
        self._closed = False

    def terminate_and_close(self) -> None:
        """Terminate every surviving process in the job/group and release OS handles at most once."""
        with self._lock:
            if self._closed:
                return
            self._closed = True
            job = self._job
            self._job = None
        if os.name == "nt":
            if job is not None:
                kernel32, _ = _win32_apis()
                kernel32.TerminateJobObject(job, 1)
                kernel32.CloseHandle(job)
        else:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except (ProcessLookupError, OSError):
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


def spawn_managed_process(
    cmd: list[str],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
) -> ManagedProcess:
    """Start `cmd` inside a lifetime-bound Win32 Job Object or POSIX session before it executes user code."""
    if os.name == "nt":
        job = _create_configured_job()
        proc: subprocess.Popen | None = None
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=cwd,
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                creationflags=CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED,
            )
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

    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        start_new_session=True,
    )
    return ManagedProcess(proc, None)
