# GEMINI.md

This file configures Google Antigravity for `Recto`. Shared working rules live in `AGENTS.md`.

- Antigravity workspace skills live in `.agents/skills/` and always-on rules live in `.agents/rules/`.
- Claude Code skills in `.claude/skills/` belong to Claude Code sessions and are ignored by Antigravity.
- On any section that touches a user interface file, read `.agents/rules/design-contract.md` (or `.claude/rules/design-contract.md`, else `AGENTS.md`) for the design contract, token source, static lint and rendered check.
