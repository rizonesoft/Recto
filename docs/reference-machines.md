# Reference Benchmark Machines

This document records the hardware, operating system, display, storage, and toolchain specifications for Recto's performance baselines, as required by `docs/plans/2026-10-13-recto-foundation.md` sections 8 and 11 (`D00 T01 §5`).
Milestone M2 and M6 performance claims (`D06 T01 §4`) are bound to `Release` builds measured on the primary x64 reference host machine below, with `ARM64` binaries cross-compiled on every build and verified against the ARM64 target profile.

## 1. Machine Profiles

| Attribute | Primary x64 Reference Machine (Binding Baseline) | Secondary ARM64 Target Profile (Cross-Compile & Runtime Profile) |
| --------- | ------------------------------------------------ | ---------------------------------------------------------------- |
| **Role** | Primary development, test, and binding section 8 benchmark host | `ARM64` cross-compilation gate (`v145`) and Windows 11 `ARM64` runtime target |
| **Architecture** | `x64` (`x64-based PC`) | `ARM64` (`ARMv8.2-A+` / Windows 11 on ARM) |
| **Manufacturer / System** | ASUS (`System Product Name`) | Windows 11 ARM64 reference class (Qualcomm Snapdragon X Plus / X Elite laptop or mini-PC) |
| **CPU** | 11th Gen Intel(R) Core(TM) i5-11600K @ 3.90 GHz | Qualcomm Snapdragon X series (`X1P-64-100` 10-core or `X1E-78-100` 12-core @ 3.40 GHz) |
| **Physical Cores / Threads** | 6 physical cores / 12 logical processors (`MaxClockSpeed = 3912 MHz`) | 10 to 12 physical cores / 10 to 12 logical processors |
| **Memory (RAM)** | 32 GB (`34,185,392,128` bytes physical memory) | 16 GB LPDDR5x (`>= 17,179,869,184` bytes physical memory) |
| **GPU & Driver** | NVIDIA GeForce RTX 2060 (6 GB GDDR6, `AdapterRAM = 4,293,918,720` cap, WDDM driver `32.0.16.1088`) | Qualcomm Adreno X1-85 integrated DirectX 12 / Direct2D GPU |
| **Display & DPI Scaling** | `3840 x 2160` @ `59 Hz`, `144 DPI` (`150%` Windows scaling, `AppliedDPI = 144`) | `2560 x 1600` or `1920 x 1200` @ `60 Hz`, `144 DPI` (`150%` scaling) |
| **OS Drive (`C:\`)** | Corsair MP600 PRO XT NVMe SSD (`1000.2 GB` physical, `930.4 GB` NTFS partition) | PCIe Gen4 NVMe SSD (`>= 512 GB`) |
| **Workspace Drive (`R:\`)** | Micron `MTFDKCD512QFM-1BD1AABLA` NVMe SSD (`512.1 GB` physical, `474.7 GB` NTFS partition) | Local PCIe Gen4 NVMe SSD |
| **Operating System** | Microsoft Windows 11 Business 64-bit, Version `10.0.26300` (Build `26300`) | Microsoft Windows 11 64-bit (`ARM64`), Version `24H2` or later (`Build >= 26100`) |
| **Compiler & SDK** | Visual Studio 2026 (`18.5`, toolset `v145`, MSVC `14.51.36231`), Windows 11 SDK `10.0.26100.0` (min `10.0.22621.0`), Windows App SDK `2.4.0` | Visual Studio 2026 (`18.5`, toolset `v145`, MSVC `14.51.36231` `Hostx64\arm64`), Windows 11 SDK `10.0.26100.0` (min `10.0.22621.0`), Windows App SDK `2.4.0` |

## 2. Section 8 Performance Targets Bound to This Baseline

All five targets from `docs/plans/2026-10-13-recto-foundation.md` section 8 are evaluated in `Release` configurations (`build\bin\x64\Release\` and `build\bin\ARM64\Release\`) with AddressSanitizer disabled (`EnableASAN=false`) and default user settings (`512 MB` tile cache budget):

| Metric | Target on Primary Reference Machine | Verification Mechanism |
| ------ | ----------------------------------- | ---------------------- |
| Cold launch to interactive Home window | `< 400 ms` | `D06 T01 §4` presentation startup harness (`Recto.exe`) |
| Open a 100-page text PDF to first visible page rendered | `< 300 ms` | `recto-cli bench` (`D01 T03 §1`) and `D06 T01 §4` harness |
| Scroll frame time with cached tiles | `<= 16.6 ms (60 fps)` at p95; `0` blank frames at normal reading scroll speed | `D06 T01 §4` scripted scroll trace harness |
| Zoom change | Stretched preview in same frame (`< 16 ms`); sharp tiles replace within `150 ms` | `D06 T01 §4` zoom latency harness and `recto-cli bench` tile throughput |
| Working set with one 500-page PDF open (`512 MB` tile cache cap) | `< 600 MB` | `D06 T01 §4` working-set monitor and `recto-cli bench` |

## 3. Benchmark Commands (`recto-cli bench` and Suite Harness)

Once `D01 T03 §1` lands the `bench` verb in `src/cli/main.cpp`, headless engine benchmarks run on the reference machine via:

```powershell
# Build Release binaries for x64 and ARM64
.\build.cmd

# Run single-worker vs multi-worker engine benchmark on a corpus PDF (x64 Release)
.\build\bin\x64\Release\recto-cli\recto-cli.exe bench tests\corpus\render\text-100p.pdf --dpi 144 --workers 8 --json

# Compare 1-worker vs 8-worker tile rasterization throughput on the 500-page mixed benchmark PDF
.\build\bin\x64\Release\recto-cli\recto-cli.exe bench tests\corpus\render\mixed-500p.pdf --dpi 144 --workers 1 --json
.\build\bin\x64\Release\recto-cli\recto-cli.exe bench tests\corpus\render\mixed-500p.pdf --dpi 144 --workers 8 --json
```

On an `ARM64` Windows 11 machine, run the exact same commands substituting `.\build\bin\ARM64\Release\recto-cli\recto-cli.exe`.
The `recto-cli bench` command reports cold document open time (`open_ms`), per-page `DisplayList` recording latency (`display_list_p50_ms`, `display_list_p95_ms`), and `512 x 512` tile rasterization throughput (`tiles_per_sec`, `page_render_ms`) for `1` worker versus `N` workers (`--workers`).

## 4. Reproducing the Hardware Fingerprint

To verify whether a benchmark run was executed on the primary x64 reference host machine, run the following PowerShell query and compare the output against Section 1:

```powershell
Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed
Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer, Model, TotalPhysicalMemory, SystemType
Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion, CurrentHorizontalResolution, CurrentVerticalResolution, CurrentRefreshRate
Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture
Get-ItemProperty -Path 'HKCU:\Control Panel\Desktop\WindowMetrics' -Name AppliedDPI -ErrorAction SilentlyContinue | Select-Object AppliedDPI
```
