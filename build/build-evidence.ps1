# What a build left behind, and what changed between two records (D00 T04 §1).
#
#   build-evidence.ps1 -Record <file.json> [-Asan] [-Platform x64] [-Configuration Debug]
#   build-evidence.ps1 -Compare <before.json> <after.json>
#
# A record holds the SHA-256 of every file under one mode's obj and bin folders for one configuration, and the write
# time of each object file. Compare prints the files that changed, appeared or went, and the object files rewritten
# (which is what the compiler did), and exits 0. It reads; it never deletes or writes anything but the record.
[CmdletBinding(DefaultParameterSetName = 'Record')]
param(
    [Parameter(ParameterSetName = 'Record', Mandatory)][string]$Record,
    [Parameter(ParameterSetName = 'Record')][switch]$Asan,
    [Parameter(ParameterSetName = 'Record')][string]$Platform = 'x64',
    [Parameter(ParameterSetName = 'Record')][string]$Configuration = 'Debug',
    [Parameter(ParameterSetName = 'Compare', Mandatory, Position = 0)][string]$Compare,
    [Parameter(ParameterSetName = 'Compare', Mandatory, Position = 1)][string]$With
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ($PSCmdlet.ParameterSetName -eq 'Record') {
    $obj = Join-Path $root ('build\' + ($(if ($Asan) { 'obj-asan' } else { 'obj' })))
    $bin = Join-Path $root ('build\' + ($(if ($Asan) { 'bin-asan' } else { 'bin' })) + "\$Platform\$Configuration")
    $files = @()
    if (Test-Path $obj) {
        # Each project's folder for this configuration: build\obj\<project>\<platform>\<configuration>\.
        $files += Get-ChildItem $obj -Directory | ForEach-Object { Join-Path $_.FullName "$Platform\$Configuration" } |
            Where-Object { Test-Path $_ } | ForEach-Object { Get-ChildItem $_ -Recurse -File }
    }
    if (Test-Path $bin) {
        $files += Get-ChildItem $bin -Recurse -File
    }
    $entries = foreach ($file in $files) {
        [pscustomobject]@{
            path    = $file.FullName.Substring($root.Length + 1)
            sha256  = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
            written = $file.LastWriteTimeUtc.Ticks
        }
    }
    $entries = @($entries)
    $json = ConvertTo-Json -InputObject $entries -Depth 3
    $temporary = "$Record.tmp"
    Set-Content -LiteralPath $temporary -Value $json -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Record -Force
    Write-Host "recorded $($entries.Count) files ($(@($entries | Where-Object { $_.path -like '*.obj' }).Count) object files) to $Record"
    exit 0
}

$before = @{}
foreach ($entry in (Get-Content -LiteralPath $Compare -Raw | ConvertFrom-Json)) { $before[$entry.path] = $entry }
$after = @{}
foreach ($entry in (Get-Content -LiteralPath $With -Raw | ConvertFrom-Json)) { $after[$entry.path] = $entry }
$changed = @($after.Keys | Where-Object { $before.ContainsKey($_) -and $before[$_].sha256 -ne $after[$_].sha256 } | Sort-Object)
$appeared = @($after.Keys | Where-Object { -not $before.ContainsKey($_) } | Sort-Object)
$gone = @($before.Keys | Where-Object { -not $after.ContainsKey($_) } | Sort-Object)
$rewritten = @($after.Keys | Where-Object { $_ -like '*.obj' -and $before.ContainsKey($_) -and $before[$_].written -ne $after[$_].written } | Sort-Object)
Write-Host "changed $($changed.Count), appeared $($appeared.Count), gone $($gone.Count), object files rewritten $($rewritten.Count)"
foreach ($path in $rewritten) { Write-Host "  rewritten $path" }
foreach ($path in $gone) { Write-Host "  gone $path" }
exit 0
