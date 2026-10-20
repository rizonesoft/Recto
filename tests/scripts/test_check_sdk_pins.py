"""Tests for scripts/check-sdk-pins.py: a stale, floating, preview, duplicated or off-line pin fails; the live project parses."""
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "check-sdk-pins.py"


def load_check_sdk_pins():
    spec = importlib.util.spec_from_file_location("check_sdk_pins", SCRIPT_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


FEED = {
    "Microsoft.Windows.CppWinRT": ["2.0.250303.1", "3.0.260715.1", "3.0.260818.1"],
    "Microsoft.Windows.SDK.BuildTools": ["10.0.26100.8249", "10.0.26100.9169", "10.0.28000.2705",
                                         "10.0.29648.1000-preview"],
}
CURRENT = {"Microsoft.Windows.CppWinRT": "3.0.260818.1", "Microsoft.Windows.SDK.BuildTools": "10.0.26100.9169"}
LINE = (10, 0, 26100)


def project(references, target="10.0.26100.0"):
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<Project ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
            f'  <PropertyGroup><WindowsTargetPlatformVersion>{target}</WindowsTargetPlatformVersion></PropertyGroup>\n'
            f'  <ItemGroup>\n{references}  </ItemGroup>\n</Project>\n')


REFERENCES = ('    <PackageReference Include="Microsoft.WindowsAppSDK" Version="2.5.1" />\n'
              '    <PackageReference Include="Microsoft.Windows.CppWinRT" Version="3.0.260818.1" />\n'
              '    <PackageReference Include="Microsoft.Windows.SDK.BuildTools" Version="10.0.26100.9169" />\n')


class CheckSdkPinsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = load_check_sdk_pins()

    def check(self, line=LINE, **overrides):
        return self.mod.check({**CURRENT, **overrides}, line, fetch=FEED.__getitem__)

    def test_latest_stable_pins_pass(self):
        self.assertEqual(self.check(), [])

    def test_stale_pin_fails_with_the_latest_version(self):
        faults = self.check(**{"Microsoft.Windows.CppWinRT": "3.0.260715.1"})
        self.assertEqual([(f[0], f[1], f[2]) for f in faults],
                         [("Microsoft.Windows.CppWinRT", "3.0.260715.1", "3.0.260818.1")])

    def test_newer_preview_release_is_ignored(self):
        self.assertEqual(self.mod.latest_stable(["10.0.26100.9169", "10.0.29648.1000-preview"]), "10.0.26100.9169")

    def test_windows_app_sdk_is_left_to_check_winui_pin(self):
        self.assertNotIn("Microsoft.WindowsAppSDK", self.mod.TRACKED)

    def test_experimental_or_floating_pin_fails(self):
        for pinned in ("3.0.260818.1-preview", "3.*", "[3.0.260818.1,)"):
            with self.subTest(pinned=pinned):
                faults = self.check(**{"Microsoft.Windows.CppWinRT": pinned})
                self.assertEqual(faults[0][3], "is not an exact stable version")

    def test_build_tools_compare_only_within_the_target_windows_sdk_line(self):
        faults = self.check(**{"Microsoft.Windows.SDK.BuildTools": "10.0.26100.8249"})
        self.assertEqual(faults[0][2], "10.0.26100.9169")

    def test_build_tools_off_the_target_windows_sdk_line_fail(self):
        # The target SDK moved to 28000 while the build tools stayed on 26100: the tools' own line is not the rule.
        faults = self.check(line=(10, 0, 28000))
        self.assertEqual(len(faults), 1)
        self.assertIn("not in the Windows SDK 10.0.28000 line", faults[0][3])

    def test_versions_compare_numerically_not_as_text(self):
        self.assertEqual(self.mod.latest_stable(["2.9.0", "2.10.0"]), "2.10.0")

    def test_project_yields_pins_and_the_target_sdk_line(self):
        pins, line = self.mod.read_project(project(REFERENCES))
        self.assertEqual((pins, line), (CURRENT, LINE))

    def test_missing_pin_cannot_be_checked(self):
        only = '    <PackageReference Include="Microsoft.Windows.CppWinRT" Version="3.0.260818.1" />\n'
        with self.assertRaises(self.mod.CannotCheck):
            self.mod.read_project(project(only))

    def test_a_commented_out_reference_is_not_a_pin(self):
        commented = REFERENCES.replace(
            '    <PackageReference Include="Microsoft.Windows.SDK.BuildTools" Version="10.0.26100.9169" />\n',
            '    <!-- <PackageReference Include="Microsoft.Windows.SDK.BuildTools" Version="10.0.26100.9169" /> -->\n')
        with self.assertRaisesRegex(self.mod.CannotCheck, "no PackageReference pin for Microsoft.Windows.SDK.BuildTools"):
            self.mod.read_project(project(commented))

    def test_a_duplicated_reference_cannot_be_checked(self):
        twice = REFERENCES + '    <PackageReference Include="Microsoft.Windows.CppWinRT" Version="2.0.250303.1" />\n'
        with self.assertRaisesRegex(self.mod.CannotCheck, "more than once"):
            self.mod.read_project(project(twice))

    def test_a_missing_or_odd_target_sdk_cannot_be_checked(self):
        for target in ("", "$(LatestTargetPlatformVersion)"):
            with self.subTest(target=target), self.assertRaises(self.mod.CannotCheck):
                self.mod.read_project(project(REFERENCES, target=target))

    def test_an_unexpected_feed_shape_cannot_be_checked(self):
        for body in ([], {"versions": None}, {"versions": ["3.0.260818.1", 7]}):
            response = io.BytesIO(json.dumps(body).encode("utf-8"))
            with self.subTest(body=body), mock.patch.object(self.mod.urllib.request, "urlopen", return_value=response):
                with self.assertRaises(self.mod.CannotCheck):
                    self.mod.fetch_versions("Microsoft.Windows.CppWinRT")

    def test_live_project_pins_parse_as_exact_stable_versions(self):
        pins, line = self.mod.read_project(self.mod.PROJECT.read_text(encoding="utf-8"))
        self.assertEqual(set(pins), set(self.mod.TRACKED))
        self.assertTrue(all(self.mod.is_stable(v) for v in pins.values()), pins)
        self.assertEqual(line[:2], (10, 0))


if __name__ == "__main__":
    unittest.main()
