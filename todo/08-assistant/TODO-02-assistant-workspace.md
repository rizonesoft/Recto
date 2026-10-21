---
schema_version: 1
id: assistant-workspace
domain: 08-assistant
status: active
title: "TODO-02 -- Assistant Workspace: Translate, Rewrite, Chat, Search, Commands and Tools"
---

# TODO-02 -- Assistant Workspace: Translate, Rewrite, Chat, Search, Commands and Tools

> **Goal:** The opt-in assistant of `D08 T01` grows into the AI toolset that UPDF and PDFelement ship, without giving up any Recto rule. A user can translate a whole document keeping its layout (with a bilingual side-by-side or interleaved output), rewrite, proofread, retone, expand or condense text and apply it through the undoable Edit PDF path, chat across several open files with per-file citations, search by meaning, ask `Ctrl+K` to find commands, get AI bookmarks and page-fix proposals, make mind maps, flashcards and quizzes, review contract risk, estimate whether text was machine-written, generate images, hear audio summaries and let AI suggest redactions, using Anthropic, OpenAI, OpenRouter, Gemini or a local model on a loopback endpoint. Every request, including each batch of a long job, is previewed and authorized by the user, nothing changes the document without an explicit undoable Apply, and a feature a provider cannot serve is disabled with the reason (foundation plan D19, widened 2026-10-21).

> **Current state:** Measured 2026-10-21. `D08 T01` plans the provider interface, transport, key store, consent gate, `ContextBuilder`, `AssistantService`, the Assistant panel and the privacy suite for four cloud providers, and none of it is built. `D03 T04` plans the Edit PDF session, paragraph model, rewrite and full-rewrite save. `D03 T05` plans the local text index. `D05` plans page operations and the redaction review. This file adds no new principles: it extends those with a fifth (local) provider, job-level authorization, structured output, and the features above. Every section is Phase 7 (After v1).

## Inputs

- Foundation plan D19 (widened scope and the per-request confirmation rule), D16 (offline by default, no telemetry), D9 (no PDF JavaScript), D21 (Group Policy name `AssistantDisabled`), and `AGENTS.md` (document text is hostile input, the UI thread never blocks, every write is temp-file-then-replace, colours from `RectoColors.xaml`, every command reachable from `Ctrl+K`).
- `D08 T01`: `IAssistantProvider` (`§1`), the transport (`§2`), `RetryingSender` (`§3`), `CredentialStore` (`§9`), settings and catalog (`§10`), `ContextBuilder` and `OutboundPreview` (`§11`, `§12`), `AssistantService` and the citation parser (`§13`), the Assistant panel (`§17`) and the privacy suite (`§19`). This file builds on them and states each change it needs in them.
- `D03 T04` (`EditSession`, paragraph model, `RewriteParagraph`, `AddImage`, full-rewrite save, `edit.*` commands), `D03 T05` (`SearchIndex`, `FileScope`, index jobs, grouped results), `D03 T03 §4` (annotation undo stack), `D02 T03 §5`, `§6`, `§11`, `§12` (outline panel, bookmark editor undo stack, `CommandRegistry`, command box), `D05 T01 §1`, `§2`, `§3`, `§4` (`GraftMap`, merge, page edit list, blank page), `D05 T02 §1`, `§9` (Organize model, redaction review), `D04 T01 §1`, `§5` (isolated jobs, `QueueCard`), `D03 T02 §5` (`AnnouncementService`).
- Cross-references to the files above are added by the coordinator in both directions.
- -> XREF: D08 T01 §1 -- local provider id.
- -> XREF: D08 T01 §2 -- loopback exception in TransportPolicy.
- -> XREF: D08 T01 §12 -- NoEndpoint state.
- -> XREF: D08 T01 §12 -- batch grants.
- -> XREF: D03 T04 §3 -- RewriteParagraph.
- -> XREF: D03 T04 §14 -- EditSession apply.
- -> XREF: D03 T04 §5 -- rewrite review.
- -> XREF: D03 T04 §2 -- paragraph model.
- -> XREF: D03 T04 §4 -- font fallback.
- -> XREF: D03 T04 §9 -- full-rewrite save.
- -> XREF: D05 T01 §1 -- GraftMap.
- -> XREF: D05 T01 §2 -- merge.
- -> XREF: D04 T01 §1 -- conversion job.
- -> XREF: D04 T01 §5 -- queue card.
- -> XREF: D03 T05 §4 -- index beside text index.
- -> XREF: D03 T05 §5 -- freshness rule.
- -> XREF: D03 T05 §8 -- open at location.
- -> XREF: D03 T05 §7 -- Meaning mode in Search panel.
- -> XREF: D02 T03 §11 -- command registry.
- -> XREF: D02 T03 §12 -- ask AI row in command box.
- -> XREF: D02 T03 §5 -- bookmark undo stack.
- -> XREF: D02 T03 §6 -- bookmark proposals.
- -> XREF: D05 T01 §3 -- page operations.
- -> XREF: D05 T02 §1 -- organize grid.
- -> XREF: D03 T03 §4 -- comments.
- -> XREF: D03 T04 §8 -- AddImage.
- -> XREF: D05 T02 §9 -- Suggested redaction mark state.
- -> XREF: D07 T02 §10 -- AssistantDisabled policy.
- -> XREF: D05 T02 §28 -- suggested marks.

## Outcome

- With no key and no local endpoint configured, none of these features makes a network call, and a test proves it for every entry point.
- A local model on `127.0.0.1`, `localhost` or `[::1]` works with no key, never through a proxy, and nothing else on the machine's network is reachable through that exception.
- A document translates into another language on a copy with the original untouched, in the original layout where it fits, and optionally as a bilingual document.
- Selected text is rewritten, proofread, retoned, expanded or condensed, and each result is a suggestion until an Apply that is one undo step.
- Chat, search by meaning, command planning, bookmark and page proposals, creation tools, contract review, the AI-content estimate, image generation, audio summaries and PII suggestions each show their exact outbound data first, report what they could not do, and never auto-apply.

**Adjacency:** list=applicable @ D08 T02 §12; document=applicable @ D08 T02 §8; settings=applicable @ D08 T02 §2; reporting=applicable @ D08 T02 §10; notifications=applicable @ D08 T02 §34; permissions=applicable @ D08 T02 §3; audit=applicable @ D08 T02 §3; exchange=applicable @ D08 T02 §24; reverse=applicable @ D08 T02 §6

**Adjacency rationale:** The multi-file chat list is the file picker and grouped citations (§12). The translated copy is the document the feature produces (§8). Settings are the local endpoint, capability overrides and embedding models (§1, §2, §13). The translation report lists what was left in the original language and why (§10). Announcements are the notifications (§34). The permission and audit boundary is job-level authorization with a per-job ledger (§3). Exports of mind maps, cards, quizzes and reports are the exchange (§24, §25). The undoable Apply paths are the reverse (§6, §21, §23, §27, §29, §32).

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Provider capability matrix and disabled-with-reason rule | D08 T01 §10 | [ ] |
| 2 | §2 | Local provider: loopback OpenAI-compatible endpoint, no key, no proxy | §1, D08 T01 §2, D08 T01 §6, D08 T01 §12 | [ ] |
| 3 | §3 | Batch authorization: one job preview, many single-use request grants | D08 T01 §12 | [ ] |
| 4 | §4 | Structured output runner: bounded JSON, schema checks and verified quotes | D08 T01 §13 | [ ] |
| 5 | §5 | Rewrite, proofread, tone, expand and condense suggestion engine | §4, D03 T04 §2 | [ ] |
| 6 | §6 | Apply AI text suggestions through the undoable Edit PDF path | §5, D03 T04 §3, D03 T04 §5, D03 T04 §14 | [ ] |
| 7 | §7 | Rewrite and proofread review in the Assistant panel | §6, D08 T01 §17 | [ ] |
| 8 | §8 | Whole-document translation on a copy keeping layout | §3, §4, D03 T04 §2, D03 T04 §5, D03 T04 §9 | [ ] |
| 9 | §9 | Bilingual output: side-by-side wide pages and interleaved pages | §8, D05 T01 §1, D05 T01 §2 | [ ] |
| 10 | §10 | Translation dialog: language, glossary, job preview, progress and report | §8, §9, D04 T01 §1, D04 T01 §5 | [ ] |
| 11 | §11 | Multi-document context builder with per-file citations | D08 T01 §11, D08 T01 §13 | [ ] |
| 12 | §12 | Multi-document chat across chosen open files | §11, D08 T01 §17 | [ ] |
| 13 | §13 | Embeddings requests for providers and local models | §1, §3, D08 T01 §3 | [ ] |
| 14 | §14 | Semantic index store beside the text index | §13, D03 T05 §4 | [ ] |
| 15 | §15 | Semantic index build job with one preview and never-upload-twice reuse | §14, §3, D03 T05 §5 | [ ] |
| 16 | §16 | Semantic query: previewed query embedding, ranking and verified hits | §15 | [ ] |
| 17 | §17 | Search by meaning mode, results and index controls | §16, D03 T05 §7, D03 T05 §8 | [ ] |
| 18 | §18 | Natural-language command planner over the command registry | §4, D02 T03 §11 | [ ] |
| 19 | §19 | Command box: ask for a command, review the plan, run what you choose | §18, D02 T03 §12 | [ ] |
| 20 | §20 | AI bookmark proposals and per-bookmark summaries | §4, D02 T03 §6 | [ ] |
| 21 | §21 | Bookmark proposals review in the Outline panel | §20, D02 T03 §5 | [ ] |
| 22 | §22 | AI page proposals: blank, misoriented and duplicate pages | §4, D05 T01 §3 | [ ] |
| 23 | §23 | Page proposals review in the Organize grid | §22, D05 T02 §1 | [ ] |
| 24 | §24 | Mind map: outline, layout, Markdown, OPML and image exports | §4 | [ ] |
| 25 | §25 | Flashcards and quizzes with Markdown and CSV exports | §4 | [ ] |
| 26 | §26 | Contract risk review with verified quotes and the not-legal-advice notice | §4, D08 T01 §11 | [ ] |
| 27 | §27 | Contract review list, page jumps and optional Apply as comments | §26, D03 T03 §4 | [ ] |
| 28 | §28 | AI-content estimate with fixed limits text | §4 | [ ] |
| 29 | §29 | Image generation inserted through the undoable Edit PDF path | §1, §3, D03 T04 §8, D03 T04 §14 | [ ] |
| 30 | §30 | Audio summaries: Windows voices or provider speech, WAV and MP3 | §1, §4 | [ ] |
| 31 | §31 | AI-assisted PII suggestions as unapplied redaction marks | §4, D05 T02 §9 | [ ] |
| 32 | §32 | Redaction Bar: review, accept and reject AI suggestions | §31 | [ ] |
| 33 | §33 | Study and creation tools in the Assistant panel | §24, §25, §28, §29, §30, D08 T01 §17 | [ ] |
| 34 | §34 | Ctrl+K commands, UIA names and announcements for the workspace tools | §7, §10, §12, §17, §19, §21, §23, §27, §32, §33, D02 T03 §11, D03 T02 §5 | [ ] |
| 35 | §35 | Workspace privacy suite: loopback only, previews, no auto-apply, zero network | §2, §34 | [ ] |

## 1. Provider capability matrix and disabled-with-reason rule

Providers differ: not every one offers embeddings, image generation, speech or image input, and a free-text model ID may not support them. This section records what each provider and model can do, in one table, and gives every feature a requirement list so an unsupported feature is shown disabled with a plain reason instead of failing at send time. It matches no single competitor feature; UPDF and PDFelement bundle one vendor's models, while Recto lets the user pick any, so it must tell them what their pick cannot do. It must not guess: an unverified capability is treated as off until the user says otherwise.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/capabilities.h/.cpp`: `Capability` (`Chat`, `Vision`, `StructuredJson`, `Embeddings`, `ImageGeneration`, `SpeechSynthesis`), a table keyed by `ProviderId` and model-ID pattern with each entry `Verified` (with the date) or `Unknown`, and `FeatureRequirements` mapping every workspace feature to the capabilities it needs. Done when: `AssistantWs.CapabilityTableCoversEveryFeature` asserts every feature has a requirement list and every provider has an entry for every capability.
2. Add `explain_disabled(feature, provider, model)` returning a sentence naming the provider, the missing capability and what to do (choose another provider, or tick the override). Done when: `AssistantWs.DisabledReasonNamesCapabilityAndFix` asserts the sentence for an Anthropic embeddings request mentions embeddings and a provider switch and is not empty for any disabled pair.
3. Add the per-model override setting `assistant.capabilityOverride.<provider>.<capability>` (default off) that the user can tick in Settings (§2) when they know their model supports a capability; an override never bypasses consent. Done when: `AssistantWs.OverrideEnablesCapabilityOnly` asserts an override flips availability and changes nothing in the gate.
4. Record the verification date of every `Verified` entry and the source page in `docs/assistant.md`. Done when: `AssistantWs.CapabilityDocsMatchTable` asserts each `Verified` entry has a dated line in `docs/assistant.md`.

