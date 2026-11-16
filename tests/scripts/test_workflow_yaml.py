"""Every GitHub workflow file parses: no unquoted (plain) YAML value holds `: ` or ` #`, which YAML reads as another
key or a comment. GitHub rejects such a file whole: the run shows no jobs, and a `workflow_dispatch` is refused with
"Workflow does not have 'workflow_dispatch' trigger" (plan.yml 43857072314, D00 T04 section 13). Stdlib only."""
from __future__ import annotations

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
# `key: value` or `- key: value`, with the value plain (not quoted, not a block, flow or alias start).
ENTRY = re.compile(r"^(?P<indent>\s*)(?:- )?(?P<key>[A-Za-z0-9_.-]+):[ ]+(?P<value>.+?)\s*$")
BLOCK = re.compile(r"^[|>][+-]?\d*\s*(#.*)?$")


def plain_value_faults(text: str) -> list[str]:
    """The lines whose plain value YAML would misread, as `<line>: <text>`."""
    faults, block_indent = [], None
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        indent = len(line) - len(line.lstrip(" "))
        if block_indent is not None:
            if not stripped or indent > block_indent:
                continue  # inside a block scalar (`run: |`): its text is not YAML
            block_indent = None
        if not stripped or stripped.startswith("#"):
            continue
        match = ENTRY.match(line)
        if not match:
            continue
        value = match.group("value")
        if BLOCK.match(value):
            block_indent = len(match.group("indent"))
            continue
        if value[0] in "'\"[{&*!|>":
            continue
        if ": " in value or value.endswith(":") or " #" in value:
            faults.append(f"{number}: {stripped}")
    return faults


class WorkflowYaml(unittest.TestCase):
    def test_no_plain_value_holds_a_colon_space_or_a_comment_mark(self):
        files = sorted(WORKFLOWS.glob("*.yml"))
        self.assertTrue(files)
        for path in files:
            with self.subTest(workflow=path.name):
                self.assertEqual(plain_value_faults(path.read_text(encoding="utf-8")), [])

    def test_the_scanner_catches_what_github_rejected_and_passes_what_it_accepts(self):
        bad = "      - name: Fail on purpose\n        run: throw 'fail_on_purpose was set: a deliberate red'\n"
        self.assertEqual(plain_value_faults(bad), ["2: run: throw 'fail_on_purpose was set: a deliberate red'"])
        self.assertEqual(plain_value_faults("    name: a # b\n"), ["1: name: a # b"])
        good = ("on:\n  push:\n    branches: [main]\n"
                "      - name: Quoted\n        run: \"throw 'a: b'\"\n"
                "      - run: |\n          echo key: value # not YAML here\n"
                "        if: inputs.fail_on_purpose\n"
                "run-name: ${{ inputs.sha != '' && format('rebuild of {0}', inputs.sha) || '' }}\n")
        self.assertEqual(plain_value_faults(good), [])


if __name__ == "__main__":
    unittest.main()
