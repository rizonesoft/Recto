# Review: merging dependency pull requests automatically

- **Candidate:** `5eb0713`

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T19:57:48Z
- Effort: medium
- Lines: 28
- Digest: c3578a9df2bf2e4d

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:68`: Campaign exclusion has a race.** The scheduled run checks guard existence once, without acquiring the campaign lock. A campaign can acquire its guard during `git fetch` or PR discovery, after which the scheduled run still merges and pulls into its working tree. Reproduce by making guard existence return false initially and true afterward: the pinned candidate reads it once and issues the merge. Coordinate exclusion with campaign acquisition for the entire mutation window.

## consistency: approve

No additional findings.

## integration: needs-attention

2. **P1, `scripts/merge_prs.py:82`: Incomplete CI evidence authorizes merging.** Any nonempty successful rollup qualifies, and even an entirely `SKIPPED` rollup qualifies. The pins producer dispatches `build.yml` and `plan.yml` separately; if one dispatch fails, the other workflow alone can authorize merging. Reproduce with an otherwise eligible PR containing only one successful check, or only a completed skipped check: `why_not()` returns `None`. Require the applicable build and validation evidence, including an actual successful build.

3. **P2, `scripts/merge_prs.py:111`: PR targets are unrestricted, but verification always pulls `main`.** Discovery neither requests `baseRefName` nor filters with `--base main`. Retarget an eligible bot PR to another branch: the script merges that branch, then pulls and validates unrelated `main`, reporting success. Restrict eligibility to the branch being synchronized and validated.

## source-defect: needs-attention

4. **P1, `scripts/merge_prs.py:123`: “Every check green” is evaluated from a stale batch snapshot.** All PR states are fetched before any merges. While the first PR merges, a later PR’s checks can be rerun and fail without changing its head SHA. The later merge still uses the original successful rollup; `--match-head-commit` only checks the SHA. Reproduce with two initially green PRs, then fail a rerun on the second while the first is processed. Refresh eligibility immediately before each merge.

## design: advisory

No user-facing design surface changed.

## record: needs-attention

5. **P2, `.conclave/automations/README.md:30` and `scripts/merge_prs.py:146`: Failure reporting incorrectly implies the PR remains open.** When merging succeeds but the subsequent pull or validation fails, the PR is already merged. The README promises it is left open, and the runtime says it is left for the operator. Reproduce by returning success for `gh pr merge` and failure for `git pull` or validation. Report the completed remote merge separately from the failed local follow-up.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 11 tests`, `OK` (against `5eb0713` the module cannot even load its required-check list); `python -m unittest discover -s scripts/tests -p test_automations.py` -> `OK (skipped=3)`. Live, against open PR #1 (`pins/vcpkg-2026.10.16`): its three required jobs succeeded on the head commit and GitHub reports it mergeable once asked through `gh pr view`.