- [ ] Implement the capability table with every entry `Unknown` until it is verified against the provider's published documentation on the day the section is built. Done when: `AssistantWs.UnknownIsOff` asserts an `Unknown` capability is unavailable.
- [ ] Implement `FeatureRequirements` for the features in this file (translation needs `Chat`; page proposals need `Chat` and `Vision`; image generation needs `ImageGeneration`; audio provider speech needs `SpeechSynthesis`; semantic search needs `Embeddings`; structured tasks need `StructuredJson` or a JSON-instruction fallback). Done when: `AssistantWs.FeatureRequirementsComplete` passes.
- [ ] Implement `explain_disabled` and the override setting in the `SettingsStore` schema. Done when: `AssistantWs.DisabledReasonNamesCapabilityAndFix` and `AssistantWs.OverrideEnablesCapabilityOnly` pass.
- [ ] Commit: `assistant: add provider capability matrix and disabled-with-reason rule`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Capability*:AssistantWs.DisabledReason*:AssistantWs.Override*:AssistantWs.UnknownIsOff:AssistantWs.FeatureRequirements*` passes, and marking an `Unknown` entry available without a dated docs line fails `AssistantWs.CapabilityDocsMatchTable`.

## 2. Local provider: loopback OpenAI-compatible endpoint, no key, no proxy

Users who will not send documents to a cloud can point Recto at a local model server such as Ollama, LM Studio or Foundry Local that speaks the OpenAI-compatible API on the same machine. This matches UPDF's and PDFelement's cloud-only AI by offering the private alternative; Acrobat has no local mode. It is a fifth provider that needs no key and sends nothing off the machine, and it is the single exception to the HTTPS-only rule of `D08 T01 §2`. It must stay a narrow exception: only literal loopback hosts, never a proxy, never a redirect, and the preview, consent and capability rules of every other provider still apply.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `Local` to `ProviderId` (`D08 T01 §1`) and author `src/services/assistant/local_provider.h/.cpp` as an `OpenAiCompatibleProvider` descriptor (`D08 T01 §6`) with base URL from the new setting `assistant.baseUrl.local` (default `http://127.0.0.1:11434/v1`) and an empty `AuthSpec` (no header). Done when: `AssistantWs.LocalBuildsRequestWithoutAuth` asserts the built request has no `Authorization` or key header.
2. Author `src/services/assistant/loopback_policy.h/.cpp`: `check(url)` accepts only `http` or `https` with host exactly `localhost`, `127.0.0.1` or `[::1]`, no userinfo, a port in 1 to 65535, no query or fragment, and for `localhost` resolves the name with `GetAddrInfoW` and refuses unless every address is loopback. Done when: `AssistantWs.LoopbackPolicyRules` accepts the three hosts and rejects `0.0.0.0`, `127.0.0.2`, `192.168.1.5`, `localhost.evil.example`, `127.0.0.1.evil.example`, `http://user:pw@127.0.0.1`, `http://[::ffff:127.0.0.1]` and `file://`.
3. Make the transport apply the policy: for `ProviderId::Local` only, `http://` is allowed after `check` passes, WinHTTP uses `WINHTTP_ACCESS_TYPE_NO_PROXY` so no proxy ever sees the content, and every redirect still returns `UnexpectedRedirect`; every other provider keeps the HTTPS-only rule unchanged. Done when: `AssistantWs.LocalNeverUsesProxy` and `AssistantWs.NonLocalHttpStillRefused` pass.
4. Add gate state `NoEndpoint` for the local provider (no key is required, but an endpoint must be set and valid) and carry `destination_is_loopback` in `OutboundPreview` so the preview shows "stays on this computer (loopback)" plus the fixed notice that Recto cannot verify where the local server forwards data. Done when: `AssistantWs.GateNoEndpointState` and `AssistantWs.PreviewShowsLoopbackNotice` pass.

- [ ] Implement `ProviderId::Local`, the descriptor and the `assistant.baseUrl.local` setting with validation through `loopback_policy`. Done when: `AssistantWs.LocalBuildsRequestWithoutAuth` and `AssistantWs.InvalidLocalUrlFallsBack` pass.
- [ ] Implement `loopback_policy` and its use in the transport and in settings load. Done when: `AssistantWs.LoopbackPolicyRules` passes.
- [ ] Add a plain-HTTP loopback test server under `tests/` that records every request and a second listener on a non-loopback address that must record none. Done when: `AssistantWs.LocalRequestStaysOnLoopback` sends a request and the second listener saw zero connections.
- [ ] Implement `Test connection` for the local endpoint, listing models from the endpoint's model list when it offers one and otherwise leaving the model field free text. Done when: `AssistantWs.LocalTestConnectionTypedErrors` covers server not running, wrong port and unknown model with plain-language messages.
- [ ] Add the local provider to the Settings `ASSISTANT` group: provider choice, endpoint field with inline validation, model field, no key row. Done when: `Settings.AssistantLocalProviderRows` finds the rows and no key control for `Local`.
- [ ] Commit: `assistant: add local loopback provider with no key, no proxy and a narrow HTTP exception`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Local*:AssistantWs.LoopbackPolicyRules:AssistantWs.NonLocalHttpStillRefused:AssistantWs.GateNoEndpointState:AssistantWs.PreviewShowsLoopbackNotice` passes, and allowing `127.0.0.2` in `loopback_policy` fails `AssistantWs.LoopbackPolicyRules`.

## 3. Batch authorization: one job preview, many single-use request grants

Translation, semantic indexing and long summaries need many requests. Asking for consent on each of 200 batches is unusable, and a single "yes" for an open-ended job would break D19. This section keeps the rule honest: the user reviews the complete set of requests of a job at once (every batch's exact text, with totals), confirms once, and receives one single-use grant per previewed request. A request whose bytes differ from its previewed digest gets no grant. It matches the one-click start of UPDF's and PDFelement's translation, while keeping Recto's preview of exactly what leaves the machine.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/job_authorization.h/.cpp`: `JobPreview` (an ordered list of `OutboundPreview`, `D08 T01 §12`, with totals of requests, characters and images), `AssistantGate::authorize_batch(const JobPreview&, ConsentInput) -> AssistantResult<BatchAuthorization>`, and `BatchAuthorization::take(index) -> Authorization` that mints the same single-use `Authorization` as `D08 T01 §12` for exactly that request's digest, once. Done when: `AssistantWs.BatchGrantIsSingleUsePerDigest` asserts a second `take` of the same index fails and a request with a different digest is refused.
2. Enforce limits: at most 500 requests and `assistant.maxJobChars` characters (default 2000000, hard cap 10000000) per job; a larger job is refused with a message that names the limit and suggests a smaller range. Done when: `AssistantWs.JobLimitsEnforced` asserts both limits.
3. Expire grants when the job ends, is cancelled or the app closes, never persist them, and require a new preview for any request not in the original list (for example a split after `ContextTooLong`). Done when: `AssistantWs.GrantsExpireAndSplitNeedsNewPreview` passes.
4. Record a per-job entry in the in-memory request ledger (`D08 T01 §12`): feature, provider, model, request count, characters, outcome, never text. Done when: `AssistantWs.JobLedgerHoldsNoText` passes.

- [ ] Implement `JobPreview` and `authorize_batch` with the digest list. Done when: `AssistantWs.BatchGrantIsSingleUsePerDigest` passes.
- [ ] Implement the job limits and their settings. Done when: `AssistantWs.JobLimitsEnforced` passes.
- [ ] Implement expiry and the new-preview rule. Done when: `AssistantWs.GrantsExpireAndSplitNeedsNewPreview` passes.
- [ ] Add a virtualized preview model that yields every request's exact text page by page without loading the whole job into memory. Done when: `AssistantWs.JobPreviewIsVirtualizedAndExact` previews a 300-request job under a 64 MB working set and each request's previewed text equals its built body text.
- [ ] Commit: `assistant: add job-level authorization with single-use grants per previewed request`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.BatchGrant*:AssistantWs.JobLimits*:AssistantWs.Grants*:AssistantWs.JobLedger*:AssistantWs.JobPreviewIsVirtualizedAndExact` passes, and minting a grant for an unpreviewed digest fails `AssistantWs.BatchGrantIsSingleUsePerDigest`.

## 4. Structured output runner: bounded JSON, schema checks and verified quotes

Most features here need machine-readable answers: proposed bookmarks, flashcards, clauses, page fixes, command plans. Model output is untrusted. This section gives one runner that asks for JSON, parses it with bounds, validates it against a small schema, rejects anything else, and checks every quote against the text that was actually sent, so a hallucinated clause or page cannot reach the user as a finding. It keeps `D08 T01 §13`'s rule that output is a suggestion and plain text.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/structured_task.h/.cpp`: `run_structured<T>(Authorization, Schema, prompt_id, ContextPackage, CancelToken)` that asks for JSON only, collects the streamed text, and parses with a bounded parser (depth 32, 4 MiB, no duplicate keys, no NaN) returning `AssistantResult<T>`. Done when: `AssistantWs.StructuredParserBounds` rejects deep, huge, duplicate-key and trailing-garbage inputs without allocating more than 16 MB.
2. Add a small schema DSL (types, required fields, enums, string max lengths, array max sizes, integer ranges) and validate before any use. Done when: `AssistantWs.SchemaRejectsOutOfRange` rejects an unknown enum, an oversize array and a page number outside the sent pages.
3. Add `verify_quote(quote, ContextPackage)` normalizing whitespace and hyphenation line breaks and requiring the quote to occur in the sent text of the stated page. Done when: `AssistantWs.QuoteVerification` accepts a real quote across a line break and rejects an invented one.
4. On invalid output return `MalformedResponse` with a plain message and a Try again action that needs a new authorization; never re-send automatically. Done when: `AssistantWs.InvalidJsonNeverResendsAutomatically` counts one request.

- [ ] Implement the bounded JSON parser and the runner. Done when: `AssistantWs.StructuredParserBounds` passes.
- [ ] Implement the schema DSL and validation. Done when: `AssistantWs.SchemaRejectsOutOfRange` passes.
- [ ] Implement `verify_quote` and the `Unverified` marking used by later sections. Done when: `AssistantWs.QuoteVerification` passes.
- [ ] Sanitize every string field as plain text (no HTML, control characters stripped, length capped). Done when: `AssistantWs.StructuredStringsAreInert` feeds `<script>` and control characters and gets inert text.
- [ ] Add `fuzz/fuzz_structured_json.cpp` to `recto-fuzz.exe` with seeds in `tests/corpus/seeds/`. Done when: `recto-fuzz.exe fuzz_structured_json -runs=100000` reports no crash, leak or oversize allocation under ASan.
- [ ] Commit: `assistant: add bounded structured output runner with schema and verified quotes`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Structured*:AssistantWs.Schema*:AssistantWs.Quote*:AssistantWs.InvalidJson*` passes under ASan, and skipping `verify_quote` makes `AssistantWs.QuoteVerification` fail.

## 5. Rewrite, proofread, tone, expand and condense suggestion engine

UPDF's and PDFelement's AI writing tools rewrite or proofread text, change its tone, and make it longer or shorter. Acrobat's AI Assistant does not edit text in place. This engine produces suggestions only, per paragraph of the selection, with a word-level diff, and keeps `D08 T01 §11`'s exclusions so marked-for-redaction text is never sent. It must not change the document and must not send text beyond the selection.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/text_tasks.h/.cpp` with modes `Rewrite` (free instruction up to 500 characters), `Proofread`, `Tone` (formal, friendly, confident, neutral, plain), `Expand` and `Condense` (target ratio 25 to 200 percent), taking the selected paragraphs by `ParagraphId` (`D03 T04 §2`) and their text from `ContextBuilder`. Done when: `AssistantWs.TextTaskBuildsOneRequestPerBatch` asserts the request contains only the selected paragraphs.
2. Return `TextSuggestion{paragraph_id, original, suggested, changes}` through the structured runner (§4) with one entry per input paragraph id, exactly once. Done when: `AssistantWs.SuggestionIdsMatchInput` rejects a response that drops, duplicates or invents an id.
3. Compute a word-level diff (longest common subsequence) between original and suggestion for display and for run mapping in §6, and flag a suggestion longer than 3 times the original or empty as `Suspect`. Done when: `AssistantWs.DiffAndSuspectFlag` covers insert, delete, replace and the two flags.
4. Honour the length target for `Expand` and `Condense` by reporting the achieved ratio instead of hiding a miss. Done when: `AssistantWs.RatioReported` asserts the ratio field.

