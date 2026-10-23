# Recto's one-command build (D00 T01 section 3). From a clean clone:
#
#     git clone https://github.com/rizonesoft/Recto.git
#     cd Recto
#     .\build.cmd
#
# That restores NuGet packages, installs the vcpkg manifest (GoogleTest,
# test-only), and builds Recto.sln for x64 and ARM64 in Debug and
# Release. Exit code 0 only when the build also passed the x64 test
# binaries.
#
# Options: -Platform x64|ARM64 (default both, or x64 under -EnableASAN),
# -Configuration Debug|Release (default both), -TestFilter <pattern>,
# -TestOutputDir <folder> (where the gtest XML reports go; default build\),
# -SkipTests, -EnableASAN.
# Requirements: Visual Studio 2026 with the v145 C++ toolset (its
# bundled vcpkg is used unless VCPKG_ROOT already points at one), the
# Windows 11 SDK 10.0.26100, and network access for the first restore.
# VS 2022 cannot substitute: the projects use the v145 toolset, which
# only VS 2026 ships. The installation must have the C++ x64 and ARM64
# tools; vswhere picks the newest one that does. The Windows SDK comes
# from its own install and C++/WinRT and the SDK build tools from NuGet,
# so neither is asked of Visual Studio (MSBuild names a missing SDK itself).
param(
    [ValidateSet('x64', 'ARM64', 'x64,ARM64', 'ARM64,x64')][string[]]$Platform = @('x64', 'ARM64'),
    [ValidateSet('Debug', 'Release', 'Debug,Release', 'Release,Debug')][string[]]$Configuration = @('Debug', 'Release'),
    [string]$TestFilter = '',
    [string]$TestOutputDir = '',
    [switch]$SkipTests,
    [switch]$EnableASAN
)
$ErrorActionPreference = 'Stop'
if ($EnableASAN -and -not $PSBoundParameters.ContainsKey('Platform')) {
    $Platform = @('x64')
}
$Platform = @($Platform | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($EnableASAN -and ($Platform -contains 'ARM64')) {
    throw 'AddressSanitizer (-EnableASAN) is supported on x64 only.'
}
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$required = @(
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
)
$vs = & $vswhere -version '[18.0,19.0)' -latest -requires $required -property installationPath
if (-not $vs) {
    $missing = @($required | Where-Object { -not (& $vswhere -version '[18.0,19.0)' -latest -requires $_ -property installationPath) })
    throw "No Visual Studio 2026 with the C++ x64 and ARM64 tools (the v145 toolset is VS 2026 only); the newest one lacks: $($missing -join ', ')"
}
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
if (-not $env:VCPKG_ROOT) {
    $vsVcpkg = Join-Path $vs 'VC\vcpkg'
    if (Test-Path (Join-Path $vsVcpkg 'scripts\buildsystems\msbuild\vcpkg.props')) {
        $env:VCPKG_ROOT = $vsVcpkg
    } elseif ($env:VCPKG_INSTALLATION_ROOT -and (Test-Path (Join-Path $env:VCPKG_INSTALLATION_ROOT 'scripts\buildsystems\msbuild\vcpkg.props'))) {
        $env:VCPKG_ROOT = $env:VCPKG_INSTALLATION_ROOT
    } else {
        $env:VCPKG_ROOT = $vsVcpkg
    }
}
Write-Host "MSBuild:    $msbuild"
Write-Host "VCPKG_ROOT: $env:VCPKG_ROOT"

. (Join-Path $PSScriptRoot 'asan-runtime.ps1')
. (Join-Path $PSScriptRoot 'msbuild-log.ps1')
# MSBuild's messages, its log summary ("N Warning(s)") included, in English whatever language Visual Studio is
# installed in, so Assert-NoMSBuildWarnings reads the same text everywhere.
$env:VSLANG = '1033'
$asanBinDir = $null
if ($EnableASAN) {
    $vcToolsRoot = Join-Path $vs 'VC\Tools\MSVC'
    $asanDll = Get-ChildItem -Path $vcToolsRoot -Recurse -Filter 'clang_rt.asan_dynamic-x86_64.dll' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\clang_rt\.asan_dynamic-x86_64\.dll$' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $asanDll) {
        throw "clang_rt.asan_dynamic-x86_64.dll not found under $vcToolsRoot"
    }
    $asanBinDir = $asanDll.DirectoryName
    Write-Host "ASan bin:   $asanBinDir"
    $env:PATH = "$asanBinDir;$env:PATH"
}

New-Item -ItemType Directory -Path (Join-Path $root 'build') -Force | Out-Null
$asanProjects = @('recto-pdf', 'recto-core', 'recto-services', 'recto-ui', 'Recto', 'recto-cli', 'recto-tests')

