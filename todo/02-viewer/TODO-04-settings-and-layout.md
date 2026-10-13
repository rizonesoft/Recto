---
schema_version: 1
id: settings-and-layout
domain: 02-viewer
status: active
title: "TODO-04 -- Settings, Layout Presets and Theme"
track: M2
---

# TODO-04 -- Settings, Layout Presets and Theme

> **Goal:** The shell becomes the SPEC's shape-shifter: one settings store with every SPEC 9.7 group, the three layout presets (Focus, Classic, Reading room) plus honest "Custom" driven by moving shared controls between slots, theme including dark page colour, and the complete SPEC section 10 keyboard map audited end to end.

> **Current state:** Measured 2026-10-13. No code exists. D02 T01 §2 will declare the slot hosts and a `LayoutSettings` placeholder; T02 and T03 build the controls that move. Everything is decided: SPEC section 8 (the five layout settings composing three presets, D17: one shell, not three), 9.7 (Settings groups and order, find-a-setting), 4 (theme and page colour), 10 (keyboard table), 11 (animations off honored), 12 (motion), with boards `D-Layout.png`, `D-Settings.png`, `D-Classic.png`, `D-ReadingRoom.png`. Settings values already consumed elsewhere: ask-before-links and page colour (D02 T02 §4), remember reading position (D02 T03 §1), tile cache size (D02 T02 §2), Office converter choice (D04 T04 §1), crash reports (D07 T02 §3).

## Inputs

- `design/SPEC.md` sections 4, 8, 9.7, 10, 11, 12; `resources/svg/layout-{focus,classic,readingroom}-{light,dark}.svg` (preset card wireframes).
- `docs/plans/2026-10-13-recto-foundation.md` sections 1 (D17), 6.2 (preset implementation summary).
- -> XREF: D02 T01 §2 -- the slot hosts and placeholder this file's preset engine drives; slot names agreed there are consumed unchanged here.
- -> XREF: D02 T03 §2 -- the panels and thumbnail list whose reparenting and orientation switching §1 here must prove.

## Outcome

- Switching presets rearranges the same live controls (toolbar, panels, thumbnails, navigation) with document state untouched; changing any of the five fine-tune settings renames the preset to Custom.
- Every SPEC 9.7 setting exists, persists, and has a working consumer; find-a-setting filters rows live.
- Light, Dark and Follow Windows themes plus the page colour setting apply live, and every SPEC section 10 shortcut does what the table says.

**Adjacency:** list=applicable @ D02 T04 §2; document=not-applicable (no printed output); settings=applicable @ D02 T04 §2; reporting=not-applicable (no reports); notifications=not-applicable (none originate here); permissions=not-applicable (no roles); audit=not-applicable (settings history is not kept, by design); exchange=not-applicable (no settings import or export in v1, a stated non-goal); reverse=applicable @ D02 T04 §1

**Adjacency rationale:** Settings is this file's whole subject, and its list is the find-a-setting filter over its own rows. The reverse surface is returning to a named preset (and to defaults per group) after fine-tuning, owned by the preset engine in section 1: a layout the user can only leave by reinstalling would be a defect. Settings export is deliberately not a v1 surface; the store format is documented JSON-free app data, and the non-goal is stated rather than silent.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Settings store and layout preset engine | D02 T03 §2 | [ ] |
| 2 | §2 | Settings page, all groups | §1 | [ ] |
| 3 | §3 | Theme, page colour and motion compliance | §1 | [ ] |
| 4 | §4 | Keyboard map completeness audit | §2, §3 | [ ] |

## 1. Settings store and layout preset engine

One store, one writer: a settings service persisting to app local data with atomic writes, typed accessors per setting with SPEC defaults, and change notifications consumers subscribe to. `LayoutSettings` holds the five SPEC section 8 settings (toolbar position, panel kind, thumbnail placement, search and outline side, navigation kind); the preset engine maps the three named presets to value sets, moves the shared controls between the shell's named slots, switches `ThumbnailList` orientation, and derives the preset name (Focus, Classic, Reading room, or Custom when any value deviates).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 8 table with `D-Focus-Light.png`, `D-Classic.png` and `D-ReadingRoom.png` as the three outcomes of the same controls.
**Job:** The user can arrange Recto the way they read, without losing their place. Consumer: every moved control keeps its state and bindings; the Settings page (§2) writes, surfaces read.
**Treatment:** Reparenting live controls between named slots, per D17. Cheaper substitute that fails the checkpoint: rebuilding the shell per preset (state loss), or three shells (tripled maintenance, refused by the plan).
**Chrome:** consume the slot hosts from D02 T01 §2 and the controls from D02 T02 and T03 unchanged. Do not invent preset-specific control variants.

