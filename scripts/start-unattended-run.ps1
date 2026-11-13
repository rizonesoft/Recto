# Starts Recto's unattended plan run in Claude Code or Google Antigravity. The Desktop shortcuts "Recto Claude
# campaign" (-Runner claude, the default) and "Recto Carbon campaign" (-Runner agy, Antigravity on Gemini 4 Carbon)
# run this file. It prints the state of the machine and the tree, holds Windows awake while the session runs, and
# starts an interactive session on the runner's writer model with permission prompts off and the session goal below.
# Setting the goal starts the work: the goal's text is the first directive, beside the run guard of the process-plan
# skill (scripts/campaign_guard.py Stop hook in .claude/settings.json and .agents/hooks.json, and the 5-minute
# heartbeat via CronCreate or schedule).
#
#   pwsh -NoProfile -File scripts\start-unattended-run.ps1            start the run in Claude Code
#   pwsh -NoProfile -File scripts\start-unattended-run.ps1 -Runner agy  start it in Antigravity on Gemini 4 Carbon
#   pwsh -NoProfile -File scripts\start-unattended-run.ps1 -PrintOnly print the checks and the command, start nothing
#   pwsh -NoProfile -File scripts\start-unattended-run.ps1 -InstallShortcut  write both Desktop shortcuts for this clone
#
# A run starts only when the owner starts it, from one of the shortcuts or this file (owner, 2026-11-12): nothing
# here starts a second session after the first one ends.
#
# Antigravity signs in with its own account on this PC and never gets Claude's token.
# Claude Code uses the one-year token kept in 1Password when `op` is installed (vault Conclave, item "Claude Code OAuth
# token (1 year, setup-token)", field credential), handed to claude.exe as CLAUDE_CODE_OAUTH_TOKEN in this process's
# environment only: it is never printed, written or passed as an argument. When `op` is absent and
# CLAUDE_CODE_OAUTH_TOKEN is already set in the environment, that token is used; when 1Password cannot be read or
# nobody approves its prompt within $TokenTimeoutSeconds, the launcher says so and starts on the stored login.
# The session cannot ask the operator anything: AskUserQuestion is switched off and PreToolUse blocks ask_question.
#
# It starts whatever the checks show: low memory, a dirty tree and a guard left by an earlier run are printed, not
# refused, because the process-plan audit decides what to do about each (owner decision 2026-11-10: warn and start).
# The Claude Code model is the alias opus, the newest Opus (owner direction 2026-11-10); Antigravity runs Gemini 4
# Carbon at medium, whose model name carries its effort (owner direction 2026-11-12). AGENTS.md, "Who writes and who
# reviews", records both. Changing one costs its line below, its test and that paragraph.
param(
    # The campaign runner: Claude Code (`claude`) or Google Antigravity on Gemini 4 Carbon (`agy`).
    [ValidateSet('claude', 'agy')][string]$Runner = 'claude',
    [switch]$PrintOnly,
    [switch]$InstallShortcut,
    [int]$TokenTimeoutSeconds = 120,
    # Where -InstallShortcut writes the shortcuts and their icons. Only the tests pass other folders.
    [string]$ShortcutFolder = [Environment]::GetFolderPath('Desktop'),
    [string]$IconFolder = (Join-Path $env:LOCALAPPDATA 'Recto')
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$model = 'opus'
$carbon = 'gemini-4-carbon-medium'
$tokenVault = 'Conclave'
$tokenItem = 'Claude Code OAuth token (1 year, setup-token)'

if ($InstallShortcut) {
    # One shortcut for each runner, named and shaped like the Office and Isotone campaign shortcuts (owner,
    # 2026-11-12). The shortcuts hold this clone's path, so they are rewritten here whenever the clone moves. Each
    # carries Recto's icon with its runner's badge in the corner, rendered in pure Python stdlib by
    # scripts/campaign-icon.py into the user's own application data folder with zero external dependencies.
    $plain = Join-Path $root 'resources\ico\recto.ico'
    if (-not (Test-Path -LiteralPath $plain)) { throw "the shortcut's icon is missing: $plain" }
    New-Item -ItemType Directory -Force -Path $IconFolder | Out-Null
    $shortcuts = @(
        @{ Runner = 'claude'; Name = 'Recto Claude campaign'; Icon = 'recto-campaign-claude.ico'
           Description = 'Start the unattended Recto campaign in Claude Code' },
        @{ Runner = 'agy'; Name = 'Recto Carbon campaign'; Icon = 'recto-campaign-carbon.ico'
           Description = 'Start the unattended Recto campaign in Google Antigravity on Gemini 4 Carbon' }
    )
    foreach ($s in $shortcuts) {
        $icon = Join-Path $IconFolder $s.Icon
        python (Join-Path $PSScriptRoot 'campaign-icon.py') badge $plain $s.Runner $icon
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "The badged icon for '$($s.Name)' could not be rendered; the shortcut carries Recto's plain icon."
            $icon = $plain
        }
        $link = Join-Path $ShortcutFolder "$($s.Name).lnk"
        $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
        $shortcut.TargetPath = (Get-Process -Id $PID).Path
        $shortcut.Arguments = "-NoLogo -NoExit -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Runner $($s.Runner)"
        $shortcut.WorkingDirectory = $root
        $shortcut.IconLocation = "$icon,0"
        $shortcut.Description = $s.Description
        $shortcut.WindowStyle = 1  # a normal window
        $shortcut.Save()
        Write-Host "Shortcut written: $link"
    }
    # The one shortcut this file wrote before the runners had one each started Claude Code without saying so.
    $old = Join-Path $ShortcutFolder 'Recto - Process Plan (Unattended).lnk'
    if (Test-Path -LiteralPath $old) {
        Remove-Item -LiteralPath $old
        Write-Host "Removed the earlier shortcut: $old"
    }
    exit 0
}

