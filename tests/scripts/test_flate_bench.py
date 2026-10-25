"""Tests for scripts/flate-bench.py (D01 T02 section 37): its refusals, a missing result, and both comparisons, with
the zlib-ng timing and the test executable replaced so no build is needed."""
import importlib.util
import io
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("flate_bench", ROOT / "scripts" / "flate-bench.py")
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)

PASSED = "[  PASSED  ] 1 test.\n"


def line(sample: str, median: float, size: int, level: str = "Default", input_size: int | None = None) -> str:
    if input_size is None:
        input_size = (ROOT / "tests" / "corpus" / sample).stat().st_size
    return (f"flate_compress {level:<7} {sample:<26} median {median:6.1f} MB/s, slowest {median - 1:6.1f} MB/s, "
            f"{input_size} -> {size} bytes\n")


# zlib-ng's measurements the cases compare against: content, image rows, collisions.
NG = {"flate/content-stream.txt": (55.0, 363_179), "flate/image-rows.bin": (55.6, 48_947),
      "flate/collisions.bin": (30.3, 61_593)}
GOOD = (line("flate/content-stream.txt", 47.4, 360_736) + line("flate/image-rows.bin", 53.3, 48_758)
        + line("flate/collisions.bin", 19.9, 62_161) + line("flate/collisions.bin", 70.0, 65_615, "Fast") + PASSED)


class FlateBench(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.exe = Path(self.tmp.name) / "recto-tests.exe"
        self.exe.write_bytes(b"")
        self.zlib = mock.patch.object(bench, "zlibng_version", return_value=(2, 2, 4))
        self.zlib.start()
        self.addCleanup(self.zlib.stop)
        def fake_time(data: bytes):
            for sample, path in ((s, ROOT / "tests" / "corpus" / s) for s in bench.SAMPLES):
                if len(data) == path.stat().st_size:
                    return NG[sample]
            raise AssertionError("an unknown sample was timed")

        self.time = mock.patch.object(bench, "time_zlib", side_effect=fake_time)
        self.time.start()
        self.addCleanup(self.time.stop)

    def run_with(self, output: str, code: int = 0, *extra: str) -> tuple[int, str]:
        with mock.patch.object(bench, "run_recto", return_value=(code, output)) as run:
            buf = io.StringIO()
            with redirect_stdout(buf):
                result = bench.main(["--exe", str(self.exe), *extra])
        if run.called:
            self.assertEqual(run.call_args.args[1], ROOT / "tests" / "corpus")
        return result, buf.getvalue()

    def test_passes_at_the_measured_ratios(self):
        code, out = self.run_with(GOOD)
        self.assertEqual(code, 0, out)
        self.assertIn("zlib-ng", out)

    def test_refuses_without_zlib_ng(self):
        for version in (None, (2, 1, 9), (1, 9)):
            with self.subTest(version=version), mock.patch.object(bench, "zlibng_version", return_value=version):
                code, out = self.run_with(GOOD)
                self.assertEqual(code, 2)
                self.assertIn("not zlib-ng", out)

    def test_refuses_without_the_executable(self):
        self.exe.unlink()
        code, out = self.run_with(GOOD)
        self.assertEqual(code, 2)
        self.assertIn("no test executable", out)

    def test_a_missing_or_skipped_result_fails(self):
        missing = GOOD.replace(line("flate/image-rows.bin", 53.3, 48_758), "")
        for output, exit_code in ((missing, 0), (GOOD.replace(PASSED, "[  SKIPPED ] 1 test\n"), 0), (GOOD, 1)):
            with self.subTest(output=output[-40:], exit_code=exit_code):
                code, _ = self.run_with(output, exit_code)
                self.assertEqual(code, 1)

    def test_slower_or_larger_fails(self):
        slow = GOOD.replace("median   19.9", "median   15.9")  # collisions at 0.52 of zlib-ng, under 0.54
        larger = GOOD.replace("360736 bytes", "363180 bytes")  # content one byte past zlib-ng
        for output, why in ((slow, "under 0.54"), (larger, "larger than zlib-ng")):
            with self.subTest(why=why):
                code, out = self.run_with(output)
                self.assertEqual(code, 1)
                self.assertIn(why, out)

    def test_a_result_on_other_input_fails(self):
        # The test executable read another corpus than the one zlib-ng was timed on: the comparison is refused.
        other = GOOD.replace(line("flate/image-rows.bin", 53.3, 48_758), line("flate/image-rows.bin", 53.3, 48_758, input_size=1))
        code, out = self.run_with(other)
        self.assertEqual(code, 1)
        self.assertIn("flate/image-rows.bin: the test compressed 1 bytes", out)

    def test_the_speed_test_reads_the_timed_corpus(self):
        # run_recto hands the executable the corpus this script reads, through the variable the tests honour.
        corpus = Path(self.tmp.name) / "corpus"
        with mock.patch.object(bench.subprocess, "run") as run:
            run.return_value = mock.Mock(returncode=0, stdout="")
            bench.run_recto(self.exe, corpus)
        self.assertEqual(run.call_args.kwargs["env"]["RECTO_CORPUS_DIR"], str(corpus.resolve()))

    def test_collisions_size_is_held_only_at_parity(self):
        # Collisions are larger than zlib-ng's at section 37 and must not be at section 39's parity, nor slower.
        self.assertEqual(self.run_with(GOOD)[0], 0)
        code, out = self.run_with(GOOD, 0, "--parity")
        self.assertEqual(code, 1)
        self.assertIn("flate/collisions.bin: 62161 bytes, larger", out)
        self.assertIn("of zlib-ng's speed, under 1.00", out)

    def test_parses_the_speed_test_lines(self):
        results = bench.recto_results(GOOD)
        size = (ROOT / "tests" / "corpus" / "flate" / "content-stream.txt").stat().st_size
        self.assertEqual(results["flate/content-stream.txt"], (47.4, size, 360_736))
        self.assertEqual(len(results), 3)  # Fast is not Default


if __name__ == "__main__":
    unittest.main()
