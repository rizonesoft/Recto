---
name: authoring-skills
description: Use when creating, moving, reviewing, or documenting project skills, skill references, or always-on rules in .agents/skills and .agents/rules.
---

# Authoring Skills

Follow `docs/agent-skills-and-rules-structure.md` before creating or moving skills or rules. Both Claude Code (`.claude/`) and Google Antigravity (`.agents/`) are writers in this repository, and the reviewer (GPT-6.2 Astra through the Codex CLI) reads `AGENTS.md` directly.

## Required Pattern

1. Put Antigravity skill content under `.agents/skills/<skill-name>/` (and mirror to `.claude/skills/<skill-name>/` with Claude-specific commands when updating shared skills).
2. Put required instructions in `SKILL.md`.
3. Name skills for the capability, not the project namespace. Prefer gerunds or clear noun phrases such as `authoring-skills` or `local-tool-safety`; keep project-specific scope in the description and instructions.
4. Put long checklists, conventions, and detailed docs in `references/`.
5. Put helper scripts in `scripts/` and templates/resources in `assets/`.
6. Keep `.agents/skills/<skill-name>/` as real files and directories, never symlinks.
7. Put Antigravity always-on rules in `.agents/rules/` (and mirror shared rules to `.claude/rules/` where `.claude/rules/` exists).

## Validation

- Confirm `SKILL.md` exists in `.agents/skills/<skill-name>/`.
- Confirm the skill `name` frontmatter matches its directory.
- Confirm no symlinks exist under `.agents/skills/`.
- Update `AGENTS.md` or `docs` when adding behavior that future agents must follow.
