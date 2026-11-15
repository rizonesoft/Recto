# MSBuild + sccache compiler-cache helper (D00 T04 s7).
# Bootstraps the native FileTracker-aware wrapper build/sccache-cl.cpp uncached
# via the discovered VS 2026 Hostx64\x64 compiler, isolates/verifies a live sccache
# daemon configured for build/sccache/ and 10G without disrupting other repositories,
# and honors -NoSccache / RECTO_USE_SCCACHE=0.

$script:SccacheUtf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Resolve-RectoUseSccache {
    param(
        [switch]$NoSccache
    )
    $rawEnv = $env:RECTO_USE_SCCACHE
    $hasEnv = -not [string]::IsNullOrWhiteSpace($rawEnv)
    $envBool = $true
    if ($hasEnv) {
        switch ($rawEnv.Trim().ToLowerInvariant()) {
            { $_ -in @('1', 'true', 'yes', 'on') } { $envBool = $true }
            { $_ -in @('0', 'false', 'no', 'off') } { $envBool = $false }
            default {
                throw "Invalid RECTO_USE_SCCACHE='$rawEnv'; expected 0, 1, true, or false."
            }
        }
    }
    if ($NoSccache) {
        return $false
    }
    if ($hasEnv) {
        return $envBool
    }
    return $true
}

function ConvertTo-RectoSccacheBytes {
    param(
        [Parameter(Mandatory = $true)][string]$SizeSpec
    )
    $trimmed = $SizeSpec.Trim()
    if ($trimmed -notmatch '^(?<num>[0-9]+)(?<unit>|K|M|G|T|KiB|MiB|GiB|TiB)$') {
        throw "Invalid SCCACHE_CACHE_SIZE='$SizeSpec'; expected positive integer bytes or suffix K, M, G, T, KiB, MiB, GiB, or TiB (for example '10G')."
    }
    [int64]$value = 0
    if (-not [int64]::TryParse($Matches['num'], [ref]$value) -or $value -le 0) {
        throw "SCCACHE_CACHE_SIZE must be positive (got '$SizeSpec')."
    }
    [int64]$multiplier = switch ($Matches['unit']) {
        '' { 1L }
        'K' { 1024L }
        'KiB' { 1024L }
        'M' { 1048576L }
        'MiB' { 1048576L }
        'G' { 1073741824L }
        'GiB' { 1073741824L }
        'T' { 1099511627776L }
        'TiB' { 1099511627776L }
        default { throw "Unsupported SCCACHE_CACHE_SIZE unit '$($Matches['unit'])'." }
    }
    if ($value -gt ([int64]::MaxValue / $multiplier)) {
        throw "SCCACHE_CACHE_SIZE overflows 64-bit byte count (got '$SizeSpec')."
    }
    return ($value * $multiplier)
}

function Get-RectoCanonicalPath {
    param(
        [Parameter(Mandatory = $true)][string]$PathText
    )
    $probe = [System.IO.Path]::GetFullPath($PathText)
    $tail = [System.Collections.Generic.List[string]]::new()
    while (-not (Test-Path -LiteralPath $probe) -and $probe.Length -gt 0) {
        $parent = Split-Path -Parent $probe
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $probe) {
            break
        }
        $tail.Insert(0, (Split-Path -Leaf $probe))
        $probe = $parent
    }
    if (Test-Path -LiteralPath $probe) {
        $probe = (Get-Item -LiteralPath $probe -Force).FullName
    }
    foreach ($seg in $tail) {
        $probe = Join-Path $probe $seg
    }
    return $probe.TrimEnd('\', '/').ToLowerInvariant()
}

