# Recto's one-command build (D00 T01 section 3). From a clean clone:
#
#     git clone https://github.com/rizonesoft/RectoPDF.git
#     cd RectoPDF
#     .\build.cmd
#
# That fetches the MuPDF submodule and its nested thirdparty sources
# (every nested submodule except MuJS, which stays uninitialized by
# design), restores NuGet packages, installs the vcpkg manifest
# (GoogleTest, test-only), and builds Recto.sln for x64 and ARM64 in
# Debug and Release. Exit code 0 only when the last build also passed
# the x64 test binaries.
#
# Options: -Platform x64|ARM64 (default both), -Configuration
# Debug|Release (default both), -SkipTests. Requirements: Visual Studio
# 2026 with the v145 C++ toolset (its bundled vcpkg is used unless
# VCPKG_ROOT already points at one), the Windows 11 SDK 10.0.26100, and
# network access for the first restore. VS 2022 cannot substitute: it
# has no Windows Store ARM64 platform, so the WinUI app does not build
# for ARM64 there (found in D00 T01 section 3).
param(
    [ValidateSet('x64', 'ARM64')][string[]]$Platform = @('x64', 'ARM64'),
    [ValidateSet('Debug', 'Release')][string[]]$Configuration = @('Debug', 'Release'),
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -version '[18.0,19.0)' -latest -property installationPath
if (-not $vs) { throw 'Visual Studio 2026 not found (v145 toolset; VS 2022 has no Windows Store ARM64 platform, D00 T01 section 3)' }
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = Join-Path $vs 'VC\vcpkg' }
Write-Host "MSBuild:  $msbuild"
Write-Host "VCPKG_ROOT: $env:VCPKG_ROOT"

git submodule update --init
if ($LASTEXITCODE -ne 0) { throw 'git submodule update --init failed' }
if (-not (Test-Path 'third_party\mupdf\thirdparty\freetype\include\freetype\freetype.h')) {
    # Never --recursive: it would initialize MuJS (see build/mupdf-win32.targets).
    git -C third_party/mupdf submodule update --init -- . ':(exclude)thirdparty/mujs'
    if ($LASTEXITCODE -ne 0) { throw 'nested submodule init failed' }
}

foreach ($plat in $Platform) {
    foreach ($cfg in $Configuration) {
        Write-Host "=== Recto $cfg|$plat"
        & $msbuild Recto.sln -restore "/p:Configuration=$cfg" "/p:Platform=$plat" /m /nologo
        if ($LASTEXITCODE -ne 0) { throw "build failed: $cfg|$plat (exit $LASTEXITCODE)" }
    }
}

if (-not $SkipTests -and $Platform -contains 'x64') {
    foreach ($cfg in $Configuration) {
        $tests = Join-Path $root "build\bin\x64\$cfg\recto-tests\recto-tests.exe"
        Write-Host "=== recto-tests $cfg|x64"
        & $tests
        if ($LASTEXITCODE -ne 0) { throw "recto-tests failed: $cfg|x64 (exit $LASTEXITCODE)" }
    }
}

Write-Host 'Recto build complete.'
