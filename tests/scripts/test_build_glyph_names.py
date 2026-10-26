"""Tests for scripts/build-glyph-names.py (D01 T02 section 28): the vendored Glyph List files match their sums, the
generated table is current, a one-byte edit is refused, every Annex D rule the transcription keeps is checked, and each
kind of field reaches the table (an edited copy read without the sums no longer matches the committed table)."""
import hashlib
import importlib.util
import io
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_glyph_names", ROOT / "scripts" / "build-glyph-names.py")
glyphs = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = glyphs  # its dataclasses and annotations look their module up there
SPEC.loader.exec_module(glyphs)

PDF_DIR = ROOT / "src" / "pdf"


def run(*args: str) -> tuple[int, str]:
    out = io.StringIO()
    with redirect_stdout(out), redirect_stderr(out):
        rc = glyphs.main(list(args))
    return rc, out.getvalue()


class BuildGlyphNames(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.pdf = Path(self.tmp.name) / "pdf"
        for sub in ("adobe-glyph-list", "pdf-encodings", "adobe-core14-afm"):
            shutil.copytree(PDF_DIR / sub, self.pdf / sub)

    def edit(self, relative: str, old: bytes, new: bytes) -> None:
        path = self.pdf / relative
        data = path.read_bytes()
        self.assertEqual(data.count(old), 1, f"{relative}: {old!r} is not unique")
        path.write_bytes(data.replace(old, new))

    def check_copy(self, *extra: str) -> tuple[int, str]:
        return run("--check", "--pdf-dir", str(self.pdf), *extra)

    def test_vendored_files_match_their_sums(self):
        readme = (PDF_DIR / "adobe-glyph-list" / "README.md").read_text(encoding="utf-8")
        for file in ("glyphlist.txt", "zapfdingbats.txt"):
            digest = hashlib.sha256((PDF_DIR / "adobe-glyph-list" / file).read_bytes()).hexdigest()
            self.assertIn(f"| `{file}` | `{digest}` |", readme)
            self.assertIn(b"Copyright 2002-2019 Adobe", (PDF_DIR / "adobe-glyph-list" / file).read_bytes())

    def test_table_is_current(self):
        rc, out = run("--check")
        self.assertEqual(rc, 0, out)

    def test_one_byte_edit_is_refused(self):
        self.edit("adobe-glyph-list/glyphlist.txt", b"# Name:          Adobe Glyph List", b"# Name:          Adobe Glyph Lisu")
        rc, out = self.check_copy()
        self.assertEqual(rc, 1)
        self.assertIn("SHA-256", out)

    def test_annex_d_rules_are_checked(self):
        cases = [
            ("WinAnsi space at 0240", b"WinAnsiEncoding 240 space\n", b"WinAnsiEncoding 240 nbspace\n", "0240"),
            ("WinAnsi bullet at 0201", b"WinAnsiEncoding 201 bullet\n", b"", "bullet"),
            ("MacRoman count", b"MacRomanEncoding 312 space\n", b"", "208 codes"),
            ("Standard against the AFM", b"StandardEncoding 101 A\n", b"StandardEncoding 101 B\n", "Times-Roman.afm"),
            ("Symbol against the AFM", b"Symbol 101 Alpha\n", b"", "Symbol.afm"),
            ("MacOSRoman extras", b"MacOSRoman 360 apple\n", b"", "fifteen"),
            ("a name no list has", b"MacExpertEncoding 041 exclamsmall\n", b"MacExpertEncoding 041 nosuchglyph\n",
             "neither list"),
            ("a code twice", b"WinAnsiEncoding 101 A\n", b"WinAnsiEncoding 101 A\nWinAnsiEncoding 101 A\n", "twice"),
        ]
        for what, old, new, said in cases:
            with self.subTest(what):
                shutil.rmtree(self.pdf / "pdf-encodings")
                shutil.copytree(PDF_DIR / "pdf-encodings", self.pdf / "pdf-encodings")
                self.edit("pdf-encodings/annex-d.txt", old, new)
                rc, out = self.check_copy()
                self.assertEqual(rc, 1, f"{what}: {out}")
                self.assertIn(said, out)

    def test_every_kind_of_field_reaches_the_table(self):
        edits = [
            ("an encoding's name", "pdf-encodings/annex-d.txt", b"MacExpertEncoding 041 exclamsmall\n",
             b"MacExpertEncoding 041 Hungarumlautsmall\n"),
            ("an AGL scalar", "adobe-glyph-list/glyphlist.txt", b"\nAE;00C6\n", b"\nAE;00C7\n"),
            ("an AGL name", "adobe-glyph-list/glyphlist.txt", b"\nAEacute;01FC\n", b"\nAEacutf;01FC\n"),
            ("a ZapfDingbats scalar", "adobe-glyph-list/zapfdingbats.txt", b"\na1;2701\n", b"\na1;2702\n"),
            ("a Macintosh name", "pdf-encodings/mac-glyph-names.txt", b"\nnonmarkingreturn\n", b"\nnonmarkingreturm\n"),
        ]
        for what, file, old, new in edits:
            with self.subTest(what):
                for sub in ("adobe-glyph-list", "pdf-encodings"):
                    shutil.rmtree(self.pdf / sub)
                    shutil.copytree(PDF_DIR / sub, self.pdf / sub)
                self.edit(file, old, new)
                rc, out = self.check_copy("--skip-sums")
                self.assertEqual(rc, 1, f"{what}: {out}")
                self.assertIn("out of date", out)


if __name__ == "__main__":
    unittest.main()
