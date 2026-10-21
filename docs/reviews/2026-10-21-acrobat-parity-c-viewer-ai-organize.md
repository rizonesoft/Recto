## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T00:32:00Z
- Effort: medium
- Lines: 57
- Digest: 179d24fd4df03c01

Review scope: the supplied diff and cited primary sources. Repository reads failed because the shell could not start, so I could not verify additional local contracts. No files were modified and no Git commands ran. Reproduction cases below are proposed checks, not executed tests.

## source-defect: needs-attention

1. **Certificate decryption cannot handle every recipient type the writer permits.** `todo/05-organize/TODO-01-page-operations.md` §11 permits ECDH, but §12 prescribes `CMSG_CTRL_DECRYPT` universally. Microsoft documents that control as applicable to RSA key-transport recipients; key agreement has a separate control and parameter structure. **Reproduce:** encrypt for a supported ECDH certificate, then open through the prescribed decryptor. Specify recipient-type dispatch or consistently refuse ECDH. [Microsoft CryptMsgControl documentation](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptmsgcontrol)

2. **The certificate “fail closed” check does not prove successful decryption.** The same file, §12, requires the catalog and first page-tree node to “decrypt and parse.” PDF encryption applies to strings and streams, not the surrounding dictionary structure. An ordinary catalog and page-tree node can parse with the wrong file key. **Reproduce:** preserve those dictionaries but corrupt the recovered seed or encrypted page-content stream. The stated success gate can pass before content decryption fails. Require validation of actual encrypted content, and avoid presenting AES-CBC parsing checks as an integrity guarantee. [ISO 32000-2 encryption clarification](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html)

