"""scripts/verify-agent-tiers.mjs: agent frontmatter it accepts is frontmatter a YAML loader can read."""
import argparse
import json
from pathlib import Path
import re
import shlex
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

    def test_only_a_fable_5_2_subagent_may_write(self):
        # Owner decision 2026-11-10: Fable 5.2 is the only writer, so a subagent with edit tools pins claude-fable-5-2;
        # Sonnet and Haiku subagents, the floating fable alias and the former writer (Opus 5.5) stay read-only.
        cases = {"claude-fable-5-2": True, "claude-fable-5-2-20261101": True, "fable": False, "claude-fable-5": False,
                 "claude-fable-6-0": False, "claude-opus-5-5": False, "opus": False, "sonnet": False}
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

    @staticmethod
    def _parse_dispatch_writer(line: str) -> str | None:
        """Parse a `scripts/review-dispatch.py` command line using argparse (with default abbreviation matching)
        so any abbreviated flag like `--writ=claude` resolves identically to `scripts/review-dispatch.py`."""
        cleaned = re.sub(r"\[[^\]]*\]", "", line).strip().strip("`")
        try:
            tokens = shlex.split(cleaned, posix=True)
        except ValueError:
            tokens = cleaned.split()
        idx = next((i for i, tok in enumerate(tokens) if tok.endswith("review-dispatch.py")), None)
        if idx is None:
            return None
        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument("--writer")
        parser.add_argument("--kind")
        parser.add_argument("--candidate")
        parser.add_argument("--prompt")
        parser.add_argument("--record")
        parser.add_argument("--surface", action="store_true")
        parser.add_argument("--paths", nargs="*")
        try:
            ns, _ = parser.parse_known_args(tokens[idx + 1:])
        except SystemExit:
            return None
        return ns.writer

    @staticmethod
    def _check_ten_writer_skills(texts: dict[str, str]) -> list[str]:
        """Verify all ten Claude Code and Antigravity workflow skill files stay aligned on three-pass,
        writer identity, pass order and runner-specific background review invocation."""
        errors: list[str] = []
        skills = ("process-todo-section", "review-todo-section", "independent-review", "process-phase", "process-plan")
        for tree, bg_tokens, allowed_writers in (
            (".claude/skills", ("run_in_background",), {"claude", "<claude|agy>"}),
            (".agents/skills", ("WaitMsBeforeAsync: 500", "IsDaemon: false"), {"agy", "<claude|agy>"}),
        ):
            for skill in skills:
                rel = f"{tree}/{skill}/SKILL.md"
                text = texts.get(rel)
                if text is None:
                    errors.append(f"missing skill file: {rel}")
                    continue
                if skill in ("process-todo-section", "review-todo-section", "process-phase"):
                    if "three-pass" not in text:
                        errors.append(f"{rel}: missing three-pass schedule reference")
                if skill in ("process-todo-section", "review-todo-section", "independent-review"):
                    writer_vals = re.findall(r"--writer(?:=|\s+)([^\s`\]]+)", text)
                    if not writer_vals:
                        errors.append(f"{rel}: missing --writer flag for review-dispatch.py")
                    for val in writer_vals:
                        if val not in allowed_writers:
                            errors.append(f"{rel}: unsupported --writer value {val!r} for {tree}")
                    # Check every executable review-dispatch.py command inside fenced code blocks (no flag filter
                    # that an abbreviation like `--prom` could bypass), plus any inline backtick span that passes
                    # a writer flag to review-dispatch.py.
                    dispatch_lines: list[str] = []
                    in_fence = False
                    for ln in text.splitlines():
                        stripped = ln.strip()
                        if stripped.startswith("```"):
                            in_fence = not in_fence
                            continue
                        if in_fence and "scripts/review-dispatch.py" in ln:
                            dispatch_lines.append(ln)
                        elif not in_fence and "scripts/review-dispatch.py" in ln:
                            for span in re.findall(r"`([^`\n]*scripts/review-dispatch\.py[^`\n]*)`", ln):
                                parsed_inline = AgentFrontmatter._parse_dispatch_writer(span)
                                if parsed_inline is not None and parsed_inline not in allowed_writers:
                                    errors.append(
                                        f"{rel}: inline dispatch span resolves writer {parsed_inline!r}, "
                                        f"not in {sorted(allowed_writers)}"
                                    )
                    if not dispatch_lines:
                        errors.append(f"{rel}: missing executable review-dispatch.py command example")
                    for ln in dispatch_lines:
                        parsed = AgentFrontmatter._parse_dispatch_writer(ln)
                        if parsed not in allowed_writers:
                            errors.append(f"{rel}: dispatch command resolves writer {parsed!r}, not in {sorted(allowed_writers)}")
                    for tok in bg_tokens:
                        if tok not in text:
                            errors.append(f"{rel}: missing runner background token {tok!r}")
                if skill in ("review-todo-section", "process-phase"):
                    passes = ("--kind plan", "--kind round", "--kind completion", "--kind reconciliation")
                    for pass_name in passes:
                        if pass_name not in text:
                            errors.append(f"{rel}: missing pass {pass_name!r}")
                    idx = [text.index(p) for p in passes if p in text]
                    if len(idx) == 4 and idx != sorted(idx):
                        errors.append(f"{rel}: three-pass order drifted")
                if skill == "process-plan":
                    if "process-phase" not in text:
                        errors.append(f"{rel}: missing process-phase handoff")
        return errors

    def test_ten_writer_skill_files_parity(self):
        skills = ("process-todo-section", "review-todo-section", "independent-review", "process-phase", "process-plan")
        texts: dict[str, str] = {}
        for tree in (".claude/skills", ".agents/skills"):
            for skill in skills:
                rel = f"{tree}/{skill}/SKILL.md"
                path = ROOT / rel
                self.assertTrue(path.is_file(), f"missing {rel}")
                texts[rel] = path.read_text(encoding="utf-8")
        self.assertEqual(self._check_ten_writer_skills(texts), [])
        # Negative checks: drift on either runner side must fail.
        drifted_agy = dict(texts)
        drifted_agy[".agents/skills/review-todo-section/SKILL.md"] = (
            drifted_agy[".agents/skills/review-todo-section/SKILL.md"].replace("WaitMsBeforeAsync: 500", "foreground")
        )
        self.assertTrue(self._check_ten_writer_skills(drifted_agy))
        drifted_claude = dict(texts)
        drifted_claude[".claude/skills/review-todo-section/SKILL.md"] = (
            drifted_claude[".claude/skills/review-todo-section/SKILL.md"].replace("--kind reconciliation", "--kind other")
        )
        self.assertTrue(self._check_ten_writer_skills(drifted_claude))
        drifted_writer = dict(texts)
        drifted_writer[".agents/skills/independent-review/SKILL.md"] = (
            drifted_writer[".agents/skills/independent-review/SKILL.md"]
            .replace("--writer agy", "--writer codex")
            .replace("--writer <claude|agy>", "--writer codex")
        )
        self.assertTrue(self._check_ten_writer_skills(drifted_writer))
        wrong_tree_writer = dict(texts)
        wrong_tree_writer[".agents/skills/independent-review/SKILL.md"] = (
            wrong_tree_writer[".agents/skills/independent-review/SKILL.md"]
            .replace("--writer agy", "--writer claude")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_tree_writer))
        wrong_eq_writer = dict(texts)
        wrong_eq_writer[".agents/skills/independent-review/SKILL.md"] = (
            wrong_eq_writer[".agents/skills/independent-review/SKILL.md"]
            .replace("--writer <claude|agy>", "--writer=claude")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_eq_writer))
        wrong_abbrev_writer = dict(texts)
        wrong_abbrev_writer[".agents/skills/independent-review/SKILL.md"] = (
            wrong_abbrev_writer[".agents/skills/independent-review/SKILL.md"]
            .replace("--writer <claude|agy>", "--writ=claude")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_abbrev_writer))
        wrong_abbrev_prompt = dict(texts)
        wrong_abbrev_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            wrong_abbrev_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace("--kind completion --candidate <full tip SHA> --writer <claude|agy> --prompt",
                     "--kind completion --candidate <full tip SHA> --writ=claude --prom")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_abbrev_prompt))
        scrambled_phase = dict(texts)
        scrambled_phase[".agents/skills/process-phase/SKILL.md"] = (
            scrambled_phase[".agents/skills/process-phase/SKILL.md"]
            .replace("--kind plan", "__ROUND__")
            .replace("--kind round", "--kind plan")
            .replace("__ROUND__", "--kind round")
        )
        self.assertTrue(self._check_ten_writer_skills(scrambled_phase))


if __name__ == "__main__":
    unittest.main()
