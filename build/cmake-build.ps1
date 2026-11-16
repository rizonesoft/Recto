# The CMake and Ninja path of build/build.ps1 (D00 T04 section 8; design: docs/specs/2026-11-09-cmake-ninja-build-design.md).
# build.ps1 dot-sources this file. Windows PowerShell 5.1 compatible: CI runs `powershell -File build/build.ps1`.

# Section 3's 2,048 MB reserve and 1,536 MB per compiler, and one link's budget (recto-tests peaked at 457 MB Debug and
# 584 MB Release of commit), as cmake/RectoPools.cmake and cmake/memory-gate.cmake use them.
$script:RectoLinkMb = 1024

# Compile and link pools from one budget over free memory: links first (two only when two links and two compilers
# fit), then as many compilers as the rest holds, capped by the cores. RECTO_BUILD_JOBS and -MaxCpuCount still set the
# compile pool, as they set MSBuild's (section 3). Below one compiler and one link the build is refused.
function Resolve-RectoCMakePools {
    param(
        [int]$Cores = [Environment]::ProcessorCount,
        [AllowNull()][AllowEmptyString()][string]$JobsEnv = $env:RECTO_BUILD_JOBS,
        [int]$MaxCpuCount = 0,
        [AllowNull()][object]$AvailableMb = $null
    )
    $reserve = $script:RectoMemoryReserveMb
    $compiler = $script:RectoPerCompilerMb
    $link = $script:RectoLinkMb
    $freeMb = 0
    if ($null -ne $AvailableMb) {
        $value = 0.0
        if (-not (Test-PositiveFiniteNumber -Value $AvailableMb -Parsed ([ref]$value))) { throw "Invalid AvailableMb '$AvailableMb'." }
        $freeMb = [int][Math]::Floor($value)
    } else {
        $freeMb = Read-AvailableMemoryMb
    }
    $minimum = $reserve + $compiler + $link
    $freeText = $freeMb.ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture)
    if ($freeMb -lt $minimum) {
        $minText = $minimum.ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture)
        throw "Insufficient free memory to start build safely: free $freeText MB is below the $minText MB minimum (2,048 MB reserve + 1,536 MB for 1 compiler + 1,024 MB for 1 link). Close memory-heavy applications and retry."
    }
    $budget = $freeMb - $reserve
    $links = if ($budget -ge (2 * $link + 2 * $compiler)) { 2 } else { 1 }
    $mode = 'auto'
    if ($null -ne $JobsEnv -and $JobsEnv.Length -gt 0) {
        $parsed = 0
        if (-not [int]::TryParse($JobsEnv.Trim(), [ref]$parsed) -or $parsed -lt 1 -or $parsed -gt $Cores) {
            throw "Invalid RECTO_BUILD_JOBS='$JobsEnv': expected an integer between 1 and $Cores (logical processors on this machine)."
        }
        $compiles = $parsed
        $mode = 'env'
    } elseif ($MaxCpuCount -gt 0) {
        if ($MaxCpuCount -gt $Cores) { throw "Invalid -MaxCpuCount ${MaxCpuCount}: expected 0 or an integer between 1 and $Cores." }
        $compiles = $MaxCpuCount
        $mode = 'cli'
    } else {
        $compiles = [Math]::Max(1, [Math]::Min($Cores, [int][Math]::Floor(($budget - $links * $link) / $compiler)))
    }
    $suffix = switch ($mode) { 'env' { " (RECTO_BUILD_JOBS=$compiles)" } 'cli' { " (-MaxCpuCount $compiles)" } default { '' } }
    return [pscustomobject]@{
        FreeMb = $freeMb; Compiles = $compiles; Links = $links; Mode = $mode
        Header = "free $freeText MB, $Cores cores: compile pool $compiles, link pool $links$suffix"
    }
}