**Build order:**

1. Settings service in `src/services/settings.cpp`: typed values with defaults from SPEC 9.7, atomic persistence, change events, a test seam for in-memory values. Done when: a value changed, app restarted, reads back identical, and a corrupted store file falls back to defaults with a logged parse failure (same degrade rule as recents).
2. Preset engine: value sets for the three presets, slot moves for toolbar, panels and navigation, thumbnail orientation switch, preset-name derivation. Done when: cycling Focus to Classic to Reading room and back with a document open preserves page, zoom, panel state and scroll, verified by driven test.

- [ ] Persistent typed settings store with one writer. Done when: no other code writes the settings file (search proves the single writer). Cheaper substitute that fails the checkpoint: `ApplicationData` roaming values written from every page.
- [ ] Preset engine over slots with the Custom rule. Done when: changing one fine-tune value flips the Settings preset label to Custom immediately.
- [ ] Commit: `viewer: settings store and layout preset engine over shell slots`

**Test checkpoint:** `recto-tests --gtest_filter=Settings.Presets` green, screenshots of all three presets against their boards in findings, with the state-preservation cycle driven and the corrupted-store degrade proven.

## 2. Settings page, all groups

SPEC 9.7 end to end: one centred column (760 to 900 epx), groups in the SPEC order (Layout with preset cards and the five fine-tune rows, Theme, Reading, Converting, Performance and privacy, Default PDF app, About entry), group labels per SPEC section 5, and a Find a setting box filtering rows as the user types. Every row's consumer must already exist or land with its named owner: cache size (128 MB to 2 GB, default 512 MB) drives D02 T02 §2's budget, converter choice shows what was found (D04 T04 §1), crash reports default off and gate D07 T02 §3.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.7 with `D-Layout.png` (Layout group and preset cards) and `D-Settings.png` (remaining groups).
**Job:** The user can find and change every tunable Recto has, and see it apply. Consumer: each row writes the store (§1); the named feature sections read their values from it.
**Treatment:** Stock settings controls (`ToggleSwitch`, `ComboBox`, `NumberBox`, sliders) in grouped cards, preset cards using the shipped wireframe SVGs. Cheaper substitute that fails the checkpoint: a settings page whose rows are inert placeholders for later milestones.
**Chrome:** consume the preset wireframe graphics, grouped-card shape (radius 16, SPEC section 7) and the store from §1. Do not invent a second appearance for settings rows anywhere (converter and AI-free options all live here).

**Build order:**

1. Layout and Theme groups wired to §1 and §3: preset cards with wireframes, five fine-tune rows, theme selector, page colour selector. Done when: every control applies live and matches its board.
2. Reading, Converting (converter selector showing detected installs, save-location choice), Performance and privacy (cache slider bounded 128 MB to 2 GB applying to the live texture cache, crash reports toggle default off), Default PDF app (opens the Windows Default apps page), About navigation (to D07 T02 §1's page, placeholder until then). Done when: each value has a quoted consumer behavior in the findings (cache budget changes telemetry, converter selector reflects this machine's installs).
3. Find a setting: filters rows by label text across groups, empty groups collapse, Esc clears. Done when: typing "cache", "dark" and "office" each leave exactly the right rows visible in driven tests.

- [ ] All SPEC 9.7 groups present and live. Done when: the surface-completeness account lists every 9.7 row working or deferred to its named owner section (converter detection depth to D04 T04 §1, About content to D07 T02 §1). Cheaper substitute that fails the checkpoint: rows that persist but nothing reads.
- [ ] Find a setting filtering. Done when: a search with no matches shows the honest empty state, not a blank page.
- [ ] Commit: `viewer: Settings page with all SPEC 9.7 groups live`

**Test checkpoint:** screenshots against `D-Layout.png` and `D-Settings.png`, cache-slider telemetry change quoted, and the three find-a-setting queries driven green.

## 3. Theme, page colour and motion compliance

