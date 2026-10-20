#!/usr/bin/env bash
# Registers this project's automations as systemd user timers (Linux and WSL); install.ps1 is the Windows
# Task Scheduler equivalent. run.py renders a .service and .timer per task in tasks.json (refusing a malformed
# file), each marked with this repository's tasks.json path; this script copies them into the user unit
# directory, enables every timer, and removes the units carrying this repository's marker that tasks.json no
# longer lists. Units without the marker (another project's, or hand-written ones) are never touched.
# Idempotent: re-run it after editing tasks.json.
#
#   bash .conclave/automations/install.sh
#   bash .conclave/automations/install.sh --uninstall
set -euo pipefail

usage() {
  printf 'Usage: %s [--uninstall]\n' "$0" >&2
}

uninstall=0
if [ "$#" -gt 1 ]; then
  usage
  exit 2
elif [ "$#" -eq 1 ]; then
  [ "$1" = "--uninstall" ] || { usage; exit 2; }
  uninstall=1
fi

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

marker=$(python3 "$here/run.py" --units "$work")
mkdir -p -- "$unit_dir"

# Ownership first, before anything changes: a unit of the same name that this repository did not write (a
# hand-written one, or another checkout's) is never replaced.
if [ "$uninstall" -eq 0 ]; then
  for unit in "$work"/*.service "$work"/*.timer; do
    [ -f "$unit" ] || continue
    destination="$unit_dir/$(basename -- "$unit")"
    if [ -e "$destination" ] || [ -L "$destination" ]; then
      if [ -L "$destination" ] || ! head -n 1 -- "$destination" | grep -qxF -- "$marker"; then
        printf 'Refusing to replace %s: it is not marked as this repository'"'"'s unit\n' "$destination" >&2
        exit 1
      fi
    fi
  done
fi

removed=0
for unit in "$unit_dir"/*.service "$unit_dir"/*.timer; do
  [ -f "$unit" ] && [ ! -L "$unit" ] || continue
  head -n 1 -- "$unit" | grep -qxF -- "$marker" || continue
  name=$(basename -- "$unit")
  if [ "$uninstall" -eq 1 ] || [ ! -f "$work/$name" ]; then
    case "$name" in
      *.timer) systemctl --user disable --now -- "$name" ;;
      *.service) systemctl --user stop -- "$name" ;;
    esac
    rm -f -- "$unit"
    printf 'removed %s\n' "$name"
    removed=$((removed + 1))
  fi
done

if [ "$uninstall" -eq 0 ]; then
  for unit in "$work"/*.service "$work"/*.timer; do
    [ -f "$unit" ] || continue
    # Temporary file beside the destination, then a rename, so systemd never reads a half-written unit.
    temporary=$(mktemp -- "$unit_dir/.automations.XXXXXX")
    install -m 0644 -- "$unit" "$temporary"
    mv -f -- "$temporary" "$unit_dir/$(basename -- "$unit")"
  done
fi

systemctl --user daemon-reload
if [ "$uninstall" -eq 1 ]; then
  printf 'Removed %d unit(s)\n' "$removed"
  exit 0
fi
timers=()
for unit in "$work"/*.timer; do
  [ -f "$unit" ] && timers+=("$(basename -- "$unit")")
done
if [ "${#timers[@]}" -gt 0 ]; then
  systemctl --user enable --now -- "${timers[@]}"
  systemctl --user list-timers --all -- "${timers[@]}"
fi
