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


REFERENCES = ('    <PackageReference Include="Microsoft.Windows.CppWinRT" Version="3.0.260818.1" />\n'
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

    def test_only_the_two_toolchain_packages_are_tracked(self):
        self.assertEqual(set(self.mod.TRACKED), {"Microsoft.Windows.CppWinRT", "Microsoft.Windows.SDK.BuildTools"})

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


def lock(versions, **overrides):
    """A build/tools.lock.json with the two owned entries at `versions` (and the other tools, which this check ignores)."""
    names = {"Microsoft.Windows.CppWinRT": "cppwinrt", "Microsoft.Windows.SDK.BuildTools": "sdk-buildtools"}
    doc = {"ninja": {"version": "1.13.2", "url": "u", "sha256": "0" * 64, "executable": "ninja.exe"}}
    for package, version in versions.items():
        doc[names[package]] = {"version": version,
                               "url": f"https://www.nuget.org/api/v2/package/{package}/{version}",
                               "sha256": "a" * 64, "executable": "bin/x.exe", "owner": "check-sdk-pins"}
    for name, entry in overrides.items():
        doc[name].update(entry)
    return json.dumps(doc, indent=2) + "\n"


class LockPinsTests(unittest.TestCase):
    """build/tools.lock.json's NuGet entries move with Recto.vcxproj (D00 T04 section 8): one update path."""

    @classmethod
    def setUpClass(cls):
        cls.mod = load_check_sdk_pins()

    def test_live_lock_names_the_versions_the_project_pins(self):
        pins, _ = self.mod.read_project(self.mod.PROJECT.read_text(encoding="utf-8"))
        locked = self.mod.read_lock(self.mod.LOCK.read_text(encoding="utf-8"))
        self.assertEqual(self.mod.lock_faults(pins, locked), [])

    def test_a_lock_entry_on_another_version_is_a_fault_that_no_pr_settles(self):
        locked = self.mod.read_lock(lock({**CURRENT, "Microsoft.Windows.CppWinRT": "3.0.260715.1"}))
        faults = self.mod.lock_faults(CURRENT, locked)
        self.assertEqual(len(faults), 1)
        package, pinned, latest, problem = faults[0]
        self.assertEqual((package, pinned, latest), ("Microsoft.Windows.CppWinRT", "3.0.260715.1", None))
        self.assertIn("not the 3.0.260818.1", problem)

    def test_a_lock_entry_this_check_does_not_own_or_cannot_trust_is_refused(self):
        for overrides in ({"cppwinrt": {"owner": "someone-else"}},
                          {"cppwinrt": {"url": "https://example.invalid/cppwinrt.nupkg"}},
                          {"sdk-buildtools": {"sha256": "not-a-hash"}}):
            with self.subTest(overrides=overrides):
                with self.assertRaises(self.mod.CannotCheck):
                    self.mod.read_lock(lock(CURRENT, **overrides))
        with self.assertRaises(self.mod.CannotCheck):
            self.mod.read_lock(json.dumps({"ninja": {}}))

    def test_a_proposed_bump_moves_the_project_and_the_lock_together(self):
        import hashlib
        import tempfile
        bumped = "10.0.26100.9457"
        payload = b"the 10.0.26100.9457 package bytes"
        fetched = []

        def fetch(package, version):
            fetched.append((package, version))
            return payload

        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp)
            (tree / "src" / "app").mkdir(parents=True)
            (tree / "build").mkdir()
            (tree / "src" / "app" / "Recto.vcxproj").write_text(project(REFERENCES), encoding="utf-8")
            (tree / "build" / "tools.lock.json").write_text(lock(CURRENT), encoding="utf-8")
            newer = [("Microsoft.Windows.SDK.BuildTools", CURRENT["Microsoft.Windows.SDK.BuildTools"], bumped)]
            self.assertNotEqual(self.mod.tree_faults(tree, newer), [])
            changed = self.mod.rewrite_tree(tree, newer, fetch=fetch)
            self.assertEqual(changed, ["src/app/Recto.vcxproj", "build/tools.lock.json"])
            self.assertEqual(fetched, [("Microsoft.Windows.SDK.BuildTools", bumped)])
            self.assertEqual(self.mod.tree_faults(tree, newer), [])
            pins, _ = self.mod.read_project((tree / "src" / "app" / "Recto.vcxproj").read_text(encoding="utf-8"))
            self.assertEqual(pins["Microsoft.Windows.SDK.BuildTools"], bumped)
            entry = json.loads((tree / "build" / "tools.lock.json").read_text(encoding="utf-8"))["sdk-buildtools"]
            self.assertEqual(entry["version"], bumped)
            self.assertEqual(entry["url"], f"https://www.nuget.org/api/v2/package/Microsoft.Windows.SDK.BuildTools/{bumped}")
            self.assertEqual(entry["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertEqual(entry["owner"], "check-sdk-pins")
            # C++/WinRT was not proposed and did not move, and nothing was fetched for it.
            self.assertEqual(json.loads((tree / "build" / "tools.lock.json").read_text(encoding="utf-8"))["cppwinrt"]["version"],
                             CURRENT["Microsoft.Windows.CppWinRT"])

    def test_the_sdk_line_still_bounds_what_a_bump_may_propose(self):
        faults = self.mod.check(CURRENT, LINE, fetch=FEED.__getitem__)
        proposed = [latest for package, _, latest, _ in faults if package == "Microsoft.Windows.SDK.BuildTools"]
        self.assertTrue(all(latest is None or latest.startswith("10.0.26100.") for latest in proposed), proposed)


class ToolPinsOwnershipTests(unittest.TestCase):
    """scripts/check-tool-pins.py leaves the entries check-sdk-pins owns alone and still refuses any other extra one."""

    def test_owned_entries_are_skipped_and_unowned_extras_refused(self):
        import tempfile
        spec = importlib.util.spec_from_file_location("check_tool_pins", REPO_ROOT / "scripts" / "check-tool-pins.py")
        tools = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tools)
        live = json.loads((REPO_ROOT / "build" / "tools.lock.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            (root / "build" / "tools.lock.json").write_text(json.dumps(live), encoding="utf-8")
            self.assertEqual(set(tools.read_lock(root)), set(tools.TOOL_SPECS))
            live["stray"] = {"version": "1", "url": "u", "sha256": "0" * 64, "executable": "x.exe"}
            (root / "build" / "tools.lock.json").write_text(json.dumps(live), encoding="utf-8")
            with self.assertRaises(tools.CannotCheck):
                tools.read_lock(root)

if __name__ == "__main__":
    unittest.main()
