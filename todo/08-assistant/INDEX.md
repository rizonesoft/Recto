# 08 Assistant

> **Phase 7 (After v1) unless the owner moves it earlier**

An opt-in AI assistant that works on the open document with the user's own provider key (Anthropic, OpenAI, OpenRouter or Google Gemini). It is off by default: Recto stays fully offline until the user adds a key, and every request shows what will leave the machine before it is sent (foundation plan D19).

## TODOs

| TODO | Title | Status |
| ---- | ----- | :----: |
| [TODO-01](./TODO-01-ai-assistant.md) | Opt-in AI Assistant with User-Supplied Provider Keys | active |

## Completed

| TODO | Title | Completed |
| ---- | ----- | :-------: |

(none yet)

## In scope

- Provider settings and keys (Anthropic, OpenAI, OpenRouter, Gemini) held in Windows Credential Manager, configurable models, consent and data-preview before every request
- Assistant features over the open document: summarize, ask with page citations, explain or translate a selection, draft alternate text for figures

## Out of scope

- Any network use while no key is configured, telemetry, or sending redacted, encrypted-without-consent or unselected content

---

Format spec: [../README.md](../README.md) · Root index: [../TODO-00-INDEX.md](../TODO-00-INDEX.md)
