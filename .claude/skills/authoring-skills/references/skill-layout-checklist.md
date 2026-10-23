# Skill Layout Checklist

Use this checklist whenever adding or changing a skill. Claude Code is the only agent kit in this repository (`.claude/`, owner decision 2026-10-23).

## Claude Skill (.claude/skills/)

- [ ] Skill lives in `.claude/skills/<skill-name>/` as a real directory, not a symlink.
- [ ] `SKILL.md` exists with `name` and `description` frontmatter, `name` matching `<skill-name>`.
- [ ] Long docs live in `references/`, executable helpers in `scripts/`, assets in `assets/`.
- [ ] No provider metadata for another agent (such as `agents/openai.yaml`) exists under the skill.

## Rules

- [ ] Repo-wide durable instructions are in `AGENTS.md`.
- [ ] Claude Code wiring and output rules are in `.claude/rules/`.
- [ ] Markdown reference checklists live in `docs/` or skill `references/`.
- [ ] A subagent in `.claude/agents/` that writes runs on `claude-opus-5-5`; every other subagent is read-only (`node scripts/verify-agent-tiers.mjs`).
