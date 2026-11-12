# Starts Recto's unattended plan run in Claude Code or Google Antigravity. The Desktop shortcuts "Recto Claude
# campaign" (-Runner claude, the default) and "Recto Carbon campaign" (-Runner agy, Antigravity on Gemini 4 Carbon)
# run this file. It prints the state of the machine and the tree, then starts an interactive session on the runner's
# writer model with permission prompts off and the session goal below. Setting the goal starts the work: the goal's
# text is the first directive, and Claude Code checks it after every turn, beside the run guard of the process-plan
# skill (the Stop hook and the heartbeat). That run guard is Claude Code's: an Antigravity session is held by the
# goal alone.
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
# The session signs in with the one-year Claude Code token kept in 1Password (vault Conclave, item "Claude Code OAuth
# token (1 year, setup-token)", field credential), handed to claude.exe as CLAUDE_CODE_OAUTH_TOKEN in this process's
# environment only: it is never printed, written or passed as an argument. A run that outlives the stored login's
# refresh therefore keeps going. When 1Password cannot be read, or nobody approves its prompt within two minutes, the launcher
# says so and starts on the stored login.
# The session cannot ask the operator anything: the AskUserQuestion tool is switched off.
#
# It starts whatever the checks show: low memory, a dirty tree and a guard left by an earlier run are printed, not
# refused, because the process-plan audit decides what to do about each (owner decision 2026-11-10: warn and start).
# The Claude Code model is the alias opus, the newest Opus (owner direction 2026-11-10), taking over from Fable 5.2;
# Antigravity runs Gemini 4 Carbon at medium, whose model name carries its effort (owner direction 2026-11-12).
# AGENTS.md, "Who writes and who reviews", records both. Changing one costs its line below, its test and that paragraph.
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
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$model = 'opus'
$carbon = 'gemini-4-carbon-medium'
$tokenVault = 'Conclave'
$tokenItem = 'Claude Code OAuth token (1 year, setup-token)'

if ($InstallShortcut) {
    # One shortcut for each runner, named and shaped like the Office and Isotone campaign shortcuts (owner,
    # 2026-11-12). The shortcuts hold this clone's path, so they are rewritten here whenever the clone moves. Each
    # carries Recto's icon with its runner's badge in the corner, rendered for this PC by scripts/campaign-icon.py
    # into the user's own application data folder. A runner's mark is its company's trademark and is not kept in this
    # repository: when marks\<name>.svg exists in that folder it is drawn in the badge, and without it the badge
    # carries the runner's colour alone.
    $plain = Join-Path $root 'resources\ico\recto.ico'
    if (-not (Test-Path -LiteralPath $plain)) { throw "the shortcut's icon is missing: $plain" }
    New-Item -ItemType Directory -Force -Path $IconFolder | Out-Null
    $shortcuts = @(
        @{ Runner = 'claude'; Name = 'Recto Claude campaign'; Icon = 'recto-campaign-claude.ico'; Fill = '#D97757'; Mark = 'claude.svg'
           Description = 'Start the unattended Recto campaign in Claude Code' },
        @{ Runner = 'agy'; Name = 'Recto Carbon campaign'; Icon = 'recto-campaign-carbon.ico'; Fill = '#4C8DF6'; Mark = 'gemini.svg'
           Description = 'Start the unattended Recto campaign in Google Antigravity on Gemini 4 Carbon' }
    )
    foreach ($s in $shortcuts) {
        $icon = Join-Path $IconFolder $s.Icon
        python (Join-Path $PSScriptRoot 'campaign-icon.py') $icon $s.Fill (Join-Path $IconFolder "marks\$($s.Mark)")
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "The badged icon for '$($s.Name)' could not be rendered (python -m pip install pillow resvg_py, then this again); the shortcut carries Recto's plain icon."
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
$Host.UI.RawUI.WindowTitle = @{ claude = 'Recto Claude campaign'; agy = 'Recto Carbon campaign' }[$Runner]

# One line, no double quotes: it is passed to claude.exe as a single argument. Claude Code allows a goal of 4,000
# characters.
$goal = @'
/goal Recto's implementation plan (todo/implementation-plan.md in {ROOT}) is 100 percent complete: `python scripts/todo-graph.py query stats` shows verified equal to total, with every section built, reviewed by the pinned reviewer and stamped through the stamp command, no partial ships, and CI green on main. The goal is also met when the run has ended the way the process-plan skill allows while rows are still open: every remaining row is blocked here (an unmet dependency, a waiting Need, or a host this session cannot reach), the run file carries its PARKED line and park record, the run was ended through `scripts/campaign_guard.py end`, and the last message lists each leftover row with what blocks it. Nothing else counts as done: not an audit, not a status report, not a finished phase while another phase has a ready row. The work: process the plan through the process-plan skill, starting now, unattended. Follow AGENTS.md, design/SPEC.md and the skills exactly. Never ask the operator a question: take the industry-standard option, record it with its cost of reversing, and carry on. Aim for excellence in every section. Recto must end as the best PDF software in its class, a rival to Adobe Acrobat, Foxit, PDFelement and UPDF that users choose over them: before building a feature, look at how the best competitor does it and make Recto faster, with fewer steps, smarter defaults, better recovery from errors and better accessibility, proven with measurements. No shortcuts, no workarounds, no weakened tests, and no avoiding work because it is hard. This machine is short of memory and shared with other work: run one heavy job at a time, run full builds and full test suites through `bosun run --`, and wait while less than 3,000 MB is free. The operator authorises restarting, alone and once 3,000 MB is free, any review or job that was stopped for low memory, including the stopped review of D01 T02 §10.
'@
$goal = $goal.Trim().Replace('{ROOT}', $root)
if ($goal.Contains('"') -or $goal.Contains("`n")) { throw 'the goal must be one line with no double quote' }
if ($goal.Length -gt 4000) { throw "the goal is $($goal.Length) characters; Claude Code allows 4,000" }

$program = (Get-Command $Runner -ErrorAction SilentlyContinue).Source
if (-not $program) { throw "$Runner is not on PATH: install $runnerName, or open a new terminal after installing it" }

Write-Host "Recto unattended run in ${runnerName}: $root"
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
}
if ($Runner -ne 'claude') {
    Write-Host "Sign-in: $runnerName's own account on this PC."
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
$before = $env:CLAUDE_CODE_OAUTH_TOKEN
try {
    # A token this shell inherited is dropped for Antigravity, and replaced by the 1Password one for Claude Code.
    if ($Runner -ne 'claude') { Remove-Item Env:CLAUDE_CODE_OAUTH_TOKEN -ErrorAction SilentlyContinue }
    elseif ($token) { $env:CLAUDE_CODE_OAUTH_TOKEN = $token }
    $token = $null
    & $program @runArgs
    $code = $LASTEXITCODE
} finally {
    $env:CLAUDE_CODE_OAUTH_TOKEN = $before
}
exit $code