- [ ] Implement the five modes and their prompts, with the paragraph text inside the data block (`D08 T01 §13`). Done when: `AssistantWs.TextTaskBuildsOneRequestPerBatch` passes.
- [ ] Implement id matching and the diff. Done when: `AssistantWs.SuggestionIdsMatchInput` and `AssistantWs.DiffAndSuspectFlag` pass.
- [ ] Exclude paragraphs overlapping pending redaction marks and report them as not sent. Done when: `AssistantWs.TextTaskExcludesPendingRedaction` finds the marked paragraph in no request body.
- [ ] Commit: `assistant: add rewrite, proofread, tone, expand and condense suggestion engine`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.TextTask*:AssistantWs.SuggestionIds*:AssistantWs.Diff*:AssistantWs.Ratio*` passes, and sending a neighbouring paragraph fails `AssistantWs.TextTaskBuildsOneRequestPerBatch`.

## 6. Apply AI text suggestions through the undoable Edit PDF path

A suggestion becomes real only when the user applies it, and it must go through the same transaction that every Edit PDF change uses, so it is undoable, saves through the normal path and never touches the viewer's document in place. This matches UPDF's and PDFelement's apply-to-page behaviour; the Recto difference is the isolated `EditSession` copy (`D03 T04 §3`) and refusing where an edit is unsafe. It must not apply stale suggestions or edit a protected file.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/apply_text.h/.cpp`: `apply(EditSession&, std::span<const AcceptedSuggestion>)` that turns each accepted suggestion into a `RewriteParagraph` command (`D03 T04 §5`), mapping the diff so unchanged spans keep their original run formatting and changed spans take the formatting of the nearest preceding original character. Done when: `AssistantWs.ApplyKeepsRunFormattingOnUnchangedSpans` rewrites a two-run bold-and-regular paragraph and the unchanged words keep their fonts.
2. Refuse safely: stale text (the paragraph no longer equals the original the suggestion was made from), `NotEditable`, `NoRoom`, `PermissionDenied` and an `/Encrypt` modify restriction each skip that paragraph with the engine's reason and change nothing for it. Done when: `AssistantWs.ApplySkipsWithReasons` asserts each reason and an unchanged file for skipped paragraphs.
3. Make one Apply one undo step: a composite history entry in the tab's history that calls `EditSession::undo` once per applied paragraph, with no change to `EditSession` itself. Done when: `AssistantWs.ApplyIsOneUndoStep` applies five paragraphs, undoes once and finds the content streams byte-identical to before.
4. Start the edit flow through `edit.enter` (`D03 T04 §14`) when the tab is not in Edit PDF mode, including its save-or-discard prompt, so no unsaved work is lost. Done when: `AssistantWs.ApplyEntersEditModeCleanly` applies from a tab with an unsaved annotation and the annotation survives.

- [ ] Implement the diff-to-runs mapping. Done when: `AssistantWs.ApplyKeepsRunFormattingOnUnchangedSpans` passes.
- [ ] Implement the refusal reasons and the report type. Done when: `AssistantWs.ApplySkipsWithReasons` passes.
- [ ] Implement the composite undo step and redo. Done when: `AssistantWs.ApplyIsOneUndoStep` passes and redo restores all five.
- [ ] Prove idempotence: applying a suggestion equal to the current text produces no patch. Done when: `AssistantWs.ApplyIdenticalTextIsNoOp` passes.
- [ ] Commit: `assistant: apply AI text suggestions as one undoable Edit PDF step`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Apply*` passes under ASan, and applying a stale suggestion fails `AssistantWs.ApplySkipsWithReasons`.

## 7. Rewrite and proofread review in the Assistant panel

The user needs to see the original and the suggestion side by side, pick what to keep, and apply it. This is UPDF's and PDFelement's rewrite popover brought into Recto's one Assistant panel (D17), not a modal. It must not apply on selection, must show the exact outbound text through the preview dialog first, and must stay usable by keyboard and screen reader.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 8.3 (side-panel card and header action slot, `D02 T03 §3`) and section 9.3 (selection toolbar actions); no screenshot exists.
**Job:** A user selects text, chooses Rewrite, Proofread, Tone, Expand or Condense, reviews the request preview, sees each paragraph's original and suggestion with changes marked, accepts or discards each, and applies the accepted ones as one undo step. Consumer: the person editing a document; `apply_text` (§6).
**Treatment:** A `Rewrite` view in `AssistantPanel` (`D08 T01 §17`) with a mode selector, a tone `ComboBox`, an instruction `TextBox`, a virtualized list of paragraph cards (original, suggestion, diff toggle, Accept and Discard), a skipped-with-reason list, and `Apply accepted (N)`. Cheaper substitute that fails the checkpoint: replacing the selection with the model's text on arrival.
**Chrome:** `SidePanelHost` body (`D02 T03 §3`), the preview dialog of `D08 T01 §16`, `RectoColors.xaml` tokens for diff add and remove, Segoe Fluent Icons.

**Build order:**

1. Add the `Rewrite` view to `src/app/panels/AssistantPanel` reached from the Tools menu extension point of `D08 T01 §17`, with the mode selector and options. Done when: `Panels.AssistantRewriteViewMounts` finds the view in all three layout presets.
2. Route Send through the preview dialog and `authorize_batch` when the selection spans several requests, then show cards as suggestions stream back. Done when: `Panels.AssistantRewriteRequiresPreview` asserts no request is sent before the dialog's Send.
3. Add Accept and Discard per card, `Accept all`, the diff toggle and `Apply accepted (N)` calling `apply_text`; show the skipped list with reasons after Apply. Done when: `Panels.AssistantRewriteApplyAndSkipped` applies two of three cards and lists one skipped with its reason.

- [ ] Build the paragraph card with plain-text rendering and token-coloured diff spans. Done when: `Panels.AssistantRewriteCardsAreInert` renders `<b>` text literally and uses only token brushes.
- [ ] Give every control `AutomationProperties.Name` and a tooltip, announce completion through `AnnouncementService`, and make Accept, Discard and Apply keyboard operable. Done when: the accessibility inventory (`D03 T02 §3`) reports zero missing names for the view and `Panels.AssistantRewriteKeyboard` passes.
- [ ] Disable the view with the reason from §1 when the provider lacks `Chat`, and show the skipped-with-reason list. Done when: `Panels.AssistantRewriteDisabledReason` shows the reason text.
- [ ] Commit: `app: add Rewrite and Proofread review view to the Assistant panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.AssistantRewrite*` passes on the display-session host, and applying on arrival fails `Panels.AssistantRewriteRequiresPreview`.

## 8. Whole-document translation on a copy keeping layout

UPDF and PDFelement translate whole documents, and Foxit-class tools often lose layout. Recto translates paragraph by paragraph and writes the translated text back into the original boxes through the Edit PDF engine, on a copy saved under a new name, so the original is never modified and the layout, fonts, colours and structure tree are kept where the text fits. It sends only text the user previewed, in batches under one job authorization (§3), and it reports every paragraph it could not translate or fit. Acrobat has no in-place translation.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/translation_job.h/.cpp`: open its own `EditSession` on a snapshot of the saved file (`D03 T04 §3`), enumerate paragraphs for the chosen pages with `ParagraphId`s (`D03 T04 §2`), and build a `JobPreview` of batches of at most 6000 characters, each carrying paragraph ids, the glossary and the target language (BCP-47). Done when: `AssistantWs.TranslationBatchesCoverEveryParagraphOnce` asserts each paragraph is in exactly one batch.
2. Protect non-translatable tokens (numbers, URLs, email addresses, glossary "never translate" terms) with numbered placeholders in the request and restore them after, and require the response to return every id exactly once with every placeholder intact. Done when: `AssistantWs.PlaceholdersSurviveRoundTrip` asserts restored text equals the original tokens.
3. Apply translated text with `RewriteParagraph` (`D03 T04 §5`) using the fit policy: leave the paragraph at its size when it fits, otherwise `ShrinkToFit` by up to 15 percent in steps (default) or `LeaveOriginal`; a paragraph that still does not fit, whose target glyphs have no font (`D03 T04 §4`), or that is `NotEditable` stays in the original language and is reported with the reason. Done when: `AssistantWs.TranslateFitPolicyAndReport` asserts all four outcomes on fixtures.
4. Save through the full-rewrite save of `D03 T04 §9` to `<name>.<bcp47>.pdf` next to the source (numbered if it exists), temp-file-then-replace, never overwriting the source; warn in the report that digital signatures are not carried onto the copy. Done when: `AssistantWs.TranslateNeverTouchesOriginal` finds the source file byte-identical and the copy present.
5. Handle cancellation by discarding the partial copy within 500 ms and releasing the job, and report pages with no extractable text as candidates for OCR. Done when: `AssistantWs.TranslateCancelDiscardsCopy` passes.

- [ ] Implement paragraph enumeration, batching and the glossary (up to 200 terms, each "translate as" or "never translate"). Done when: `AssistantWs.TranslationBatchesCoverEveryParagraphOnce` and `AssistantWs.GlossaryAppearsInPreview` pass.
- [ ] Implement placeholders and response validation. Done when: `AssistantWs.PlaceholdersSurviveRoundTrip` and `AssistantWs.TranslateRejectsMissingIds` pass.
- [ ] Implement the fit policy, right-to-left targets through the shaping rules of `D03 T04 §5`, and the report. Done when: `AssistantWs.TranslateFitPolicyAndReport` and `AssistantWs.TranslateArabicTargetShapedOrReported` pass.
- [ ] Implement the copy save and cancellation. Done when: `AssistantWs.TranslateNeverTouchesOriginal` and `AssistantWs.TranslateCancelDiscardsCopy` pass.
- [ ] Verify the structure tree and marked content of translated pages stay valid with the `D04 T05 §7` validator on a tagged fixture. Done when: `AssistantWs.TranslateKeepsTagsValid` reports zero new findings.
- [ ] Commit: `assistant: translate whole documents on a copy keeping layout`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Translat*:AssistantWs.Placeholders*:AssistantWs.Glossary*` passes under ASan, and writing over the source fails `AssistantWs.TranslateNeverTouchesOriginal`.

## 9. Bilingual output: side-by-side wide pages and interleaved pages

PDFelement and UPDF offer bilingual output, original and translation together. Recto builds it from the original and the translated copy of §8 with its own graft engine: either one wide page per source page with the original on the left and the translation on the right, or alternating pages. It is a new file, never an in-place change, and it states what it does not carry.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/pdf/bilingual_layout.h/.cpp`: `make_side_by_side(original, translated, gutter)` creating pages of width `w1 + gutter + w2` and height `max(h1, h2)` that place each source page as a Form XObject through `GraftMap` (`D05 T01 §1`) with a translation matrix, returning `PdfResult<Document>`. Done when: `Bilingual.SideBySideGeometry` asserts page boxes and placement for pages of different sizes.
2. Add `make_interleaved(original, translated)` using the multi-document merge (`D05 T01 §2`) with the page selection original 1, translated 1, original 2, and so on. Done when: `Bilingual.InterleavedOrder` asserts the order and page count `2n`.
3. Require equal page counts and refuse otherwise with `PageCountMismatch`; state in the job report that side-by-side output carries no links, fields or structure tree (a visual copy) while interleaved output keeps each page as it was. Done when: `Bilingual.RefusesMismatchedCounts` and `Bilingual.ReportStatesWhatIsNotCarried` pass.
4. Save as `<name>.<bcp47>.bilingual.pdf` temp-file-then-replace. Done when: `Bilingual.AtomicSave` kills the writer mid-write and the target is absent or complete.