- Finding 1 (P1): the scheduled run holds the campaign guard's lock (`campaign_guard._Lock`) from the guard check to the end of its window, so a campaign's `acquire` waits rather than starting mid-merge (`test_the_scheduled_task_holds_the_guard_lock_for_its_whole_window`).
- Finding 2 (P1): checks are read from the head commit (`gh api .../commits/<sha>/check-runs`), because GitHub does not list the pins workflow's dispatched runs on the pull request; every check must have finished green, and the build, ASan and plan-gates jobs must each have succeeded (skipped is not enough).
- Finding 3 (P2): only open pull requests into `main` are listed and eligible.
- Finding 4 (P1): each pull request is re-read (`gh pr view`, which also settles an `UNKNOWN` mergeable state) and its checks fetched right before it is judged, after any earlier merge (`test_each_pull_request_is_judged_as_it_is_right_before_its_merge`).
- Finding 5 (P2): a failed merge reports the pull request as still open; a failed pull or validate after a merge reports it as merged on GitHub, and the README's table says the same.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:02:38Z
- Effort: medium
- Lines: 28
- Digest: c60e26a3559b0bfd

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:92`: Failing commit statuses are invisible to the merge gate.** The script reads only check runs. GitHub’s legacy commit statuses are a separate API and can report pending or failing CI on the same SHA. Reproduce with successful required check runs plus a failing status context: the script never requests that context and proceeds to merge if branch protection does not independently block it. The mocked probe confirmed the merge path queries only `/check-runs`. Read both forms of CI evidence before authorizing a merge. [GitHub commit-status documentation](https://docs.github.com/en/rest/commits/statuses#get-the-combined-status-for-a-specific-reference)

## consistency: approve

No additional findings.

## integration: needs-attention

2. **P2, `scripts/merge_prs.py:45`: Most Dependabot updates cannot satisfy the required checks.** `plan-gates` is mandatory, but `.github/workflows/plan.yml:27` filters pull requests to specific paths, including only `plan.yml` among workflow files. A Dependabot update confined to `build.yml` or another workflow therefore never produces `plan-gates`. The explicit dispatch mechanism described for pins does not cover these Dependabot updates. Reproduce with an otherwise eligible Dependabot PR containing successful build and ASan checks: the mocked probe returned exit 0, skipped the PR, and merged nothing. Ensure these PRs actually receive the required validation.

## source-defect: approve

No additional findings beyond the CI evidence mismatch in finding 1.

## design: advisory

No user-facing design surface changed.

## record: needs-attention

3. **P3, `.conclave/automations/README.md:35`: The email claim includes conditions that silently succeed.** The README says checks failing, conflicts, or a stopped merge are reported by email. Failed checks and conflicts instead take `scripts/merge_prs.py:160`’s skip path and ultimately return 0; the scheduler emails only on failure or timeout. Reproduce with a conflicting PR or failed required check: the script reports a skip and exits successfully, so no failure email follows. Narrow the documentation to actual merge or follow-up failures, or implement the promised reporting.

Validation used read-only inspection and in-memory mocked probes, not live merges or the full test suite. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 12 tests`, `OK`. Live, PR #1: GitHub first answered `UNKNOWN`, then `MERGEABLE` on a re-read, and the PR is judged eligible.

- Finding 1 (P1): legacy commit statuses are read too (`gh api .../commits/<sha>/status`); any status that is not `success` (failure, error or pending) blocks the merge (`test_a_failing_or_pending_commit_status_blocks_the_merge`).
- Finding 2 (P2): `.github/workflows/plan.yml` now runs on any change under `.github/workflows/` and on `.github/dependabot.yml`, in both its push and pull-request filters, so a Dependabot action update gets `plan-gates` like every other pull request.
- Finding 3 (P3): the README no longer promises email for skipped pull requests: only a stopped merge, or a failed pull or validate after one, emails; `merge_prs.py --dry-run` says why each other one waits.

Also: GitHub computes mergeability in the background, so `current()` re-reads a pull request still `UNKNOWN` up to five times, three seconds apart, before skipping it for that run.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:06:05Z
- Effort: medium
- Lines: 26
- Digest: 10d31ed2f6457db6

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:102`: A failing status beyond the first page does not block merging.** `commit_statuses()` neither paginates nor reads the combined `state`. GitHub defaults this endpoint to 30 results per page. Reproduce with successful required check runs, 30 successful status contexts on page 1, and a failing context on page 2. The in-memory probe returned exit 0 and merged PR #1. Paginate the statuses or evaluate the combined state with explicit handling for no statuses. [GitHub API documentation](https://docs.github.com/en/rest/commits/statuses#get-the-combined-status-for-a-specific-reference)

## consistency: approve

No additional findings.

## integration: needs-attention

2. **P1, `scripts/merge_prs.py:161`: Clone safety is checked only once for the entire batch.** Subsequent merges do not recheck working-tree cleanliness, synchronization, or whether a section has started. The campaign lock does not prevent ordinary user edits. Reproduce with two eligible PRs and make the working tree dirty after the first merge. The in-memory probe merged both PRs, returned exit 0, and recorded only one `git status` call. A nonconflicting edit can survive `git pull --ff-only`, so that command does not enforce the promised clean-tree gate. Recheck safety before each merge and stop when it changes.

## source-defect: needs-attention

Finding 1 violates the upstream pagination contract. Finding 2 violates the governing requirement that merges run only from a clean, synchronized clone with no section in progress.

## design: advisory

No user-facing design surface changed.

## record: advisory

Validation used candidate and fix-diff inspection plus two in-memory mocked probes with bytecode generation disabled. The full test suite and historical live-check claims were not independently reproduced. No files were modified by this review.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 14 tests`, `OK`.

