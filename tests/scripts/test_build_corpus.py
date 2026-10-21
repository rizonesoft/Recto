import importlib.util
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

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

    def test_rejects_enumeration_errors_and_directory_links(self) -> None:
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as ext_tmp:
            root = Path(tmp)
            external = Path(ext_tmp)
            self.mod.generate_corpus(root)
            (external / "unlisted.bin").write_bytes(b"hidden")

            # 1. Root enumeration failure must fail check_corpus.
            with mock.patch("os.scandir", side_effect=PermissionError("denied at root")):
                ok, errors = self.mod.check_corpus(root)
                self.assertFalse(ok)
                self.assertTrue(
                    any("directory enumeration error in .:" in e for e in errors), errors
                )

            # 2. Nested directory enumeration failure must fail check_corpus.
            real_scandir = os.scandir

            def fail_on_render(path):
                if Path(path).name == "render":
                    raise PermissionError("denied in render")
                return real_scandir(path)

            with mock.patch("os.scandir", side_effect=fail_on_render):
                ok, errors = self.mod.check_corpus(root)
                self.assertFalse(ok)
                self.assertTrue(
                    any("directory enumeration error in render:" in e for e in errors), errors
                )

            # 3. Directory symlink or NTFS junction inside corpus must be rejected.
            link_path = root / "render" / "linked"
            try:
                os.symlink(external, link_path, target_is_directory=True)
            except OSError:
                subprocess.run(
                    ["cmd", "/c", "mklink", "/J", str(link_path), str(external)],
                    check=True,
                    capture_output=True,
                )
            try:
                ok, errors = self.mod.check_corpus(root)
                self.assertFalse(ok)
                self.assertTrue(
                    any(
                        "symlink or reparse point not allowed in corpus: render/linked" in e
                        for e in errors
                    ),
                    errors,
                )
            finally:
                try:
                    os.rmdir(link_path)
                except OSError:
                    link_path.unlink(missing_ok=True)

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

    def _one_page_pdf(self, content_obj: bytes) -> bytes:
        pdf = self.mod.ClassicPdfBuilder("test.pdf")
        pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
        pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
        pdf.set_obj(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>")
        pdf.set_obj(4, content_obj)
        return pdf.build(1)

    def test_a_stream_length_is_checked_whatever_the_dictionary_layout(self) -> None:
        self.mod.validate_pdf_structure("test.pdf", self._one_page_pdf(b"<<\n/Length 3\n>>\nstream\nq Q\nendstream"))
        for bad in (b"<<\n/Length 999\n>>\nstream\nq Q\nendstream", b"<< /Length 999 >>\nstream\nq Q\nendstream"):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, "is not followed by"):
                self.mod.validate_pdf_structure("test.pdf", self._one_page_pdf(bad))

    def test_only_the_stream_dictionarys_own_length_counts(self) -> None:
        for bad in (b"<< /Params << /Length 3 >> /Length 999 >>\nstream\nq Q\nendstream",
                    b"<< /Note (/Length 3) /Length 999 >>\nstream\nq Q\nendstream",
                    b"<< /Params << /Length 3 >> >>\nstream\nq Q\nendstream",
                    b"<< /Length 3 /Length 3 >>\nstream\nq Q\nendstream",
                    b"<< /Length 4 0 R >>\nstream\nq Q\nendstream"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.mod.validate_pdf_structure("test.pdf", self._one_page_pdf(bad))
        self.mod.validate_pdf_structure("test.pdf", self._one_page_pdf(b"<< /Params << /Length 999 >> /Length 3 >>\nstream\nq Q\nendstream"))

    def test_stream_dictionaries_parse_strings_and_refuse_escaped_keys(self) -> None:
        # A string holding << or >> is data, not structure.
        self.mod.validate_pdf_structure("test.pdf", self._one_page_pdf(b"<< /Length 3 /Note (<< x >>) >>\nstream\nq Q\nendstream"))
        # /Len#67th is /Length spelled with an escape; the corpus dialect refuses escaped keys.
        with self.assertRaisesRegex(ValueError, "not a plain name"):
            self.mod.validate_pdf_structure("test.pdf", self._one_page_pdf(b"<< /Length 3 /Len#67th 999 >>\nstream\nq Q\nendstream"))

    def test_xref_stream_declarations_are_the_ones_applied(self) -> None:
        data = self.mod.build_compressed_xref_2p()
        self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", data)
        xref_at = int(re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data).group(1))
        for old, new, reason in ((b"/Type /XRef /Size 11", b"/Type /XRef /Index [1 11] /Size 11", "/Index"),
                                 (b"/Filter /FlateDecode /DecodeParms", b"/Filter /ASCII85Decode /DecodeParms", "/Filter")):
            at = data.index(old)
            # The edit sits in the xref stream's own dictionary, the last object, so every object offset and the
            # startxref value stay right and only the declaration differs.
            self.assertGreater(at, xref_at)
            mutated = data[:at] + new + data[at + len(old):]
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, re.escape(reason)):
                self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", mutated)

    def test_only_the_corpus_dialect_passes_for_xref_streams_and_strings(self) -> None:
        data = self.mod.build_compressed_xref_2p()
        xref_at = int(re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", data).group(1))
        for old, new, reason in ((b"/W [1 2 2]", b"/Note (/W [1 2 2]) /W [1 1 3]", "keys the corpus never writes"),
                                 (b"/Predictor 12 /Columns 5", b"/Predictor 12 /Colors 3 /Columns 5", "/DecodeParms")):
            at = data.index(old)
            self.assertGreater(at, xref_at)  # inside the last object: no offset moves
            mutated = data[:at] + new + data[at + len(old):]
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, re.escape(reason)):
                self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", mutated)
        # A string may hold the endstream keyword; it is data, and the stream is still length-checked.
        self.mod.validate_pdf_structure(
            "test.pdf", self._one_page_pdf(b"<< /Length 3 /Note (\nendstream) >>\nstream\nq Q\nendstream"))

    def test_a_classic_xref_entry_has_one_form(self) -> None:
        data = self.mod.build_simple_1p()
        self.mod.validate_pdf_structure("render/simple-1p.pdf", data)
        xref = data.index(b"\nxref\n") + 1  # the table, not startxref
        first_in_use = data.index(b" 00000 n \n", xref) - 10
        for bad in (b"9999999999 00000 x \n", b"0000000015 00000 n  ", b"000000001x 00000 n \n"):
            mutated = data[:first_in_use] + bad + data[first_in_use + 20 :]
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, "malformed xref entry"):
                self.mod.validate_pdf_structure("render/simple-1p.pdf", mutated)

    def test_object_stream_offsets_must_locate_their_objects(self) -> None:
        self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", self.mod.build_compressed_xref_2p())
        real = self.mod.flate_compress

        def shifted(raw: bytes) -> bytes:  # the first object's offset now points inside its dictionary
            return real(b"1 9 2 " + raw[6:] if raw.startswith(b"1 0 2 ") else raw)

        with mock.patch.object(self.mod, "flate_compress", side_effect=shifted):
            data = self.mod.build_compressed_xref_2p()
        with self.assertRaisesRegex(ValueError, "do not locate its objects|is not one complete object"):
            self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", data)

    def test_fixture_descriptions_claim_only_what_the_fixtures_hold(self) -> None:
        import io
        import zipfile
        files = self.mod.expected_corpus_files()
        purposes = {k: v.lower() for k, v in self.mod.CORPUS_PURPOSES.items()}
        rotated = files["render/rotated-pages.pdf"]
        for box in ("bleed", "trim", "art"):
            if f"/{box.capitalize()}Box".encode() not in rotated:
                self.assertNotIn(box, purposes["render/rotated-pages.pdf"])
        docx = zipfile.ZipFile(io.BytesIO(files["convert/sample.docx"])).read("word/document.xml")
        for marker, word in ((b"<w:tbl", "table"), (b"<w:numPr", "list"), (b"w:pStyle", "heading")):
            if marker not in docx:
                self.assertNotIn(word, purposes["convert/sample.docx"])
        xlsx = zipfile.ZipFile(io.BytesIO(files["convert/sample.xlsx"]))
        if "xl/sharedStrings.xml" not in xlsx.namelist():
            self.assertNotIn("shared strings", purposes["convert/sample.xlsx"])
        if b"<f>" not in xlsx.read("xl/worksheets/sheet1.xml"):
            self.assertNotIn("formula", purposes["convert/sample.xlsx"])

    def test_encrypted_broken_seeds_and_readme(self) -> None:
        files = self.mod.expected_corpus_files()
        for enc_name in (
            "render/encrypted-rc4-128.pdf",
            "render/encrypted-aes-128.pdf",
            "render/encrypted-aes-256.pdf",
        ):
            self.assertIn(enc_name, files)
            self.mod.validate_encrypted_pdf_structure(enc_name, files[enc_name])

        broken_expected = {
            "broken/empty-0b.pdf",
            "broken/non-pdf-header.pdf",
            "broken/truncated-header.pdf",
            "broken/truncated-stream.pdf",
            "broken/corrupt-xref-recoverable.pdf",
            "broken/cyclic-xref-prev.pdf",
            "broken/cyclic-pages-tree.pdf",
            "broken/deep-nested-array-70.pdf",
            "broken/deflate-bomb-10kb.pdf",
        }
        self.assertTrue(broken_expected.issubset(files.keys()))
        for bname in sorted(broken_expected):
            self.mod.validate_broken_pdf_structure(bname, files[bname])

        seed_pdfs = [k for k in files if k.startswith("seeds/") and k.endswith(".pdf")]
        seed_bins = [k for k in files if k.startswith("seeds/") and k.endswith(".bin")]
        self.assertGreaterEqual(len(seed_pdfs), 6)
        self.assertEqual(len(seed_bins), 11)
        seen_selectors: set[int] = set()
        for sname in seed_pdfs:
            self.assertLess(len(files[sname]), 4096)
            self.mod.validate_seed_pdf_structure(sname, files[sname])
        for bname in seed_bins:
            self.assertLess(len(files[bname]), 4096)
            self.mod.validate_seed_filter_bin_structure(bname, files[bname])
            seen_selectors.add(files[bname][0])
        self.assertEqual(seen_selectors, set(range(11)))

        with self.assertRaisesRegex(ValueError, "selector byte"):
            self.mod.validate_seed_filter_bin_structure(
                "seeds/seed-filter-00-flate.bin",
                b"\xFF" + files["seeds/seed-filter-00-flate.bin"][1:],
            )

        readme = (self.mod.CORPUS_ROOT / "README.md").read_text(encoding="utf-8")
        self.assertIn("recto-test", readme)
        self.assertIn("recto-owner", readme)
        self.assertIn("PROVENANCE.md", readme)
        self.assertIn("seed-filter-00-flate.bin", readme)

        provenance = (self.mod.CORPUS_ROOT / "PROVENANCE.md").read_text(encoding="utf-8")
        self.assertIn("| Path | SHA-256 | License | Origin | Purpose |", provenance)
        for rel_path in files:
            self.assertIn(f"| {rel_path} | ", provenance)
            self.assertIn(" | CC0-1.0 | scripts/build-corpus.py | ", provenance)


if __name__ == "__main__":
    unittest.main()
