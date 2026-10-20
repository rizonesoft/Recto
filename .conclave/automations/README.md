# Recto automations

Every scheduled job for Recto lives here: the task list in `tasks.json`, the runner, the failure alert, and the installers for Windows Task Scheduler (`install.ps1`) and systemd user timers (`install.sh`). The Conclave-SDK `automations` kit installed this folder; `tasks.json` and this README are Recto's own, and the runner, alert and installers are refreshed from the kit (`forge.py sync --include-scripts`). Recto runs on Windows, so the tasks are Task Scheduler tasks under `\Recto\`.

## What runs

| Task | When (local time) | Runs | Emails you when | Fails when |
| --- | --- | --- | --- | --- |
| `recto-winui-pin` | 10 minutes after logon, then every 24 hours | `scripts/check-winui-pin.py update` | a newer stable WinUI 3 release is on branch `winui/<version>`, ready to merge | a proposal's checks fail, or the releases or NuGet cannot be read |
| `recto-toolchain-pins` | Monday 07:00 | `scripts/check-sdk-pins.py notify` | a newer C++/WinRT or build tools release is out (once per version) | a pin is not an exact stable version, or nuget.org cannot be read |
| `recto-windows-sdk-line` | Monday 07:05 | `scripts/check-windows-sdk-line.py notify` | a newer Windows SDK line ships (once per line; moving is your decision) | the `WindowsTargetPlatformVersion` values disagree, or nuget.org cannot be read |
| `recto-actions-pins` | Monday 07:10 | `scripts/check-actions-pins.py update` | GitHub Actions at their latest majors are on branch `pins/actions-<key>` | an action is not pinned by a major tag, a proposal fails, or GitHub cannot be read |
| `recto-vcpkg-baseline` | Monday 07:15 | `scripts/check-vcpkg-baseline.py update` | the latest vcpkg release is on branch `pins/vcpkg-<tag>` after `build.cmd` passed | the build fails (no branch is left), or GitHub cannot be read |
| `recto-python-pin` | Monday 07:20 | `scripts/check-python-pin.py notify` | CI's `python-version` or this machine's Python is behind the newest stable CPython | no `python-version` pin is found, or python.org cannot be read |
| `recto-visual-studio` | Monday 07:25 | `scripts/check-visual-studio.py notify` | this machine's Visual Studio is behind its latest servicing release (once per release) | the toolset's Visual Studio is not installed, or vswhere or the channel cannot be read |
| `recto-conclave-kits` | Monday 07:30 | `scripts/check-conclave-kits.py notify` | kit files drift from the Conclave SDK's committed kits, or its doctor reports a gap | the SDK checkout is missing (`CONCLAVE_SDK_ROOT`) or an allow-list entry has no reason |

A weekly task starts when available: a run missed while this PC was off fires at the next chance, with a five-minute random delay. A logon task fires its delay after logon and then on its interval, so `recto-winui-pin` runs once a day while the operator is logged on (the kit has no fixed-time daily trigger). A start while the task still runs is ignored, and a run is stopped five minutes past its timeout. Tasks run only while the operator is logged on, so no password is stored.

## How you hear about it

Every email goes to `rizonetech@gmail.com` from "Recto automations" (`website@rizonetech.com`, through SMTP2GO), and there are exactly three kinds:

- **"Recto: ... is ready to merge"**: a pin moved on a branch. The WinUI 3, GitHub Actions and vcpkg tasks change the pin only on a new branch from main, after its checks pass, and the email names the branch and the `git merge` command. Nothing changes on main or in your checkout until you merge, and nothing is pushed.
- **"Recto: ... is available" or "has drifted"**: something newer exists that is your call or a machine update (a Windows SDK line, Python, Visual Studio, C++/WinRT or build tools, Conclave kits). The task succeeds and sends this once per new version or state.
- **"Automation failed: <task> on <host>"**: a check could not run or found a broken pin. It repeats on every failing run until fixed, with the log tail and how to run it again.

No email means every pin is current. To see the last result of each task at any time, run the `Get-ScheduledTask` line below or read `build/automations/<name>/last.json`. Each announcement and finished proposal is recorded as a git ref (`refs/recto-notice/<key>`, `refs/recto-pin/<key>`, `refs/winui-pin/<version>`); `git for-each-ref refs/recto-notice refs/recto-pin refs/winui-pin` lists them, and deleting one makes its task announce or propose again.

