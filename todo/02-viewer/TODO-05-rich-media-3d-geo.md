---
schema_version: 1
id: rich-media-3d-geo
domain: 02-viewer
status: active
title: "TODO-05 -- Rich Media, 3D Models and Geospatial PDFs"
---

# TODO-05 -- Rich Media, 3D Models and Geospatial PDFs

> **Goal:** Match Acrobat Pro for the three content kinds Recto cannot show today. Rich media (`/RichMedia`, `/Screen` with media renditions, `/Sound`, `/Movie`) plays embedded audio and video only after the user says yes, through Windows media APIs, and saves any embedded media as a file; legacy Flash is never run and is explained. 3D annotations (U3D and PRC) open in a Direct3D 11 viewer with orbit, pan, zoom, named views, a model tree, part visibility, lighting presets and a cross section, over a poster image fallback. Geospatial PDFs report latitude and longitude under the pointer, find a location, measure geodesic distance and area, and export coordinates. No script ever runs, nothing leaves the machine, and every parser is bounded and fuzzed.

> **Current state:** Measured 2026-10-21. `D01 T01 §10` resolves pages, `/Annots` and links; `D02 T02 §1` creates the Direct3D 11 device and presentation thread; `D02 T02 §5` and `§9` provide `PageLayoutGeometry` (`viewport_to_page_point`) and the `"Open this link?"` prompt; `D02 T03 §8` provides the embedded-file filename sanitiser. No code parses `/RichMedia`, `/Screen`, `/Sound`, `/Movie`, `/3D`, U3D, PRC, `/VP` or `/Measure`; a PDF that uses them shows its poster appearance or a blank box with no explanation. `tests/corpus/` has no rich media, 3D or geospatial fixture, and `fuzz/` has no media, 3D or geo target. Every unknown annotation subtype keeps its existing behaviour (preserved on save, appearance drawn) until a section below changes it.

## Inputs

- `AGENTS.md` architecture rules (hostile input, one `DocumentActor` per document, UI thread never parses, jobs open their own `recto::pdf::Document`, atomic writes, links ask first), `design/SPEC.md` sections 4 (tokens), 6 (glyphs), 8.2 and 8.3 (toolbars and panels), 9.7 (Settings), 9.9 (confirmation dialog chrome), 10 (keyboard).
- `docs/plans/2026-10-13-recto-foundation.md` sections 3.3, 4 (actor and render contract), 9 (security), decisions D3, D9 (no PDF JavaScript), D12, D16 (offline by default), D17 (one shell) and D18 (Acrobat Pro parity).
- ISO 32000-1 sections 12.5.6.18 to 12.5.6.20 (Screen, Movie, Sound annotations), 13.2 to 13.3 (sounds, movies), 13.6 (3D artwork), ISO 32000-2 sections 12.5.6.22 (RichMedia) and 12.9 (geospatial features: viewports, `/Measure` `/GEO`), ECMA-363 (U3D) and ISO 14739-1 (PRC).
- Windows APIs: `Windows.Media.Playback.MediaPlayer`, `Windows.Media.Core.MediaSource` (`CreateFromStream`), `Windows.Storage.Streams.InMemoryRandomAccessStream`, Direct3D 11 (`ID3D11Device`, `IDXGISwapChain1`), `Windows.Storage.Pickers.FileSavePicker`.
- `todo/06-quality/TODO-03-user-failure-gates.md`: the hostile-number fixtures (`D06 T03 §1`) are the template for the fixtures here.
- -> XREF: D02 T02 §9 -- external media references go through the ask-first link prompt.
- -> XREF: D02 T02 §1 -- the 3D renderer builds on the Direct3D 11 device and presentation thread.
- -> XREF: D06 T02 §3 -- media, U3D, PRC and geospatial fuzz targets join the scheduled campaign.
- -> XREF: D03 T04 §3 -- staged edit session.
- -> XREF: D06 T04 §7 -- the `D-RichMedia.png` board and `design/SPEC.md` section 9.12 that §4, §9, §10, §13, §14, §17 here build against.

## Outcome

- A `/Screen`, `/RichMedia`, `/Sound` or `/Movie` annotation shows its poster with a play control; pressing it asks `Play this media?` naming the file type and size; on yes the embedded audio or video plays in place through `MediaPlayer` from the embedded stream only, and `Save media as...` writes it to a file. Flash (`.swf`) shows a placeholder saying Flash is unsupported with a `Save as file` action; no embedded script runs.
- A `/3D` annotation shows its poster image; clicking it opens the 3D view where the model can be orbited, panned and zoomed, switched between named views, shown part by part from a model tree, lit by a preset, and cut by a cross section. Corrupt or oversized models fall back to the poster with a stated reason. 3D JavaScript never runs.
- A PDF with a geospatial viewport shows `Lat, Lon` under the pointer, finds a typed location, measures geodesic distance and area, and exports clicked or measured points as CSV and GeoJSON.
- U3D, PRC, media and geo parsers return `PdfResult`, bound recursion and allocation, and are covered by corpus fixtures and libFuzzer targets in the scheduled campaign.
- A user can embed a local audio or video file in a page as a `/Screen` annotation with a poster, from the app or `recto-cli media add`, without any script, external reference or autoplay (§16 to §18).

