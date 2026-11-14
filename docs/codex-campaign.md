# Codex campaign

Run `scripts/start-unattended-run.ps1 -Runner codex`, or open "Recto Codex campaign" on the Windows Desktop. This starts Codex as writer using its configured model at medium reasoning effort and its existing account. Claude credentials are never required or passed to it.

Codex follows `.codex/skills/process-plan/SKILL.md`, using the existing plan, campaign guard and review dispatcher. Its Stop hook is `.codex/hooks.json`; approve this project's hook in Codex after changing it. Its reviewer route is `codex -> carbon`, preserving the project's review schedule. Only one writer may own the tree. A stopped or crashed campaign is not restarted by a background service.

The owner authorised Codex as an additional campaign writer on 2026-11-14. Existing Claude and Carbon launchers remain available. The Codex-specific session and timer rules in its process-plan skill take precedence over the Claude-specific examples in the shared workflow documents.

## Independent reviewer

Codex routes to Orchestrator (Carbon), `gemini-pro-orchestrator`, through the Antigravity CLI (`agy`) in sandboxed plan mode. Claude and Antigravity writers still route to GPT-6 Astra through Codex. The three-pass policy, lenses, blocking severities and 33-pass cap are unchanged.

The shared schedule sets no effort override because Orchestrator manages its own effort. This also removes the previous medium override for Astra: scheduled reviews now use the Codex CLI configuration, not the reviewer entry's `high` value (that value applies only without a schedule). Reversing the model choice requires changing the Carbon entry and restoring only compatible scheduled efforts.

Validate with `build\python.cmd scripts/review-dispatch.py --check-pin --json`. A live review must return all required lens lines, a consistent severity and a valid final verdict. Timeout, empty output and malformed replies are failed attempts, never approval.