## The WinUI 3 pin

Owner rule (2026-10-20): the repository always pins the latest stable WinUI 3 release, the newest `winui3/release/<version>` GitHub release of the microsoft-ui-xaml repository that is neither a prerelease nor experimental. The pin is the `Microsoft.WindowsAppSDK` PackageReference version in `src/app/Recto.vcxproj`, and the policy is foundation plan D4. The other two toolchain pins (C++/WinRT and the Windows SDK build tools) are checked weekly by `recto-toolchain-pins` and bumped by hand when it announces a release. Recto has no .NET dependency (D4: no .NET runtime), so no .NET SDK is pinned.

Each day `scripts/check-winui-pin.py update` compares the two. When a newer stable release exists and NuGet publishes it, the script adds a temporary git worktree on a new branch `winui/<version>` from main, rewrites only the pin there, runs the checks (the file parses, NuGet has the version, and `build.cmd` passes: x64 and ARM64, Debug and Release, then the x64 tests, all inside the worktree), commits, removes the worktree and emails `rizonetech@gmail.com` the branch to merge (`git merge winui/<version>`). It never touches the operator's checkout or main, and never pushes. The baseline is the pin on main, never the checkout: a pin on main that is not an exact stable version, is ahead of the newest stable release, or is not on NuGet fails the run. A proposal is finished only when the ref `refs/winui-pin/<version>` exists, written after the checks passed, the commit landed and the email went; a finished release is never proposed or emailed again, even after you delete its branch. A `winui/<version>` branch without that ref is an unfinished attempt (an email that failed, a run cut short), which the next run discards and redoes; a branch it cannot discard, or a worktree it cannot remove, fails every run until repaired. It fails (and alerts) when a check fails, which leaves no branch, or when the releases or NuGet cannot be read.

`python scripts/check-winui-pin.py check` exits 1 while a newer stable release is pending, and `update --dry-run` says what the task would do.

## When a run fails

Task Scheduler starts `pythonw.exe .conclave/automations/run.py <name>`. The runner writes the run's output to `build/automations/<name>/<UTC>.log` (the newest 20 kept) and its outcome to `build/automations/<name>/last.json`. On a non-zero exit or a timeout it calls `alert.py`, which emails `rizonetech@gmail.com` the task, its exit, the tail of that run's log, and how to run it again.

- It sends through SMTP2GO from `website@rizonetech.com`, the verified Rizonetech sender, as "Recto automations".
- The key is read at send time from the Conclave vault item "Rizonetech SMTP2GO (website mail)" through `conclave-op` (in WSL when the run is on Windows). Nothing is stored here, and there is no `.env`.
- SMTP2GO answers a refused send with HTTP 200 and `succeeded: 0`; the alert treats that as a failure.
- The runner exits with the command's code (124 on a timeout, 126 when it cannot start), or 3 when the alert could not be sent, with the reason appended to the run's log, so Task Scheduler's Last Run Result shows every failure.

## Install and inspect

```powershell
pwsh .conclave/automations/install.ps1              # idempotent; re-run after editing tasks.json
pwsh .conclave/automations/install.ps1 -Uninstall   # remove every \Recto\ task
Get-ScheduledTask -TaskPath '\Recto\' | Get-ScheduledTaskInfo | Format-Table TaskName, NextRunTime, LastRunTime, LastTaskResult
Start-ScheduledTask -TaskPath '\Recto\' -TaskName <name>   # run one now
python .conclave/automations/run.py <name>                    # or run it in this console
python .conclave/automations/alert.py <name> --dry-run        # preview the alert for its last run
python -m unittest discover -s scripts/tests -p "test_automations.py"
```

Not installed yet: run the installer above, then record the registration and one real alert here (task, next run, last result, and the alert email's subject, received time and `internetMessageId`).

The installer reads `tasks.json` and registers every task it lists; a task under `\Recto\` that the file no longer lists is removed.

## Adding an automation

1. Add an entry to `tasks.json`: a `recto-` name, a one-line description, the command (a script in this repository, `python` first), a trigger (`{"weekly": "<Day>", "at": "HH:MM"}` or `{"at_logon_delay_minutes": N, "every_minutes": N}`), and `timeout_minutes`.
2. Add its row to the table above; `scripts/tests/test_automations.py` fails until the table and the file agree.
3. Run the tests, then the installer.
