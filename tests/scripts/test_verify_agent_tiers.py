"""scripts/verify-agent-tiers.mjs: agent frontmatter it accepts is frontmatter a YAML loader can read."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = (ROOT / "scripts" / "verify-agent-tiers.mjs").as_uri()


def read(lines):
    """readAgentFrontmatter on a frontmatter block holding lines: (accepted, error)."""
    text = "---\n" + "\n".join(lines) + "\n---\n"
    script = (f"import {{ readAgentFrontmatter }} from {json.dumps(MODULE)};"
              f"const r = readAgentFrontmatter({json.dumps(text)});"
              "console.log(JSON.stringify({ok: r.error === null, error: r.error}));")
    out = subprocess.run(["node", "--input-type=module", "-e", script], capture_output=True, text=True, check=True)
    result = json.loads(out.stdout)
    return result["ok"], result["error"]


def verify(lines):
    """verifyAgentTiers over one agent definition in a scratch repository: its error messages."""
    with tempfile.TemporaryDirectory() as tmp:
        agents = Path(tmp) / ".claude" / "agents"
        agents.mkdir(parents=True)
        (agents / "a.md").write_text("---\n" + "\n".join(lines) + "\n---\n\nBody.\n", encoding="utf-8")
        script = (f"import {{ verifyAgentTiers }} from {json.dumps(MODULE)};"
                  f"console.log(JSON.stringify(verifyAgentTiers({json.dumps(tmp)}).errors));")
        out = subprocess.run(["node", "--input-type=module", "-e", script], capture_output=True, text=True, check=True)
        return json.loads(out.stdout)


@unittest.skipUnless(shutil.which("node"), "needs Node.js")
class AgentFrontmatter(unittest.TestCase):
    def test_a_plain_value_yaml_cannot_read_is_refused(self):
        for line in ("description: Use this: to locate code", "description: ends with a colon:",
                     "description: @mention", "description: `code` first", "description: %directive",
                     "description: - dash", "description: ? question", "description:\tTabbed"):
            with self.subTest(line=line):
                ok, error = read(["name: a", line, "model: sonnet"])
                self.assertFalse(ok, line)

    def test_quoted_values_yaml_refuses_are_refused(self):
        for lines in (['description: "a"b"'], ["description: 'a'b'"], ['description: "a"b" # note'],
                      ["description: a\x01b"], ['description: "\\q"'], ["# comment\rmodel: haiku"],
                      ["description: a\x85b"], ["description: a\u2028b"], ["\u00a0"], ["\u2003"],
                      ["x" * 1025 + ": value"], ["unknown_key: value"]):
            with self.subTest(lines=lines):
                ok, error = read(["name: a", *lines, "model: sonnet"])
                self.assertFalse(ok, lines)

    def test_nested_blocks_and_block_values_are_refused(self):
        # Recto reads flat key: value lines only; each of these is valid YAML or not, but none is flat.
        for lines in (["description: |", "  first"], ["description: >", "  first"], ["description: |2", "  x"],
                      ["tools: Read", "hooks:", "  Stop:", "    - matcher: a"], ["description: a", "  continued"],
                      ["description: |", "    ", "  first"], ["description: |", "\t", "  first"]):
            with self.subTest(lines=lines):
                ok, error = read(["name: a", *lines, "model: sonnet"])
                self.assertFalse(ok, lines)

    def test_values_yaml_reads_are_accepted(self):
        for line in ("description: a #comment", 'description: "Use this: quoted"', "description: -dash-word",
                     "description: plain words, commas", "description: key:value without space",
                     "description: 50% done"):
            with self.subTest(line=line):
                ok, error = read(["name: a", line, "model: sonnet"])
                self.assertTrue(ok, error)

    def test_an_indented_blank_or_comment_line_is_allowed(self):
        for lines in (["description: helper", "  # comment"], ["description: helper", "  "]):
            with self.subTest(lines=lines):
                ok, error = read(["name: a", *lines, "model: sonnet"])
                self.assertTrue(ok, error)

    def test_only_an_opus_5_5_subagent_may_write(self):
        # Owner decision 2026-10-23: Opus 5.5 is the only writer, so a subagent with edit tools pins claude-opus-5-5;
        # Sonnet and Haiku subagents, and the floating opus alias, stay read-only.
        cases = {"claude-opus-5-5": True, "claude-opus-5-5-20261001": True, "opus": False, "claude-opus-5": False,
                 "claude-opus-6-0": False, "sonnet": False}
        for model, allowed in cases.items():
            for tools in ("Read, Edit", "Write", "'*'", "Edit(src/**)"):
                with self.subTest(model=model, tools=tools):
                    errors = verify(["name: writer", "description: writes code", f"model: {model}", f"tools: {tools}",
                                     "model_rationale: the sole writer"])
                    self.assertEqual(errors == [], allowed, errors)
                    self.assertEqual(any("grants edit tools" in e for e in errors), not allowed, errors)
        self.assertEqual(verify(["name: reader", "description: reads code", "model: sonnet", "tools: Read, Grep"]), [])

    def test_the_repository_agents_pass(self):
        result = subprocess.run(["node", str(ROOT / "scripts" / "verify-agent-tiers.mjs")], cwd=ROOT,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
