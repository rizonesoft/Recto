## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T02:09:54Z
- Effort: medium
- Lines: 31
- Digest: 63bc95faee355459

Review is limited to the supplied candidate diff. Repository reads failed because the command runner could not start, so referenced documents and interfaces remain unverified. No files were modified and no Git commands ran.

Below, **T01** means `todo/08-assistant/TODO-01-ai-assistant.md`; **T02** means `todo/08-assistant/TODO-02-assistant-workspace.md`.

**source-defect: needs-attention**

1. **T01 §2, build step 1: sanitization destroys the JSON consumed by provider error classifiers.** Unescaping strings and returning the decoded body with “re-encodes nothing” can turn `\"` into a structural quote or `\n` into an illegal literal newline. Ordinary provider errors can consequently become unparseable, losing their typed classification. Preserve valid JSON through structural sanitization, or explicitly change the classifier interface to accept a sanitized parsed representation. Test escaped quotes and newlines in otherwise valid error objects. [RFC 8259, §7](https://www.rfc-editor.org/rfc/rfc8259.html#section-7)

**consistency: needs-attention**

2. **T01 §2 introductory contract versus T02 §2: the transport still cannot be constructed without a key.** The new local provider explicitly has no key, but the retained transport contract forbids construction when no key is configured. `NoEndpoint` alone does not resolve this contradiction. Define provider-specific readiness and credential requirements through the gate, factory and send interface, and test a successful keyless local request.

3. **T02 §1, build step 3: the “per-model” override is actually provider-wide.** `assistant.capabilityOverride.<provider>.<capability>` contains no model identity. Enabling a capability for one model therefore enables it after switching to an unsupported model, contradicting the disabled-with-reason contract. Include model identity in the stored override and test model switching.

4. **T02 §15, build step 2 versus §35, build step 7: cancellation only at batch boundaries cannot guarantee 500 ms.** An in-flight embedding request can remain blocked for the transport’s much longer timeout. Require cancellation propagation into the active request and any checkpoint write, and test cancellation during a stalled response, not merely between batches.

**integration: needs-attention**

5. **T02 §§14 and 16: embedding cache identity is insufficient.** The vector cache uses `(model, chunk SHA-256)` and the query cache uses query text plus model. Different providers or local endpoints can expose the same model ID while producing incompatible vectors, even with equal dimensions. Changing endpoints can therefore silently reuse the wrong vectors and rankings. Define one embedding-space identity covering provider, endpoint, model and relevant embedding configuration; use it consistently for caches, index selection and mismatch checks.

6. **T02 §29 and T01 §1: `UnsupportedResponse` has no defined error-taxonomy entry.** Image generation requires this error, but the candidate’s explicit `AssistantErrorCode` list excludes it and no extension supplies its description or retry policy. Add the error throughout the taxonomy or specify an existing code. Otherwise an executor must invent the missing contract.

**adversarial: needs-attention**

7. **T02 §5, build step 3: the word-level LCS introduces unbounded computational cost.** The structured runner permits responses up to 4 MiB, but the diff operation has no token-product limit, memory budget or cancellation requirement. A large valid suggestion can exhaust memory or stall processing despite passing JSON bounds. Bound both diff inputs and computational work, and test a worst-case disjoint-word response.

8. **T02 §§20, 22, 27 and 31: proposals are not bound to a document revision through Apply.** Unlike §6’s stale-text check, these paths validate pages or quotes when generating proposals but specify no revalidation after edits or page reordering. A delayed page-deletion proposal or accepted redaction suggestion can target different content. Bind results to document identity and revision, then reject or revalidate stale proposals before mutation. Test changes between response arrival and acceptance.

9. **T02 §35, build step 1: the feature table omits a new network entry point.** It tests only commands listed in §34, which excludes §2’s local `Test connection` and model-list request. That route can bypass disabled-state or policy handling while `NoKeyZeroNetworkEveryFeature` and the command-based policy checks pass. Include settings connection tests and model discovery in the network-entry inventory, with explicit authorization and `AssistantDisabled` assertions.

OVERALL: needs-attention

## Response to round 1

All 9 findings were verified and fixed in the next commit:

1. D08 T01 §2: JSON error bodies are parsed, each string value decoded and scrubbed, and re-serialized, so the output stays valid JSON; `Assistant.ScrubberPreservesValidJson`.
2. D08 T01 §2, §12, D08 T02 §2: one `provider_ready(provider)` gate (a key for cloud providers, a loopback endpoint for the local one); a keyless `SecretSource`; `AssistantWs.LocalKeylessRequestSucceeds`.
3. D08 T02 §1: capability overrides are stored per provider, model and capability; `AssistantWs.OverrideDoesNotFollowModelSwitch`.
4. D08 T02 §15: cancel reaches in-flight requests and checkpoint writes (temp file abandoned on cancel); `AssistantWs.SemanticJobCancelDuringStalledResponse`.
5. D08 T02 §13, §14, §16: an `EmbeddingSpaceId` (provider, endpoint, model, dimension, options) keys every cache and index set and is stored in the index header.
6. D08 T01 §1, D08 T02 §29: `UnsupportedResponse` joins the error list as non-retryable.
7. D08 T02 §5: a bounded Myers diff (20000 words, edit distance 2000, 32 MB, cancel polling) with a whole-replacement fallback; `AssistantWs.DiffWorstCaseIsBounded`.
8. D08 T02 §4, §20, §22, §27, §31: results carry a `RevisionToken` and every Apply refuses `Stale` before changing anything; one test per consumer.
9. D08 T02 §2, §35: `Test connection` and local model discovery run only on an explicit click and are disabled when the assistant is off or `AssistantDisabled` is set; the zero-network table covers them.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T05:06:56Z
- Effort: medium
- Lines: 23
- Digest: 012fe480df539eac

Review is limited to the supplied diff. Repository reads failed twice because the command runner could not start (`helper_unknown_error: setup refresh had errors`). Governing-document consistency beyond the supplied text remains unverified. No files were modified and no Git commands ran.

T01 and T02 refer to the two candidate files under `todo/08-assistant/`.

**record: needs-attention**

1. **Earlier finding 8 remains incompletely resolved: T02 §4 build step 5, §22 acceptance path.** `revalidate` can return `RebasedByHash`, but its specified return contract supplies no corrected page targets. Consumers only have to refuse `Stale`; §22 can therefore accept a reordered document and convert the proposal’s original page numbers into an edit list. The new check does not require the conversion to use the matched pages. Require either unconditional rejection on revision changes or an explicit, uniquely resolved target mapping consumed by the mutation. Test which page actually changes after reordering, including duplicate-content pages.

No other earlier finding is demonstrably unresolved within the supplied diff. This is a review of planned contracts, not evidence that their named tests exist or pass.

**consistency: needs-attention**

2. **T02 §4 versus §§20 and 31: reorder handling has contradictory acceptance criteria.** The shared contract explicitly allows `RebasedByHash` when page indices move and referenced content hashes match. However, `BookmarkApplyRejectsStale` requires refusal after a reorder, and §31 explicitly requires reordered-page suggestions to be refused. An implementation following the shared contract can fail the consumer checkpoints; one following the checkpoints disables the advertised rebasing. Specify whether each consumer permits rebasing, and make the shared contract and tests enforce that choice.

**source-defect: advisory**

The unavailable repository context prevents verification against the full provider interfaces and governing documents. No separate standard or API defect is established from the supplied changes alone.

**adversarial: needs-attention**

3. **T01 §2 build step 1: structural sanitization omits JSON member names.** The replacement algorithm walks and scrubs string **values**, then serializes the tree. A valid error body such as `{"<exact credential>":"failure"}` preserves the credential in its member name and returns it in `RawErrorResponse`. This violates the requirement that everything above the transport receive already-scrubbed content. The decoded-form check is described within the value-scrubbing path and does not explicitly cover member names or the complete serialized result. Scrub both names and values, or discard bodies containing a credential in a member name. Add raw, Unicode-escaped and percent-encoded member-name fixtures using a credential without a recognized prefix, and inspect `RawErrorResponse` itself.

OVERALL: needs-attention

## Response to round 2

1. Fixed: D08 T02 §4 step 5 replaces `RebasedByHash` with `Rebased{PageMap}`, produced only when every referenced page's hash matches exactly one current page (duplicate content, a gone or edited page, or another document gives `Stale`), and a consumer that accepts it must take every page target from the `PageMap`; §22 (`RequireFresh`) now tests a deletion, a reorder and a swap of identical-content pages with no edit list and an unchanged copy, §27 adds `Panels.ContractApplyFollowsReorder` (comment lands on the moved page's new index; a clause on a page with an identical twin is skipped), and §4 adds `AssistantWs.RevalidateDuplicatePagesAreStale`.
2. Fixed: D08 T02 §4 step 5 adds a `RebasePolicy` argument fixed per consumer (§20 bookmarks, §22 page proposals and §31 PII suggestions use `RequireFresh`, §27 contract comments uses `AllowRebase`), `AssistantWs.RevisionTokenDetectsEditsAndReorder` covers both policies, and the §20, §22, §27 and §31 revalidation items name their policy so the reorder-refusal tests match the shared contract.
3. Fixed: D08 T01 §2 step 1 decodes every member name as well as every value, discards the body when a member name holds the key, drops a response header whose name holds the key, and decodes the complete re-serialized output as a last check before returning; the scrubber item adds `Assistant.TransportScrubsKeyInMemberNames` (raw, `\u`-escaped and percent-encoded member names with a prefix-free key, plus a header name, inspecting `RawErrorResponse` itself) and the Test checkpoint names it as the falsifier and now includes `Assistant.Scrubber*`.