function Get-RectoSccacheLocationPath {
    param(
        [string]$RawLocation
    )
    if ([string]::IsNullOrWhiteSpace($RawLocation)) {
        return ''
    }
    $trimmed = $RawLocation.Trim()
    if ($trimmed -match '^Local disk:\s*"(?<inner>.*)"$') {
        $unescaped = $Matches['inner'] -replace '\\\\', '\'
        try {
            return (Get-RectoCanonicalPath -PathText $unescaped)
        } catch {
            return $unescaped.TrimEnd('\', '/').ToLowerInvariant()
        }
    }
    return $trimmed
}

function Resolve-RectoSccacheExe {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )
    $toolsDir = if (-not [string]::IsNullOrWhiteSpace($env:RECTO_TOOLS_DIR)) {
        [System.IO.Path]::GetFullPath($env:RECTO_TOOLS_DIR)
    } else {
        Join-Path $RepoRoot '.tools'
    }
    $lockPath = Join-Path $RepoRoot 'build\tools.lock.json'
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
        throw "Tool lock file not found: $lockPath"
    }
    $lockObj = Get-Content -LiteralPath $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($null -eq $lockObj.sccache -or [string]::IsNullOrWhiteSpace([string]$lockObj.sccache.version) -or [string]::IsNullOrWhiteSpace([string]$lockObj.sccache.executable)) {
        throw "Missing sccache entry in $lockPath"
    }
    $version = [string]$lockObj.sccache.version
    $exeRel = ([string]$lockObj.sccache.executable).Replace('/', '\')
    $sccacheExe = Join-Path (Join-Path (Join-Path $toolsDir 'sccache') $version) $exeRel
    if (-not (Test-Path -LiteralPath $sccacheExe -PathType Leaf)) {
        throw "Pinned sccache binary not found at '$sccacheExe'; run build\bootstrap-tools.ps1 first."
    }
    return [System.IO.Path]::GetFullPath($sccacheExe)
}

function Get-RectoDeterministicSccachePort {
    param(
        [Parameter(Mandatory = $true)][string]$CanonicalCacheDir
    )
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($CanonicalCacheDir))
        [uint32]$val = [BitConverter]::ToUInt32($hash, 0)
        return [int](42000 + ($val % 20000))
    } finally {
        $sha.Dispose()
    }
}

function Test-RectoLocalTcpPortListening {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutMs = 200
    )
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $async = $client.BeginConnect([System.Net.IPAddress]::Loopback, $Port, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne($TimeoutMs, $false)) {
            return $false
        }
        $client.EndConnect($async)
        return $true
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

function Test-RectoLocalTcpPortBindable {
    param(
        [Parameter(Mandatory = $true)][int]$Port
    )
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
    try {
        # Detect Windows Hyper-V/WinNAT excluded ranges (WSAEACCES) and already-bound ports before starting sccache.
        $listener.ExclusiveAddressUse = $true
        $listener.Start()
        return $true
    } catch [System.Net.Sockets.SocketException] {
        return $false
    } finally {
        $listener.Stop()
    }
}

function Get-RectoSccacheProbeTimeoutMs {
    param(
        [int]$DefaultMs = 3000
    )
    $raw = $env:RECTO_SCCACHE_PROBE_TIMEOUT_MS
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $DefaultMs
    }
    [int]$parsed = 0
    if (-not [int]::TryParse($raw.Trim(), [ref]$parsed) -or $parsed -lt 100 -or $parsed -gt 120000) {
        throw "Invalid RECTO_SCCACHE_PROBE_TIMEOUT_MS='$raw'; expected integer milliseconds in 100..120000."
    }
    return $parsed
}

