"""Checks on src/pdf/error.cpp's ETW registration: production registers the Recto.Diagnostics provider and never
enables it itself (D16); only the test seam force-enables it. Whether a session happens to enable the provider on a
given machine is outside the code, so this is checked in the source, where the defect would live."""
import pathlib
import re
import unittest

ERROR_CPP = pathlib.Path(__file__).resolve().parents[2] / "src" / "pdf" / "error.cpp"


def function_body(text: str, signature: str) -> str:
    """The braced body of the function whose definition starts with signature."""
    start = text.index(signature)
    open_at = text.index("{", start)
    depth = 0
    for i in range(open_at, len(text)):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            return text[open_at : i + 1]
    raise AssertionError(f"unbalanced body for {signature}")


class EtwRegistration(unittest.TestCase):
    def setUp(self):
        # Comments are removed so a mention in prose is not taken for a call.
        source = ERROR_CPP.read_text(encoding="utf-8")
        self.text = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)

    def test_production_registration_never_enables_the_provider(self):
        body = function_body(self.text, "bool ensure_etw_provider_registered() noexcept")
        self.assertIn("TraceLoggingRegister(", body)
        self.assertNotIn("_tlgEnableCallback", body)
        self.assertNotIn("force_enable_etw_provider_for_testing", body)

    def test_forced_enablement_lives_only_in_the_test_seam(self):
        seam = function_body(self.text, "void force_enable_etw_provider_for_testing() noexcept")
        self.assertEqual(self.text.count("_tlgEnableCallback("), 1)
        self.assertIn("_tlgEnableCallback(", seam)
        self.assertEqual(len(re.findall(r"\bforce_enable_etw_provider_for_testing\s*\(", self.text)), 1)  # its definition


if __name__ == "__main__":
    unittest.main()