3. **U3D base geometry is not necessarily full-resolution geometry.** `todo/02-viewer/TODO-05-rich-media-3d-geo.md` §6 asks for full-resolution geometry while explicitly skipping progressive updates. ECMA-363 defines those updates as adding positions between minimum and final resolution. **Reproduce:** use a CLOD mesh whose minimum resolution is substantially below its final resolution. The prescribed parser loses geometry while its minimal-box checkpoint passes. [ECMA-363, §9.6.1.3](https://www.ecma-international.org/wp-content/uploads/ECMA-363_4th_edition_june_2007.pdf)

4. **The geospatial algorithm cannot satisfy its stated accuracy contract.** The same file, §11, uses a least-squares affine fit for more than four control points but requires every control point to round-trip within `1e-9` degrees. An overdetermined, non-affine control set generally has nonzero residuals. **Reproduce:** provide five valid, non-collinear control pairs with the fifth displaced from the affine fit. Define acceptable residuals and refusal behavior instead of requiring exact interpolation from a least-squares approximation.

## consistency: needs-attention

5. **Persistent consent suppression contradicts D19.** `todo/08-assistant/TODO-01-ai-assistant.md` §§12 and 16 introduce “Don’t ask again,” including persistence across sessions. Foundation D19 requires confirmation and disclosure for each request; the parity ledger also promises preview before every request. **Reproduce:** authorize one request with suppression enabled, then submit another matching request. The plan explicitly permits sending without another preview. Remove suppression or change the governing decision explicitly.

6. **The open-tabs search contract is internally incompatible.** `todo/03-text/TODO-05-multi-file-search.md` §§2 and 3 require searching unsaved live documents and already-unlocked encrypted tabs, while also requiring each search document to be independently opened and never sharing the viewer document. No snapshot/export or actor-message contract reconciles these requirements. **Reproduce:** create an unsaved document, or search an unlocked encrypted tab without retaining its password. A path-based worker cannot obtain the promised content. Define immutable snapshot acquisition through the owning actor.

7. **The exact-accent acceptance test silently changes case matching.** The same file, §3, expects query `café` with exact accents enabled to match only lowercase `café`, excluding `Café`. Its stated options treat case matching and accent matching independently. **Reproduce:** search the supplied two-word fixture with `match_diacritics=true` and `match_case=false`. Both words should match. Correct the checkpoint rather than changing the engine.

## integration: needs-attention

8. **Retry classification has no defined route through the interfaces.** `todo/08-assistant/TODO-01-ai-assistant.md` §§1 to 3 place provider-specific error classification in `IAssistantProvider`, but `RetryingTransport` wraps only `ITransport`, which streams body chunks and returns `ResponseMeta`. It must retry classified errors while refusing retries after any body bytes reach the caller. **Reproduce:** return a normal 429 JSON error body. Forwarding it prevents retry; buffering and classifying it requires an unspecified provider-aware boundary. Define status/header delivery, bounded error-body buffering and classifier ownership.

9. **The successful transport-streaming test has no usable production path.** The same file, §§2 and 4, requires HTTPS-only transport but supplies plain HTTP loopback for the streaming and cancellation tests. The only TLS server described deliberately has an untrusted certificate and must fail before HTTP. **Reproduce:** point `WinHttpTransport` at the prescribed HTTP helper. It returns `InsecureUrl`; replacing it with `FakeTransport` does not test WinHTTP streaming. Supply a trusted test TLS endpoint without weakening production validation.

10. **Alt-text conversion can stamp with a disconnected stub.** The same file, §14, explicitly allows `AltOverrideSurvivesUntilConversion` to pass against a stub tagger and leaves the real `convert_to_pdfua` parameter as a “cross-file request in the author report.” **Reproduce:** implement the tab-session override and stub test, but leave the real converter unchanged. Every stated override checkpoint can pass while conversion discards the approved text. Own the real interface change and require saved-output `/Alt` readback.

## adversarial: needs-attention

11. **HTTPS redirects can disclose provider keys to another origin.** `todo/08-assistant/TODO-01-ai-assistant.md` §2 allows any HTTPS-to-HTTPS redirect. Scheme validation does not bind the destination to the authorized provider. Microsoft explicitly warns that added headers transfer across redirects. **Reproduce:** have the configured endpoint return a redirect to another HTTPS host and inspect its received `x-api-key` or `x-goog-api-key`. Reject cross-origin redirects before forwarding credentials or request content. [Microsoft WinHttpAddRequestHeaders documentation](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpaddrequestheaders)

12. **Authorization covers only part of what is transmitted.** The same file, §§11 to 13 and 16, hashes the `ContextPackage`, provider and model, but `ask(question, history)` adds conversation history outside that package. The configurable OpenRouter endpoint is also absent from the authorization identity and preview. **Reproduce:** authorize page 1 while history contains previously included protected page 2, or change the OpenRouter URL after consent. The described hash remains valid although the outgoing data or recipient changes. Bind authorization to the complete immutable outbound payload and destination.

13. **Pending redaction filtering does not cover figure pixels.** The same file, §§11 and 14, excludes text by intersecting character quads, then renders figure bounding boxes into PNGs. No rule excludes or securely removes pending-redaction regions from those pixels or their nearby captions. **Reproduce:** mark a scanned account number inside a figure for redaction, then request alt text. The text exclusion tests pass while the image still contains the number. Define exclusion for every image and auxiliary text source, with an end-to-end request-body test.

14. **The credential lifetime guarantee is not enforced by the proposed API.** The same file, §§1, 4 and 9, claims callback-scoped access prevents a `std::string` copy, yet adapters return `HttpRequest` objects containing authentication headers, and the fake transport records them. A callback receiving readable key bytes can copy them. **Reproduce:** copy the callback argument into an outer string or inspect a retained request after the callback returns. Zeroing the original buffer leaves those copies intact. Define secret ownership and cleanup through request construction, retries, transport and test recording; replace the impossible compile-fail guarantee.

15. **Indexed search can silently omit valid matches.** `todo/03-text/TODO-05-multi-file-search.md` §§4 and 5 use folded term postings to narrow candidates, then scan only those candidates, including for regex fallback. No completeness-preserving candidate algorithm is specified for substring or arbitrary regex searches. **Reproduce:** index `invoice`, then search `voice` without whole-word matching, or use `inv.*ce`. A term lookup can eliminate the page before confirmation. Unsupported query forms must scan the entire eligible scope.

16. **Index freshness checks miss replacements and changes during reads.** The same file, §§4 and 5, trusts size and modification time, and checks them only immediately before reading. **Reproduce:** replace a PDF with different content of equal length and restore its timestamp, or modify it after the pre-read check. Stale postings can omit newly introduced matches or be published for inconsistent content. Define stable file identity/read semantics, post-read validation and conservative invalidation.

17. **The no-link traversal promise is vulnerable to a filesystem race.** The same file, §§2 and 3, checks reparse attributes during enumeration but later opens paths independently. **Reproduce:** after enumeration, replace an ordinary subdirectory with a junction to an outside folder before its PDF is opened. The static-junction test still passes, but the later open escapes the selected scope. Require handle-based identity and containment validation at the actual open boundary.

18. **Background listing loses previously added marks.** `todo/05-organize/TODO-01-page-operations.md` §13 inserts each background as a new first content stream but tells `list_marks` to read only the first stream. **Reproduce:** add two backgrounds to the same page, save and reopen. Only the newest wrapper is examined, so the older mark cannot reliably be listed, updated or removed. Enumerate all managed streams and preserve unrelated resource uses during removal.

19. **Page replacement does not define a valid tagged-PDF merge.** The same file, §14, prunes destination elements belonging solely to removed pages, but does not specify importing source structure trees, allocating distinct `/StructParents` keys, rebuilding source parent-tree mappings or pruning removed-page content references inside mixed-page elements. **Reproduce:** replace one page using a tagged source whose `/StructParents` value collides with a retained destination page. The page count and `/Pg` scan can pass while the accessibility tree points to the wrong content. Require explicit structure-tree remapping and semantic validation.

20. **Hostile-input memory limits are not aggregate budgets.** `todo/02-viewer/TODO-05-rich-media-3d-geo.md` §§2, 6 and 7 permit up to 1,000 one-megabyte names per page and millions of mesh vertices and triangles, while promising application/parser caps far below the possible combined allocations. No shared allocation budget is defined. **Reproduce:** construct many individually valid names, meshes or nodes whose combined decoded size exceeds the cap. Every individual count check can pass. Charge decoded storage, intermediate buffers and retained models against one enforced budget.

## record: needs-attention

21. **The ledger presents deliberately incomplete 3D support as unqualified parity.** `docs/acrobat-parity.md`, “3D models,” says Planned, but `todo/02-viewer/TODO-05-rich-media-3d-geo.md` §§6 and 7 explicitly omit progressive U3D updates, compressed PRC tessellation and geometry generation for B-rep-only parts. **Reproduce:** complete every listed section and open those supported-by-format cases. Recto still displays incomplete geometry or a fallback. Record Partial with the limitations and remaining owners, or add implementation scope.

22. **The index’s recorded privacy and performance claims contradict its implementation.** `todo/03-text/TODO-05-multi-file-search.md`, Outcome and Adjacency, claims repeat searches avoid reopening unchanged files and says only paths, metadata and text offsets are indexed. Sections 4 and 5 actually persist folded document terms and reopen candidate pages to confirm hits. **Reproduce:** inspect an index for distinctive unencrypted document words and trace file opens during a second unchanged search. Both advertised claims fail. State what sensitive content is persisted and what work the index actually avoids.

OVERALL: needs-attention

## Response to round 1

Every finding was checked against the commit; all 22 were accepted and fixed in the next commit, in the section that owns each:

1. D05 T01 §11, §12: certificate encryption writes RSA key-transport recipients only and refuses other recipient types by name.
2. D05 T01 §12: the fail-closed check decrypts and verifies real encrypted streams and strings.
3. D02 T05 §6: U3D applies resolution updates to final resolution, or shows reduced detail with a notice.
4. D02 T05 §11: exact round-trip only for 3 and 4 control points; least-squares reports its residual and refuses inconsistent points.
5. D08 T01 §12, §16: "Don't ask again" is removed; every request shows its preview and needs a single-use authorization.
6. D03 T05 §2, §3: open tabs are searched through the owning actor's `PageText` snapshots.
7. D03 T05 §3: the exact-accent test no longer changes case matching.
8. D08 T01 §2, §3: retries classify the buffered error body through the provider.
9. D08 T01 §2, §4: TLS tests use a loopback HTTPS server with a constrained, removed test root.
10. D08 T01 §14: the tagger gains `FigureAltOverrides`, tested on saved output.
11. D08 T01 §2: every redirect is refused, so a key can never reach another origin.
12. D08 T01 §12, §13, §16: authorization binds the complete outbound request, history and destination included.
13. D08 T01 §11, §14: pending redaction marks are painted out of figure images before encoding.
14. D08 T01 §1, §2, §9: keys are read only while adding the header and zeroed after; a scan limits `with_key` callers.
15. D03 T05 §4, §5: the index uses trigrams and falls back to a full scan when it cannot answer completely.
16. D03 T05 §4, §5: freshness compares file id, size, write time and change time, rechecked on the same handle.
17. D03 T05 §2, §3: files open only through `open_checked`, which refuses reparse points and verifies the final path.
18. D05 T01 §13: every content stream is enumerated for backgrounds, each with a unique resource name.
19. D05 T01 §14: imported pages arrive untagged and the destination structure tree is rebuilt consistently.
20. D02 T05 §2, §5-§7: one aggregate parse budget for media and 3D.
21. `docs/acrobat-parity.md`: 3D is Partial, listing the PRC and U3D limits.
22. D03 T05 Outcome and §9: the index's privacy and performance claims match what it does.

These fixes have not had a second independent round yet.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T00:57:57Z
- Effort: medium
- Lines: 23
- Digest: 5a6f4842708bf299

Review is limited to the supplied diff: repository reads failed twice with `helper_unknown_error: setup refresh had errors`. No files were modified, Git commands run, or tests executed.

**record: needs-attention**

1. **Round 1 #13 remains incomplete: auxiliary text cannot reliably use the promised quad filter.** `todo/08-assistant/TODO-01-ai-assistant.md` §11 step 4 requires existing `/Alt` and conversation history to be filtered by character quads, but neither has a defined mapping to those quads. Section 13 explicitly retains earlier answers, which can quote subsequently redacted content. Reproduce by obtaining an answer containing an account number, marking that number for redaction, then asking a follow-up. Image masking passes while the earlier answer can resend the number. Define conservative exclusion for text without reliable provenance, and test the complete follow-up request body.

2. **Round 1 #14 remains incomplete: key sanitization conflicts with the new access restriction.** `todo/08-assistant/TODO-01-ai-assistant.md` §§1, 3 and 9 require provider error sanitization to receive the configured key, but permit `with_key` only in the transport’s header-add scope and the fake transport’s presentation check. `RetryingSender` subsequently passes the buffered error body to the provider without a key or a defined sanitization service. An endpoint echoing an arbitrary key without a recognized prefix exposes this gap. Specify an authorized sanitization boundary and test such an echoed key through the production response path.

3. **Round 1 #16 remains incomplete: freshness is specified for index building, not for excluding files during a search.** `todo/03-text/TODO-05-multi-file-search.md` §§4-5 define `plan_update`, post-read metadata validation and manual digest verification, but do not require `run_indexed` to validate every eligible entry before trusting negative postings. The remaining instruction even drops a *replaced* file from candidates rather than searching its replacement. Reproduce by indexing a file without `invoice`, replacing it with one containing `invoice`, then searching before another index build. Require changed, new and unverifiable files to enter a direct scan or synchronous refresh before candidate exclusion. Manual `Verify index` cannot establish ordinary search completeness.

4. **Round 1 #19 remains incomplete: stripping page keys does not strip all imported structure associations.** `todo/05-organize/TODO-01-page-operations.md` §14 removes imported page `/StructParents`, but imports widgets and other page content without specifying removal of annotation `/StructParent` or Form XObject `/StructParents`. Those keys can still collide with retained destination entries. Rebuilding the parent tree “for retained pages only” also leaves retained annotation and XObject mappings unspecified. Parent-tree keys belong to individual objects and content streams as well as pages. Require complete removal of imported structural associations and preservation of every retained association, tested with tagged widgets and Form XObjects. [PDF Association structure reference](https://pdfa.org/download-area/cheat-sheets/LogicalStructureObjects.pdf)

**consistency: needs-attention**

5. **The preview implementation still follows the superseded authorization contract.** `todo/08-assistant/TODO-01-ai-assistant.md` §12 hashes the complete `HttpRequest` inside `OutboundPreview`, while §16 step 1 says the dialog binds to a `ContextPackage` that is “the same object the gate hashes.” Its acceptance test compares only page text. Consequently, the implementation checkpoint can pass without proving that displayed history, destination and images match the authorized request. Bind the dialog to the complete immutable preview and require end-to-end equality after page selection and protected-content changes.

6. **Malformed U3D updates are now explicitly returned as successful models.** `todo/02-viewer/TODO-05-rich-media-3d-geo.md` §6 requires `U3D.ClodBadUpdate` to return an intermediate mesh with `reduced_detail`, grouping malformed input with unsupported features. This contradicts the supplied `AGENTS.md` failure mode prohibiting malformed-input failures reported as success. Return a typed malformed-input error; if partial display is intended, define an explicit recovery result that preserves the failure and distinguishes it from valid but unsupported content.

**source-defect: needs-attention**

7. **The revised certificate validation requires content that a valid PDF need not contain.** `todo/05-organize/TODO-01-page-operations.md` §12 requires decrypting the first encrypted stream of the first page’s `/Contents`. A blank first page may have no `/Contents`; encrypted content may exist only on later pages, or there may be no suitable encrypted sample. The plan defines neither case, so following it literally can reject correctly encrypted documents. Define bounded sample selection across eligible encrypted objects and explicit behavior when no sample exists. Add blank-first-page and empty-document fixtures. Also replace “catch a wrong or corrupted key” with probabilistic sanity-check language consistent with the paragraph’s acknowledgment that CBC provides no authentication.

OVERALL: needs-attention

## Response to round 2

All 7 findings were accepted and fixed in the next commit:

1. D08 T01 §11, §13: text without quads follows a fail-closed provenance rule; answers derived from later-marked pages are dropped from follow-ups.
2. D08 T01 §1, §2, §9: the transport scrubs the exact key from error bodies inside its key scope; nothing above it needs the key.
3. D03 T05 §5: freshness is checked at search time; only `Unchanged` files may be excluded by the index.
4. D05 T01 §14: every imported structure association (pages, annotations, widgets, XObjects) is stripped, and the parent tree keeps retained owners only.
5. D08 T01 §12, §16: the dialog binds the exact `OutboundPreview` the gate digests and authorizes.
6. D02 T05 §6: a malformed U3D update is a typed error with poster fallback; only a valid unsupported update yields reduced detail.
7. D05 T01 §12: the key check samples up to 8 encrypted objects of any kind and reports `key_unverified` when none exist.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T01:08:13Z
- Effort: medium
- Lines: 16
- Digest: 10d7d0f41aae84db

Repository reads failed twice with `helper_unknown_error: setup refresh had errors`. This review therefore covers the supplied diff, not the wider repository. No files were modified, Git commands run, or tests executed.

1. **Exact-key scrubbing can still leak the key after JSON decoding.** `todo/08-assistant/TODO-01-ai-assistant.md` §2, build step 1. Replacing the key and “its JSON-escaped form” does not cover JSON’s multiple equivalent representations. For example, a response can escape only one character of an otherwise unprefixed key as `\u0041`. The byte replacement misses it; `classify_error` decodes it back into the original key; the prefix-based helper does nothing. JSON permits escaping any character. Require bounded JSON decoding and sanitization of decoded strings inside the authorized secret scope, with mixed-escape tests through `describe`. **Round 2 #2 / Round 1 #14 remain incomplete.** [RFC 8259, §7](https://www.rfc-editor.org/rfc/rfc8259#section-7)

2. **Search-time freshness still has a check/use gap for negative postings.** `todo/03-text/TODO-05-multi-file-search.md` §5. Calling metadata-only `plan_update` first does not bind subsequent exclusion to a stable file version. Pause after a file is classified `Unchanged`, replace it with content containing the query, then resume: the old negative posting excludes it without opening or revalidating the replacement. The new test replaces the file *before* searching, so it cannot detect this case. Define a consistent search snapshot or revalidate excluded entries before publishing, scanning changed entries and reporting instability when necessary. **Round 2 #3 / Round 1 #16 remain incomplete.**

3. **The preview checkpoint still does not verify the transmitted images.** `todo/08-assistant/TODO-01-ai-assistant.md` §16. `AssistantPreviewMatchesOutboundPreview` compares text, history and host; `AssistantAuthorizesWhatItShows` compares a request digest against the same underlying preview object. Both can pass when the image strip displays a masked image but the request contains different PNG bytes. Neither proves the requested equality after page toggles and protected-content changes. Require an end-to-end check that decodes outbound image attachments and compares them with the preview’s image content, exercising both rebuild paths. **Round 2 #5 remains incomplete at its acceptance checkpoint.**

Lens verdicts:

- **record: needs-attention**, findings 1 to 3.
- **consistency: needs-attention**, finding 3.
- **source-defect: needs-attention**, finding 1.
- **adversarial: needs-attention**, findings 1 and 2.

OVERALL: needs-attention

## Response to round 3

All 3 findings were accepted and fixed in the next commit. This was the last allowed round:

1. D08 T01 §2: error bodies and headers are decoded in bounded rounds (JSON unescape including surrogate pairs, percent-decoding) before the key is scrubbed from every decoded form; a body that still contains the key, or exceeds the bounds, is discarded. The test covers raw, escaped, mixed, surrogate, URL-encoded, double-encoded and over-deep bodies.
2. D03 T05 §5: exclusion is decided only while holding the handle opened by `FileScope::open_checked` (no writers admitted), from the identity read through that handle; anything else is scanned. `MultiSearch.ExclusionBoundToHandle` writes between the identity read and the exclusion.
3. D08 T01 §16: `Shell.AssistantImagesSentEqualImagesShown` decodes every image in the recorded request per provider format and compares SHA-256 with the bytes the image strip showed, after a page toggle and the protected-content rebuild, and checks masked regions are black.
