---
name: process-plan
description: Explain Recto writer ownership when process-plan is requested in Codex.
---

# Process plan ownership

Codex is a read-only reviewer in Recto. Do not implement the plan, acquire a writer campaign guard or launch a Codex writer session. The owner restored Claude Code and Antigravity as the only writers on 2026-11-14, superseding the earlier Codex writer adapter. Direct plan execution to `scripts/start-unattended-run.ps1 -Runner claude` or `-Runner agy`; only the owner starts a campaign. Read `AGENTS.md` and `docs/codex-campaign.md` for the current roles. Review through the repository dispatcher when invoked by either writer.
