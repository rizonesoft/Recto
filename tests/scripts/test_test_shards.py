"""Tests for build/test-shards.ps1: the x64 suite in parallel GoogleTest shards (D00 T04 section 11).

The shard count is read from injected memory and cores; the runner is driven end to end with a fake test binary (a
Python script that honours GTEST_TOTAL_SHARDS, GTEST_SHARD_INDEX and --gtest_filter like recto-tests.exe), so the
sharding, the serial pass, the reports, the failure log and every kill path run for real without a C++ build. Every
console child is started without a window.
"""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
from scripts.lib.proc_group import spawn_managed_process, windowless  # noqa: E402

TEST_SHARDS_PS1 = ROOT / "build" / "test-shards.ps1"
TEST_TIMES = ROOT / "scripts" / "test-times.py"
SERIAL_LIST = ROOT / "tests" / "recto-tests" / "serial-tests.txt"
# CI runs build.ps1 under Windows PowerShell 5.1, so the helper is proven there first.
POWERSHELL: str = shutil.which("powershell") or shutil.which("pwsh") or ""
ON_WINDOWS = os.name == "nt" and bool(POWERSHELL)

# A stand-in for recto-tests.exe. Twelve tests in three suites; FAKE_FAIL names a test that fails, FAKE_NO_XML a
# process ("shard0", "serial") that writes no report, FAKE_HANG a process that starts a child and both sleep (at most
# 120 s, so nothing it starts can outlive a broken test for long). Each process records its own and its child's pid.
FAKE_GTEST = textwrap.dedent(
    r'''
    import fnmatch, os, subprocess, sys, time
    TESTS = [f"Fast.T{i}" for i in range(8)] + ["Slow.Timed", "Slow.Budget", "Ui.App.Starts", "Ui.DISABLED_App.Probe"]
    args = sys.argv[1:]
    xml = next((a.split(":", 1)[1] for a in args if a.startswith("--gtest_output=xml:")), None)
    # Like gtest: --gtest_filter wins, GTEST_FILTER stands in when no flag is given, and the list honours it too.
    flt = next((a.split("=", 1)[1] for a in args if a.startswith("--gtest_filter=")), os.environ.get("GTEST_FILTER", "*"))
    pos, _, neg = flt.partition("-")
    pos = pos or "*"
    def match(name, pats):
        return any(fnmatch.fnmatchcase(name, p) for p in pats.split(":") if p)
    selected = [t for t in TESTS if match(t, pos) and not (neg and match(t, neg))]
    if "--gtest_list_tests" in args:
        print("Running main() from fake_gtest.py")
        suite = None
        for name in selected:
            s, t = name.split(".", 1)
            if s != suite:
                print(s + ".")
                suite = s
            print("  " + t)
        sys.exit(0)
    total = int(os.environ.get("GTEST_TOTAL_SHARDS", "1"))
    index = int(os.environ.get("GTEST_SHARD_INDEX", "0"))
    me = f"shard{index}" if "GTEST_TOTAL_SHARDS" in os.environ else "serial"
    selected = [t for i, t in enumerate(selected) if i % total == index]
    piddir = os.environ.get("FAKE_PID_DIR")
    if piddir:
        with open(os.path.join(piddir, me + ".pid"), "w") as f:
            f.write(str(os.getpid()))
    if os.environ.get("FAKE_HANG") == me:
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)"], creationflags=0x08000000)
        if piddir:
            with open(os.path.join(piddir, me + ".child"), "w") as f:
                f.write(str(child.pid))
        time.sleep(120)
        sys.exit(3)
    failed = 0
    rows = []
    for name in selected:
        suite, test = name.split(".", 1)
        disabled = test.startswith("DISABLED_")
        print(f"[ RUN      ] {name}")
        if name == os.environ.get("FAKE_FAIL"):
            print(f"fake_gtest.py: Failure in {name}")
            print(f"[  FAILED  ] {name} (0 ms)")
            failed += 1
        elif not disabled:
            print(f"[       OK ] {name} (0 ms)")
        status = "notrun" if disabled else "run"
        rows.append(f'<testsuite name="{suite}" tests="1" time="0.001"><testcase name="{test}" status="{status}" '
                    f'result="completed" time="0.001" classname="{suite}" /></testsuite>')
    if xml and os.environ.get("FAKE_NO_XML") != me:
        with open(xml, "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?><testsuites tests="%d" time="0.01">%s</testsuites>\n'
                    % (len(rows), "".join(rows)))
    sys.exit(1 if failed else 0)
    '''
)


