# AGENTS.md

Agent instructions for this repository. Google Antigravity loads `AGENTS.md` directly; Claude Code loads it through the `CLAUDE.md` import stub.

## What is here

Recto is an open-source (`GPL-3.0-or-later`) PDF viewer, OCR tool and document converter for Windows: Recto's own native C++23 PDF engine (`recto-pdf`), C++23 concurrency, OCR and conversion runtime (`recto-core` and `recto-services`), and a Win32 app drawn by the Rizonesoft UI toolkit Vellum (`libs/vellum`, shared later with Rizonesoft Office and Isotone, D22: DirectComposition, Direct2D, DirectWrite, UI Automation; D4), distributed via Inno Setup (full installer and self-contained portable mode, plus an in-app Portable mode setting) and GitHub Releases, with optional MSIX packaging for the Microsoft Store. The repository holds the approved design, the foundation plan, the shipping graphics, the module scaffold, and the TODO execution plan that builds version 1 milestone by milestone (M0 to M6).

| Path | Purpose |
| ---- | ------- |
| `docs/plans/2026-10-13-recto-foundation.md` | Decisions D1-D17, architecture, milestones & targets. |
| `design/SPEC.md` | Interface source of truth (`screenshots/`, `tokens/`). |
| `resources/` | Generated icons/graphics (`build_resources.py`). |
| `todo/` | Live execution plan (`README.md`, generated plan). |
| `scripts/` | TODO graph, review dispatch, LSP/MCP, test runner, run guard, pin checks; stdlib Python. |
| `tests/scripts/` | Tooling tests; app suites land under `tests/` and kit tests under `scripts/tests/`. |
| `.claude/`, `.agents/`, `.lsp.json`, `.mcp.json` | Agent kits and portable C++23/Python LSP servers (`.agents/plugins/recto-lsp/`). |
| `.conclave/` | Reviewer pin (`reviewers.toml`), `unattended.json`, and scheduled `automations/`. |
| `build/` | Ignored output (plus `build.ps1`, `python.cmd`, `tools.lock.json`, generated `compile_commands.json`). |

The repository layout is fixed by foundation plan section 3.2 (`src/pdf`, `src/core`, `src/services`, `src/app`, `src/cli`, `libs/vellum`, `tests/recto-tests`, `tests/corpus`, `fuzz`; `src/ui` until `D10 T04 §1` moves it to `libs/vellum`). Do not invent a different one.

## Product ambition

Recto is not a clone. It takes on Adobe Acrobat, Foxit, PDFelement, UPDF, Microsoft, Apple and Google and does things better (owner decisions 2026-10-23 and 2026-11-13). Every feature is optimised, not just implemented: before building, look at how the best competitor does it and find where Recto can be faster, need fewer steps, choose smarter defaults, recover from errors better, be more accessible and feel effortless. The aim is that users think "how is this possible". Nothing is off limits because it is hard: no shortcuts, no workarounds, and no avoiding difficult work.

- An improvement within the section's designed surface and behaviour (speed, fewer steps, smarter defaults, better errors and recovery, keyboard paths, accessibility, polish) is built in the section.
- An improvement that changes the designed surface goes through a `design/SPEC.md` subsection and board first, filed through `add-todo` the `D06 T04` way, and is built after that.
- Nothing weakens a Recto improvement already decided: foundation plan D18 says parity never removes one.
- Measured claims beat adjectives: performance targets, step counts and golden images, not "fast" or "simple".

## Autonomy

The writer decides and acts, in attended sessions as in unattended runs (owner directions 2026-11-10 and 2026-11-13). Never stop to ask a question whose answer can be reasoned out from the sources, the code, first principles or good engineering practice: take the strongest engineering option, build it, verify it, and report it with its cost of reversing.

