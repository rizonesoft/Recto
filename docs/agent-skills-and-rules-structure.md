# Agent Skills and Rules Structure

Claude Code (`claude`) and Google Antigravity on Orchestrator (Carbon) (`agy`, `gemini-pro-orchestrator`) are the writers in this repository (owner directions 2026-11-10, 2026-11-12, 2026-11-13 and 2026-11-14). There is a single canonical skill tree under `.claude/skills/`: `.agents/skills.json` points Antigravity directly at `.claude/skills`, and `.agents/hooks.json` wires Antigravity's `SessionStart`, `PreToolUse` and `Stop` hooks to the same `scripts/` modules as `.claude/settings.json`, so zero skill files are ever duplicated under `.agents/skills/`. The `.codex/` writer kit removed on 2026-10-23 stays removed: Codex runs only as the read-only reviewer CLI invoked by `scripts/review-dispatch.py`, reading `AGENTS.md` directly.

## Skill location

Skills live under:

```text
.claude/skills/<skill-name>/
  SKILL.md
  references/
  scripts/
  assets/
```

- `SKILL.md` is required.
- `references/` is standard Agent Skills structure for longer documentation.
- `scripts/` is for executable helpers.
- `assets/` is for templates or resources.
- Keep every skill folder a real directory with real files, never a symlink.

## Naming and placement

- Name a skill for its capability, not with a project namespace prefix. Project-specific routing context belongs in the description and instructions, where it is read on demand, rather than in the name, which is paid for in the always-on catalog by every session.

## Rules

Use the word "rules" carefully:

- Durable project behavior belongs in `AGENTS.md`, which Antigravity loads directly, Claude Code loads through the `CLAUDE.md` stub, and the reviewer reads directly.
- `.claude/rules/` holds Claude Code output and safety rules; `.claude/settings.json` and `.agents/hooks.json` wire the shared lifecycle hooks.
- Markdown checklists or rule references belong in a skill `references/` folder or in `docs/`.
- Claude Code subagent definitions live in `.claude/agents/`. A subagent that writes code, tests, plan or docs runs on Fable 5.2 (`claude-fable-5-2`); Sonnet or Haiku run only read-only helpers whose output the main session verifies (`scripts/verify-agent-tiers.mjs` enforces it). Antigravity uses Orchestrator (Carbon) automatically and defines zero custom subagents under `.agents/`.

## Adding a New Skill

1. Create `.claude/skills/<skill-name>/SKILL.md` as a real file (never a symlink).
2. Keep the `name` field equal to `<skill-name>`, using lowercase letters, numbers, and hyphens only.
3. Name the skill for the capability, not the project namespace (`authoring-skills`, `local-tool-safety`, `process-todo-section`).
4. Keep `description` specific enough for automatic selection.
5. Put long checklists in `references/`.
6. Update `AGENTS.md` if the skill changes project behavior.
7. Validate the skill file and its references.

## Current Shared Skills

| Skill | Path |
| ----- | ---- |
| `local-tool-safety` | `.claude/skills/local-tool-safety/` |
| `authoring-skills` | `.claude/skills/authoring-skills/` |
