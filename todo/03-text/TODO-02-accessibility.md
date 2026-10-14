---
schema_version: 1
id: accessibility
domain: 03-text
status: active
title: "TODO-02 -- Accessibility: UIA Text, Keyboard Order and Announcements"
track: M3
---

# TODO-02 -- Accessibility: UIA Text, Keyboard Order and Announcements

> **Goal:** Recto is operable and readable without sight or a mouse: page text exposed through a UI Automation text provider (the M3 accessibility deliverable), a logical keyboard order with visible accent focus, names and shortcuts on every icon-only control, and live announcements for the states a sighted user would glance at.

> **Current state:** Measured 2026-10-13. No code exists; the obligations are written down in SPEC section 11 and scattered through sections 9 and 10 (selection announced, hit targets at least 32 epx, 36 in the floating toolbar, colour never the only signal). Sections in D02 and D03 T01 each carry their own surface-level accessibility duties; this file builds the text provider they cannot, and audits the whole app against section 11 as the M3 gate. Reduced motion and high contrast are owned by D02 T04 §3 and re-verified here only where text surfaces touch them.

## Inputs

- `design/SPEC.md` sections 9.3 (selection announcements), 10, 11 (the whole contract), 4 (HighContrast dictionary).
- `docs/plans/2026-10-13-recto-foundation.md` sections 6 (UIA text provider at M3), 7 (M3 row).
- Microsoft UI Automation provider documentation for `ITextProvider`, `ITextRangeProvider` and WinUI `AutomationPeer` authoring.
- -> XREF: D03 T03 §4 -- Interactive form fields and annotation comments expose UIA peers alongside the page text provider

## Outcome

- A screen reader can read a page top to bottom, move by word and line, select text, and follow find results, against the same `PageText` model sighted selection uses.
- Keyboard-only operation reaches every control in title-bar, toolbar, panel, page order with visible focus.
- Find counts, current page changes, conversion completion (when D04 lands its events here) and selection extents announce politely without stealing focus.

**Adjacency:** all=not-applicable (an access layer over other files records and surfaces; it owns no data, and its every element is the accessible twin of a surface owned elsewhere)

**Adjacency rationale:** Accessibility here is a property of surfaces other sections build, plus one genuinely new surface (the UIA text provider), which is the accessible form of D03 T01's text model. Declaring product adjacencies would double-own them; the file's contract is instead the section 11 audit in section 2, which checks each owning surface in place.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | UI Automation text provider for pages | D03 T01 §1 | [ ] |
| 2 | §2 | Keyboard order, names and focus audit | D02 T04 §4 | [ ] |
| 3 | §3 | Announcements and live regions | §1, D03 T01 §3 | [ ] |

## 1. UI Automation text provider for pages

A custom `AutomationPeer` for `DocumentView` implementing the text pattern over §1 of D03 T01's `PageText`: document range, ranges by unit (character, word, line, page), selection synchronized both ways with the visual selection, text attributes where `PageText` provides them (including OCR-recognized text layers from D03 T01 §4), and scrolling a range into view through the view's geometry. The peer serves the visible document, extracting pages through the actor as ranges demand them, with the same caching the visual path uses.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 11 (page text through a UIA text provider) with `D-Search.png` for the selection it must mirror; no visual board governs the peer itself.
**Job:** A blind user can read and select document text with their screen reader. Consumer: Narrator and other UIA clients; the visual selection (D03 T01 §2) shares state with the peer.
**Treatment:** `ITextProvider` over the shared `PageText` model, ranges as (page, offset) endpoints. Cheaper substitute that fails the checkpoint: exposing only the control name and a static dump, or a parallel text extraction that disagrees with visual selection.
**Chrome:** consume `PageText`, the selection state and geometry from D03 T01 §2 and D02 T02 §3. Do not invent a second selection or text store for accessibility.

**Build order:**

1. Implement the peer and range provider in `src/app` (peer) over a `recto-services` text-range model fed by `PageText`: `GetSelection`, `RangeFromPoint` and `RangeFromChild` equivalents, `Move`, `ExpandToEnclosingUnit`, `FindText` delegating to the D03 T01 §3 engine, `ScrollIntoView`. Done when: an automation client in tests reads the corpus page's text in order and expands a range by word and line to the fixture strings.
2. Two-way selection sync and eventing: visual selection changes raise `TextSelectionChanged`; peer-driven selection paints the same quads. Done when: selecting by mouse then reading the peer selection (and the reverse) agree character for character in driven tests with Accessibility Insights or an in-process UIA client.

