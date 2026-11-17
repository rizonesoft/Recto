# The build's in-flight mark, dot-sourced by build\build.ps1 and (under build.cmd's -MarkBuild) build\bootstrap-tools.ps1.
# While a build runs, build\build-in-flight\<pid>.json names it (pid, creation time, a 2-hour deadline), so the
# campaign run guard (scripts\campaign_guard.py build_in_flight) lets the session end its turn and wait for the
# build's notification instead of sleep-polling its output. A mark whose process has ended, or whose pid now belongs
# to another process, is never believed, so a killed build cannot hold a run.

function Remove-RectoBuildMark([string]$Path) {
    # A mark left behind is harmless (its process has ended), so a failed removal warns and never masks the build's
    # own result.
    if (-not $Path) { return }
    try {
        if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    } catch {
        Write-Warning "could not remove the build mark ${Path}: $_"
    }
}

# Writes this process's mark and returns its path, or $null when this process is already marked (the caller then
# leaves the existing mark to its owner). Marks of builds that have ended are swept first.
function Enter-RectoBuildMark([string]$Root, [string]$Label) {
    $dir = Join-Path $Root 'build\build-in-flight'
    $path = Join-Path $dir "$PID.json"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    foreach ($old in @(Get-ChildItem -LiteralPath $dir -Filter '*.json' -File)) {
        $oldPid = 0
        # Get-Process reports a missing pid as an error; here that is the answer (the build that wrote it is gone).
        $alive = [int]::TryParse($old.BaseName, [ref]$oldPid) -and $null -ne (Get-Process -Id $oldPid -ErrorAction Ignore)
        if (-not $alive) { Remove-RectoBuildMark $old.FullName }
    }
    if (Test-Path -LiteralPath $path) { return $null }
    $doc = [ordered]@{
        pid      = $PID
        started  = (Get-Process -Id $PID).StartTime.ToFileTimeUtc()
        deadline = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 7200
        args     = $Label
    }
    $temp = "$path.tmp"
    [System.IO.File]::WriteAllText($temp, ($doc | ConvertTo-Json -Compress))
    Move-Item -LiteralPath $temp -Destination $path -Force
    return $path
}
