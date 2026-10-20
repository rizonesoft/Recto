# Recto automations

Every scheduled job that must run on this machine lives here: the task list in `tasks.json`, the runner, the failure alert, and the installers for Windows Task Scheduler (`install.ps1`) and systemd user timers (`install.sh`). The Conclave-SDK `automations` kit installed this folder; `tasks.json` and this README are Recto's own, and the runner, alert and installers are refreshed from the kit (`forge.py sync --include-scripts`). Recto runs on Windows, so the tasks are Task Scheduler tasks under `\Recto\`.

Repository pins are not kept here. The daily GitHub workflow `.github/workflows/pins.yml` keeps them on their latest stable releases, and Dependabot keeps the GitHub Actions current (foundation plan D4, revised 2026-10-21). Only the checks that need this PC (its Visual Studio, its Python, its Conclave SDK checkout) run from this folder.

## What runs here

| Task | When (local time) | Runs | Emails you when | Fails when |
| --- | --- | --- | --- | --- |
| `recto-python-pin` | Monday 07:00 | `scripts/check-python-pin.py notify --scope local` | this machine's Python is behind the newest stable CPython (once per finding) | python.org cannot be read |
| `recto-visual-studio` | Monday 07:05 | `scripts/check-visual-studio.py notify` | this machine's Visual Studio is behind its latest servicing release (once per release) | the toolset's Visual Studio is not installed, or vswhere or the release channel cannot be read |
| `recto-conclave-kits` | Monday 07:10 | `scripts/check-conclave-kits.py notify` | Recto's kit files drift from the Conclave SDK's committed kits, or its doctor reports a gap (once per state) | the SDK checkout is missing (set `CONCLAVE_SDK_ROOT`), or an allow-list entry has no reason |

A weekly task starts when available: a run missed while this PC was off fires at the next chance, with a five-minute random delay. A start while the task still runs is ignored, and a run is stopped five minutes past its timeout. Tasks run only while the operator is logged on, so no password is stored.

## What runs on GitHub

| Check | Daily at 05:23 UTC | Becomes |
| --- | --- | --- |
| WinUI 3 (`Microsoft.WindowsAppSDK`) | `scripts/check-winui-pin.py pr` | a pull request on `winui/<version>` for each new stable `microsoft-ui-xaml` release |
| C++/WinRT and Windows SDK build tools | `scripts/check-sdk-pins.py pr` | one pull request on `pins/toolchain-...` for the newer releases |
| vcpkg baseline (test-only) | `scripts/check-vcpkg-baseline.py pr` | a pull request on `pins/vcpkg-<tag>` for each new vcpkg release |
| Windows SDK line | `scripts/check-windows-sdk-line.py issue` | one issue per newer line (moving the target platform is your decision); closed automatically when Recto targets the newest line |
| CI Python | `scripts/check-python-pin.py issue --scope ci` | one issue when `python-version` in a workflow is behind; closed when fixed |
| GitHub Actions | Dependabot (`.github/dependabot.yml`), daily | a Dependabot pull request per action |

Every pull request from the pins workflow has `build.yml` (x64 and ARM64, Debug and Release, then the tests) and `plan.yml` dispatched on its branch, so its checks show on the PR before you merge. A version that already has a pull request, open, merged or closed, is never proposed again: closing a PR declines that version. Nothing merges by itself. The WinUI 3 rule (owner, 2026-10-20) is unchanged: the pin is the newest `winui3/release/<version>` release of `microsoft/microsoft-ui-xaml` that is neither a prerelease nor experimental, and NuGet must publish it.

The workflow needs the repository setting "Allow GitHub Actions to create and approve pull requests" (enabled 2026-10-21, with the default token permission left read-only; recorded in `D99 T01 §2`).

## How you hear about it

