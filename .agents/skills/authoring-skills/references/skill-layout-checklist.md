# Skill Layout Checklist

Use this checklist whenever adding or changing a skill. Claude Code (`.claude/`) and Antigravity (`.agents/`) are the repository's two writers, each loading its own native skill and rule folders.

## Skill (`.agents/skills/` and `.claude/skills/`)

- [ ] Skill lives in `.agents/skills/<skill-name>/` and `.claude/skills/<skill-name>/` as real directories, not symlinks.
- [ ] `SKILL.md` exists with `name` and `description` frontmatter, `name` matching `<skill-name>`.
- [ ] Long docs live in `references/`, executable helpers in `scripts/`, assets in `assets/`.
- [ ] No provider metadata for another agent (such as `agents/openai.yaml`) exists under the skill.

## Rules

- [ ] Repo-wide durable instructions are in `AGENTS.md`.
- [ ] Claude Code wiring and output rules are in `CLAUDE.md` and `.claude/rules/`.
- [ ] Antigravity wiring and output rules are in `GEMINI.md`, `.agents/rules/`, and `.agents/hooks.json`.
- [ ] Markdown reference checklists live in `docs/` or skill `references/`.
- [ ] A subagent in `.claude/agents/` that writes runs on `claude-fable-5-2`; every other subagent is read-only (`node scripts/verify-agent-tiers.mjs`).
