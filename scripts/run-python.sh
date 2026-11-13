#!/bin/sh
# Cross-platform Python launcher for Recto hooks, LSP servers, and MCP servers (D00 T04 §6).
# On Windows, bootstraps and execs the pinned CPython in .tools/ even with no system python on PATH.
# On Linux/WSL or minimal temp-repo test fixtures without build/tools.lock.json, execs python3/python.
set -eu
me=$0
case "$me" in
    *\\*) me=$(printf '%s\n' "$me" | tr '\\' '/') ;;
esac
case "$me" in
    */*) script_dir=$(CDPATH= cd -- "${me%/*}" && pwd) ;;
    *)   script_dir=$(pwd) ;;
esac
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
if [ "${1:-}" = "--from-git-prefix" ]; then
    shift
    if [ -n "${GIT_PREFIX:-}" ] && [ -d "$root/$GIT_PREFIX" ]; then
        cd -- "$root/$GIT_PREFIX"
    fi
fi
if [ -f "$root/build/tools.lock.json" ] && [ "${OS:-}" = "Windows_NT" ] && [ ! -d /run/WSL ] && [ ! -e /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    tools="${RECTO_TOOLS_DIR:-$root/.tools}"
    exe_file="$tools/cpython-exe.txt"
    lock_ok="$tools/tools.lock.verified"
    py_exe=""
    if [ -f "$exe_file" ] && [ -f "$lock_ok" ] && cmp -s "$root/build/tools.lock.json" "$lock_ok"; then
        py_exe=$(tr -d '\r\n' < "$exe_file")
    fi
    if [ -z "$py_exe" ] || [ ! -f "$py_exe" ]; then
        ps_exe="powershell.exe"
        if ! command -v "$ps_exe" >/dev/null 2>&1 && [ -n "${SYSTEMROOT:-}" ] && [ -x "$SYSTEMROOT/System32/WindowsPowerShell/v1.0/powershell.exe" ]; then
            ps_exe="$SYSTEMROOT/System32/WindowsPowerShell/v1.0/powershell.exe"
        fi
        "$ps_exe" -NoProfile -ExecutionPolicy Bypass -File "$root/build/bootstrap-tools.ps1" >&2
        py_exe=$(tr -d '\r\n' < "$exe_file")
    fi
    if [ -f "$py_exe" ]; then
        exec "$py_exe" "$@"
    fi
    echo "run-python.sh: pinned CPython missing at $py_exe" >&2
    exit 1
fi
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "" >/dev/null 2>&1; then
        exec "$candidate" "$@"
    fi
done
echo "run-python.sh: no Python interpreter available" >&2
exit 127