**Adjacency:** list=applicable @ D02 T05 §10; document=applicable @ D02 T05 §4; settings=not-applicable (play-on-ask and script-never-runs are fixed security rules, not options; no setting would be read); reporting=not-applicable (no report is produced); notifications=applicable @ D02 T05 §4; permissions=applicable @ D02 T05 §4; audit=not-applicable (nothing about the user's action is persisted); exchange=applicable @ D02 T05 §14; reverse=applicable @ D02 T05 §9

**Adjacency rationale:** The list surface is the 3D model tree and named views (`§10`). Document is saving embedded media to a file (`§4`). Notifications are the Flash placeholder, the unsupported-codec message and the 3D poster-fallback reason (`§4`, `§9`). Permissions are the ask-before-play dialog (`§4`). Exchange is the coordinate export to CSV and GeoJSON (`§14`). Reverse is closing the 3D view back to the poster and stopping playback (`§4`, `§9`). Settings do not apply: making ask-first optional would weaken a security rule, so no row exists to be read.

## Implementation Order

| Order | Section | Deliverable | Depends On | Status |
| ----- | ------- | ----------- | ---------- | ------ |
| 1 | §1 | Rich media, 3D and geospatial corpus fixtures in `tests/corpus/rich/` | D00 T03 §3 | [ ] |
| 2 | §2 | `recto-pdf` media model: `/RichMedia`, `/Screen`, `/Sound`, `/Movie`, bounded extract | §1, D01 T01 §10 | [ ] |
| 3 | §3 | Embedded media playback service on Windows media APIs with no network access | §2, D01 T02 §8 | [ ] |
| 4 | §4 | Media annotation surface: ask before play, Flash placeholder, `Save media as...` | §3, D02 T02 §9, D02 T03 §8, D06 T04 §7 | [ ] |
| 5 | §5 | `recto-pdf` 3D model: `/3D` annotations, `/3DV` views, `/VA`, lighting, cross sections | §1, §2, D01 T01 §10 | [ ] |
| 6 | §6 | Bounded U3D (ECMA-363) parser into `Scene3D` and `fuzz_u3d` | §5 | [ ] |
| 7 | §7 | Bounded PRC (ISO 14739-1) tessellation parser into `Scene3D` and `fuzz_prc` | §5 | [ ] |
| 8 | §8 | Direct3D 11 `Scene3DRenderer`: camera, lighting presets, visibility, cross section | §6, §7, D02 T02 §1 | [ ] |
| 9 | §9 | 3D view surface: poster fallback, activation, orbit, pan, zoom | §8, D02 T02 §9, D06 T04 §7 | [ ] |
| 10 | §10 | 3D panel: named views, model tree, show/hide parts, lighting, cross section | §9, D02 T03 §3, D02 T03 §11, D06 T04 §7 | [ ] |
| 11 | §11 | `recto-pdf` geospatial model: `/VP`, `/Measure` `/GEO` and page-to-coordinate transform | §1, D01 T01 §10 | [ ] |
| 12 | §12 | Geodesic distance and area on the ellipsoid (`geodesy.cpp`) | -- | [ ] |
| 13 | §13 | Geo surface: coordinates under the pointer and `Find location` | §11, D02 T02 §5, D02 T03 §11, D06 T04 §7 | [ ] |
| 14 | §14 | Geo measure tools and coordinate export (CSV, GeoJSON) | §12, §13, D06 T04 §7 | [ ] |
| 15 | §15 | Register `fuzz_media`, `fuzz_u3d`, `fuzz_prc`, `fuzz_geo` in the fuzz campaign and audit | §2, §6, §7, §11, D06 T02 §3 | [ ] |
| 16 | §16 | `recto-pdf` media authoring: embed audio or video as a `/Screen` annotation | §2, D03 T04 §3, D01 T02 §5 | [ ] |
| 17 | §17 | Add audio or video tool: pick a file, place it, choose a poster, preview | §3, §4, §16, D03 T03 §4, D02 T01 §10, D06 T04 §7 | [ ] |
| 18 | §18 | `recto-cli media`: add and list embedded audio and video | §2, §16, D01 T03 §1 | [ ] |

## 1. Rich media, 3D and geospatial corpus fixtures in `tests/corpus/rich/`

Every parser and surface below needs reproducible inputs, including hostile ones. The fixtures are generated by `scripts/build-corpus.py` (standard library only, fixed timestamps), listed in `tests/corpus/PROVENANCE.md`, and never edited by hand. This section must not change any existing fixture or the `Corpus.Provenance` rule.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Extend `scripts/build-corpus.py` to emit `tests/corpus/rich/` with the files named below and register each in `PROVENANCE.md`. Done when: `python scripts/build-corpus.py --check` exits 0 and `Corpus.Provenance` lists every new file.
2. Document the folder, each file's expected parse result and its hostile property in `tests/corpus/README.md`. Done when: the README table names `rich/` with one row per file.

- [ ] Generate `screen-rendition-wav.pdf` (a `/Screen` annotation with a `/Rendition` action `/S /Rendition /OP 0` and a media rendition `/R` whose clip `/D` is an embedded 1 s PCM `.wav` stream) and `sound-annot.pdf` (a `/Sound` annotation with a raw `/Sound` stream `/R 8000 /C 1 /B 8 /E /Unsigned`). Done when: both parse with `pdf::Document` and the audio bytes are present.
- [ ] Generate `movie-annot.pdf` (a `/Movie` annotation whose `/Movie` dictionary `/F` is an embedded filespec holding a tiny MPEG-4 stream) and `richmedia-video.pdf` (a `/RichMedia` annotation with `/RichMediaContent` `/Assets` and a `/Configurations` instance `/Subtype /Video`). Done when: both parse and the streams are present.
- [ ] Generate `richmedia-flash.pdf` (a `/RichMedia` instance `/Subtype /Flash` with an embedded `.swf` stream starting `FWS`) and `screen-javascript.pdf` (a `/Screen` annotation with a `/JavaScript` action and a rendition `/AN` script). Done when: both parse and contain the marker bytes the tests look for.
- [ ] Generate `external-url-media.pdf` (a rendition clip `/D` filespec of `/FS /URL` pointing at `https://example.invalid/clip.mp4`) and `media-bomb.pdf` (a Flate-bombed media stream expanding past 512 MB). Done when: the first parses and the second is a 10 KB file whose stream decodes past the cap.
- [ ] Generate `model-u3d.pdf` (a `/3D` annotation, `/3DD` stream `/Subtype /U3D` holding a valid minimal U3D file with one box mesh, one material and one light, a `/VA` array of two named views `Front` and `Top`, an `/AP` poster) and `model-prc.pdf` (the same shape with `/Subtype /PRC` holding a minimal PRC file with one tessellated part). Done when: both parse and the streams start with the format magic.
- [ ] Generate `model-u3d-clod.pdf` (a U3D mesh with a coarse base and resolution update blocks growing it to 2000 triangles). Done when: the file parses and its stream starts with the U3D header.
- [ ] Generate hostile 3D files `u3d-block-bomb.pdf` (a block header claiming a 4 GB data size), `u3d-deep-nodes.pdf` (nodes nested 200 deep), `prc-bad-offsets.pdf` (section offsets beyond the stream), and `model-js.pdf` (a 3D stream with `/OnInstantiate` holding JavaScript). Done when: each file is present and its hostile property is recorded in the README.
- [ ] Generate `geo-map.pdf` (page 1 with a `/VP` whose `/BBox` covers a map region and `/Measure` `/Subtype /GEO` with `/GPTS` of four WGS84 latitude and longitude pairs, matching `/LPTS`, `/GCS` `/Type /GEOGCS` `/EPSG 4326`, `/PDU [/KM /SQKM /DEG]`), `geo-projected.pdf` (the same with `/GCS /Type /PROJCS` and a `/WKT` string), and `geo-bad.pdf` (`/GPTS` with five points, NaN-like values and `/LPTS` of the wrong length). Done when: the first two parse and the third is rejected by the §11 test.
- [ ] Commit: `corpus: add rich media, 3D and geospatial fixtures`

**Test checkpoint:** `python scripts/build-corpus.py --check` exits 0 and `recto-tests.exe --gtest_filter=Corpus.Provenance` passes with the new files listed.

## 2. `recto-pdf` media model: `/RichMedia`, `/Screen`, `/Sound`, `/Movie`, bounded extract

Parse the four media annotation kinds into one `MediaItem` model so the playback service and the surface never touch PDF dictionaries. The parser reads data only: it never executes a rendition action's script, never resolves an external file or URL, and never starts anything. It matches Acrobat Pro's media annotations while keeping zero PDF JavaScript (D9) and offline by default (D16).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `MediaItem { kind, mime, extension, name, poster_page_rect, source, byte_size, supported, unsupported_reason }` (`kind` in `Audio`, `Video`, `Flash`, `Other`; `source` in `Embedded(stream)`, `ExternalRef`) in `src/pdf/media.h` and implement `parse_media_annotations(page)` returning `PdfResult<std::vector<MediaItem>>` in `src/pdf/media.cpp`. Done when: `PdfMedia.*` tests return the expected items for the §1 fixtures.
2. Implement the bounded stream extractor `extract_media_bytes(item, max_bytes)` through the `D01 T01 §7` filter pipeline with the 256 MB ceiling and a 512 MB application cap. Done when: `PdfMedia.MediaBombRefused` returns a typed `PdfError` for `media-bomb.pdf` without allocating past the cap.

- [ ] Parse `/Screen` annotations: follow `/A` (and `/AA`) only to read `/S /Rendition` actions, `/R` renditions of type `/MR` (media) with `/C` media clip data `/D` and `/CT`, and `/OP`; read `/S /JavaScript` actions only to record `has_script = true`. Done when: `PdfMedia.ScreenRendition` returns the `.wav` item and `PdfMedia.ScreenScriptIgnored` returns `has_script = true` with no script text kept.
- [ ] Parse `/RichMedia` annotations through `/RichMediaContent` (`/Assets` name tree, `/Configurations`, `/Instances` with `/Subtype` `/3D`, `/Video`, `/Sound`, `/Flash`) and `/RichMediaSettings`; hand 3D instances to §5 and classify `/Flash` as `Flash` with `supported = false` and the reason `Adobe Flash is unsupported`. Done when: `PdfMedia.RichMediaVideoAndFlash` returns one supported video and one unsupported Flash item.
- [ ] Parse `/Sound` annotations and `/Movie` annotations, wrapping raw sound data (`/R`, `/C`, `/B`, `/E` of `Raw`, `Signed`, `Unsigned`, `muLaw`, `ALaw`) in an in-memory RIFF WAVE header and leaving compressed `/Sound` `/Filter` data to the media pipeline. Done when: `PdfMedia.SoundWav` yields bytes that start with `RIFF....WAVEfmt ` and the right sample rate.
- [ ] Mark any filespec that is `/FS /URL`, a relative or absolute path, or a UNC path as `ExternalRef` with `supported = false` and the reason `Media is stored outside this PDF`; never read it. Done when: `PdfMedia.ExternalRefNeverRead` over `external-url-media.pdf` performs zero file and network calls (a test hook counts them).
- [ ] Sniff the stream magic bytes (`FWS`, `CWS`, `ZWS` for SWF) and trust the sniff over a lying `/CT`. Done when: `PdfMedia.SniffBeatsLabel` classifies an `.swf` labelled `video/mp4` as `Flash`.
- [ ] Bound everything with one shared allocation budget: add `ParseBudget` in `src/pdf/parse_budget.h` (default 512 MB per parse call, charged with checked arithmetic for every decoded stream, name, intermediate buffer and retained model, released when freed, returning `PdfError::BudgetExceeded` when spent), plus at most 64 nesting levels, 1000 items per page and 1 MB per name. Done when: `PdfMedia.AggregateBudget` shows 1000 individually valid 1 MB names on one page fail with `BudgetExceeded` before the budget is passed, and the hostile cases in `PdfMedia.Bounds` return `PdfError` and never throw.
- [ ] Add `fuzz/fuzz_media.cpp` to `recto-fuzz.exe` driving `parse_media_annotations` and `extract_media_bytes` on arbitrary PDFs, with seeds under `tests/corpus/seeds/` built by `scripts/build-corpus.py`. Done when: `recto-fuzz.exe fuzz_media -runs=100000` reports no crash, leak or oversize allocation under ASan.
- [ ] Add `PdfMedia.*` tests in `tests/recto-tests/pdf_media_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfMedia.*` passes under ASan.
- [ ] Commit: `pdf: parse RichMedia, Screen, Sound and Movie annotations into MediaItem`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfMedia.*` passes under ASan; `PdfMedia.ScreenScriptIgnored` fails if any script text is kept and `PdfMedia.ExternalRefNeverRead` fails on any I/O.

## 3. Embedded media playback service on Windows media APIs with no network access

Play a `MediaItem` from its extracted bytes only. The service wraps `Windows.Media.Playback.MediaPlayer` and `MediaSource::CreateFromStream` over an `InMemoryRandomAccessStream`, so Windows decodes with its installed codecs and Recto adds no codec of its own. It cannot start from a URI, so a network fetch is impossible by construction. It runs in the app layer and is the only code in Recto that plays media.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `MediaPlaybackService` in `src/app/services/media_playback_service.h/.cpp` with `prepare(MediaItem, bytes)`, `play`, `pause`, `stop`, `seek`, `volume`, `state` events and `dispose`. Done when: `MediaPlayback.*` tests prepare and play a generated `.wav` and report `Playing` then `Ended`.
2. Make the service refuse anything not backed by bytes: no `Uri` overloads are used or exposed, and `ExternalRef`, `Flash` and unsupported items return a typed refusal. Done when: `MediaPlayback.RefusesExternalAndFlash` returns the refusal reasons from §2.

- [ ] Build the source only with `MediaSource::CreateFromStream(stream, contentType)` from the extracted bytes and a MIME type from the §2 sniff. Done when: `MediaPlayback.StreamOnly` asserts, by an API-use audit of the file with `rg "CreateFromUri|Uri\(" src/app/services/media_playback_service.cpp`, that no `Uri` construction exists.
- [ ] Map codec failures (`MediaPlayer::MediaFailed` error codes) to a user-readable reason `Windows cannot play this audio or video format.` and keep `Save media as...` available. Done when: `MediaPlayback.UnsupportedCodec` feeds a stream with an unknown codec and receives the reason and `state == Failed`.
- [ ] Own one `MediaPlayer` per active item, dispose on tab close, document close, navigation away, and when another item starts, releasing the in-memory stream. Done when: `MediaPlayback.LifetimeAndDispose` shows zero live players and zero stream bytes held after the tab closes.
- [ ] Never autoplay: `prepare` does not start playback and `play` requires a caller token produced only by the §4 confirmation. Done when: `MediaPlayback.NoAutoplay` shows `play` without a token returns `Refused` and a prepared item stays `Paused`.
- [ ] Run decoding off the UI thread (the `MediaPlayer` pipeline) and marshal state events through `DispatcherQueue::TryEnqueue`. Done when: the test records that no engine thread blocks the UI dispatcher.
- [ ] Add `MediaPlayback.*` tests in `tests/recto-tests/media_playback_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=MediaPlayback.*` passes.
- [ ] Commit: `app: add MediaPlaybackService that plays embedded media from memory only`

**Test checkpoint:** `recto-tests.exe --gtest_filter=MediaPlayback.*` passes; `MediaPlayback.NoAutoplay` fails if playback starts without the confirmation token.

## 4. Media annotation surface: ask before play, Flash placeholder, `Save media as...`

The user sees what an audio or video annotation is, decides whether to play it, and can always take the file out. Matches Acrobat Pro's media playback and `Save media`, and keeps Recto's rule that nothing embedded runs without consent. A Flash annotation is explained, not left as a blank box.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 9.9 (the confirmation `ContentDialog` and its glyph row), section 5 type styles and section 4 tokens, drawn over the page canvas of `D02 T02`. Design board: `design/SPEC.md` section 9.12 with `design/screenshots/D-RichMedia.png` (drawn by `D06 T04 §7`).
**Job:** The user opens a PDF with an embedded recording or clip, sees a poster with a play button, is asked once per item whether to play it, then watches or listens in place with pause, seek and volume. For a Flash item they read why nothing plays and can still extract the file. Consumer: `MediaPlaybackService` (§3), `MediaItem` (§2), `OpenDocumentPipeline` link prompt contract (`D02 T02 §9`), `AnnouncementService` (`D03 T02 §5`).
**Treatment:** An overlay per `MediaItem` on the page canvas: poster (the annotation `/AP /N` when present, otherwise the media rectangle outlined in `RectoBorderBrush` with a `24 epx` `RectoIcons` `video` or `speaker_2` glyph drawn directly on the overlay, no filled tile behind it), a `Play` button that opens a `ContentDialog` `Play this media?` stating `Audio` or `Video`, the file extension and size, and `Play` / `Cancel`; after consent a `MediaPlayerElement` with transport controls and a `More` flyout holding `Save media as...`. Flash and unsupported items show a bordered placeholder `Adobe Flash content is not supported. Recto never runs Flash.` with `Save as file`. Cheaper substitute that fails the checkpoint: autoplay, a play button that skips the dialog, or leaving Flash as an empty rectangle.
**Chrome:** consume `RectoDialogStyles.xaml` (`D02 T01 §10`), `RectoColors.xaml` tokens, Fluent UI System Icons (`RectoIcons`), `FileSavePicker`, the filename sanitiser of `D02 T03 §8`.

**Build order:**

1. Add `MediaAnnotationOverlay` in `src/app/overlays/` hosted over the page canvas by the document view, positioned from `PageLayoutGeometry` and re-laid-out on zoom and scroll. Done when: `Overlays.MediaPosition` finds the overlay at the annotation rectangle at 100 percent and 200 percent zoom.
2. Add the `Play this media?` dialog, the playing state, the Flash placeholder and `Save media as...`. Done when: `Overlays.MediaConsentAndPlay` drives consent, play and stop, and `Overlays.FlashPlaceholder` asserts the placeholder text.

- [ ] Show the consent dialog on every first play of an item in a tab session and remember `Play` only for that item until the tab closes; `Cancel` plays nothing. Done when: `Overlays.MediaConsentAndPlay` plays after `Play` and not after `Cancel`, and asks again after the tab is reopened. Cheaper substitute: a global remember-forever checkbox.
- [ ] Implement `Save media as...` with `FileSavePicker`, a suggested name from `MediaItem::name` passed through the `D02 T03 §8` sanitiser, and a temp-file-then-replace write of the exact embedded bytes. Done when: `Overlays.SaveMediaAs` saves a file whose SHA-256 equals the embedded stream's and a name of `..\..\evil.exe` is saved as `evil.exe`.
- [ ] Offer `Save as file` on Flash and unsupported items and the same action for audio and video that fail to decode. Done when: `Overlays.FlashPlaceholder` extracts the `.swf` bytes byte-for-byte and `Overlays.UnsupportedCodecSave` does the same.
- [ ] Show `This PDF stores the media outside the file. Recto does not open it automatically.` for `ExternalRef` items, with an `Open link...` button routed through the `D02 T02 §9` `"Open this link?"` prompt. Done when: `Overlays.ExternalRefUsesLinkPrompt` shows the link prompt and nothing opens before it is accepted.
- [ ] Register `Play media`, `Pause media` and `Save media as...` in the `CommandRegistry` so each is reachable from `Ctrl+K` while a media annotation is focused. Done when: `Commands.Reachability` finds the three commands. Cheaper substitute: overlay buttons only.
- [ ] Give the play, pause, seek, volume, `More` and `Save` controls an `AutomationProperties.Name` and tooltip, make the overlay keyboard-focusable with `Enter` to activate, and announce `Playing audio` and `Stopped` through `AnnouncementService`. Done when: `A11y.MediaOverlayInventory` finds no unnamed control.
- [ ] Use only `RectoColors.xaml` tokens and Fluent UI System Icons (`RectoIcons`). Done when: `rg "#[0-9A-Fa-f]{6}" src/app/overlays/MediaAnnotationOverlay.xaml` prints nothing.
- [ ] Add `Overlays.Media*` and `Overlays.FlashPlaceholder` tests in `tests/recto-tests/media_overlay_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Overlays.Media*:Overlays.FlashPlaceholder:Overlays.SaveMediaAs:Overlays.ExternalRefUsesLinkPrompt:Commands.Reachability` passes.
- [ ] Commit: `viewer: add media annotation overlay with ask-first play and Flash placeholder`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Overlays.Media*:Overlays.FlashPlaceholder:Overlays.SaveMediaAs` passes; `Overlays.MediaConsentAndPlay` fails if playback starts before the dialog is accepted.

## 5. `recto-pdf` 3D model: `/3D` annotations, `/3DV` views, `/VA`, lighting, cross sections

Describe a 3D annotation in one `Annot3D` model that the renderer and panel consume: the 3D stream (`/3DD`), its default view (`/3DV`), the view array (`/VA`), poster appearance, activation (`/3DA`), and per-view lighting scheme, cross sections and node overrides. It also defines `Scene3D`, the format-neutral scene (meshes, materials, lights, nodes) that §6 and §7 fill. Matches Acrobat Pro's 3D tools; 3D scripts never run (D9).

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Define `Annot3D`, `View3D { external_name, internal_name, camera_to_world, center_of_orbit, projection, background, render_mode, lighting_scheme, cross_sections, node_overrides }`, and `Scene3D { meshes, materials, lights, nodes, bounds, unsupported_notes }` in `src/pdf/model3d.h`. Done when: the header compiles in `recto-pdf` and `recto-core`.
2. Implement `parse_3d_annotations(page)` returning `PdfResult<std::vector<Annot3D>>` in `src/pdf/model3d.cpp`, with `/3DD` stream bytes extracted through the §2 bounded extractor. Done when: `Pdf3D.*` tests return the two named views `Front` and `Top` for `model-u3d.pdf`.

- [ ] Parse `/3DV` view dictionaries: `/XN`, `/IN`, `/MS` (`/M` matrix or `/U3D` path), `/C2W`, `/U3DPath`, `/CO`, `/P` projection (`/Subtype /P` perspective with `/FOV`, `/O` orthographic with `/OS`, `/PS` scaling), `/BG`, `/RM`, `/LS`, `/SA`, `/NA`. Done when: `Pdf3D.ViewFields` round-trips every field for a view written by the generator.
- [ ] Map the `/LS` lighting scheme names `None`, `White`, `Day`, `Night`, `Hard`, `Primary`, `Blue`, `Red`, `Cube`, `CAD`, `Headlamp` to a `LightingPreset` enum, with unknown names becoming `Default` plus an `unsupported_notes` entry. Done when: `Pdf3D.LightingNames` maps all eleven and an unknown name.
- [ ] Parse `/3DCrossSection` dictionaries (`/C` centre, `/O` orientation, `/PO` plane opacity, `/PC` plane colour, `/IV` intersection visible, `/IC` intersection colour) and `/3DNode` overrides (`/N`, `/O` opacity, `/V` visibility, `/M` matrix). Done when: `Pdf3D.CrossSectionAndNodes` reads both for the generated fixture.
- [ ] Read `/3DA` activation (`/A` activation event, `/AIS`, `/D`, `/DIS`) only to learn whether the file asks to open automatically, and never honour it: the surface (§9) always waits for a click. Done when: `Pdf3D.ActivationIgnored` shows `auto_activate` is recorded as a note but the model reports `requires_user_activation = true`.
- [ ] Ignore every script: `/OnInstantiate`, any `/JS` entry and any 3D-JavaScript stream are recorded as `has_script = true` and discarded. Done when: `Pdf3D.JavaScriptNeverRuns` over `model-js.pdf` returns `has_script = true`, retains no script text and executes nothing.
- [ ] Validate numbers: reject non-finite values, matrices that are singular, field of view outside 1 to 179 degrees and counts over 4096 views, 100000 nodes or 64 cross sections, with typed `PdfError`. Done when: `Pdf3D.HostileNumbers` rejects each case.
- [ ] Add `Pdf3D.*` tests in `tests/recto-tests/pdf_3d_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Pdf3D.*` passes under ASan.
- [ ] Commit: `pdf: parse 3D annotations, views, lighting and cross sections`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Pdf3D.*` passes under ASan; `Pdf3D.JavaScriptNeverRuns` fails if any script text is retained.

## 6. Bounded U3D (ECMA-363) parser into `Scene3D` and `fuzz_u3d`

Read the Universal 3D file inside a `/3DD` stream into `Scene3D`. The parser is the highest-risk code in this file: it takes a binary format with block sizes, bitstream decoding and node hierarchies from an untrusted PDF. Every length is checked against the stream size with checked arithmetic, every count against a cap, recursion is iterative or capped at 64, and nothing throws.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `parse_u3d(bytes, limits)` returning `PdfResult<Scene3D>` in `src/pdf/u3d.cpp`: verify the File Header block (type `0x00443355`), walk declaration and continuation blocks by their type, data size and metadata size with 4-byte padding, and fill `Scene3D` from the node (group, model, light, view), mesh, material and shader resource blocks, applying the progressive resolution update (continuation) blocks of ECMA-363 so each mesh reaches its final resolution. Done when: `U3D.MinimalBox` yields one mesh of 12 triangles, one material and one light from the §1 `model-u3d.pdf` stream, and `U3D.ClodFinalResolution` yields the full vertex and triangle count of `model-u3d-clod.pdf`, whose base mesh holds a fraction of them.
2. Add the limits and hostile tests. Done when: `U3D.Hostile*` return typed errors for `u3d-block-bomb.pdf`, `u3d-deep-nodes.pdf`, truncated files and zero-length blocks.

- [ ] Define `U3dLimits` charged against the caller's `ParseBudget` (§2) together with the retained `Scene3D` (default: 64 MB input, 200000 blocks, 8 million vertices, 8 million triangles, 100000 nodes, depth 64, 1 million total decoded bitstream symbols per block) in `src/pdf/u3d.h`. Done when: each limit trips its own `U3D.Limit*` test.
- [ ] Decode the mesh base geometry (positions, normals, per-vertex or per-face materials, texture coordinates where present), then apply the resolution update blocks in order up to the final resolution. A malformed update block (bad size, index out of range, inconsistent counts) is malformed input and makes `parse_u3d` return a typed `PdfError::MalformedInput` naming the block, never a successful model; a well-formed update of a kind this parser does not implement is valid but unsupported, so the mesh stays at the last applied resolution, `Scene3D::reduced_detail = true` is set and an `unsupported_notes` entry makes the UI show `Model shown at reduced detail`. Done when: `U3D.ClodFinalResolution` reaches the final counts, `U3D.ClodBadUpdate` returns `MalformedInput` and no `Scene3D`, and `U3D.ClodUnsupportedUpdate` returns the intermediate mesh with `reduced_detail = true`; in the app a `MalformedInput` model falls back to the poster with its reason (`Overlays.Model3DFallback`).
- [ ] Resolve node parents with an iterative walk and a visited set; reject cycles and orphan references as `PdfError`. Done when: `U3D.NodeCycle` rejects a parent loop and `U3D.DeepNodes` rejects 200-deep nesting.
- [ ] Reject unknown required block types with `unsupported_notes` for optional blocks and `PdfError` for structural ones; unknown optional blocks are skipped by size. Done when: `U3D.UnknownBlocks` skips an unknown optional block and fails on a bad header.
- [ ] Never read outside the stream: every read goes through one bounds-checked reader. Done when: `U3D.NoOverread` runs the whole corpus plus truncations at every byte offset under ASan with zero findings.
- [ ] Add `fuzz/fuzz_u3d.cpp` to `recto-fuzz.exe`, seeds from `tests/corpus/seeds/` built by `scripts/build-corpus.py`. Done when: `recto-fuzz.exe fuzz_u3d -runs=200000` reports no crash, leak or allocation above 256 MB under ASan.
- [ ] Add `U3D.*` tests in `tests/recto-tests/u3d_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=U3D.*` passes under ASan.
- [ ] Commit: `pdf: add bounded U3D parser and fuzz_u3d`

**Test checkpoint:** `recto-tests.exe --gtest_filter=U3D.*` passes under ASan; `U3D.NoOverread` fails on any out-of-bounds read.

## 7. Bounded PRC (ISO 14739-1) tessellation parser into `Scene3D` and `fuzz_prc`

Read the Product Representation Compact file inside a `/3DD` stream into the same `Scene3D`. Scope is the product structure tree (assemblies, parts, names, matrices, colours and visibility) and stored tessellations (triangle meshes with normals), which is what a PDF viewer draws. B-rep parts without a stored tessellation are listed in the model tree with the note `No preview geometry` and drawn as their bounding box; PRC files that store tessellation in the compressed form are listed with `Compressed tessellation is not supported` and fall back to the poster. Those limits are stated in the UI, not hidden.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `parse_prc(bytes, limits)` returning `PdfResult<Scene3D>` in `src/pdf/prc.cpp`: check the file signature and versions, read the file-structure sections (globals, tree, tessellation) through a bounds-checked bit and byte reader, and build the part hierarchy and meshes. Done when: `Prc.MinimalPart` yields one mesh from the §1 `model-prc.pdf` stream.
2. Add the limits and hostile tests. Done when: `Prc.Hostile*` return typed errors for `prc-bad-offsets.pdf`, truncations and absurd counts.

- [ ] Define `PrcLimits` charged against the caller's `ParseBudget` (§2) together with the retained `Scene3D` (default: 64 MB input, 8 million vertices, 8 million triangles, 100000 tree nodes, depth 64, 4096 sections) in `src/pdf/prc.h`. Done when: each limit trips its own `Prc.Limit*` test.
- [ ] Walk the product occurrence tree iteratively, composing transformation matrices with a finite and non-singular check, and carrying names, colours and visibility. Done when: `Prc.AssemblyTransforms` places two instances of one part at the expected positions.
- [ ] Decode stored (uncompressed) tessellation faces and normals into triangle lists, honouring triangle fans and strips, and mark compressed tessellation and B-rep-only parts with the stated notes. Done when: `Prc.TessellationKinds` draws a strip and a fan correctly and reports one compressed and one B-rep-only part.
- [ ] Validate every offset and index against its container, rejecting indices outside the vertex array and sections past the stream end. Done when: `Prc.BadOffsets` and `Prc.IndexRange` return `PdfError`.
- [ ] Add `fuzz/fuzz_prc.cpp` to `recto-fuzz.exe`, seeds from `tests/corpus/seeds/`. Done when: `recto-fuzz.exe fuzz_prc -runs=200000` reports no crash, leak or allocation above 256 MB under ASan.
- [ ] Add `Prc.*` tests in `tests/recto-tests/prc_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Prc.*` passes under ASan.
- [ ] Commit: `pdf: add bounded PRC tessellation parser and fuzz_prc`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Prc.*` passes under ASan; `Prc.BadOffsets` fails if an out-of-range offset is read.

## 8. Direct3D 11 `Scene3DRenderer`: camera, lighting presets, visibility, cross section

Draw a `Scene3D` with Direct3D 11 into a render target the 3D view presents. The renderer owns the camera, lighting presets, per-node visibility and opacity, and a clipping-plane cross section. It uses its own `ID3D11Device` resources on the presentation thread of `D02 T02 §1`, uploads meshes once, never parses a file, and caps GPU memory so a large model degrades instead of crashing.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** no

**Build order:**

1. Implement `Scene3DRenderer` in `src/app/render3d/scene3d_renderer.h/.cpp`: create vertex and index buffers from `Scene3D`, a depth buffer, a constant buffer (camera, lights, clip plane) and HLSL shaders compiled at build time (`.hlsl` to `.cso`). Done when: `Render3D.BoxGolden` renders the §1 box to a `256 x 256` texture matching `tests/corpus/rich/goldens/box-front.png` within the `D06 T01 §1` tolerance.
2. Add camera, lighting, visibility and cross section. Done when: `Render3D.*` goldens pass for each lighting preset, a hidden node and a cross section.

- [ ] Implement the camera (`Camera3D`) as orbit around `center_of_orbit` with yaw, pitch, dolly and pan, perspective (`/FOV`) and orthographic (`/OS`) projections, and `fit_to_bounds`. Done when: `Render3D.CameraFitsBounds` leaves the model inside the frustum for a 1000-unit and a 0.001-unit model.
- [ ] Implement the eleven lighting presets as light rigs in `src/app/render3d/lighting.cpp` (`Headlamp` follows the camera) plus the file's own `Scene3D` lights when the preset is `File`. Done when: `Render3D.LightingGoldens` matches a golden per preset and `Render3D.HeadlampFollowsCamera` moves the light with the camera.
- [ ] Implement per-node visibility, opacity (sorted back-to-front for transparent nodes) and render modes `Solid`, `Wireframe`, `SolidWireframe`, `Transparent`, `BoundingBox`. Done when: `Render3D.VisibilityAndModes` goldens match.
- [ ] Implement the cross section as a user clip plane with a cut-cap fill and plane outline from the §5 cross-section fields. Done when: `Render3D.CrossSection` golden shows the cut and moving the plane changes the result.
- [ ] Enforce budgets: refuse meshes above 512 MB of GPU buffers, release on device-lost and recreate, and render a decimated bounding-box stand-in for over-budget models with the reason surfaced to the caller. Done when: `Render3D.OverBudgetFallsBack` returns the fallback reason and `Render3D.DeviceLost` recovers after a forced device removal.
- [ ] Render off the UI thread on the presentation thread and cancel in-flight frames when the view closes. Done when: `Render3D.NoUiThreadWork` proves no D3D call is made on the UI thread.
- [ ] Add `Render3D.*` tests and goldens in `tests/recto-tests/render3d_tests.cpp` and `tests/corpus/rich/goldens/` (registered in `PROVENANCE.md` by `scripts/build-corpus.py`). Done when: `recto-tests.exe --gtest_filter=Render3D.*` passes.
- [ ] Commit: `app: add Direct3D 11 Scene3DRenderer with lighting presets and cross section`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Render3D.*` passes; `Render3D.BoxGolden` fails if the box is mis-drawn and `Render3D.OverBudgetFallsBack` fails if an over-budget model allocates.

## 9. 3D view surface: poster fallback, activation, orbit, pan, zoom

The user sees a 3D annotation as its poster, clicks to activate it, and moves around the model. Matches Acrobat Pro's 3D toolbar for navigation and keeps a fallback for any model Recto cannot draw.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` sections 4 (tokens), 6 (glyphs), 8.2 (the floating tool bar pattern) and 9.9 (dialog chrome), drawn over the page canvas of `D02 T02` in all three layout presets (D17). Design board: `design/SPEC.md` section 9.12 with `design/screenshots/D-RichMedia.png` (drawn by `D06 T04 §7`).
**Job:** The user opens an engineering drawing or product sheet with a 3D model, clicks the poster, rotates and zooms the model with mouse, touch or keyboard, and returns to the page with one action. Models that cannot be drawn say why and keep the poster. Consumer: `Scene3DRenderer` (§8), `Annot3D` (§5), `CommandRegistry` (`D02 T03 §11`).
**Treatment:** A `Model3DView` control (`SwapChainPanel`) that replaces the poster in place on activation, with mouse drag to orbit, middle or `Shift` drag to pan, wheel and pinch to zoom, double-click to fit, arrow keys to orbit, `+`/`-` to zoom, `Esc` or the `Close 3D` button to return to the poster, and a `Reset view` button. Poster shown when the model fails to parse, exceeds a limit or the device is lost, with a one-line reason (`Compressed PRC tessellation is not supported`). Cheaper substitute that fails the checkpoint: auto-activating on page load, no poster fallback, or mouse-only navigation.
**Chrome:** consume `SwapChainPanel` hosting from `D02 T02 §1`, `RectoColors.xaml`, Fluent UI System Icons (`RectoIcons`), `RectoDialogStyles.xaml`.

**Build order:**

1. Add `Model3DView` in `src/app/overlays/Model3DView.xaml/.h/.cpp`, parse the model on a worker thread through the §5 to §7 parsers, and keep the poster until the scene is ready. Done when: `Overlays.Model3DActivation` shows the poster, then the model after a click, never before.
2. Add input handling and the fallback path. Done when: `Overlays.Model3DNavigation` drives orbit, pan, zoom, fit and reset by mouse and keyboard and `Overlays.Model3DFallback` shows the reason for each failing fixture.

- [ ] Parse and upload on a background job that opens its own `recto::pdf::Document`, so the viewer's actor is never blocked and the UI thread never parses. Done when: `Overlays.Model3DParsesOffUi` shows the UI thread stays responsive while `model-u3d.pdf` loads.
- [ ] Support mouse, touch and keyboard navigation as listed in Treatment, with inertia off when Windows reduced motion is set. Done when: `Overlays.Model3DNavigation` and `Overlays.Model3DReducedMotion` pass. Cheaper substitute: mouse-only.
- [ ] Show the poster with a reason and a `Retry` for parse errors, limit errors, device loss and unsupported PRC forms, and never show a blank rectangle. Done when: `Overlays.Model3DFallback` covers `u3d-block-bomb.pdf`, `prc-bad-offsets.pdf`, an over-budget model and a forced device loss.
- [ ] Apply the default view from `/3DV` on activation (camera, projection, background, render mode) and `Reset view` returns to it. Done when: `Overlays.Model3DDefaultView` shows the camera matches `/3DV` after activation and after reset.
- [ ] Register `Activate 3D`, `Close 3D`, `Reset 3D view` and `Fit 3D model` in the `CommandRegistry` so each is reachable from `Ctrl+K` while a 3D annotation is focused. Done when: `Commands.Reachability` finds all four.
- [ ] Give every control an automation name and tooltip and announce `3D view opened` and `3D view closed` through `AnnouncementService`. Done when: `A11y.Model3DInventory` finds no unnamed control.
- [ ] Use only `RectoColors.xaml` tokens and Fluent UI System Icons (`RectoIcons`). Done when: `rg "#[0-9A-Fa-f]{6}" src/app/overlays/Model3DView.xaml` prints nothing.
- [ ] Add `Overlays.Model3D*` tests in `tests/recto-tests/model3d_view_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Overlays.Model3D*:Commands.Reachability:A11y.Model3DInventory` passes.
- [ ] Commit: `viewer: add 3D model view with poster fallback and navigation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Overlays.Model3D*` passes; `Overlays.Model3DActivation` fails if the model draws before a click.

## 10. 3D panel: named views, model tree, show/hide parts, lighting, cross section

The user drives the model without leaving the keyboard: pick a named view from the file's `/VA`, see the part tree and toggle parts, choose a lighting preset and render mode, and cut the model with a plane. Matches Acrobat Pro's 3D Model Tree, Views, Lighting, Render Mode and Cross Section tools.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 8.3 (side panel list, `SidePanelHost`) and section 4 tokens, with the Fluent UI System Icons (`RectoIcons`) `cube_rotate`, `lightbulb` and `cut` glyphs, in the right inspector in Reading room and as a `SidePanelHost` card in Focus and Classic. Design board: `design/SPEC.md` section 9.12 with `design/screenshots/D-RichMedia.png` (drawn by `D06 T04 §7`).
**Job:** The user switches between `Front`, `Top` and custom views, hides a bracket to see what is behind it, dims or isolates a part, lights the model for inspection, and slices it to see inside. Consumer: `Model3DView` (§9), `Scene3DRenderer` (§8), `SidePanelHost` (`D02 T03 §3`), `CommandRegistry` (`D02 T03 §11`).
**Treatment:** A `Model3DPanel` with four sections (`Views`, `Model tree`, `Lighting and render`, `Cross section`). `Views` lists `/VA` views by `/XN` name and applies one with a short camera animation (instant under reduced motion). `Model tree` is a virtualized `TreeView` of the PRC or U3D node hierarchy with a visibility checkbox per node (parents toggle children), `Isolate` and `Show all`, and the note column for `No preview geometry`. `Lighting and render` has a preset `ComboBox` of the eleven presets plus `File lights`, and a render mode `ComboBox`. `Cross section` has an axis choice, a position slider, `Flip` and `Show cap`. Cheaper substitute that fails the checkpoint: a flat part list with no hierarchy, a view picker that does not move the camera, or a panel that works only with the mouse.
**Chrome:** consume `SidePanelHost` (`D02 T03 §3`), `RectoColors.xaml`, Fluent UI System Icons (`RectoIcons`), `CommandRegistry`.

**Build order:**

1. Add `Model3DPanel` in `src/app/panels/Model3DPanel.xaml/.h/.cpp` with a view model over `Annot3D`, `Scene3D` and `Scene3DRenderer`. Done when: `Panels.Model3DViews` lists the `Front` and `Top` views and applying `Top` moves the camera to the `/C2W` of that view.
2. Add the model tree, lighting, render mode and cross section controls. Done when: `Panels.Model3DTree` toggles a node and the renderer hides it, and `Panels.Model3DCrossSection` moves the plane.

- [ ] Build the tree with a virtualized `TreeView` and a three-state parent checkbox (checked, unchecked, mixed). Done when: `Panels.Model3DTree` shows a mixed parent when one child is hidden and a 20000-node tree scrolls without frame drops over `4 ms` per frame in the harness.
- [ ] Implement `Isolate`, `Show all`, and `Hide others` on a node's context menu and via `Delete` and `Space` keys. Done when: `Panels.Model3DIsolate` shows only the selected part and `Show all` restores every node.
- [ ] Apply a view's `/NA` node overrides and `/SA` cross sections when the view is selected, and let the user changes remain until `Reset view`. Done when: `Panels.Model3DViewOverrides` shows the view's overrides applied and manual changes kept.
- [ ] Wire the lighting preset, render mode and cross-section controls to `Scene3DRenderer` and keep them in the tab session only. Done when: `Panels.Model3DLightingAndModes` changes the renderer state for every option and a new tab starts at the file defaults.
- [ ] Register `Next 3D view`, `Previous 3D view`, `Show all 3D parts` and `Toggle 3D cross section` in the `CommandRegistry` for `Ctrl+K`. Done when: `Commands.Reachability` finds all four.
- [ ] Give every control an automation name and tooltip, with tree items exposing name, visibility state and level to UIA. Done when: `A11y.Model3DPanelInventory` finds no unnamed control and the tree exposes `ExpandCollapse` and `Toggle` patterns.
- [ ] Use only `RectoColors.xaml` tokens and Fluent UI System Icons (`RectoIcons`). Done when: `rg "#[0-9A-Fa-f]{6}" src/app/panels/Model3DPanel.xaml` prints nothing.
- [ ] Add `Panels.Model3D*` tests in `tests/recto-tests/model3d_panel_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Panels.Model3D*:Commands.Reachability:A11y.Model3DPanelInventory` passes.
- [ ] Commit: `viewer: add 3D panel with views, model tree, lighting and cross section`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Panels.Model3D*` passes; `Panels.Model3DTree` fails if toggling a node does not hide it in the renderer.

## 11. `recto-pdf` geospatial model: `/VP`, `/Measure` `/GEO` and page-to-coordinate transform

Read the geospatial features of ISO 32000-2 into a `GeoViewport` and give callers a pure function from a page point to latitude and longitude and back. It reads data only. A PDF whose geospatial data is missing, inconsistent or hostile is treated as not geospatial, with a reason, never guessed. Matches Acrobat Pro's geospatial location tool.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Define `GeoViewport { page_index, bbox, bounds, gpts, lpts, gcs, display_units, datum }` and `GeoTransform` in `src/pdf/geo.h`, and implement `parse_geo_viewports(page)` returning `PdfResult<std::vector<GeoViewport>>` in `src/pdf/geo.cpp` from `/VP` entries (`/BBox`, `/Measure`) with `/Subtype /GEO`. Done when: `PdfGeo.*` tests parse `geo-map.pdf` and `geo-projected.pdf` and reject `geo-bad.pdf`.
2. Implement `GeoTransform::page_to_lat_lon` and `lat_lon_to_page` over the `/GPTS` and `/LPTS` control points (latitude and longitude degrees against the unit square of `/Bounds`) with an exact affine mapping for three points, an exact bilinear mapping for four points and a least-squares affine mapping for five or more, reporting the maximum residual in metres. A fit whose maximum residual exceeds 0.1 percent of the control-point span is refused as `Control points are inconsistent` and the viewport is treated as not geospatial. Done when: `PdfGeo.RoundTrip` returns each control point within 1e-9 degrees for the three and four point cases, `PdfGeo.Residual` reports the residual for five consistent points and stays within it, and `PdfGeo.InconsistentRefused` refuses five points with the fifth displaced by 5 percent of the span.

- [ ] Parse `/GCS` (`/Type` `/GEOGCS` or `/PROJCS`, `/EPSG`, `/WKT`) and `/DCS`, take the datum and ellipsoid from the EPSG code or WKT `SPHEROID` when it is present, and otherwise assume WGS84 and set `datum_assumed = true`. Done when: `PdfGeo.Datum` returns WGS84 for EPSG 4326, the WKT ellipsoid for `geo-projected.pdf`, and `datum_assumed = true` for a missing `/GCS`.
- [ ] Parse `/PDU` preferred display units for linear, area and angular output and map them to `Units` values, defaulting to metric when absent. Done when: `PdfGeo.DisplayUnits` returns km, sq km and degrees for `geo-map.pdf`.
- [ ] Validate the data: finite latitudes within -90 to 90 and longitudes within -180 to 180, `/GPTS` and `/LPTS` of equal length with at least three points, `/LPTS` within 0 to 1, a non-degenerate (non-collinear) control set, and a `/BBox` inside the page. Done when: `PdfGeo.Validation` rejects each broken case in `geo-bad.pdf` with a `PdfError` naming the field.
- [ ] Support several viewports on one page, choosing the topmost `/BBox` containing the point, and return `None` outside every viewport. Done when: `PdfGeo.ViewportSelection` picks the right viewport for overlapping boxes and returns `None` outside.
- [ ] Add `fuzz/fuzz_geo.cpp` to `recto-fuzz.exe` driving `parse_geo_viewports` and the transform on arbitrary PDFs, with seeds from `tests/corpus/seeds/`. Done when: `recto-fuzz.exe fuzz_geo -runs=100000` reports no crash, leak, NaN propagation into the transform or oversize allocation under ASan.
- [ ] Add `PdfGeo.*` tests in `tests/recto-tests/pdf_geo_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=PdfGeo.*` passes under ASan.
- [ ] Commit: `pdf: parse geospatial viewports and add page to latitude/longitude transform`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfGeo.*` passes under ASan; `PdfGeo.RoundTrip` fails if a control point drifts by more than 1e-9 degrees.

## 12. Geodesic distance and area on the ellipsoid (`geodesy.cpp`)

Compute distance and polygon area on the reference ellipsoid, not on the flat page, so a measured road or parcel matches the real world. The module is pure maths with no I/O: an inverse geodesic solver (Karney's method for accurate results near antipodal points) and the geodesic polygon area. It is the only code that computes real-world measures.

**Phase:** 7
**Needs:** none
**Surface:** no

**Build order:**

1. Implement `Ellipsoid` (WGS84 default; semi-major axis and flattening from `GeoViewport::datum`), `geodesic_inverse(p1, p2)` returning distance in metres and azimuths, and `geodesic_path_length(points)` in `src/core/geodesy.h/.cpp`. Done when: `Geodesy.EquatorAndMeridian` returns `6378137 * delta_lambda` along the equator to 1 mm and the pole-to-pole meridian length of 20003931.4586 m to 1 m on WGS84.
2. Implement `geodesic_polygon_area(points)` returning signed area in square metres and the perimeter, for simple polygons that may cross the antimeridian or enclose a pole. Done when: `Geodesy.PolygonArea` returns half the WGS84 surface area (255032810.86 square kilometres) for the equator polygon to within 0.001 percent, and an antimeridian-crossing rectangle equals the same rectangle shifted to the Greenwich meridian.

- [ ] Validate inputs and return `PdfResult` values: non-finite or out-of-range coordinates, fewer than 2 points for distance and fewer than 3 for area, and more than 100000 points are typed errors. Done when: `Geodesy.InputValidation` covers each.
- [ ] Reject self-intersecting polygons for area with `Self-intersecting polygon` rather than returning a misleading number. Done when: `Geodesy.SelfIntersectionRefused` rejects a bow-tie and accepts a concave polygon.
- [ ] Convert results to the `/PDU` display units (m, km, ft, US survey ft, mi, nautical mi; sq m, hectare, sq km, sq ft, acre, sq mi, sq nautical mi; degrees, grads) with the conversion factors in one table. Done when: `Geodesy.UnitConversions` checks each factor against its published definition.
- [ ] Verify against a reference set of at least 200 point pairs generated by the independent GeographicLib `GeodSolve` tool and stored as `tests/corpus/rich/geodesic-reference.csv` (registered in `PROVENANCE.md`). Done when: `Geodesy.ReferenceSet` agrees within 1 mm for distance and 1e-9 degrees for azimuth.
- [ ] Add `Geodesy.*` tests in `tests/recto-tests/geodesy_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Geodesy.*` passes under ASan.
- [ ] Commit: `core: add ellipsoidal geodesic distance and polygon area`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Geodesy.*` passes under ASan; `Geodesy.ReferenceSet` fails if any distance differs from the reference by more than 1 mm.

## 13. Geo surface: coordinates under the pointer and `Find location`

On a geospatial page the user reads the real-world position of the pointer and jumps to a coordinate. Matches Acrobat Pro's `Location` tool (`Show Location`, `Find a Location`).

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 8.2 (floating tool bar), section 4 tokens and section 5 type styles, with a coordinate readout chip over the page canvas and a `Find location` flyout in the Floating Page Toolbar of `D02 T02 §8`. Design board: `design/SPEC.md` section 9.12 with `design/screenshots/D-RichMedia.png` (drawn by `D06 T04 §7`).
**Job:** A user reading a survey, utility or hiking map moves the pointer and sees `Lat, Lon` in their preferred format, copies a position, types or pastes `-33.9249, 18.4241` and lands on that spot with a marker, and is told clearly when the page or the location is outside the map. Consumer: `GeoTransform` (§11), `PageLayoutGeometry::viewport_to_page_point` (`D02 T02 §5`), `CommandRegistry` (`D02 T03 §11`).
**Treatment:** A `GeoReadout` chip near the bottom left of the canvas, visible only over a geospatial viewport, showing `Latitude, Longitude` (decimal degrees by default, degrees-minutes-seconds on a toggle) that updates from the pointer or, for keyboard users, from the focused crosshair; `Ctrl+C` copies it while focused. `Find location` accepts decimal degrees, degrees-minutes-seconds and `lat, lon` pastes, validates them, scrolls the page, centres the point, draws a marker and announces the result; a location outside the viewport shows `That location is outside this map.` Cheaper substitute that fails the checkpoint: showing page points instead of coordinates, accepting only one format, or a silent no-op for an outside location.
**Chrome:** consume `PageLayoutGeometry` (`D02 T02 §5`), the Floating Page Toolbar (`D02 T02 §8`), `RectoColors.xaml`, Fluent UI System Icons (`RectoIcons`), `AnnouncementService`.

**Build order:**

1. Add `GeoReadout` in `src/app/overlays/GeoReadout.xaml/.h/.cpp`, fed by the pointer position converted with `viewport_to_page_point` and `GeoTransform` on the UI thread (a pure function, no engine call). Done when: `Overlays.GeoReadoutValues` reports the known latitude and longitude at each control point of `geo-map.pdf` within 1e-6 degrees.
2. Add `Find location` with input parsing and a marker overlay. Done when: `Overlays.GeoFindLocation` lands on a control point and shows the outside message for a far point.

- [ ] Parse coordinate input in `src/core/coordinate_parse.cpp` for decimal degrees, hemisphere letters (`33.9249 S, 18.4241 E`), degrees-minutes-seconds with symbols or spaces, and a pasted pair separated by comma, semicolon or whitespace, returning `PdfResult<LatLon>`. Done when: `CoordinateParse.Formats` accepts ten valid forms and rejects `91, 0`, `abc` and an empty string.
- [ ] Show the readout only inside a viewport's `/BBox`, hide it elsewhere and on non-geospatial pages, and show `Georeferencing missing or invalid` in the Info panel's row for pages with `geo-bad.pdf` data instead of a wrong coordinate. Done when: `Overlays.GeoReadoutVisibility` covers inside, outside, non-geo and bad-geo cases.
- [ ] Copy the readout with `Ctrl+C` in the current format, and add a keyboard crosshair moved by arrow keys (1 point) and `Shift`+arrow (10 points) for users without a pointer. Done when: `Overlays.GeoReadoutKeyboard` moves the crosshair and copies `-33.924900, 18.424100` style text.
- [ ] Register `Find location...`, `Toggle coordinate format` and `Copy coordinates` in the `CommandRegistry` for `Ctrl+K`, enabled only on a geospatial page. Done when: `Commands.Reachability` finds all three and `Commands.GeoDisabledOnPlainPage` shows them disabled elsewhere.
- [ ] Give the chip, flyout fields and buttons an automation name and tooltip and announce the found location and the outside message through `AnnouncementService`. Done when: `A11y.GeoInventory` finds no unnamed control.
- [ ] Use only `RectoColors.xaml` tokens and Fluent UI System Icons (`RectoIcons`). Done when: `rg "#[0-9A-Fa-f]{6}" src/app/overlays/GeoReadout.xaml` prints nothing.
- [ ] Add `Overlays.Geo*` and `CoordinateParse.*` tests in `tests/recto-tests/geo_surface_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Overlays.Geo*:CoordinateParse.*:Commands.Reachability:A11y.GeoInventory` passes.
- [ ] Commit: `viewer: show coordinates under the pointer and add Find location`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Overlays.Geo*:CoordinateParse.*` passes; `Overlays.GeoReadoutValues` fails if a control point reads off by more than 1e-6 degrees.

## 14. Geo measure tools and coordinate export (CSV, GeoJSON)

The user measures a real-world distance or area by clicking on the map, sees the result in the file's preferred units, and exports points. Matches Acrobat Pro's `Perimeter`, `Distance` and `Area` measuring tools on geospatial pages. Measurements are shown on screen and exported on request; they are not written into the PDF, so the original is never modified.

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 8.2 (tool bar tool pattern), section 4 tokens and section 9.9 (the Save dialog is a `FileSavePicker`), reusing the `GeoReadout` chip of §13. Design board: `design/SPEC.md` section 9.12 with `design/screenshots/D-RichMedia.png` (drawn by `D06 T04 §7`).
**Job:** A planner clicks along a path and reads its true length, clicks around a field and reads its area and perimeter, switches units, and exports the clicked points as a file for GIS software. Consumer: `geodesic_path_length`, `geodesic_polygon_area` (§12), `GeoTransform` (§11), `GeoReadout` (§13).
**Treatment:** A `Measure` tool group (`Distance`, `Area`) in the geo toolbar: click adds a vertex, `Backspace` removes the last, double-click or `Enter` finishes, `Esc` cancels; a live label shows distance or area plus perimeter in the `/PDU` units with a units `ComboBox`; finished measurements stay on screen until cleared. `Export...` writes the points with `FileSavePicker` as CSV (UTF-8, header `index,latitude,longitude`, decimal point) or GeoJSON (RFC 7946: `[longitude, latitude]` order, a `LineString` or `Polygon` with closed ring, WGS84) through temp-file-then-replace. Cheaper substitute that fails the checkpoint: measuring in page points, writing coordinates in latitude-longitude order into GeoJSON, or writing the export in place without the temp-file replace.
**Chrome:** consume `PageLayoutGeometry`, `GeoReadout` (§13), the Floating Page Toolbar, `FileSavePicker`, `RectoColors.xaml`, Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Add `GeoMeasureTool` in `src/app/tools/geo_measure_tool.h/.cpp` building the vertex list in page points, converting to coordinates through `GeoTransform`, and calling §12 for the result. Done when: `Tools.GeoMeasureDistance` over the `geo-map.pdf` control points matches the §12 reference distance to 1 mm.
2. Add the coordinate exporters in `src/core/geo_export.h/.cpp`. Done when: `GeoExport.Csv` and `GeoExport.GeoJson` write the expected bytes and `GeoExport.AtomicWrite` leaves the old file intact on an injected failure.

- [ ] Implement area with live perimeter and refuse a self-intersecting polygon with the §12 message shown inline. Done when: `Tools.GeoMeasureArea` finishes a rectangle and shows `Self-intersecting polygon` for a bow-tie.
- [ ] Implement unit switching across the `/PDU` units and persist the choice per tab only. Done when: `Tools.GeoUnits` converts a displayed result between km and mi and a new tab starts at the file's `/PDU`.
- [ ] Write CSV with header `index,latitude,longitude`, 8 decimal places, UTF-8 without BOM, and GeoJSON with `[longitude, latitude]` pairs, a closed polygon ring and a `crs`-free WGS84 body per RFC 7946. Done when: `GeoExport.Csv` and `GeoExport.GeoJson` parse back to the same points within 1e-8 degrees and `GeoExport.LonLatOrder` fails if the pair order is swapped.
- [ ] Transform to WGS84 on export when the file's datum differs and mark `datum_assumed` in a GeoJSON `properties` member. Done when: `GeoExport.DatumNote` shows the property for an assumed datum.
- [ ] Register `Measure distance`, `Measure area`, `Clear measurements` and `Export coordinates...` in the `CommandRegistry` for `Ctrl+K`, enabled only on a geospatial page. Done when: `Commands.Reachability` finds all four.
- [ ] Give each tool button, label and the units box an automation name and tooltip, make the tools usable by keyboard (crosshair of §13 plus `Enter`) and announce each finished measurement. Done when: `A11y.GeoMeasureInventory` finds no unnamed control.
- [ ] Use only `RectoColors.xaml` tokens and Fluent UI System Icons (`RectoIcons`), and never write measurements into the PDF. Done when: `rg "#[0-9A-Fa-f]{6}" src/app/tools/` prints nothing and `Tools.GeoMeasureDoesNotDirty` shows the tab dirty flag stays false after measuring.
- [ ] Add `Tools.GeoMeasure*`, `GeoExport.*` tests in `tests/recto-tests/geo_measure_tests.cpp`. Done when: `recto-tests.exe --gtest_filter=Tools.GeoMeasure*:Tools.GeoUnits:GeoExport.*:Commands.Reachability` passes.
- [ ] Commit: `viewer: add geodesic measure tools and CSV/GeoJSON coordinate export`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Tools.GeoMeasure*:GeoExport.*` passes; `GeoExport.LonLatOrder` fails if GeoJSON coordinates are written latitude first.

## 15. Register `fuzz_media`, `fuzz_u3d`, `fuzz_prc`, `fuzz_geo` in the fuzz campaign and audit

The four parsers read hostile bytes, so they join the scheduled campaign with the others, and the hostile-input audit records that no script, Flash or external media runs. This section adds no parser; it wires §2, §6, §7 and §11 into `D06 T02`'s machinery and proves the claims.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add the four targets to `scripts/run-fuzz-campaign.ps1` and the scheduled workflow, with the seeds in `tests/corpus/seeds/` and minimized corpora. Done when: `pwsh -NoProfile -File scripts/run-fuzz-campaign.ps1 -SecondsPerTarget 60` completes all targets including the four new ones with no crash.
2. Extend `docs/security-audit.md` with rows for media, Flash, 3D scripts, external media references and geo data. Done when: each row names the test that proves it.

- [ ] Add `fuzz_media`, `fuzz_u3d`, `fuzz_prc` and `fuzz_geo` to the campaign target list and the coverage summary. Done when: the campaign summary lists the four targets with non-zero coverage.
- [ ] Add a regression-promotion rule: any crashing input becomes a named file in `tests/corpus/rich/regressions/` with a `PROVENANCE.md` row and a failing test before the fix. Done when: a planted crash input is promoted by the documented command and the test fails before the fix and passes after it.
- [ ] Add audit rows to `docs/security-audit.md` for `PdfMedia.ScreenScriptIgnored`, `PdfMedia.ExternalRefNeverRead`, `MediaPlayback.StreamOnly`, `MediaPlayback.NoAutoplay`, `Pdf3D.JavaScriptNeverRuns`, `Pdf3D.ActivationIgnored`, `U3D.NoOverread`, `Prc.BadOffsets` and `PdfGeo.Validation`. Done when: each row links to an existing gtest name and `rg` finds all nine names in `docs/security-audit.md`.
- [ ] Prove no new network or process-launch API is reachable from the new code: add an API-use audit test listing banned calls (`WinHttp`, `InternetOpen`, `URLDownloadToFile`, `CreateProcess`, `ShellExecute`, `LaunchUriAsync`) against the new source files. Done when: `Audit.RichMediaNoNetworkNoLaunch` passes and fails when a banned call is added to a scratch copy.
- [ ] Commit: `fuzz: add media, U3D, PRC and geo targets to the campaign and security audit`

**Test checkpoint:** `pwsh -NoProfile -File scripts/run-fuzz-campaign.ps1 -SecondsPerTarget 60` completes with no crash for all targets, and `recto-tests.exe --gtest_filter=Audit.RichMediaNoNetworkNoLaunch` passes.

## 16. `recto-pdf` media authoring: embed audio or video as a `/Screen` annotation

Foxit PDF Editor and PDFelement add audio and video to a page, and Acrobat Pro does too. §2 to §4 only play what a file already contains; this section writes the annotation. It embeds a local file as a `/Screen` annotation whose rendition holds a media clip with the file as an embedded stream (`/EF`) and a MIME type, plus a poster appearance (`/AP /N`). It writes no `/JS`, no external file or URL reference, no `Launch` and no autoplay, so §4's ask-before-play rule still governs playback. RichMedia authoring is not built (Acrobat reads `/Screen` renditions; RichMedia needs a player asset bundle); that is recorded as a default.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Implement `src/pdf/media_author.h/.cpp`: `add_media_annotation(Document&, int page, PdfRect rect, MediaSource { bytes_or_stream, file_name, mime }, Poster { rgba, width, height }, MediaOptions { alt_text, title }) -> PdfResult<AnnotRef>` writing the `/Screen` annotation (`/Rect`, `/T`, `/Contents` alt text, `/P`), a `/MediaClip` of subtype `MCD` with the embedded `/Filespec` and `/CT`, a rendition action limited to a play operation with no `/JS` entry, and a poster form XObject as `/AP /N`. Done when: the written fixture reopens through §2's parser as one `MediaItem` with the expected size, MIME and poster, and contains no `/JS`, `/Launch` or `/URI`.
2. Allow only these extensions and sniffed container types: `.mp3`, `.m4a`, `.aac`, `.wav`, `.wma`, `.mp4`, `.m4v`, `.mov`, `.wmv`; refuse `.swf` and everything else with a typed `UnsupportedMedia` error naming the reason; cap one file at 256 MB and a document at 1 GB of embedded media, streaming the file into the output without loading it whole. Done when: an `.swf`, a renamed `.exe`, a 300 MB file and a file that fails the container sniff are each refused with the right error.

- [ ] Add the accessibility alt text and title as required inputs with a default taken from the file name. Done when: the annotation has `/Contents` set.
- [ ] Write through the staged edit session (`D03 T04 §3`) on a copy and the incremental writer, never in place. Done when: the source file hash is unchanged until Save.
- [ ] Add `tests/corpus/rich/` inputs (a 1 s mp3, a 1 s mp4) generated by `scripts/build-corpus.py` and a `PROVENANCE.md` row. Done when: `python scripts/build-corpus.py --check` passes.
- [ ] Add `PdfMediaAuthor.WritesScreenAnnotation`, `PdfMediaAuthor.NoScriptNoLaunchNoUri`, `PdfMediaAuthor.RefusesUnsupported` and `PdfMediaAuthor.SizeCaps` under ASan. Done when: `recto-tests.exe --gtest_filter=PdfMediaAuthor.*` passes. Cheaper substitute: referencing the media by path instead of embedding it.
- [ ] Commit: `pdf: embed audio and video as a Screen annotation`

**Test checkpoint:** `recto-tests.exe --gtest_filter=PdfMediaAuthor.*` passes under ASan and the written fixture plays through `D02 T05 §3` after the ask-first dialog.

## 17. Add audio or video tool: pick a file, place it, choose a poster, preview

The user-facing half of §16, matching Foxit's and PDFelement's `Add video` and `Add audio` tools and Acrobat Pro's media tools. The user picks a local file (no network sources), draws or accepts a rectangle on the page, chooses a poster (a frame from the video through the Windows thumbnail service, a chosen image, or, for audio, the outlined rectangle with the `speaker_2` glyph and no filled tile, matching §4), previews it, and adds it as an undoable staged edit. Playback of the result still asks first (§4).

**Phase:** 7
**Needs:** windows-host, display-session
**Surface:** yes

**Fidelity:** Genuinely new surface; it follows `design/SPEC.md` section 9.9 form layout, section 8.2 contextual bars and `D03 T03 §4` creation handles. Design board: `design/SPEC.md` section 9.12 with `design/screenshots/D-RichMedia.png` (drawn by `D06 T04 §7`).
**Job:** A user embeds a recording or clip in a page and sees what readers will see. Consumer: `media_author` (§16), `MediaPlaybackService` (§3), annotation handles (`D03 T03 §4`).
**Treatment:** A picker, a placement mode and a dialog over the engine. Cheaper substitute that fails the checkpoint: adding the media with no alt text, or from a URL.
**Chrome:** `RectoDialogStyles.xaml` (`D02 T01 §10`), `RectoColors.xaml` tokens, Fluent UI System Icons (`RectoIcons`).

**Build order:**

1. Add the `Add audio or video...` command (Tools menu, `PageToolbar` `More` menu, `Ctrl+K`): `FileOpenPicker` for the allowed extensions, then a placement mode on the canvas using the annotation creation handles (`D03 T03 §4`) with a default rectangle sized from the video's aspect ratio (at most 320 epx wide), then `AddMediaDialog` with alt text, poster choice and a preview that plays the local file once through the §3 playback service muted by default. Done when: choosing an mp4 draws the placeholder, the dialog shows a frame poster, and `Add` creates the annotation on the page.
2. Generate the poster with `StorageFile.GetThumbnailAsync` for video, accept `PNG` or `JPEG` for a custom poster, and for audio write the §4 default poster into the appearance: a transparent rectangle outlined at 1 pt in the `RectoBorderBrush` colour with the Fluent `speaker_2` 20 px Regular glyph (from the vendored `resources/fonts/fluent-svg/` SVG, converted to PDF path operators) filled in the `RectoTextSecondaryBrush` colour at its centre, with no filled box, tile or circle behind the glyph (SPEC section 6); token colours are resolved to RGB from the Light dictionary at write time; keep the poster at most 1024 px on the long side. Done when: a video poster, a custom poster and the audio poster each render in the page appearance, and `Overlays.AddMediaTool` finds that the audio appearance stream's only fill operators (`f`, `f*`, `B`, `B*`) paint the glyph's own subpaths, so no other filled path intersects the glyph's bounding box.

- [ ] Apply through the staged edit session as one undoable step, mark the tab dirty, and select the new annotation so it can be moved and resized with the standard handles. Done when: `Ctrl+Z` removes the annotation and its embedded stream.
- [ ] Show the file size and warn above 50 MB that the PDF will grow by that amount. Done when: a 60 MB file shows the warning with the size.
- [ ] Surface refusals from §16 as plain messages (`Flash isn't supported`, `This file is larger than 256 MB`). Done when: each refusal shows its reason and adds nothing.
- [ ] Give the dialog, the placeholder and the added annotation automation names, including the alt text. Done when: Narrator reads the alt text of the added media.
- [ ] Add `Overlays.AddMediaTool` covering placement, poster choices, refusal messages, undo and the dirty state. Done when: `recto-tests.exe --gtest_filter=Overlays.AddMediaTool` passes. Cheaper substitute: a tool that writes the annotation without the preview and alt text.
- [ ] Commit: `viewer: add the add audio or video tool`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Overlays.AddMediaTool` passes on the display-session host.

## 18. `recto-cli media`: add and list embedded audio and video

Scripts and the local MCP server (D21) need the same operation without a window. `recto-cli media add` embeds a file through §16 and `recto-cli media list` prints the `MediaItem` model of §2. Both are offline, and `add` always writes a new file.

**Phase:** 7
**Needs:** windows-host
**Surface:** no

**Build order:**

1. Add `recto-cli media add <in.pdf> --page N --rect x1,y1,x2,y2 --file <media> [--poster <image>] [--alt <text>] -o <out.pdf>` and `recto-cli media list <in.pdf> [--json]` in `src/cli/`, exit code 2 on validation failure or unsupported media, 0 on success, and refuse `-o` equal to the input. Done when: add then list reports the new item and the input file is byte-identical.
2. Make `media list --json` print `page`, `rect`, `kind`, `mime`, `size`, `title`, `alt` and `embedded` for each item with a stable field order. Done when: the JSON output matches a golden file.

- [ ] Register the verbs in the `recto-cli verbs --json` table so the MCP server exposes them. Done when: the verb table lists both verbs with their argument schemas.
- [ ] Add `Cli.MediaAddList` covering success, refusals, an out-of-range page and rectangle, and golden JSON. Done when: `recto-tests.exe --gtest_filter=Cli.MediaAddList` passes. Cheaper substitute: a CLI that edits the input in place.
- [ ] Commit: `cli: add recto-cli media add and list`

**Test checkpoint:** `recto-tests.exe --gtest_filter=Cli.MediaAddList` passes.

## Verification

- A PDF with a `/Screen` audio rendition, a `/Sound` annotation and a `/Movie` annotation plays only after `Play this media?` is accepted, plays from memory through `MediaPlayer` (`MediaPlayback.StreamOnly`), and `Save media as...` writes the exact embedded bytes (`Overlays.SaveMediaAs`); the Flash fixture shows the placeholder and never runs (`Overlays.FlashPlaceholder`).
- The U3D and PRC fixtures open in the 3D view after a click, switch named views, hide parts, change lighting and cut with a cross section (`Panels.Model3D*`, `Render3D.*`); hostile and over-budget models fall back to the poster with a reason (`Overlays.Model3DFallback`); `model-js.pdf` runs nothing (`Pdf3D.JavaScriptNeverRuns`).
- `geo-map.pdf` reports coordinates under the pointer, finds a location, measures a distance and an area that agree with the independent reference set (`Geodesy.ReferenceSet`) and exports CSV and GeoJSON in the right axis order (`GeoExport.LonLatOrder`).
- The scheduled fuzz campaign covers `fuzz_media`, `fuzz_u3d`, `fuzz_prc` and `fuzz_geo` with no crash, and `Audit.RichMediaNoNetworkNoLaunch` passes.
- `python scripts/todo-graph.py validate` and `python scripts/todo-graph.py plan --check` pass.
- Media authoring (§16 to §18) is proven by `PdfMediaAuthor.*`, `Overlays.AddMediaTool` and `Cli.MediaAddList`.
