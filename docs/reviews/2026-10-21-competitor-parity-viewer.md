## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T01:55:13Z
- Effort: medium
- Lines: 43
- Digest: 7ba8c9665945a50f

The candidate needs attention. This review uses the supplied diff and primary documentation. Local reads failed before execution, so I could not independently verify the other governing files. No files were modified; no git commands or tests ran.

**source-defect: needs-attention**

1. **Dropbox conflict handling is specified incorrectly.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §9: stale `rev` does not necessarily create a conflicted copy. `autorename` defaults to false; `strict_conflict=true` supports stricter rejection. Specify revision-conditional upload with autorename disabled and map its conflict response directly. Test a remote change after the preliminary metadata read, including upload-session completion. The current fake conflicted-copy response validates the wrong default contract. [Dropbox API specification](https://github.com/dropbox/dropbox-api-spec/blob/main/files.stone)

2. **Right-to-left layout is conflated with reversed page sequence.** `todo/02-viewer/TODO-02-document-view.md` §19: deriving `reverse_order` from `/Direction /R2L` makes an ordinary right-to-left book run backwards. Reading direction controls spread arrangement; reversing the document sequence is a separate choice. Define independent flags and test all four combinations. [Adobe PDF reference, ViewerPreferences](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/pdfreference1.3.pdf)

3. **The bookmark contrast threshold permits inaccessible normal-sized text.** `todo/02-viewer/TODO-03-home-and-panels.md` §20: falling back only below 3:1 permits normal bookmark labels between 3:1 and 4.5:1. Require 4.5:1 unless the actual rendered label qualifies as large text. Test a regular-sized label at approximately 3.5:1. [WCAG 2.2 contrast requirements](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum/)

4. **Layer merging does not preserve the promised content union.** `todo/02-viewer/TODO-03-home-and-panels.md` §23: replacing every source OCG reference with one target changes membership logic. For example, a visibility expression `A AND NOT B` becomes `T AND NOT T`, permanently hiding its content. The section neither handles `/VE` semantics nor defines refusal for incompatible memberships. Require preservation or explicit rejection, with nested membership-policy and visibility-expression fixtures. [PDF optional-content specification](https://developer.adobe.com/document-services/docs/assets/35e4369068f86065372c18787171a17e/PDF_ISO_32000-1.pdf)

**consistency: needs-attention**

5. **The network gate prevents first sign-in.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §§1–3, 15: every HTTP request requires an already signed-in provider, but exchanging the authorization code uses that same gated client. The loopback listener also contradicts the absolute prohibition on opening sockets before sign-in. Define a bounded, user-authorized authentication state permitting only that provider’s authentication flow; keep ordinary provider calls blocked. Exercise first sign-in through the real gate rather than bypassing it in fakes.

6. **Google Drive explicitly violates the no-overwrite guarantee.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §8 versus Goal, Outcome and §13: a metadata check followed by an unconditional upload can overwrite a change made between those operations. Documenting the race does not satisfy “a newer remote file is never overwritten.” Require a verified atomic conditional operation, or make this provider save new copies only. Add a test that changes the remote file after the check and before commit.

7. **The edit session lacks a coherent saved revision.** `todo/01-engine/TODO-02-actor-and-render.md` §14: `mark_ocg_saved()` merely clears dirty, while discard always returns to the originally opened document. After edit A, Save, edit B, Discard, that restores the pre-A document rather than the saved A state. The parameterless save acknowledgement can also clear dirty after a newer edit arrives during file writing. Define saved revision identity, revision-bound acknowledgements and dirty comparison against that identity. Test save/undo/redo/discard and an edit arriving between export and save completion.

**integration: needs-attention**

8. **The layer import API cannot implement the actor’s builder contract as written.** `todo/02-viewer/TODO-03-home-and-panels.md` §24 versus `todo/01-engine/TODO-02-actor-and-render.md` §14: the builder receives `const Document&` and returns `ObjectChangeSet`, while `import_page_as_layer` requires mutable `Document&` and returns only `LayerId`. Specify a compatible change-set-producing operation, including allocated references and the returned layer identity. An integration test must stage the actual importer, not a test-only builder.

9. **The transition parser requires its downstream consumer to exist first.** `todo/02-viewer/TODO-02-document-view.md` §12 requires confirming the actor query, but `todo/01-engine/TODO-02-actor-and-render.md` §12 builds that query only after the parser section. This is a completion-cycle in the prose even if graph validation passes. Move the actor integration assertion into the actor section; let the parser section finish using engine tests.

10. **Conflict reload conflicts with cloud deduplication.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §12 selects the existing tab for an already-open item ID; §13 requires Reload to open a fresh tab for that same item while preserving the edited tab. Define an explicit reload/fork operation and distinct local-copy identities. Test that the two tabs contain different revisions and neither copy is overwritten.

11. **The plain-text bookmark round-trip requirement is impossible for the specified format.** `todo/02-viewer/TODO-03-home-and-panels.md` §18: text stores only indentation, title and page, yet export/parse must reproduce the tree, whose JSON representation also contains coordinates, zoom, colour, style and open state. Tabs and newlines inside titles have no escaping rule either. Define the text format’s intentional losses and escaping, or extend its schema. Test non-default properties and delimiter-containing titles.

**adversarial: needs-attention**

12. **Provider-controlled IDs enter local paths unsanitized.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §12: only the filename is sanitized in `<item-id>-<sanitized-name>`. IDs containing separators, traversal components or Windows stream syntax can escape the intended naming scheme; SharePoint’s specified composite ID already contains separators. Use an application-generated local filename, retain remote IDs as metadata, and verify resolved-path confinement. Include hostile IDs and reparse-point cases.

13. **URL restrictions cover redirects but leave other server-supplied URLs unspecified.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §§2, 6–10: pagination links and resumable-upload URLs become new requests, not necessarily redirects. The client only mandates HTTPS for initial requests. A malicious `nextLink` can therefore direct an authenticated request outside the provider boundary. Validate every request destination and its credential audience, including pagination and upload-session URLs. Test an allowed API response containing an attacker-host URL.

14. **Refresh-token replacement and sign-out races are missing.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §4: the broker refreshes access tokens but never requires persisting a replacement refresh token. Box refresh tokens are single-use, so retaining the consumed token breaks subsequent refreshes. Also specify that an in-flight refresh cannot repopulate credentials after sign-out. Test two refresh cycles across restart and sign-out while refresh is pending. [Box token lifetimes](https://github.com/box/developer.box.com/blob/main/content/guides/authentication/tokens/index.md)

15. **Cloud upload completion can mark newer edits saved.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §13: uploading a saved revision and then unconditionally clearing dirty loses track of edits made during the upload. The section also does not require the upload source to remain immutable while another save replaces the local file. Bind each transfer to an immutable revision, serialize competing saves, and clear dirty only when the current revision matches the acknowledged revision. Test edits and a second Save during a delayed upload.

16. **Transition tests can pass with every transition replaced by a cut.** `todo/02-viewer/TODO-02-document-view.md` §14: checking only the first and last frames proves neither the named effect nor its parameters. An implementation displaying the old page until the final frame satisfies those assertions. Add intermediate-frame assertions for representative directions, split dimensions and inward/outward motion, plus timing checks. Explicitly identify which PDF transition parameters are supported and which intentionally fall back.

OVERALL: needs-attention

## Response to round 1

All 16 findings were verified; 15 were fixed in the next commit and one (9) was fixed by moving the check to its owner:

1. D02 T06 §9: Dropbox uploads use `mode: update` with `strict_conflict`; a stale `rev` is a `Conflict`, tested with a change between the metadata read and the commit.
2. D02 T02 §19: `reverse_order` and `spread_direction` are independent; `/Direction /R2L` mirrors spreads without reversing the sequence.
3. D02 T03 §20: bookmark colours fall back below 4.5:1 (regular-size text).
4. D02 T03 §23: a merge that would change `/OCMD` visibility expressions or policies is refused with `MergeChangesMembership`.
5. D02 T06 §1, §3: the network gate has an `Auth` class with a short-lived, host-bound authorization token, and an `Api` class that needs a signed-in provider.
6. D02 T06 §8: Google Drive replace is `Unsupported` unless an atomic condition is proven at build time; only new copies are offered.
7. D01 T02 §13, §14: revisions carry never-reused ids; `mark_ocg_saved(revision)` names the exported revision; dirty is current versus saved revision; discard returns to the last saved state. The D02 consumers follow.
8. D02 T03 §23, §24 and D01 T02 §14: every layer operation is a builder returning `OcgEditOutput`; importers open their source inside the builder on the actor thread.
9. D02 T02 §12: the parser no longer waits on the actor query; the actor-side test lives in D01 T02 §12.
10. D02 T06 §12, §13: deduplication only for an unmodified open item; `Reload` forks into a new folder with its own revision.
11. D02 T03 §18: the text bookmark format defines escapes and states that it carries title, depth and page only.
12. D02 T06 §12: temp folders are generated GUIDs, with containment and reparse checks; hostile provider ids are tested.
13. D02 T06 §2: every provider-supplied URL is validated against the provider's hosts, and credentials stay bound to the original host.
14. D02 T06 §4: replacement refresh tokens are persisted before use, and a generation counter stops a late refresh after sign-out.
15. D02 T06 §13: uploads are bound to an immutable snapshot and clear dirty only when the tab is still at that revision.
16. D02 T02 §14: transitions are tested on intermediate frames against golden images with a fake clock; `docs/presentation-transitions.md` lists support per style.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T04:58:17Z
- Effort: medium
- Lines: 25
- Digest: 52536e4b19d79357

Review is limited to the supplied diff: local reads failed before shell execution. I could not verify the surrounding governing files or the actor-side assertion for previous finding 9. No files were modified; no git commands or tests ran.

**record: needs-attention**

1. **Previous finding 7 remains incomplete: exported revisions can expire before acknowledgement.** `todo/01-engine/TODO-02-actor-and-render.md` §14 retains exported buffers only “until a later export or close,” but `mark_ocg_saved(revision)` must retrieve any acknowledged export’s bytes. Export A, start writing A, export B, then acknowledge A: A’s snapshot need no longer exist. Retain pending exports until completion or explicit abandonment, and test this sequence.

2. **Previous finding 15 remains incomplete: local and remote saved state conflict.** `todo/02-viewer/TODO-01-app-shell.md` §8 clears dirty after local save and acknowledges the actor revision; `todo/01-engine/TODO-02-actor-and-render.md` §14 defines dirty exactly as current versus saved revision. Meanwhile, `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §13 requires dirty to remain set until upload succeeds. After a successful local save followed by upload failure, these contracts disagree. Define separate locally saved and remotely acknowledged revisions, with tab-close protection covering pending uploads, and test through the actual SavePipeline and actor.

**consistency: needs-attention**

3. **The new authentication gate blocks routine refresh and revocation.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §§1, 3–4: token-endpoint and revocation requests require an authorization token issued only by pressing `Open browser`, expiring after 180 seconds. Background refresh after expiry or restart has no valid authorization path; ordinary sign-out cannot perform its specified revocation either. Define narrowly scoped refresh and revocation authorization based on the existing account session, and exercise both through the real gate without reopening sign-in.

4. **The builder fix introduces a dependency from the PDF engine back into core.** `todo/02-viewer/TODO-03-home-and-panels.md` §§23–24 now require functions in `src/pdf` to return `OcgEditOutput`, defined in `src/core/ocg_edit.h` by `todo/01-engine/TODO-02-actor-and-render.md` §14. That core type itself contains PDF-engine types. Put the engine result in `recto-pdf`, then adapt it to actor messages in core; verify the engine builds independently of core headers.

**source-defect: needs-attention**

5. **The new transition requirements contradict both cut semantics and the PDF parameter definitions.** `todo/02-viewer/TODO-02-document-view.md` §14 requires intermediate frames differing from both endpoints for *every* style, including `R`, explicitly defined as a cut. It also requires `/M` to reverse `Blinds`, although `/M` applies to `Split`, `Box` and `Fly`; Blinds has fixed movement directions. Exempt cuts from intermediate-image assertions and use style-specific parameter tests. Previous finding 16 therefore needs another correction. [ISO 32000-1, Table 162](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf)

**adversarial: needs-attention**

6. **Previous finding 12 remains incomplete: path confinement is checked after the download writes.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §12 orders download before verifying containment and reparse points. A planted junction can redirect the write before rejection; merely asserting that the operation is refused misses that damage. Establish confinement before opening the destination and maintain it through writing and cleanup. Test that an external sentinel file remains unchanged.

7. **Previous finding 14 still permits credential resurrection unless sign-out and persistence are atomic relative to each other.** `todo/02-viewer/TODO-06-cloud-storage-connectors.md` §4 specifies comparing the generation “before the write.” Refresh can pass that comparison, sign-out can increment the generation and delete credentials, then refresh can write the replacement token. Serialize generation validation and credential persistence with sign-out, and test a barrier specifically between validation and `CredWriteW`.

OVERALL: needs-attention

## Response to round 2

1. Fixed: `D01 T02 §14` keeps every pending export (not acknowledged and not released by the new `abandon_ocg_export(revision)`) up to 4, releases older pending exports on acknowledgement, and adds `Actor.OcgEditPendingExports` for export A, export B, acknowledge A; `D02 T01 §8` calls `abandon_ocg_export` on a failed or cancelled write.
2. Fixed: `D02 T06 §13` now keeps two states apart: local dirty stays the actor's (`D01 T02 §14`), and `CloudOrigin.uploaded_revision` drives a separate `upload pending` state with a `Not uploaded` badge and its own close prompt; `Cloud.SaveUploadPendingState` runs through the real `SavePipeline` and `DocumentActor`.
3. Fixed: `D02 T06 §1` splits the gate's request classes into `Api`, `Authorize`, `Refresh` (allowed with a stored refresh credential, token host only) and `Revoke` (a 30 s `SignOutInProgress` token, revocation host only); `§4` refresh and sign-out go through the real gate.
4. Fixed: `D01 T02 §14` defines `recto::pdf::OcgBuildResult` in `src/pdf/ocg_build.h` with `OcgEditOutput` as a core alias, and requires the engine header to compile with no `src/core` include path; `D02 T03 §23` and `§24` builders return `PdfResult<recto::pdf::OcgBuildResult>`.
5. Fixed: `D02 T02 §14` exempts `R` (a cut) from the intermediate-frame assertion and applies `/M` to `Split`, `Box` and `Fly` per ISO 32000-1 Table 162, with a test that `/M` leaves `Blinds` unchanged.
6. Fixed: `D02 T06 §12` establishes confinement before any write (reparse-point checks on each folder component, `CREATE_NEW` with `FILE_FLAG_OPEN_REPARSE_POINT`, `GetFinalPathNameByHandleW` on the open handle, handles held through write and cleanup) and tests that an outside sentinel file is unchanged.
7. Fixed: `D02 T06 §4` holds a per-provider mutex across the generation check and `CredWriteW` that `sign_out` also takes, with `Cloud.RefreshSignOutRace` parking the refresh at a barrier between check and write.