# This process's environment as the Visual Studio developer prompt for `Arch` sets it (vcvarsall.bat <arch> <sdk>), so
# cl, link, lib, dumpbin and the SDK come from the same Visual Studio MSBuild uses and the SDK Recto targets.
function Enter-RectoDevEnvironment([string]$VsPath, [string]$Arch, [string]$SdkVersion) {
    $vcvars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvarsall.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvarsall.bat not found in $VsPath" }
    $lines = & cmd.exe /d /s /c "`"$vcvars`" $Arch $SdkVersion >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvarsall.bat $Arch $SdkVersion failed (exit $LASTEXITCODE)" }
    foreach ($line in $lines) {
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) { [Environment]::SetEnvironmentVariable($line.Substring(0, $eq), $line.Substring($eq + 1), 'Process') }
    }
    if (-not $env:VSINSTALLDIR) { throw "vcvarsall.bat $Arch set no developer environment" }
}

function Get-RectoCMakePreset([string]$Platform, [string]$Configuration, [bool]$Asan) {
    $arch = if ($Platform -eq 'ARM64') { 'arm64' } else { 'x64' }
    $cfg = $Configuration.ToLowerInvariant()
    if ($Asan) { return "$arch-asan-$cfg" }
    return "$arch-$cfg"
}

# Configures and builds one preset with pools sized now, then returns the folder its executables are in. `-Target`
# narrows the build to one target (and recto-tests, as MSBuild's -Project path does); the boundary check and the
# build-start memory gate run inside the build itself (cmake/RectoBoundary.cmake, cmake/memory-gate.cmake).
function Invoke-RectoCMakeBuild {
    param(
        [string]$Root, [string]$CMake, [string]$Preset, [object]$Pools, [bool]$Sccache, [string[]]$Targets = @()
    )
    $configure = @('--preset', $Preset, "-DRECTO_COMPILE_JOBS=$($Pools.Compiles)", "-DRECTO_LINK_JOBS=$($Pools.Links)",
        "-DRECTO_SCCACHE=$(if ($Sccache) { 'ON' } else { 'OFF' })")
    Write-Host "=== cmake $($configure -join ' ')"
    & $CMake @configure
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed: $Preset (exit $LASTEXITCODE)" }
    $build = @('--build', '--preset', $Preset)
    if ($Targets.Count -gt 0) { $build += @('--target') + $Targets }
    Write-Host "=== cmake $($build -join ' ')"
    $log = Join-Path $Root "build\cmake-$Preset.log"
    $global:LASTEXITCODE = 0
    & $CMake @build 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Host
    $buildExit = $LASTEXITCODE
    if ($buildExit -ne 0) { throw "build failed: $Preset (exit $buildExit)" }
    # The build is warning-free by contract (D00 T01 section 3, D06 T05 section 27): /WX makes compiler warnings errors,
    # but command-line warnings (D9xxx) and tool warnings are not, so any warning line fails the build here, as
    # Assert-NoMSBuildWarnings does for MSBuild.
    $warnings = @(Select-String -LiteralPath $log -Pattern '\bwarning\b' -CaseSensitive:$false)
    if ($warnings.Count -gt 0) {
        $warnings | Select-Object -First 20 | ForEach-Object { Write-Host "  $($_.Line)" }
        throw "build produced $($warnings.Count) warning line(s): $Preset (log $log)"
    }
}

# Whether a build writes build/compile_commands.json: only a full x64 Debug build without AddressSanitizer, the
# configuration clangd reads (D00 T04 sections 8 and 12). Release, ARM64, ASan and -Project builds leave it alone.
function Test-RectoWritesCompileDatabase([string]$Platform, [string]$Configuration, [bool]$Asan, [string]$Project) {
    return (-not $Asan) -and ($Platform -eq 'x64') -and ($Configuration -eq 'Debug') -and (-not $Project)
}

# Replaces build/compile_commands.json with the preset's, through a temporary file in the same folder.
function Write-RectoCompileDatabase([string]$Root, [string]$Preset) {
    $source = Join-Path $Root "build\cmake\$Preset\compile_commands.json"
    $target = Join-Path $Root 'build\compile_commands.json'
    $temporary = "$target.tmp"
    Copy-Item -LiteralPath $source -Destination $temporary -Force
    Move-Item -LiteralPath $temporary -Destination $target -Force
    Write-Host "compile_commands.json from $Preset"
}