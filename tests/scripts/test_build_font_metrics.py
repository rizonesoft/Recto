"""Tests for scripts/build-font-metrics.py (D01 T02 section 27): the vendored AFM files match the sums in their README,
the generated table is current, a one-byte edit of an AFM is refused, and each kind of field the table carries reaches
it (an edited copy read without the sums no longer matches the committed table)."""
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
SPEC = importlib.util.spec_from_file_location("build_font_metrics", ROOT / "scripts" / "build-font-metrics.py")
metrics = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = metrics  # its dataclasses look their module up there
SPEC.loader.exec_module(metrics)

AFM_DIR = ROOT / "third_party" / "adobe-core14-afm"


def run(*args: str) -> tuple[int, str]:
    out = io.StringIO()
    with redirect_stdout(out), redirect_stderr(out):
        rc = metrics.main(list(args))
    return rc, out.getvalue()


class BuildFontMetrics(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.copy = Path(self.tmp.name) / "afm"
        shutil.copytree(AFM_DIR, self.copy)

    def edit(self, file: str, old: bytes, new: bytes) -> None:
        path = self.copy / file
        data = path.read_bytes()
        self.assertEqual(data.count(old), 1, f"{file}: {old!r} is not unique")
        path.write_bytes(data.replace(old, new))

    def test_vendored_files_match_their_sums(self):
        sums = metrics.readme_sums(AFM_DIR)
        self.assertEqual(len(sums), 15)  # fourteen AFM files and Adobe's notice
        for file, want in sums.items():
            self.assertEqual(hashlib.sha256((AFM_DIR / file).read_bytes()).hexdigest(), want, file)
        self.assertIn(b"may be used, copied, and distributed", (AFM_DIR / "MustRead.html").read_bytes())

    def test_table_is_current(self):
        rc, out = run("--check")
        self.assertEqual(rc, 0, out)

    def test_one_byte_edit_is_refused(self):
        # A comment byte: nothing the table carries, so only the sum can catch it.
        self.edit("Courier.afm", b"Comment Creation Date", b"Comment Creation Datf")
        rc, out = run("--check", "--afm-dir", str(self.copy))
        self.assertEqual(rc, 1)
        self.assertIn("SHA-256", out)

    def test_every_kind_of_field_reaches_the_table(self):
        edits = [
            ("width", "Helvetica.afm", b"C 65 ; WX 667 ; N A ;", b"C 65 ; WX 668 ; N A ;"),
            ("box", "Times-Roman.afm", b"N A ; B 15 0 706 674 ;", b"N A ; B 15 0 706 675 ;"),
            ("code", "Courier.afm", b"C 65 ; WX 600 ; N A ;", b"C -1 ; WX 600 ; N A ;"),
            ("name", "Symbol.afm", b"N Alpha ;", b"N Alphb ;"),
            ("cap height", "Courier-Bold.afm", b"CapHeight 562", b"CapHeight 563"),
            ("ascender", "Times-Bold.afm", b"Ascender 683", b"Ascender 684"),
            ("descender", "Helvetica-Bold.afm", b"Descender -207", b"Descender -208"),
            ("italic angle", "Times-Italic.afm", b"ItalicAngle -15.5", b"ItalicAngle -15.6"),
            ("fixed pitch", "Courier-Oblique.afm", b"IsFixedPitch true", b"IsFixedPitch false"),
            ("font box", "ZapfDingbats.afm", b"FontBBox -1 -143 981 820", b"FontBBox -1 -143 981 821"),
        ]
        for what, file, old, new in edits:
            with self.subTest(what):
                shutil.rmtree(self.copy)
                shutil.copytree(AFM_DIR, self.copy)
                self.edit(file, old, new)
                rc, out = run("--check", "--afm-dir", str(self.copy), "--skip-sums")
                self.assertEqual(rc, 1, f"{what}: {out}")
                self.assertIn("out of date", out)

    def test_malformed_records_are_refused(self):
        cases = [
            ("record without a width", "Courier.afm", b"C 65 ; WX 600 ; N A ;", b"C 65 ; N A ;", "exactly C, WX, N"),
            ("count off by one", "Symbol.afm", b"StartCharMetrics 190", b"StartCharMetrics 191", "says 191"),
            ("width past 16 bits", "Helvetica.afm", b"C 65 ; WX 667 ; N A ;", b"C 65 ; WX 66700 ; N A ;", "16 bits"),
        ]
        for what, file, old, new, said in cases:
            with self.subTest(what):
                shutil.rmtree(self.copy)
                shutil.copytree(AFM_DIR, self.copy)
                self.edit(file, old, new)
                rc, out = run("--check", "--afm-dir", str(self.copy), "--skip-sums")
                self.assertEqual(rc, 1, f"{what}: {out}")
                self.assertIn(said, out)


if __name__ == "__main__":
    unittest.main()
