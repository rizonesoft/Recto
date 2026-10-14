# Builds the pinned MuPDF static libraries exactly as D00 T01 section 2 wired
# them: build/mupdf-config.h forced-included (FZ_ENABLE_JS 0) and
# build/mupdf-win32.targets removing the MuJS unity file. Invoked by the
# `mupdf` project in Recto.sln (third_party/mupdf.vcxproj); also runnable
# directly. Exits early when the libraries are already built from the same
# submodule commit, config and toolset (stamp under build/obj/).
param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'ARM64')][string]$Platform,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$mupdf = Join-Path $root 'third_party\mupdf'
$outDir = Join-Path $mupdf "platform\win32\$Platform\$Configuration"
$libmupdf = Join-Path $outDir 'libmupdf.lib'

function Get-Sha256([string]$Path) {
    # Deliberately not Get-FileHash: it is absent from some Windows
    # PowerShell installs this script must run on (seen on the M0 host).
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '') }
    finally { $sha.Dispose() }
}

function Get-StampKey {
    $head = git -C $mupdf rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'third_party/mupdf is not checked out; run git submodule update --init first' }
    $hashes = @('build\mupdf-config.h', 'build\mupdf-win32.targets', 'build\build-mupdf.ps1') | ForEach-Object {
        Get-Sha256 (Join-Path $root $_)
    }
    return (@($head, $Configuration, $Platform, 'v145') + $hashes) -join ' '
}

$stampDir = Join-Path $root 'build\obj'
$stamp = Join-Path $stampDir "mupdf-$Configuration-$Platform.stamp"
$key = Get-StampKey
if (-not $Clean -and (Test-Path $libmupdf) -and (Test-Path $stamp) -and ((Get-Content -Raw $stamp).Trim() -eq $key)) {
    Write-Host "mupdf $Configuration|$Platform up to date (stamp matches the pinned commit and Recto config)"
    exit 0
}

# The upstream .gitignore covers platform/win32/x64 but not ARM64, so an
# ARM64 build dirties the submodule with thousands of output files. Use
# the submodule's local exclude file (never a tracked upstream change) to
# keep its status and therefore the superproject's clean.
$exclude = git -C $mupdf rev-parse --git-path info/exclude
if ($LASTEXITCODE -eq 0 -and $exclude) {
    if (-not [IO.Path]::IsPathRooted($exclude)) { $exclude = Join-Path $mupdf $exclude }
    $lines = @(Get-Content -LiteralPath $exclude -ErrorAction SilentlyContinue)
    if ($lines -notcontains 'platform/win32/ARM64/') {
        Add-Content -LiteralPath $exclude -Value 'platform/win32/ARM64/' -Encoding Ascii
    }
}

# Nested thirdparty sources are required; MuJS deliberately stays
# uninitialized (see build/mupdf-win32.targets). Never use --recursive here.
if (-not (Test-Path (Join-Path $mupdf 'thirdparty\freetype\include\freetype\freetype.h'))) {
    git -C $mupdf submodule update --init -- . ':(exclude)thirdparty/mujs'
    if ($LASTEXITCODE -ne 0) { throw 'nested submodule init failed' }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -version '[18.0,19.0)' -latest -property installationPath
if (-not $vs) { throw 'Visual Studio 2026 (v145 toolset) not found; Recto standardizes on it because VS 2022 has no Windows Store ARM64 platform (D00 T01 section 3)' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'

$env:CL = '/FI"' + (Join-Path $root 'build\mupdf-config.h') + '"'
$target = if ($Clean) { 'libmupdf:Rebuild' } else { 'libmupdf' }
try {
    & $msbuild (Join-Path $mupdf 'platform\win32\mupdf.sln') "/t:$target" `
        "/p:Configuration=$Configuration" "/p:Platform=$Platform" /p:PlatformToolset=v145 `
        "/p:CustomAfterMicrosoftCommonTargets=$(Join-Path $root 'build\mupdf-win32.targets')" `
        /m /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "MuPDF build failed with exit $LASTEXITCODE" }
} finally {
    Remove-Item Env:CL -ErrorAction SilentlyContinue
}

if (-not (Test-Path $libmupdf)) { throw "expected output missing: $libmupdf" }

# D12 as a failing check, not a hope: no MuJS symbols defined in
# libthirdparty.lib and none referenced undefined from libmupdf.lib.
$dumpbin = Get-ChildItem (Join-Path $vs 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe') | Select-Object -First 1 -ExpandProperty FullName
foreach ($lib in @('libmupdf.lib', 'libthirdparty.lib')) {
    $symbols = & $dumpbin /nologo /symbols (Join-Path $outDir $lib)
    $js = $symbols | Select-String -Pattern '\bjs_[A-Za-z]' | Where-Object { $_.Line -match 'External|UNDEF' }
    if ($js) { throw "MuJS symbols found in ${lib}: $($js | Select-Object -First 3 | ForEach-Object { $_.Line.Trim() })" }
}

New-Item -ItemType Directory -Force -Path $stampDir | Out-Null
Set-Content -LiteralPath $stamp -Value $key -Encoding Ascii
Write-Host "mupdf $Configuration|$Platform built; no MuJS symbols; stamp written"
