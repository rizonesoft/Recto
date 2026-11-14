# Codex reviewer only

On 2026-11-14 the owner retired the Codex writer campaign and Antigravity independent reviewer route. Claude Code and Antigravity are the only writers, one session at a time under the existing campaign guard. Codex reviews through its read-only sandbox and never writes.

Start the writer with `scripts/start-unattended-run.ps1 -Runner claude` or `-Runner agy`. Run `scripts/start-unattended-run.ps1 -InstallShortcut` to refresh the Claude and Carbon Desktop shortcuts and remove the retired Codex campaign shortcut. No campaign starts during shortcut installation.

Both writer routes select GPT-6 Astra (`gpt-6-astra`) through Codex CLI. Plan, initial round and completion use medium effort; follow-up rounds and reconciliation use low effort. Review lenses, blocking severities and the 33-pass cap are preserved. A timeout, empty response or malformed verdict is never approval. Antigravity remains the Orchestrator (Carbon) writer (`gemini-pro-orchestrator`), not an independent reviewer.

The Codex writer skill and campaign Stop hook are retired. Historical review records describe their original candidate and do not authorise a current writer route.