- [ ] Text pattern over `PageText` with unit movement. Done when: a screen-reader walk of a two-column corpus page reads in the model's reading order. Cheaper substitute that fails the checkpoint: pattern methods returning not-implemented for word and line units.
- [ ] Selection sync and events both ways. Done when: copied text after a peer-driven selection equals the clipboard fixture from D03 T01 §2.
- [ ] Commit: `a11y: UI Automation text provider over the shared PageText model`

**Test checkpoint:** `recto-tests --gtest_filter=A11y.TextProvider` (the automated UIA client this section adds) quoted in findings (document text, word and line expansion, selection sync), plus a Narrator smoke pass recorded by the reviewer on the display-session host.

## 2. Keyboard order, names and focus audit

SPEC 11's mechanical half, audited as one table now that every M2 and M3 surface exists: logical tab order (title bar, toolbar, side panel, page), stock focus visual in the accent, `AutomationProperties.Name` plus shortcut tooltip on every icon-only button (a full enumeration scraped from the command registry, D02 T03 §4), hit targets at least 32 epx (36 in the floating toolbar), and Esc closing the topmost layer in order.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 7 (target sizes) and 11, checked against every built board rather than one.
**Job:** A keyboard user can reach and identify everything. Consumer: focus and automation clients; the audit list itself becomes the regression checklist future surfaces extend.
**Treatment:** Registry-driven enumeration plus driven tab walks. Cheaper substitute that fails the checkpoint: spot-checking the famous controls and trusting defaults for the rest.
**Chrome:** consume the command registry as the name and shortcut source. Do not invent per-control names that drift from command labels.

**Build order:**

1. Generate the control inventory from the registry and the XAML tree (every icon-only button, its name, tooltip, target size) and fix gaps at their owning sections' surfaces from here (small fixes in this commit; structural ones filed with owners through `add-todo`). Done when: the inventory is complete and clean, quoted as counts.
2. Drive the tab order through each preset and each mode page, asserting the SPEC order and visible focus at every stop. Done when: the walk script passes in Focus, Classic and Reading room.

- [ ] Complete name, tooltip and target-size inventory with gaps fixed or filed. Done when: zero icon-only controls lack a name. Cheaper substitute that fails the checkpoint: naming controls as encountered with no enumeration.
- [ ] Tab-order walks in all presets. Done when: focus never lands on an invisible or dead element in any walk.
- [ ] Commit: `a11y: keyboard order, names and focus verified across surfaces`

**Test checkpoint:** `recto-tests --gtest_filter=A11y.Inventory` quotes inventory counts and all three preset walks green in findings; the inventory script fails when a nameless button is planted (proven, then removed).

## 3. Announcements and live regions

Polite live-region announcements for the states section 11 implies and sights users get free: current page changes while reading, find result counts and the current hit's position, selection extent, document opened and closed, and conversion job completion once D04 T01 §3 emits its events (the announcement sink is built here; that file plugs in). Announcements never steal focus, are rate-limited during fast scroll, and are testable through an automation event listener rather than by ear alone.

**Phase:** 3
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 9.3 and 11; no visual board governs the announcements themselves.
**Job:** A screen-reader user hears what changed without hunting for it. Consumer: UIA notification events via the peer from §1; D04 T01 §3's completion events when they land.
**Treatment:** One announcement service (polite, coalescing) fed by view-model events. Cheaper substitute that fails the checkpoint: assertive announcements per scroll tick that drown the reader, or visual toasts with no live twin.
**Chrome:** consume view and find state from D02 T02 and D03 T01; the sink is registered once in the shell. Do not invent per-surface announcement hacks.

**Build order:**

1. Announcement service with coalescing windows (page changes announce the settled page, find announces counts once per query) wired to view, find, selection and tab events. Done when: an automation event listener records exactly the expected sequence for a scripted open, find, walk and close session.
2. Reduced-verbosity check: rapid scroll and rapid find-walk produce at most the coalesced announcements the section specifies. Done when: a 2-second fast scroll yields one page announcement for the landing page in the listener log.

- [ ] Coalescing announcement service over app events. Done when: the scripted session's event sequence matches the recorded expectation exactly. Cheaper substitute that fails the checkpoint: raising a notification per event with no coalescing.
- [ ] Conversion-ready sink documented for D04 T01 §3 to plug into (no code there yet, the contract named here). Done when: that file's section cites this sink instead of inventing its own.
- [ ] Commit: `a11y: polite announcements for page, find and selection state`

**Test checkpoint:** `recto-tests --gtest_filter=A11y.Announcements` listener logs quoted in findings for the scripted session and the fast-scroll case, plus a Narrator smoke pass recorded by the reviewer.

## Verification

- SPEC section 11 read line by line against the findings: every clause proven, filed with an owner, or recorded as deliberately deferred with its section.
- M3's UIA deliverable demonstrable with Narrator on the reference machine.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