$runnerName = @{ claude = 'Claude Code'; agy = 'Google Antigravity (Gemini 4 Carbon)' }[$Runner]
try { $Host.UI.RawUI.WindowTitle = @{ claude = 'Recto Claude campaign'; agy = 'Recto Carbon campaign' }[$Runner] } catch {}

# One line, no double quotes: it is passed to claude.exe as a single argument. Claude Code allows a goal of 4,000
# characters.
$goal = @'
/goal Recto's implementation plan (todo/implementation-plan.md in {ROOT}) is 100 percent complete: `python scripts/todo-graph.py query stats` shows verified equal to total, with every section built, reviewed by the pinned reviewer and stamped through the stamp command, no partial ships, and CI green on main. The goal is also met when the run has ended the way the process-plan skill allows while rows are still open: every remaining row is blocked here (an unmet dependency, a waiting Need, or a host this session cannot reach), the run file carries its PARKED line and park record, the run was ended through `scripts/campaign_guard.py end`, and the last message lists each leftover row with what blocks it. Nothing else counts as done: not an audit, not a status report, not a finished phase while another phase has a ready row. The work: process the plan through the process-plan skill, starting now, unattended. Follow AGENTS.md, design/SPEC.md and the skills exactly. Never ask the operator a question: take the industry-standard option, record it with its cost of reversing, and carry on. Self-improve and self-heal: repair any broken gate, hook, script or drifted plan claim at its owning layer with a regression test, and shape every feature to beat Adobe Acrobat, Foxit, PDFelement, UPDF, Microsoft, Apple and Google in speed, step count, smart defaults, error recovery and accessibility, proven with measurements. No shortcuts, no workarounds, no weakened tests, and no avoiding work because it is hard. Balance speed, memory and excellence: use LSP diagnostics and symbol navigation first, run fast targeted local checks while iterating (`.\build.cmd -Quick` and `python scripts/run-tests.py`), push candidate commits so GitHub Actions CI runs the heavy full matrix in parallel with reviews (`python scripts/ci_status.py --wait`), run one heavy local job at a time, wait while less than 3,000 MB is free, and in Antigravity delegate read-only mechanical lookups and test runs to `gemini-4-hydrogen-medium` helpers while keeping all writing, design, diagnosis and verdicts in Gemini 4 Carbon.
'@
$goal = $goal.Trim().Replace('{ROOT}', $root)
if ($goal.Contains('"') -or $goal.Contains("`n")) { throw 'the goal must be one line with no double quote' }
if ($goal.Length -gt 4000) { throw "the goal is $($goal.Length) characters; Claude Code allows 4,000" }

$program = (Get-Command $Runner -ErrorAction SilentlyContinue).Source
if (-not $program) { throw "$Runner is not on PATH: install $runnerName, or open a new terminal after installing it" }

