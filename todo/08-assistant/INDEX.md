# 08 Assistant

> **Phase 7 (After v1); built by the unattended campaign (owner decision 2026-10-22)**

An opt-in AI assistant that works on the open document with the user's own provider key (Anthropic, OpenAI, OpenRouter or Google Gemini) or a local model on a loopback endpoint. It is off by default: Recto stays fully offline until the user adds a key, and every request shows what will leave the machine before it is sent (foundation plan D19).

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-ai-assistant.md) | Opt-in AI Assistant with User-Supplied Provider Keys | active |
| [TODO-02](./TODO-02-assistant-workspace.md) | Assistant Workspace: Translate, Rewrite, Chat, Search, Commands and Tools | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Provider settings and keys (Anthropic, OpenAI, OpenRouter, Gemini) and the keyless local provider (loopback only) held in Windows Credential Manager, configurable models, consent and data-preview before every request
- Assistant features over the open document: summarize, ask with page citations, explain or translate a selection, draft alternate text for figures
- Workspace tools: whole-document translation with bilingual output, rewrite and proofread applied through the undoable Edit PDF path, multi-document chat, semantic search, natural-language commands, AI bookmarks and page proposals, mind maps, flashcards and quizzes, contract risk review, an AI-content estimate, image generation, audio summaries and AI redaction suggestions

## Out of scope

- Any network use while no key is configured, telemetry, or sending redacted, encrypted-without-consent or unselected content

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
