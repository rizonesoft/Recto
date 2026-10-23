"""Error codes the plan names are codes `src/pdf/error.h` has (D01 T02 §22), checked by
`scripts/check-error-code-names.py`: qualified names must be `ErrorCode` enumerators, the `PdfErrorCode` scope must not
appear, and a retired name must not come back anywhere outside a correction note."""
import importlib.util
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts" / "check-error-code-names.py"
HEADER = REPO / "src" / "pdf" / "error.h"
TODO = REPO / "todo"


def load_checker():
    spec = importlib.util.spec_from_file_location("check_error_code_names", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ErrorCodeNamesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_checker()

    def test_header_enumerators_are_read(self) -> None:
        codes = self.mod.error_codes(HEADER)
        for name in ("Ok", "DamagedFormat", "InternalError", "ResourceLimit", "InvalidArgument", "ConversionFailed",
                     "RollbackIncomplete"):
            self.assertIn(name, codes)
        self.assertFalse(any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in codes), codes)

    def test_todo_tree_names_only_codes_error_h_has(self) -> None:
        self.assertEqual(self.mod.unknown_names(TODO, self.mod.error_codes(HEADER)), [])
        result = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout), (0, ""))

    def test_planted_names_are_refused(self) -> None:
        known = self.mod.error_codes(HEADER)
        unknown = "Error" + "Code::" + "NoSuch" + "Code"
        pdf_scoped = "Pdf" + "Error" + "Code::" + "InvalidArgument"
        retired = "`" + "Password" + "Required" + "`"
        source = TODO / "01-engine" / "TODO-02-actor-and-render.md"
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "todo" / "01-engine").mkdir(parents=True)
            (root / "src" / "pdf").mkdir(parents=True)
            shutil.copyfile(HEADER, root / "src" / "pdf" / "error.h")
            planted = root / "todo" / "01-engine" / source.name
            shutil.copyfile(source, planted)
            self.assertEqual(self.mod.unknown_names(root / "todo", known), [])
            with planted.open("a", encoding="utf-8") as handle:
                handle.write(f"\n- [ ] Return `{unknown}` on a planted refusal.\n- [ ] Or `{pdf_scoped}`.\n"
                             f"- [ ] Or {retired} when the password is missing.\n"
                             f"- [ ] Fine. **Corrected 2026-10-23:** (was {retired}).\n"
                             f"- [ ] Or `JobState::{retired.strip('`')} (missing)`, a larger span.\n"
                             f"- [ ] A note **Corrected 2026-10-23:** (was {retired} (twice)). Then {retired} again.\n"
                             f"- [ ] A test name is not a code: `PubSec.{retired.strip('`')}`.\n"
                             f"```cpp\nreturn {retired.strip('`')}; // a fenced block is code throughout\n```\n"
                             f"~~~\nreturn {retired.strip('`')}; // a tilde fence too\n~~~\n"
                             f"````md\n```\nreturn {retired.strip('`')}; // a shorter fence does not close a longer one\n````\n"
                             f"\n- [ ] A code span may run `across a line,\n{retired.strip('`')} included` (CommonMark 6.1).\n"
                             f"\n- [ ] A double-backtick span ``holds a `{retired.strip('`')}` inside``.\n"
                             f"\n> ```\n> return {retired.strip('`')}; // a fence inside a block quote\n> ```\n"
                             f"\n- [ ] A literal unmatched backtick: `.\n- [ ] Return {retired}.\n"
                             f"\n> ```\n> An example.\n\n```\nreturn {retired.strip('`')};\n```\n"
                             f"\n- ```cpp\n  return {retired.strip('`')};\n  ```\n"
                             f"\n- [ ] Or {retired.strip('`')} in plain prose.\n")
            found = self.mod.unknown_names(root / "todo", known)
            self.assertEqual(len(found), 15, found)
            self.assertTrue(found[0].endswith(unknown), found)
            self.assertTrue(found[1].endswith(pdf_scoped), found)
            self.assertTrue(all(line.endswith(retired) for line in found[2:]), found)
            # The generated plan is a projection of the TODO files and is never read.
            (root / "todo" / "implementation-plan.md").write_text(f"`{unknown}`\n", encoding="utf-8")
            self.assertEqual(len(self.mod.unknown_names(root / "todo", known)), 15)
            # The command line reports each one and exits 1.
            result = subprocess.run([sys.executable, str(SCRIPT), "--root", str(root)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(len(result.stdout.splitlines()), 15, result.stdout)


if __name__ == "__main__":
    unittest.main()
