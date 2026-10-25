#!/usr/bin/env python3
"""Measure Recto's Flate compressor against zlib-ng on this host (D01 T02 section 37).

Usage:
    python scripts/flate-bench.py --exe build/bin/x64/Release/recto-tests/recto-tests.exe [--parity]

Requires Python's zlib to be zlib-ng 2.2 or later (`zlib.ZLIBNG_VERSION`), the backend the targets were measured
against, and refuses (exit 2) without it. Times zlib-ng at level 6 on the three samples in `tests/corpus/flate/`
(median of seven), runs `PdfFilters.FlateCompressSpeed` from the given test executable, which prints each level's
median and size per sample, and compares `Default`:

- by default (section 37), `Default` is no larger than zlib-ng on the content and image samples, and its speed is at
  least 0.62, 0.60 and 0.54 of zlib-ng's on content, image rows and collisions: about 10% under the lowest of five
  runs of this gate on the reference host (0.69, 0.68 and 0.60), so a real regression fails and run-to-run noise
  does not;
- with `--parity` (section 39), `Default` is at least as fast and no larger on all three.

Exit 0 when every comparison holds, 1 when one does not or a result is missing (the test skipped or failed, a sample
not printed), 2 when the comparison cannot be made (no zlib-ng, no executable).
"""
from __future__ import annotations

import argparse
import re
import statistics
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SAMPLES = ("flate/content-stream.txt", "flate/image-rows.bin", "flate/collisions.bin")
SPEED_FLOORS = {"flate/content-stream.txt": 0.62, "flate/image-rows.bin": 0.60, "flate/collisions.bin": 0.54}
SIZE_NOT_LARGER = ("flate/content-stream.txt", "flate/image-rows.bin")
MIN_ZLIBNG = (2, 2)
RUNS = 7
LINE = re.compile(
    r"^flate_compress (?P<level>\w+)\s+(?P<sample>\S+)\s+median\s+(?P<median>[\d.]+) MB/s, "
    r"slowest\s+(?P<slowest>[\d.]+) MB/s, (?P<bytes>\d+) bytes$")


def zlibng_version() -> tuple[int, ...] | None:
    """The zlib-ng version behind Python's zlib, or None when it is classic zlib."""
    version = getattr(zlib, "ZLIBNG_VERSION", None)
    if not version:
        return None
    return tuple(int(part) for part in re.findall(r"\d+", version)[:3])


def time_zlib(data: bytes) -> tuple[float, int]:
    """zlib-ng at level 6: the median MB/s of seven runs and the output size."""
    seconds = []
    size = 0
    for _ in range(RUNS):
        start = time.perf_counter()
        size = len(zlib.compress(data, 6))
        seconds.append(time.perf_counter() - start)
    return len(data) / 1e6 / statistics.median(seconds), size


def run_recto(exe: Path) -> tuple[int, str]:
    result = subprocess.run([str(exe), "--gtest_filter=PdfFilters.FlateCompressSpeed"], cwd=ROOT,
                            capture_output=True, text=True, timeout=600)
    return result.returncode, result.stdout


def recto_results(output: str) -> dict[str, tuple[float, int]]:
    """`Default`'s median MB/s and size per sample from the speed test's lines."""
    found = {}
    for line in output.splitlines():
        m = LINE.match(line.strip())
        if m and m["level"] == "Default":
            found[m["sample"]] = (float(m["median"]), int(m["bytes"]))
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--exe", type=Path, required=True, help="the Release recto-tests.exe to measure")
    parser.add_argument("--parity", action="store_true", help="hold Default to zlib-ng's speed and size on all samples")
    parser.add_argument("--corpus", type=Path, default=ROOT / "tests" / "corpus")
    args = parser.parse_args(argv)

    version = zlibng_version()
    if version is None or version[:2] < MIN_ZLIBNG:
        print(f"flate-bench: refused: Python's zlib is {zlib.ZLIB_VERSION}, not zlib-ng "
              f"{'.'.join(map(str, MIN_ZLIBNG))} or later, the backend the targets were measured against")
        return 2
    if not args.exe.is_file():
        print(f"flate-bench: refused: no test executable at {args.exe}")
        return 2
    print(f"flate-bench: zlib-ng {zlib.ZLIBNG_VERSION} at level 6, against {args.exe}")

    code, output = run_recto(args.exe)
    if code != 0 or "[  SKIPPED ]" in output or "[  PASSED  ] 1 test" not in output:
        print(f"flate-bench: PdfFilters.FlateCompressSpeed did not pass and run (exit {code})")
        return 1
    recto = recto_results(output)

    failures = []
    for sample in SAMPLES:
        if sample not in recto:
            failures.append(f"{sample}: no Default result from the test")
            continue
        data = (args.corpus / sample).read_bytes()
        ng_speed, ng_size = time_zlib(data)
        speed, size = recto[sample]
        floor = 1.0 if args.parity else SPEED_FLOORS[sample]
        ratio = speed / ng_speed
        print(f"  {sample:26} Recto {speed:6.1f} MB/s {size:8} bytes   zlib-ng {ng_speed:6.1f} MB/s {ng_size:8} bytes"
              f"   speed {ratio:.2f} of zlib-ng (floor {floor:.2f})")
        if ratio < floor:
            failures.append(f"{sample}: {ratio:.2f} of zlib-ng's speed, under {floor:.2f}")
        if (args.parity or sample in SIZE_NOT_LARGER) and size > ng_size:
            failures.append(f"{sample}: {size} bytes, larger than zlib-ng's {ng_size}")
    for failure in failures:
        print(f"flate-bench: FAIL {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
