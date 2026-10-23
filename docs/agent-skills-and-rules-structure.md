# Agent Skills and Rules Structure

Opus 5.5 through Claude Code is the only writer in this repository, so there is one agent kit: `.claude/` (owner decision 2026-10-23). The Antigravity (`.agents/`) and Codex (`.codex/`) writer kits were removed that day. Codex stays only as the reviewer CLI that `scripts/review-dispatch.py` runs read-only; it reads `AGENTS.md` and needs no kit of its own.

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

- Durable project behavior belongs in `AGENTS.md`, which the writer loads through the `CLAUDE.md` stub and the reviewer reads directly.
- `.claude/rules/` holds Claude Code wiring and output rules.
- Markdown checklists or rule references belong in a skill `references/` folder or in `docs/`.
- Subagent definitions live in `.claude/agents/`. A subagent that writes code, tests, plan or docs runs on Opus 5.5 (`claude-opus-5-5`); Sonnet or Haiku run only read-only helpers whose output the main session verifies. `scripts/verify-agent-tiers.mjs` enforces it.

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
