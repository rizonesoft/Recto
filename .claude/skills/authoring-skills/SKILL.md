---
name: authoring-skills
description: Use when creating, moving, reviewing, or documenting project skills, skill references, skill folders (.claude/skills, .agents/skills), agent rules (.claude/rules, .agents/rules) or subagent definitions (.claude/agents).
---

# Authoring Skills

Follow `docs/agent-skills-and-rules-structure.md` before creating or moving skills or rules. Claude Code and Antigravity are the repository's two writers, so each project skill lives as real files in both `.claude/skills/<skill-name>/` and `.agents/skills/<skill-name>/`, and the independent reviewer reads `AGENTS.md` and needs no kit.

## Required Pattern

1. Put skill content under `.claude/skills/<skill-name>/` and `.agents/skills/<skill-name>/`.
2. Put required instructions in `SKILL.md`.
3. Name skills for the capability, not the project namespace. Prefer gerunds or clear noun phrases such as `authoring-skills` or `local-tool-safety`; keep project-specific scope in the description and instructions.
4. Put long checklists, conventions, and detailed docs in `references/`.
5. Put helper scripts in `scripts/` and templates/resources in `assets/`.
6. Keep `.claude/skills/<skill-name>/` and `.agents/skills/<skill-name>/` as real files and directories, never symlinks.
7. Use `.claude/rules/` for Claude Code rules, `.agents/rules/` for Antigravity always-on rules, and `.claude/agents/` for Claude subagents. A subagent that writes code, tests, plan or docs pins `model: claude-fable-5-2`; Sonnet and Haiku subagents are read-only.

## Validation

- Confirm `SKILL.md` exists in `.claude/skills/<skill-name>/` and `.agents/skills/<skill-name>/`.
- Confirm the skill `name` frontmatter matches its directory.
- Confirm no symlinks exist under `.claude/skills/` or `.agents/skills/`.
- Run `node scripts/verify-agent-tiers.mjs` after changing a subagent definition.
- Update `AGENTS.md` or `docs` when adding behavior that future agents must follow.
