# Memory-bounded MSBuild project (/m) and cl.exe (/MP via CL_MPCount) parallelism (D00 T04 section 3).
# Reserves 2,048 MB for the OS and desktop, budgets 1,536 MB per concurrent compiler, and splits the
# total compiler budget across MSBuild nodes (/m:<projects>) and per-project cl.exe workers
# (/p:CL_MPCount=<compilers per project>) so projects * CL_MPCount never exceeds the budget.

$script:RectoMemoryReserveMb = 2048
$script:RectoPerCompilerMb = 1536
$script:RectoMinAutoMemoryMb = $script:RectoMemoryReserveMb + $script:RectoPerCompilerMb

function Test-PositiveFiniteNumber([object]$Value, [ref]$Parsed) {
    if ($null -eq $Value -or $Value -is [bool] -or $Value -is [char] -or $Value -is [DateTime]) {
        return $false
    }
    $num = 0.0
    if ($Value -is [double] -or $Value -is [float] -or $Value -is [decimal] -or
        $Value -is [int] -or $Value -is [long] -or $Value -is [uint32] -or $Value -is [uint64] -or
        $Value -is [int16] -or $Value -is [uint16] -or $Value -is [byte] -or $Value -is [sbyte]) {
        $num = [Convert]::ToDouble($Value, [System.Globalization.CultureInfo]::InvariantCulture)
    } elseif ($Value -is [string]) {
        if (-not [double]::TryParse($Value.Trim(), [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$num)) {
            return $false
        }
    } else {
        return $false
    }
    if ([double]::IsNaN($num) -or [double]::IsInfinity($num) -or $num -le 0.0) {
        return $false
    }
    $Parsed.Value = $num
    return $true
}

function Read-AvailableMemoryMb {
    param(
        [scriptblock]$CounterProbe = {
            $samples = @((Get-Counter -Counter '\Memory\Available MBytes' -ErrorAction Stop).CounterSamples)
            if ($samples.Count -lt 1) { throw 'no CounterSamples returned' }
            $samples[0].CookedValue
        },
        [scriptblock]$CimProbe = { ([double](Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop).FreePhysicalMemory) / 1024.0 }
    )
    $counterErr = $null
    try {
        $rawCounter = & $CounterProbe
        $val = 0.0
        if (Test-PositiveFiniteNumber -Value $rawCounter -Parsed ([ref]$val)) {
            return [int][Math]::Floor($val)
        }
        $counterErr = "returned unusable value '$rawCounter'"
    } catch {
        $counterErr = $_.Exception.Message
    }

    $cimErr = $null
    try {
        $rawCim = & $CimProbe
        $val = 0.0
        if (Test-PositiveFiniteNumber -Value $rawCim -Parsed ([ref]$val)) {
            return [int][Math]::Floor($val)
        }
        $cimErr = "returned unusable value '$rawCim'"
    } catch {
        $cimErr = $_.Exception.Message
    }

    throw "Cannot determine available physical memory ('\Memory\Available MBytes' failed: $counterErr; 'Win32_OperatingSystem.FreePhysicalMemory' failed: $cimErr). Repair Windows performance counters or WMI, or set RECTO_BUILD_JOBS=<1..cores> (or pass -MaxCpuCount <1..cores>) explicitly."
}

function Resolve-BuildParallelism {
    param(
        [int]$Cores = [Environment]::ProcessorCount,
        [AllowNull()][AllowEmptyString()][string]$JobsEnv = $env:RECTO_BUILD_JOBS,
        [int]$MaxCpuCount = 0,
        [AllowNull()][object]$AvailableMb = $null,
        [AllowNull()][scriptblock]$CounterProbe = $null,
        [AllowNull()][scriptblock]$CimProbe = $null
    )
    if ($Cores -lt 1) {
        throw "Invalid logical processor count: $Cores."
    }
    if ($MaxCpuCount -lt 0 -or $MaxCpuCount -gt $Cores) {
        throw "Invalid -MaxCpuCount ${MaxCpuCount}: expected 0 (automatic memory-based sizing) or an integer between 1 and $Cores."
    }

    $mode = 'auto'
    $total = 0
    if ($null -ne $JobsEnv -and $JobsEnv.Length -gt 0) {
        $parsedEnv = 0
        $ok = [int]::TryParse($JobsEnv.Trim(), [System.Globalization.NumberStyles]::AllowLeadingSign, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$parsedEnv)
        if (-not $ok -or $parsedEnv -lt 1 -or $parsedEnv -gt $Cores) {
            throw "Invalid RECTO_BUILD_JOBS='$JobsEnv': expected an integer between 1 and $Cores (logical processors on this machine)."
        }
        $total = $parsedEnv
        $mode = 'env'
    } elseif ($MaxCpuCount -gt 0) {
        $total = $MaxCpuCount
        $mode = 'cli'
    }

    $probeParams = @{}
    if ($null -ne $CounterProbe) { $probeParams['CounterProbe'] = $CounterProbe }
    if ($null -ne $CimProbe) { $probeParams['CimProbe'] = $CimProbe }

    $freeMb = $null
    if ($mode -eq 'auto') {
        if ($null -ne $AvailableMb) {
            $parsedAvail = 0.0
            if (-not (Test-PositiveFiniteNumber -Value $AvailableMb -Parsed ([ref]$parsedAvail))) {
                throw "Invalid AvailableMb '$AvailableMb': expected a positive finite number of megabytes."
            }
            $freeMb = [int][Math]::Floor($parsedAvail)
        } else {
            $freeMb = Read-AvailableMemoryMb @probeParams
        }
        $fit = [int][Math]::Floor(($freeMb - $script:RectoMemoryReserveMb) / [double]$script:RectoPerCompilerMb)
        if ($fit -lt 1) {
            $freeText = $freeMb.ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture)
            $minText = $script:RectoMinAutoMemoryMb.ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture)
            throw "Insufficient free memory to start build safely: free $freeText MB is below the $minText MB minimum (2,048 MB reserve + 1,536 MB for 1 compiler). Close memory-heavy applications and retry, or set RECTO_BUILD_JOBS=1 (or pass -MaxCpuCount 1) to force a single-compiler build."
        }
        $total = [Math]::Min($Cores, $fit)
    } else {
        if ($null -ne $AvailableMb) {
            $parsedAvail = 0.0
            if (Test-PositiveFiniteNumber -Value $AvailableMb -Parsed ([ref]$parsedAvail)) {
                $freeMb = [int][Math]::Floor($parsedAvail)
            }
        } else {
            try {
                $freeMb = Read-AvailableMemoryMb @probeParams
            } catch {
                $freeMb = $null
            }
        }
    }

    # VC Microsoft.Cl.Common.props maps CL_MPCount directly to per-project ProcessorNumber (/MP<CL_MPCount>),
    # independent of MSBuild /m:<projects>, so concurrent compilers across nodes can reach projects * CL_MPCount.
    $projects = if ($total -lt 4) {
        1
    } elseif ($total -lt 6) {
        2
    } else {
        3
    }
    $projects = [Math]::Min($Cores, $projects)
    $clMpCount = [Math]::Max(1, [int][Math]::Floor($total / $projects))

    $freeLabel = if ($null -ne $freeMb) {
        $freeMb.ToString('N0', [System.Globalization.CultureInfo]::InvariantCulture)
    } else {
        'unknown'
    }
    $suffix = switch ($mode) {
        'env' { " (RECTO_BUILD_JOBS=$total)" }
        'cli' { " (-MaxCpuCount $total)" }
        default { '' }
    }
    $header = "free $freeLabel MB, $Cores cores: /m:$projects, CL_MPCount=$clMpCount$suffix"

    return [pscustomobject]@{
        FreeMb = $freeMb
        Cores = $Cores
        TotalCompilers = $total
        Projects = $projects
        ClMpCount = $clMpCount
        Mode = $mode
        Header = $header
    }
}
