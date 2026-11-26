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
    def _parse_dispatch_command(line: str, script_name: str = "review-dispatch.py") -> tuple[bool, str | None, str | None, str | None]:
        """Parse a `review-dispatch.py` or `review-prompt.py` command line (with either `/` or `\\` path separators)
        using argparse (with default abbreviation matching) so any abbreviated flag like `--writ=claude`, `--prom`
        or `--kin=round` resolves identically to the script. Returns `(is_executable_invocation, writer, kind, prompt_file)`."""
        cleaned = re.sub(r"\[[^\]]*\]", "", line).strip().strip("`").replace("\\", "/")
        try:
            tokens = shlex.split(cleaned, posix=True)
        except ValueError:
            tokens = cleaned.split()
        target = script_name.lower()
        idx = next((i for i, tok in enumerate(tokens) if tok.lower().endswith(target)), None)
        if idx is None:
            return (False, None, None, None)
        parser = argparse.ArgumentParser(add_help=False, exit_on_error=False)
        parser.add_argument("--writer")
        parser.add_argument("--kind")
        parser.add_argument("--candidate")
        parser.add_argument("--prompt")
        parser.add_argument("--record")
        parser.add_argument("--out")
        parser.add_argument("--surface", action="store_true")
        parser.add_argument("--paths", nargs="*")
        try:
            ns, _ = parser.parse_known_args(tokens[idx + 1:])
        except (SystemExit, argparse.ArgumentError):
            return (True, None, None, None)
        is_exec = any(v is not None for v in (ns.writer, ns.candidate, ns.prompt, ns.record, ns.out))
        prompt_file = ns.out if target == "review-prompt.py" else ns.prompt
        return (is_exec, ns.writer, ns.kind, prompt_file)

    @staticmethod
    def _parse_dispatch_writer(line: str) -> str | None:
        return AgentFrontmatter._parse_dispatch_command(line)[1]

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
                # Validate --writer values and every executable review-dispatch.py command across ALL five skills
                # in both trees (requiring presence on the three dispatch-owning skills).
                writer_vals = re.findall(r"--writer(?:=|\s+)([^\s`\]]+)", text)
                if skill in ("process-todo-section", "review-todo-section", "independent-review") and not writer_vals:
                    errors.append(f"{rel}: missing --writer flag for review-dispatch.py")
                for val in writer_vals:
                    if val not in allowed_writers:
                        errors.append(f"{rel}: unsupported --writer value {val!r} for {tree}")
                # Extract every executable review-prompt.py and review-dispatch.py command in exact document order
                # across CommonMark backtick/tilde fences, indented code blocks, multi-line continuations,
                # single-line and multi-line wrapped inline code spans, and bare prose paragraphs.
                ordered_cmds: list[tuple[str, str]] = []

                def _emit_code_block(block_lines: list[str]) -> None:
                    pending = ""
                    target = ""
                    for raw_ln in block_lines:
                        s = raw_ln.strip()
                        if pending:
                            pending = f"{pending} {s}"
                        elif "review-dispatch.py" in s.lower():
                            pending, target = s, "dispatch"
                        elif "review-prompt.py" in s.lower():
                            pending, target = s, "prompt"
                        if pending:
                            if pending.endswith(("\\", "`")):
                                pending = pending[:-1].rstrip()
                            else:
                                ordered_cmds.append((target, pending))
                                pending, target = "", ""
                    if pending:
                        ordered_cmds.append((target, pending))

                def _emit_prose(prose_lines: list[str]) -> None:
                    prose = "\n".join(prose_lines)
                    if "review-dispatch.py" not in prose.lower() and "review-prompt.py" not in prose.lower():
                        return
                    # Walk inline code spans (including multi-line wrapped spans) and bare non-span segments in order.
                    pos = 0
                    for m in re.finditer(r"(`+)(.+?)\1", prose, flags=re.DOTALL):
                        gap = prose[pos:m.start()]
                        pos = m.end()
                        if "review-dispatch.py" in gap.lower() or "review-prompt.py" in gap.lower():
                            for para in re.split(r"\r?\n[ \t]*\r?\n", gap):
                                flat = " ".join(para.split())
                                for role, script in (("prompt", "review-prompt.py"), ("dispatch", "review-dispatch.py")):
                                    if script not in flat.lower():
                                        continue
                                    is_exec, parsed_inline, _, _ = AgentFrontmatter._parse_dispatch_command(flat, script)
                                    if is_exec:
                                        ordered_cmds.append((role, flat))
                                        if role == "dispatch" and parsed_inline not in allowed_writers:
                                            errors.append(
                                                f"{rel}: bare dispatch text resolves writer {parsed_inline!r}, "
                                                f"not in {sorted(allowed_writers)}"
                                            )
                        span_flat = " ".join(m.group(2).split())
                        span_low = span_flat.lower()
                        for role, script in (("prompt", "review-prompt.py"), ("dispatch", "review-dispatch.py")):
                            if script not in span_low:
                                continue
                            is_exec, parsed_inline, _, _ = AgentFrontmatter._parse_dispatch_command(span_flat, script)
                            if is_exec:
                                ordered_cmds.append((role, span_flat))
                                if role == "dispatch" and parsed_inline not in allowed_writers:
                                    errors.append(
                                        f"{rel}: inline dispatch span resolves writer {parsed_inline!r}, "
                                        f"not in {sorted(allowed_writers)}"
                                    )
                    tail = prose[pos:]
                    if "review-dispatch.py" in tail.lower() or "review-prompt.py" in tail.lower():
                        for para in re.split(r"\r?\n[ \t]*\r?\n", tail):
                            flat = " ".join(para.split())
                            for role, script in (("prompt", "review-prompt.py"), ("dispatch", "review-dispatch.py")):
                                if script not in flat.lower():
                                    continue
                                is_exec, parsed_inline, _, _ = AgentFrontmatter._parse_dispatch_command(flat, script)
                                if is_exec:
                                    ordered_cmds.append((role, flat))
                                    if role == "dispatch" and parsed_inline not in allowed_writers:
                                        errors.append(
                                            f"{rel}: bare dispatch text resolves writer {parsed_inline!r}, "
                                            f"not in {sorted(allowed_writers)}"
                                        )

                fence_char = ""
                fence_len = 0
                code_buf: list[str] = []
                prose_buf: list[str] = []
                for ln in text.splitlines():
                    if not fence_char:
                        m_open = re.match(r"^[ ]{0,3}(`{3,}|~{3,})", ln)
                        if m_open:
                            if prose_buf:
                                _emit_prose(prose_buf)
                                prose_buf = []
                            if code_buf:
                                _emit_code_block(code_buf)
                                code_buf = []
                            fence_char = m_open.group(1)[0]
                            fence_len = len(m_open.group(1))
                            continue
                    else:
                        if re.match(rf"^[ ]{{0,3}}{re.escape(fence_char)}{{{fence_len},}}\s*$", ln):
                            _emit_code_block(code_buf)
                            code_buf = []
                            fence_char = ""
                            fence_len = 0
                            continue
                        code_buf.append(ln)
                        continue
                    if ln.startswith(("    ", "\t")):
                        if prose_buf:
                            _emit_prose(prose_buf)
                            prose_buf = []
                        code_buf.append(ln)
                    else:
                        if code_buf:
                            _emit_code_block(code_buf)
                            code_buf = []
                        prose_buf.append(ln)
                if code_buf:
                    _emit_code_block(code_buf)
                if prose_buf:
                    _emit_prose(prose_buf)
                dispatch_lines = [cmd for role, cmd in ordered_cmds if role == "dispatch"]
                if skill in ("process-todo-section", "review-todo-section", "independent-review"):
                    if not dispatch_lines:
                        errors.append(f"{rel}: missing executable review-dispatch.py command example")
                    for tok in bg_tokens:
                        if tok not in text:
                            errors.append(f"{rel}: missing runner background token {tok!r}")
                parsed_sequence: list[tuple[str, str | None, str | None]] = []
                for role, cmd in ordered_cmds:
                    script = "review-prompt.py" if role == "prompt" else "review-dispatch.py"
                    _, parsed_writer, kind, prompt_file = AgentFrontmatter._parse_dispatch_command(cmd, script)
                    parsed_sequence.append((role, kind, prompt_file))
                    if role == "dispatch" and parsed_writer not in allowed_writers:
                        errors.append(
                            f"{rel}: dispatch command resolves writer {parsed_writer!r}, not in {sorted(allowed_writers)}"
                        )
                ordered_kinds = [(role, kind) for role, kind, _ in parsed_sequence]
                if skill == "process-todo-section":
                    expected = [("prompt", "plan"), ("dispatch", "plan")]
                    if ordered_kinds != expected:
                        errors.append(f"{rel}: process-todo-section command order {ordered_kinds!r} != {expected!r}")
                    elif not parsed_sequence[0][2] or parsed_sequence[0][2] != parsed_sequence[1][2]:
                        errors.append(f"{rel}: process-todo-section prompt --out != dispatch --prompt")
                if skill in ("review-todo-section", "process-phase"):
                    passes = ("--kind plan", "--kind round", "--kind completion", "--kind reconciliation")
                    for pass_name in passes:
                        if pass_name not in text:
                            errors.append(f"{rel}: missing pass {pass_name!r}")
                    idx = [text.index(p) for p in passes if p in text]
                    if len(idx) == 4 and idx != sorted(idx):
                        errors.append(f"{rel}: three-pass order drifted")
                if skill == "process-phase":
                    phase_kinds = re.findall(r"--kind(?:=|\s+)([a-zA-Z0-9_-]+)", text)
                    if phase_kinds != ["plan", "round", "completion", "reconciliation"]:
                        errors.append(f"{rel}: process-phase --kind sequence {phase_kinds!r} drifted")
                if skill == "review-todo-section":
                    expected = [
                        ("prompt", "round"), ("dispatch", "round"),
                        ("prompt", "completion"), ("dispatch", "completion"),
                        ("prompt", "reconciliation"), ("dispatch", "reconciliation"),
                        ("dispatch", None),
                    ]
                    if ordered_kinds != expected:
                        errors.append(f"{rel}: review-todo-section command order {ordered_kinds!r} != {expected!r}")
                    else:
                        for i in range(0, 6, 2):
                            p_file, d_file = parsed_sequence[i][2], parsed_sequence[i + 1][2]
                            if not p_file or p_file != d_file:
                                errors.append(f"{rel}: review-todo-section pair {i // 2} prompt --out {p_file!r} != dispatch --prompt {d_file!r}")
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
        wrong_backslash_dispatch = dict(texts)
        wrong_backslash_dispatch[".agents/skills/review-todo-section/SKILL.md"] = (
            wrong_backslash_dispatch[".agents/skills/review-todo-section/SKILL.md"]
            .replace("scripts/review-dispatch.py --kind completion --candidate <full tip SHA> --writer <claude|agy>",
                     r"scripts\review-dispatch.py --kind completion --candidate <full tip SHA> --writ=claude")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_backslash_dispatch))
        wrong_tilde_fence = dict(texts)
        wrong_tilde_fence[".agents/skills/review-todo-section/SKILL.md"] = (
            wrong_tilde_fence[".agents/skills/review-todo-section/SKILL.md"]
            .replace(
                '```powershell\nbuild\\python.cmd scripts/review-prompt.py --kind completion --candidate <full tip SHA> --ref "D<NN> T<NN> §<N>" [--fidelity <design source>] --purpose "<one or two sentences>" --governing <the spec or plan> --out <scratch-completion-prompt.md>\nbuild\\python.cmd scripts/review-dispatch.py --kind completion --candidate <full tip SHA> --writer <claude|agy> --prompt <scratch-completion-prompt.md> --record docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md\n```',
                '~~~powershell\nbuild\\python.cmd scripts/review-prompt.py --kind completion --candidate <full tip SHA> --ref "D<NN> T<NN> §<N>" [--fidelity <design source>] --purpose "<one or two sentences>" --governing <the spec or plan> --out <scratch-completion-prompt.md>\nbuild\\python.cmd scripts/review-dispatch.py --kind completion --candidate <full tip SHA> --writ=claude --prompt <scratch-completion-prompt.md> --record docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md\n~~~',
            )
        )
        self.assertNotEqual(
            wrong_tilde_fence[".agents/skills/review-todo-section/SKILL.md"],
            texts[".agents/skills/review-todo-section/SKILL.md"],
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_tilde_fence))
        wrong_phase_writer = dict(texts)
        wrong_phase_writer[".agents/skills/process-phase/SKILL.md"] = (
            wrong_phase_writer[".agents/skills/process-phase/SKILL.md"]
            + "\n```powershell\nbuild\\python.cmd scripts/review-dispatch.py --kind round --candidate abc --writer claude --prompt p.md --record r.md\n```\n"
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_phase_writer))
        wrong_plan_writer = dict(texts)
        wrong_plan_writer[".agents/skills/process-plan/SKILL.md"] = (
            wrong_plan_writer[".agents/skills/process-plan/SKILL.md"]
            + "\n```powershell\nbuild\\python.cmd scripts/review-dispatch.py --kind round --candidate abc --writ=claude --prompt p.md --record r.md\n```\n"
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_plan_writer))
        missing_inline_writer = dict(texts)
        missing_inline_writer[".agents/skills/process-phase/SKILL.md"] = (
            missing_inline_writer[".agents/skills/process-phase/SKILL.md"]
            + "\nRun `build\\python.cmd scripts/review-dispatch.py --kind round --candidate abc --prompt p.md --record r.md`.\n"
        )
        self.assertTrue(self._check_ten_writer_skills(missing_inline_writer))
        dangling_inline_writer = dict(texts)
        dangling_inline_writer[".agents/skills/process-phase/SKILL.md"] = (
            dangling_inline_writer[".agents/skills/process-phase/SKILL.md"]
            + "\nRun `build\\python.cmd scripts/review-dispatch.py --kind round --candidate abc --prompt p.md --record r.md --writer`.\n"
        )
        self.assertTrue(self._check_ten_writer_skills(dangling_inline_writer))
        wrong_completion_dispatch_kind = dict(texts)
        wrong_completion_dispatch_kind[".agents/skills/review-todo-section/SKILL.md"] = (
            wrong_completion_dispatch_kind[".agents/skills/review-todo-section/SKILL.md"]
            .replace("review-dispatch.py --kind completion", "review-dispatch.py --kind round")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_completion_dispatch_kind))
        wrong_completion_prompt_kind = dict(texts)
        wrong_completion_prompt_kind[".agents/skills/review-todo-section/SKILL.md"] = (
            wrong_completion_prompt_kind[".agents/skills/review-todo-section/SKILL.md"]
            .replace("review-prompt.py --kind completion", "review-prompt.py --kind round")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_completion_prompt_kind))
        wrong_plan_dispatch_kind = dict(texts)
        wrong_plan_dispatch_kind[".agents/skills/process-todo-section/SKILL.md"] = (
            wrong_plan_dispatch_kind[".agents/skills/process-todo-section/SKILL.md"]
            .replace("review-dispatch.py --kind plan", "review-dispatch.py --kind round")
        )
        self.assertTrue(self._check_ten_writer_skills(wrong_plan_dispatch_kind))
        transposed_completion = dict(texts)
        p_line = 'build\\python.cmd scripts/review-prompt.py --kind completion --candidate <full tip SHA> --ref "D<NN> T<NN> §<N>" [--fidelity <design source>] --purpose "<one or two sentences>" --governing <the spec or plan> --out <scratch-completion-prompt.md>'
        d_line = 'build\\python.cmd scripts/review-dispatch.py --kind completion --candidate <full tip SHA> --writer <claude|agy> --prompt <scratch-completion-prompt.md> --record docs/reviews/<NN-domain>/D<NN>-T<NN>-s<N>.md'
        transposed_completion[".agents/skills/review-todo-section/SKILL.md"] = (
            transposed_completion[".agents/skills/review-todo-section/SKILL.md"]
            .replace(f"{p_line}\n{d_line}", f"{d_line}\n{p_line}")
        )
        self.assertNotEqual(
            transposed_completion[".agents/skills/review-todo-section/SKILL.md"],
            texts[".agents/skills/review-todo-section/SKILL.md"],
        )
        self.assertTrue(self._check_ten_writer_skills(transposed_completion))
        inline_early_dispatch = dict(texts)
        inline_early_dispatch[".agents/skills/review-todo-section/SKILL.md"] = (
            inline_early_dispatch[".agents/skills/review-todo-section/SKILL.md"]
            .replace(
                "3. **The completion and excellence review**",
                f"Run `{d_line}` first.\n3. **The completion and excellence review**",
            )
        )
        self.assertNotEqual(
            inline_early_dispatch[".agents/skills/review-todo-section/SKILL.md"],
            texts[".agents/skills/review-todo-section/SKILL.md"],
        )
        self.assertTrue(self._check_ten_writer_skills(inline_early_dispatch))
        wrapped_inline_dispatch = dict(texts)
        wrapped_d_line = d_line.replace("review-dispatch.py --kind completion", "review-dispatch.py\n--kind=completion")
        wrapped_inline_dispatch[".agents/skills/review-todo-section/SKILL.md"] = (
            wrapped_inline_dispatch[".agents/skills/review-todo-section/SKILL.md"]
            .replace(
                "3. **The completion and excellence review**",
                f"Run `{wrapped_d_line}` first.\n3. **The completion and excellence review**",
            )
        )
        self.assertNotEqual(
            wrapped_inline_dispatch[".agents/skills/review-todo-section/SKILL.md"],
            texts[".agents/skills/review-todo-section/SKILL.md"],
        )
        self.assertTrue(self._check_ten_writer_skills(wrapped_inline_dispatch))
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
