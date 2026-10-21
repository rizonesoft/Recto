# Audit - D00 T01 §4: Build CI for x64 and ARM64

- **Audit section:** `D06 T05 §7` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T01 §4` (`docs/reviews/d00-t01-s4.json`, 2 rounds under the earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

The newest green `build` run on `main` when the audit started, run `39636918376` for `e7cf65d` (`gh run view 39636918376 --json jobs` and `--log`): job "Build x64 & ARM64 and run recto-tests" succeeded with steps "Build Debug (x64 and ARM64) and run Debug|x64 recto-tests" and "Build Release (x64 and ARM64) and run Release|x64 recto-tests" both succeeding; the ASan job and the test-list note job also succeeded. Across the log: 5 x `0 Warning(s)`, 5 x `0 Error(s)`, 3 x `[  PASSED  ] 98 tests.`. The checkpoint holds. ARM64 is cross-compiled on the x64 runner; ARM64 test execution is `arm64-host` work (`D99 T01 §4`), outside this section. Context: builds from `59902fc` to `23183fb` failed on a `vswhere` component requirement added by the D06 T05 §3 audit and fixed in `07f3563`; the run above is after that fix.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T16:56:31Z
- Effort: medium
- Lines: 28
- Digest: 7108d5a485a57aed

### adversarial: needs-attention

1. **P2: Concurrent jobs can permanently populate an incomplete dependency cache.** `.github/workflows/build.yml:93` and `:221` use identical cache keys and paths, but the ASan job builds only x64 (`:227`). If ASan saves first on a cold key, the cache lacks ARM64 packages and the app’s NuGet packages. Subsequent full builds restore that incomplete cache and rebuild missing dependencies without updating it. This violates the repeat-run guarantee at `todo/00-workspace/TODO-01-repo-and-build.md:146`. Reproduce with a fresh key, let ASan finish first, then repeat the full workflow. This is a static finding, not a live reproduction. GitHub documents that existing cache contents cannot be updated. [Cache semantics](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching)

### consistency: needs-attention

2. **P3: The recorded trigger contract no longer matches the workflow.** `todo/00-workspace/TODO-01-repo-and-build.md:131` and `docs/plans/2026-10-13-recto-foundation.md:58` promise builds on every push, while `.github/workflows/build.yml:13` excludes documentation, TODO and Markdown changes. Reproduce with a documentation-only push to main: no build runs. The later optimization is documented elsewhere, but these authoritative promises remain unreconciled.

### integration: needs-attention

Finding **1 (P2)** is an integration regression between the original four-configuration build and the later ASan job. Their shared cache identity assumes equivalent contents despite different build targets.

### source-defect: needs-attention

Finding **1 (P2)** relies on incorrect cache lifecycle assumptions: a successful full build cannot enrich an exact-hit cache with the missing ARM64 archives. Separate cache identities or a coordinated cache producer are required.

### design: advisory

Not applicable; this section owns no user surface.

### record: needs-attention

3. **P3: The checkpoint declaration overstates its evidence.** `docs/reviews/06-quality/audit-d00-t01-s4.md:10` declares “The checkpoint holds” using run `39636918376` for `e7cf65d`, whereas the checkpoint at `todo/00-workspace/TODO-01-repo-and-build.md:153` requires candidate-commit evidence. The requested candidate is `cd0bfee`. The product build inputs inspected are unchanged between those commits, which supports qualified reuse, but that qualification is absent.

   I attempted `gh run list --workflow=build.yml --limit 1`, the recorded run’s job lookup, and its log lookup. All failed because the configured proxy at `127.0.0.1:9` refused connection. Consequently, the claimed warning counts, test counts and live success remain independently unverified.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P2): `.github/workflows/build.yml` gives the four-configuration build and the x64-only ASan job separate cache keys (`build-deps-full-` and `build-deps-asan-`), so the ASan job can no longer save the key the full build restores; both keys now also hash `vcpkg-configuration.json`, so a baseline bump starts a fresh cache instead of exact-hitting a stale one.
- Finding 2 (P3): the foundation plan's architecture row carries a dated correction (CI builds every push that changes code; `todo/`, `docs/` and Markdown-only pushes skip it, with the cost of reversing), and `D00 T01 §4` carries a dated audit note; its stamped checklist keeps its wording.
- Finding 3 (P3): the checkpoint is re-run on the fix commit itself (below), which is the candidate's tip.
Checkpoint on the fix commit: `build` run `39639400776` for `dce42ea` (`gh run watch` exit 0): all three jobs succeeded ("Build x64 & ARM64 and run recto-tests", the ASan job, the test-list note); the log holds 5 x `0 Warning(s)`, 5 x `0 Error(s)`, 3 x `[  PASSED  ] 98 tests.`; and the caches are separate: `Cache saved with key: build-deps-full-Windows-<hash>` and `Cache saved with key: build-deps-asan-Windows-<hash>`, each first missed under the new names.