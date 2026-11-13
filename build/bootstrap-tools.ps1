param(
    [string]$LockFile = '',
    [string]$ToolsDir = '',
    [double]$DownloadTimeoutSec = 300,
    [switch]$NoRun
)
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.Net.Http

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Get-StringSha256([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text))
        return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Get-FileSha256([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $bytes = $sha.ComputeHash($stream)
        return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

function Save-FileAtomically([string]$TargetPath, [string]$Content) {
    $parent = Split-Path -Parent $TargetPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $tmp = Join-Path $parent (".tmp-" + [System.IO.Path]::GetFileName($TargetPath) + "-" + [guid]::NewGuid().ToString('N'))
    try {
        [System.IO.File]::WriteAllText($tmp, $Content, $utf8NoBom)
        if (Test-Path -LiteralPath $TargetPath -PathType Leaf) {
            [System.IO.File]::Replace($tmp, $TargetPath, [NullString]::Value, $true)
        } else {
            try {
                [System.IO.File]::Move($tmp, $TargetPath)
            } catch [System.IO.IOException] {
                if (Test-Path -LiteralPath $TargetPath -PathType Leaf) {
                    [System.IO.File]::Replace($tmp, $TargetPath, [NullString]::Value, $true)
                } else {
                    throw
                }
            }
        }
    } finally {
        if (Test-Path -LiteralPath $tmp) {
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        }
    }
}

function Download-Archive([string]$Url, [string]$Destination, [double]$TimeoutSec = 300, [System.Net.Http.HttpMessageHandler]$Handler = $null) {
    if ($TimeoutSec -le 0) {
        throw "DownloadTimeoutSec must be positive (got $TimeoutSec)."
    }
    $uri = [System.Uri]$Url
    if ($uri.Scheme -eq 'file') {
        if (-not (Test-Path -LiteralPath $uri.LocalPath)) {
            throw "Local archive not found: $($uri.LocalPath)"
        }
        [System.IO.File]::Copy($uri.LocalPath, $Destination, $true)
        return
    }
    if ($uri.Scheme -ne 'https') {
        throw "Refusing non-HTTPS tool URL: $Url"
    }
    try {
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor [System.Net.SecurityProtocolType]::Tls12
    } catch {}
    $ownsHandler = $false
    if ($null -eq $Handler) {
        $httpHandler = [System.Net.Http.HttpClientHandler]::new()
        $httpHandler.AllowAutoRedirect = $true
        $Handler = $httpHandler
        $ownsHandler = $true
    }
    $client = [System.Net.Http.HttpClient]::new($Handler, $ownsHandler)
    $client.Timeout = [System.Threading.Timeout]::InfiniteTimeSpan
    $client.DefaultRequestHeaders.UserAgent.ParseAdd('recto-bootstrap-tools/1.0')
    $deadlineMs = [long][Math]::Max(1.0, [Math]::Ceiling($TimeoutSec * 1000.0))
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $cts = [System.Threading.CancellationTokenSource]::new([TimeSpan]::FromMilliseconds($deadlineMs))
    $completed = $false
    try {
        try {
            $reqWaitMs = [int][Math]::Min([double][int]::MaxValue, [double][Math]::Max(1L, $deadlineMs - $stopwatch.ElapsedMilliseconds))
            $reqTask = $client.GetAsync($uri, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead, $cts.Token)
            if (-not $reqTask.Wait($reqWaitMs)) {
                $cts.Cancel()
                throw "Download timed out after ${TimeoutSec}s fetching $Url"
            }
            $response = $reqTask.GetAwaiter().GetResult()
            try {
                if (-not $response.IsSuccessStatusCode) {
                    throw "HTTP $([int]$response.StatusCode) $($response.ReasonPhrase) fetching $Url"
                }
                $inStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
                $outStream = [System.IO.File]::Create($Destination)
                try {
                    $bodyWaitMs = [int][Math]::Min([double][int]::MaxValue, [double][Math]::Max(1L, $deadlineMs - $stopwatch.ElapsedMilliseconds))
                    $copyTask = $inStream.CopyToAsync($outStream, 81920, $cts.Token)
                    if (-not $copyTask.Wait($bodyWaitMs)) {
                        $cts.Cancel()
                        try { $response.Dispose() } catch {}
                        throw "Download timed out after ${TimeoutSec}s fetching $Url"
                    }
                    $null = $copyTask.GetAwaiter().GetResult()
                    $outStream.Flush()
                    $completed = $true
                } finally {
                    $outStream.Dispose()
                    $inStream.Dispose()
                }
            } finally {
                $response.Dispose()
            }
        } catch {
            if ($cts.IsCancellationRequested) {
                throw "Download timed out after ${TimeoutSec}s fetching $Url"
            }
            throw
        }
    } finally {
        if (-not $completed -and (Test-Path -LiteralPath $Destination)) {
            Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
        }
        $cts.Dispose()
        $client.Dispose()
    }
}

function Update-DirectoryJunction([string]$LinkPath, [string]$TargetPath) {
    $targetFull = [System.IO.Path]::GetFullPath($TargetPath)
    if (Test-Path -LiteralPath $LinkPath) {
        $item = Get-Item -LiteralPath $LinkPath -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            $existingTarget = @($item.Target | Select-Object -First 1)[0]
            if ($existingTarget) {
                $cleanTarget = ([string]$existingTarget) -replace '^\\\?\\', ''
                if ([string]::Equals([System.IO.Path]::GetFullPath($cleanTarget).TrimEnd('\', '/'), $targetFull.TrimEnd('\', '/'), [System.StringComparison]::OrdinalIgnoreCase)) {
                    return
                }
            }
            [System.IO.Directory]::Delete($LinkPath)
        } else {
            Remove-Item -LiteralPath $LinkPath -Recurse -Force
        }
    }
    New-Item -ItemType Junction -Path $LinkPath -Target $targetFull | Out-Null
}

function Extract-ZipSafely([string]$ZipPath, [string]$DestinationDir) {
    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    $destFull = [System.IO.Path]::GetFullPath($DestinationDir).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($entry in $archive.Entries) {
            $rel = $entry.FullName.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
            if (-not $rel) { continue }
            $target = [System.IO.Path]::GetFullPath((Join-Path $DestinationDir $rel))
            if (-not $target.StartsWith($destFull, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing zip entry outside destination directory: $($entry.FullName)"
            }
            if ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\')) {
                New-Item -ItemType Directory -Force -Path $target | Out-Null
            } else {
                $entryParent = Split-Path -Parent $target
                if ($entryParent) {
                    New-Item -ItemType Directory -Force -Path $entryParent | Out-Null
                }
                [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
            }
        }
    } finally {
        $archive.Dispose()
    }
}

$siteCustomizeSource = @'
"""Restore standard sys.path behavior for Recto's repo-scoped embeddable CPython while keeping ._pth registry isolation."""
import os
import sys


def _parse_cli(orig_argv):
    ignore_env = False
    safe_path = False
    mode = None
    script_arg = None
    args = list(orig_argv[1:])
    i = 0
    while i < len(args):
        tok = args[i]
        if tok == "--":
            if i + 1 < len(args):
                mode = "script"
                script_arg = args[i + 1]
            break
        if tok == "-" or not tok.startswith("-"):
            mode = "stdin" if tok == "-" else "script"
            script_arg = None if tok == "-" else tok
            break
        if tok in ("-c", "-m"):
            mode = tok
            break
        if tok in ("-W", "-X", "--check-hash-based-pycs"):
            i += 2
            continue
        if tok.startswith(("-W", "-X", "--check-hash-based-pycs=")):
            i += 1
            continue
        if tok.startswith("--"):
            i += 1
            continue
        j = 1
        stopped = False
        while j < len(tok):
            ch = tok[j]
            if ch == "I":
                ignore_env = True
                safe_path = True
            elif ch == "E":
                ignore_env = True
            elif ch == "P":
                safe_path = True
            elif ch in ("c", "m"):
                mode = "-" + ch
                stopped = True
                break
            elif ch in ("W", "X"):
                if j + 1 == len(tok):
                    i += 1
                break
            j += 1
        if stopped:
            break
        i += 1
    if not ignore_env and os.environ.get("PYTHONSAFEPATH"):
        safe_path = True
    return ignore_env, safe_path, mode, script_arg


def _configure_sys_path():
    ignore_env, safe_path, mode, script_arg = _parse_cli(getattr(sys, "orig_argv", sys.argv))
    exe_dir = os.path.dirname(os.path.abspath(sys.executable))
    exe_norm = os.path.normcase(exe_dir)
    kept = []
    for entry in sys.path:
        if not entry:
            continue
        entry_abs = os.path.abspath(entry)
        if os.path.normcase(entry_abs) == exe_norm and not entry.lower().endswith(".zip"):
            continue
        kept.append(entry_abs)
    prefix = []
    if not safe_path:
        if mode == "script" and script_arg:
            prefix.append(os.path.dirname(os.path.abspath(script_arg)))
        else:
            prefix.append(os.getcwd())
    if not ignore_env:
        raw_pp = os.environ.get("PYTHONPATH", "")
        if raw_pp:
            for item in raw_pp.split(os.pathsep):
                if item:
                    prefix.append(os.path.abspath(item))
    sys.path[:] = prefix + kept + [exe_dir]


_configure_sys_path()
'@.Replace("`r`n", "`n")

function Configure-EmbeddableCpython([string]$StageDir) {
    $pthFiles = @(Get-ChildItem -LiteralPath $StageDir -Filter 'python*._pth' -File)
    if ($pthFiles.Count -ne 1) {
        throw "Expected exactly one python*._pth file in staged CPython directory '$StageDir', found $($pthFiles.Count)."
    }
    $pthPath = $pthFiles[0].FullName
    $lines = [System.IO.File]::ReadAllLines($pthPath)
    $updated = New-Object System.Collections.Generic.List[string]
    $hasImportSite = $false
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^#\s*import\s+site\s*$') {
            $updated.Add('import site')
            $hasImportSite = $true
        } elseif ($trimmed -eq 'import site') {
            $updated.Add('import site')
            $hasImportSite = $true
        } else {
            $updated.Add($line)
        }
    }
    if (-not $hasImportSite) {
        $updated.Add('import site')
    }
    [System.IO.File]::WriteAllText($pthPath, (($updated -join "`n") + "`n"), $utf8NoBom)
    [System.IO.File]::WriteAllText((Join-Path $StageDir 'sitecustomize.py'), $siteCustomizeSource, $utf8NoBom)
}

function Test-ToolReady([string]$ToolName, [string]$FinalDir, [string]$ExecutableRel, [string]$ExpectedHash) {
    if (-not (Test-Path -LiteralPath $FinalDir)) { return $false }
    $exePath = Join-Path $FinalDir ($ExecutableRel.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $exePath)) { return $false }
    $markerPath = Join-Path $FinalDir '.verified-sha256'
    if (-not (Test-Path -LiteralPath $markerPath)) { return $false }
    $recorded = ([System.IO.File]::ReadAllText($markerPath)).Trim().ToLowerInvariant()
    if ($recorded -ne $ExpectedHash) { return $false }
    if ($ToolName -eq 'cpython') {
        $sitePath = Join-Path $FinalDir 'sitecustomize.py'
        if (-not (Test-Path -LiteralPath $sitePath)) { return $false }
        if ([System.IO.File]::ReadAllText($sitePath) -ne $siteCustomizeSource) { return $false }
        $pth = @(Get-ChildItem -LiteralPath $FinalDir -Filter 'python*._pth' -File)
        if ($pth.Count -ne 1) { return $false }
        $pthText = [System.IO.File]::ReadAllText($pth[0].FullName)
        if ($pthText -notmatch '(?m)^\s*import site\s*$') { return $false }
    }
    return $true
}

