"""Checks on build/build.ps1, the one-command build and test driver."""
import pathlib
import re
import unittest

BUILD_PS1 = pathlib.Path(__file__).resolve().parents[2] / "build" / "build.ps1"


class AsanRuntimeStaging(unittest.TestCase):
    def setUp(self):
        self.text = BUILD_PS1.read_text(encoding="utf-8")

    def test_the_staged_runtime_is_the_one_each_executable_imports(self):
        # Since VS 2022 17.7 the toolset picks the ASan runtime DLL (one clang_rt.asan_dynamic for Debug and Release),
        # so the script reads each executable's imports instead of naming a DLL per configuration.
        self.assertRegex(self.text, r"/dependents \$exe")
        self.assertNotIn("asan_dbg_dynamic", self.text)
        self.assertIsNone(re.search(r"\$rtDllName\s*=\s*if", self.text))


if __name__ == "__main__":
    unittest.main()
