---
schema_version: 1
id: bridge-runtime
domain: 01-engine
status: active
title: "TODO-01 -- MuPDF Bridge Runtime"
frozen: true
track: M1
---

# TODO-01 -- MuPDF Bridge Runtime

> **Goal:** `src/bridge` exists as the only code that includes MuPDF headers: it creates the locked base context every other context clones from, turns every MuPDF error into a returned status (never a `longjmp` across C++), and exposes document open, password, outline and page information to `recto-core` in Recto's own types.

> **Current state:** Measured 2026-10-13. No C or C++ code exists. D00 T01 §2 will pin the MuPDF submodule and `build/mupdf-config.h` (JavaScript disabled, D12); §3 creates the empty `recto-bridge` project with the right references. The contract this file implements is foundation plan sections 4.1 to 4.4: why a hand-written C bridge instead of `mupdfcpp`, the seven threading rules, the `SRWLOCK` lock table, and the `rc_status` error shape. This file is `frozen: true`: the threading contract it establishes is what every later engine section is checked against.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 4.1 (bridge rationale), 4.2 (the seven rules), 4.3 (lock implementation, with the reference `runtime.c`), 4.4 (error handling across the C and C++ line), 3.3 (`Runtime`, `PdfError` core types).
- MuPDF documentation on multi-threading and the `multi-threaded.c` example in the pinned submodule's `docs/examples/`.
- -> XREF: D06 T02 §1 -- the security review that later proves the bridge's hostile-input posture (no JavaScript objects, errors contained) still holds; its findings land here if the bridge is where they belong.

## Outcome

- `rc_runtime_create` produces a base context with a full `FZ_LOCK_MAX` lock table; clones can be made from any thread and outlive nothing they should not.
- No MuPDF error can escape the bridge as control flow: every bridge function returns `rc_status`, and `recto-core` turns a failure into `PdfError`.
- A document can be opened, password-checked and described (page count, page sizes, outline, metadata) through Recto types alone.

**Adjacency:** all=not-applicable (a C library boundary with no records, surfaces or user data of its own; its diagnostics surface is the warning and error callback wired in section 2, owned here)

**Adjacency rationale:** The bridge's only user-visible adjunct is the log its MuPDF warning and error callbacks feed, which is a diagnostic trail, not an audit record over domain data. Everything a user would call a surface (lists, settings, documents, notifications) belongs to the viewer and services domains that consume this boundary.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Locks and base context | D00 T01 §3 | [ ] |
| 2 | §2 | Error bridge and logging callbacks | §1 | [ ] |
| 3 | §3 | Document open and description API | §2 | [ ] |

## 1. Locks and base context