function Stop-RectoProcessTree {
    param(
        [System.Diagnostics.Process]$Process
    )
    if ($null -eq $Process) {
        return
    }
    try {
        if ($Process.HasExited) {
            return
        }
        $taskkill = Join-Path $env:SystemRoot 'System32\taskkill.exe'
        if (Test-Path -LiteralPath $taskkill -PathType Leaf) {
            $kPsi = [System.Diagnostics.ProcessStartInfo]::new()
            $kPsi.FileName = $taskkill
            $kPsi.Arguments = "/PID $($Process.Id) /T /F"
            $kPsi.WorkingDirectory = $env:SystemRoot
            $kPsi.UseShellExecute = $false
            $kPsi.CreateNoWindow = $true
            $kPsi.RedirectStandardOutput = $true
            $kPsi.RedirectStandardError = $true
            $kProc = [System.Diagnostics.Process]::Start($kPsi)
            if ($null -ne $kProc) {
                try {
                    [void]$kProc.WaitForExit(5000)
                } finally {
                    $kProc.Dispose()
                }
            }
        }
        if (-not $Process.HasExited) {
            $Process.Kill()
            [void]$Process.WaitForExit(2000)
        }
    } catch [System.InvalidOperationException] {
        # Process exited concurrently while tearing down its tree.
    } catch [System.ComponentModel.Win32Exception] {
        # Access denied only when the target exited before OpenProcess/TerminateProcess completed.
        if (-not $Process.HasExited) {
            throw
        }
    }
}

function Invoke-RectoProcessBounded {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][int]$TimeoutMs,
        [hashtable]$Environment = @{}
    )
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    $psi.Arguments = $Arguments
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = $script:SccacheUtf8NoBom
    $psi.StandardErrorEncoding = $script:SccacheUtf8NoBom
    foreach ($entry in $Environment.GetEnumerator()) {
        $psi.EnvironmentVariables[[string]$entry.Key] = [string]$entry.Value
    }

    $proc = [System.Diagnostics.Process]::Start($psi)
    if ($null -eq $proc) {
        throw "Failed to launch process '$FilePath'."
    }
    try {
        $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
        $stderrTask = $proc.StandardError.ReadToEndAsync()
        if (-not $proc.WaitForExit($TimeoutMs)) {
            Stop-RectoProcessTree -Process $proc
            $outText = if ($stdoutTask.Wait(1000)) { $stdoutTask.GetAwaiter().GetResult() } else { '' }
            $errText = if ($stderrTask.Wait(1000)) { $stderrTask.GetAwaiter().GetResult() } else { '' }
            return [pscustomobject]@{
                TimedOut = $true
                ExitCode = -1
                StdOut = $outText
                StdErr = $errText
            }
        }
        $proc.WaitForExit()
        return [pscustomobject]@{
            TimedOut = $false
            ExitCode = [int]$proc.ExitCode
            StdOut = $stdoutTask.GetAwaiter().GetResult()
            StdErr = $stderrTask.GetAwaiter().GetResult()
        }
    } finally {
        $proc.Dispose()
    }
}