def _alive(pid: int) -> bool:
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = ctypes.c_void_p
    handle = kernel32.OpenProcess(0x1000 | 0x00100000, False, pid)  # QUERY_LIMITED_INFORMATION | SYNCHRONIZE
    if not handle:
        return False
    try:
        return kernel32.WaitForSingleObject(ctypes.c_void_p(handle), 0) != 0
    finally:
        kernel32.CloseHandle(ctypes.c_void_p(handle))


def _wait_gone(pids: list[int], seconds: float) -> list[int]:
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        left = [p for p in pids if _alive(p)]
        if not left:
            return []
        time.sleep(0.1)
    return [p for p in pids if _alive(p)]


def _ps(script: str, timeout: float = 120, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
        capture_output=True, text=True, timeout=timeout, env=env, creationflags=windowless(),
    )


@unittest.skipUnless(ON_WINDOWS, "needs Windows PowerShell")
class ShardCount(unittest.TestCase):
    def _count(self, args: str, pre: str = "") -> dict:
        res = _ps(f"{pre}. '{TEST_SHARDS_PS1}'; Resolve-TestShardCount {args} | ConvertTo-Json -Compress")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        return json.loads(res.stdout.strip())

    def test_automatic_count_from_injected_memory_and_cores(self):
        cases = [
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 25000", 6),    # this host: one per physical core
            ("-Cores 4 -PhysicalCores 2 -ShardsEnv '' -AvailableMb 25000", 2),     # a 4-vCPU CI runner
            ("-Cores 4 -PhysicalCores 4 -ShardsEnv '' -AvailableMb 25000", 4),     # no SMT
            ("-Cores 32 -PhysicalCores 16 -ShardsEnv '' -AvailableMb 25000", 6),   # the cap of 6
            ("-Cores 4 -PhysicalCores 8 -ShardsEnv '' -AvailableMb 25000", 4),     # never above the logical count
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 6394", 4),     # (6394 - 2048) / 1024
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 4096", 2),
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 3072", 1),
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 3071", 1),     # too little for a second process
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 25000 -Asan", 5),   # (25000 - 2048) / 4096
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 10240 -Asan", 2),
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 8000 -Asan", 1),    # two ASan processes do not fit
            ("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 4096 -Asan", 1),
            ("-Cores 1 -PhysicalCores 1 -ShardsEnv '' -AvailableMb 25000", 1),
        ]
        for args, want in cases:
            with self.subTest(args=args):
                got = self._count(args)
                self.assertEqual(got["Count"], want, got)
                self.assertIn("MB", got["Reason"])
        low = self._count("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 3071")
        self.assertIn("too little for a second process", low["Reason"])
        self.assertIn("4,096 MB per AddressSanitizer shard", self._count("-Cores 12 -PhysicalCores 6 -ShardsEnv '' -AvailableMb 25000 -Asan")["Reason"])
        self.assertIn("6 physical cores (12 logical)", low["Reason"])

    def test_physical_cores_fall_back_to_half_the_logical_count(self):
        pre = "function global:Get-CimInstance { throw 'wmi gone' }\n"
        for cores, want in ((12, 6), (4, 2), (1, 1)):
            with self.subTest(cores=cores):
                res = _ps(f"{pre}. '{TEST_SHARDS_PS1}'; Get-PhysicalCoreCount -Cores {cores}")
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                self.assertEqual(int(res.stdout.strip()), want)

    def test_unreadable_memory_falls_back_to_one_process(self):
        pre = ("function global:Get-Counter { throw 'counter gone' }\n"
               "function global:Get-CimInstance { throw 'wmi gone' }\n")
        got = self._count("-Cores 12 -ShardsEnv ''", pre=pre)
        self.assertEqual(got["Count"], 1, got)
        self.assertIn("free memory unreadable", got["Reason"])

    def test_override_accepted_and_refused(self):
        for value, want in (("1", 1), ("12", 12), (" 3 ", 3)):
            with self.subTest(value=value):
                self.assertEqual(self._count(f"-Cores 12 -ShardsEnv '{value}' -AvailableMb 25000")["Count"], want)
        for bad in ("0", "13", "x", "1.5", "-2"):
            with self.subTest(bad=bad):
                res = _ps(f". '{TEST_SHARDS_PS1}'; Resolve-TestShardCount -Cores 12 -ShardsEnv '{bad}' -AvailableMb 25000")
                self.assertNotEqual(res.returncode, 0, res.stdout)
                self.assertIn(f"Invalid RECTO_TEST_SHARDS='{bad}'", res.stdout + res.stderr)

    def test_timeout_minutes_accepted_and_refused(self):
        for value, want in (("", 30.0), ("0.5", 0.5), ("45", 45.0)):
            with self.subTest(value=value):
                res = _ps(f". '{TEST_SHARDS_PS1}'; (Resolve-TestTimeoutMinutes -TimeoutEnv '{value}').ToString([cultureinfo]::InvariantCulture)")
                self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
                self.assertEqual(float(res.stdout.strip()), want)
        for bad in ("0", "-1", "abc", "NaN", "1e3"):
            with self.subTest(bad=bad):
                res = _ps(f". '{TEST_SHARDS_PS1}'; Resolve-TestTimeoutMinutes -TimeoutEnv '{bad}'")
                self.assertNotEqual(res.returncode, 0, res.stdout)
                self.assertIn("RECTO_TEST_TIMEOUT_MINUTES", res.stdout + res.stderr)