MuPDF is safe from many threads only when the base context is created with a lock table; without it, cloning is not allowed and nothing above the bridge is sound. The lock array and `fz_locks_context` are static so they outlive every context, the callbacks are plain C functions that never throw and never block on anything but the lock, and `SRWLOCK`s are correct because MuPDF never takes the same lock twice on one thread (foundation plan 4.3).

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/bridge/runtime.c` and `src/bridge/recto_bridge.h`: the static lock array, `rc_lock` and `rc_unlock`, `rc_runtime_create(store_bytes)` registering document handlers inside `fz_try`, and `rc_runtime_destroy` plus clone create and drop helpers. Done when: a test clones the base from 8 threads at once, drops every clone, and destroys the base with no crash and a live-clone count of zero.
2. Start `store_bytes` at 256 MB (`FZ_STORE_DEFAULT`) behind the create parameter, per plan 4.3, tuned later in M2 against the tile budget. Done when: the value is a parameter with the default named in one place, not a literal scattered through callers.

- [ ] `src/bridge/runtime.c` with the lock table and base context. Done when: the 8-thread clone test passes under the D00 T03 §3 ASan configuration. Cheaper substitute: a lock-free single context with a global mutex added later, which voids every rule in plan 4.2.
- [ ] Live-clone accounting (debug counter incremented on clone, decremented on drop, asserted zero at destroy). Done when: leaking a clone in a test trips the assert in Debug.
- [ ] Commit: `bridge: locked base context and clone lifecycle`

**Freeze check:** The section 4.11 clone and shutdown invariants hold as written in the foundation plan: locks first, one context per thread, clones die before the base. Proven by the multi-threaded clone test in `recto-tests` (`Bridge.ClonesFromManyThreads`) run under ASan; the fixture is the test itself, committed in `tests/`.

**Test checkpoint:** `recto-tests --gtest_filter=Bridge.*` passes under ASan with the clone-storm test green (quote pass count and zero sanitizer findings).

## 2. Error bridge and logging callbacks

MuPDF errors are `setjmp` and `longjmp`; a `longjmp` over C++ frames with destructors is undefined under `/EHsc`, which is the bridge's reason to exist (plan 4.4). The rules: bridge code is C only; cleanup lives in `fz_always`; every local changed inside `fz_try` and read after it is marked `fz_var`; nothing returns, breaks or gotos out of an `fz_try` block; callers see `rc_status { int code; char message[256]; }` and nothing else.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `rc_status` and `rc_status_from_error` to `recto_bridge.h`, plus the internal pattern every bridge function follows, demonstrated on one real function (document open from §3 may stub forward). Done when: a test feeding the `tests/corpus/broken/` file gets a non-zero status with MuPDF's message, and the process continues to open a good file after.
2. Install warning and error callbacks (`fz_set_warning_callback`, `fz_set_error_callback`) on the base context routing to Recto's log with a thread-name tag, as plain C functions that cannot throw. Done when: opening the damaged corpus file produces a tagged log line and a clean status, in that order.

- [ ] `rc_status` error shape used by every bridge entry point. Done when: `grep` over `src/bridge` finds no bridge function returning a raw MuPDF object without a status. Cheaper substitute: letting `fz_throw` unwind into C++ "because it worked in the test".
- [ ] Warning and error callbacks to the Recto log. Done when: the damaged-file test asserts a log line naming the thread.
- [ ] Commit: `bridge: rc_status error bridge and MuPDF logging callbacks`

**Freeze check:** No C++ frame can sit between an `fz_try` and a throw: `recto-bridge` remains `/TC` C only, and the damaged-file test (`Bridge.DamagedFileReturnsStatus`) proves errors arrive as values. The build itself is the fixture: a `.cpp` file in the bridge project fails review on sight.

**Test checkpoint:** `recto-tests --gtest_filter=Bridge.*` passes under ASan, including the damaged-file and not-a-PDF cases returning statuses (quote the messages).

## 3. Document open and description API

The actor's front door (plan 4.6 steps 1 to 3): open a document on the caller's context clone, report password need, authenticate, and describe the document (page count, per-page bounds, outline, core metadata) in plain C structs the core can copy. Loading pages and building display lists belong to D01 T02; this section stops at description so the boundary is provable before rendering enters.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/bridge/document.c`: open from path and from memory buffer, `rc_document_needs_password`, `rc_document_authenticate`, page count, `rc_page_bounds`, outline walk into a flat array (title, depth, page index), metadata getters (title, author, producer, dates, PDF version where MuPDF exposes it). Every function follows the §2 pattern. Done when: tests over the corpus starter set (D00 T03 §2) assert page counts, the encrypted file demands then accepts its known password, and the outlined file returns its entries in order.
2. Define the matching `recto-core` types (`PdfError` from `rc_status`, document description structs) in `src/core/` with no MuPDF header visible. Done when: `recto-core` compiles with the bridge header as its only engine include and a core-side test describes a corpus file end to end.

- [ ] `src/bridge/document.c` open, password and description functions. Done when: wrong password returns a distinct status from damaged file, and neither crashes. Cheaper substitute: exposing `fz_document*` upward "temporarily".
- [ ] `recto-core` description types and `PdfError`. Done when: the core test prints the same page count and outline the corpus provenance records.
- [ ] Commit: `bridge: document open, password and description API`

**Freeze check:** Document objects stay on the context that opened them: the API takes the caller's `fz_context*` explicitly and stores no document in globals; `Bridge.DocumentStaysOnOwnerContext` opens on one clone and describes from the same clone across the suite. Cross-thread misuse is unrepresentable above the bridge because core never sees the handle type.

**Test checkpoint:** `recto-tests --gtest_filter=Bridge.*:Core.*` passes under ASan over the full starter corpus (quote files exercised and results).

## Verification

- `grep` proves only `src/bridge` includes MuPDF headers anywhere under `src/`.
- The full `Bridge.*` and `Core.*` suites pass under ASan; `python scripts/todo-graph.py validate` passes.
- No JavaScript is linked (the D00 T01 §2 config check still holds with real code above it).