function Invoke-RectoSccacheCommandBounded {
    param(
        [Parameter(Mandatory = $true)][string]$SccacheExe,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][int]$Port,
        [int]$TimeoutMs = 0
    )
    $effectiveTimeout = if ($TimeoutMs -gt 0) { $TimeoutMs } else { Get-RectoSccacheProbeTimeoutMs }
    return (Invoke-RectoProcessBounded `
        -FilePath $SccacheExe `
        -Arguments ($Arguments -join ' ') `
        -WorkingDirectory $env:SystemRoot `
        -TimeoutMs $effectiveTimeout `
        -Environment @{ SCCACHE_SERVER_PORT = [string]$Port })
}

function Get-RectoSccacheServerInfo {
    param(
        [Parameter(Mandatory = $true)][string]$SccacheExe,
        [Parameter(Mandatory = $true)][int]$Port
    )
    if (-not (Test-RectoLocalTcpPortListening -Port $Port)) {
        return $null
    }
    $probe = Invoke-RectoSccacheCommandBounded -SccacheExe $SccacheExe -Arguments @('--show-stats', '--stats-format', 'json') -Port $Port
    if ($probe.TimedOut) {
        return [pscustomobject]@{
            Port = $Port
            IsSccache = $false
            CachePath = ''
            RawCacheLocation = '<non-sccache-listener timeout>'
            MaxCacheSize = -1L
            Stats = $null
        }
    }
    if ($probe.ExitCode -ne 0) {
        return [pscustomobject]@{
            Port = $Port
            IsSccache = $false
            CachePath = ''
            RawCacheLocation = "<non-sccache-listener exit=$($probe.ExitCode)>"
            MaxCacheSize = -1L
            Stats = $null
        }
    }
    $joined = ([string]$probe.StdOut).Trim()
    if ([string]::IsNullOrWhiteSpace($joined)) {
        return [pscustomobject]@{
            Port = $Port
            IsSccache = $false
            CachePath = ''
            RawCacheLocation = '<empty-listener-response>'
            MaxCacheSize = -1L
            Stats = $null
        }
    }
    try {
        $parsed = $joined | ConvertFrom-Json -ErrorAction Stop
    } catch {
        return [pscustomobject]@{
            Port = $Port
            IsSccache = $false
            CachePath = ''
            RawCacheLocation = '<invalid-json-listener>'
            MaxCacheSize = -1L
            Stats = $null
        }
    }
    return [pscustomobject]@{
        Port = $Port
        IsSccache = $true
        CachePath = Get-RectoSccacheLocationPath -RawLocation ([string]$parsed.cache_location)
        RawCacheLocation = [string]$parsed.cache_location
        MaxCacheSize = if ($null -ne $parsed.max_cache_size) { [int64]$parsed.max_cache_size } else { -1L }
        Stats = $parsed
    }
}

function Stop-RectoSccacheServerAtPort {
    param(
        [Parameter(Mandatory = $true)][string]$SccacheExe,
        [Parameter(Mandatory = $true)][int]$Port
    )
    [void](Invoke-RectoSccacheCommandBounded -SccacheExe $SccacheExe -Arguments @('--stop-server') -Port $Port)
}

function Save-RectoFileAtomically {
    param(
        [Parameter(Mandatory = $true)][string]$TargetPath,
        [Parameter(Mandatory = $true)][string]$Content
    )
    $parent = Split-Path -Parent $TargetPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $tmp = Join-Path $parent ('.tmp-' + [System.IO.Path]::GetFileName($TargetPath) + '-' + [guid]::NewGuid().ToString('N'))
    try {
        [System.IO.File]::WriteAllText($tmp, $Content, $script:SccacheUtf8NoBom)
        if (Test-Path -LiteralPath $TargetPath -PathType Leaf) {
            [System.IO.File]::Replace($tmp, $TargetPath, [NullString]::Value, $true)
        } else {
            [System.IO.File]::Move($tmp, $TargetPath)
        }
    } finally {
        if (Test-Path -LiteralPath $tmp) {
            [System.IO.File]::Delete($tmp)
        }
    }
}

function Build-RectoSccacheWrapper {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$VsPath
    )
    if (-not [string]::IsNullOrWhiteSpace($env:RECTO_SCCACHE_WRAPPER_EXE)) {
        $overrideExe = [System.IO.Path]::GetFullPath($env:RECTO_SCCACHE_WRAPPER_EXE)
        if (-not (Test-Path -LiteralPath $overrideExe -PathType Leaf)) {
            throw "RECTO_SCCACHE_WRAPPER_EXE points to a missing file: $overrideExe"
        }
        return (Get-Item -LiteralPath $overrideExe -Force).FullName
    }

    $cppPath = Join-Path $RepoRoot 'build\sccache-cl.cpp'
    if (-not (Test-Path -LiteralPath $cppPath -PathType Leaf)) {
        throw "sccache wrapper source not found: $cppPath"
    }

    $vcvars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
        throw "Visual Studio Hostx64\x64 environment script not found for wrapper bootstrap: $vcvars"
    }

    $wrapperDir = Join-Path $RepoRoot 'build\sccache-wrapper'
    $wrapperExe = Join-Path $wrapperDir 'sccache-cl.exe'
    $stampPath = Join-Path $wrapperDir 'sccache-cl.sha256'

    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $srcBytes = [System.IO.File]::ReadAllBytes($cppPath)
        $srcHex = [BitConverter]::ToString($sha.ComputeHash($srcBytes)).Replace('-', '').ToLowerInvariant()
        $vsKey = Get-RectoCanonicalPath -PathText $VsPath
        $stampBytes = [System.Text.Encoding]::UTF8.GetBytes("${srcHex}|${vsKey}")
        $expectedStamp = [BitConverter]::ToString($sha.ComputeHash($stampBytes)).Replace('-', '').ToLowerInvariant()
        $lockKeyBytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes((Get-RectoCanonicalPath -PathText $wrapperDir)))
        $mutexName = 'Local\RectoSccacheWrapper-' + [BitConverter]::ToString($lockKeyBytes).Replace('-', '').Substring(0, 16)
    } finally {
        $sha.Dispose()
    }

    New-Item -ItemType Directory -Force -Path $wrapperDir | Out-Null
    $mutex = [System.Threading.Mutex]::new($false, $mutexName)
    $acquired = $false
    try {
        try {
            $acquired = $mutex.WaitOne([TimeSpan]::FromMinutes(5))
        } catch [System.Threading.AbandonedMutexException] {
            $acquired = $true
        }
        if (-not $acquired) {
            throw "Timed out waiting for wrapper build lock '$mutexName'."
        }

        if ((Test-Path -LiteralPath $wrapperExe -PathType Leaf) -and (Test-Path -LiteralPath $stampPath -PathType Leaf)) {
            $recorded = [System.IO.File]::ReadAllText($stampPath, [System.Text.Encoding]::UTF8)
            if ($recorded.Trim() -eq $expectedStamp) {
                return (Get-Item -LiteralPath $wrapperExe -Force).FullName
            }
        }

        Write-Host "=== Bootstrapping sccache-cl wrapper (uncached Hostx64\x64)"
        $stageDir = Join-Path $wrapperDir ('.stage-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $stageDir | Out-Null
        try {
            $stageExe = Join-Path $stageDir 'sccache-cl.exe'
            $stageObj = Join-Path $stageDir 'sccache-cl.obj'
            $cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
            $bootArgs = '/d /s /c "call "%RECTO_BOOT_VCVARS%" && cl.exe /nologo /std:c++23preview /EHsc /O2 /MT /W4 /WX /utf-8 /Fo"%RECTO_BOOT_OBJ%" /Fe"%RECTO_BOOT_EXE%" "%RECTO_BOOT_CPP%""'
            $bootRes = Invoke-RectoProcessBounded `
                -FilePath $cmdExe `
                -Arguments $bootArgs `
                -WorkingDirectory $stageDir `
                -TimeoutMs 120000 `
                -Environment @{
                    RECTO_BOOT_VCVARS = $vcvars
                    RECTO_BOOT_OBJ = $stageObj
                    RECTO_BOOT_EXE = $stageExe
                    RECTO_BOOT_CPP = $cppPath
                }
            if ($bootRes.TimedOut -or $bootRes.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $stageExe -PathType Leaf)) {
                $joinedOut = ("$($bootRes.StdOut)`n$($bootRes.StdErr)").Trim()
                $reason = if ($bootRes.TimedOut) { 'timed out after 120000 ms' } else { "exit $($bootRes.ExitCode)" }
                throw "Uncached bootstrap compile of build\sccache-cl.cpp failed ($reason):`n$joinedOut"
            }

            if (Test-Path -LiteralPath $wrapperExe -PathType Leaf) {
                $oldBackup = Join-Path $wrapperDir ('.old-' + [guid]::NewGuid().ToString('N'))
                try {
                    [System.IO.File]::Replace($stageExe, $wrapperExe, $oldBackup, $true)
                } finally {
                    if (Test-Path -LiteralPath $oldBackup) {
                        [System.IO.File]::Delete($oldBackup)
                    }
                }
            } else {
                [System.IO.File]::Move($stageExe, $wrapperExe)
            }
            Save-RectoFileAtomically -TargetPath $stampPath -Content "$expectedStamp`n"
        } finally {
            if (Test-Path -LiteralPath $stageDir) {
                Remove-Item -LiteralPath $stageDir -Recurse -Force
            }
        }
        return (Get-Item -LiteralPath $wrapperExe -Force).FullName
    } finally {
        if ($acquired) {
            $mutex.ReleaseMutex()
        }
        $mutex.Dispose()
    }
}

