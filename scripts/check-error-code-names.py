"""Error codes the plan names are codes `src/pdf/error.h` has (D01 T02 §22).

A section built from a TODO contract that names a missing code either fails to compile or invents one on the spot.
Every `ErrorCode::<name>` under `todo/` (outside the generated `implementation-plan.md`) must be an `ErrorCode`
enumerator, the `PdfErrorCode` scope, which does not exist, must not appear qualified at all, and a name the decision
table (`docs/reviews/01-engine/D01-T02-s22.md`) retired must not come back unqualified (outside a dated correction
note, and outside `D01 T02 §22`'s own account of them). CI (`plan.yml`) and the pre-commit hook run it; it prints
each offending `path:line: name` and exits 1, or exits 0.
"""
import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
ENUM = re.compile(r"enum class ErrorCode\b[^{]*\{(?P<body>.*?)\};", re.DOTALL)
# A qualified name: `ErrorCode::Name`, or the nonexistent `PdfErrorCode::Name`. A placeholder such as
# `ErrorCode::<code>` is not a name and does not match.
QUALIFIED = re.compile(r"(?<![A-Za-z0-9_])(?P<scope>Pdf)?ErrorCode::(?P<name>[A-Za-z_][A-Za-z0-9_]*)")
# Names the decision table mapped to other codes; written in backticks they read as a code.
RETIRED = ("PasswordRequired", "InvalidPassword", "LimitExceeded", "UndoBudgetExceeded", "CorruptStructure",
           "StructTreeMalformed", "OutputInvalid", "ClipboardEmpty", "ClipboardUnsupported", "UnsupportedRecipientType")
# A retired word anywhere in a code span, except as a member or test name after a dot (`PubSec.UnsupportedRecipientType`).
CODE_SPAN = re.compile(r"`(?P<span>[^`]+)`")
RETIRED_WORD = re.compile(r"(?<![A-Za-z0-9_.])(?P<name>" + "|".join(RETIRED) + r")(?![A-Za-z0-9_])")
NOTE_MARKER = re.compile(r"\*\*Corrected \d{4}-\d{2}-\d{2}:\*\*\s*")
SECTION = re.compile(r"^## (?P<number>\d+)\. ")
# The section that retired them lists them in its own context.
EXEMPT_SECTION = ("01-engine/TODO-02-actor-and-render.md", "22")


def error_codes(header: pathlib.Path) -> set[str]:
    """The `ErrorCode` enumerators, read from the header with its comments removed."""
    match = ENUM.search(header.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"no `enum class ErrorCode` in {header}")
    body = re.sub(r"//[^\n]*", "", match.group("body"))
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
    names = set()
    for item in body.split(","):
        item = item.strip()
        if item:
            names.add(item.split("=")[0].strip())
    return names


def without_notes(line: str) -> str:
    """The line with each dated correction note removed: the marker and the parenthesis that follows it (nested
    parentheses included), or, without one, the rest of that sentence. Text after a note is still read."""
    out = []
    at = 0
    for marker in NOTE_MARKER.finditer(line):
        if marker.start() < at:
            continue
        out.append(line[at:marker.start()])
        end = marker.end()
        if end < len(line) and line[end] == "(":
            depth = 0
            while end < len(line):
                if line[end] == "(":
                    depth += 1
                elif line[end] == ")":
                    depth -= 1
                    if depth == 0:
                        end += 1
                        break
                end += 1
            if end < len(line) and line[end] == ".":
                end += 1
        else:
            stop = line.find(". ", end)
            end = len(line) if stop < 0 else stop + 1
        at = end
    out.append(line[at:])
    return "".join(out)


def unknown_names(todo_root: pathlib.Path, known: set[str]) -> list[str]:
    """Every qualified error code under `todo_root` that is not in `known` or uses the `PdfErrorCode` scope, and every
    retired name used unqualified, as `path:line: name`."""
    found = []
    for path in sorted(todo_root.rglob("*.md")):
        if path.name == "implementation-plan.md":
            continue
        relative = path.relative_to(todo_root).as_posix()
        section = None
        fenced = False
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if heading := SECTION.match(line):
                section = heading.group("number")
            for match in QUALIFIED.finditer(line):
                if match.group("scope") or match.group("name") not in known:
                    found.append(f"{relative}:{number}: {match.group(0)}")
            if line.lstrip().startswith(("```", "~~~")):  # a backtick or tilde fence (CommonMark)
                fenced = not fenced
                continue
            if (relative, section) == EXEMPT_SECTION:
                continue
            # A fenced block is code from end to end; elsewhere only inline code spans are.
            spans = [without_notes(line)] if fenced else [m.group("span") for m in CODE_SPAN.finditer(without_notes(line))]
            for span in spans:
                for match in RETIRED_WORD.finditer(span):
                    found.append(f"{relative}:{number}: `{match.group('name')}`")
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=pathlib.Path, default=REPO, help="repository root (default: this one)")
    args = parser.parse_args(argv)
    found = unknown_names(args.root / "todo", error_codes(args.root / "src" / "pdf" / "error.h"))
    for line in found:
        print(line)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
