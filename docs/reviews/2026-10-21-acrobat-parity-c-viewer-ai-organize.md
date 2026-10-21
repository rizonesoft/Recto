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
