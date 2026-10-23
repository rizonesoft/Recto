"""Error codes the plan names are codes `src/pdf/error.h` has (D01 T02 §22): a section built from a TODO contract that
names a missing code either fails to compile or invents one on the spot. Every `ErrorCode::<name>` under `todo/`
(outside the generated `implementation-plan.md`) must be an `ErrorCode` enumerator, and the `PdfErrorCode` scope, which
does not exist, must not appear qualified at all."""
import pathlib
import re
import shutil
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
HEADER = REPO / "src" / "pdf" / "error.h"
TODO = REPO / "todo"
ENUM = re.compile(r"enum class ErrorCode\b[^{]*\{(?P<body>.*?)\};", re.DOTALL)
# A qualified name: `ErrorCode::Name`, or the nonexistent `PdfErrorCode::Name`. A placeholder such as
# `ErrorCode::<code>` is not a name and does not match.
QUALIFIED = re.compile(r"(?<![A-Za-z0-9_])(?P<scope>Pdf)?ErrorCode::(?P<name>[A-Za-z_][A-Za-z0-9_]*)")


def error_codes(header: pathlib.Path) -> set[str]:
    """The `ErrorCode` enumerators, read from the header with its comments removed."""
    match = ENUM.search(header.read_text(encoding="utf-8"))
    if match is None:
        raise AssertionError(f"no `enum class ErrorCode` in {header}")
    body = re.sub(r"//[^\n]*", "", match.group("body"))
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    names = set()
    for item in body.split(","):
        item = item.strip()
        if item:
            names.add(item.split("=")[0].strip())
    return names


def unknown_names(todo_root: pathlib.Path, known: set[str]) -> list[str]:
    """Every qualified error code under `todo_root` that is not in `known`, or that uses the `PdfErrorCode` scope, as
    `path:line: name`."""
    found = []
    for path in sorted(todo_root.rglob("*.md")):
        if path.name == "implementation-plan.md":
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for match in QUALIFIED.finditer(line):
                if match.group("scope") or match.group("name") not in known:
                    found.append(f"{path.relative_to(todo_root).as_posix()}:{number}: {match.group(0)}")
    return found


class ErrorCodeNamesTests(unittest.TestCase):
    def test_header_enumerators_are_read(self) -> None:
        codes = error_codes(HEADER)
        for name in ("Ok", "DamagedFormat", "InternalError", "ResourceLimit", "InvalidArgument", "ConversionFailed",
                     "RollbackIncomplete"):
            self.assertIn(name, codes)
        self.assertFalse(any(not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) for name in codes), codes)

    def test_todo_tree_names_only_codes_error_h_has(self) -> None:
        self.assertEqual(unknown_names(TODO, error_codes(HEADER)), [])

    def test_planted_names_are_refused(self) -> None:
        known = error_codes(HEADER)
        unknown = "Error" + "Code::" + "NoSuch" + "Code"
        pdf_scoped = "Pdf" + "Error" + "Code::" + "InvalidArgument"
        source = TODO / "01-engine" / "TODO-02-actor-and-render.md"
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp) / "todo"
            (root / "01-engine").mkdir(parents=True)
            planted = root / "01-engine" / source.name
            shutil.copyfile(source, planted)
            self.assertEqual(unknown_names(root, known), [])
            with planted.open("a", encoding="utf-8") as handle:
                handle.write(f"\n- [ ] Return `{unknown}` on a planted refusal.\n- [ ] Or `{pdf_scoped}`.\n")
            found = unknown_names(root, known)
            self.assertEqual(len(found), 2, found)
            self.assertTrue(found[0].endswith(unknown), found)
            self.assertTrue(found[1].endswith(pdf_scoped), found)
            # The generated plan is a projection of the TODO files and is never read.
            (root / "implementation-plan.md").write_text(f"`{unknown}`\n", encoding="utf-8")
            self.assertEqual(len(unknown_names(root, known)), 2)


if __name__ == "__main__":
    unittest.main()
