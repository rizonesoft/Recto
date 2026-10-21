## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T00:27:45Z
- Effort: medium
- Lines: 45
- Digest: c53eb95ee583f242

Review is based on the supplied diff. Local reads failed because the command runner could not start, so the governing files and parity ledger were not independently inspected. No files were modified and git was not run. References below use `T02`, `T03` and `T04` for the three candidate files under `todo/03-text/`.

## source-defect: needs-attention

1. **T04 §2, §5: `/ToUnicode` cannot supply logical reading order.** §2 requires logical order “from `ToUnicode`”, but that mapping converts character codes to Unicode values; it does not establish the order of separately positioned text runs. Reproduce with an untagged Arabic line emitted in visual order, with individually positioned glyphs and valid mappings. The specified input cannot establish the asserted logical order reliably, and a whole-line rewrite can change the text. Define the ordering and shaping pipeline, including refusal when recovery is ambiguous. [Adobe PDF Reference, §5.9.2](https://openn.library.upenn.edu/Data/0014/ArchimedesPalimpsest/Documents/External/pdf_reference_1-7.pdf)

2. **T04 §10: constant displayed line width is impossible under an arbitrary non-uniform transform using only `w`.** The section requires setting width in object space so it displays as 2 pt under a scaled `cm`. Reproduce with a rectangle under `2 0 0 1 0 0 cm`: one scalar width cannot make both horizontal and vertical strokes uniformly 2 pt. Specify uniform-scale restrictions or a geometry-transform operation that preserves the requested stroke width.

## consistency: needs-attention

3. **T02 §9 versus T04 §14: `Ctrl+Shift+E` has two competing owners.** It stops Read Out Loud and enters Edit PDF, both applicable in a focused document view. Reproduce by starting speech and pressing the shortcut. Both requirements cannot hold alongside §14’s “bound exactly once” assertion. Assign distinct accelerators.

4. **T04 Goal, §9 and §14: text replacement can retain the removed text.** The goal promises physically removed content, but §9 explicitly permits incremental saves for text-only edits. Reproduce by replacing a sensitive paragraph and saving incrementally: the previous revision can retain the original text. Classify text rewrites that remove or replace content as requiring full rewrite, and test recovery from earlier revisions.

5. **T03 §13: a click in Select mode both edits and activates a link.** One requirement selects the link and displays handles; another opens its target on that same click. Reproduce by clicking an existing external link to resize it. The plan does not define which action wins, allowing an unwanted launch prompt during editing. Separate activation and selection gestures or modes.

## integration: needs-attention

6. **T04 §1, §3, §8: editing one occurrence of a shared Form XObject has no isolation contract.** Operator locations identify stream parts and ordinals, but the plan does not define invocation identity or cloning of shared forms and resources. Reproduce with one Form XObject invoked twice, then replace an image inside only one visible occurrence. Patching the shared stream changes both occurrences. Define occurrence paths and copy-on-write semantics, including nested forms.

7. **T04 §3, §5: a paragraph is assumed to occupy one replaceable operator range.** Grouping by geometry can produce a paragraph whose text operators are interleaved with another paragraph or a painted object. Reproduce with paragraph A’s first line, an overlapping shape, then A’s second line. A single replacement range either absorbs the shape or moves text across its paint position. Specify multiple patches and state dependencies, or refuse such layouts before mutation.

8. **T02 §7 through §9: spoken text is not always mappable to exact character quads.** The plan inserts figure alternative text and table-header prefixes, then requires every spoken word to map to exact `CharQuad` ranges. Reproduce with a tagged figure whose `/Alt` sentence has no drawn glyphs. Define synthetic speech units and their highlight policy, such as highlighting the figure rectangle or omitting word highlighting.

9. **T04 §3, §14: starting from the saved file can discard existing unsaved work.** The session explicitly opens a snapshot of the saved file, while saving later swaps the viewer to that session’s output. Reproduce by adding an unsaved annotation or form value, entering Edit PDF, changing text and saving. No reconciliation step brings the earlier changes into the edit session. Define a unified staged snapshot or an explicit save/discard transition before entry.

## adversarial: needs-attention

10. **T03 §18: segment and intersection caps do not bound intersection-search work.** A uniform grid can still perform quadratic candidate comparisons. Reproduce with 200,000 overlapping collinear segments in one cell: the intersection count stays zero while pair comparisons approach 20 billion. Cancellation alone does not bound an uncancelled build. Add comparison, grid-occupancy and subdivision budgets with deterministic truncation tests.

11. **T04 §1: the proposed stable IDs change when colliding objects are inserted.** IDs use content-derived fields and an ordinal collision suffix. Reproduce with two identical strings at the same origin, then insert another identical object before them. Rebuilding shifts suffixes, potentially redirecting a stored command to a different object. The existing unrelated-path test misses this. Define persistent session identity and unambiguous reconciliation after rebuild.

12. **T03 §12: rename and deletion leave supported destination references broken.** Rename covers string `/D` values in actions, but omits direct `/Dest` references, which §11 explicitly permits. Destination deletion also has no inbound-reference policy. Reproduce with `/Dest (Intro)`, rename `Intro` to `Start`, then follow the link; or delete a destination still used by an outline. Add complete reference rewriting and an explicit deletion policy, tested across both direct destinations and actions.

13. **T04 §8, §9: destructive crop lacks a mandatory full-rewrite classification.** “Apply crop” discards pixels, but writer selection explicitly requires full rewrite only for delete and replace. Reproduce by destructively cropping an image and saving through an incremental path: the earlier image stream can retain the discarded region. Classify destructive crop explicitly and test that the old pixels cannot be recovered from prior revisions.

## record: needs-attention

14. **T04 Goal and §13 claim vector operations that §10 does not build.** The goal promises vector resize, rotate and flip, and §13 supplies corresponding manipulation handles. §10 defines only move, recolour, line width and delete. Reproduce by tracing a path-object rotation gesture to its engine operation: there is no implementing requirement or engine checkpoint. Add the missing vector transforms and their tests, or narrow the claim and surface.

15. **T04 Outcome promises removal of an XObject that §9 correctly retains when shared.** Reproduce by deleting one of two uses of the same image. §9 requires retaining the shared image, while the Outcome says the deleted object “and its XObject” are absent from file bytes. Qualify the guarantee as removal of the selected occurrence and pruning of resources with no remaining uses.

The parity ledger and the AI, rich-media, Action Wizard and certificate-encryption sections were unavailable for inspection; no verdict on their implementation claims is implied.

OVERALL: needs-attention

## Response to round 1

Every finding was checked against the commit; all 15 were accepted and fixed in the next commit, in the section that owns each:

1. D03 T04 §2, §5: logical order comes from UAX #9 over glyph positions (and `/ActualText`), never `/ToUnicode`; ambiguous paragraphs are `NotEditable`.
2. D03 T04 §10: non-uniform transforms rewrite the path into page space before setting `w`.
3. Edit PDF keeps `Ctrl+Shift+E` (D03 T04 §14); Read Out Loud's stop moved to `Ctrl+Shift+Y` (D03 T02 §9). `design/SPEC.md` section 10 still needs that row.
4. D03 T04 Goal, §9: any edit that removes or replaces text forces a full rewrite, proven by `EditSave.NoPriorRevisionLeak`.
5. D03 T03 §13: in Select mode a click only selects a link; activation is `Ctrl+click` or `Enter`.
6. D03 T04 §1, §3: shared Form XObjects are copied on write for the selected occurrence only.
7. D03 T04 §2, §5: paragraphs keep a per-line operator range and refuse interleaved content they cannot keep in place.
8. D03 T02 §7-§9: synthetic speech (alt text, header prefixes) highlights a unit box or nothing, never invented quads.
9. D03 T04 §3, §14: entering Edit PDF on a dirty tab asks Save, Discard or Cancel first.
10. D03 T03 §18: the intersection search has its own comparison budget.
11. D03 T04 §1: edit ids come from a session registry and never change when identical objects are inserted.
12. D03 T03 §12, §14: renames rewrite direct `/Dest` references too; deleting a used name needs explicit consent.
13. D03 T04 §9: destructive crop forces a full rewrite.
14. D03 T04 §10: vector resize, rotate and flip are built, matching the §13 handles.
15. D03 T04 Outcome: a shared XObject is removed only when nothing else uses it.