- [ ] Implement side-by-side placement. Done when: `Bilingual.SideBySideGeometry` passes and a rendered left half equals the original page render within the `D00 T03 §2` SSIM threshold.
- [ ] Implement interleaving. Done when: `Bilingual.InterleavedOrder` passes.
- [ ] Add `Bilingual.*` tests in `tests/recto-tests/bilingual_tests.cpp` under ASan. Done when: `recto-tests.exe --gtest_filter=Bilingual.*` passes.
- [ ] Commit: `pdf: add bilingual side-by-side and interleaved document output`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Bilingual.*` passes under ASan, and swapping the left and right pages fails `Bilingual.SideBySideGeometry`.

## 10. Translation dialog: language, glossary, job preview, progress and report

The user starts a translation from one dialog: pick the target language, the pages, the glossary, the output (translated only, side by side, interleaved) and the fit policy, then review the complete job preview and confirm once. This follows the Convert flow of Recto (queue card, completion notice) rather than a new pattern, and it matches the translate dialogs of UPDF and PDFelement while adding the preview those lack.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface on the `design/SPEC.md` section 9.9 dialog chrome (`D02 T01 §10`) with the `QueueCard` of `D04 T01 §5` for progress; no screenshot exists.
**Job:** A user translates a PDF into another language, sees exactly what will be sent for every batch before starting, watches progress, and opens the result and a report of paragraphs that stayed in the original language. Consumer: the person reading a foreign-language document.
**Treatment:** A `ContentDialog` with options, a glossary editor, a virtualized job-preview list (every batch's exact text, totals of requests and characters), `Start` and `Cancel`; progress in the `QueueCard`; a completion notice with `Open file` and `View report`. Cheaper substitute that fails the checkpoint: a one-line "this will send your document" confirmation or a silent background start.
**Chrome:** `RectoDialogStyles.xaml`, `QueueCard`, `RectoColors.xaml` tokens, Segoe Fluent Icons.

**Build order:**

1. Author `src/app/dialogs/TranslateDocumentDialog.xaml/.h/.cpp` with the options and glossary editor, enabling Start only after the job preview has been scrolled to its end or the user ticks "I have reviewed this". Done when: `Shell.TranslateDialogOptionsAndReview` asserts Start is disabled until review.
2. Run the job as a `ConversionJob` (`D04 T01 §1`) with its own `EditSession`, progress and per-job cancel in `QueueCard`, and completion through the focus-aware notice (`D04 T01 §6`). Done when: `Shell.TranslateJobQueueAndCancel` cancels mid-job and the copy is discarded.
3. Show the report (translated, skipped with reason, shrunk, left in original) as a list that jumps to the page of each entry. Done when: `Shell.TranslateReportJumpsToPage` passes.

- [ ] Build the dialog with token brushes and Light, Dark and HighContrast themes. Done when: `Shell.TranslateDialogThemes` finds no literal colour.
- [ ] Label every control for UIA and expose the job-preview text to the UIA text pattern. Done when: the accessibility inventory reports zero missing names and `Shell.TranslatePreviewUia` reads a batch's text.
- [ ] Disable the command with the reason from §1 when the provider lacks `Chat`. Done when: `Shell.TranslateDisabledReason` shows the reason.
- [ ] Commit: `app: add Translate document dialog with job preview, progress and report`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.Translate*` passes on the display-session host, and starting without the review step fails `Shell.TranslateDialogOptionsAndReview`.

## 11. Multi-document context builder with per-file citations

