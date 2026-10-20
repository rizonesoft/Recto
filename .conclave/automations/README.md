# Recto automations

Every scheduled job for Recto lives here: the task list in `tasks.json`, the runner, the failure alert, and the Task Scheduler installer. Ported from Office-2028's automations (operator request, 2026-10-20), which were ported from Isotone's and adapted from Rizonetech's; Recto runs on Windows, so the tasks are Windows Task Scheduler tasks under `\Recto\`.

## What runs

| Task | When (local time) | Runs | Fails when |
| --- | --- | --- | --- |
| `recto-winui-pin` | Daily 06:23 | `scripts/check-winui-pin.py update` | a newer stable WinUI 3 release cannot be proposed (a check fails, which leaves no branch) or the releases or NuGet cannot be read; a proposal is emailed and is a success |
| `recto-toolchain-pins` | Monday 07:00 | `scripts/check-sdk-pins.py` | the `Microsoft.Windows.CppWinRT` pin, or the `Microsoft.Windows.SDK.BuildTools` pin within its Windows SDK line, is behind the latest stable NuGet release, or is not an exact stable version; nuget.org unreadable exits 2 and also alerts |

Every task starts when available: a run missed while this PC was off fires at the next chance. A start while the task still runs is ignored, the execution limit is five minutes past the task's timeout, and weekly tasks carry a five-minute random delay. Tasks run only while the operator is logged on, so no password is stored.

## The WinUI 3 pin

Owner rule (2026-10-20): the repository always pins the latest stable WinUI 3 release, the newest `winui3/release/<version>` GitHub release of the microsoft-ui-xaml repository that is neither a prerelease nor experimental. The pin is the `Microsoft.WindowsAppSDK` PackageReference version in `src/app/Recto.vcxproj`, and the policy is foundation plan D4. The other two toolchain pins (C++/WinRT and the Windows SDK build tools) are checked weekly by `recto-toolchain-pins` and bumped by hand when it alerts.

Each morning `scripts/check-winui-pin.py update` compares the two. When a newer stable release exists and NuGet publishes it, the script adds a temporary git worktree on a new branch `winui/<version>` from main, rewrites only the pin there, runs the checks (the file parses, NuGet has the version, and `build.cmd` passes: x64 and ARM64, Debug and Release, then the x64 tests, all inside the worktree), commits, removes the worktree and emails `rizonetech@gmail.com` the branch to merge (`git merge winui/<version>`). It never touches the operator's checkout or main, and never pushes. The baseline is the pin on main, never the checkout: a pin on main that is not an exact stable version, is ahead of the newest stable release, or is not on NuGet fails the run. A proposal is finished only when the ref `refs/winui-pin/<version>` exists, written after the checks passed, the commit landed and the email went; a finished release is never proposed or emailed again, even after you delete its branch. A `winui/<version>` branch without that ref is an unfinished attempt (an email that failed, a run cut short), which the next run discards and redoes; a branch it cannot discard, or a worktree it cannot remove, fails every run until repaired. It fails (and alerts) when a check fails, which leaves no branch, or when the releases or NuGet cannot be read.

`python scripts/check-winui-pin.py check` exits 1 while a newer stable release is pending, and `update --dry-run` says what the task would do.

## When a run fails

Task Scheduler starts `pythonw.exe .conclave/automations/run.py <name>`. The runner writes the run's output to `build/automations/<name>/<UTC>.log` (the newest 20 kept) and its outcome to `build/automations/<name>/last.json`. On a non-zero exit or a timeout it calls `alert.py`, which emails `rizonetech@gmail.com` the task, its exit, and the tail of that run's log.

- It sends through SMTP2GO from `website@rizonetech.com`, the verified sender the Rizonetech automations use (Rizonesoft is a brand of Rizonetech).
- The key is read at send time from the Conclave vault item "Rizonetech SMTP2GO (website mail)" through `conclave-op` in WSL. Nothing is stored here, and there is no `.env`.
- SMTP2GO answers a refused send with HTTP 200 and `succeeded: 0`; the alert treats that as a failure.
- The runner exits with the command's code (124 on a timeout), or 3 when the alert could not be sent, with the reason appended to the run's log, so Task Scheduler's Last Run Result shows every failure.

## Install and inspect

```powershell
pwsh .conclave/automations/install.ps1              # idempotent; re-run after editing tasks.json
pwsh .conclave/automations/install.ps1 -DryRun      # print what would be registered or removed
pwsh .conclave/automations/install.ps1 -Uninstall   # remove every \Recto\ task
Get-ScheduledTask -TaskPath '\Recto\' | Get-ScheduledTaskInfo | Format-Table TaskName, NextRunTime, LastRunTime, LastTaskResult
python .conclave/automations/run.py <name>                    # run one task in this console
python .conclave/automations/alert.py <name> --dry-run        # preview the alert for its last run
python -m unittest discover -s tests/scripts -p "test_automations.py"
```

Not installed yet: run the installer above, then record the registration and one real alert here (task, next run, last result, and the alert email's subject, received time and `internetMessageId`).

The installer reads `tasks.json` and registers every task it lists; a task under `\Recto\` that the file no longer lists is removed.

## Adding an automation

1. Add an entry to `tasks.json`: a `recto-` name, a description, the command (a script in this repository, `python` first), a trigger (`{"daily_at": "HH:MM"}`, `{"weekly": "<Day>", "at": "HH:MM"}` or `{"at_logon_delay_minutes": N, "every_minutes": N}`), and `timeout_minutes`.
2. Add its row to the table above; `tests/scripts/test_automations.py` fails until the table and the file agree.
3. Run the tests, then the installer.