function Initialize-RectoSccache {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$VsPath
    )
    $fullRoot = [System.IO.Path]::GetFullPath($RepoRoot)
    $sccacheExe = Resolve-RectoSccacheExe -RepoRoot $fullRoot

    $cacheDir = if (-not [string]::IsNullOrWhiteSpace($env:SCCACHE_DIR)) {
        [System.IO.Path]::GetFullPath($env:SCCACHE_DIR)
    } else {
        Join-Path $fullRoot 'build\sccache'
    }
    $canonicalCacheDir = Get-RectoCanonicalPath -PathText $cacheDir
    New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null

    $cacheSize = if (-not [string]::IsNullOrWhiteSpace($env:SCCACHE_CACHE_SIZE)) {
        $env:SCCACHE_CACHE_SIZE.Trim()
    } else {
        '10G'
    }
    [int64]$expectedBytes = ConvertTo-RectoSccacheBytes -SizeSpec $cacheSize

    $wrapperExe = Build-RectoSccacheWrapper -RepoRoot $fullRoot -VsPath $VsPath
    $wrapperDir = Split-Path -Parent $wrapperExe

    $env:RECTO_SCCACHE_EXE = $sccacheExe
    $env:SCCACHE_DIR = $cacheDir
    $env:SCCACHE_CACHE_SIZE = $cacheSize

    if ($env:RECTO_SCCACHE_NO_DAEMON -eq '1') {
        return [pscustomobject]@{
            SccacheExe = $sccacheExe
            WrapperExe = $wrapperExe
            WrapperDir = $wrapperDir
            CacheDir = $cacheDir
            CacheSize = $cacheSize
            ExpectedCacheBytes = $expectedBytes
            ServerPort = $null
            Stats = $null
        }
    }

    $explicitPort = $null
    if (-not [string]::IsNullOrWhiteSpace($env:SCCACHE_SERVER_PORT)) {
        [int]$parsedPort = 0
        if (-not [int]::TryParse($env:SCCACHE_SERVER_PORT.Trim(), [ref]$parsedPort) -or $parsedPort -lt 1024 -or $parsedPort -gt 65535) {
            throw "Invalid SCCACHE_SERVER_PORT='$($env:SCCACHE_SERVER_PORT)'; expected TCP port 1024..65535."
        }
        $explicitPort = $parsedPort
    }

    $selectedPort = $null
    $needsStart = $false

    if ($null -ne $explicitPort) {
        $info = Get-RectoSccacheServerInfo -SccacheExe $sccacheExe -Port $explicitPort
        if ($null -ne $info) {
            if (-not $info.IsSccache -or $info.CachePath -ne $canonicalCacheDir) {
                throw "An external sccache server on explicit SCCACHE_SERVER_PORT=$explicitPort uses '$($info.RawCacheLocation)' instead of '$cacheDir'. Unset SCCACHE_SERVER_PORT or use a dedicated port so other builds are not disrupted."
            }
            if ($info.MaxCacheSize -ne $expectedBytes) {
                Stop-RectoSccacheServerAtPort -SccacheExe $sccacheExe -Port $explicitPort
                $needsStart = $true
            }
        } else {
            $needsStart = $true
        }
        $selectedPort = $explicitPort
    } else {
        $basePort = Get-RectoDeterministicSccachePort -CanonicalCacheDir $canonicalCacheDir
        $candidateOffsets = [System.Collections.Generic.List[int]]::new()
        for ($offset = 0; $offset -lt 16; $offset++) {
            $candidateOffsets.Add($offset)
        }
        # Stride 109 (> 100) escapes contiguous Windows Hyper-V/WinNAT excluded TCP port blocks while staying deterministic.
        for ($step = 1; $step -le 64; $step++) {
            $candidateOffsets.Add(15 + ($step * 109))
        }
        foreach ($offset in $candidateOffsets) {
            $candidatePort = 42000 + (($basePort - 42000 + $offset) % 20000)
            $info = Get-RectoSccacheServerInfo -SccacheExe $sccacheExe -Port $candidatePort
            if ($null -eq $info) {
                if (-not (Test-RectoLocalTcpPortBindable -Port $candidatePort)) {
                    continue
                }
                $selectedPort = $candidatePort
                $needsStart = $true
                break
            }
            if ($info.IsSccache -and $info.CachePath -eq $canonicalCacheDir) {
                if ($info.MaxCacheSize -ne $expectedBytes) {
                    Stop-RectoSccacheServerAtPort -SccacheExe $sccacheExe -Port $candidatePort
                    $needsStart = $true
                }
                $selectedPort = $candidatePort
                break
            }
        }
        if ($null -eq $selectedPort) {
            throw "Could not find a free local TCP port for sccache (ports near $basePort are all bound or unbindable)."
        }
    }

    $env:SCCACHE_SERVER_PORT = [string]$selectedPort
    if ($needsStart) {
        $startRes = Invoke-RectoSccacheCommandBounded -SccacheExe $sccacheExe -Arguments @('--start-server') -Port $selectedPort -TimeoutMs 15000
        if ($startRes.TimedOut -or $startRes.ExitCode -ne 0) {
            # Another caller racing on the same port may already have started it; re-check before failing.
            $raceCheck = Get-RectoSccacheServerInfo -SccacheExe $sccacheExe -Port $selectedPort
            if ($null -eq $raceCheck -or -not $raceCheck.IsSccache) {
                $joinedStart = ("$($startRes.StdOut)`n$($startRes.StdErr)").Trim()
                $startStatus = if ($startRes.TimedOut) { 'timed out after 15000 ms' } else { "exit $($startRes.ExitCode)" }
                throw "Failed to start sccache server on port $selectedPort ($startStatus):`n$joinedStart"
            }
        }
    }

    $verified = Get-RectoSccacheServerInfo -SccacheExe $sccacheExe -Port $selectedPort
    if ($null -eq $verified -or -not $verified.IsSccache) {
        throw "sccache server on 127.0.0.1:$selectedPort did not respond after startup."
    }
    if ($verified.CachePath -ne $canonicalCacheDir) {
        throw "sccache server on port $selectedPort reported cache_location=$($verified.RawCacheLocation), expected '$cacheDir'."
    }
    if ($verified.MaxCacheSize -ne $expectedBytes) {
        throw "sccache server on port $selectedPort reported max_cache_size=$($verified.MaxCacheSize), expected $expectedBytes ($cacheSize)."
    }

    return [pscustomobject]@{
        SccacheExe = $sccacheExe
        WrapperExe = $wrapperExe
        WrapperDir = $wrapperDir
        CacheDir = $cacheDir
        CacheSize = $cacheSize
        ExpectedCacheBytes = $expectedBytes
        ServerPort = $selectedPort
        Stats = $verified.Stats
    }
}

function Get-RectoSccacheCounterSum {
    param(
        $CounterGroup
    )
    [int64]$total = 0
    if ($null -eq $CounterGroup -or $null -eq $CounterGroup.counts) {
        return $total
    }
    foreach ($prop in $CounterGroup.counts.PSObject.Properties) {
        $total += [int64]$prop.Value
    }
    return $total
}