- **Self-improve:** continuously shape the repository, runners, hooks, toolset, development methods, architecture and product to be the best they can be. When a runner, script, gate, skill or workflow is slow, wasteful, asymmetrical or weaker than it could be, improve it at its owning layer, or file it through `add-todo` when it is larger than the work in hand. Nobody has to ask.
- **Self-heal:** a failed gate, a red CI run, a dead watch, a stale guard, a broken hook or a drifted plan claim is repaired at its cause as soon as it is seen, with a regression test where code failed, then the work continues.
- **No competitor sets the ceiling:** Adobe, Microsoft, Apple or Google doing something a certain way is a starting point to beat, never a reason to aim lower, and difficulty is never a reason to skip work.
- **What still waits for the owner:** destroying data, creating, rotating or exposing credentials, force-pushing, and spending money. Each becomes a section Need or an operator item, and the rest of the work goes on.

## Who writes and who reviews

Claude Code (`claude`) and Google Antigravity (`agy`) are the writers here: code, tests, plan and docs (owner directions 2026-11-10, 2026-11-12 and 2026-11-13). In Claude Code, Fable 5.2 (`claude-fable-5-2`) is the pinned writer for `.claude/agents/` (`scripts/verify-agent-tiers.mjs`), while `scripts/start-unattended-run.ps1` starts on `opus` (Opus 5.5 today; reversing or pinning costs `$model` there and its test). In Antigravity, the writer is Gemini 4 Carbon at medium (`gemini-4-carbon-medium`, `-Runner agy` in `scripts/start-unattended-run.ps1`), offloaded by read-only Gemini 4 Hydrogen helpers at medium (`gemini-4-hydrogen-medium`, `.agents/agents/README.md`) on `swarm` (`workspace: "inherit"`) or `invoke_subagent` (`research`) while `scripts/dispatch_hook.py` blocks any Hydrogen write or mutation so 100% of code, diagnosis, design and verdicts stay in Carbon. Both runners load `.claude/skills/` (Antigravity via `.agents/skills.json`, zero duplicated skills) and share lifecycle hooks (`scripts/ci_status.py`, `scripts/dispatch_hook.py`, `scripts/campaign_guard.py` in `.claude/settings.json` and `.agents/hooks.json`), passing `--writer` (`claude` or `agy`) to `scripts/review-dispatch.py`. One reviewer, GPT-6 Astra via Codex CLI in its read-only sandbox, reviews every pass across `claude` and `agy` (owner decisions 2026-10-28, 2026-11-10 and 2026-11-13); it reads this file and never edits. `.conclave/reviewers.toml` pins the `three-pass` schedule from Conclave-SDK `kits/reviewer` (`scripts/review_pin.py`, `scripts/review-prompt.py`, `scripts/review-dispatch.py`): `--kind plan` before build, one `--kind round` after build (adversarial, integration, source-defect, consistency, quality, optimization and, on surface sections, design), `--kind completion` on the final candidate (plan-delivery, record), and `--kind reconciliation` when completion returns `P1` or `P2` (all medium). Historical `converge` and panel stamps (`D01 T02 §33`, `D01 T02 §42`, `D01 T01 §11`) replay against the pin committed at their plan or stamp commit. Prompts carry settled items (`- Filed:`, `- Not taken:`, `- Settled:`), not reopened without new evidence, and define `P1` by its consequence on a real path.

## The TODO system

`todo/` is the live execution plan; format spec: `todo/README.md`. Markdown is canonical. Eleven domains map to the milestones: `00-workspace` (M0), `01-engine` (M1), `02-viewer` (M2), `03-text` (M3), `04-convert` (M4, M5), `05-organize` (M4), `06-quality`, `07-release` (M6), `08-assistant` (after v1), `10-ui` (M2, the Vellum toolkit; `09` belonged to the withdrawn D20 and is not reused), `99-manual` (operator only). Numbers are stable addresses: a new domain appends after the last.