- Finding 1 (P1): commit statuses are read with `--paginate` and `per_page=100`, so a failing status past the first 30 still blocks (`test_commit_statuses_are_read_across_every_page`).
- Finding 2 (P1): the safety check (clean tree, `HEAD` at `origin/main`, no section in progress, and for the scheduled run no campaign guard) runs again before every pull request after the first; a change stops the run, and later pull requests wait (`test_safety_is_checked_again_before_each_merge`).

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:17:03Z
- Effort: medium
- Lines: 26
- Digest: b9f6ab216d7eb012

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:198`: The merge target is not bound to the repository whose safety was checked.** Git checks and subsequent validation use the local clone’s `origin/main`, but every `gh` command inherits repository selection independently. An inherited `GH_REPO` can therefore redirect listing, checks and merges to another repository while Recto passes all local gates. Reproduce in a controlled fixture with `GH_REPO` pointing to another repository containing an eligible PR with the required check names. The script can merge that PR, pull unchanged Recto, and report success. The in-memory probe confirmed no explicit repository binding. Resolve and verify the target against `origin`, then bind all GitHub operations to it. [GitHub CLI environment contract](https://cli.github.com/manual/gh_help_environment)

## consistency: needs-attention

2. **P2, `scripts/merge_prs.py:60`: Subprocess timeouts bypass the promised failure reporting.** `subprocess.run(timeout=600)` raises `TimeoutExpired`, but the surrounding handlers catch only `RuntimeError`. Reproduce by having the injected runner raise `TimeoutExpired` during `gh pr merge`: the exception escapes with no state report, as confirmed by the in-memory probe. The remote merge may already have completed. Also, a nonzero merge exit unconditionally reports “still open” at line 201 without checking GitHub. Handle transport failures and timeouts explicitly, and read back the PR state before claiming whether it merged; report unknown when verification fails.

## integration: needs-attention

Finding 1 disconnects remote mutations from local synchronization and validation: those checks can succeed against a different repository.

## source-defect: needs-attention

Finding 1 overlooks documented `GH_REPO` behavior. Finding 2 violates the script’s stated contract to distinguish a failed merge from a completed merge whose follow-up failed.

## design: advisory

No user-facing design surface changed.

## record: advisory

Reviewed the candidate diff and follow-up fixes, and ran in-memory probes with bytecode generation disabled. No files were modified. The full test suite and historical live-check claims were not independently reproduced. Live GitHub inspection was blocked by the network proxy.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 4

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 17 tests`, `OK`. Live: `origin_repo` -> `rizonesoft/Recto`; PR #1 judged eligible with every call bound to it.

- Finding 1 (P1): the GitHub repository is read from `origin` (`origin_repo`, refusing a non-GitHub origin), and every `gh` call names it (`--repo`, or the explicit `repos/<owner>/<name>/` API path), so an inherited `GH_REPO` cannot redirect a merge (`test_every_github_call_names_the_repository_behind_origin`).
- Finding 2 (P2): a timeout or a failure to start a command becomes a reported failure, and a failed or timed-out merge reports the pull request's state read back from GitHub (or that it is unknown) instead of assuming it is still open (`test_a_timed_out_merge_reports_what_github_says`).

