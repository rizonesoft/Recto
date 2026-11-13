# Recto's one-command build (D00 T01 section 3). From a clean clone:
#
#     git clone https://github.com/rizonesoft/Recto.git
#     cd Recto
#     .\build.cmd
#
# That restores NuGet packages, installs the vcpkg manifest (GoogleTest,
# test-only), and builds Recto.sln for x64 in Debug and Release (ARM64 is
# built in CI on every code push until Recto v1, D00 T04 section 2; pass
# -Platform x64,ARM64 or -Platform ARM64 to build ARM64 locally). Exit code 0
# only when the build also passed the x64 test binaries.
#
# Options: -Quick (x64 Debug + tests for fast local iteration),
# -Platform x64|ARM64 (default x64; pass x64,ARM64 or ARM64 to include ARM64),
# -Configuration Debug|Release (default both, or Debug under -Quick),
# -Project <vcxproj-or-target> (single project/target for fast local iteration),
# -TestFilter <pattern>, -TestOutputDir <folder> (default build\),
# -SkipTests, -EnableASAN, -ForceRestore, -MaxCpuCount <N> (0 = auto memory-bounded
# sizing with 2,048 MB reserve, refusing below 3,584 MB free; RECTO_BUILD_JOBS=<1..cores>
# overrides both automatic sizing and -MaxCpuCount).
# Requirements: Visual Studio 2026 with the v145 C++ toolset (its
# bundled vcpkg is used unless VCPKG_ROOT already points at one), the
# Windows 11 SDK 10.0.26100, and network access for the first restore.
# VS 2022 cannot substitute: the projects use the v145 toolset, which
# only VS 2026 ships. The installation must have the C++ x64 tools (and
# ARM64 tools when -Platform includes ARM64); vswhere picks the newest one
# that does. The Windows SDK comes
# from its own install and C++/WinRT and the SDK build tools from NuGet,
# so neither is asked of Visual Studio (MSBuild names a missing SDK itself).
param(
    [ValidateSet('x64', 'ARM64', 'x64,ARM64', 'ARM64,x64')][string[]]$Platform = @('x64'),
    [ValidateSet('Debug', 'Release', 'Debug,Release', 'Release,Debug')][string[]]$Configuration = @('Debug', 'Release'),
    [string]$Project = '',
    [string]$TestFilter = '',
    [string]$TestOutputDir = '',
    [switch]$Quick,
    [switch]$SkipTests,
    [switch]$EnableASAN,
    [switch]$ForceRestore,
    [int]$MaxCpuCount = 0
)
$ErrorActionPreference = 'Stop'
if (($Quick -or $EnableASAN) -and -not $PSBoundParameters.ContainsKey('Platform')) {
    $Platform = @('x64')
}
if ($Quick -and -not $PSBoundParameters.ContainsKey('Configuration')) {
    $Configuration = @('Debug')
}
$Platform = @($Platform | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($EnableASAN -and ($Platform -contains 'ARM64')) {
    throw 'AddressSanitizer (-EnableASAN) is supported on x64 only.'
}
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

. (Join-Path $PSScriptRoot 'parallelism.ps1')
$parallelism = Resolve-BuildParallelism -MaxCpuCount $MaxCpuCount
$jobs = $parallelism.Projects
$clMpCount = $parallelism.ClMpCount
Write-Host $parallelism.Header

function Get-RestoreFingerprint([string]$RepoRoot) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $files = @(
        (Join-Path $RepoRoot 'Recto.sln'),
        (Join-Path $RepoRoot 'Directory.Build.props'),
        (Join-Path $RepoRoot 'Directory.Build.targets'),
        (Join-Path $RepoRoot 'vcpkg.json'),
        (Join-Path $RepoRoot 'vcpkg-configuration.json')
    )
    $vcxprojs = @(Get-ChildItem -Path $RepoRoot -Recurse -Filter '*.vcxproj' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '\\(build|vcpkg_installed|\.git)\\' } |
        Sort-Object FullName)
    $files += @($vcxprojs | ForEach-Object { $_.FullName })
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.AppendLine("root:$([System.IO.Path]::GetFullPath($RepoRoot))")
    [void]$builder.AppendLine("nuget:$($env:NUGET_PACKAGES)")
    $prefixLen = $RepoRoot.TrimEnd('\', '/').Length + 1
    foreach ($f in $files) {
        if (Test-Path $f) {
            $full = [System.IO.Path]::GetFullPath($f)
            $rel = $full.Substring($prefixLen).Replace('\', '/')
            $fileHash = [BitConverter]::ToString($sha.ComputeHash([System.IO.File]::ReadAllBytes($full))).Replace('-', '')
            [void]$builder.AppendLine("${rel}:${fileHash}")
        }
    }
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($builder.ToString())
    $digest = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()
    $sha.Dispose()
    return @{ Hash = $digest; Vcxprojs = $vcxprojs }
}

function Test-RestoreCurrent([string]$RepoRoot, [string]$StampFile, [hashtable]$Meta) {
    if (-not (Test-Path $StampFile)) { return $false }
    $recorded = (Get-Content -Path $StampFile -Raw -ErrorAction SilentlyContinue).Trim()
    if ($recorded -ne $Meta.Hash) { return $false }
    foreach ($proj in $Meta.Vcxprojs) {
        $raw = Get-Content -Path $proj.FullName -Raw -ErrorAction SilentlyContinue
        if ($raw -match '<PackageReference\s') {
            $objDir = Join-Path $proj.DirectoryName 'obj'
            $assets = Join-Path $objDir 'project.assets.json'
            $gProps = Join-Path $objDir "$($proj.Name).nuget.g.props"
            $gTargets = Join-Path $objDir "$($proj.Name).nuget.g.targets"
            if (-not (Test-Path $assets) -or -not (Test-Path $gProps) -or -not (Test-Path $gTargets)) {
                return $false
            }
            try {
                $doc = Get-Content -Path $assets -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
            } catch {
                return $false
            }
            if ($null -eq $doc -or $null -eq $doc.packageFolders -or $null -eq $doc.libraries) {
                return $false
            }
            $pkgFolders = @($doc.packageFolders.PSObject.Properties.Name)
            if ($pkgFolders.Count -eq 0) { return $false }
            foreach ($libProp in $doc.libraries.PSObject.Properties) {
                $lib = $libProp.Value
                if ($null -eq $lib -or $lib.type -ne 'package' -or [string]::IsNullOrWhiteSpace($lib.path)) {
                    continue
                }
                $relPkg = ([string]$lib.path).Replace('/', '\')
                $foundPkg = $false
                foreach ($folder in $pkgFolders) {
                    $pkgDir = Join-Path $folder $relPkg
                    if (-not (Test-Path (Join-Path $pkgDir '.nupkg.metadata'))) {
                        continue
                    }
                    $missingFile = $false
                    if ($null -ne $lib.files) {
                        foreach ($entry in $lib.files) {
                            $entryStr = [string]$entry
                            if ($entryStr -match '\.(props|targets)$') {
                                if (-not (Test-Path (Join-Path $pkgDir ($entryStr.Replace('/', '\'))))) {
                                    $missingFile = $true
                                    break
                                }
                            }
                        }
                    }
                    if (-not $missingFile) {
                        $foundPkg = $true
                        break
                    }
                }
                if (-not $foundPkg) { return $false }
            }
        }
    }
    return $true
}

function Resolve-SolutionTarget([string]$RepoRoot, [string]$Requested) {
    $slnPath = Join-Path $RepoRoot 'Recto.sln'
    $rx = [regex]'^Project\("\{[0-9A-Fa-f-]+\}"\)\s*=\s*"([^"]+)",\s*"([^"]+\.vcxproj)"'
    $entries = @()
    foreach ($line in (Get-Content -Path $slnPath)) {
        $m = $rx.Match($line)
        if ($m.Success) {
            $name = $m.Groups[1].Value
            $rel = $m.Groups[2].Value
            $full = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $rel))
            $entries += [pscustomobject]@{ Name = $name; RelPath = $rel; FullPath = $full }
        }
    }
    $trimmed = $Requested.Trim()
    $reqNorm = $trimmed.Replace('/', '\').TrimStart('.', '\')
    $reqFull = if ([System.IO.Path]::IsPathRooted($trimmed)) {
        [System.IO.Path]::GetFullPath($trimmed)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $trimmed))
    }
    $reqStem = [System.IO.Path]::GetFileNameWithoutExtension($trimmed)
    foreach ($entry in $entries) {
        if ($entry.Name.Equals($trimmed, [System.StringComparison]::OrdinalIgnoreCase) -or
            $entry.RelPath.Equals($reqNorm, [System.StringComparison]::OrdinalIgnoreCase) -or
            $entry.FullPath.Equals($reqFull, [System.StringComparison]::OrdinalIgnoreCase) -or
            ($trimmed -match '\.vcxproj$' -and $entry.Name.Equals($reqStem, [System.StringComparison]::OrdinalIgnoreCase))) {
            return $entry.Name.Replace('.', '_')
        }
    }
    throw "Unknown project '$Requested'. Valid projects in Recto.sln: $($entries.Name -join ', ')"
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$required = @('Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
if ($Platform -contains 'ARM64') {
    $required += 'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
}
$vs = & $vswhere -version '[18.0,19.0)' -latest -requires $required -property installationPath
if (-not $vs) {
    $missing = @($required | Where-Object { -not (& $vswhere -version '[18.0,19.0)' -latest -requires $_ -property installationPath) })
    throw "No Visual Studio 2026 with the required C++ tools (the v145 toolset is VS 2026 only); the newest one lacks: $($missing -join ', ')"
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

# MSBuild's LinkEmbedManifest writes *_manifest.rc only when absent, embedding an absolute path; self-heal after a clone move.
Get-ChildItem -Path (Join-Path $root 'build') -Recurse -Filter '*_manifest.rc' -ErrorAction SilentlyContinue | ForEach-Object {
    $rcText = Get-Content -Path $_.FullName -Raw -ErrorAction SilentlyContinue
    if ($rcText -and ($rcText.IndexOf($root, [System.StringComparison]::OrdinalIgnoreCase) -lt 0)) {
        Remove-Item -LiteralPath $_.FullName -Force
    }
}

$restoreStamp = Join-Path $root 'build\restore.stamp'
$restoreMeta = Get-RestoreFingerprint -RepoRoot $root
if ($ForceRestore -or -not (Test-RestoreCurrent -RepoRoot $root -StampFile $restoreStamp -Meta $restoreMeta)) {
    Write-Host '=== Restoring NuGet packages for Recto.sln'
    & $msbuild 'Recto.sln' '-t:Restore' "/m:$jobs" '/nr:false' '/nologo' '/v:minimal'
    if ($LASTEXITCODE -ne 0) { throw "NuGet restore failed (exit $LASTEXITCODE)" }
    Set-Content -Path $restoreStamp -Value $restoreMeta.Hash -Encoding Ascii -NoNewline
}

$genCompileDb = Join-Path $root 'scripts\gen-compile-commands.py'
if ((Test-Path $genCompileDb) -and (Get-Command python -ErrorAction SilentlyContinue)) {
    & python $genCompileDb | Out-Null
}

$asanProjects = @('recto-pdf', 'recto-core', 'recto-services', 'recto-ui', 'Recto', 'recto-cli', 'recto-tests')
$resolvedProject = if ($Project) { Resolve-SolutionTarget -RepoRoot $root -Requested $Project } else { '' }
if ($EnableASAN -and $resolvedProject -and ($asanProjects -notcontains $resolvedProject)) {
    throw "Project '$resolvedProject' is not supported under -EnableASAN."
}
$binFolder = if ($EnableASAN) { 'bin-asan' } else { 'bin' }

foreach ($plat in $Platform) {
    foreach ($cfg in $Configuration) {
        # An ASan build has its own obj and bin folders (Directory.Build.props), so neither build touches the other's.
        $desiredAsanState = if ($EnableASAN) { 'true' } else { 'false' }

        # The boundary manifests of this configuration come from this build only (scripts/check-ui-boundary.py).
        $boundaryManifests = Join-Path $root "build\boundary\$plat\$cfg"
        if (-not $Project) {
            if (Test-Path $boundaryManifests) { Remove-Item $boundaryManifests -Recurse -Force }
        }
        $logFile = Join-Path $root "build\msbuild-$cfg-$plat.log"
        $msbuildArgs = @(
            'Recto.sln',
            "/p:Configuration=$cfg",
            "/p:Platform=$plat",
            "/p:TreatWarningsAsErrors=true",
            "/p:EnableASAN=$desiredAsanState",
            "/p:CL_MPCount=$clMpCount",
            '/fl',
            "/flp:logfile=$logFile;verbosity=normal",
            "/m:$jobs",
            '/nr:false',
            '/nologo'
        )
        if ($resolvedProject) {
            $targets = @($resolvedProject)
            if (-not $SkipTests -and $plat -eq 'x64' -and ($targets -notcontains 'recto-tests')) {
                $targets += 'recto-tests'
            }
            $msbuildArgs += "-t:$($targets -join ';')"
            $asanNote = if ($EnableASAN) { ', ASan' } else { '' }
            Write-Host "=== Recto $cfg|$plat ($($targets -join ';')$asanNote)"
        } elseif ($EnableASAN) {
            $msbuildArgs += "-t:$($asanProjects -join ';')"
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
        if (-not $Project) {
            & python (Join-Path $root 'scripts\check-ui-boundary.py') --configuration $cfg --platform $plat --vs $vs
            if ($LASTEXITCODE -ne 0) { throw "the engine/recto-ui boundary check failed: $cfg|$plat (exit $LASTEXITCODE)" }
        }

        if ($EnableASAN -and $plat -eq 'x64') {
            # Recto.exe too: recto-tests launches it (Ui.App tests).
            foreach ($exeProj in @('recto-tests', 'recto-cli', 'Recto')) {
                $exe = Join-Path $root "build\$binFolder\x64\$cfg\$exeProj\$exeProj.exe"
                if (Test-Path $exe) {
                    $staged = Copy-AsanRuntime -ExePath $exe -AsanBinDir $asanBinDir -Dumpbin (Join-Path $asanBinDir 'dumpbin.exe')
                    Write-Host "Staged for ${exeProj}: $($staged -join ', ')"
                }
            }
        }
    }
}

if (-not $SkipTests -and $Platform -contains 'x64') {
    $prevExpectAsan = $env:RECTO_EXPECT_ASAN
    try {
        $env:RECTO_EXPECT_ASAN = if ($EnableASAN) { '1' } else { '0' }
        foreach ($cfg in $Configuration) {
            $tests = Join-Path $root "build\$binFolder\x64\$cfg\recto-tests\recto-tests.exe"
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
