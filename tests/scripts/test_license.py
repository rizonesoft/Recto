"""Tests for LICENSE: the file is the verbatim GPL-3.0 text, so GitHub and other scanners detect the licence."""
import hashlib
import pathlib
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
# SHA-256 of https://www.gnu.org/licenses/gpl-3.0.txt with LF line endings (674 lines, 35149 bytes).
GPL_3_0_SHA256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"


class LicenseTest(unittest.TestCase):
    def test_license_is_verbatim_gpl_3_0(self):
        data = (REPO / "LICENSE").read_bytes().replace(b"\r\n", b"\n")
        self.assertEqual(data.count(b"\n"), 674, "LICENSE lost its line structure; licence scanners will not detect it")
        self.assertEqual(hashlib.sha256(data).hexdigest(), GPL_3_0_SHA256)


if __name__ == "__main__":
    unittest.main()
