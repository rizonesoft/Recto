---
name: adopt-conclave
description: Use when importing Conclave-SDK kits into a project, updating a project's kits, or checking what a project's kits need, for example "adopt-conclave, add the reviewer kit to R:\GitHub\Site", "update the kits in this repo", or when the Conclave VS Code extension's Copy import request or Copy update request was pasted. Runs forge.py with dry runs first and the operator in the loop; never forces an overwrite.
---

# Adopt Conclave

Imports, updates and checks Conclave-SDK kits in a project through the SDK's own `forge.py`. The VS Code extension only reads kit state; this skill is how kits get into a project (owner decision, 2026-10-20). forge does the writing; you choose what to run, preview it, and check what changed.

## Find the SDK

1. The repository you are in, when it holds `forge.py` and `kits/` (you are in Conclave-SDK itself).
2. Else the `CONCLAVE_SDK` environment variable.
3. Else ask the operator once for the path to their Conclave-SDK checkout, and suggest setting `CONCLAVE_SDK`.

Run forge as `python <sdk>/forge.py ...` (`python3` on Linux and WSL). Every command prints JSON; read it, do not guess.

## Workflow

1. **Look first.** `python <sdk>/forge.py kits`, then `forge.py check <target>` and `forge.py doctor <target>`. Note the kits the target has, its kit stamp (`kit_stamp` in `check`) and every check that is not `ok`.
2. **Preview each kit.** For every kit to add or update: `forge.py kit <target> <kit> --dry-run --agent all` (add `--host pressable|wpcom` for WordPress). Show the operator what it would install, keep and update. A preview with `merge_needed` paths stops here: those files exist and differ, so resolve each with the operator (merge by hand, or leave the project's version) before writing.
3. **Write.** A new project: `forge.py init <target> --kit <kit> ...` (base always comes with it). An existing one: `forge.py kit <target> <kit>` per kit, or `forge.py sync <target>` to bring every kit it has up to date (`--include-scripts` also refreshes existing `scripts/` and `bin/` files). Never pass `--force` unless the operator asks for that one run: it overwrites the entry docs.
4. **A kit set up by a newer SDK.** `kit` refuses it (exit 2) and `sync` lists it under `refused_newer` with the SDK commit and date it came from. Do not work around it: run that SDK, or a later one, for those kits.
5. **Check the protected files with git.** `git status` and `git diff --stat` in the target: `AGENTS.md`, `GEMINI.md`, `CLAUDE.md`, `CODEX.md`, `.conclave/`, `.claude/settings.json`, `todo/` and `docs/` change only where forge reported writing them. Anything else changed is a stop: show the diff and restore it with the operator.
6. **Check health again.** `forge.py doctor <target>`; each `setup_required` check names the step that finishes it (for example the `wordpress-connect` skill).
7. **Report.** What forge installed, updated and kept per kit, any refusal and why, the protected-file check, the doctor checks still open, and the kit stamp now in `.conclave/kits.json`.

## Stop and ask when

- A preview has `merge_needed` paths, or a protected file changed that forge did not report.
- The target is not trusted (not the operator's repository), or is inside a `.git` folder, or is the SDK folder itself (forge refuses the last two anyway).
- forge refuses a kit as set up by a newer SDK and the operator has not said which SDK to use.

## Never

- Never copy kit files by hand or edit them in the project to look installed: forge owns the files and the stamp.
- Never run `--force`, delete project files, or commit for the operator without being asked.
