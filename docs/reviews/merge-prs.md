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