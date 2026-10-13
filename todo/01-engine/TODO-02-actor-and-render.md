---
schema_version: 1
id: actor-and-render
domain: 01-engine
status: active
title: "TODO-02 -- Document Actor, Display Lists and Render Pool"
frozen: true
track: M1
---

# TODO-02 -- Document Actor, Display Lists and Render Pool

> **Goal:** The engine core above the bridge: one `DocumentActor` thread per open document owning its MuPDF objects, immutable display lists as the only hand-off, a shared `RenderPool` rasterizing 512-pixel tiles with cancellation, and a shutdown order that provably drops every clone before the base context.

> **Current state:** Measured 2026-10-13. No code exists; D01 T01 will provide the locked base context, `rc_status` and the document description API this file builds on. The design is fixed by foundation plan sections 3.3 (core types), 4.5 (thread map), 4.6 (open and viewing flow), 4.7 (tile rendering, with the reference `rc_render_tile`), 4.9 (conversion isolation, consumed by D04) and 4.10 (shutdown order). The presentation thread and GPU side live in D02 T02; this file ends at CPU tiles delivered through a callback. This file is `frozen: true`.

## Inputs

- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3, 4.2 (rules 3 to 7), 4.5, 4.6, 4.7, 4.10, 4.11 (the battery D01 T03 §3 runs over this file's output).
- `src/bridge/recto_bridge.h` as built by D01 T01 (the only MuPDF-adjacent include `recto-core` may see is none: core types wrap the bridge's C structs).
- -> XREF: D02 T02 §2 -- the viewer consumes `Tile`s from this file's scheduler and owns their GPU textures; the tile hand-off shape (key, stride, premultiplied BGRA) is agreed here and rendered there.

## Outcome

- Opening a document yields a `DocumentHandle` the UI thread can post requests to without ever blocking on MuPDF.
- Pages render to deterministic tiles from many workers at once, cancellable mid-render with nothing partial ever cached.
- Closing documents in any order, with renders in flight, leaks nothing and drops the base context last.

**Adjacency:** all=not-applicable (an engine core with no records or surfaces; its settings are the cache budgets named in sections 2 and 3 as parameters with consumers in this file, and its reverse is the shutdown order in section 4)

**Adjacency rationale:** The budgets a reader might call settings (display-list cache size, worker count, store bytes) are constructor parameters owned and consumed inside this file, surfaced to the user only later through the Settings page (D02 T04 §2), which owns the tunable. Cancellation is the reverse of every render request and is built into section 3 rather than declared as a separate surface.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | DocumentActor and DocumentHandle | D01 T01 §3 | [ ] |
| 2 | §2 | Display list cache and tile rendering | §1 | [ ] |
| 3 | §3 | Scheduler, priorities and cancellation | §2 | [ ] |
| 4 | §4 | Shutdown order and lifecycle proof | §3 | [ ] |

## 1. DocumentActor and DocumentHandle

Plan rule 3: a document and its pages are used only on their owning thread. `DocumentActor` is that thread: it clones the base context at start, opens the document through the bridge, answers description and password requests from its message queue, and is the only code that ever touches `fz_document` and `fz_page` for its document (through bridge calls on its own clone). `DocumentHandle` is the copyable, UI-safe reference that posts to it (plan 3.3, 4.6).

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/core/document_actor.cpp` and `src/core/document_handle.h`: actor thread with a bounded request queue (open, authenticate, describe, load-page-for-display-list (filled in §2), close), each request completing through a future or callback that never blocks the posting thread on MuPDF. Done when: a test opens 20 corpus documents at once through handles from the main thread and reads back 20 correct page counts.
2. Thread naming and rule enforcement: the actor registers its thread name with the bridge log (D01 T01 §2) and, in Debug, asserts bridge document calls arrive on the actor thread. Done when: a deliberate wrong-thread call in a Debug test trips the assert instead of corrupting state.

- [ ] `DocumentActor` with queue, clone lifecycle and close. Done when: open, authenticate and describe round-trip for the starter corpus including the encrypted file. Cheaper substitute: mutex-guarded document access from any thread, refused by plan rule 3.
- [ ] `DocumentHandle` as the only UI-facing reference. Done when: `recto-services` and the future app can hold, copy and post through handles without including an actor or bridge header.
- [ ] Commit: `core: DocumentActor thread and UI-safe DocumentHandle`

**Freeze check:** Actor isolation: `Actor.TwentyDocumentsConcurrent` in `recto-tests` opens, describes and closes 20 documents in random order under ASan with zero findings, and the Debug wrong-thread assert fires on demand. Both tests are committed fixtures of the frozen contract.

**Test checkpoint:** `recto-tests --gtest_filter=Actor.*` passes under ASan (quote document count and zero findings).

## 2. Display list cache and tile rendering

Plan rules 4 to 6 and section 4.7: the actor builds an `fz_display_list` per page (annotations and widgets included), cached LRU at about 16 pages; workers render tiles from kept display-list references with their own clones, into BGRA premultiplied pixmaps whose bbox is the tile rect in device space, 512 by 512 device pixels, stride always taken from `fz_pixmap_stride`. This section covers the bridge render function and the core `DisplayList`, `TileKey` and `Tile` types; scheduling policy is §3.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `src/bridge/render.c` with `rc_build_display_list` (actor side) and `rc_render_tile` exactly per plan 4.7, including opaque-white clear and cookie pass-through. Done when: one worker renders a known corpus page and the pixels match a single-threaded reference render byte for byte.
2. Add `src/core/display_list.h`, `src/core/tile.h` (`TileKey` with document, page, zoom bucket, rotation, tile coordinates, render flags) and the actor-side LRU display-list cache. Done when: 8 workers render the same display list 1000 times and every tile matches the reference (the first 4.11 battery row, run in full by D01 T03 §3).

- [ ] `rc_render_tile` and display-list build in the bridge. Done when: aborts mid-render return an incomplete status and no pixmap, never a partial one. Cheaper substitute: rendering whole pages and cropping, which reintroduces the memory and latency blow-up tiles exist to prevent.
- [ ] `DisplayList` ref-counted hand-off and the 16-page LRU cache. Done when: workers dropping lists the actor created is the normal path in tests, leak-free under ASan.
- [ ] Commit: `core: display list cache and deterministic tile rendering`

**Freeze check:** Display lists are the only page data crossing threads, and keep/drop are the only cross-thread operations on them: `Render.SameListManyWorkersDeterministic` compares 8-worker output against a single-threaded render over the render corpus, byte-identical, under ASan.

**Test checkpoint:** `recto-tests --gtest_filter=Render.*` passes under ASan with deterministic multi-worker output (quote tile count compared).

## 3. Scheduler, priorities and cancellation

The scheduler turns viewport needs into tile work in the plan's priority order (4.6 step 5): visible tiles at the current zoom, visible tiles at neighbouring zoom buckets, next and previous pages, thumbnails. Cancellation runs through `CancelToken` over `fz_cookie`: abort is written through an interlocked mirror, and a tile whose cookie reports abort or incomplete is discarded and never cached. The `RenderPool` is `max(2, cores - 2)` workers shared by all documents, each cloning its context at thread start and dropping it on exit.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Write `src/core/render_pool.cpp` and `src/core/scheduler.cpp`: priority queues per the plan order, zoom buckets as discrete render levels, request coalescing by `TileKey`, and completion delivery through a callback the consumer registers (D02 T02 §2 is the first). Done when: a scripted viewport trace in tests receives visible tiles before any prefetch tile, in order.
2. Cancellation: viewport changes retire queued and running requests by key set; retired running renders abort through the cookie and deliver nothing. Done when: randomly cancelling tiles during rendering (battery row 3) leaks nothing and caches nothing partial, asserted by cache inspection in tests.

- [ ] Priority scheduler with zoom buckets and coalescing. Done when: duplicate concurrent requests for one `TileKey` produce one render and two deliveries. Cheaper substitute: FIFO rendering, which starves the visible page behind prefetch.
- [ ] `CancelToken` abort path with interlocked mirror. Done when: an aborted tile's key is absent from every cache after the abort test.
- [ ] Commit: `core: render scheduler with priorities and cancellation`

**Freeze check:** Workers never touch an `fz_document` or `fz_page`: the pool's translation units include no bridge document API, and `Render.CancelRandomTiles` (battery row 3) passes under ASan with zero cached partials. The include audit is part of the section's evidence.

**Test checkpoint:** `recto-tests --gtest_filter=Render.*:Scheduler.*` passes under ASan, including the random-cancel test (quote cancels issued and cache misses verified).

## 4. Shutdown order and lifecycle proof

Plan 4.10 fixes the order: cancel every cookie, stop the scheduler and join workers (each dropping its clone), join conversion jobs (D04 plugs into the same sequence), actors drop display lists, pages, document and clone then join, presentation resources release (D02's slot in the sequence), base context last, with a Debug assert that live clones are zero before the final drop. This section implements the runtime side and proves it under load.

**Phase:** 1
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement ordered shutdown in `src/core/runtime.cpp` (`Runtime` from plan 3.3): cancel-all, pool stop and join, actor close and join, base drop, with join points other owners (conversion jobs, presentation) register into. Done when: shutdown with renders in flight and documents open completes within a bounded time in tests, every run.
2. Lifecycle soak test: open, scroll-simulate and close the 20-document set in random order with cancellation noise, repeatedly. Done when: the soak passes under ASan with the live-clone assert holding at every shutdown.

- [ ] Ordered `Runtime` shutdown with registration points for later owners. Done when: no owner can outlive the base context without tripping the Debug assert. Cheaper substitute: process exit as the shutdown strategy, which leaks in every embedding and test.
- [ ] Soak test over random open, render, cancel and close. Done when: 100 iterations pass under ASan with stable memory (quote iteration count and peak).
- [ ] Commit: `core: ordered shutdown with live-clone proof`

**Freeze check:** Clones die before the base, always: the soak test plus the D01 T01 §1 live-clone counter assert zero at every `Runtime` destruction, under ASan, and the assert is compiled into Debug permanently, not test-only.

**Test checkpoint:** `recto-tests --gtest_filter=Runtime.*` passes under ASan with the soak green (quote iterations and shutdown time bound).

## Verification

- The 4.11 battery rows owned here (deterministic shared display lists, 20-document isolation, random cancellation) pass under ASan; D01 T03 §3 re-runs them as one suite.
- Include audit: no translation unit outside `src/bridge` names a MuPDF type; workers name no document API.
- `python scripts/todo-graph.py validate` passes.
