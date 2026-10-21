# Reference Benchmark Machines

This document records the hardware, operating system, display, storage, and toolchain specifications for Recto's performance baselines, as required by `docs/plans/2026-10-13-recto-foundation.md` sections 8 and 11 (`D00 T01 §5`).

Milestone M2 and M6 performance claims (measured by the `D06 T01 §7` harness and recorded by `D06 T01 §8`) are bound to `Release` builds measured on the primary x64 reference host machine below; no physical `ARM64` hardware is attached today, so the secondary `ARM64` column defines a non-binding cross-compilation and target profile (`ARM64` is compile-and-link verified on every build under toolset `v145`, and naming a specific physical `ARM64` benchmark device is deferred to `D99 T01 §4`).

## 1. Machine Profiles

| Attribute | Primary x64 Reference Machine (Binding Baseline) | Secondary ARM64 Target Profile (Non-Binding Until `D99 T01 §4`) |
| --------- | ------------------------------------------------ | --------------------------------------------------------------- |
| **Role** | Primary development, test, and binding section 8 benchmark host | `ARM64` cross-compilation gate (`v145`) and non-binding Windows 11 `ARM64` target profile (no physical `ARM64` device attached yet; specific device deferred to `D99 T01 §4`) |
| **Architecture** | `x64` (`x64-based PC`) | `ARM64` (`ARMv8.x-A` / `AArch64`, Windows 11 on ARM) |
| **Manufacturer / System** | ASUS (`System Product Name`) | Windows 11 ARM64 target class (to be pinned to a specific device in `D99 T01 §4`) |
| **CPU** | 11th Gen Intel(R) Core(TM) i5-11600K @ 3.90 GHz | Qualcomm Snapdragon X series target class |
| **Physical Cores / Threads** | 6 physical cores / 12 logical processors (`MaxClockSpeed = 3912 MHz`) | Target class: 10 to 12 physical cores |
| **Memory (RAM)** | 32 GB (`34,185,392,128` bytes physical memory) | Target class: `>= 16 GB` LPDDR5x (`>= 17,179,869,184` bytes) |
| **GPU & Driver** | NVIDIA GeForce RTX 2060 (6 GB GDDR6, `AdapterRAM = 4,293,918,720` cap, WDDM driver `32.0.16.1088`) | Qualcomm Adreno integrated DirectX 12 / Direct2D GPU |
| **Display & DPI Scaling** | `3840 x 2160` @ `59 Hz` (`59.94 Hz` nominal, ~`16.68 ms` vsync interval), `144 DPI` (`150%` Windows scaling, `AppliedDPI = 144`) | Target class: `144 DPI` (`150%` scaling) @ `60 Hz` |
| **OS Drive (`C:\`)** | Corsair MP600 PRO XT NVMe SSD (`1000.2 GB` decimal / `1,000,204,886,016` B physical (`Get-Disk` `Size`), `930.4 GiB` NTFS partition) | PCIe Gen4 NVMe SSD |
| **Workspace Drive (`R:\`)** | Micron `MTFDKCD512QFM-1BD1AABLA` NVMe SSD (`512.1 GB` decimal / `512,110,190,592` B physical (`Get-Disk` `Size`), `474.7 GiB` NTFS partition) | Local PCIe Gen4 NVMe SSD |
| **Operating System** | Microsoft Windows 11 Business 64-bit, Version `10.0.26300` (Build `26300`) | Microsoft Windows 11 64-bit (`ARM64`), Version `24H2` or later (`Build >= 26100`) |
| **Compiler & SDK** | Visual Studio 2026 `18.11.1` (`18.11.12315.225`, MSBuild `18.11.1.51303`, toolset `v145`, MSVC `14.51.36231`), Windows 11 SDK `10.0.26100.0` (min `10.0.22621.0`), Windows App SDK `2.5.1` | Visual Studio 2026 `18.11.1` (`18.11.12315.225`, toolset `v145`, MSVC `14.51.36231` `Hostx64\arm64`), Windows 11 SDK `10.0.26100.0` (min `10.0.22621.0`), Windows App SDK `2.5.1` |

## 2. Section 8 Performance Targets Bound to This Baseline

All five targets from `docs/plans/2026-10-13-recto-foundation.md` section 8 are evaluated in `Release` configurations (`build\bin\x64\Release\`) on the primary x64 reference machine with AddressSanitizer disabled (`EnableASAN=false`) and default user settings (`512 MB` tile cache budget):

| Metric | Target on Primary Reference Machine | Verification Mechanism |
| ------ | ----------------------------------- | ---------------------- |
| Cold launch to interactive Home window | `< 400 ms` | `D06 T01 §7` presentation startup harness (`Recto.exe`) |
| Open a 100-page text PDF to first visible page rendered | `< 300 ms` | `recto-cli bench` (`D01 T03 §3`) and `D06 T01 §7` harness |
| Scroll frame time with cached tiles | `<= 16.6 ms (60 fps)` at p95; `0` blank frames at normal reading scroll speed | `D06 T01 §7` scripted scroll trace harness |
| Zoom change | Stretched preview in same frame (`< 16 ms`); sharp tiles replace within `150 ms` | `D06 T01 §7` zoom latency harness and `recto-cli bench` tile throughput |
| Working set with one 500-page PDF open (`512 MB` tile cache cap) | `< 600 MB` | `D06 T01 §7` working-set monitor and `recto-cli bench` |

Because the primary x64 reference display reports `59 Hz` (`59.94 Hz`, or `16.68 ms` per vertical blank interval), the `D06 T01 §7` scroll harness must measure per-frame CPU/GPU presentation work prior to `IDXGISwapChain::Present` vsync wait (or compare vsync-paced frame intervals against the display's native `16.68 ms` cadence) so display refresh reporting does not create a false failure against the `16.6 ms` budget.

## 3. Benchmark Commands (`recto-cli bench` and Suite Harness)

Once `D01 T03 §3` implements the `bench` verb in `src/cli/main.cpp` and `D06 T01 §7` generates the benchmark PDF fixtures, headless engine benchmarks run on the primary x64 reference machine via:

```powershell
# Build Release binaries for x64 and ARM64
.\build.cmd

# Cold open, first-page DisplayList and 1-worker vs default-pool tile rasterization, median and p95 (x64 Release)
.\build\bin\x64\Release\recto-cli\recto-cli.exe bench <100-page-text-corpus.pdf> --iterations 20 --dpi 144 --json

# The same on the 500-page mixed benchmark PDF, which also reports the working set
.\build\bin\x64\Release\recto-cli\recto-cli.exe bench <500-page-mixed-corpus.pdf> --iterations 20 --dpi 144 --json
```

When a physical `ARM64` Windows 11 machine is recorded under `D99 T01 §4`, run the same commands using `.\build\bin\ARM64\Release\recto-cli\recto-cli.exe`.

As `D01 T03 §3` specifies, `recto-cli bench <file.pdf> [--iterations <n>] [--dpi <n>] [--json]` reports median and p95 timings for cold document open, first-page `DisplayList` compilation, and full-page tile rasterization with `1` worker against the default worker pool, plus the working set; its `--json` fields are `open_ms`, `display_list_ms`, `render_1w_ms`, `render_nw_ms` and `working_set_mb`. Worker counts are set on `recto-cli stress` (`--workers`), not on `bench`. The benchmark corpus filenames are finalized by `D06 T01 §7`.

## 4. Reproducing the Hardware Fingerprint

To verify whether a benchmark run was executed on the primary x64 reference host machine, run the following PowerShell query and compare the output against Section 1:

```powershell
Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed
Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer, Model, TotalPhysicalMemory, SystemType
Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, AdapterRAM, CurrentHorizontalResolution, CurrentVerticalResolution, CurrentRefreshRate
Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture
Get-ItemProperty -Path 'HKCU:\Control Panel\Desktop\WindowMetrics' -Name AppliedDPI -ErrorAction SilentlyContinue | Select-Object AppliedDPI
Get-Partition -DriveLetter C, R -ErrorAction SilentlyContinue | ForEach-Object { $d = Get-Disk -Number $_.DiskNumber; [PSCustomObject]@{ Drive=$_.DriveLetter; Model=$d.Model; BusType=$d.BusType; DiskBytes=$d.Size; PartitionGiB=[math]::Round($_.Size/1GB,1) } }
$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -version '[18.0,19.0)' -property installationPath
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -version '[18.0,19.0)' -property installationVersion
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -version '[18.0,19.0)' -property catalog_productDisplayVersion
Get-ChildItem "$vsPath\VC\Tools\MSVC" | Select-Object -ExpandProperty Name
& "$vsPath\MSBuild\Current\Bin\MSBuild.exe" -version -nologo
Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Include" | Select-Object -ExpandProperty Name
Select-String -Path src\app\Recto.vcxproj -Pattern 'Microsoft.WindowsAppSDK"' | ForEach-Object { $_.Line.Trim() }
```
