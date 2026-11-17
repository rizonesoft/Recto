## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-11-17T17:51:17Z
- Effort: medium
- Lenses: adversarial, integration, source-defect, consistency, quality, optimization
- Seconds: 95
- Lines: 14
- Digest: 06b420f2a98fd41f

- **P1: Build registration starts after a potentially long bootstrap.** [build/build.ps1:73](/D:/Recto/build/build.ps1:73) creates the mark only after [build.cmd:13](/D:/Recto/build.cmd:13) finishes its separate bootstrap invocation. That bootstrap can wait 15 minutes for its mutex and download missing tools. During this period, a tracked background `build.cmd` has no mark: Stop blocks the requested wait and heartbeat accounting can count it as stalled. The new test bypasses this path by invoking `build.ps1` directly. **Fix:** register the build before the first bootstrap, preserving bootstrap failure propagation, and test the actual `build.cmd` entry point with a delayed bootstrap.

- **P2: The new integration test does not terminate its process tree.** [tests/scripts/test_campaign_guard.py:818](/D:/Recto/tests/scripts/test_campaign_guard.py:818) uses `subprocess.run(timeout=120)` around PowerShell, whose fake bootstrap launches another Python process at line 800. If that descendant hangs, timeout kills PowerShell but leaves Python running, violating the explicit test teardown rule. Python documents timeout cleanup for the direct child, not its descendants. [Python subprocess documentation](https://docs.python.org/3/library/subprocess.html#subprocess.run) **Fix:** use a Windows job object or explicit process-tree cleanup on timeout and teardown, and verify descendant termination with a bounded fixture.

Static review only; `git diff --check 863ee72^ 863ee72` passed. No builds or test suites were run. The working tree remained clean.

LENS adversarial: findings
LENS integration: findings
LENS source-defect: clean
LENS consistency: findings
LENS quality: findings
LENS optimization: clean
HIGHEST: P1
OVERALL: needs-attention

## Writer note: round 1 fixes

- Fixed: P1 (build.cmd's own bootstrap ran unmarked): real. The mark helper moved to `build/build-mark.ps1`; `build.cmd` passes `-MarkBuild` to its bootstrap, which marks itself before the tools mutex wait and removes the mark in its `finally`; `python.cmd` never passes it, so the Stop hook's own Python calls cannot mark themselves. The D00 T04 §6 contract (build.cmd bootstraps first) is kept. `test_bootstrap_waiting_on_the_tools_mutex_holds_the_run_and_a_killed_one_does_not` holds the real mutex and sees the mark, then kills the tree and sees it disbelieved; it fails with `if ($MarkBuild)` mutated to `if ($false)`, and `test_build_cmd_marks_its_own_bootstrap_and_python_cmd_does_not` fails with the flag removed from `build.cmd`.
- Fixed: P2 (test tree not stopped): real. `BuildMark` runs PowerShell through `scripts/lib/proc_group.py` `spawn_managed_process` (a job object), stopping the whole tree on timeout and teardown.
- Evidence: unittest test_campaign_guard, test_build_ps1, test_cmake_build, test_test_shards, test_compile_commands, test_bootstrap_tools: Ran 140 tests, OK; `run-tests.py --quick`: all 36 suite(s) passed.