- **A pin moved:** a GitHub pull request labelled `dependencies` and `pins`, from the pins workflow or Dependabot. GitHub notifies you as for any PR (email and the GitHub app, per your notification settings). Merge it when its checks are green.
- **Something needs your decision:** a GitHub issue labelled `pins` and `pins:<check>`. It closes itself once the finding is gone, and closing it yourself dismisses that finding.
- **Something on this PC is behind:** an email from "Recto automations" (`website@rizonetech.com`, through SMTP2GO) to `rizonetech@gmail.com`, titled "Recto: ... is available" or "has drifted", sent once per new version or state.
- **A check broke:** on GitHub, a failed `pins` run, which GitHub emails to whoever last edited the schedule; on this PC, an email titled "Automation failed: <task> on <host>" with the log tail and how to run it again, repeated on every failing run until fixed.

No PR, issue or email means everything is current. `gh pr list --label pins`, `gh issue list --label pins` and the `Get-ScheduledTask` line below show the state at any time.

## When a run fails

Task Scheduler starts `pythonw.exe .conclave/automations/run.py <name>`. The runner writes the run's output to `build/automations/<name>/<UTC>.log` (the newest 20 kept) and its outcome to `build/automations/<name>/last.json`. On a non-zero exit or a timeout it calls `alert.py`, which emails `rizonetech@gmail.com` the task, its exit, the tail of that run's log, and how to run it again.

- It sends through SMTP2GO from `website@rizonetech.com`, the verified Rizonetech sender, as "Recto automations".
- The key is read at send time from the Conclave vault item "Rizonetech SMTP2GO (website mail)" through `conclave-op` (in WSL when the run is on Windows). Nothing is stored here, and there is no `.env`.
- SMTP2GO answers a refused send with HTTP 200 and `succeeded: 0`; the alert treats that as a failure.
- The runner exits with the command's code (124 on a timeout, 126 when it cannot start), or 3 when the alert could not be sent, with the reason appended to the run's log, so Task Scheduler's Last Run Result shows every failure.

A notice email ("is available") is recorded as the git ref `refs/recto-notice/<key>` after it is sent, so it goes out once; `git for-each-ref refs/recto-notice` lists them, and deleting one sends that notice again.

## Install and inspect

```powershell
pwsh .conclave/automations/install.ps1              # idempotent; re-run after editing tasks.json
pwsh .conclave/automations/install.ps1 -Uninstall   # remove every \Recto\ task
Get-ScheduledTask -TaskPath '\Recto\' | Get-ScheduledTaskInfo | Format-Table TaskName, NextRunTime, LastRunTime, LastTaskResult
Start-ScheduledTask -TaskPath '\Recto\' -TaskName <name>   # run one now
python .conclave/automations/run.py <name>                    # or run it in this console
python .conclave/automations/alert.py <name> --dry-run        # preview the alert for its last run
python -m unittest discover -s scripts/tests -p "test_automations.py"
python -m unittest tests.scripts.test_pin_automations
```

The installer reads `tasks.json` and registers every task it lists; a task under `\Recto\` that the file no longer lists is removed.

Installed 2026-10-21 on this PC: `recto-python-pin`, `recto-visual-studio` and `recto-conclave-kits`, next run Monday 2026-10-26. A first run of `recto-visual-studio` exited 0 (Visual Studio 2026 already at 18.11.1, nothing sent). A first run of `recto-python-pin` exited 0 and sent the notice "Recto: a newer Python is available" from "Recto automations <website@rizonetech.com>" to `rizonetech@gmail.com`, received 2026-10-20 23:10:08 UTC (Message-Id `<E1xJIy8-FnQW0hPwWQi-OrwO@message-id.smtpcorp.com>`, SPF, DKIM and DMARC pass).

## Adding an automation

A check of a repository pin belongs in `.github/workflows/pins.yml` (a `pr` or `issue` mode through `scripts/lib/pin_proposal.py`). Only a check that needs this machine belongs here:

1. Add an entry to `tasks.json`: a `recto-` name, a one-line description, the command (a script in this repository, `python` first), a trigger (`{"weekly": "<Day>", "at": "HH:MM"}` or `{"at_logon_delay_minutes": N, "every_minutes": N}`), and `timeout_minutes`.
2. Add its row to the table above; `scripts/tests/test_automations.py` fails until the table and the file agree.
3. Run the tests, then the installer.