Theme follows SPEC section 4 and 9.7: Light, Dark or Follow Windows, applied live including title-bar and caption treatment, with the HighContrast dictionary mapping every Recto brush to system colours. Page colour (Original, Dimmed default, Inverted) is D02 T02 §4's compositing reading this setting. Motion follows SPEC sections 11 and 12: panel and card transitions at 167 ms, and when Windows "Show animations" is off, the toolbar does not fade and panels appear without motion.

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC sections 4, 11 and 12 with `D-Focus-Dark.png` and `D-ReadingRoom.png` (dark surfaces and dimmed pages).
**Job:** The user can read in their OS's light or dark world, and in high contrast, without Recto fighting it. Consumer: every surface through theme resources; the view's page compositing reads the page colour value.
**Treatment:** `RequestedTheme` plus theme dictionaries, system theme change listening for Follow Windows. Cheaper substitute that fails the checkpoint: restart-required theme changes, or manual colour swaps on individual controls.
**Chrome:** consume `RectoColors.xaml` theme dictionaries and the light and dark SVG variants through theme resources. Do not invent code-driven colour switching.

**Build order:**

1. Theme application: selector value to window and content theme, Follow Windows reacting to OS changes live, Mica Alt backdrop where available per SPEC section 4's window brush note. Done when: flipping the OS theme with Follow Windows re-themes Recto without restart, screenshots both ways.
2. HighContrast verification pass: every surface built so far checked under a high-contrast theme for legibility and non-colour-only signals (SPEC 11: selected pages also carry the check badge, errors carry icons and text). Done when: the pass's findings list is empty or filed with owners.
3. Reduced-motion compliance: animation settings query gating the toolbar fade and panel transitions. Done when: with animations off at OS level, panels appear instantly and the toolbar stays opaque.

- [ ] Live theme switching including Follow Windows. Done when: captions, menus and dialogs follow, not just the canvas. Cheaper substitute that fails the checkpoint: theme applied at launch only.
- [ ] HighContrast and reduced-motion passes recorded. Done when: the checks are repeatable steps named in the findings, not impressions.
- [ ] Commit: `viewer: live theme, high contrast and reduced-motion compliance`

**Test checkpoint:** `recto-tests --gtest_filter=Settings.Theme` green, screenshot set in findings (light, dark, OS-flip, high contrast) against the boards, with the reduced-motion behavior driven and observed.

## 4. Keyboard map completeness audit

SPEC section 10 is a contract table; this section proves every row of it against the running app now that shell, view, panels, command box and settings all exist: file and tab keys, find keys (find itself lands in D03 T01 §3, its accelerators register here against the command registry), zoom and rotate, go-to-page, panel toggle, Settings (Ctrl+,), full screen, Esc layering. The audit is a table-driven driven test plus a registry check (every accelerator maps to a registry command, D02 T03 §4), and it owns the keyboard-shortcuts reference the About page links to (SPEC 9.8).

**Phase:** 2
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 10 (the table is the board; no screenshot governs keys) and section 11 (focus order and visuals).
**Job:** The user can drive Recto entirely from the keyboard, predictably. Consumer: the command registry executes; About's shortcuts page (D07 T02 §1) republishes this audit's table.
**Treatment:** Accelerators bound to registry commands on the shell, with view-level keys scoped to the document view. Cheaper substitute that fails the checkpoint: ad-hoc `KeyDown` handlers per control that conflict and drift from the table.
**Chrome:** consume the command registry and stock focus visuals in the accent. Do not invent per-page key handling outside the registry.

**Build order:**

1. Bind every SPEC section 10 row through the registry, resolving collisions with stock control behavior deliberately (recorded where a stock control wins). Done when: the audit table generation (registry dump) lists each SPEC row with its binding.
2. Driven audit: synthesize each shortcut in the running app and assert its effect (tab cycles, zoom steps, panel toggles, full screen, Esc closes topmost first). Done when: the audit runs green as a repeatable script whose output is quoted at stamp time.
3. Write `docs/keyboard-shortcuts.md` from the audited table (About links it). Done when: the document and the registry dump agree line for line.

- [ ] Complete accelerator binding through the registry. Done when: no SPEC row is missing, duplicated or shadowed silently. Cheaper substitute that fails the checkpoint: binding the famous keys and leaving the tail (Ctrl+B, Ctrl+G, F11) for later.
- [ ] Repeatable driven audit plus the shortcuts document. Done when: the audit script fails if a binding is removed (proven once by removal, then restored).
- [ ] Commit: `viewer: complete SPEC keyboard map, audited end to end`

**Test checkpoint:** the keyboard audit (`recto-tests --gtest_filter=Keyboard.Audit`, the script this section adds) quotes its full table green in findings, plus the removal-failure proof, and focus order (title bar, toolbar, panel, page) driven by Tab alone.

## Verification

- All three presets rendered against their boards from one installed build, state preserved across switches.
- Every SPEC 9.7 row and section 10 key accounted for in findings; settings consumers quoted per row.
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes. M2's remaining done-when evidence (performance targets) closes in D06 T01 §4.
