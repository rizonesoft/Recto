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
.EXAMPLE
  pwsh .conclave/automations/install.ps1
  pwsh .conclave/automations/install.ps1 -Uninstall
#>
[CmdletBinding()]
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here '..\..')).Path
$pythonw = (Get-Command pythonw.exe -ErrorAction Ignore).Source  # absent is the throw below
if (-not $pythonw) { throw 'pythonw.exe is not on PATH; install Python 3 for Windows first' }
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
