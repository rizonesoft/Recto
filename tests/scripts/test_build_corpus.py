import importlib.util
from pathlib import Path
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "build-corpus.py"


def load_build_corpus():
    spec = importlib.util.spec_from_file_location("build_corpus", SCRIPT_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BuildCorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_build_corpus()

    def test_deterministic_double_generation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp1, tempfile.TemporaryDirectory() as tmp2:
            r1 = Path(tmp1)
            r2 = Path(tmp2)
            h1 = self.mod.generate_corpus(r1)
            h2 = self.mod.generate_corpus(r2)
            self.assertEqual(h1, h2)
            for rel_path in h1:
                self.assertEqual((r1 / rel_path).read_bytes(), (r2 / rel_path).read_bytes())

    def test_live_corpus_check_passes(self) -> None:
        ok, errors = self.mod.check_corpus(self.mod.CORPUS_ROOT)
        self.assertTrue(ok, f"live corpus check failed: {errors}")
        self.assertEqual(errors, [])

    def test_detects_missing_mutated_and_unexpected_files(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.mod.generate_corpus(root)
            ok, errors = self.mod.check_corpus(root)
            self.assertTrue(ok, errors)

            # Mutate a stream /Length in simple-1p.pdf
            simple = root / "render" / "simple-1p.pdf"
            orig = simple.read_bytes()
            corrupted = orig.replace(b"/Length ", b"/Length 9", 1)
            self.mod.write_atomic(simple, corrupted)
            ok, errors = self.mod.check_corpus(root)
            self.assertFalse(ok)
            self.assertTrue(any("not followed by '\\nendstream'" in e for e in errors), errors)
            self.assertTrue(any("byte mismatch for render/simple-1p.pdf" in e for e in errors), errors)

            # Restore simple-1p.pdf, add an unexpected file, and delete scanned.pdf
            self.mod.write_atomic(simple, orig)
            (root / "text" / "scanned.pdf").unlink()
            self.mod.write_atomic(root / "render" / "stray.pdf", b"%PDF-1.7\n")
            ok, errors = self.mod.check_corpus(root)
            self.assertFalse(ok)
            self.assertIn("missing corpus file: text/scanned.pdf", errors)
            self.assertIn("unexpected corpus file: render/stray.pdf", errors)

    def test_content_semantics_and_xref_gen65535(self) -> None:
        with self.assertRaisesRegex(ValueError, "unterminated"):
            self.mod.validate_content_stream_syntax("test.pdf", b"BT 72 720 Td (unclosed ET")

        with self.assertRaisesRegex(ValueError, "outside page bounds"):
            self.mod.validate_content_stream_syntax(
                "test.pdf", b"BT 72 730 Td (A) Tj 180 680 Td (OffPage) Tj ET"
            )

        files = self.mod.expected_corpus_files()
        self.assertIn(b"/DR << /Font << /F1 5 0 R /ZaDb 10 0 R >> >>", files["convert/tables-and-forms.pdf"])
        self.assertIn(b"/AP << /N << /Yes 12 0 R /Off 13 0 R >> >>", files["convert/tables-and-forms.pdf"])
        self.assertIn(b"/W [1 2 2]", files["render/compressed-xref-2p.pdf"])


if __name__ == "__main__":
    unittest.main()
