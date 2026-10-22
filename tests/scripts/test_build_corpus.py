import importlib.util
import os
import re
import struct
from pathlib import Path
import subprocess
import tempfile
import unittest
import zlib
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

    def test_every_byte_belongs_to_an_object_or_a_revision_ending(self) -> None:
        classic = self.mod.build_simple_1p()
        compressed = self.mod.build_compressed_xref_2p()
        for name, bad in (
            ("second startxref", classic.replace(b"\nstartxref\n", b"\nstartxref\n0\n%%EOF\nGARBAGE\nstartxref\n")),
            ("xref stream garbage", compressed.replace(b"\nstartxref\n", b"\nUNPARSED_GARBAGE\nstartxref\n")),
            ("bytes between objects", classic.replace(b"endobj\n2 0 obj", b"endobj\nnoise\n2 0 obj", 1)),
        ):
            self.assertNotEqual(bad, classic if name != "xref stream garbage" else compressed)
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.mod.validate_pdf_structure("render/test.pdf", bad)

    def test_each_earlier_revision_is_valid_on_its_own(self) -> None:
        data = self.mod.build_incremental_2rev()
        self.mod.validate_pdf_structure("render/incremental-2rev.pdf", data)
        first_eof = data.index(b"%%EOF\n") + len(b"%%EOF\n")
        # Revision 1's entry for an object revision 2 redefines, pointed at revision 2's copy: a real header, but one
        # revision 1 cannot contain.
        later = re.search(rb"\n(\d+) 0 obj\n", data[first_eof - 1 :])
        num, later_at = int(later.group(1)), first_eof - 1 + later.start() + 1
        table = data.index(b"\nxref\n") + 1
        subsection = re.match(rb"xref\n0 (\d+)\n", data[table:])
        at = table + subsection.end() + 20 * num
        retargeted = data[:at] + f"{later_at:010d}".encode() + data[at + 10 :]
        for bad in (data.replace(b"/Size 7", b"/Size 0", 1), data.replace(b"/Root 1 0 R", b"/Root 9 0 R", 1), retargeted):
            self.assertNotEqual(bad, data)
            self.assertEqual(bad[first_eof:], data[first_eof:])  # each edit is in the first revision
            self.assertEqual(len(bad[:first_eof]), first_eof)
            with self.subTest(bad=bad[:first_eof][-80:]), self.assertRaisesRegex(ValueError, "revision ending at byte"):
                self.mod.validate_pdf_structure("render/incremental-2rev.pdf", bad)

    def test_earlier_revisions_are_validated_once_each(self) -> None:
        data = self.mod.build_incremental_2rev()
        real = self.mod.validate_pdf_structure
        calls = []

        def counted(rel_path, data, earlier_revisions=True):
            calls.append(earlier_revisions)
            return real(rel_path, data, earlier_revisions)

        with mock.patch.dict(real.__globals__, {"validate_pdf_structure": counted}):
            real("render/incremental-2rev.pdf", data)
        self.assertEqual(calls, [False])  # one earlier revision, validated once, without recursing again

    def test_encryption_is_read_from_what_the_trailer_names(self) -> None:
        files = self.mod.expected_corpus_files()
        aes = files["render/encrypted-aes-128.pdf"]
        self.mod.validate_encrypted_pdf_structure("render/encrypted-aes-128.pdf", aes)
        for old, new in ((b"/Encrypt 7 0 R", b"/Encrypt 6 0 R"), (b"/CFM /AESV2", b"/CFM /V2   "),
                         (b"/Length 128 /P", b"/Length 040 /P"),
                         (b"/EncryptMetadata true /CF", b"/EncryptMetadata false/CF")):  # same length: no layout error
            self.assertIn(old, aes)
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.mod.validate_encrypted_pdf_structure("render/encrypted-aes-128.pdf", aes.replace(old, new, 1))

    def test_rc4_and_aes_256_declare_the_key_they_derive(self) -> None:
        files = self.mod.expected_corpus_files()
        rc4, aes256 = files["render/encrypted-rc4-128.pdf"], files["render/encrypted-aes-256.pdf"]
        for name, data, old, new, says in (
                ("render/encrypted-rc4-128.pdf", rc4, b"/Length 128", b"/Length 040", "/Length 040"),
                ("render/encrypted-aes-256.pdf", aes256, b"/EncryptMetadata true /CF", b"/EncryptMetadata false/CF",
                 "/EncryptMetadata must be true")):
            self.mod.validate_encrypted_pdf_structure(name, data)
            self.assertIn(old, data)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, re.escape(says)):
                self.mod.validate_encrypted_pdf_structure(name, data.replace(old, new, 1))

    def test_the_objects_around_the_encryption_have_one_form(self) -> None:
        name = "render/encrypted-rc4-128.pdf"
        data = self.mod.expected_corpus_files()[name]
        for old, new, says in ((b"/FlateDecode", b"/DCTDecode  ", "/Filter /FlateDecode"),
                               (b"/Type /Catalog", b"/Type /INVALID", "object 1"),
                               (b"/BaseFont /Helvetica", b"/BaseFont /Courier  ", "object 5")):
            self.assertIn(old, data)
            with self.subTest(old=old), self.assertRaisesRegex(ValueError, re.escape(says)):
                self.mod.validate_encrypted_pdf_structure(name, data.replace(old, new, 1))

    def test_encrypted_strings_and_the_promised_revision_are_proved(self) -> None:
        files = self.mod.expected_corpus_files()
        aes256 = files["render/encrypted-aes-256.pdf"]
        start = aes256.index(b"/Title <") + 8
        end = aes256.index(b">", start)
        zeroed = aes256[:start] + b"0" * (end - start) + aes256[end:]
        with self.assertRaisesRegex(ValueError, "object 6 does not decrypt"):
            self.mod.validate_encrypted_pdf_structure("render/encrypted-aes-256.pdf", zeroed)
        with self.assertRaisesRegex(ValueError, "not the revision this fixture's path promises"):
            self.mod.validate_encrypted_pdf_structure("render/encrypted-aes-256.pdf",
                                                      files["render/encrypted-rc4-128.pdf"])

    def test_the_recoverable_fixture_is_the_one_page_document_exactly(self) -> None:
        name = "broken/corrupt-xref-recoverable.pdf"
        data = self.mod.expected_corpus_files()[name]
        self.mod.validate_broken_pdf_structure(name, data)
        for old, new, says in ((b"<< /Type /Catalog /Pages 2 0 R >>", b"null", "object 1"),
                               (b"/Info 6 0 R", b"/Info 5 0 R", "the tail")):
            self.assertIn(old, data)
            with self.subTest(old=old), self.assertRaisesRegex(ValueError, re.escape(says)):
                self.mod.validate_broken_pdf_structure(name, data.replace(old, new, 1))

    def test_encrypted_fixtures_get_the_full_cross_reference_checks(self) -> None:
        name = "render/encrypted-rc4-128.pdf"
        data = self.mod.expected_corpus_files()[name]
        xref = data.rindex(b"xref\n0 8\n") + len(b"xref\n0 8\n")
        entry_1 = xref + 20  # the second 20-byte entry: object 1, the catalog
        freed = data[:entry_1] + b"0000000000 65535 f \n" + data[entry_1 + 20 :]
        for mutated, says in ((freed, "object 1"), (data.replace(b"/Size 8", b"/Size 1", 1), "/Size")):
            with self.subTest(says=says), self.assertRaisesRegex(ValueError, re.escape(says)):
                self.mod.validate_encrypted_pdf_structure(name, mutated)

    def test_validator_inflation_is_bounded_and_exact(self) -> None:
        inflate = self.mod.inflate
        self.assertEqual(inflate(zlib.compress(b"abc"), "x"), b"abc")
        for data, says in ((zlib.compress(b" " * (self.mod.VALIDATOR_INFLATE_LIMIT + 1)), "inflates past"),
                           (zlib.compress(b"abc")[:-3], "incomplete"),
                           (zlib.compress(b"abc") + b"junk", "after the end"),
                           (b"not flate", "bad Flate data")):
            with self.subTest(says=says), self.assertRaisesRegex(ValueError, says):
                inflate(data, "x")
        # An encrypted fixture whose content inflates past the ceiling refuses instead of being inflated whole.
        real = self.mod.flate_compress
        with mock.patch.object(self.mod, "flate_compress",
                               side_effect=lambda b: real(b" " * (self.mod.VALIDATOR_INFLATE_LIMIT + 1))):
            big = self.mod.build_encrypted_rc4_128()
        with self.assertRaisesRegex(ValueError, "inflates past"):
            self.mod.validate_encrypted_pdf_structure("render/encrypted-rc4-128.pdf", big)

    def test_the_recoverable_fixture_starts_with_its_exact_header(self) -> None:
        name = "broken/corrupt-xref-recoverable.pdf"
        data = self.mod.expected_corpus_files()[name]
        for old, new, says in ((b"%PDF-1.7", b"NOTAPDF!", "exact %PDF-1.7 header"),
                               (b"endobj\n2 0 obj", b"endobj\n\n2 0 obj", "")):
            self.assertIn(old, data)
            with self.subTest(old=old), self.assertRaisesRegex(ValueError, re.escape(says)):
                self.mod.validate_broken_pdf_structure(name, data.replace(old, new, 1))

    def test_broken_fixtures_carry_their_fault_as_parsed_objects(self) -> None:
        # Each mutation goes through the generator, so offsets stay valid and only the fault's own form changes.
        real = self.mod.ClassicPdfBuilder.set_obj

        def regenerate(build, num, old, new):
            def set_obj(builder, obj_num, content):
                if obj_num == num:
                    text = content.decode("latin-1") if isinstance(content, bytes) else content
                    self.assertIn(old, text)
                    content = text.replace(old, new, 1)
                return real(builder, obj_num, content)
            with mock.patch.object(self.mod.ClassicPdfBuilder, "set_obj", set_obj):
                return build()

        nested = "[" * 70 + "42" + "]" * 70
        cases = (("broken/deep-nested-array-70.pdf", self.mod.build_broken_deep_nested_array_70, 6,
                  nested, f"({nested})"),
                 ("broken/cyclic-pages-tree.pdf", self.mod.build_broken_cyclic_pages_tree, 3,
                  "/Kids [2 0 R]", "/Kids [3 0 R]"))
        for name, build, num, old, new in cases:
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, f"object {num}"):
                self.mod.validate_broken_pdf_structure(name, regenerate(build, num, old, new))
        truncated = self.mod.expected_corpus_files()["broken/truncated-stream.pdf"]
        with self.assertRaisesRegex(ValueError, "truncated-stream"):
            self.mod.validate_broken_pdf_structure("broken/truncated-stream.pdf",
                                                   truncated.replace(b"/Length 512", b"/Length 999", 1))

    def test_each_seed_proves_its_own_construct(self) -> None:
        files = self.mod.expected_corpus_files()
        seeds = sorted(k for k in files if k.startswith("seeds/") and k.endswith(".pdf"))
        self.assertEqual(len(seeds), 6)
        for name in seeds:
            self.mod.validate_seed_pdf_structure(name, files[name])
            for other in seeds:
                if other != name:
                    with self.subTest(name=name, standing_in=other), self.assertRaises(ValueError):
                        self.mod.validate_seed_pdf_structure(name, files[other])

    def test_content_operators_skip_what_operands_contain(self) -> None:
        ops = self.mod.content_operators("x", b"q 1 0 0 1 5 5 cm BT /F1 12 Tf (re f BT \\) ET) Tj <414243> Tj "
                                              b"[(a) -40 (b)] TJ ET % m l c\n<< /K 1 >> BDC EMC Q")
        self.assertEqual(ops, [b"q", b"cm", b"BT", b"Tf", b"Tj", b"Tj", b"TJ", b"ET", b"BDC", b"EMC", b"Q"])
        with self.assertRaisesRegex(ValueError, "unterminated string"):
            self.mod.content_operators("x", b"BT (open Tj ET")

    def test_seed_content_must_be_executable_as_written(self) -> None:
        check = self.mod.validate_content_instructions
        res = {b"/Font": {b"/F1"}, b"/XObject": {b"/Fm1"}, b"/ExtGState": {b"/GS1"}}
        self.assertEqual(check("x", b"q /GS1 gs 1 0 0 rg 0 0 10 10 re W* n BT /F1 12 Tf [(a) -4 (b)] TJ ET /Fm1 Do Q",
                               res)[:3], [b"q", b"gs", b"rg"])
        for content, says in ((b" ".join(sorted(self.mod.SEED_RENDER_OPERATORS[4])), ""),
                              (b"1 0 0 1 0 0 cm 12 Tf", "Tf takes operands"),
                              (b"/Fm2 Do", "resources lack"),
                              (b"(x) Tj", "outside BT"),
                              (b"10 10 m 1 0 0 rg 20 20 l S", "inside a path"),
                              (b"q BT ET", "ends inside"),
                              (b"BT /F1 12 Tf [/Invalid] TJ ET", "TJ takes operands"),
                              (b"[3 /x] 0 d", "d takes operands"),
                              (b"1.5 0 0 rg", "from 0 to 1"),
                              (b"3 J", "0, 1 or 2"),
                              (b"[0 0] 0 d", "not all zero"),
                              (b"BT (a) Tj /F1 12 Tf ET", "before any Tf"),
                              (b"q BT /F1 12 Tf ET Q BT (a) Tj ET", "before any Tf"),  # Q restored no font
                              (b"BT /F1 12 Tf q (a) Tj Q ET", "inside BT"),
                              (b"BT /Fm1 Do ET", "inside BT"),
                              (b"q Q EMC", "not one the corpus content uses"),
                              (b"/Tag BMC q Q EMC", "not one the corpus content uses"),
                              (b"0 0 m W 10 10 l n", "only a painting operator"),
                              (b"0 0 m W W* n", "only a painting operator"),
                              (b"W n", "no path to clip")):
            with self.subTest(content=content), self.assertRaisesRegex(ValueError, says):
                check("x", content, res)
        # The same regression through the generator: the render seed's page content reduced to bare operator names.
        real = self.mod.flate_compress
        bare = b" ".join(sorted(self.mod.SEED_RENDER_OPERATORS[4]))
        with mock.patch.object(self.mod, "flate_compress",
                               side_effect=lambda b: real(bare if b"Seed 6: Render Ops" in b else b)):
            seed = self.mod.build_seed_render_ops()
        with self.assertRaises(ValueError):
            self.mod.validate_seed_pdf_structure("seeds/seed-render-ops.pdf", seed)

    def test_encrypted_and_broken_content_must_be_executable_too(self) -> None:
        real = self.mod.flate_compress
        stray = mock.patch.object(self.mod, "flate_compress", side_effect=lambda b: real(
            b + b"\nEMC\n" if b"Tj" in b else b))
        cases = (("render/encrypted-rc4-128.pdf", self.mod.build_encrypted_rc4_128,
                  self.mod.validate_encrypted_pdf_structure),
                 ("broken/corrupt-xref-recoverable.pdf", self.mod.build_broken_corrupt_xref_recoverable,
                  self.mod.validate_broken_pdf_structure),
                 ("broken/deep-nested-array-70.pdf", self.mod.build_broken_deep_nested_array_70,
                  self.mod.validate_broken_pdf_structure))
        for name, build, validate in cases:
            validate(name, build())
            with stray:
                mutated = build()
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "not one the corpus content uses"):
                validate(name, mutated)

    def test_encrypted_permissions_are_the_documented_mask(self) -> None:
        for mask, says in ((0, "reserved permission bits"), (-1092, "reserved permission bits"),
                           (-4, "not the documented -1028")):
            with self.subTest(mask=mask), mock.patch.object(self.mod, "CORPUS_PERMISSIONS_MASK", mask):
                data = self.mod.build_encrypted_rc4_128()
            with self.subTest(mask=mask), self.assertRaisesRegex(ValueError, says):
                self.mod.validate_encrypted_pdf_structure("render/encrypted-rc4-128.pdf", data)

    def test_the_documented_contract_matches_the_readme(self) -> None:
        readme = (Path(__file__).resolve().parents[2] / "tests" / "corpus" / "README.md").read_text(encoding="utf-8")
        for stated in (f"**User password:** `{self.mod.DOCUMENTED_USER_PASSWORD.decode()}`",
                       f"**Owner password:** `{self.mod.DOCUMENTED_OWNER_PASSWORD.decode()}`",
                       f"`/P {self.mod.DOCUMENTED_PERMISSIONS}`", f"`{self.mod.DOCUMENTED_PDF_DATE}`",
                       f"`< {self.mod.DOCUMENTED_SEED_MAX_BYTES // 1024} KB` each",
                       f"`{self.mod.DOCUMENTED_STREAM_CEILING // (1024 * 1024)} MB` per-stream"):
            self.assertIn(stated, readme)

    def test_a_changed_generator_password_is_caught(self) -> None:
        # The regression as it would happen: the generator's constants changed, and the fixtures regenerated and
        # validated with them still changed. A validator that read those constants would follow along and pass.
        builds = (("render/encrypted-rc4-128.pdf", self.mod.build_encrypted_rc4_128),
                  ("render/encrypted-aes-128.pdf", self.mod.build_encrypted_aes_128),
                  ("render/encrypted-aes-256.pdf", self.mod.build_encrypted_aes_256))
        with mock.patch.object(self.mod, "CORPUS_USER_PASSWORD", b"changed-user"), \
                mock.patch.object(self.mod, "CORPUS_OWNER_PASSWORD", b"changed-owner"):
            for name, build in builds:
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "password"):
                    self.mod.validate_encrypted_pdf_structure(name, build())

    def test_aes_256_perms_block_is_exact(self) -> None:
        # Regenerate with each of the twelve defined /Perms plaintext bytes changed in turn, re-encrypted with the real
        # file key, so only the block's meaning differs.
        name = "render/encrypted-aes-256.pdf"
        real = self.mod.aes_encrypt_block
        for index in range(12):
            def corrupt(block, round_keys, index=index):
                if len(block) == 16 and block[8:12] == b"Tadb":  # the /Perms plaintext
                    block = block[:index] + bytes([block[index] ^ 0x01]) + block[index + 1 :]
                return real(block, round_keys)
            with mock.patch.object(self.mod, "aes_encrypt_block", side_effect=corrupt):
                data = self.mod.build_encrypted_aes_256()
            with self.subTest(byte=index), self.assertRaisesRegex(ValueError, "/Perms"):
                self.mod.validate_encrypted_pdf_structure(name, data)

    def test_each_encrypted_fixture_declares_the_version_its_revision_needs(self) -> None:
        files = self.mod.expected_corpus_files()
        for name, old, new in (("render/encrypted-aes-256.pdf", b"%PDF-2.0", b"%PDF-1.7"),
                               ("render/encrypted-rc4-128.pdf", b"%PDF-1.7", b"%PDF-2.0")):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "header"):
                self.mod.validate_encrypted_pdf_structure(name, files[name].replace(old, new, 1))

    def test_the_bomb_outer_stage_must_be_complete(self) -> None:
        name = "broken/deflate-bomb-10kb.pdf"
        real = self.mod.ClassicPdfBuilder.set_obj

        def truncate_outer(builder, num, content):
            if num == 4:
                raw = content.encode("latin-1") if isinstance(content, str) else content
                start = raw.index(b"stream\n") + len(b"stream\n")
                end = raw.rindex(b"\nendstream")
                payload = raw[start:end][:-6]
                head = re.sub(rb"/Length \d+", f"/Length {len(payload)}".encode(), raw[:start], count=1)
                content = head + payload + raw[end:]
            return real(builder, num, content)

        with mock.patch.object(self.mod.ClassicPdfBuilder, "set_obj", truncate_outer):
            data = self.mod.build_broken_deflate_bomb_10kb()
        with self.assertRaisesRegex(ValueError, "incomplete"):
            self.mod.validate_broken_pdf_structure(name, data)

    def test_lzw_is_decoded_with_growing_code_widths(self) -> None:
        def pack(codes):
            buf = count = 0
            out = bytearray()
            for code in codes:
                buf, count = (buf << 9) | code, count + 9
                while count >= 8:
                    count -= 8
                    out.append((buf >> count) & 0xFF)
            return bytes(out + (bytes([(buf << (8 - count)) & 0xFF]) if count else b""))

        decode = self.mod.lzw_decode_pdf
        self.assertEqual(decode("x", pack([256, 65, 66, 258, 257])), b"ABAB")
        self.assertEqual(decode("x", self.mod.lzw_encode_literals(b"hello " * 100)), b"hello " * 100)
        for codes, says in (([256, 65, 300, 257], "past the next table entry"), ([256, 65, 66], "no EOD"),
                            ([65, 257], "clear-table code")):
            with self.subTest(codes=codes), self.assertRaisesRegex(ValueError, says):
                decode("x", pack(codes))
        # The regression through the generator: fixed 9-bit codes, no clears, past where the width must grow.
        with mock.patch.object(self.mod, "lzw_encode_literals",
                               side_effect=lambda raw: pack([256, *(raw + b" " * 300), 257])):
            seed = self.mod.build_seed_filters_lzw_ccitt()
        with self.assertRaisesRegex(ValueError, "LZW"):
            self.mod.validate_seed_pdf_structure("seeds/seed-filters-lzw-ccitt.pdf", seed)

    def test_ascii85_is_decoded_strictly(self) -> None:
        import base64
        decode = self.mod.ascii85_decode_pdf
        for plain in (b"", b"a", b"ab", b"abc", b"abcd", b"\x00" * 8 + b"xyz", bytes(range(256))):
            with self.subTest(plain=plain):
                self.assertEqual(decode(base64.a85encode(plain) + b"~>"), plain)
        self.assertEqual(decode(b"z~>"), b"\x00" * 4)
        for bad in (b"!~>", b"ab!z!~>", b"s8W-\"~>", b"abc{~>", b"9jqo^~>x~>"):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                decode(bad)
        seed = self.mod.build_seed_filter_03_ascii85()
        with self.assertRaisesRegex(ValueError, "one character"):
            self.mod.validate_seed_filter_bin_structure("seeds/seed-filter-03-ascii85.bin", seed[:-2] + b"!~>")

    def test_user_report_reproductions_are_kept_and_verified(self) -> None:
        import hashlib
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.mod.generate_corpus(root)
            repro = b"%PDF-1.7\n% user report reproduction\n"
            (root / "user-reports").mkdir()
            (root / "user-reports" / "issue-123.pdf").write_bytes(repro)
            prov = root / "PROVENANCE.md"
            row = (f"| user-reports/issue-123.pdf | {hashlib.sha256(repro).hexdigest()} | CC0-1.0 | "
                   f"Issue #123, synthetic reproducer, no private data | Empty xref table |")
            lines = prov.read_text(encoding="utf-8").splitlines()
            at = max(i for i, line in enumerate(lines) if line.startswith("| ")) + 1  # sorts after text/, last
            prov.write_text("\n".join(lines[:at] + [row] + lines[at:]) + "\n", encoding="utf-8", newline="\n")
            self.assertEqual(self.mod.check_corpus(root), (True, []))
            self.mod.generate_corpus(root)  # regenerating keeps the recorded row
            self.assertIn(row, prov.read_text(encoding="utf-8"))
            self.assertEqual(self.mod.check_corpus(root), (True, []))
            # An altered reproduction, an unrecorded one and a row without an issue each refuse.
            (root / "user-reports" / "issue-123.pdf").write_bytes(repro + b"x")
            self.assertIn("does not match its PROVENANCE.md SHA-256", " ".join(self.mod.check_corpus(root)[1]))
            with self.assertRaisesRegex(ValueError, "does not match"):
                self.mod.generate_corpus(root)
            (root / "user-reports" / "issue-123.pdf").write_bytes(repro)
            (root / "user-reports" / "issue-124.pdf").write_bytes(repro)
            self.assertIn("unexpected corpus file: user-reports/issue-124.pdf", self.mod.check_corpus(root)[1])
            (root / "user-reports" / "issue-124.pdf").unlink()
            prov.write_text(prov.read_text(encoding="utf-8").replace("Issue #123", "Issue 123"), encoding="utf-8",
                            newline="\n")
            self.assertIn("malformed user-report row", " ".join(self.mod.check_corpus(root)[1]))
            # A spacing variant or a blank cell is refused, by --check and by regeneration, never silently dropped.
            for variant in (" " + row, row.replace("| user-reports/", "|  user-reports/"),
                            row.replace("| Empty xref table |", "|   |"), row.replace("Issue #123, synthetic", "  ")):
                with self.subTest(variant=variant):
                    prov.write_text(prov.read_text(encoding="utf-8").replace("Issue 123", "Issue #123"), encoding="utf-8",
                                    newline="\n")
                    good = prov.read_text(encoding="utf-8")
                    prov.write_text(good.replace(row, variant), encoding="utf-8", newline="\n")
                    self.assertIn("malformed user-report row", " ".join(self.mod.check_corpus(root)[1]))
                    with self.assertRaisesRegex(ValueError, "malformed user-report row"):
                        self.mod.generate_corpus(root)
                    prov.write_text(good, encoding="utf-8", newline="\n")

    def test_ascii_hex_is_decoded_strictly(self) -> None:
        self.assertEqual(self.mod.asciihex_decode_pdf("x", b"48 65\n6c6C6>"), b"Hell`")  # odd final digit reads as x0
        for bad in (b"4G>", b"48", b"48>x", b"4>8>"):
            with self.subTest(hex=bad), self.assertRaises(ValueError):
                self.mod.asciihex_decode_pdf("x", bad)

    def test_run_length_refuses_data_after_eod(self) -> None:
        self.assertEqual(self.mod.rle_decode_pdf(bytes([1]) + b"ab" + bytes([128])), b"ab")
        with self.assertRaisesRegex(ValueError, "after the RunLengthDecode EOD"):
            self.mod.rle_decode_pdf(bytes([1]) + b"ab" + bytes([128]) + b"x")

    def test_png_predictor_reads_only_up_rows(self) -> None:
        with self.assertRaisesRegex(ValueError, "not Up"):
            self.mod.flate_png_up_decompress(zlib.compress(bytes([0]) + b"abcdefgh"), columns=8)  # tag 0 (None)
        with self.assertRaisesRegex(ValueError, "whole rows"):
            self.mod.flate_png_up_decompress(zlib.compress(b""), columns=8)

    def test_ascii85_strips_only_pdf_white_space(self) -> None:
        seed = b"\x03" + b"\x0b" + self.mod.ascii85_encode_pdf(self.mod.KNOWN_FILTER_PLAINTEXT)  # vertical tab
        with self.assertRaises(ValueError):
            self.mod.validate_seed_filter_bin_structure("seeds/seed-filter-03-ascii85.bin", seed)

    def test_ccitt_horizontal_mode_must_progress(self) -> None:
        with self.assertRaisesRegex(ValueError, "make no progress"):
            self.mod.validate_seed_filter_bin_structure("seeds/seed-filter-06-ccitt.bin",
                                                        bytes.fromhex("06362fb74a3ffc9a86f0010010"))

    def test_truncated_image_headers_are_value_errors(self) -> None:
        for check, data in ((lambda d: self.mod.validate_jpeg_baseline("x", d, 8, 8, 3), "ffd8ffc00002"),
                            (lambda d: self.mod.validate_jpeg_baseline("x", d, 8, 8, 3), "ffd8ffda0002"),
                            (lambda d: self.mod.validate_jpx_codestream("x", d, 8, 8), "ff4fff510002"),
                            (lambda d: self.mod.validate_jbig2_embedded("x", d), "0000000030")):
            with self.subTest(data=data), self.assertRaises(ValueError):
                check(bytes.fromhex(data))

    def test_jpeg_tables_and_scan_components_are_checked(self) -> None:
        jpeg = self.mod.make_minimal_jpeg_8x8_rgb()
        dqt = jpeg.index(b"\xFF\xDB")
        no_tables = jpeg[:dqt] + b"\xFF\xDB\x00\x02" + jpeg[dqt + 4 + 65 :]
        sos = jpeg.index(b"\xFF\xDA")
        wrong_component = jpeg[: sos + 5] + bytes([99]) + jpeg[sos + 6 :]
        dht = jpeg.index(b"\xFF\xC4")
        # The first table: one code of length 1 becomes two, the second being the reserved all-ones code.
        table = jpeg[dht + 4 : dht + 4 + 18]
        overflow_table = table[:1] + bytes([2]) + table[2:17] + b"\x00\x01"
        rest = jpeg[dht + 4 + 18 : dht + 2 + int.from_bytes(jpeg[dht + 2 : dht + 4], "big")]
        overflow = jpeg[:dht] + b"\xFF\xC4" + (len(overflow_table + rest) + 2).to_bytes(2, "big") + overflow_table + rest \
            + jpeg[dht + 2 + int.from_bytes(jpeg[dht + 2 : dht + 4], "big") :]
        for data, says in ((no_tables, "DQT"), (wrong_component, "SOS"), (overflow, "code space")):
            with self.subTest(says=says), self.assertRaisesRegex(ValueError, says):
                self.mod.validate_jpeg_baseline("x", data, 8, 8, 3)

    def test_image_marker_grammar_is_the_corpus_form(self) -> None:
        jpeg = bytearray(self.mod.make_minimal_jpeg_8x8_rgb())
        dht, sos = jpeg.index(b"\xff\xc4"), jpeg.index(b"\xff\xda")
        reserved = bytearray(jpeg)
        for offset in (18, 54):  # the two AC tables' one symbol becomes 0x20, a reserved zero-size run
            reserved[dht + 4 + offset + 17] = 0x20
        sof = jpeg.index(b"\xff\xc0")
        sof_segment = bytes(jpeg[sof : sof + 2 + int.from_bytes(jpeg[sof + 2 : sof + 4], "big")])
        two_frames = bytes(jpeg[:sof]) + sof_segment + bytes(jpeg[sof:])
        for data, says in ((bytes(reserved), "reserved symbol"), (two_frames, "once each")):
            with self.subTest(says=says), self.assertRaisesRegex(ValueError, says):
                self.mod.validate_jpeg_baseline("x", data, 8, 8, 3)
        jpx = bytearray(self.mod.make_minimal_jpx_8x8_gray())
        cod, qcd = jpx.index(b"\xff\x52"), jpx.index(b"\xff\x5c")
        levels = bytearray(jpx)
        levels[cod + 9] = 1
        levels[qcd + 2 : qcd + 4] = (7).to_bytes(2, "big")
        levels[qcd + 6 : qcd + 6] = b"\x40" * 3
        unknown = bytes(jpx[:cod]) + b"\xff\x00\x00\x02" + bytes(jpx[cod:])
        for data, says in ((bytes(levels), "no decomposition"), (unknown, "not one the corpus image uses")):
            with self.subTest(says=says), self.assertRaisesRegex(ValueError, says):
                self.mod.validate_jpx_codestream("x", data, 8, 8)

    def test_jpx_packet_header_limits_and_stuffing(self) -> None:
        def with_packet(packet_hex):
            p = bytearray(self.mod.make_minimal_jpx_8x8_gray())
            sot, sod = p.index(b"\xff\x90"), p.index(b"\xff\x93")
            p = p[: sod + 2] + bytes.fromhex(packet_hex) + b"\xff\xd9"
            p[sot + 6 : sot + 10] = (len(p) - 2 - sot).to_bytes(4, "big")
            return bytes(p)

        for packet, says in (("efff7fff7000000000", "above"), ("efffffff7000000000", "stuffed 0")):
            with self.subTest(packet=packet), self.assertRaisesRegex(ValueError, says):
                self.mod.validate_jpx_codestream("x", with_packet(packet), 8, 8)

    def test_jpx_single_code_block_and_baseline_huffman_ids(self) -> None:
        p = bytearray(self.mod.make_minimal_jpx_8x8_gray())
        c = p.index(b"\xff\x52")
        p[c + 10 : c + 12] = b"\x00\x00"  # 4x4 code-blocks: four in the tile
        p[-3] = 0xE0
        with self.assertRaisesRegex(ValueError, "64x64 code-blocks"):
            self.mod.validate_jpx_codestream("x", bytes(p), 8, 8)
        j = bytearray(self.mod.make_minimal_jpeg_8x8_rgb())
        d, s = j.index(b"\xff\xc4"), j.index(b"\xff\xda")
        j[d + 4] = 2  # the first DC table becomes id 2, and the scan names it
        j[s + 6] = 0x20
        with self.assertRaisesRegex(ValueError, "baseline"):
            self.mod.validate_jpeg_baseline("x", bytes(j), 8, 8, 3)

    def test_jpx_cod_qcd_and_packet_are_read(self) -> None:
        jpx = self.mod.make_minimal_jpx_8x8_gray()
        cod, qcd = jpx.index(b"\xFF\x52"), jpx.index(b"\xFF\x5C")
        short_cod = jpx[:cod] + b"\xFF\x52\x00\x02" + jpx[cod + 14 :]
        short_qcd = jpx[:qcd] + b"\xFF\x5C\x00\x02" + jpx[qcd + 6 :]
        sod = jpx.index(b"\xFF\x93")
        junk = jpx[: sod + 2] + b"\xFF\xFF" + b"\xFF\xD9"
        junk = junk.replace(self.mod.struct.pack(">I", 15), self.mod.struct.pack(">I", 16), 1)  # Psot covers it
        old_packet = jpx[: sod + 2] + b"\x80\x00" + b"\xFF\xD9"  # the generator's former 80 00 tile
        old_packet = old_packet.replace(self.mod.struct.pack(">I", 15), self.mod.struct.pack(">I", 16), 1)
        for data, says in ((short_cod, "COD"), (short_qcd, "QCD"), (junk, "packet"), (old_packet, "packet")):
            with self.subTest(says=says), self.assertRaisesRegex(ValueError, says):
                self.mod.validate_jpx_codestream("x", data, 8, 8)

    def test_image_codecs_are_read_by_their_own_structure(self) -> None:
        m = self.mod

        def bits_to_bytes(bits):
            bits += "0" * (-len(bits) % 8)
            return bytes(int(bits[i : i + 8], 2) for i in range(0, len(bits), 8))

        self.assertEqual(m.ccitt_g4_decode("x", m.make_ccitt_group4_16x16(), 16, 16), m.CCITT_EXPECTED_16X16)
        all_white = bits_to_bytes("1" * 16 + "000000000001000000000001")
        self.assertEqual(m.ccitt_g4_decode("x", all_white, 16, 16), [[0] * 16] * 16)
        good = m.make_ccitt_group4_16x16()
        for data, says in ((good[:-3], "ends before"), (good + b"\x00", "after the CCITT EOFB"),
                           (bits_to_bytes("0000001" + "1" * 15), "no valid mode code")):
            with self.subTest(ccitt=says), self.assertRaisesRegex(ValueError, says):
                m.ccitt_g4_decode("x", data, 16, 16)
        m.validate_jbig2_embedded("x", m.make_minimal_jbig2_16x16())
        m.validate_jpeg_baseline("x", m.make_minimal_jpeg_8x8_rgb(), 8, 8, 3)
        m.validate_jpx_codestream("x", m.make_minimal_jpx_8x8_gray(), 8, 8)
        jpeg, jpx = m.make_minimal_jpeg_8x8_rgb(), m.make_minimal_jpx_8x8_gray()
        for check, data, says in (
                (lambda d: m.validate_jpeg_baseline("x", d, 8, 8, 3), jpeg.replace(b"\x08\x00\x08\x00\x08\x03", b"\x08\x00\x10\x00\x08\x03", 1), "SOF0"),
                (lambda d: m.validate_jpeg_baseline("x", d, 8, 8, 3), jpeg[:-3] + b"\x00\xFF\xD9", "MCUs"),
                (lambda d: m.validate_jpeg_baseline("x", d, 8, 8, 3), jpeg + b"\x00", "EOI"),
                (lambda d: m.validate_jpx_codestream("x", d, 8, 8), jpx[:-2] + b"\x00\xFF\xD9", "Psot"),
                (lambda d: m.validate_jbig2_embedded("x", d),
                 m.make_minimal_jbig2_16x16().replace(m.struct.pack(">IIII", 16, 16, 72, 72), m.struct.pack(">IIII", 16, 15, 72, 72), 1),
                 "page information")):
            with self.subTest(says=says), self.assertRaisesRegex(ValueError, says):
                check(data)
        # The regression the old checks missed: the generator drawing a different picture. They compared the seed with
        # the generator's own output, so it passed; the decoders compare with the documented image.
        with mock.patch.object(m, "make_ccitt_group4_16x16", return_value=all_white):
            for name, build in (("seeds/seed-filter-06-ccitt.bin", m.build_seed_filter_06_ccitt),
                                ("seeds/seed-filter-07-jbig2.bin", m.build_seed_filter_07_jbig2)):
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "expected"):
                    m.validate_seed_filter_bin_structure(name, build())

    def test_inflation_stops_just_past_its_limit(self) -> None:
        bomb = zlib.compress(b"\0" * (8 * 1024 * 1024))
        total, kept = self.mod.inflate_bounded(bomb, 1024, keep=False)
        self.assertGreater(total, 1024)
        self.assertLessEqual(total, 1024 + (1 << 20))  # one bounded step past the limit, not the whole 8 MiB
        self.assertEqual(kept, b"")
        self.assertEqual(self.mod.inflate_bounded(zlib.compress(b"abc"), 100), (3, b"abc"))

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