@unittest.skipUnless(ON_WINDOWS, "needs Windows PowerShell")
class SerialList(unittest.TestCase):
    def test_live_list_parses_and_every_pattern_is_a_single_name(self):
        res = _ps(f". '{TEST_SHARDS_PS1}'; (Read-SerialTestPatterns '{SERIAL_LIST}') -join \"`n\"")
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        patterns = res.stdout.split()
        self.assertGreater(len(patterns), 0)
        self.assertEqual(len(patterns), len(set(patterns)), "a pattern is listed twice")
        self.assertIn("Ui.App.SelfTestStartupBudget", patterns)
        self.assertIn("PdfRasterizer.MidReplayCancelUnder10Ms", patterns)

    def test_comments_are_dropped_and_filter_syntax_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            good = Path(tmp) / "good.txt"
            good.write_text("# header\n\nA.B   # why\n  C.*\n", encoding="utf-8")
            res = _ps(f". '{TEST_SHARDS_PS1}'; (Read-SerialTestPatterns '{good}') -join ','")
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            self.assertEqual(res.stdout.strip(), "A.B,C.*")
            for bad in ("A.B:C.D", "-A.B", "A.B C.D"):
                with self.subTest(bad=bad):
                    path = Path(tmp) / "bad.txt"
                    path.write_text(bad + "\n", encoding="utf-8")
                    res = _ps(f". '{TEST_SHARDS_PS1}'; Read-SerialTestPatterns '{path}'")
                    self.assertNotEqual(res.returncode, 0, res.stdout)
                    self.assertIn("is not one gtest filter pattern", res.stdout + res.stderr)


