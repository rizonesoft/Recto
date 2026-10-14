---
schema_version: 1
id: jobs-and-queue
domain: 04-convert
status: active
title: "TODO-01 -- Conversion Jobs and the Queue"
track: M4
---

# TODO-01 -- Conversion Jobs and the Queue

> **Goal:** Every conversion in Recto runs the same way: an isolated job on its own thread with its own `recto::pdf::Document` instance, progress and cancellation through its `CancelToken`, at most two at once, visible in the SPEC queue card with honest states, and completion announced by toast or Windows notification. The individual converters (T02 to T04) plug into this frame and write nothing else.

> **Current state:** Measured 2026-10-14. No code exists. The isolation contract is foundation plan 4.9 (a conversion never shares the viewer's document; progress via `CancelToken` posted with `DispatcherQueue::TryEnqueue`), the writer pattern is plan section 5, and the surfaces are SPEC 9.5 (three-step Convert page, queue card states running, waiting, done, failed with "Enter password") and 9.9 (finished-conversion notification and toast), boards `D-Convert.png` and `explorations/A-Convert.png`, `A-Dialogs.png`, `B-Dialogs.png`. Shutdown joins jobs at their registered point (D01 T02 §4).

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3 (`ConversionJob`, `CancelToken`), 4.5, 4.9, 4.10, 5 (writer pattern and the atomic saving rule), 7 (M4).
- `design/SPEC.md` sections 9.5, 9.9, 11 (announcements twin via D03 T02 §3's sink).
- -> XREF: D05 T02 §4 -- Organize saves run as jobs on this frame (a page-operation save is a conversion with a progress and a failure mode); the job contract below is what that section consumes.
- -> XREF: D04 T02 §1 -- the first real converters register against §1's contract and re-prove its isolation with a live writer.
- -> XREF: D04 T03 §1 -- the native `DocxWriter` registers the Word document card and step 3 options against §2's converter-card registry.
- -> XREF: D04 T04 §1 -- the `Office -> PDF` converters register the PDF from Office card and `NeedsConverter` recovery path against §1 and §2 here.

## Outcome

- A job of any kind runs isolated, reports progress, cancels cleanly leaving no output file, and fails with the engine's message and a recovery action where one exists (password re-entry above all).
- The queue card and its expanded list show true states at all times, and the window stays fully usable while jobs run.
- Finished jobs raise the SPEC notification or toast with Open file and Show in folder working.

**Adjacency:** list=applicable @ D04 T01 §2; document=applicable @ D04 T01 §3; settings=applicable @ D02 T04 §2; reporting=not-applicable (job history is session-scoped by design, stated in section 2); notifications=applicable @ D04 T01 §3; permissions=not-applicable (no roles; per-file password refusal is a job state, section 2); audit=not-applicable (the job log lines are diagnostics, not an audit trail); exchange=not-applicable (the converters own their formats, T02 to T04); reverse=applicable @ D04 T01 §1

**Adjacency rationale:** The queue is the list, its completion notice is both the document the user acts on and the notification surface, and cancel is the reverse of starting a job. Save-location defaults live in Settings (D02 T04 §2 owns the value, the Convert page reads it). Session-scoped history is a deliberate boundary: Recto keeps no conversion log across restarts in v1, stated here instead of discovered.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | ConversionJob runtime and job contract | D01 T02 §4 | [ ] |
| 2 | §2 | Convert page frame and queue UI | §1, D02 T01 §2 | [ ] |
| 3 | §3 | Completion notifications and error actions | §2 | [ ] |

## 1. ConversionJob runtime and job contract

Plan 4.9 as an enforceable contract: a job type registers a factory producing a `ConversionJob` (inputs, options struct, run function); the runtime runs at most two concurrently, each on its own thread with its own opened `recto::pdf::Document` instances, exposing progress (`CancelToken`-polled, throttled to the UI), cancel, and a terminal result (outputs written, or an error with the engine message and a recovery kind: none, needs-password, needs-converter). Output writes follow plan section 5's rule: temp file in the destination folder, then replace; a cancelled or failed job removes its temp files and creates nothing.

**Phase:** 4
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/services/conversion.cpp`: job registry, the two-at-once runner with FIFO waiting, per-job lifecycle joined into `Runtime` shutdown, `CancelToken` progress polling marshalled to the UI. Done when: a test job type (page-count copy through the writer pattern) runs two-up over corpus files while a viewer document of the same file stays responsive and unaffected (battery row 4 at services level).
2. Result and recovery contract: typed outcomes including `NeedsPassword` carrying the job's inputs for resubmission and `NeedsConverter` for D04 T04 §4. Done when: the encrypted corpus file's job fails as `NeedsPassword` (not a generic error) and resubmits successfully with the password supplied.

- [ ] Isolated runner with the two-at-once bound. Done when: a third submitted job demonstrably waits and starts on completion of the first. Cheaper substitute: unbounded threads per job, which reintroduces contention plan 4.9 exists to remove.
- [ ] Cancel and atomic-output rules in the contract. Done when: cancelling mid-write leaves neither output nor temp file (asserted).
- [ ] Commit: `convert: isolated ConversionJob runtime with progress and cancel`

**Test checkpoint:** `recto-tests --gtest_filter=Jobs.*` passes under ASan: isolation while viewing, two-at-once bound, cancel cleanliness, password recovery resubmission (quote job counts).

## 2. Convert page frame and queue UI

SPEC 9.5's page as the frame every converter hangs in: step 1 Files (drop zone and picker, removable chips, PDFs and Office files mixed), step 2 What to make (the option-card grid with per-file-type enablement and reasons, cards registered by T02 to T04), step 3 Options (each card's own options control), the honest hint line, and the floating queue card (bottom-right, progress ring) expanding to the full job list with cancel, open folder and error actions. Started jobs keep running when the user leaves the page; the card is visible app-wide while any job lives.

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.5 with `D-Convert.png` and `explorations/A-Convert.png` (queue states), `B-Convert.png` (a step-3 form).
**Job:** The user can turn files into other files through a three-step flow that explains itself, and watch or cancel the work. Consumer: the output files, opened from §3's actions; the cards' converters (T02 to T04).
**Treatment:** One page hosting registered converter cards and option controls; enablement computed from the actual input set with a stated reason when disabled. Cheaper substitute that fails the checkpoint: one hard-coded page per conversion, or enabling everything and failing at run time.
**Chrome:** consume a navigation slot in the shell, the SPEC card selection treatment (accent border, tint glow, check badge) and the queue card shape for all jobs including Organize saves. Do not invent per-converter pages or progress widgets.

**Build order:**

1. The three-step frame with the converter-card registry (id, title, glyph, hint text, accepted-input predicate with disable reason, options factory, job factory into §1) and file chips with type detection. Done when: the test job from §1 runs end to end through the real page, and an Office-only card shows its SPEC disable reason over a PDF-only input set.
2. The queue card and expanded list bound to the §1 runner: states, per-job progress, cancel, and persistence across page navigation. Done when: a running job's card survives switching to the Viewer and back with continuous progress.

- [ ] Convert page frame with registry-driven cards. Done when: adding T02's first converter later is a registration, not a page edit. Cheaper substitute that fails the checkpoint: `if` chains per output type inside the page.
- [ ] Queue card with true states and cancel. Done when: the four SPEC states are each demonstrable on demand in the driven test.
- [ ] Commit: `convert: three-step Convert page and the job queue card`

**Test checkpoint:** screenshots against `D-Convert.png` and `A-Convert.png` in findings, with enablement reasons and a cancel driven end to end.

## 3. Completion notifications and error actions

SPEC 9.9's completion behavior: window not focused raises a Windows notification ("<output> is ready", Open file, Show in folder); focused shows the in-app toast with the same actions. Failures surface in the queue with the engine message and, per recovery kind, Enter password (resubmits through §1) or the converter-missing path (D04 T04 §4). Announcements twin through D03 T02 §3's sink. Notification activation when the app was closed reopens the outputs sensibly (file exists check first, honest message if moved).

**Phase:** 4
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** SPEC section 9.9 with `explorations/A-Dialogs.png` (Windows notification) and `B-Dialogs.png` (in-app toast).
**Job:** The user learns the moment their file is ready, and can act on it or fix what failed, from wherever they are. Consumer: the shell's open pipeline (Open file) and Explorer (Show in folder).
**Treatment:** App notifications through the Windows App SDK with an arg payload naming outputs; toast shares the action handlers. Cheaper substitute that fails the checkpoint: a status text only visible on the Convert page.
**Chrome:** consume the queue card's action handlers so card, toast and notification behave identically. Do not invent three copies of Open and Show logic.

**Build order:**

1. Notification and toast delivery keyed on window focus at job completion, with activation handling.
2. Error actions in the expanded queue: Enter password flow (SPEC dialog from D02 T01 §4 reused), open folder, dismiss; failed multi-file jobs report per-file outcomes. Done when for both: the encrypted-file job completes through the queue's own password action in the driven test, and both notification forms are captured in findings.

- [ ] Focus-aware completion notices with working actions. Done when: Open file from a notification opens the produced file in a Recto tab (or its handler for non-PDF outputs). Cheaper substitute that fails the checkpoint: notifications that only inform.
- [ ] Password recovery from the queue. Done when: the resubmitted job replaces the failed entry's state rather than duplicating it.
- [ ] Commit: `convert: completion notifications, toasts and queue error actions`

**Test checkpoint:** `recto-tests --gtest_filter=Jobs.Notices` green, captures of both notice forms in findings against the boards, password-through-queue driven green, and the announcement listener (D03 T02 §3) recording the completion.

## Verification

- Every SPEC 9.5 and job-related 9.9 element accounted for on rendered surfaces.
- Job isolation re-proven with real converters once T02 lands (its §1 re-runs the service-level isolation test).
- `recto-tests` full suite green; `python scripts/todo-graph.py validate` passes.