Core mechanics: the Implementation Order table in each TODO file is the dependency graph; a row flips to `[x]` only through `python scripts/todo-graph.py stamp`, backed by a review receipt in `docs/reviews/`; `todo/implementation-plan.md` is regenerated by `plan --sync`, never ticked or edited by hand; cross-references (`§N`, `TNN §N`, `DNN TNN §N`) are bidirectional; every section declares Phase, Needs and Surface, carries a Build order with `Done when:` per step, and must be implementable with zero conversation context.

How the TODO system works: `docs/todo-workflow.md`. The `process-plan` run guard (the Stop hook in `.claude/settings.json` and `.agents/hooks.json` running `scripts/campaign_guard.py`, plus a 5-minute session heartbeat via `CronCreate` in Claude Code or `schedule` in Antigravity) is specified in the `process-plan` skill. For TODO work, use the skill that owns the step: capture through `add-todo`, author a file through `create-todo`, build through `process-todo-section`, stamp through `review-todo-section` (running independent review rounds through `scripts/review-dispatch.py` with the reviewer pinned in `.conclave/reviewers.toml`), close a file through `process-todo-file`, run a phase or the plan through `process-phase` or `process-plan`, harden the tree through `groom-plan`. The lifecycle is: capture, author, validate, the one-pass plan review (its gaps fixed in the section before any code), record `Started:`, implement, run the section's gates, commit, the one-pass completion and excellence review (its in-scope gaps built, the rest filed), review rounds and stamp, sync the plan.

## Architecture rules that are not negotiable in passing

These come from foundation plan sections 3 and 4. A change that needs to break one records the correction in the plan first, dated, with the cost of reversing it.

- `recto-pdf` (`src/pdf`, compiled as C++23, D14) is Recto's own native PDF 1.7 and 2.0 engine: zero MuPDF, zero C `setjmp`/`longjmp`, and zero PDF JavaScript execution by construction (D3, D9, D12). Every parser, stream filter, decryptor, digital signature verifier/signer (`crypt32.dll`/`bcrypt.dll`), content-stream interpreter, operator editor, form/annotation engine, redactor/sanitizer, PDF/A & PDF/UA & ZUGFeRD/Factur-X compliance engine, linearizer, and writer operation returns typed `PdfResult<T>` / `PdfError` values and bounds recursion and allocation against hostile inputs.
- One `DocumentActor` thread per open document owns its `recto::pdf::Document` instance and page object graph; immutable, reference-counted `DisplayList`s and `PageText` snapshots are the only page data that crosses threads to `RenderPool` workers; workers rasterize tiles in parallel with cooperative `CancelToken` abort; actors and workers join before `Runtime` shuts down. Files that touch this contract are `frozen: true` and carry a Freeze check per section.
- The UI thread never parses or renders PDFs and never blocks on an engine thread. Conversion, authoring, redaction, signing, optimization, and OCR jobs open their own `recto::pdf::Document` instance; they never share the viewer's.
- Built-in OCR (High-Accuracy In-Process C++23 Neural OCR + `Windows.Media.Ocr` + C++23 image preprocessing + morphological line removal + XY-cut/table layout segmentation + per-character CTC quad alignment + vector-outlined-text OCR + searchable-PDF invisible-text layer synthesis) and 100% pure native C++23 document converters (3-mode `PDF -> Word .docx/.rtf` Flowing/Exact/Continuous + orthogonal-line & grid table reconstruction, `PDF -> Excel .xlsx/.csv/.xml`, `PDF -> HTML5/Markdown/EPUB/LaTeX/SVG/XPS/Multipage TIFF/Animated GIF`, and built-in `Office (.docx/.docm/.xlsx/.xlsm/.pptx/.pptm), Legacy Binary Office (.doc/.xls/.ppt), RTF, HTML, Markdown, XPS, SVG -> PDF` layout engines) live in `recto-core` and `recto-services` with zero external runtimes, subprocesses, or Office/LibreOffice dependencies (D5, D7, D10).
- Treat every PDF, Office, and HTML file as hostile: OpenXML (`vbaProject.bin`) and legacy OLE2 (`_VBA_PROJECT_CUR`, `Macros`, `VBA`) macro streams are stripped by construction and never parsed or executed, HTML scripts are disabled, document links and portfolio attachments ask before opening, PDF launch and JavaScript actions never execute, and true redaction strips underlying content stream objects before drawing redaction overlays.
- Every file write is temp-file-then-replace in the destination folder. Organize and authoring operations always work on a copy; the open viewer document is never modified in place.