class SharedTempNames(unittest.TestCase):
    """No recto-tests source names a file in the machine's shared temp folder: side by side, two processes would write
    and remove the same file (PdfRasterizer.GoldenSeam did, until 2026-11-16). recto::test::TempDir is the way."""

    # The only allowed uses: TempDir itself, and a disabled test run by hand for an external decoder cross-check.
    ALLOWED = {
        ("test_support.cpp", "GetTempPathW"),
        ("pdf_filters_tests.cpp", '::testing::TempDir()) / "recto-j2k-styles"'),
    }

    def test_no_test_writes_under_a_shared_temp_name(self):
        found = []
        for source in sorted((ROOT / "tests" / "recto-tests").glob("*.cpp")):
            for lineno, line in enumerate(source.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
                code = line.split("//", 1)[0]
                for token in ("temp_directory_path", "GetTempPath", "testing::TempDir"):
                    if token in code and not any(source.name == f and allowed in code for f, allowed in self.ALLOWED):
                        found.append(f"{source.name}:{lineno}: {line.strip()}")
        self.assertEqual(found, [], "use recto::test::TempDir, whose folder is this process's own")


@unittest.skipUnless(ON_WINDOWS, "needs Windows PowerShell")
class ShardRun(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.base = Path(self._tmp.name).resolve()
        self.fake = self.base / "fake_gtest.py"
        self.fake.write_text(FAKE_GTEST, encoding="utf-8")
        self.out = self.base / "out dir"  # a space, as a user folder may have
        self.out.mkdir()
        self.pids = self.base / "pids"
        self.pids.mkdir()

    def tearDown(self):
        # Whatever a failing test left is killed here, by pid, before the folder goes.
        leftovers = [int(p.read_text()) for p in self.pids.iterdir() if p.read_text().strip().isdigit()]
        for pid in leftovers:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)], capture_output=True, creationflags=windowless())
        self._tmp.cleanup()

    def _invoke_script(self, timeout_minutes: float = 2.0, serial: str = "@('Slow.*')", tag: str = "Debug") -> str:
        return (
            f". '{TEST_SHARDS_PS1}'\n"
            f"$r = Invoke-RectoTestShards -Exe '{sys.executable}' -PrefixArguments @('{self.fake}') "
            f"-WorkingDirectory '{self.base}' -XmlDir '{self.out}' -Tag '{tag}' -Shards 3 -SerialPatterns {serial} "
            f"-TimeoutMinutes {timeout_minutes}\n"
            "$r.Xml -join \"`n\"\n"
            "'LIST=' + $r.List\n"
        )

    def _env(self, **extra: str) -> dict[str, str]:
        env = os.environ.copy()
        for key in ("FAKE_FAIL", "FAKE_NO_XML", "FAKE_HANG", "GTEST_TOTAL_SHARDS", "GTEST_SHARD_INDEX"):
            env.pop(key, None)
        env["FAKE_PID_DIR"] = str(self.pids)
        env.update(extra)
        return env

    def _pids(self) -> list[int]:
        return [int(p.read_text()) for p in self.pids.iterdir() if p.read_text().strip().isdigit()]

    def test_shards_and_serial_pass_cover_every_test_once(self):
        res = _ps(self._invoke_script(tag="Debug-asan"), env=self._env())
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        xml = [line for line in res.stdout.splitlines() if line.endswith(".xml")]
        names = sorted(Path(x).name for x in xml)
        self.assertEqual(names, sorted([f"test-results-x64-Debug-asan-shard{i}.xml" for i in range(3)] + ["test-results-x64-Debug-asan-serial.xml"]))
        for x in xml:
            self.assertEqual(Path(x).parent, self.out)
        for i in range(3):
            self.assertTrue((self.out / f"test-x64-Debug-asan-shard{i}.log").is_file())
        serial_log = (self.out / "test-x64-Debug-asan-serial.log").read_text(encoding="utf-8")
        self.assertIn("[       OK ] Slow.Timed", serial_log)
        for i in range(3):
            self.assertNotIn("Slow.", (self.out / f"test-x64-Debug-asan-shard{i}.log").read_text(encoding="utf-8"))
        listing = next(line[5:] for line in res.stdout.splitlines() if line.startswith("LIST="))
        cov = subprocess.run([sys.executable, str(TEST_TIMES), "coverage", listing, *xml], capture_output=True, text=True,
                             timeout=60, creationflags=windowless())
        self.assertEqual(cov.returncode, 0, cov.stdout + cov.stderr)
        self.assertIn("12 listed tests each reported exactly once across 4 report(s) (11 enabled run, 1 disabled not run)", cov.stdout)
        # One shard's report left out of the coverage check fails it, naming that shard's tests.
        partial = subprocess.run([sys.executable, str(TEST_TIMES), "coverage", listing, *[x for x in xml if "shard1" not in x]],
                                 capture_output=True, text=True, timeout=60, creationflags=windowless())
        self.assertEqual(partial.returncode, 1, partial.stdout + partial.stderr)
        self.assertIn("missing from every report: Fast.T1", partial.stderr)
        self.assertIn("peak commit", res.stdout)

    def test_an_inherited_gtest_filter_narrows_neither_the_list_nor_the_runs(self):
        # Round 1 #2: the list inherited GTEST_FILTER while the runs' own filters replaced it, so coverage refused a
        # correct run as "reported but not listed".
        res = _ps(self._invoke_script(), env=self._env(GTEST_FILTER="Fast.T0"))
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        xml = [line for line in res.stdout.splitlines() if line.endswith(".xml")]
        listing = next(line[5:] for line in res.stdout.splitlines() if line.startswith("LIST="))
        cov = subprocess.run([sys.executable, str(TEST_TIMES), "coverage", listing, *xml], capture_output=True, text=True,
                             timeout=60, creationflags=windowless())
        self.assertEqual(cov.returncode, 0, cov.stdout + cov.stderr)
        self.assertIn("12 listed tests each reported exactly once across 4 report(s)", cov.stdout)

    def test_every_process_is_created_inside_the_job(self):
        # Round 1 #1: CreateProcessW then AssignProcessToJobObject leaves a moment when a killed host orphans the
        # suspended child. The job goes in the attribute list (PROC_THREAD_ATTRIBUTE_JOB_LIST), so creation is the
        # assignment; the race cannot be forced in a test, so the source is held to it.
        source = TEST_SHARDS_PS1.read_text(encoding="utf-8")
        self.assertIn("PROC_THREAD_ATTRIBUTE_JOB_LIST = (IntPtr)0x0002000D", source)
        self.assertIn("UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs", source)
        self.assertNotIn("AssignProcessToJobObject(", source)
        self.assertEqual(source.count("CreateProcessW("), 2, "one declaration and one call, both through the attribute list")

    def test_a_failing_test_fails_the_run_with_its_log_printed(self):
        res = _ps(self._invoke_script(), env=self._env(FAKE_FAIL="Fast.T4"))
        self.assertNotEqual(res.returncode, 0, res.stdout)
        out = res.stdout + res.stderr
        self.assertIn("recto-tests failed: shard 1 of 3 exit 1", out)
        self.assertIn("fake_gtest.py: Failure in Fast.T4", out)
        self.assertIn("[  FAILED  ] Fast.T4", out)

    def test_a_process_that_leaves_no_report_fails_the_run(self):
        for who, name in (("shard0", "shard 0 of 3"), ("serial", "serial pass")):
            with self.subTest(who=who):
                res = _ps(self._invoke_script(), env=self._env(FAKE_NO_XML=who))
                self.assertNotEqual(res.returncode, 0, res.stdout)
                self.assertIn(f"recto-tests did not produce XML report: {name}", res.stdout + res.stderr)

    def _assert_hang_killed(self, who: str, running: str) -> None:
        started = time.monotonic()
        res = _ps(self._invoke_script(timeout_minutes=2 / 60), env=self._env(FAKE_HANG=who), timeout=60)
        self.assertNotEqual(res.returncode, 0, res.stdout)
        self.assertIn("passed its deadline of", res.stdout + res.stderr)
        self.assertIn(f"{running} still running; killed with every process it started", res.stdout + res.stderr)
        child = int((self.pids / f"{who}.child").read_text())
        hung = int((self.pids / f"{who}.pid").read_text())
        self.assertEqual(_wait_gone([hung, child], 10), [], "the hung process or its child outlived the deadline")
        self.assertLess(time.monotonic() - started, 2 + 10 + 20, "the deadline kill took too long")

    def test_a_hung_shard_and_its_child_are_killed_at_the_deadline(self):
        self._assert_hang_killed("shard1", "shard 1 of 3")

    def test_a_hung_serial_pass_and_its_child_are_killed_at_the_deadline(self):
        self._assert_hang_killed("serial", "serial pass")

    def _single_script(self, timeout_minutes: float) -> str:
        return (
            f". '{TEST_SHARDS_PS1}'\n"
            f"$code = Invoke-RectoTestSingle -Exe '{sys.executable}' -PrefixArguments @('{self.fake}') "
            f"-Arguments @('--gtest_output=xml:{self.out / 'single.xml'}') -WorkingDirectory '{self.base}' "
            f"-TimeoutMinutes {timeout_minutes}\n"
            "Write-Output \"EXIT=$code\"\n"
        )

    def test_the_single_run_writes_to_the_console_and_returns_its_exit_code(self):
        res = _ps(self._single_script(2.0), env=self._env())
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("[       OK ] Fast.T0", res.stdout, "the single run's output did not reach the caller's console")
        self.assertIn("EXIT=0", res.stdout)
        self.assertTrue((self.out / "single.xml").is_file())
        failing = _ps(self._single_script(2.0), env=self._env(FAKE_FAIL="Slow.Timed"))
        self.assertEqual(failing.returncode, 0, failing.stdout + failing.stderr)
        self.assertIn("[  FAILED  ] Slow.Timed", failing.stdout)
        self.assertIn("EXIT=1", failing.stdout)

    def test_the_single_run_shares_a_real_console(self):
        # From a terminal, build.ps1's output is a console handle, which only a process attached to that console can
        # write to. A host on a console of its own (hidden, so nothing shows) runs a probe that records whether its
        # write to standard output succeeded and whether it has a console.
        probe = self.base / "probe.py"
        result = self.base / "probe.txt"
        probe.write_text(
            "import ctypes, os, sys\n"
            "try:\n"
            "    wrote = os.write(1, b'probe on the console\\n')\n"
            "except OSError as exc:\n"
            "    wrote = f'error {exc}'\n"
            "attached = bool(ctypes.windll.kernel32.GetConsoleWindow())\n"
            f"open(r'{result}', 'w').write(f'{{wrote}} {{attached}}')\n",
            encoding="utf-8",
        )
        script = (
            f". '{TEST_SHARDS_PS1}'\n"
            f"$code = Invoke-RectoTestSingle -Exe '{sys.executable}' -PrefixArguments @('{probe}') -WorkingDirectory '{self.base}' -TimeoutMinutes 1\n"
            "exit $code\n"
        )
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0  # SW_HIDE: a console of its own that nobody sees
        host = subprocess.run([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                              creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup, timeout=120)
        self.assertEqual(host.returncode, 0)
        self.assertEqual(result.read_text(encoding="utf-8"), "21 True", "the single run could not write to the caller's console")

    def test_a_hung_single_run_and_its_child_are_killed_at_the_deadline(self):
        res = _ps(self._single_script(2 / 60), env=self._env(FAKE_HANG="serial"), timeout=60)
        self.assertNotEqual(res.returncode, 0, res.stdout)
        self.assertIn("recto-tests still running; killed with every process it started", res.stdout + res.stderr)
        child = int((self.pids / "serial.child").read_text())
        hung = int((self.pids / "serial.pid").read_text())
        self.assertEqual(_wait_gone([hung, child], 10), [], "the hung single run or its child outlived the deadline")

    def test_a_process_that_cannot_start_stops_the_ones_already_started(self):
        script = (
            f". '{TEST_SHARDS_PS1}'\n"
            "$job = [Recto.TestShards.Job]::Create()\n"
            "$specs = @(\n"
            f"  [pscustomobject]@{{ Name = 'shard 0 of 2'; Arguments = @(); LogPath = '{self.out / 'a.log'}'; Environment = @('GTEST_TOTAL_SHARDS=2', 'GTEST_SHARD_INDEX=0') }},\n"
            f"  [pscustomobject]@{{ Name = 'shard 1 of 2'; Arguments = @(); LogPath = '{self.base / 'no such folder' / 'b.log'}'; Environment = @() }}\n"
            ")\n"
            "try {\n"
            f"  Start-RectoTestProcessSet -Job $job -Exe '{sys.executable}' -PrefixArguments @('{self.fake}') -WorkingDirectory '{self.base}' -Specs $specs\n"
            "} catch { Write-Output \"CAUGHT: $($_.Exception.Message)\"; Write-Output \"ACTIVE=$([Recto.TestShards.Job]::ActiveProcesses($job))\" }\n"
        )
        res = _ps(script, env=self._env(FAKE_HANG="shard0"), timeout=60)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("CAUGHT: recto-tests: could not start shard 1 of 2", res.stdout)
        self.assertIn("every process already started was stopped", res.stdout)
        self.assertIn("ACTIVE=0", res.stdout)
        started = [int(p.read_text()) for p in self.pids.iterdir()]
        self.assertEqual(_wait_gone(started, 10), [], "the shard already started outlived the failed start")

    def test_killing_the_host_kills_every_process_of_the_set(self):
        script = self._invoke_script(timeout_minutes=5)
        managed = spawn_managed_process([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script],
                                        cwd=self.base, env=self._env(FAKE_HANG="shard2"), capture_output=True)
        try:
            child_file = self.pids / "shard2.child"
            until = time.monotonic() + 60
            while not child_file.is_file() or not child_file.read_text().strip():
                self.assertLess(time.monotonic(), until, "the hung shard never started its child")
                time.sleep(0.1)
            time.sleep(0.3)
            pids = self._pids()
            self.assertGreaterEqual(len(pids), 4, pids)  # three shards and the hung one's child
            managed.proc.kill()  # the host alone, as an interrupted or killed build.ps1; its job handle closes with it
            managed.proc.wait(timeout=30)
            self.assertEqual(_wait_gone(pids, 10), [], "a test process outlived the PowerShell host that started it")
        finally:
            managed.terminate_and_close()


if __name__ == "__main__":
    unittest.main()
