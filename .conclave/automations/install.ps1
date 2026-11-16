#Requires -Version 7.0
<#
.SYNOPSIS
  Registers this project's automations in Windows Task Scheduler, under the folder tasks.json names.
.DESCRIPTION
  Reads every task from tasks.json (the one list; nothing here names a task) and registers it to run
  `pythonw.exe .conclave\automations\run.py <name>` from this repository, as the signed-in user, only while
  that user is logged on (no stored password). Idempotent: re-run it after editing tasks.json. A task in the
  project's folder that tasks.json no longer lists is unregistered. On Linux and WSL, install.sh is the
  systemd equivalent.

  Settings on every task: start when available (a run missed while the PC was off fires at the next chance,
  as systemd's Persistent=true does), ignore a new start while one runs, an execution limit five minutes past
  the task's timeout, and run on battery. Weekly tasks carry a five-minute random delay.

  -Check says whether every task is registered for this clone and able to start (run.py --check-installed: exit 0
  all well, 1 with one line per task to repair, 3 when Task Scheduler cannot be read), changing nothing. -Dump is
  the Task Scheduler reader it uses: the project folder's tasks as JSON. Neither runs the tool bootstrap.
.EXAMPLE
  pwsh .conclave/automations/install.ps1
  pwsh .conclave/automations/install.ps1 -Check
  pwsh .conclave/automations/install.ps1 -Uninstall
#>
[CmdletBinding()]
param([switch]$Uninstall, [switch]$Check, [switch]$Dump)
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..')).Path

if ($Dump) {
  # What Task Scheduler holds for this project's folder, as run.py's records; no bootstrap, no change.
  $folderName = (Get-Content -Raw -Path (Join-Path $here 'tasks.json') | ConvertFrom-Json).folder
  if (-not $folderName) { '[]'; return }
  $records = @(Get-ScheduledTask | Where-Object TaskPath -eq $folderName | ForEach-Object {
    $action = @($_.Actions)[0]
    $info = Get-ScheduledTaskInfo -TaskPath $_.TaskPath -TaskName $_.TaskName
    $arguments = [string]$action.Arguments
    $script = if ($arguments -match '^\s*"([^"]+)"') { $Matches[1] } elseif ($arguments -match '^\s*(\S+)') { $Matches[1] } else { '' }
    [ordered]@{
      name = $_.TaskName; script = $script; workdir = [string]$action.WorkingDirectory
      executable = [string]$action.Execute; enabled = ([string]$_.State -ne 'Disabled')
      last_result = [long]$info.LastTaskResult
    }
  })
  ConvertTo-Json -InputObject $records -Depth 3 -Compress
  return
}

if ($Check) {
  # The comparison lives in run.py, which reads Task Scheduler back through -Dump.
  $toolsCheck = if ($env:RECTO_TOOLS_DIR -and $env:RECTO_TOOLS_DIR.Trim()) { $env:RECTO_TOOLS_DIR.Trim() } else { Join-Path $repo '.tools' }
  $python = Join-Path $toolsCheck 'cpython\current\python.exe'
  if (-not (Test-Path -LiteralPath $python)) { $python = (Get-Command python.exe -ErrorAction Ignore).Source }
  if (-not $python) { 'unknown: no python to compare the registrations with'; 'next: pwsh build/bootstrap-tools.ps1'; exit 3 }
  & $python (Join-Path $here 'run.py') --check-installed
  exit $LASTEXITCODE
}
$bootstrap = Join-Path $repo 'build\bootstrap-tools.ps1'
$toolsDir = if ($env:RECTO_TOOLS_DIR -and $env:RECTO_TOOLS_DIR.Trim()) {
  [System.IO.Path]::GetFullPath($env:RECTO_TOOLS_DIR.Trim())
} else {
  Join-Path $repo '.tools'
}
$pythonw = $null
if (Test-Path -LiteralPath $bootstrap) {
  & $bootstrap -ToolsDir $toolsDir
  if ($LASTEXITCODE -ne 0) { throw "bootstrap-tools.ps1 failed (exit $LASTEXITCODE)" }
  $candidateW = Join-Path $toolsDir 'cpython\current\pythonw.exe'
  if (-not (Test-Path -LiteralPath $candidateW)) {
    throw "Pinned pythonw.exe was not found at $candidateW after bootstrap"
  }
  $pythonw = $candidateW
} elseif ($env:RECTO_TOOLS_DIR -and (Test-Path -LiteralPath (Join-Path $toolsDir 'cpython\current\pythonw.exe'))) {
  $pythonw = Join-Path $toolsDir 'cpython\current\pythonw.exe'  # a named tools folder, as run.py expects
} else {
  $pythonw = (Get-Command pythonw.exe -ErrorAction Ignore).Source  # absent is the throw below
}
if (-not $pythonw) { throw 'pythonw.exe is not available in .tools or on PATH; run build/bootstrap-tools.ps1 or install Python 3 for Windows first' }
$runner = Join-Path $here 'run.py'

# run.py's own loader checks tasks.json, so nothing is registered that the runner would refuse.
$folder = & (Join-Path (Split-Path $pythonw) 'python.exe') $runner --check
if ($LASTEXITCODE -ne 0) { throw 'tasks.json was refused (above); nothing was registered' }
$manifest = Get-Content -Raw -Path (Join-Path $here 'tasks.json') | ConvertFrom-Json
$existing = @(Get-ScheduledTask | Where-Object TaskPath -eq $folder)

if ($Uninstall) {
  foreach ($task in $existing) { Unregister-ScheduledTask -TaskPath $folder -TaskName $task.TaskName -Confirm:$false }
  Write-Host "Removed $($existing.Count) task(s) from $folder"
  return
}

$user = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited

foreach ($task in $manifest.tasks) {
  $action = New-ScheduledTaskAction -Execute $pythonw -Argument "`"$runner`" $($task.name)" -WorkingDirectory $repo
  $trigger = $task.trigger
  if ($null -ne $trigger.every_minutes) {
    # Two triggers: after each logon (with the delay), and on the interval from now on. A logon trigger's
    # repetition starts only at the next logon, so on its own it would never tick in the current one.
    $every = New-TimeSpan -Minutes $trigger.every_minutes
    $logon = New-ScheduledTaskTrigger -AtLogOn -User $user
    $logon.Delay = "PT$($trigger.at_logon_delay_minutes)M"
    $logon.Repetition = (New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval $every).Repetition
    $t = @($logon, (New-ScheduledTaskTrigger -Once -At (Get-Date).Add($every) -RepetitionInterval $every))
  } else {
    $t = New-ScheduledTaskTrigger -Weekly -DaysOfWeek $trigger.weekly -At $trigger.at -RandomDelay (New-TimeSpan -Minutes 5)
  }
  $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew `
      -ExecutionTimeLimit (New-TimeSpan -Minutes ($task.timeout_minutes + 5)) `
      -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
  Register-ScheduledTask -TaskPath $folder -TaskName $task.name -Description $task.description `
      -Action $action -Trigger $t -Settings $settings -Principal $principal -Force | Out-Null
}

$listed = @($manifest.tasks | ForEach-Object { $_.name })
foreach ($task in $existing) {
  if ($listed -notcontains $task.TaskName) {
    Unregister-ScheduledTask -TaskPath $folder -TaskName $task.TaskName -Confirm:$false
    Write-Host "Removed $($task.TaskName): tasks.json no longer lists it"
  }
}
Get-ScheduledTask -TaskPath $folder | Get-ScheduledTaskInfo |
    Select-Object TaskName, NextRunTime, LastRunTime, LastTaskResult | Format-Table -AutoSize
