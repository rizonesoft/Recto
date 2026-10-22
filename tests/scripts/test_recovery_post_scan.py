"""Checks on damaged-file recovery in src/pdf/xref.cpp: the step after the forward scan that decides which dictionaries
with an indirect /Type are catalogs reads and parses nothing. It consults only what the scan recorded
(catalog_names), so recovery's work stays linear in the file however the references are arranged. A runtime test
cannot see work that produces the same answer, so the guard is on the source, as test_etw_registration.py is."""
import pathlib
import re
import unittest

XREF_CPP = pathlib.Path(__file__).resolve().parents[2] / "src" / "pdf" / "xref.cpp"
START = "// A dictionary whose /Type is an indirect reference is a catalog"
END = "// Validate that a candidate `/Root` reference resolves"
# Anything that reads, parses, decodes or resolves an object.
FORBIDDEN = ("read_indirect_object", "resolve_object", "resolve_and_decrypt", "Parser", "Lexer", "decode_stream",
             "parse_object_stream", "extract_object_stream_member", "file_bytes")


def post_scan_block(source: str) -> str:
    start = source.index(START)
    return source[start:source.index(END, start)]


class RecoveryPostScan(unittest.TestCase):
    def test_post_scan_catalog_step_reads_nothing(self):
        source = XREF_CPP.read_text(encoding="utf-8")
        code = re.sub(r"//[^\n]*", "", post_scan_block(source))  # comments may name what the code avoids
        for name in FORBIDDEN:
            with self.subTest(name=name):
                self.assertNotIn(name, code)
        self.assertIn("catalog_names", code)


if __name__ == "__main__":
    unittest.main()