## Review failure modes

The generic `review-todo-section` and `independent-review` skills check the failure modes this file names for the area a change touches. For Recto they are:

- A `recto::pdf::Document` call on the wrong thread, a document instance shared across threads instead of owned by one `DocumentActor`, or unbounded recursion or allocation on hostile PDF, Office or HTML input.
- An engine or parser function that throws, aborts or `longjmp`s instead of returning `PdfResult<T>` / `PdfError`, or a malformed-input failure reported as success.
- A tile cached after abort, a bitmap stride assumed to be `width * 4`, or a `CancelToken` polled without cooperative abort semantics.
- A file write that is not temp-file-then-replace, or an original modified in place by an Organize or authoring operation.
- A macro stream parsed or executed, an HTML script enabled, a PDF launch or JavaScript action run, or a document link or attachment opened without asking.
- A hex colour, font size or glyph set directly on a control instead of Recto's generated theme roles and Fluent UI System Icons through the `RectoIcons` font (`vellum::Icon`).
- A file under `libs/vellum` that includes a header from `src/**`, names Recto (a brand string, `RECTO_` macro, Recto path, colour or icon), or hard-codes a consumer identity instead of taking it from the consumer (foundation plan D23, `D10 T04 §2`).
- A glyph inside a filled or tinted tile, or a control radius other than the SPEC section 7 system (4 epx controls, 8 epx overlays, pills only for chips, keycaps, badges and counters).
- A setting nothing reads, a command unreachable from the command box (Ctrl+K), or an icon-only control without an automation name and tooltip.

Review lenses read against Recto's sources: `adversarial` covers malformed PDFs, races, aborts, macros and links; `integration` covers the actor contract, tile hand-off, job queue and shell slots; `source-defect` covers ISO 32000-1/-2, ECMA-376, the Win32, DirectComposition, Direct2D, DirectWrite and UI Automation documentation, and the SPEC.

## Interface rules

- `design/SPEC.md` wins over any screenshot, mockup or memory. Colours come only from `design/tokens/colors.json` through Recto's generated theme roles (`D10 T01 §8`, `D10 T04 §3`); no control sets a hex value. Glyphs are Fluent UI System Icons through the `RectoIcons` font (SPEC section 6), never inside a filled or tinted tile. Corner radii follow SPEC section 7. Vellum controls first (they reproduce the Fluent controls the SPEC was written against); a new general-purpose control goes into `libs/vellum` with no Recto branding, never a one-off in `src/app`, while Recto's shell, theme, icons and page surface stay in `src/app` (D22).
- One app shell, three layout presets (Focus default, Classic, Reading room) through settings and named slots: never three shells (D17).
- Every toolbar and menu command is reachable from the command box (Ctrl+K), every icon-only button carries an automation name and tooltip, and every control on a SPEC surface is accounted for as working or deferred to a named section before the section stamps.

## Working rules

