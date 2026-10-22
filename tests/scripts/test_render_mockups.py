"""Checks on design/tools/render-mockups.mjs, run against a temporary copy of the design tree so the repository's own
boards are never touched."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RENDERER = ROOT / "design" / "tools" / "render-mockups.mjs"
NODE = shutil.which("node")
BOARDS = ("A-Banner", "B-Banner")


@unittest.skipUnless(NODE, "needs Node.js")
class RenderMockups(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.design = pathlib.Path(self._tmp.name) / "design"
        (self.design / "tools").mkdir(parents=True)
        (self.design / "mockups" / "source").mkdir(parents=True)
        (self.design / "screenshots" / "explorations").mkdir(parents=True)
        shutil.copy(RENDERER, self.design / "tools" / RENDERER.name)
        shutil.copy(ROOT / "design" / "mockups" / "source" / "canvas.json", self.design / "mockups" / "source")
        for board in BOARDS:
            shutil.copy(ROOT / "design" / "mockups" / "source" / f"{board}.dc.html", self.design / "mockups" / "source")

    def tearDown(self):
        self._tmp.cleanup()

    def render(self, *args, env=None):
        return subprocess.run([NODE, str(self.design / "tools" / RENDERER.name), *args], capture_output=True, text=True,
                              timeout=120, env={**os.environ, **(env or {})})

    def html(self):
        folder = self.design / "mockups" / "html"
        return sorted(p.name for p in folder.iterdir()) if folder.exists() else []

    def test_only_renders_just_the_named_boards(self):
        run = self.render("--no-png", "--only", "B-Banner")
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(self.html(), ["B-Banner.html"])

    def test_every_argument_is_consumed_or_refused(self):
        for args in (["--no-png", "--only", "A-Banner", "--only", "B-Banner"], ["--no-png", "--only", "A-Banner", "B-Banner"],
                     ["--bogus"], ["--no-png", "--no-png"], ["--no-png", "--only"]):
            with self.subTest(args=args):
                run = self.render(*args)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn("unexpected argument", run.stderr)
                self.assertEqual(self.html(), [])

    def test_an_unknown_board_is_refused(self):
        run = self.render("--no-png", "--only", "NoSuchBoard")
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("--only names no board: NoSuchBoard", run.stderr)

    def test_a_failed_screenshot_leaves_the_previous_png(self):
        # The stand-in browser is Node itself, which refuses the browser's flags and exits non-zero without writing.
        png = self.design / "screenshots" / "explorations" / "A-Banner.png"
        png.write_bytes(b"previous screenshot")
        run = self.render("--only", "A-Banner", env={"RECTO_RENDER_BROWSER": NODE})
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual(png.read_bytes(), b"previous screenshot")
        self.assertEqual(list(png.parent.glob("*.tmp.png")), [])


if __name__ == "__main__":
    unittest.main()