Write-Host "Recto unattended run in ${runnerName}: $root"
$osInfo = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
$freeMb = if ($osInfo) { [int]($osInfo.FreePhysicalMemory / 1024) } else { [int](Get-Counter '\Memory\Available MBytes').CounterSamples[0].CookedValue }
Write-Host ("Free memory: {0:N0} MB" -f $freeMb)
if ($freeMb -lt 3000) {
    Write-Warning 'Under 3,000 MB free: heavy builds and reviews should wait until memory clears. Largest processes:'
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
$hookFile = @{ claude = '.claude/settings.json'; agy = '.agents/hooks.json' }[$Runner]
$guardOut = python (Join-Path $PSScriptRoot 'campaign_guard.py') --self-test --hook-file $hookFile 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Warning "Stop hook self-test failed ($hookFile): $guardOut"
}

# The token goes from 1Password into claude.exe's environment and nowhere else: it is set just before claude.exe starts
# and removed when it ends, so the shell the shortcut leaves open does not keep it. 1Password may ask the operator to
# approve the read; with nobody there the read is stopped after $TokenTimeoutSeconds and the run starts regardless.
$token = $null
$usedEnvToken = $false
# Antigravity signs in with its own account on this PC: 1Password is not read and Claude's token never reaches it.
$op = if ($Runner -eq 'claude') { (Get-Command op -ErrorAction SilentlyContinue).Source } else { $null }
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
} elseif ($Runner -eq 'claude' -and $env:CLAUDE_CODE_OAUTH_TOKEN) {
    $token = $env:CLAUDE_CODE_OAUTH_TOKEN
    $usedEnvToken = $true
}
if ($Runner -ne 'claude') {
    Write-Host "Sign-in: $runnerName's own account on this PC."
} elseif ($usedEnvToken) {
    Write-Host 'Sign-in: CLAUDE_CODE_OAUTH_TOKEN from environment.'
} elseif ($token) {
    Write-Host 'Sign-in: the one-year token from 1Password.'
} else {
    Write-Warning "Sign-in: 1Password did not give the token ($tokenItem, vault $tokenVault); starting on the stored Claude Code login."
}

if ($Runner -eq 'claude') {
    Write-Host "Model: $model, permission prompts off, questions off, goal of $($goal.Length) characters."
    # --disallowedTools takes a list, so an option follows it and the goal stays the last, positional argument.
    $runArgs = @('--dangerously-skip-permissions', '--disallowedTools', 'AskUserQuestion', '--model', $model, $goal)
} else {
    Write-Host "Model: $carbon, permission prompts off, goal of $($goal.Length) characters."
    # -i runs the opening prompt and keeps the conversation open, as Claude Code does with its prompt argument.
    $runArgs = @('--model', $carbon, '--dangerously-skip-permissions', '-i', $goal)
}
if ($PrintOnly) {
    Write-Host "Would run: $program $(($runArgs | Select-Object -SkipLast 1) -join ' ') <goal>"
    Write-Host $goal
    exit 0
}

# Hold Windows awake (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) while the unattended session runs so overnight runs never
# stall in Modern Standby or S3 sleep, and release it when the session exits.
$powerAwake = $false
if ($IsWindows) {
    try {
        if (-not ('Recto.PowerGuard' -as [type])) {
            Add-Type -Namespace Recto -Name PowerGuard -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint esFlags);'
        }
        [void][Recto.PowerGuard]::SetThreadExecutionState(0x80000001u)
        $powerAwake = $true
    } catch {}
}

$before = $env:CLAUDE_CODE_OAUTH_TOKEN
$beforeUnattended = $env:CONCLAVE_UNATTENDED_RUN
try {
    $env:CONCLAVE_UNATTENDED_RUN = '1'
    # A token this shell inherited is dropped for Antigravity, and replaced by the 1Password one for Claude Code.
    if ($Runner -ne 'claude') { Remove-Item Env:CLAUDE_CODE_OAUTH_TOKEN -ErrorAction SilentlyContinue }
    elseif ($token) { $env:CLAUDE_CODE_OAUTH_TOKEN = $token }
    else { Remove-Item Env:CLAUDE_CODE_OAUTH_TOKEN -ErrorAction SilentlyContinue }
    $token = $null
    & $program @runArgs
    $code = $LASTEXITCODE
} finally {
    if ($powerAwake) {
        try { [void][Recto.PowerGuard]::SetThreadExecutionState(0x80000000u) } catch {}
    }
    $env:CLAUDE_CODE_OAUTH_TOKEN = $before
    $env:CONCLAVE_UNATTENDED_RUN = $beforeUnattended
}
exit $code