## Round 5: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:25:30Z
- Effort: medium
- Lines: 28
- Digest: 058dfd1686b945d9

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:102`: Rejected origin URLs can disclose credentials.** The exception includes the complete URL, including any embedded username/password or token. The scheduled runner saves this diagnostic and emails its log tail. Reproduce by mocking `git remote get-url origin` to return a credential-bearing HTTPS URL. An in-memory probe confirmed the synthetic credential appears in the exception. Reject unsupported origins without including their raw URL.

2. **P1, `scripts/merge_prs.py:133`: GitHub Actions identity does not establish pins-workflow provenance.** Any PR authored by `github-actions[bot]` with either accepted label passes this gate, regardless of its originating workflow, branch or changed files. This exceeds the authorization to merge Dependabot and pins-workflow PRs. Reproduce with an Actions-authored feature PR on `unrelated-feature`, labelled `dependencies`, with successful required checks: `why_not()` returns `None`, confirmed in memory. Verify the approved producer and permitted change scope.

## consistency: needs-attention

Finding 1 violates the repository’s prohibition on credentials entering logs. Finding 2 violates the rule reserving other PRs for operator merging.

## integration: needs-attention

3. **P2, `scripts/merge_prs.py:225`: Successful CLI exit is treated as a completed merge without verification.** With a merge queue required, `gh pr merge` can successfully enqueue a PR instead of merging it. The script immediately reports it merged, pulls and validates unchanged `main`, then processes another PR. This defeats the promised merge, synchronize, validate sequence. Reproduce with two eligible PRs targeting a queue-protected branch. The equivalent mocked probe left both PRs `OPEN` while returning `merge-prs: 2 merged`. Read back successful requests and verify the resulting merge reached the local checkout before continuing. [GitHub CLI merge contract](https://cli.github.com/manual/gh_pr_merge)

## source-defect: needs-attention

Finding 2 substitutes a shared bot identity for the governing document’s specific workflow authorization. Finding 3 overlooks GitHub CLI’s documented queue behavior.

## design: advisory

No rendered interface changes require review.

## record: advisory

Inspected the candidate, follow-up fixes, governing documents and recorded review evidence. Ran read-only, in-memory probes with bytecode generation disabled. Historical test counts and live-GitHub claims were not independently reproduced. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 5

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 20 tests`, `OK`. Live PR #1 (`pins/vcpkg-2026.10.16`, changing only `vcpkg-configuration.json`) is eligible.

- Finding 1 (P1): a refused `origin` is reported as "origin is not a github.com repository", never with its URL (`test_an_origin_url_never_reaches_the_output`).
- Finding 2 (P1): provenance is author, branch and files together (`PRODUCERS`): the pins workflow on `winui/<version>` or `pins/toolchain-*` / `pins/vcpkg-*` changing only `src/app/Recto.vcxproj` or `vcpkg-configuration.json`; Dependabot on `dependabot/*` changing only files under `.github/`. A github-actions pull request on another branch, or touching other files, is left for the operator (`test_provenance_is_branch_and_files_not_just_the_author`).
- Finding 3 (P2): after `gh pr merge` succeeds, the pull request is read back; only `MERGED` with a merge commit counts, and that commit must be in the checkout after `git pull --ff-only` (`git merge-base --is-ancestor`). A queued or refused merge stops the run (`test_a_queued_merge_stops_the_run`).

## Round 6: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:31:56Z
- Effort: medium
- Lines: 26
- Digest: 8b442a5ecc9e7364

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:261`: Failed validation does not prevent the next scheduled run from merging more PRs.** After a merge lands and `todo-graph validate` fails, the script returns without recording a stop condition. The checkout can remain clean and equal to `origin/main`, so the next invocation passes `ensure_safe()` and merges another PR before validating again. Reproduced in memory using two eligible PRs and persistent validation failure: two successive invocations both returned 1, but merge calls were `['1', '2']`. Require recovery validation before another merge and preserve the promised operator handoff across invocations.

## consistency: needs-attention

2. **P2, `scripts/merge_prs.py:63`: Scheduled subprocesses omit Windows console suppression.** The automation runner launches this script without a console, but `run_cmd()` starts Git, GitHub CLI and Python without `CREATE_NO_WINDOW`. These nested launches can create visible console windows, contrary to the automation runner’s explicit contract. Reproduce by launching through the scheduled runner on Windows; an in-memory subprocess probe confirmed `creationflags` is absent. Apply the Windows flag at this subprocess boundary. [Microsoft process creation flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags)

## integration: needs-attention

Finding 1 affects the recurring `recto-pr-merge` task: its next execution resumes merging despite unresolved validation failure. Finding 2 affects its Windows launcher.

## source-defect: needs-attention

Finding 1 contradicts `.conclave/automations/README.md`’s promise that later PRs are left for the operator after failure. The implementation stops only the current process.

## design: advisory

No rendered interface changes to assess.

## record: advisory

Inspected the candidate and follow-up commits. Ran read-only, in-memory probes with bytecode generation disabled. Historical test counts and live GitHub claims were not independently reproduced. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 6

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 22 tests`, `OK`.

