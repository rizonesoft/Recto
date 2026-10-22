# ASan runtime staging for build/build.ps1, kept apart so tests/scripts/test_build_ps1.py can drive it with a fake
# dumpbin. Since VS 2022 17.7 the toolset picks the ASan runtime DLL (one clang_rt.asan_dynamic for Debug and
# Release), so the DLLs staged are the ones each executable imports, as dumpbin reports them.

function Copy-AsanRuntime {
    param(
        [Parameter(Mandatory)][string]$ExePath,
        [Parameter(Mandatory)][string]$AsanBinDir,
        [Parameter(Mandatory)][string]$Dumpbin
    )
    $imports = @(& $Dumpbin /nologo /dependents $ExePath | ForEach-Object { "$_".Trim() } |
        Where-Object { $_ -match '^clang_rt\.asan[\w.-]*\.dll$' })
    if ($LASTEXITCODE -ne 0 -or $imports.Count -eq 0) {
        throw "$(Split-Path $ExePath -Leaf) imports no ASan runtime: is it built with /fsanitize=address?"
    }
    foreach ($rtDllName in $imports) {
        $rtDllPath = Join-Path $AsanBinDir $rtDllName
        if (-not (Test-Path $rtDllPath)) { throw "ASan runtime DLL not found: $rtDllPath" }
        Copy-Item -Path $rtDllPath -Destination (Join-Path (Split-Path $ExePath -Parent) $rtDllName) -Force
    }
    return $imports
}