Asking one question across several documents (UPDF's and PDFelement's chat across files; Acrobat's assistant also works across files) needs a send set that is exact per file. This builder wraps `ContextBuilder` once per chosen open document, shares the character budget fairly, labels each page with a file alias, and extends the citation form so answers cite `[file N, p. M]`. It must never send file names or paths unless the user ticks that, and it applies every exclusion of `D08 T01 §11` to every file.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/multi_context.h/.cpp`: input a list of up to 10 chosen `DocumentTabSession`s, each with its own scope, exclusions, security flags and the shared budget; output a `MultiContextPackage` of per-file `ContextPackage`s labelled `=== File 2, Page 5 ===` with aliases `File 1` to `File N` (local mapping to display names). Done when: `AssistantWs.MultiContextAliasesHideNames` finds no file name or path in the built request unless "send file names" is ticked.
2. Split the budget evenly with spare capacity redistributed, record `OverBudget` per file, and show truncation per file. Done when: `AssistantWs.MultiContextBudgetPerFile` asserts the split and the per-file truncation report.
3. Extend the citation parser (`D08 T01 §13`) with `[file N, p. M]` validated against the files and pages actually sent. Done when: `AssistantWs.MultiCitationsOnlySentPairs` rejects a citation to an unsent file or page.
4. Apply protected-content, redaction and extraction exclusions per file, so one protected file does not block or leak into the others. Done when: `AssistantWs.MultiContextExclusionsPerFile` passes with one `AES-256` file and one file with pending marks.

- [ ] Implement `MultiContextBuilder` over `ContextBuilder` using each tab's own `DocumentActor::get_page_text` snapshot. Done when: `AssistantWs.MultiContextAliasesHideNames` passes and no document is touched from the wrong thread under ASan.
- [ ] Implement the budget split and the per-file notices in `ResultNotice`. Done when: `AssistantWs.MultiContextBudgetPerFile` passes.
- [ ] Implement the multi-file citation parser. Done when: `AssistantWs.MultiCitationsOnlySentPairs` passes.
- [ ] Commit: `assistant: add multi-document context builder with per-file citations`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.MultiContext*:AssistantWs.MultiCitations*` passes under ASan, and putting a file name in the request fails `AssistantWs.MultiContextAliasesHideNames`.

## 12. Multi-document chat across chosen open files

The user picks which open documents the assistant may read and asks questions across them. This matches UPDF's and PDFelement's multi-file chat; Recto adds a per-file view of exactly what is sent and citations that open the right tab and page. It must not read any tab the user did not tick and must show each file's included pages and exclusions.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 8.3 (side-panel card) and the `D08 T01 §16` preview dialog; no screenshot exists.
**Job:** A user ticks two to ten open documents, asks a question, reviews the per-file send set, reads a streamed answer and clicks `File 2, p. 5` to land on that page of that tab. Consumer: a researcher comparing documents.
**Treatment:** An `Across files` mode in `AssistantPanel` with a checklist of open tabs (name, page count, security note), per-file scope, the shared question box, the standard preview dialog grouped by file, and citation buttons that activate the tab and navigate. Cheaper substitute that fails the checkpoint: reading all open tabs by default, or citations that show only a page number.
**Chrome:** `SidePanelHost`, `D08 T01 §16` dialog, token brushes, Segoe Fluent Icons.

**Build order:**

1. Add the `Across files` mode with the tab checklist, empty and one-file states, and the per-file scope controls. Done when: `Panels.AssistantAcrossFilesChecklist` asserts nothing is ticked by default and the sent set equals the ticked set.
2. Extend the preview dialog model to group by file with per-file exclusions and totals. Done when: `Shell.AssistantPreviewGroupsByFile` shows each file's pages and exclusion reasons.
3. Implement citation buttons that call the tab switcher and go-to-page for `[file N, p. M]`. Done when: `Panels.AssistantFileCitationOpensTabAndPage` lands on the right tab and page, and an unsent pair is plain text.

- [ ] Wire answers through `MultiContextBuilder` and `AssistantService` with `Authorization` for the single request. Done when: `Panels.AssistantAcrossFilesAnswer` streams a fake answer with citations from two files.
- [ ] Label every control for UIA, announce start and finish, and verify keyboard use. Done when: the accessibility inventory reports zero missing names and `Panels.AssistantAcrossFilesKeyboard` passes.
- [ ] Handle a ticked tab that closes mid-request by dropping its citations to plain text with a notice. Done when: `Panels.AssistantAcrossFilesTabClosed` passes.
- [ ] Commit: `app: add multi-document chat across chosen open files`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.AssistantAcrossFiles*:Shell.AssistantPreviewGroupsByFile:Panels.AssistantFileCitation*` passes on the display-session host, and reading an unticked tab fails `Panels.AssistantAcrossFilesChecklist`.

## 13. Embeddings requests for providers and local models

Search by meaning needs vectors. UPDF and PDFelement compute them on their own servers; Recto asks the user's chosen provider, or the local model, to embed text, and only for providers and models that offer embeddings (§1). This section adds the embedding request and response to the provider interface and sends through job authorization (§3). It must not embed text that was not previewed, and Anthropic-style providers without an embeddings endpoint stay disabled with the reason.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Extend `IAssistantProvider` (`D08 T01 §1`) with optional `build_embedding_request(texts, config)` and `parse_embedding_response(body)` for OpenAI-compatible `/embeddings` (OpenAI, OpenRouter where the model offers it, local) and the Gemini embedding endpoint; providers without one report the `Embeddings` capability as absent. Done when: `AssistantWs.EmbeddingRequestShapes` asserts each adapter's request against a fixture and that no key appears in the request object.
2. Bound the call: at most 96 texts per request and 8000 characters per text (longer text is cut at a sentence boundary and the cut is recorded, never silent), vectors validated for count, finite floats and a single dimension, and normalized to unit length. Done when: `AssistantWs.EmbeddingResponseValidation` rejects a wrong count, NaN and mixed dimensions.
3. Add the settings `assistant.embeddingModel.<provider>` with a default verified against the provider's published list on the day the section is built and recorded with its date in `docs/assistant.md`. Done when: `AssistantWs.EmbeddingModelDefaultsDocumented` passes.

- [ ] Implement the request builders and parsers for the three wire shapes with fixtures under `tests/assistant-fixtures/`. Done when: `AssistantWs.EmbeddingRequestShapes` passes.
- [ ] Implement validation, normalization and the visible cut record. Done when: `AssistantWs.EmbeddingResponseValidation` and `AssistantWs.EmbeddingCutIsRecorded` pass.
- [ ] Send embedding batches only with a `BatchAuthorization` grant. Done when: `AssistantWs.EmbeddingsNeedGrant` shows a send without a grant does not compile.
- [ ] Commit: `assistant: add embeddings requests for providers and local models`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Embedding*` passes, and accepting a vector with NaN fails `AssistantWs.EmbeddingResponseValidation`.

## 14. Semantic index store beside the text index

The vectors for search by meaning are stored locally, beside the text index of `D03 T05 §4`, and a given piece of text is embedded once per model and never uploaded twice. The index is a derived cache like the text index: a corrupt or wrong-model file is discarded and rebuilt, never trusted. It holds vectors and offsets but no document text, and the settings copy says the vectors are derived from the user's documents and can be deleted.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define the format in `src/core/semantic_index.h/.cpp` (magic `RSEM`, version, little-endian): header (provider, embedding model, dimension, chunker version), file table (path, file ID, size, last write time, change time, page count, status), chunk table (file, page, start and end offsets, SHA-256 of the chunk text) and float32 unit vectors. Done when: `SemanticIndex.RoundTrip` writes and reads a 3-file index byte-identically.
2. Load with full validation (every offset and length checked with checked arithmetic, 2 GB total cap, chunk count cap) returning `PdfResult<SemanticIndex>`. Done when: `SemanticIndex.RejectsCorrupt` returns a typed error for truncation, flipped bytes, a wrong version and a table claiming 4 billion chunks, allocating under 64 MB.
3. Resolve the folder as `AppPaths::data_dir() / "semantic-index"` (Portable mode respected) and write every file temp-file-then-replace. Done when: `SemanticIndex.PortableMode` and `SemanticIndex.AtomicWrite` pass.
4. Add the vector cache keyed by (model, chunk SHA-256) so identical text, even in another file, is never embedded again, and keep one index set per model so switching model never mixes vectors. Done when: `SemanticIndex.CacheDedupesAcrossFiles` and `SemanticIndex.ModelSwitchUsesSeparateSet` pass.

- [ ] Implement the format, loader and writer. Done when: `SemanticIndex.RoundTrip` and `SemanticIndex.RejectsCorrupt` pass.
- [ ] Treat a corrupt or wrong-version index as absent and record the reason. Done when: `SemanticIndex.CorruptIsRebuiltNotTrusted` shows a flipped byte yields an empty index and never a wrong hit.
- [ ] Never index encrypted files or pages without stored text; store status only. Done when: `SemanticIndex.NoSecretsIndexed` finds no vector for `encrypted-aes-256.pdf` and status `SkippedEncrypted`.
- [ ] Add `fuzz/fuzz_semantic_index.cpp` with seeds built by `scripts/build-corpus.py`. Done when: `recto-fuzz.exe fuzz_semantic_index -runs=100000` reports no crash, leak or oversize allocation under ASan.
- [ ] Commit: `search: add SemanticIndex store with model-keyed vector cache`

**Test checkpoint:** `recto-tests.exe --gtest_filter=SemanticIndex.*` passes under ASan, and a corrupt index returning a hit fails `SemanticIndex.CorruptIsRebuiltNotTrusted`.

## 15. Semantic index build job with one preview and never-upload-twice reuse

Building the index sends text to the embedding model, so the user reviews every chunk to be embedded before anything leaves the machine. Chunks already in the cache are not sent again. This matches the "index my files" step of UPDF's and PDFelement's AI search, with Recto's preview and its local-first option: with the local provider nothing leaves the computer.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/semantic_index_job.h/.cpp`: chunk page text from `PageText` (paragraph-aligned, at most 1200 characters with 150 overlap, chunker version recorded), apply the exclusions of `D08 T01 §11` for open documents, read unopened files through `FileScope::open_checked` (`D03 T05 §2`), and build a `JobPreview` (§3) of only the chunks whose hash is not in the cache. Done when: `AssistantWs.SemanticJobPreviewsOnlyUncachedChunks` indexes a corpus twice and the second preview is empty.
2. Embed through `BatchAuthorization` grants with checkpointing every 50 batches, cancel at any batch boundary within 500 ms, and resume after cancel or crash by recomputing the preview for the remaining chunks (a new confirmation). Done when: `AssistantWs.SemanticJobResumesWithNewPreview` passes.
3. Validate freshness as `D03 T05 §5` does (file identity, size, times read from the same handle after reading) and discard chunks from a file that changed mid-read. Done when: `AssistantWs.SemanticJobFileChangedMidRead` finds no vectors from the changed read.
4. Cap a job at 200000 chunks and the limits of §3, and run at below-viewer priority. Done when: `AssistantWs.SemanticJobCapsAndPriority` passes.

- [ ] Implement chunking and the job preview. Done when: `AssistantWs.SemanticJobPreviewsOnlyUncachedChunks` passes.
- [ ] Implement embedding, checkpointing, cancel and resume. Done when: `AssistantWs.SemanticJobResumesWithNewPreview` passes.
- [ ] Implement freshness and caps. Done when: `AssistantWs.SemanticJobFileChangedMidRead` and `AssistantWs.SemanticJobCapsAndPriority` pass.
- [ ] Publish typed events (`SemanticProgress`, `SemanticSkipped`, `SemanticRebuilt`) on the job event channel of `D03 T05 §5`. Done when: `AssistantWs.SemanticJobEvents` receives them in order.
- [ ] Commit: `assistant: add previewed semantic index build job with vector reuse`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.SemanticJob*` passes under ASan, and re-embedding a cached chunk fails `AssistantWs.SemanticJobPreviewsOnlyUncachedChunks`.

## 16. Semantic query: previewed query embedding, ranking and verified hits

A meaning search embeds the user's query and ranks stored vectors locally. The query text is itself an outbound request, so it is previewed and sent only when the user presses Search, not on every keystroke. Ranking, the index and the results never leave the machine. Hits are checked against the real page text so a stale index cannot show something that is no longer there.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/semantic_query.h/.cpp`: `search(query, scope, k)` embeds the query with one authorized request (cached in memory per session by query text and model so a repeat sends nothing), then ranks by cosine similarity against the scoped vectors and returns the top `k` (default 20) above a minimum score. Done when: `AssistantWs.SemanticRankingOrder` ranks a fixture so the known best chunk is first.
2. Confirm every hit: re-read the file with `FileScope::open_checked`, require the stored file identity to match, extract the chunk text from the real `PageText` and return the page, a quote and the score; files that changed since indexing are excluded and counted as stale. Done when: `AssistantWs.SemanticHitsAreConfirmed` and `AssistantWs.SemanticStaleFilesExcluded` pass.
3. Refuse when the query model differs from the index model, with a message naming both and the rebuild action. Done when: `AssistantWs.SemanticModelMismatchRefused` passes.

- [ ] Implement the cache, ranking and top-k selection (bounded heap). Done when: `AssistantWs.SemanticRankingOrder` and `AssistantWs.SemanticRepeatQuerySendsNothing` pass.
- [ ] Implement confirmation and the stale count. Done when: `AssistantWs.SemanticHitsAreConfirmed` passes.
- [ ] Implement the model-mismatch refusal. Done when: `AssistantWs.SemanticModelMismatchRefused` passes.
- [ ] Commit: `assistant: add previewed semantic query with confirmed ranked hits`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Semantic*` passes, and trusting an unverified hit fails `AssistantWs.SemanticHitsAreConfirmed`.

## 17. Search by meaning mode, results and index controls

The Search panel gains a `Meaning` mode next to words: the user builds an index once, types a question, presses Search by meaning and gets files and pages ranked by relevance. It reuses the grouped results list and open-at-location behaviour of `D03 T05` and adds index controls. It matches the AI search of UPDF and PDFelement and keeps Recto's preview and local option.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 9.3 (find card rows, match context rows) and the `D03 T05 §7` panel; no screenshot exists.
**Job:** A user with a folder of papers asks "where do they discuss refunds?", reviews the one-line request preview, and clicks a result to land on the page. Consumer: the person searching a folder or their open tabs.
**Treatment:** A `Words | Meaning` toggle in the Search panel; the Meaning view has the scope picker, a `Build index` button opening the job preview, an inline request-preview row (provider, model, exact query text) with `Search by meaning`, score bars in the grouped results, a stale-files note, and index size with `Delete meaning index`. Cheaper substitute that fails the checkpoint: sending the query as the user types.
**Chrome:** `D03 T05 §7` panel and list, token brushes, Segoe Fluent Icons.

**Build order:**

1. Add the mode toggle and Meaning view, disabled with the reason from §1 when the provider lacks `Embeddings` or no index exists. Done when: `Panels.MeaningSearchDisabledReason` shows each reason.
2. Add the inline preview row and send only on the button or Enter on that row. Done when: `Panels.MeaningSearchSendsOnlyOnConfirm` types a query and counts zero requests until confirmed.
3. Show results with the open-at-location behaviour of `D03 T05 §8` and score bars, and add `Delete meaning index` with a confirmation that names the folder. Done when: `Panels.MeaningSearchOpenAtLocation` and `Panels.MeaningIndexDelete` pass.

- [ ] Wire `Build index` to the job preview and `QueueCard`. Done when: `Panels.MeaningIndexBuildFlow` runs a small corpus with a fake provider.
- [ ] Label every control for UIA, announce result counts politely and check keyboard use. Done when: the accessibility inventory reports zero missing names and `Panels.MeaningSearchKeyboard` passes.
- [ ] Commit: `app: add Search by meaning mode with previewed queries and index controls`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.MeaningSearch*:Panels.MeaningIndex*` passes on the display-session host, and sending on keystroke fails `Panels.MeaningSearchSendsOnlyOnConfirm`.

## 18. Natural-language command planner over the command registry

UPDF and PDFelement let users describe an action in words. Recto's version maps a request to commands that already exist in the `CommandRegistry` (`D02 T03 §11`) and proposes them; it never executes anything itself. The request sends the user's words and the command catalog, which contains no document content, and every proposed command is validated against the registry before it is shown.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/command_planner.h/.cpp`: build a catalog from the registry (id, title, synonyms, one-line description, parameter schema, risk class `Normal` or `Destructive`), excluding the assistant's own key and settings commands; send the full catalog when it is at most 40 KB, otherwise the 120 best lexical matches to the request, and show that choice in the preview. Done when: `AssistantWs.PlannerCatalogHasNoDocumentContent` finds no page text, file name or path in the request.
2. Request a plan through the structured runner (§4): at most 5 commands, each `{id, args}`, where `args` must satisfy the registry's declared parameter schema (for example page number, zoom percent); drop unknown ids and invalid args and list them as dropped. Done when: `AssistantWs.PlannerValidatesAgainstRegistry` drops an invented command and an out-of-range argument.
3. Mark `Destructive` commands (delete pages, apply redactions, sign, encrypt, overwrite) in the plan and require the command's own confirmation when run; the planner never suppresses a command's dialogs. Done when: `AssistantWs.PlannerNeverSkipsConfirmations` asserts a planned `Apply Redactions` still shows its dialog.
4. Execute only commands the user selected, in order, stopping at the first failure or cancel. Done when: `AssistantWs.PlannerRunsSelectedOnlyAndStopsOnFailure` passes.

- [ ] Implement catalog building and the size rule. Done when: `AssistantWs.PlannerCatalogHasNoDocumentContent` passes.
- [ ] Implement plan validation and the dropped list. Done when: `AssistantWs.PlannerValidatesAgainstRegistry` passes.
- [ ] Implement ordered execution through the registry's own `execute`. Done when: `AssistantWs.PlannerRunsSelectedOnlyAndStopsOnFailure` and `AssistantWs.PlannerNeverSkipsConfirmations` pass.
- [ ] Commit: `assistant: add natural-language command planner over the command registry`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Planner*` passes, and executing a command the user did not select fails `AssistantWs.PlannerRunsSelectedOnlyAndStopsOnFailure`.

## 19. Command box: ask for a command, review the plan, run what you choose

In the `Ctrl+K` box, when no command matches what the user typed, a row offers to ask the assistant. The user sees exactly what will be sent, presses Send, reads the proposed commands with the model's one-line reason, ticks the ones to run and presses Run. This is Recto's version of UPDF's and PDFelement's command prompts, with the plan always shown before anything runs and no auto-execution.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface inside the command box of `D02 T03 §12` (`design/SPEC.md` section 9.2 command box rows); no screenshot exists.
**Job:** A user types "make this page landscape and save a copy", gets no direct match, asks the assistant, sees the request, then a list of commands to run or skip. Consumer: someone who does not know the command names.
**Treatment:** An `Ask AI to find a command` row appears only when the assistant is enabled and ready; selecting it expands an inline preview (provider, model, exact text, catalog size with an expander) with `Send`; results are a checkbox list with a destructive badge and the explanation; `Run selected` executes them. Enter on the query never sends. Cheaper substitute that fails the checkpoint: running the first proposed command on Enter.
**Chrome:** command box overlay (`D02 T03 §12`), token brushes, Segoe Fluent Icons.

**Build order:**

1. Add the row and expansion in the command box view model, shown only when the gate state is `Ready` and no registry match exists. Done when: `Shell.CommandBoxAskRowVisibility` asserts the row is absent when the assistant is off, has no key, or a command matches.
2. Add the inline preview with `Send` and the result list with `Run selected` and `Esc` to close. Done when: `Shell.CommandBoxAskRequiresSend` asserts no request before `Send` and no execution before `Run selected`.
3. Show dropped items from the planner and destructive badges. Done when: `Shell.CommandBoxPlanShowsDroppedAndDestructive` passes.

- [ ] Label every row and button for UIA and announce the plan count. Done when: the accessibility inventory reports zero missing names and `Shell.CommandBoxAskKeyboard` passes.
- [ ] Render with token brushes in Light, Dark and HighContrast. Done when: `Shell.CommandBoxAskThemes` finds no literal colour.
- [ ] Commit: `app: add ask-for-a-command flow to the command box with plan review`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Shell.CommandBoxAsk*:Shell.CommandBoxPlan*` passes on the display-session host, and running on Enter fails `Shell.CommandBoxAskRequiresSend`.

## 20. AI bookmark proposals and per-bookmark summaries

PDFelement and UPDF generate outlines and section summaries with AI. Recto proposes a bookmark tree and a short summary per bookmark from the document text, shows it for review, and applies the accepted bookmarks as one undoable step through the bookmark editor's history (`D02 T03 §6`). It does not invent pages and it marks which titles are quoted headings and which are generated. Summaries are kept for display and export; they are not written into the PDF.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/bookmark_proposals.h/.cpp` using the structured runner (§4): `BookmarkProposal{title, page, level (1 to 4), source: Heading|Generated, summary}`; titles at most 120 characters; pages must be among the sent pages; `Heading` titles must pass `verify_quote` on their page. Done when: `AssistantWs.BookmarkProposalValidation` rejects an unsent page, a level jump of more than one and an unverifiable `Heading`.
2. Batch long documents by page ranges through `JobPreview` (§3) and merge the partial outlines by page order, keeping levels consistent. Done when: `AssistantWs.BookmarkProposalMergesBatches` passes.
3. Apply accepted proposals as one composite command on the bookmark undo stack in `Add to existing` or `Replace outline` mode (the latter behind a confirmation), through the `/Outlines` serializer of `D02 T03 §6`. Done when: `AssistantWs.BookmarkApplyIsOneUndoStep` applies, undoes and finds `/Outlines` identical to before.
4. Export summaries as Markdown with page numbers; never write them into the PDF. Done when: `AssistantWs.BookmarkSummaryExport` writes the file atomically and the PDF is unchanged.

- [ ] Implement proposal parsing and validation. Done when: `AssistantWs.BookmarkProposalValidation` passes.
- [ ] Implement batching and merge. Done when: `AssistantWs.BookmarkProposalMergesBatches` passes.
- [ ] Implement apply and undo. Done when: `AssistantWs.BookmarkApplyIsOneUndoStep` passes.
- [ ] Implement the summary export. Done when: `AssistantWs.BookmarkSummaryExport` passes.
- [ ] Commit: `assistant: add AI bookmark proposals with summaries and undoable apply`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Bookmark*` passes under ASan, and a proposal to an unsent page fails `AssistantWs.BookmarkProposalValidation`.

## 21. Bookmark proposals review in the Outline panel

The Outline panel gets a `Suggest bookmarks with AI` action. The user reviews the proposed tree, edits titles, ticks what to keep, reads summaries, and applies. It extends the bookmark editor of `D02 T03 §5` and `§6` rather than adding a second place to edit bookmarks.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface extending `design/SPEC.md` section 8.3 (Outline with Bookmark Editor); no screenshot exists.
**Job:** A user with an unstructured PDF gets a proposed table of contents, fixes a title, unticks two entries and applies the rest. Consumer: the person navigating a long document.
**Treatment:** A header action in `OutlinePanel` opens a proposals `TreeView` with checkboxes, inline title edit, `Heading` or `Generated` badges, a summary expander per node, and `Apply (N)` with an `Add` or `Replace` choice; the standard preview dialog runs before sending. Cheaper substitute that fails the checkpoint: replacing the existing outline on arrival.
**Chrome:** `OutlinePanel` (`D02 T03 §5`), the preview dialog, token brushes, Segoe Fluent Icons.

**Build order:**

1. Add the header action and proposals tree, disabled with the §1 reason when the provider lacks `Chat`. Done when: `Panels.BookmarkProposalsMount` and `Panels.BookmarkProposalsDisabledReason` pass.
2. Wire Apply to §20 and show the result in the normal outline with undo available. Done when: `Panels.BookmarkProposalsApplyAndUndo` applies, presses `Ctrl+Z` and finds the outline restored.
3. Label every control for UIA and test keyboard use. Done when: the accessibility inventory reports zero missing names and `Panels.BookmarkProposalsKeyboard` passes.

- [ ] Render with token brushes and plain-text titles. Done when: `Panels.BookmarkProposalsInert` renders `<b>` literally.
- [ ] Commit: `app: add AI bookmark proposals review to the Outline panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.BookmarkProposals*` passes on the display-session host, and applying on arrival fails `Panels.BookmarkProposalsApplyAndUndo`.

## 22. AI page proposals: blank, misoriented and duplicate pages

Scanned files often contain blank pages, sideways pages and duplicates. UPDF and PDFelement flag them with AI. Recto finds blank and duplicate pages locally with no request at all, and uses the model only for orientation and ambiguous pages, sending low-resolution page images that the user previewed. The output is a list of proposed Organize operations; nothing is changed until the user applies them to a copy.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/page_proposals.h/.cpp` with a local pre-pass: `Blank` (no stored text and ink coverage under 0.2 percent of a 100 DPI render), `ImageOnly`, and `Duplicate` (equal SHA-256 of normalized text and equal 64-bit difference hash of a low-resolution render). Done when: `AssistantWs.LocalBlankAndDuplicateNeedNoRequest` finds blank and duplicate pages in a fixture with the transport factory counter unchanged.
2. For candidates needing judgement (image-only pages for orientation, noisy near-blank pages), build `JobPreview` (§3) entries with page images of at most 512 pixels on the long edge, masked for pending redaction marks, and ask the structured runner for `{page, issue: Misoriented|Blank|None, rotate: 90|180|270|null}`. Done when: `AssistantWs.PageProposalImagesAreBoundedAndMasked` decodes the request images and checks size and masking.
3. Convert accepted proposals into Organize edit-list operations (`D05 T01 §3`: delete, rotate; for duplicates keep the first) as a list the Organize model applies on a copy. Done when: `AssistantWs.PageProposalsToEditList` produces the expected operations and leaves the source file untouched.

- [ ] Implement the local detectors with deterministic thresholds recorded in `docs/assistant.md`. Done when: `AssistantWs.LocalBlankAndDuplicateNeedNoRequest` and `AssistantWs.DuplicateHashIgnoresWhitespace` pass.
- [ ] Implement the image candidate rendering through the `RenderPool` path. Done when: `AssistantWs.PageProposalImagesAreBoundedAndMasked` passes.
- [ ] Implement proposal validation (pages exist, rotations allowed) and the edit-list conversion. Done when: `AssistantWs.PageProposalValidation` and `AssistantWs.PageProposalsToEditList` pass.
- [ ] Commit: `assistant: add AI page proposals with local blank and duplicate detection`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.PageProposal*:AssistantWs.LocalBlank*:AssistantWs.DuplicateHash*` passes, and a blank-page request sent for a clear blank fixture fails `AssistantWs.LocalBlankAndDuplicateNeedNoRequest`.

## 23. Page proposals review in the Organize grid

The user sees proposals as badges on the thumbnails they already work with and applies the ones they accept through the Organize model's undo stack. It extends the page grid of `D05 T02 §1`.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface extending `design/SPEC.md` section 9.6 (Organize modes) and the `D05 T02 §1` page grid; no screenshot exists.
**Job:** A user opens a scanned batch, chooses Suggest fixes, sees badges for blank, sideways and duplicate pages, accepts some and saves a copy. Consumer: the person cleaning scans.
**Treatment:** An `AI suggestions` button in the Organize toolbar opens a side list and thumbnail badges (`Blank`, `Rotate 90`, `Duplicate of 4`); Accept per item or `Accept all of a kind`; accepted items become normal `OrganizeEditModel` operations with `Undo`. The local detectors run immediately; the model step needs the preview dialog. Cheaper substitute that fails the checkpoint: deleting flagged pages on arrival.
**Chrome:** `PageGrid`, `OrganizeEditModel` (`D05 T02 §1`), token brushes, Segoe Fluent Icons.

**Build order:**

1. Add the toolbar button, local-first run and the badges. Done when: `Organize.AiSuggestionsBadges` shows badges from the local pass with no request.
2. Add the side list with Accept and the model step behind the preview dialog. Done when: `Organize.AiSuggestionsRequirePreviewForModelStep` passes.
3. Apply accepted items as undoable operations and keep Save and Save as copy on the normal path (`D05 T02 §5`). Done when: `Organize.AiSuggestionsApplyAndUndo` passes.

- [ ] Label every badge and control for UIA and test keyboard use. Done when: the accessibility inventory reports zero missing names and `Organize.AiSuggestionsKeyboard` passes.
- [ ] Commit: `app: add AI page suggestions to the Organize grid`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Organize.AiSuggestions*` passes on the display-session host, and deleting on arrival fails `Organize.AiSuggestionsApplyAndUndo`.

## 24. Mind map: outline, layout, Markdown, OPML and image exports

UPDF's and PDFelement's AI mind map turns a document into a diagram. Recto asks for a node tree, lays it out locally with its own deterministic layout, renders it with Direct2D, and exports Markdown, OPML 2.0 or a PNG, or inserts the image into a copy of the PDF. All text stays plain, and the exports are written temp-file-then-replace.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/mind_map.h/.cpp` using the structured runner (§4): `{title, nodes:[{id, parent, label, page}]}` with at most 200 nodes, depth 6, labels up to 80 characters, parents existing and acyclic, pages among the sent pages. Done when: `AssistantWs.MindMapSchemaRejectsCycles` rejects a cycle, an orphan and an unsent page.
2. Lay out the tree with a deterministic radial or left-to-right tidy-tree algorithm and render to PNG off the UI thread with DirectWrite text at a chosen size. Done when: `AssistantWs.MindMapLayoutIsDeterministic` produces byte-identical PNGs twice.
3. Export Markdown (nested list with page numbers), OPML 2.0 (XML-escaped) and PNG, each temp-file-then-replace. Done when: `AssistantWs.MindMapExports` matches golden files and `AssistantWs.MindMapOpmlEscapesXml` passes with `<` and `&` in labels.
4. Insert the PNG into a copy through the blank-page and image paths (`D05 T01 §4`, `D03 T04 §8`) saved as `<name>.mindmap.pdf`. Done when: `AssistantWs.MindMapInsertCopy` finds the new page and the source unchanged.

- [ ] Implement validation and layout. Done when: `AssistantWs.MindMapSchemaRejectsCycles` and `AssistantWs.MindMapLayoutIsDeterministic` pass.
- [ ] Implement the three exports with atomic writes. Done when: `AssistantWs.MindMapExports` and `AssistantWs.MindMapOpmlEscapesXml` pass.
- [ ] Implement the insert-into-copy path. Done when: `AssistantWs.MindMapInsertCopy` passes.
- [ ] Commit: `assistant: add mind map generation with Markdown, OPML and image output`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.MindMap*` passes, and an unescaped `&` in OPML fails `AssistantWs.MindMapOpmlEscapesXml`.

## 25. Flashcards and quizzes with Markdown and CSV exports

PDFelement and UPDF turn a document into study cards and quizzes. Recto asks for cards and questions that each cite a page and quote the supporting text, drops any whose quote is not in the sent text, and exports Markdown or CSV. The CSV is safe to open in a spreadsheet. Outputs are files the user saves; the PDF is not changed.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/study_sets.h/.cpp` with two structured tasks (§4): cards `{front, back, page, evidence}` (5 to 100) and questions `{prompt, kind: Choice|TrueFalse|Short, choices (2 to 6), answer, explanation, page, evidence}`. Done when: `AssistantWs.StudySetSchema` rejects a choice question with no valid answer index and an unsent page.
2. Verify every `evidence` quote with `verify_quote`; drop failures into an `Unverified` count shown to the user, never exported as facts. Done when: `AssistantWs.StudySetDropsUnverified` passes.
3. Export Markdown (cards as `Q:` and `A:` lines; quiz with the answer key in a separate section) and CSV (RFC 4180, UTF-8 with BOM, columns `front,back,page,source`), prefixing any cell that starts with `=`, `+`, `-` or `@` with an apostrophe. Done when: `AssistantWs.StudySetExports` matches goldens and `AssistantWs.CsvFormulaInjectionGuard` finds no cell that starts with a formula character.
4. Write each export temp-file-then-replace. Done when: `AssistantWs.StudySetAtomicExport` interrupts the write and the previous file is intact.

- [ ] Implement both tasks and validation. Done when: `AssistantWs.StudySetSchema` passes.
- [ ] Implement quote verification and the unverified count. Done when: `AssistantWs.StudySetDropsUnverified` passes.
- [ ] Implement the exports with the formula guard. Done when: `AssistantWs.StudySetExports`, `AssistantWs.CsvFormulaInjectionGuard` and `AssistantWs.StudySetAtomicExport` pass.
- [ ] Commit: `assistant: add flashcards and quizzes with verified evidence and safe CSV`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.StudySet*:AssistantWs.CsvFormula*` passes, and exporting an unverified card fails `AssistantWs.StudySetDropsUnverified`.

## 26. Contract risk review with verified quotes and the not-legal-advice notice

AI contract review flags risky clauses. It is useful and easy to over-trust, so Recto only reports a clause when its quoted text exists on the cited page, labels severity as a model estimate, lists clauses it could not find as absent only within what was sent, and attaches a fixed not-legal-advice notice to the panel and every export. It is optional, off until chosen, and uses the user's provider or a local model.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/contract_review.h/.cpp` using the structured runner (§4) over a `ContextPackage` (`D08 T01 §11`): `{clauses:[{category, severity: Low|Medium|High, quote, page, why, suggestion}], absent:[category]}` for a user-chosen subset of categories (termination, auto-renewal, liability and indemnity, payment terms, intellectual property, confidentiality, governing law and venue, non-compete, data protection, unilateral change). Done when: `AssistantWs.ContractSchema` rejects an unknown category and a severity outside the enum.
2. Keep a clause only if `verify_quote` finds `quote` on `page`; others go to an `Unverified` list not shown as findings. Done when: `AssistantWs.ContractDropsUnverifiedClauses` passes.
3. Qualify `absent` results: shown as "not found in the pages that were sent" and suppressed when the package was truncated or pages were excluded, with a notice saying why. Done when: `AssistantWs.ContractAbsentIsQualified` passes.
4. Attach the fixed text "This is an automated reading, not legal advice. Have a qualified lawyer review anything that matters." to the result, the panel header and every export, and keep model wording for severity labelled "model estimate". Done when: `AssistantWs.ContractNoticeInEveryOutput` finds the text in the result, the Markdown export and the comments of §27.

- [ ] Implement the task, categories and validation. Done when: `AssistantWs.ContractSchema` passes.
- [ ] Implement verification, absent qualification and the notice. Done when: `AssistantWs.ContractDropsUnverifiedClauses`, `AssistantWs.ContractAbsentIsQualified` and `AssistantWs.ContractNoticeInEveryOutput` pass.
- [ ] Export a Markdown report atomically with clauses, pages, quotes and the notice. Done when: `AssistantWs.ContractReportExport` matches a golden file.
- [ ] Commit: `assistant: add contract risk review with verified quotes and fixed notice`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Contract*` passes, and removing the notice from the export fails `AssistantWs.ContractNoticeInEveryOutput`.

## 27. Contract review list, page jumps and optional Apply as comments

The review result is a list the user can filter by severity and category, with each clause linking to its page, and an explicit Apply that turns chosen clauses into comments on the page. This extends the Assistant panel and the annotation engine (`D03 T03`) and never adds comments on its own.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 8.3 (side-panel card) and the comment styling of the Comments panel; no screenshot exists.
**Job:** A user runs a review, sees flagged clauses with the notice at the top, clicks a clause to jump to its page, ticks some and applies them as comments they can edit or delete. Consumer: someone reading a contract before signing.
**Treatment:** A `Contract review` view in `AssistantPanel` with the fixed notice pinned, a category picker, filters, a list of clause cards (severity, quote, why), citation-style page buttons, and `Add as comments (N)`. Comments are highlight plus sticky-note annotations with author `Recto Assistant (AI)` created as one undoable group. Cheaper substitute that fails the checkpoint: adding comments for every clause automatically.
**Chrome:** `SidePanelHost`, the annotation undo stack of `D03 T03 §4`, token brushes, Segoe Fluent Icons.

**Build order:**

1. Add the view with the pinned notice, category picker and the standard preview dialog before sending. Done when: `Panels.ContractViewRequiresPreviewAndShowsNotice` passes.
2. Add filters, clause cards and page buttons that jump to the quote's page. Done when: `Panels.ContractClauseJumpsToPage` lands on the cited page.
3. Add `Add as comments (N)` creating annotations on the verified quote's quads as one undoable group with the notice in the comment text. Done when: `Panels.ContractApplyAsCommentsUndo` applies, undoes and finds the annotations removed.

- [ ] Label every control for UIA and test keyboard use. Done when: the accessibility inventory reports zero missing names and `Panels.ContractKeyboard` passes.
- [ ] Render in Light, Dark and HighContrast with token brushes only. Done when: `Panels.ContractThemes` finds no literal colour.
- [ ] Commit: `app: add contract review view with page jumps and undoable comments`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.Contract*` passes on the display-session host, and creating comments on arrival fails `Panels.ContractApplyAsCommentsUndo`.

## 28. AI-content estimate with fixed limits text

Some tools claim to detect machine-written text. Such detectors are unreliable, produce false accusations, and are worst for non-native writers. Recto offers the feature the competitors ship but reports only a coarse band, never a percentage or per-sentence highlight, and always shows the same limits text that no model can change. It is an estimate, never proof, and nothing is written into the document.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/content_estimate.h/.cpp` using the structured runner (§4): `{band: Unlikely|Possible|Likely|InsufficientText, signals (up to 5 short plain strings)}` for a chosen page range, refusing with `InsufficientText` below 300 words before any request. Done when: `AssistantWs.EstimateRefusesShortTextLocally` asserts no request for 120 words.
2. Show the fixed text "This is an estimate by a language model. Detectors like this are unreliable: they give false positives, especially for people writing in a second language, and false negatives. Do not treat it as proof of anything." in the result and every export, as a constant not generated by the model. Done when: `AssistantWs.EstimateLimitsTextIsConstant` finds the exact constant in every output and fails if the model response contains a replacement.
3. Never return or display a numeric probability or sentence-level highlighting; ignore such fields. Done when: `AssistantWs.EstimateDropsNumericScores` passes.

- [ ] Implement the task, the local length guard and the bands. Done when: `AssistantWs.EstimateRefusesShortTextLocally` passes.
- [ ] Implement the constant limits text and field filtering. Done when: `AssistantWs.EstimateLimitsTextIsConstant` and `AssistantWs.EstimateDropsNumericScores` pass.
- [ ] Commit: `assistant: add limited AI-content estimate with constant limits text`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Estimate*` passes, and letting the model replace the limits text fails `AssistantWs.EstimateLimitsTextIsConstant`.

## 29. Image generation inserted through the undoable Edit PDF path

UPDF and PDFelement can generate an image from a prompt and place it in a document. Recto sends the user's prompt (and selected text only if the user adds it) to a provider that offers image generation, receives inline image data, validates and re-encodes it, and adds it with the Edit PDF `AddImage` command so it is undoable and saves through the normal path. It contacts no second host.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/image_generation.h/.cpp`: a request with a prompt up to 1000 characters and a size from the provider's offered list, available only when §1 reports `ImageGeneration`. Done when: `AssistantWs.ImageGenDisabledReason` shows the reason for providers without it.
2. Accept only responses carrying inline image data; a response that returns only a link is an `UnsupportedResponse` error naming the reason, and no other host is contacted. Done when: `AssistantWs.ImageGenRefusesUrlOnlyResponses` asserts the transport made one request.
3. Validate with WIC: decode, cap 16 megapixels and 20 MiB, re-encode as PNG, strip metadata. Done when: `AssistantWs.ImageGenValidatesAndReencodes` rejects a decompression bomb and a corrupt file.
4. Insert with `AddImage` (`D03 T04 §8`) at the chosen page and position through the `EditSession`, set `/Alt` to an editable description, and offer an optional visible "AI-generated" caption. Done when: `AssistantWs.ImageGenInsertIsUndoable` inserts, undoes and finds the page content identical to before.

- [ ] Implement the request, capability check and size list. Done when: `AssistantWs.ImageGenDisabledReason` passes.
- [ ] Implement response handling and validation. Done when: `AssistantWs.ImageGenRefusesUrlOnlyResponses` and `AssistantWs.ImageGenValidatesAndReencodes` pass.
- [ ] Implement the insertion, alt text and caption option. Done when: `AssistantWs.ImageGenInsertIsUndoable` passes.
- [ ] Commit: `assistant: add image generation inserted through the undoable Edit PDF path`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.ImageGen*` passes under ASan, and following a returned link fails `AssistantWs.ImageGenRefusesUrlOnlyResponses`.

## 30. Audio summaries: Windows voices or provider speech, WAV and MP3

PDFelement and UPDF read a summary aloud. Recto generates a summary as text (the user can edit it), then turns it into audio either entirely on the machine with installed Windows voices or with the chosen provider's speech API, and saves a `.wav` or `.mp3` file. The audio is never embedded in the PDF. The local path sends nothing off the machine for the speech step.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/audio_summary.h/.cpp`: step one asks for a summary of a chosen range with a target length of 1 to 5 minutes (about 150 to 750 words) through `AssistantService`; step two shows the summary text for editing before any speech step. Done when: `AssistantWs.AudioSummaryTextIsEditableBeforeSpeech` asserts the speech step uses the edited text.
2. Local speech with `Windows.Media.SpeechSynthesis` using an installed voice and rate, producing a WAV stream with no network. Done when: `AssistantWs.LocalSpeechMakesNoRequest` asserts the transport factory counter is unchanged.
3. Provider speech only when §1 reports `SpeechSynthesis`: send the summary text as a separate previewed request and accept inline audio only. Done when: `AssistantWs.ProviderSpeechNeedsPreviewAndInlineAudio` passes.
4. Save `.wav` directly and `.mp3` through the Windows transcoder when its MP3 encoder is present, otherwise offer WAV with a message naming the missing encoder; write temp-file-then-replace. Done when: `AssistantWs.AudioExportAtomicAndMissingEncoderMessage` passes.

- [ ] Implement the summary step and the edit gate. Done when: `AssistantWs.AudioSummaryTextIsEditableBeforeSpeech` passes.
- [ ] Implement local speech and the voice list. Done when: `AssistantWs.LocalSpeechMakesNoRequest` passes.
- [ ] Implement provider speech and the exports. Done when: `AssistantWs.ProviderSpeechNeedsPreviewAndInlineAudio` and `AssistantWs.AudioExportAtomicAndMissingEncoderMessage` pass.
- [ ] Commit: `assistant: add audio summaries with local voices, provider speech and WAV or MP3`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.AudioSummary*:AssistantWs.LocalSpeech*:AssistantWs.ProviderSpeech*:AssistantWs.AudioExport*` passes, and sending the unedited summary fails `AssistantWs.AudioSummaryTextIsEditableBeforeSpeech`.

## 31. AI-assisted PII suggestions as unapplied redaction marks

Regex presets (`D05 T02 §9`) miss names, addresses and free-form identifiers. PDFelement and Foxit offer smarter detection; Recto lets the model suggest spans, but the suggestions are only proposals: they become suggested marks, never pending marks and never applied redactions, until the user accepts them in the review (§32). The text sent is exactly the sensitive text the user wants to find, so the preview says so and recommends a local model.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Author `src/services/assistant/pii_suggestions.h/.cpp` using the structured runner (§4): `{spans:[{page, text, category}]}` with categories person, address, national or tax ID, phone, email, account or card number, date of birth, health detail; `text` must occur verbatim in that page's sent text. Done when: `AssistantWs.PiiSpanMustExistVerbatim` drops an invented span and counts it.
2. Map each verified span to every exact occurrence on the page as `PageText` quads and return `SuggestedMark{page, quads, category}`. Done when: `AssistantWs.PiiSpansMapToQuads` finds all occurrences of a repeated name.
3. Add `RedactionViewModel::add_suggested_marks` (`D05 T02 §9`) holding a `Suggested` state that `Apply Redactions` ignores and that the context builder does not treat as a pending mark; accepting converts a suggestion into a normal undoable pending mark. Done when: `AssistantWs.SuggestedMarksAreNeverApplied` runs `Apply Redactions` with only suggested marks and the document is unchanged.
4. Show in the preview that the request contains the very text being searched for, and mark the local provider as the recommended choice. Done when: `AssistantWs.PiiPreviewWarnsAboutSensitiveText` passes.

- [ ] Implement the task and verification. Done when: `AssistantWs.PiiSpanMustExistVerbatim` passes.
- [ ] Implement quad mapping. Done when: `AssistantWs.PiiSpansMapToQuads` passes.
- [ ] Implement the `Suggested` state and acceptance conversion. Done when: `AssistantWs.SuggestedMarksAreNeverApplied` and `AssistantWs.AcceptConvertsToUndoablePendingMark` pass.
- [ ] Commit: `assistant: add AI PII suggestions as unapplied redaction marks`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.Pii*:AssistantWs.SuggestedMarks*:AssistantWs.AcceptConverts*` passes, and applying a suggested mark fails `AssistantWs.SuggestedMarksAreNeverApplied`.

## 32. Redaction Bar: review, accept and reject AI suggestions

The user reviews AI suggestions inside the Redaction & Sanitize Bar they already use, not in a separate tool. Suggestions look different from pending marks, can be filtered by category, accepted one by one or all at once, and rejected.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface extending `design/SPEC.md` section 8.2 (`Redaction & Sanitize Bar`); no screenshot exists.
**Job:** A user runs Suggest redactions, sees dashed suggested boxes, filters by category, accepts the right ones into pending marks and applies redaction through the usual confirmation. Consumer: someone preparing a document for release.
**Treatment:** A `Suggest with AI` button in the bar opens the request preview, then draws suggestions with a dashed `RectoDangerBrush` outline and light fill distinct from pending marks, with a `Suggestions (N)` list (category chips, Accept, Reject, `Accept all in category`). Cheaper substitute that fails the checkpoint: turning suggestions straight into pending marks or applying them.
**Chrome:** `Redaction & Sanitize Bar` (`D05 T02 §9`), `RectoDangerBrush`, Segoe Fluent Icons.

**Build order:**

1. Add the button, preview step and suggested-mark overlay distinct from pending marks. Done when: `RedactionUi.AiSuggestionsOverlayDistinct` asserts a different visual state.
2. Add the list, filters, Accept, Reject and `Accept all in category`. Done when: `RedactionUi.AiSuggestionsAcceptReject` passes.
3. Keep `Apply Redactions (N)` counting pending marks only. Done when: `RedactionUi.ApplyCountExcludesSuggestions` passes.

- [ ] Label every control for UIA, announce counts and test keyboard use. Done when: the accessibility inventory reports zero missing names and `RedactionUi.AiSuggestionsKeyboard` passes.
- [ ] Commit: `app: add AI redaction suggestion review to the Redaction Bar`

**Test checkpoint:** `recto-tests.exe --gtest_filter=RedactionUi.AiSuggestions*:RedactionUi.ApplyCountExcludesSuggestions` passes on the display-session host, and counting suggestions in `Apply Redactions (N)` fails `RedactionUi.ApplyCountExcludesSuggestions`.

## 33. Study and creation tools in the Assistant panel

Mind maps, flashcards, quizzes, the AI-content estimate, image generation and audio summaries share one place in the Assistant panel: a `Tools` menu opening one view per tool, each with its options, the standard request preview, the result and its exports. This is where UPDF's and PDFelement's creative AI features live in Recto, inside the one shell (D17).

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface following `design/SPEC.md` section 8.3 (side-panel card and header action slot); no screenshot exists.
**Job:** A user picks a tool, chooses pages, previews the request, gets a result they can read, edit where applicable, export or insert, and sees why a tool is unavailable for their provider. Consumer: students, researchers and editors.
**Treatment:** The `Tools` menu from `D08 T01 §17` lists each tool; each view has a scope control, options, `Preview and send`, the result, and export buttons (`Markdown`, `OPML`, `CSV`, `PNG`, `WAV`, `MP3`, `Insert into document`). Unavailable tools are shown disabled with the §1 reason. Cheaper substitute that fails the checkpoint: separate floating windows per tool or tools that send without the preview dialog.
**Chrome:** `SidePanelHost`, preview dialog, file save pickers, token brushes, Segoe Fluent Icons.

**Build order:**

1. Add the tool views to `AssistantPanel` through the extension point, one view per tool, each using the standard preview dialog. Done when: `Panels.AssistantToolsMount` finds all six views in all three layout presets.
2. Wire each view to its engine (§24, §25, §28, §29, §30), show verified, dropped and unverified counts, and the fixed limits text for the estimate. Done when: `Panels.AssistantToolsShowCounts` passes.
3. Wire the exports and `Insert into document`, each through the save pickers or the Edit PDF path. Done when: `Panels.AssistantToolsExports` writes each format atomically.

- [ ] Show disabled tools with the reason. Done when: `Panels.AssistantToolsDisabledReason` shows the reason for each missing capability.
- [ ] Label every control for UIA, announce completion and test keyboard use. Done when: the accessibility inventory reports zero missing names and `Panels.AssistantToolsKeyboard` passes.
- [ ] Render in Light, Dark and HighContrast with token brushes only. Done when: `Panels.AssistantToolsThemes` finds no literal colour.
- [ ] Commit: `app: add study and creation tools to the Assistant panel`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.AssistantTools*` passes on the display-session host, and a tool that sends without the preview dialog fails `Panels.AssistantToolsShowCounts`.

## 34. Ctrl+K commands, UIA names and announcements for the workspace tools

Every workspace feature must be reachable from the command box and usable with a screen reader, and a feature a provider cannot serve must still be findable and explain itself. This registers the commands, keeps polite announcements, and extends the accessibility inventory.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Register in `CommandRegistry` (`D02 T03 §11`): `assistant.translateDocument`, `assistant.rewriteSelection`, `assistant.proofreadSelection`, `assistant.chatAcrossFiles`, `assistant.searchByMeaning`, `assistant.buildMeaningIndex`, `assistant.suggestBookmarks`, `assistant.suggestPageFixes`, `assistant.mindMap`, `assistant.flashcards`, `assistant.quiz`, `assistant.contractReview`, `assistant.aiContentCheck`, `assistant.generateImage`, `assistant.audioSummary`, `assistant.suggestRedactions`, with synonyms ("translate", "rewrite", "proofread", "tone", "shorten", "expand", "semantic", "study", "flashcard", "risk", "bilingual", "AI"). Done when: `Commands.AssistantWorkspaceReachableFromCtrlK` finds each command and synonym.
2. When the assistant is off, has no key or endpoint, or lacks a capability, the command opens the Settings `ASSISTANT` group or shows the §1 reason, without any network call. Done when: `Commands.AssistantWorkspaceOffRoutesWithoutNetwork` passes with the transport factory counter unchanged.
3. Announce through `AnnouncementService` (`D03 T02 §5`): job start, one progress milestone per 25 percent, completion with counts, failure with next step, never streamed text. Done when: `A11y.AssistantWorkspaceAnnouncementsCoalesced` counts the expected announcements for a translation job.
4. Add every new control to the accessibility inventory. Done when: the inventory test reports zero missing names across §7, §10, §12, §17, §19, §21, §23, §27, §32 and §33.

- [ ] Register the commands with synonyms. Done when: `Commands.AssistantWorkspaceReachableFromCtrlK` passes.
- [ ] Implement off-state routing. Done when: `Commands.AssistantWorkspaceOffRoutesWithoutNetwork` passes.
- [ ] Implement announcements. Done when: `A11y.AssistantWorkspaceAnnouncementsCoalesced` passes.
- [ ] Commit: `app: reach every Assistant workspace tool from Ctrl+K and announce progress politely`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Commands.AssistantWorkspace*:A11y.AssistantWorkspace*` passes on the display-session host, and removing one registration fails `Commands.AssistantWorkspaceReachableFromCtrlK`.

## 35. Workspace privacy suite: loopback only, previews, no auto-apply, zero network

This suite proves the widened D19 promises for every feature in this file so a later change cannot weaken them. It extends `D08 T01 §19` and must fail when any promise breaks. It uses fake transports and the loopback test server only, never a live provider.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Add `tests/recto-tests/assistant/workspace_privacy_suite.cpp` with a table of every feature entry point (one row per command in §34). For each row with no key and no endpoint, assert the transport factory counter is unchanged after invoking it. Done when: `AssistantWs.NoKeyZeroNetworkEveryFeature` passes and fails if any row constructs a transport.
2. Assert for each row that sending requires an `Authorization` or a `BatchAuthorization` grant and that the preview dialog or job preview is shown first, using a recording fake gate. Done when: `AssistantWs.EveryFeatureNeedsPreviewAndGrant` passes.
3. Assert no feature changes the document without an explicit Apply: run each feature to completion and compare the source file bytes and the open document revision. Done when: `AssistantWs.NoFeatureAutoApplies` passes.
4. Assert redaction, protected-content and unselected-page exclusions in the recorded request bodies of every text-sending feature, including images and history (§22, §11). Done when: `AssistantWs.ExclusionsEveryFeature` passes.
5. Assert the local provider stays on loopback: a non-loopback listener sees zero connections, no proxy is used, and the cloud providers still refuse `http://`. Done when: `AssistantWs.LocalProviderLoopbackOnly` passes.
6. Assert the key never leaks through the new sinks (semantic index, exports, reports, job ledger, translation copies) by re-running the sentinel scan of `D08 T01 §19`. Done when: `AssistantWs.KeyNeverLeaksNewSinks` passes.
7. Assert every long job cancels within 500 ms and `Group Policy` `AssistantDisabled` (D21, read through `SettingsStore`) forces gate state `Disabled` with zero network. Done when: `AssistantWs.CancelEveryJob` and `AssistantWs.PolicyDisablesEverything` pass using an injected policy source.

- [ ] Implement the feature table and the no-network, grant and preview assertions. Done when: `AssistantWs.NoKeyZeroNetworkEveryFeature` and `AssistantWs.EveryFeatureNeedsPreviewAndGrant` pass.
- [ ] Implement the no-auto-apply and exclusions assertions. Done when: `AssistantWs.NoFeatureAutoApplies` and `AssistantWs.ExclusionsEveryFeature` pass.
- [ ] Implement the loopback, key-leak, cancel and policy assertions. Done when: `AssistantWs.LocalProviderLoopbackOnly`, `AssistantWs.KeyNeverLeaksNewSinks`, `AssistantWs.CancelEveryJob` and `AssistantWs.PolicyDisablesEverything` pass.
- [ ] Run the mutation checks: skip one preview, auto-apply one result, allow `127.0.0.2`, embed a cached chunk once more. Done when: each mutation fails exactly its named test and the source is restored byte for byte.
- [ ] Update `docs/assistant.md` with the workspace features, what each sends, the local provider rule and the capability table. Done when: `AssistantWs.DocsListEveryFeature` finds every command's feature named in the file.
- [ ] Run `AssistantWs.*` under AddressSanitizer. Done when: `recto-tests.exe --gtest_filter=AssistantWs.*` passes in the `asan` configuration (`D00 T03 §6`).
- [ ] Commit: `tests: prove assistant workspace privacy, previews, loopback limits and no auto-apply`

**Test checkpoint:** `recto-tests.exe --gtest_filter=AssistantWs.NoKeyZeroNetworkEveryFeature:AssistantWs.EveryFeatureNeedsPreviewAndGrant:AssistantWs.NoFeatureAutoApplies:AssistantWs.ExclusionsEveryFeature:AssistantWs.LocalProviderLoopbackOnly:AssistantWs.KeyNeverLeaksNewSinks:AssistantWs.CancelEveryJob:AssistantWs.PolicyDisablesEverything` exits 0, and each mutation in the mutation item fails its named test.

## Verification

- With no key and no endpoint, no workspace feature makes a network call (`AssistantWs.NoKeyZeroNetworkEveryFeature`).
- Every request and every batch of a job is previewed and authorized, and a request that differs from its previewed bytes is refused (`AssistantWs.EveryFeatureNeedsPreviewAndGrant`, `AssistantWs.BatchGrantIsSingleUsePerDigest`).
- A translated copy keeps the original untouched and reports what it could not translate or fit (`AssistantWs.TranslateNeverTouchesOriginal`, `AssistantWs.TranslateFitPolicyAndReport`).
- Text suggestions apply as one undo step through the Edit PDF path, and generated results never apply on their own (`AssistantWs.ApplyIsOneUndoStep`, `AssistantWs.NoFeatureAutoApplies`).
- The local provider reaches only loopback, with no proxy and no key (`AssistantWs.LocalProviderLoopbackOnly`).
- Unverifiable quotes, clauses, cards and PII spans are dropped and counted, and contract and AI-content outputs carry their fixed notices (`AssistantWs.QuoteVerification`, `AssistantWs.ContractNoticeInEveryOutput`, `AssistantWs.EstimateLimitsTextIsConstant`).
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