- Finding 1 (P1): every stop after a merge request (a failed or queued merge, an unreadable state, a failed pull, ancestry check or validate) writes `build/merge-prs-stopped.json`; every later run, scheduled or the campaign's, refuses with exit 1 until the operator checks `main` and deletes it (`test_a_stop_holds_until_the_operator_clears_it`). The README says so.
- Finding 2 (P2): `run_cmd` passes `CREATE_NO_WINDOW` on Windows, so the scheduled task's git, gh and Python launches open no console (`test_launches_open_no_console_window`).

## Round 7: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:37:37Z
- Effort: medium
- Lines: 24
- Digest: c05df461b7a1d83b

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:250`: Process termination bypasses the persistent stop safeguard.** The stop marker is written only after a caught failure. The scheduled runner forcibly kills the process tree after 20 minutes (`.conclave/automations/run.py:156`). If termination occurs after pulling a merged PR but before validation completes, the checkout can remain clean and synchronized without a stop marker. The next run merges further PRs without operator recovery. Reproduced in memory by interrupting validation and invoking again: marker absent, merge requests `['1', '2']`. Persist an unresolved operation before requesting the merge, clearing it only after verified follow-up.

## consistency: approve

## integration: needs-attention

2. **P1, `scripts/merge_prs.py:289`: Unscheduled invocations bypass serialization entirely.** Only `--scheduled` acquires a lock. A manual invocation can overlap another invocation after its pull but before validation, when the checkout already satisfies the safety checks. It can merge another PR before the first operation is validated, defeating the one-at-a-time and stop-on-failure contract. Reproduced with an in-memory second invocation at that boundary: `merge #1, merge #2, validate, validate`. Require a shared merge-operation lock for every invocation, while retaining the scheduled campaign exclusion.

## source-defect: needs-attention

Findings 1 and 2 violate `.conclave/automations/README.md`’s promised merge, synchronize, validate sequence and operator handoff after failure.

## design: advisory

No rendered interface changes to assess.

## record: advisory

Inspected the candidate, follow-up commits and recorded evidence. Ran in-memory probes with bytecode generation disabled; no files were modified. Historical test counts and live GitHub claims were not independently reproduced.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 7

`python -m unittest tests.scripts.test_merge_prs` -> `OK` (both new tests fail against `7eeca30`).

