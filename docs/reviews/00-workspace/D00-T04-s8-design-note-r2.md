## Plan review: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-11-16T04:44:49Z
- Candidate: d8e3043e4562f9a3484521ce203da33efc6a5837
- Effort: medium
- Lenses: plan
- Seconds: 82
- Lines: 10
- Digest: bc57755a44e52969

1. **P2: Direct builds reuse stale memory limits.** Section lines 291-292; design note lines 34 and 40-41. Pool sizing and low-memory refusal run at configure time. A later `cmake --build --preset x64-debug` or Visual Studio rebuild can reuse those limits after WSL or other processes have consumed memory, bypassing the promised build-start refusal. Ninja pools contain fixed concurrency limits, not live memory measurements. Add a supported mechanism to refresh or validate the budget before subsequent builds, and test configuring with ample memory followed by a rebuild below the minimum without manually reconfiguring. [CMake pool semantics](https://cmake.org/cmake/help/latest/prop_gbl/JOB_POOLS.html).

2. **P2: Naming CMake in a preset does not select the pinned executable on the command line.** Section line 291; design note lines 19 and 35. `cmake --preset` runs whichever CMake the developer prompt resolves. The preset’s `cmakeExecutable` field is reserved for IDEs and ignored by CMake itself; checking that the pinned executable exists does not establish that it is running. Specify the pinned invocation for direct commands and verify the executable used, including a developer prompt with another CMake earlier on `PATH`. [CMake preset documentation](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html#configure-preset).

3. **P2: The advertised test presets have no execution or acceptance contract.** Section lines 290-291 and 296; design note line 19. The note promises test presets, but neither it nor the section specifies test registration, the working directory, or preservation of the existing runner’s time budgets, serial-test exclusions and process-tree cleanup. The parity proof can pass entirely through `build.ps1` while these presets remain empty or bypass those safeguards. Define how the presets invoke the existing test machinery, require empty discovery to fail, and exercise the presets themselves with a passing suite and an intentional failure. CTest needs explicit test registration; presets alone do not supply it. [CTest documentation](https://cmake.org/cmake/help/latest/manual/ctest.1.html).

LENS plan: findings
HIGHEST: P2
PLAN: gaps
OVERALL: needs-attention

- Implemented: build-start memory gate (#1), pinned CMake refused otherwise with build\cmake.cmd (#2), no test presets, the suite through build.ps1's runner only (#3), in the design note and D00 T04 §8
