# Warning check for build/build.ps1, kept apart so tests/scripts/test_build_ps1.py can drive it with a sample log.

function Assert-NoMSBuildWarnings {
    param(
        [Parameter(Mandatory)][string]$LogFile,
        [Parameter(Mandatory)][string]$What
    )
    # MSBuild's file logger ends with "N Warning(s)" and "N Error(s)". A missing summary means the log did not finish,
    # which is no evidence of a clean build either.
    $summary = Select-String -Path $LogFile -Pattern '^\s*(\d+) Warning\(s\)\s*$' | Select-Object -Last 1
    if (-not $summary) { throw "build log has no warning summary: $What ($LogFile)" }
    $count = [int]$summary.Matches[0].Groups[1].Value
    if ($count -gt 0) {
        $lines = Select-String -Path $LogFile -Pattern ': warning ' | Select-Object -First 10 | ForEach-Object { $_.Line.Trim() }
        throw "build has $count warning(s): $What`n$($lines -join "`n")"
    }
}
