"""Tests for scripts/build-cff-data.py (D01 T02 section 29): the generated table is current, every check the
transcription must pass refuses a broken copy, and an edited copy reaches the table (it no longer matches the committed
one)."""
import importlib.util
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_cff_data", ROOT / "scripts" / "build-cff-data.py")
cff = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = cff
SPEC.loader.exec_module(cff)

DATA = ROOT / "src" / "pdf" / "pdf-encodings" / "cff-standard-data.txt"
TABLE = ROOT / "src" / "pdf" / "cff_data.inc"


def run(*args: str) -> tuple[int, str]:
    out = io.StringIO()
    with redirect_stdout(out), redirect_stderr(out):
        rc = cff.main(list(args))
    return rc, out.getvalue()


class BuildCffData(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.data = Path(self.tmp.name) / "cff-standard-data.txt"

    def check_edited(self, old: str, new: str) -> tuple[int, str]:
        text = DATA.read_text(encoding="utf-8")
        self.assertEqual(text.count(old), 1, f"{old!r} is not unique")
        self.data.write_text(text.replace(old, new), encoding="utf-8", newline="\n")
        return run("--check", "--data", str(self.data), "--out", str(TABLE))

    def test_table_is_current(self):
        rc, out = run("--check")
        self.assertEqual(rc, 0, out)

    def test_counts_and_names_are_checked(self):
        cases = [
            ("a standard string out of order", "string 34 A\nstring 35 B\n", "string 35 B\nstring 34 A\n", "out of order"),
            ("a standard string missing", "string 390 Semibold\n", "", "391 unique"),
            ("a standard string twice", "string 35 B\n", "string 35 A\n", "391 unique"),
            ("a charset glyph missing", "charset ExpertSubset 86 commainferior\n", "", "86"),
            ("a charset glyph with no SID", "charset Expert 2 exclamsmall\n", "charset Expert 2 nosuchglyph\n",
             "non-standard"),
            ("an encoding code twice", "encoding Expert 33 exclamsmall\n",
             "encoding Expert 33 exclamsmall\nencoding Expert 33 space\n", "repeated"),
            ("an unknown line", "string 1 space\n", "string 1 space\nglyph 1 space\n", "not a string"),
        ]
        for what, old, new, said in cases:
            with self.subTest(what):
                rc, out = self.check_edited(old, new)
                self.assertEqual(rc, 1, out)
                self.assertIn(said, out)

    def test_each_table_follows_the_transcription(self):
        # A valid edit to each table reads back as a different generated file.
        cases = [
            ("a standard string", "string 34 A\n", "string 34 Aprime\n"),
            ("charset", "charset Expert 2 exclamsmall\n", "charset Expert 2 space\n"),
            ("encoding", "encoding Expert 33 exclamsmall\n", "encoding Expert 33 space\n"),
        ]
        for what, old, new in cases:
            with self.subTest(what):
                rc, out = self.check_edited(old, new)
                self.assertEqual(rc, 1, out)
                self.assertIn("out of date", out)

    def test_generated_table_holds_the_data(self):
        text = TABLE.read_text(encoding="utf-8")
        self.assertIn("kCffStandardStrings = {", text)
        self.assertIn('".notdef", "space", "exclam"', text)
        self.assertIn("std::array<uint16_t, 165> kCffExpertCharset", text)
        self.assertIn("std::array<uint16_t, 86> kCffExpertSubsetCharset", text)


if __name__ == "__main__":
    unittest.main()