if ($NoRun) {
    $global:LASTEXITCODE = 0
    return
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $LockFile) {
    $LockFile = Join-Path $PSScriptRoot 'tools.lock.json'
}
if (-not $ToolsDir) {
    if ($env:RECTO_TOOLS_DIR -and $env:RECTO_TOOLS_DIR.Trim()) {
        $ToolsDir = $env:RECTO_TOOLS_DIR.Trim()
    } else {
        $ToolsDir = Join-Path $repoRoot '.tools'
    }
}
$LockFile = [System.IO.Path]::GetFullPath($LockFile)
$ToolsDir = [System.IO.Path]::GetFullPath($ToolsDir)

if (-not (Test-Path -LiteralPath $LockFile)) {
    throw "Tools lock file not found: $LockFile"
}

$mutexName = 'Local\RectoBootstrapTools-' + (Get-StringSha256 ($ToolsDir.ToLowerInvariant())).Substring(0, 24)
$mutex = [System.Threading.Mutex]::new($false, $mutexName)
$acquired = $false
try {
    try {
        $acquired = $mutex.WaitOne([TimeSpan]::FromMinutes(15))
    } catch [System.Threading.AbandonedMutexException] {
        $acquired = $true
    }
    if (-not $acquired) {
        throw "Timed out waiting for tools bootstrap mutex ($mutexName)."
    }

    $rawLock = [System.IO.File]::ReadAllText($LockFile)
    $lockObj = $rawLock | ConvertFrom-Json
    if ($null -eq $lockObj) {
        throw "Tools lock file is empty or invalid JSON: $LockFile"
    }
    $props = @($lockObj.PSObject.Properties)
    $requiredTools = @('ninja', 'cmake', 'sccache', 'cpython')
    foreach ($req in $requiredTools) {
        if (-not ($props.Name -contains $req)) {
            throw "Tools lock file '$LockFile' is missing required entry '$req'."
        }
    }

    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null

    foreach ($prop in $props) {
        $tool = [string]$prop.Name
        $entry = $prop.Value
        $version = [string]$entry.version
        $url = [string]$entry.url
        $expectedHash = ([string]$entry.sha256).Trim().ToLowerInvariant()
        $exeRel = [string]$entry.executable

        if (-not $version -or $version -match '[\\/]' -or $version -eq '.' -or $version -eq '..') {
            throw "Invalid version '$version' for tool '$tool' in $LockFile"
        }
        if ($expectedHash -notmatch '^[0-9a-f]{64}$') {
            throw "Invalid SHA-256 '$expectedHash' for tool '$tool' in $LockFile (expected 64 hex digits)"
        }
        if (-not $exeRel -or $exeRel.Contains('..') -or [System.IO.Path]::IsPathRooted($exeRel)) {
            throw "Invalid executable path '$exeRel' for tool '$tool' in $LockFile"
        }

        $toolRoot = Join-Path $ToolsDir $tool
        $finalDir = Join-Path $toolRoot $version
        if (Test-ToolReady -ToolName $tool -FinalDir $finalDir -ExecutableRel $exeRel -ExpectedHash $expectedHash) {
            if ($tool -eq 'cpython') {
                Update-DirectoryJunction -LinkPath (Join-Path $toolRoot 'current') -TargetPath $finalDir
            }
            if (Test-Path -LiteralPath $toolRoot) {
                Get-ChildItem -LiteralPath $toolRoot -Directory -Force | Where-Object { $_.Name -ne $version -and $_.Name -ne 'current' } | ForEach-Object {
                    if (($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                        [System.IO.Directory]::Delete($_.FullName)
                    } else {
                        Remove-Item -LiteralPath $_.FullName -Recurse -Force
                    }
                }
            }
            continue
        }

        Write-Host "=== Bootstrapping $tool $version into $ToolsDir"
        $id = [guid]::NewGuid().ToString('N')
        $tempZip = Join-Path $ToolsDir ".download-$tool-$id.zip"
        $stageParent = Join-Path $ToolsDir ".staging-$tool-$id"
        $stageDir = Join-Path $stageParent $version
        try {
            Download-Archive -Url $url -Destination $tempZip -TimeoutSec $DownloadTimeoutSec
            $actualHash = Get-FileSha256 -Path $tempZip
            if ($actualHash -ne $expectedHash) {
                throw "SHA-256 mismatch for $tool ($version): expected $expectedHash, actual $actualHash"
            }

            Extract-ZipSafely -ZipPath $tempZip -DestinationDir $stageDir
            Remove-Item -LiteralPath $tempZip -Force -ErrorAction SilentlyContinue

            if ($tool -eq 'cpython') {
                Configure-EmbeddableCpython -StageDir $stageDir
            }

            $stagedExe = Join-Path $stageDir ($exeRel.Replace('/', '\'))
            if (-not (Test-Path -LiteralPath $stagedExe)) {
                throw "Extracted archive for $tool ($version) did not contain '$exeRel'"
            }
            [System.IO.File]::WriteAllText((Join-Path $stageDir '.verified-sha256'), "$expectedHash`n", $utf8NoBom)

            $toolRootExisted = Test-Path -LiteralPath $toolRoot
            New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
            try {
                if (Test-Path -LiteralPath $finalDir) {
                    Remove-Item -LiteralPath $finalDir -Recurse -Force
                }
                [System.IO.Directory]::Move($stageDir, $finalDir)
            } catch {
                if (-not $toolRootExisted -and (Test-Path -LiteralPath $toolRoot) -and @(Get-ChildItem -LiteralPath $toolRoot -Force).Count -eq 0) {
                    Remove-Item -LiteralPath $toolRoot -Force -ErrorAction SilentlyContinue
                }
                throw
            }

            if ($tool -eq 'cpython') {
                Update-DirectoryJunction -LinkPath (Join-Path $toolRoot 'current') -TargetPath $finalDir
            }

            Get-ChildItem -LiteralPath $toolRoot -Directory -Force | Where-Object { $_.Name -ne $version -and $_.Name -ne 'current' } | ForEach-Object {
                if (($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                    [System.IO.Directory]::Delete($_.FullName)
                } else {
                    Remove-Item -LiteralPath $_.FullName -Recurse -Force
                }
            }
        } finally {
            if (Test-Path -LiteralPath $tempZip) {
                Remove-Item -LiteralPath $tempZip -Force -ErrorAction SilentlyContinue
            }
            if (Test-Path -LiteralPath $stageParent) {
                Remove-Item -LiteralPath $stageParent -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    $cpythonEntry = $lockObj.cpython
    $cpythonExe = Join-Path (Join-Path (Join-Path $ToolsDir 'cpython') ([string]$cpythonEntry.version)) (([string]$cpythonEntry.executable).Replace('/', '\'))
    Save-FileAtomically -TargetPath (Join-Path $ToolsDir 'cpython-exe.txt') -Content $cpythonExe
    Save-FileAtomically -TargetPath (Join-Path $ToolsDir 'tools.lock.verified') -Content $rawLock
    $global:LASTEXITCODE = 0
} finally {
    if ($acquired) {
        $mutex.ReleaseMutex()
    }
    $mutex.Dispose()
}
