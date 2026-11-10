# Starts Recto's unattended plan run in Claude Code (the Desktop shortcut "Recto - Process Plan (Unattended)" runs
# this file). It prints the state of the machine and the tree, then starts an interactive Claude Code session on the
# writer model with permission prompts off and the session goal below. Setting the goal starts the work: the goal's
# text is the first directive, and Claude Code checks it after every turn, beside the run guard of the process-plan
# skill (the Stop hook and the heartbeat).
#
#   pwsh -NoProfile -File scripts\start-unattended-run.ps1            start the run
#   pwsh -NoProfile -File scripts\start-unattended-run.ps1 -PrintOnly print the checks and the command, start nothing
#   pwsh -NoProfile -File scripts\start-unattended-run.ps1 -InstallShortcut  write the Desktop shortcut for this clone
#
# The session signs in with the one-year Claude Code token kept in 1Password (vault Conclave, item "Claude Code OAuth
# token (1 year, setup-token)", field credential), handed to claude.exe as CLAUDE_CODE_OAUTH_TOKEN in this process's
# environment only: it is never printed, written or passed as an argument. A run that outlives the stored login's
# refresh therefore keeps going. When 1Password cannot be read, or nobody approves its prompt within two minutes, the launcher
# says so and starts on the stored login.
# The session cannot ask the operator anything: the AskUserQuestion tool is switched off.
#
# It starts whatever the checks show: low memory, a dirty tree and a guard left by an earlier run are printed, not
# refused, because the process-plan audit decides what to do about each (owner decision 2026-11-10: warn and start).
# The model is the alias opus, the newest Opus (owner direction 2026-11-10), taking over from Fable 5.2; AGENTS.md, "Who writes and
# who reviews", records it. Changing it costs the $model line below, its test and that paragraph.
param(
    [switch]$PrintOnly,
    [switch]$InstallShortcut,
    [int]$TokenTimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$model = 'opus'
$shortcutName = 'Recto - Process Plan (Unattended).lnk'
$tokenVault = 'Conclave'
$tokenItem = 'Claude Code OAuth token (1 year, setup-token)'

if ($InstallShortcut) {
    # The shortcut holds this clone's path, so it is rewritten here whenever the clone moves.
    $icon = Join-Path $root 'resources\ico\recto.ico'
    if (-not (Test-Path $icon)) { throw "the shortcut's icon is missing: $icon" }
    $pwsh = (Get-Command pwsh).Source
    $link = Join-Path ([Environment]::GetFolderPath('Desktop')) $shortcutName
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
    $shortcut.TargetPath = $pwsh
    $shortcut.Arguments = "-NoExit -NoProfile -File `"$PSCommandPath`""
    $shortcut.WorkingDirectory = $root
    $shortcut.IconLocation = "$icon,0"
    $shortcut.Description = 'Start the unattended Recto plan run in Claude Code'
    $shortcut.Save()
    Write-Host "Shortcut written: $link"
    exit 0
}

# One line, no double quotes: it is passed to claude.exe as a single argument. Claude Code allows a goal of 4,000
# characters.
$goal = @'
/goal Recto's implementation plan (todo/implementation-plan.md in {ROOT}) is 100 percent complete: `python scripts/todo-graph.py query stats` shows verified equal to total, with every section built, reviewed by the pinned reviewer and stamped through the stamp command, no partial ships, and CI green on main. The goal is also met when the run has ended the way the process-plan skill allows while rows are still open: every remaining row is blocked here (an unmet dependency, a waiting Need, or a host this session cannot reach), the run file carries its PARKED line and park record, the run was ended through `scripts/campaign_guard.py end`, and the last message lists each leftover row with what blocks it. Nothing else counts as done: not an audit, not a status report, not a finished phase while another phase has a ready row. The work: process the plan through the process-plan skill, starting now, unattended. Follow AGENTS.md, design/SPEC.md and the skills exactly. Never ask the operator a question: take the industry-standard option, record it with its cost of reversing, and carry on. Aim for excellence in every section. Recto must end as the best PDF software in its class, a rival to Adobe Acrobat, Foxit, PDFelement and UPDF that users choose over them: before building a feature, look at how the best competitor does it and make Recto faster, with fewer steps, smarter defaults, better recovery from errors and better accessibility, proven with measurements. No shortcuts, no workarounds, no weakened tests, and no avoiding work because it is hard. This machine is short of memory and shared with other work: run one heavy job at a time, run full builds and full test suites through `bosun run --`, and wait while less than 3,000 MB is free. The operator authorises restarting, alone and once 3,000 MB is free, any review or job that was stopped for low memory, including the stopped review of D01 T02 §10.
'@
$goal = $goal.Trim().Replace('{ROOT}', $root)
if ($goal.Contains('"') -or $goal.Contains("`n")) { throw 'the goal must be one line with no double quote' }
if ($goal.Length -gt 4000) { throw "the goal is $($goal.Length) characters; Claude Code allows 4,000" }

$claude = (Get-Command claude -ErrorAction SilentlyContinue).Source
if (-not $claude) { throw 'claude is not on PATH: install Claude Code, or open a new terminal after installing it' }

Write-Host "Recto unattended run: $root"
$freeMb = [int](Get-Counter '\Memory\Available MBytes').CounterSamples[0].CookedValue
Write-Host ("Free memory: {0:N0} MB" -f $freeMb)
if ($freeMb -lt 3000) {
    Write-Warning 'Under 3,000 MB free: builds and reviews will wait, and Claude Code may stop them. Largest processes:'
} else {
    Write-Host 'Largest processes:'
}
Get-Process | Sort-Object PrivateMemorySize64 -Descending | Select-Object -First 5 | ForEach-Object {
    Write-Host ("  {0,-28} {1,7:N0} MB" -f $_.Name, ($_.PrivateMemorySize64 / 1MB))
}
Write-Host ("Branch: " + (git status -sb | Select-Object -First 1))
$dirty = @(git status --short)
if ($dirty.Count -gt 0) { Write-Warning "$($dirty.Count) uncommitted file(s) in the tree; the run's audit reads them before it writes." }
if (Test-Path (Join-Path $root 'build\claude-campaign-guard.json')) {
    Write-Warning 'build\claude-campaign-guard.json exists: an earlier run did not end, or another session is running. The run reconciles it.'
}
# The token goes from 1Password into claude.exe's environment and nowhere else: it is set just before claude.exe starts
# and removed when it ends, so the shell the shortcut leaves open does not keep it. 1Password may ask the operator to
# approve the read; with nobody there the read is stopped after $TokenTimeoutSeconds and the run starts regardless.
$token = $null
$op = (Get-Command op -ErrorAction SilentlyContinue).Source
if ($op) {
    $info = [System.Diagnostics.ProcessStartInfo]::new($op)
    foreach ($a in @('item', 'get', $tokenItem, '--vault', $tokenVault, '--fields', 'label=credential', '--reveal')) {
        $info.ArgumentList.Add($a)
    }
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $read = [System.Diagnostics.Process]::Start($info)
    $out = $read.StandardOutput.ReadToEndAsync()
    $null = $read.StandardError.ReadToEndAsync()
    if ($read.WaitForExit($TokenTimeoutSeconds * 1000)) {
        if ($read.ExitCode -eq 0) { $token = $out.Result.Trim() }
    } else {
        $read.Kill($true)
        Write-Warning "1Password did not answer within $TokenTimeoutSeconds seconds; its read was stopped."
    }
    $read.Dispose()
}
if ($token) {
    Write-Host 'Sign-in: the one-year token from 1Password.'
} else {
    Write-Warning "Sign-in: 1Password did not give the token ($tokenItem, vault $tokenVault); starting on the stored Claude Code login."
}
Write-Host "Model: $model, permission prompts off, questions off, goal of $($goal.Length) characters."

# --disallowedTools takes a list, so an option follows it and the goal stays the last, positional argument.
$claudeArgs = @('--dangerously-skip-permissions', '--disallowedTools', 'AskUserQuestion', '--model', $model, $goal)
if ($PrintOnly) {
    Write-Host "Would run: $claude --dangerously-skip-permissions --disallowedTools AskUserQuestion --model $model <goal>"
    Write-Host $goal
    exit 0
}
$before = $env:CLAUDE_CODE_OAUTH_TOKEN
try {
    if ($token) { $env:CLAUDE_CODE_OAUTH_TOKEN = $token }
    $token = $null
    & $claude @claudeArgs
    $code = $LASTEXITCODE
} finally {
    $env:CLAUDE_CODE_OAUTH_TOKEN = $before
}
exit $code
