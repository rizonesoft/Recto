# Audit - D00 T01 §3: Solution scaffold for all modules, x64 and ARM64

- **Audit section:** `D06 T05 §3` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T01 §3`, stamped 2026-10-14 at `5e3d9a6` (`docs/reviews/d00-t01-s3.json`, 1 round under the earlier process)
- **Date:** 2026-10-21

## Checkpoint re-run

`.\build.cmd` at `e49114f` exited 0: x64 and ARM64, Debug and Release, each `0 Warning(s)` and `0 Error(s)`; `recto-tests.exe` ran 98 tests in Debug and Release, all passed. The three tests the section names pass: `Scaffold.PdfEngineReportsAbiVersionOne`, `Scaffold.CoreReportsThePdfEngineAbi`, `Scaffold.ServicesReportTheCoreAbi` (`[  PASSED  ] 3 tests`). The checkpoint holds; the suite has grown from 3 to 98 tests as later sections added theirs.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T14:55:27Z
- Effort: medium
- Lines: 27
- Digest: 6e8d1ddd96268124

## adversarial: needs-attention

1. **P2: Toolchain discovery can select an unusable installation despite a valid one being installed.** `build/build.ps1:41` selects the latest VS 2026 instance without requiring C++, ARM64 or WinUI components. Reproduce with two installations: an older instance containing all documented prerequisites and a newer instance lacking C++ tools. `build.cmd` selects the latter and fails instead of using the valid toolchain. This is a static finding; that installation arrangement was not reproduced here. Microsoft provides component filtering through `vswhere -requires`. [vswhere documentation](https://github.com/microsoft/vswhere)

## consistency: needs-attention

2. **P3: The live section contract still specifies the superseded Windows App SDK version.** `todo/00-workspace/TODO-01-repo-and-build.md:100` requires `2.4.0`, while `src/app/Recto.vcxproj` and foundation decision D4 require `2.5.1`. Reproduce by running the section resolver and comparing its body with the project’s `Microsoft.WindowsAppSDK` reference. The legitimate upgrade needs a correction in the live contract; the historical receipt can remain unchanged.

## integration: approve

No additional actionable finding identified.

## source-defect: needs-attention

3. **P3: An installation-specific missing component is presented as a Visual Studio product limitation.** `CONTRIBUTING.md:33`, repeated in `build/build.ps1:18` and the section’s correction, claims VS 2022 cannot build the WinUI ARM64 project because it lacks the platform targets. Microsoft documents ARM64 and WinUI 3 support in VS 2022. Reproduce the documentation discrepancy by comparing that statement with Microsoft’s release announcement. Keep the intentional `v145` requirement, but distinguish it from the observed incomplete VS 2022 installation. [Microsoft’s VS 2022 ARM64 announcement](https://devblogs.microsoft.com/visualstudio/visual-studio-2022-17-4/)

## design: advisory

Not applicable: the section declares no user surface.

## record: advisory

The checkpoint claim at `docs/reviews/06-quality/audit-d00-t01-s3.md:10` is only partially independently verifiable today. The existing Release binary passes all three named scaffold tests, but `build/msbuild-Release-x64.log:26` now records the five-project ASan target selection, excluding `Recto` and `recto-fuzz`. It cannot substantiate the claimed seven-project Release x64 rerun.

The relevant source files are unchanged between `e49114f` and `dcd83bc`. However, I could not rerun the complete checkpoint or the deliberate compiler-failure probes under this session’s read-only restriction. This is an evidence limitation, not proof that the recorded build failed. No files were modified.

OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P2): `build/build.ps1` asks `vswhere` for the newest Visual Studio 2026 that has `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`, `VC.Tools.ARM64`, `Windows11SDK.26100` and `WindowsAppSdkSupport.Cpp` (IDs checked against this machine's installation, which they select), and its refusal names the missing components. `.\build.cmd -Platform x64 -Configuration Release -SkipTests` -> `0 Warning(s)`, `0 Error(s)`, exit 0.
- Finding 2 (P3): a dated audit note in `D00 T01 §3` records Windows App SDK 2.5.1 (D4); the stamped checklist keeps its wording.
- Finding 3 (P3): `build/build.ps1` and `CONTRIBUTING.md` now give the real reason VS 2022 cannot substitute (the v145 toolset ships only with VS 2026); the audit note says the missing ARM64 platform came from an incomplete installation.
- Record: the checkpoint evidence is the full `build.cmd` at `e49114f` (log in the session scratchpad); `build/msbuild-Release-x64.log` was later overwritten by the ASan build, as the reviewer saw.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:01:59Z
- Effort: medium
- Lines: 27
- Digest: 98c399f4c2a200ff

## adversarial: approve

No actionable defect identified within this section’s scope.

## consistency: approve

No additional finding.

## integration: approve

No additional finding.

## source-defect: approve

No additional finding after checking [Microsoft’s toolchain discovery documentation](https://github.com/microsoft/vswhere/wiki/Find-VC).

## design: advisory

Not applicable: this section declares no user surface.

## record: needs-attention

1. **P3: The checkpoint’s claim that build files remain unchanged is false.** `docs/reviews/06-quality/audit-d00-t01-s3.md:9` says build files are unchanged since `e49114f`, but `build/build.ps1:42` now contains the component requirements added after round 1. Reproduce with `git diff e49114f 59902fc -- build/build.ps1`. Update the checkpoint’s applicability statement to distinguish the earlier full build from the subsequent discovery change and its narrower verification.

Verification limitation: existing logs record four successful configurations with zero warnings; existing XML reports record 98 passing tests per x64 configuration. I independently ran the three scaffold tests in both existing x64 binaries, successfully. These do not establish a fresh build from `59902fc`; the read-only restriction prevented rerunning the full build and deliberate compiler-failure probes. No files were modified.

OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P3): the round 1 change to `build/build.ps1` is covered by a full re-run at `59902fc`: `.\build.cmd` exited 0, x64 and ARM64 in Debug and Release each `0 Warning(s)` and `0 Error(s)`, `[  PASSED  ] 98 tests` in Debug and Release, and `Scaffold.*` passes in the fresh Release binary. The opening record no longer claims the build files were unchanged.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T15:06:27Z
- Effort: medium
- Lines: 29
- Digest: e333f35cc736978d

## adversarial: approve

No actionable defect identified within this section’s scope at `13f99e6`.

## consistency: approve

No additional finding.

## integration: approve

No additional finding.

## source-defect: approve

No additional finding after checking Microsoft’s [toolchain discovery documentation](https://github.com/microsoft/vswhere/wiki/Find-VC).

## design: advisory

Not applicable: this section declares no user surface.

## record: advisory

The round 2 finding is resolved. Between `59902fc` and `13f99e6`, only the audit Markdown changed.

Existing logs cover all seven projects in all four configurations with zero warnings and errors. Existing XML reports record 98 passing tests per x64 configuration. I independently ran `Scaffold.*` in both existing x64 binaries: three tests passed in each.

Verification limitation: these establish consistency with the recorded rerun, not independent proof of a clean-clone build. The no-write constraint prevented rerunning `build.cmd` and the deliberate compiler-failure probes. No files were modified.

OVERALL: approve

## Outcome

- The stamp of `D00 T01 §3` is confirmed: a full `build.cmd` at `59902fc` builds all modules for x64 and ARM64 in Debug and Release with 0 warnings and runs 98 passing tests, including the three scaffold tests. The audit made toolchain discovery require the components the build needs and corrected the VS 2022 reason; a dated audit note records Windows App SDK 2.5.1. No downstream stamp is suspect.