- **Self-contained and portable:** `scripts/` uses 100% Python standard library with zero third-party Python packages and zero hardcoded machine or user paths in tracked configs. Portable LSP servers (`scripts/clangd-lsp.py`, `scripts/python-lsp.py` with stdlib AST fallback, and `scripts/lsp_mcp.py` wired through `.lsp.json`, `.mcp.json` and `.agents/plugins/recto-lsp/`) discover `clangd`, MSVC/SDK headers (`scripts/gen-compile-commands.py`) and `pyright-langserver` dynamically and auto-idle after 5 minutes.
- **Balanced local-vs-CI cadence:** run the iteration gate while building and after fixes (`.\build.cmd -Platform x64 -Configuration Debug -TestFilter '<the section's tests>'` + `-EnableASAN` when named for C++; Python suites via `unittest` / `python scripts/run-tests.py --quick` for Python-only; both for mixed, preserving each section's actual Test checkpoint) with `lsp_diagnostics`/`lsp_symbols`/`lsp_definition`, push candidates so GitHub Actions (`build.yml` x64/ARM64/ASan and `plan.yml` `run-tests.py --full`) runs in parallel with Astra's review (`scripts/ci_status.py --wait` / `--gate`), and run `.\build.cmd` once on the final candidate tip at stamp.
- **Output discipline:** bound every command (filtered tests, `Select-Object -First`, field extraction). Keep full logs in ignored scratch (`R:\scratch` or `build/`).
- **Writes are serial:** one session owns the working tree. Check `git status` before building over unfamiliar work.
- **Subagents take the mechanical work:** output the main session only needs the conclusion of goes to read-only helpers: `.claude/agents/` (`test-runner`, `git-runner`, `code-explorer`, `mutation-tester`, `debug-evidence` on Sonnet 5.5, held by `node scripts/verify-agent-tiers.mjs`) in Claude Code, and `gemini-4-hydrogen-medium` workers on `swarm` (`workspace: "inherit"`, `.agents/agents/README.md`) or `research` subagents in Antigravity, mechanically locked to read-only by `scripts/dispatch_hook.py` and `scripts/hydrogen_exec.py`. The writer session runs `scripts/mutate-check.py`, verifies every helper's citation/exit code, and keeps 100% of writing, diagnosis, design and verdicts.
- **Fix at the owning layer:** a setting in its setting, behaviour in the code that produces it, a token in the token file. No output filters that hide a problem. Code changes end the report with a `Layer:` line.
- **Tests prove claims:** a section's Test checkpoint must be able to fail, and is quoted, not paraphrased, in its stamp. Engine and threading claims run under AddressSanitizer where the plan names it.
- **User-reported bugs:** a fix carries `Fixes-User:` and `Regression-Test:` trailers and a test that fails before it, and the issue closes only on release; the rule is `docs/support/user-bugs.md`.
- **No em dashes or en dashes** anywhere: prose, code, comments, commit messages and generated files. One line per paragraph and list item in Markdown. Keep every section title (the `<Title>` in `## N. <Title>`, which is also its `Deliverable` cell) and index scope cell `<= 88` characters so Markdown tables never wrap.
- **Reviews run in the background:** run every review (`scripts/review-dispatch.py` with `--prompt`, passing `--writer claude` in Claude Code or `--writer agy` in Antigravity) through the runner's tracked background option (`run_in_background` in Claude Code; `WaitMsBeforeAsync: 500` and `IsDaemon: false` on `run_command` in Antigravity), never detached or in the foreground, so the session is notified the moment it finishes. `scripts/dispatch_hook.py`, wired as a PreToolUse hook in `.claude/settings.json` and `.agents/hooks.json`, refuses anything else.
- **Unattended run:** `scripts/start-unattended-run.ps1` (with Conclave-SDK `kits/unattended` in `.conclave/unattended.json`, `scripts/unattended-preflight.py`, `scripts/unattended-run.py` and `docs/unattended-prompt.md`) runs the plan with permission prompts off, `SetThreadExecutionState` sleep prevention and a session goal, in Claude Code on `opus` (`-Runner claude`, default) or Antigravity on Gemini 4 Carbon at medium (`-Runner agy`, owner direction 2026-11-12; reversing costs the `agy` branch and its test). Desktop shortcuts "Recto Claude campaign" and "Recto Carbon campaign" use badges rendered by stdlib `scripts/campaign-icon.py` into `%LOCALAPPDATA%\Recto`, and `-InstallShortcut` rewrites both after a clone move. Claude Code gets the one-year token from 1Password (`op` or `CLAUDE_CODE_OAUTH_TOKEN`) in its environment only and disables `AskUserQuestion`; Antigravity uses its own account, never gets that token, and both enforce the run guard (`SessionStart` CI gate, `PreToolUse` review/Hydrogen/`ask_question` guard, `Stop` hook in `scripts/campaign_guard.py`, and 5-minute heartbeat via `CronCreate` or `schedule`). Only the owner starts a run: nothing restarts a dead session (owner direction 2026-11-12).
- **Dependency pull requests:** `scripts/merge_prs.py` merges green Dependabot and pins PRs one at a time from a clean clone in sync with `origin/main` with no section in progress (`process-phase` after each stamp push, and `recto-pr-merge` when idle). Every other PR is merged by the operator.
- **Commits:** always straight on `main` (owner direction, repeated 2026-11-10): no feature branch, extra worktree or writer PR in attended or unattended runs. Only automation opens branches (Dependabot and `pins/` proposals, merged by `scripts/merge_prs.py`). Concise imperative messages, section ref in body. Never amend a candidate under review, bypass hooks or force-push. Every clone runs `git config core.hooksPath scripts/hooks`: pre-commit refuses TODO edits committed without `plan --sync`.
- **Credentials** never enter tracked files, arguments or logs. Store and signing material lives outside the repository.

## Unknowns and questions

Answer from source first: the foundation plan, the SPEC, the ISO 32000-1 / 32000-2 PDF specifications, ECMA-376 OpenXML, and Microsoft Learn for Win32, DirectComposition, Direct2D, DirectWrite, UI Automation, Text Services Framework, WIC, CNG and `Windows.Media.Ocr`. When an unanswered question would change implementation, take the industry-standard default, record that it is a default with its cost of changing, and carry on. Questions that truly need the owner become a section Need (`owner-decision`, `third-party`), not a stall.

## Validation

```powershell
build\python.cmd scripts/todo-graph.py validate      # graph integrity; any fault exits 2 with reason
build\python.cmd scripts/todo-graph.py plan --check  # generated plan is current (CI runs this)
build\python.cmd scripts/todo-graph.py query ready   # dependency-safe work right now
build\python.cmd scripts/todo-graph.py query blocked # sections waiting, and on what
build\python.cmd scripts/todo-graph.py query stats   # tree health
build\python.cmd scripts/todo-graph.py query deferred # open and resolved deferrals
build\python.cmd -m unittest tests.scripts.test_todo_graph # tooling tests, including live tree
build\python.cmd scripts/check-sdk-pins.py           # C++/WinRT and build tools pins are latest stable (D4)
build\python.cmd scripts/check-tool-pins.py check    # .tools/ Ninja, CMake, sccache and CPython pins (D4)
build\python.cmd scripts/check-windows-sdk-line.py check # Recto targets newest Windows SDK line (D4)
build\python.cmd scripts/check-actions-pins.py check # GitHub Actions are on latest stable majors (D4)
build\python.cmd scripts/check-vcpkg-baseline.py check # vcpkg baseline is latest vcpkg release (D4)
build\python.cmd scripts/check-python-pin.py check   # CI and .tools/ Python are newest stable CPython (D4)
build\python.cmd scripts/check-visual-studio.py check # Visual Studio is at latest servicing release
build\python.cmd scripts/ci_status.py                # main's latest build.yml and plan.yml runs
build\python.cmd scripts/gen-compile-commands.py --check # C++23 clangd database matches Recto.sln
build\python.cmd scripts/run-tests.py --quick        # fast RAM-bounded local Python cadence (+ --full in CI)
.\build.cmd -Platform x64 -Configuration Debug -TestFilter '<the section's tests>'  # iteration gate while building and after each review fix (+ -EnableASAN when named; Python-only via unittest/run-tests.py; both for mixed)
.\build.cmd                                  # stamp gate: x64 Debug and Release, every test, once on the candidate tip that stamps, quoted in stamp --check (-Platform x64,ARM64 adds ARM64)
```

Report only commands actually run, and distinguish static evidence, test output and review proof.