- Finding 1 (P1): the stop marker is written before GitHub is asked to merge and deleted only after the pull, the ancestry check and validate all pass, so a run killed in between (the task's timeout, a crash) leaves it, and the next run refuses (`test_a_run_killed_mid_merge_leaves_the_stop_marker`).
- Finding 2 (P1): every run, not only the scheduled one, holds the campaign guard's lock for its whole window, so two runs can never interleave a merge with another's pull and validate (`test_every_run_holds_the_guard_lock`, which also shows a verified merge clears its marker).

## Round 8: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:42:40Z
- Effort: medium
- Lines: 26
- Digest: f992c6091045145e

## adversarial: needs-attention

1. **P2, `scripts/merge_prs.py:171`: A draft dependency PR can halt all automatic merging indefinitely.** `FIELDS` omits `isDraft`, and `why_not()` accepts an otherwise eligible draft. GitHub refuses its merge, causing the script to persist the stop marker and block subsequent runs, including unrelated eligible PRs. Draft PRs cannot be merged. [GitHub documentation](https://docs.github.com/en/pull-requests/reference/pull-requests)

   Reproduce by converting a green pins PR to draft while another eligible PR waits, then invoking the automation. An in-memory probe confirmed eligibility returned `None`, the draft received a merge attempt, execution returned 1, and the stop-marker writer was called. Fetch `isDraft` and skip drafts before requesting a merge; add regression coverage showing a later eligible PR still proceeds.

## consistency: approve

## integration: needs-attention

Finding 1 turns an intentionally deferred PR into a persistent outage of both scheduled and campaign-boundary merging. Making the PR ready again does not clear the local stop marker.

## source-defect: needs-attention

Finding 1 treats conflict-free `MERGEABLE` status as sufficient readiness without checking GitHub’s separate draft restriction.

## design: advisory

No rendered interface changes to assess.

## record: advisory

Inspected the candidate, follow-up changes and review record. Ran an in-memory mocked reproduction with bytecode generation disabled. No files were modified. Historical test results and live GitHub claims were not independently reproduced.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 8

`python -m unittest tests.scripts.test_merge_prs` -> `OK` (`test_a_draft_is_skipped_and_later_pull_requests_still_merge` fails against `38715aa`).

- Finding 1 (P2): `isDraft` is read, and a draft (or a pull request whose draft state is not reported) is skipped, never attempted, so it cannot trip the stop marker; a later eligible pull request still merges.

## Round 9: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:48:44Z
- Effort: medium
- Lines: 26
- Digest: 611e8bee193a5bb5

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:115`: Repository selection drops the hostname, allowing merges against another GitHub server.** `origin_repo()` returns only `owner/name`; subsequent `gh pr` calls omit the host, and `gh api` calls omit `--hostname`. An inherited `GH_HOST` therefore redirects both checks and merges away from the verified `github.com` origin. With an authenticated Enterprise server containing the same repository name and an eligible PR, the script can merge there before local follow-up detects the mismatch.

   Reproduced without external requests: set `GH_HOST=review-host.invalid`, use a synthetic credential and a refused localhost proxy, then execute the generated read commands. Both `gh pr list --repo rizonesoft/Recto` and `gh api repos/rizonesoft/Recto/...` attempted `review-host.invalid`. This follows [GitHub CLI’s documented host selection](https://cli.github.com/manual/gh_help_environment). Preserve `github.com` in every `--repo` argument and explicitly set `--hostname github.com` for API calls.

## consistency: approve

## integration: needs-attention

Finding 1 breaks the contract between the repository checked by Git and the repository mutated by GitHub CLI.

## source-defect: needs-attention

Finding 1 contradicts `origin_repo()`’s explicit guarantee that inherited defaults cannot redirect merges. Supplying `owner/name` does not pin the hostname.

## design: advisory

No rendered interface changes to assess.

## record: advisory

Inspected the candidate and round 8 fix. Ran the isolated CLI routing reproduction; no files were modified or live merges attempted. Historical test results were not independently reproduced.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 9

`python -m unittest tests.scripts.test_merge_prs` -> `OK` (the extended test fails against `70991a4`). Live: `origin_repo` -> `github.com/rizonesoft/Recto`; PR #1 eligible.

- Finding 1 (P1): the host is pinned in every call: `gh pr` commands take `--repo github.com/<owner>/<name>` and `gh api` calls `--hostname github.com`, so neither `GH_REPO` nor `GH_HOST` can point a read or a merge at another repository or server (`test_every_github_call_names_the_repository_behind_origin`, run with both set).

## Round 10: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:56:23Z
- Effort: medium
- Lines: 26
- Digest: 28e020dcee4fe764

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:254`: Failing test-merge checks are invisible to eligibility.** Both check queries use only `headRefOid`; `FIELDS` also omits the PR’s check rollup. A green branch head can therefore authorize merging despite failing checks on its combination with current `main`, unless branch protection independently refuses it.

   Reproduce with an eligible bot PR whose head has successful required jobs, but whose test merge commit has a failing status. The script queries only the head and requests the merge. An in-memory probe with successful head checks and a failing PR rollup returned exit 0 and attempted merge `#1`. Read the test-merge evidence alongside the dispatched head checks and reject failures or pending checks.

## consistency: approve

## integration: needs-attention

Finding 1 disconnects merge eligibility from integration failures against the target branch. Post-merge `todo-graph validate` cannot detect build or runtime failures and runs after the remote mutation.

## source-defect: needs-attention

Finding 1 overlooks GitHub’s distinction between head checks and test-merge checks. GitHub documents that when the test merge commit has statuses, those determine the required-check outcome. Head-only evidence is insufficient. [GitHub documentation](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/troubleshooting-required-status-checks#conflicts-between-head-commit-and-test-merge-commit)

## design: advisory

No rendered interface changes.

## record: advisory

Inspected the candidate and final fix. Ran mocked, in-memory probes with bytecode generation disabled; no live merges or file modifications. Historical test results were not independently rerun.

HIGHEST: P1  
OVERALL: needs-attention

## Fixes after round 10

`python -m unittest tests.scripts.test_merge_prs` -> `OK` (`test_a_failing_or_pending_check_on_the_pull_request_blocks` fails against `3543c40`).

- Finding 1 (P1): the pull request's own check rollup (`statusCheckRollup`, which includes checks on GitHub's test merge with current `main`) is read too; any entry that is not a completed success, neutral or skipped check, or a successful status, blocks. An empty rollup stays allowed, because the pins workflow's dispatched runs never appear there and the head commit's required jobs already prove those changes build.

## Round 11: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:03:27Z
- Effort: medium
- Lines: 34
- Digest: 9a27d4f34dec0059

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:178`: Test-merge failures remain invisible.** The round 10 fix assumes `gh pr view --json statusCheckRollup` includes the test merge commit. Installed CLI version 2.102.0 actually queries `commits(last: 1)`, so this reads the PR head again. [CLI source](https://github.com/cli/cli/blob/v2.102.0/api/query_builder.go#L222-L257)

   Reproduce with an eligible pins PR whose head checks succeed but whose test merge against current `main` fails. The supplied head checks and rollup pass `why_not()`; my in-memory probe returned `None`. Without independent branch protection, the script can merge the failing combination. Query the current test merge SHA and its checks explicitly.

2. **P1, `scripts/merge_prs.py:97`: Fetch errors can disclose credentials before origin validation.** `ensure_safe()` fetches before `origin_repo()` rejects unsupported URLs, and line 77 copies raw stderr into output that the scheduled runner logs and emails.

   Reproduce with an origin containing a synthetic query credential, such as `https://example.invalid/repo.git?token=SYNTHETIC_REVIEW_TOKEN`, and a connection failure. A read-only Git transport probe confirmed that the query token survives in stderr; the candidate’s error handler preserves it. Validate the origin before fetching and prevent credential-bearing diagnostics from entering logs.

## consistency: needs-attention

Finding 2 violates the governing rule that credentials must never enter logs. Rejecting the URL later does not protect the earlier fetch error.

## integration: needs-attention

Finding 1 leaves integration failures against `main` outside the eligibility decision. Post-merge `todo-graph validate` cannot detect build failures and occurs after the remote mutation.

## source-defect: needs-attention

Finding 1 misreads the CLI field’s meaning. GitHub distinguishes head checks from test-merge checks and requires the latter when the test merge has statuses. [GitHub documentation](https://docs.github.com/en/pull-requests/how-tos/merge-and-close-pull-requests/troubleshooting-required-status-checks#conflicts-between-head-commit-and-test-merge-commit)

## design: advisory

No rendered interface changes to assess.

## record: needs-attention

3. **P2, `docs/reviews/merge-prs.md:431`: Round 10 incorrectly records the test-merge gap as fixed.** Its regression test injects a failing rollup directly rather than establishing that the CLI retrieves test-merge evidence. Reproduce using finding 1’s separate green-head/failing-merge scenario: eligibility still passes. Correct the evidence claim and test the actual retrieval contract.

Reviewed the candidate commit directly and ran read-only, in-memory probes with bytecode disabled. No files were modified, no merges were attempted, and the historical full test suite was not rerun.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 11

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 28 tests`, `OK` (the new tests fail against `0a2c93d`). Live: GitHub reports test-merge commit `f5bdcf9` for PR #1, read through GraphQL.

- Finding 1 (P1), and finding 3 (P2) on the record: round 10's claim was wrong. `gh pr view --json statusCheckRollup` reads the head commit, so the rollup never covered the test merge, and its test only injected a rollup. Now `test_merge_sha` asks GitHub for the pull request's `potentialMergeCommit` (GraphQL, host pinned), and when there is one, every check run and status on it must have finished green (`test_a_failing_or_unfinished_test_merge_blocks`); with none, no check ran there and the head commit's required jobs stand. The rollup comment now says it reads the head.
- Finding 2 (P1): `origin` is resolved (a local read) before any fetch, so a non-github.com remote is refused before a network error could quote it, and every failure message passes through `redact`, which replaces URLs and scp-style remotes (`test_a_fetch_error_never_shows_the_remote`).

## Round 12: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:13:51Z
- Effort: medium
- Lines: 28
- Digest: e305f88efefdcf05

## adversarial: needs-attention

1. **P1** · `scripts/merge_prs.py:293` and `scripts/merge_prs.py:311`: The merge target can change after eligibility is checked. Retarget an eligible PR from `main` to another mergeable branch while the subsequent check queries run, without changing its head SHA. The script still invokes the merge. `--match-head-commit` protects only the head SHA, not the base branch, as confirmed by [GitHub CLI’s implementation](https://github.com/cli/cli/blob/trunk/pkg/cmd/pr/merge/merge.go). The later pull and ancestry check cannot undo a merge into an unauthorized branch.

   Reproduced in memory using the candidate’s `FakeRun`: change `baseRefName` to `release` during the GraphQL query after eligibility checking. The merge command is still issued for that PR. Preventing this requires target-branch enforcement at the mutation boundary; another preliminary read alone leaves a race.

## consistency: approve

No additional findings.

## integration: approve

No additional findings.

## source-defect: needs-attention

Finding 1 violates the governing requirement that automated merges target `main`.

## design: approve

No user-facing surface changed.

## record: advisory

Inspected the candidate and final corrective commit. Ran the in-memory reproduction with filesystem mutations mocked. Did not run the full test suite or perform a live merge; GitHub CLI network access was blocked by the sandbox proxy. No files modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 12

`python -m unittest tests.scripts.test_merge_prs` -> `Ran 29 tests`, `OK` (`test_a_base_retargeted_before_the_merge_is_skipped` fails against `7ecb540`).

- Finding 1 (P1): GitHub's merge call can pin the head commit (`--match-head-commit`) but offers no precondition on the base branch, so the window cannot be closed at the mutation itself (no API expresses it). The base is now read again immediately before the request, and a pull request no longer targeting `main` is skipped; a retarget in the last instant still lands somewhere other than `main`, which the follow-up then catches (its merge commit is not in `main`), stopping the run with the marker set. Only Dependabot and pins-workflow pull requests are eligible, and retargeting one needs write access to the repository.

## Round 13: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:21:47Z
- Effort: medium
- Lines: 30
- Digest: 6ceef64298b485c8

## adversarial: needs-attention

1. **P1, `scripts/merge_prs.py:311`: The final read discards updated eligibility information.** `current()` retrieves the full PR state, but only `baseRefName` is checked. Removing both accepted labels during the preceding check queries still allows the merge, even though the final response explicitly reports that the PR no longer qualifies. The subsequent ancestry check and validation cannot detect this policy violation.

   Reproduction: start with an eligible PR; have `FakeRun` return `labels=[]` on its second `gh pr view`, keeping the head SHA and base unchanged. The in-memory probe issued merge request `#1` and returned exit 0, while `why_not()` rejected that same updated state as `not labelled dependencies or pins`.

   Revalidate the final snapshot against the eligibility rules and bind it to the checked head. This defect occurs **before the final read completes**, independently of the acknowledged residual race after that read.

## consistency: approve

No additional findings.

## integration: needs-attention

Finding 1 also discards a newly reported draft state. The corresponding probe still issued a merge request. A real refusal then takes the persistent-stop path, blocking later scheduled and campaign invocations instead of skipping the draft.

## source-defect: needs-attention

Finding 1 violates `.conclave/automations/README.md`’s requirement that automatically merged PRs carry an accepted label.

## design: advisory

No rendered interface changes to assess.

## record: advisory

Inspected the candidate and recorded fixes. Ran in-memory reproductions with filesystem mutations mocked and bytecode disabled. No files changed or live merges attempted. Historical test results were not independently rerun.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 13

`python -m unittest tests.scripts.test_merge_prs` -> `OK` (`test_a_late_change_to_eligibility_skips_the_merge` fails against `283a841` for each change).

- Finding 1 (P1): the final read just before the merge request is judged in full by `why_not`, against the check runs and statuses already read for the same head commit, and a head that moved since is skipped too: a removed label, a switch to draft, a retarget or a new head commit each skip the pull request with the reason logged.