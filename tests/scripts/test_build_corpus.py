import importlib.util
import os
import re
import struct
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

    def _objstm_variant(self, old: bytes, new: bytes) -> bytes:
        """compressed-xref-2p with old replaced by new in the object stream's dictionary only; the builder recomputes
        every outer offset."""
        real = self.mod.make_stream_obj

        def edited(extra_dict, payload, compress=True):
            out = real(extra_dict, payload, compress)
            return out.replace(old, new, 1) if b"/Type /ObjStm" in out else out

        with mock.patch.object(self.mod, "make_stream_obj", side_effect=edited):
            return self.mod.build_compressed_xref_2p()

    def test_object_streams_and_prev_pointers_are_read_from_their_own_entries(self) -> None:
        cases = [
            (self._objstm_variant(b"/First 32", b"/Note (/First 32) /First 0"), "/Type /ObjStm the corpus writes"),
            (self._objstm_variant(b"/Filter /FlateDecode", b"/Filter /ASCII85Decode"), "/Filter must be /FlateDecode"),
        ]
        incremental = self.mod.build_incremental_2rev()
        self.mod.validate_pdf_structure("render/incremental-2rev.pdf", incremental)
        prev = re.findall(rb"/Prev \d+", incremental)[-1]
        at = incremental.rindex(prev)
        cases.append((incremental[:at] + b"/Note (" + prev + b") /Prev 999999" + incremental[at + len(prev):],
                       "trailer has keys the corpus never writes"))
        compressed = self.mod.build_compressed_xref_2p()
        at = compressed.index(b"/Type /XRef") + len(b"/Type /XRef")
        cases.append((compressed[:at] + b" /Prev 999999" + compressed[at:], "does not locate an xref section"))
        for data, reason in cases:
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, re.escape(reason)):
                self.mod.validate_pdf_structure("render/test.pdf", data)

    def test_xref_offsets_name_only_objects_the_walk_parsed(self) -> None:
        fake = b"4 0 obj\n<< /Length 999 >>\nstream\nq Q\nendstream\nendobj\n"
        pdf = self.mod.ClassicPdfBuilder("test.pdf")
        pdf.set_obj(1, "<< /Type /Catalog /Pages 2 0 R >>")
        pdf.set_obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>")
        pdf.set_obj(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>")
        pdf.set_obj(4, b"<< /Length 3 >>\nstream\nq Q\nendstream")
        pdf.set_obj(5, b"<< /Length " + str(len(fake)).encode() + b" >>\nstream\n" + fake + b"\nendstream")
        data = pdf.build(1)
        self.mod.validate_pdf_structure("test.pdf", data)
        xref = data.index(b"\nxref\n") + 1
        self.assertTrue(data.startswith(b"xref\n0 6\n", xref))
        entry_4 = xref + len(b"xref\n0 6\n") + 20 * 4  # 20-byte entries for objects 0 to 5
        redirected = data[:entry_4] + f"{data.index(fake):010d}".encode() + data[entry_4 + 10:]
        with self.assertRaisesRegex(ValueError, "xref entry for 4 0"):
            self.mod.validate_pdf_structure("test.pdf", redirected)

    def test_an_object_stream_slot_keeps_dictionary_and_array_delimiters_apart(self) -> None:
        real = self.mod.flate_compress

        def bracketed(raw: bytes) -> bytes:  # equal length, so every offset in the stream header still lines up
            return real(raw.replace(b"<< /Type /Catalog", b"[  /Type /Catalog", 1))

        with mock.patch.object(self.mod, "flate_compress", side_effect=bracketed):
            data = self.mod.build_compressed_xref_2p()
        with self.assertRaisesRegex(ValueError, "is not one complete object"):
            self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", data)

    def test_the_xref_chain_locates_every_object_and_sizes_it(self) -> None:
        d = self.mod.build_simple_1p()
        x = d.index(b"\nxref\n") + 1
        t = d.index(b"trailer\n", x)
        size = re.search(rb"/Size \d+", d[t:]).group(0)
        for mutated, reason in ((d[:x] + b"xref\n0 1\n0000000000 65535 f \n" + d[t:],
                                 "is not what the cross-reference chain locates"),
                                (d[:t] + d[t:].replace(size, b"/Size 0", 1), "newest /Size 0")):
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, re.escape(reason)):
                self.mod.validate_pdf_structure("render/simple-1p.pdf", mutated)

    def test_every_object_follows_the_pdf_value_grammar(self) -> None:
        real = self.mod.flate_compress
        for bad in (b"<< 12345 /Catalog", b"<< /Type XXXXXXXX"):
            def edited(raw: bytes, bad=bad) -> bytes:  # equal length: every offset in the stream header lines up
                return real(raw.replace(b"<< /Type /Catalog", bad, 1))

            with mock.patch.object(self.mod, "flate_compress", side_effect=edited):
                data = self.mod.build_compressed_xref_2p()
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, "is not one complete object"):
                self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", data)
        for value in (b"<< /K 12 0 R /A [1 2.5 -3 .5] /B (s) /C <0aF> /D true /E null >>", b"[ /A 1 0 R ]"):
            self.assertEqual(self.mod._skip_object(value, 0), len(value))
        for value in (b"<< /K >>", b"<< 1 2 >>", b"[ R ]", b"<zz>", b"tru"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.mod._skip_object(value, 0)

    def test_a_newer_free_entry_and_a_repeated_key_refuse(self) -> None:
        d = self.mod.build_incremental_2rev()
        t = d.rfind(b"trailer\n")
        with self.assertRaisesRegex(ValueError, "is not what the cross-reference chain locates"):
            self.mod.validate_pdf_structure("render/incremental-2rev.pdf", d[:t] + b"1 1\n0000000000 65535 f \n" + d[t:])
        real = self.mod.flate_compress

        def repeated(raw: bytes) -> bytes:  # equal length: every offset in the stream header lines up
            return real(raw.replace(b"/Type /Catalog /Pages", b"/Type /Catalog /Type ", 1))

        with mock.patch.object(self.mod, "flate_compress", side_effect=repeated):
            data = self.mod.build_compressed_xref_2p()
        with self.assertRaisesRegex(ValueError, "is not one complete object"):
            self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", data)
        with self.assertRaisesRegex(ValueError, "appears twice"):
            self.mod._skip_object(b"<< /A 1 /A 2 >>", 0)

    def test_the_root_resolves_to_a_live_catalog(self) -> None:
        d = self.mod.build_simple_1p()
        with self.assertRaisesRegex(ValueError, "does not locate"):
            self.mod.validate_pdf_structure("render/simple-1p.pdf", d.replace(b"/Root 1 0 R", b"/Root 9 0 R"))
        with self.assertRaisesRegex(ValueError, "is not a /Type /Catalog"):
            self.mod.validate_pdf_structure("render/simple-1p.pdf", d.replace(b"/Root 1 0 R", b"/Root 2 0 R"))
        real = self.mod.flate_png_up_compress

        def freed_catalog(raw, columns):  # the xref stream's entry for object 1 becomes free
            rows = bytearray(raw)
            rows[5:10] = struct.pack(">BHH", 0, 0, 65535)
            return real(bytes(rows), columns)

        with mock.patch.object(self.mod, "flate_png_up_compress", side_effect=freed_catalog):
            data = self.mod.build_compressed_xref_2p()
        with self.assertRaisesRegex(ValueError, "does not locate"):
            self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", data)

    def test_every_object_is_generation_zero(self) -> None:
        d = self.mod.build_simple_1p()
        x = d.index(b"\nxref\n") + 1
        entry_1 = x + len(b"xref\n0 ") + len(re.match(rb"\d+\n", d[x + len(b"xref\n0 "):]).group(0)) + 20
        self.assertEqual(d[entry_1 + 11 : entry_1 + 16], b"00000")
        mutated = d.replace(b"\n1 0 obj\n", b"\n1 1 obj\n", 1)
        mutated = mutated[:entry_1 + 11] + b"00001" + mutated[entry_1 + 16:]
        with self.assertRaisesRegex(ValueError, "generation 1; the corpus writes 0 only"):
            self.mod.validate_pdf_structure("render/simple-1p.pdf", mutated)

    def test_an_xref_stream_update_takes_precedence_over_the_section_it_points_back_to(self) -> None:
        base = self.mod.build_simple_1p()
        prev = int(re.search(rb"startxref\n(\d+)\n%%EOF\n\Z", base).group(1))
        headers = {int(m.group(1)): m.start() + 1 for m in re.finditer(rb"\n(\d+) 0 obj\n", base)}
        new_at = len(base)
        rows = struct.pack(">BHH", 0, 0, 65535) + b"".join(
            struct.pack(">BHH", 1, headers[n], 0) for n in range(1, max(headers) + 1))
        rows += struct.pack(">BHH", 1, new_at, 0)
        size = max(headers) + 2
        payload = self.mod.flate_png_up_compress(rows, 5)
        update = (f"{size - 1} 0 obj\n<< /Type /XRef /Size {size} /Root 1 0 R /Prev {prev} /W [1 2 2] "
                  f"/Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 5 >> /Length {len(payload)} >>\n"
                  "stream\n").encode() + payload + f"\nendstream\nendobj\nstartxref\n{new_at}\n%%EOF\n".encode()
        # The newest section says /Size 8; the classic section it points back to says 7.
        self.mod.validate_pdf_structure("test.pdf", base + update)

    def test_classic_free_entries_are_object_zeros_form(self) -> None:
        d = self.mod.build_simple_1p()
        self.assertIn(b"0000000000 65535 f \n", d)
        for bad in (b"0000000000 00000 f \n", b"9999999999 65535 f \n"):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, "must be free with offset 0"):
                self.mod.validate_pdf_structure("render/simple-1p.pdf", d.replace(b"0000000000 65535 f \n", bad, 1))

    def test_object_zero_is_listed_and_points_nowhere(self) -> None:
        d = self.mod.build_simple_1p()
        size = int(re.search(rb"/Size (\d+)", d).group(1))
        without_zero = d.replace(f"xref\n0 {size}\n0000000000 65535 f \n".encode(), f"xref\n1 {size - 1}\n".encode(), 1)
        self.assertNotEqual(without_zero, d)
        with self.assertRaisesRegex(ValueError, "no free entry for object 0"):
            self.mod.validate_pdf_structure("render/simple-1p.pdf", without_zero)
        real = self.mod.flate_png_up_compress

        def next_free(raw, columns):
            return real(struct.pack(">BHH", 0, 65535, 65535) + raw[5:], columns)

        with mock.patch.object(self.mod, "flate_png_up_compress", side_effect=next_free):
            data = self.mod.build_compressed_xref_2p()
        with self.assertRaisesRegex(ValueError, "must be free with next 0"):
            self.mod.validate_pdf_structure("render/compressed-xref-2p.pdf", data)

    def test_an_object_appears_once_in_a_section(self) -> None:
        d = self.mod.build_simple_1p()
        t = d.index(b"trailer\n")
        with self.assertRaisesRegex(ValueError, "appears twice in the xref section"):
            self.mod.validate_pdf_structure("render/simple-1p.pdf", d[:t] + b"1 1\n0000000000 65535 f \n" + d[t:])
        self.mod.validate_pdf_structure("render/incremental-2rev.pdf", self.mod.build_incremental_2rev())

    def test_a_subsection_header_is_two_non_negative_numbers(self) -> None:
        data = self.mod.build_simple_1p()
        at = data.index(b"trailer\n")
        for subsection in (b"7 -1\n", b"-1 1\n0000000000 65535 f \n", b"+7 0\n", b"7  0\n"):
            with self.subTest(subsection=subsection), self.assertRaisesRegex(ValueError, "invalid xref subsection header"):
                self.mod.validate_pdf_structure("render/simple-1p.pdf", data[:at] + subsection + data[at:])

    def test_the_trailer_is_followed_directly_by_startxref(self) -> None:
        data = self.mod.build_simple_1p()
        bad = data.replace(b"\nstartxref\n", b"\nUNPARSED_GARBAGE\nstartxref\n")
        self.assertNotEqual(bad, data)
        with self.assertRaisesRegex(ValueError, "not followed directly by startxref"):
            self.mod.validate_pdf_structure("render/simple-1p.pdf", bad)

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