foreach ($plat in $Platform) {
    foreach ($cfg in $Configuration) {
        $desiredAsanState = if ($EnableASAN) { 'true' } else { 'false' }
        $stateFile = Join-Path $root "build\asan-state-$cfg-$plat.txt"
        $prevAsanState = if (Test-Path $stateFile) { (Get-Content $stateFile -Raw).Trim() } else { '' }
        if ($prevAsanState -ne $desiredAsanState) {
            foreach ($proj in $asanProjects) {
                $objDir = Join-Path $root "build\obj\$proj\$plat\$cfg"
                $binDir = Join-Path $root "build\bin\$plat\$cfg\$proj"
                if (Test-Path $objDir) { Remove-Item $objDir -Recurse -Force }
                if (Test-Path $binDir) { Remove-Item $binDir -Recurse -Force }
            }
        }

        # The boundary manifests of this configuration come from this build only (scripts/check-ui-boundary.py).
        $boundaryManifests = Join-Path $root "build\boundary\$plat\$cfg"
        if (Test-Path $boundaryManifests) { Remove-Item $boundaryManifests -Recurse -Force }
        $logFile = Join-Path $root "build\msbuild-$cfg-$plat.log"
        $msbuildArgs = @(
            'Recto.sln',
            '-restore',
            "/p:Configuration=$cfg",
            "/p:Platform=$plat",
            "/p:TreatWarningsAsErrors=true",
            "/p:EnableASAN=$desiredAsanState",
            '/fl',
            "/flp:logfile=$logFile;verbosity=normal",
            '/m',
            '/nologo'
        )
        if ($EnableASAN) {
            $msbuildArgs += '-t:recto-pdf;recto-core;recto-services;recto-ui;Recto;recto-cli;recto-tests'
            Write-Host "=== Recto $cfg|$plat (ASan)"
        } else {
            Write-Host "=== Recto $cfg|$plat"
        }
        & $msbuild @msbuildArgs
        if ($LASTEXITCODE -ne 0) { throw "build failed: $cfg|$plat (exit $LASTEXITCODE)" }
        # A warning from any tool MSBuild runs (not only the compiler, which /WX already stops) fails the build: the
        # build is warning-free on x64 and ARM64 by contract (D00 T01 §3, D06 T05 §27).
        Assert-NoMSBuildWarnings -LogFile $logFile -What "$cfg|$plat"
        # The D13 boundary between the engine libraries and recto-ui, proven from the compiler's /sourceDependencies
        # reports and the librarian's directives, members and symbols of this configuration's libraries (D10 T01 §1).
        # It reads what this build compiled from the manifests the RectoBoundaryManifest target wrote (emptied below,
        # before the build), never from a re-evaluation of the projects.
        & python (Join-Path $root 'scripts\check-ui-boundary.py') --configuration $cfg --platform $plat --vs $vs
        if ($LASTEXITCODE -ne 0) { throw "the engine/recto-ui boundary check failed: $cfg|$plat (exit $LASTEXITCODE)" }

        if ($EnableASAN -and $plat -eq 'x64') {
            # Recto.exe too: recto-tests launches it (Ui.App tests).
            foreach ($exeProj in @('recto-tests', 'recto-cli', 'Recto')) {
                $exe = Join-Path $root "build\bin\x64\$cfg\$exeProj\$exeProj.exe"
                if (Test-Path $exe) {
                    $staged = Copy-AsanRuntime -ExePath $exe -AsanBinDir $asanBinDir -Dumpbin (Join-Path $asanBinDir 'dumpbin.exe')
                    Write-Host "Staged for ${exeProj}: $($staged -join ', ')"
                }
            }
        }
        Set-Content -Path $stateFile -Value $desiredAsanState -NoNewline
    }
}

if (-not $SkipTests -and $Platform -contains 'x64') {
    $prevExpectAsan = $env:RECTO_EXPECT_ASAN
    try {
        $env:RECTO_EXPECT_ASAN = if ($EnableASAN) { '1' } else { '0' }
        foreach ($cfg in $Configuration) {
            $tests = Join-Path $root "build\bin\x64\$cfg\recto-tests\recto-tests.exe"
            $xmlDir = if (-not $TestOutputDir) { Join-Path $root 'build' } elseif ([System.IO.Path]::IsPathRooted($TestOutputDir)) { $TestOutputDir } else { Join-Path $root $TestOutputDir }
            New-Item -ItemType Directory -Path $xmlDir -Force | Out-Null
            $xmlOut = Join-Path $xmlDir "test-results-x64-$cfg.xml"
            if (Test-Path $xmlOut) { Remove-Item $xmlOut -Force }
            $argsList = @("--gtest_output=xml:$xmlOut")
            if ($TestFilter) {
                $argsList += "--gtest_filter=$TestFilter"
            }
            Write-Host "=== recto-tests $cfg|x64"
            & $tests @argsList
            if ($LASTEXITCODE -ne 0) { throw "recto-tests failed: $cfg|x64 (exit $LASTEXITCODE)" }
            if (-not (Test-Path $xmlOut)) { throw "recto-tests did not produce XML report: $xmlOut" }
            [xml]$report = Get-Content $xmlOut
            if ([int]$report.testsuites.tests -le 0) {
                throw "recto-tests executed 0 tests (filter='$TestFilter'): $cfg|x64"
            }
        }
    } finally {
        if ($null -eq $prevExpectAsan) {
            Remove-Item Env:\RECTO_EXPECT_ASAN -ErrorAction SilentlyContinue
        } else {
            $env:RECTO_EXPECT_ASAN = $prevExpectAsan
        }
    }
}

Write-Host 'Recto build complete.'
