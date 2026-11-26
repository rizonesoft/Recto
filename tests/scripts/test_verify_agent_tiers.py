"""scripts/verify-agent-tiers.mjs: agent frontmatter it accepts is frontmatter a YAML loader can read."""
import argparse
import contextlib
import importlib.util
import io
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
    def _strip_shell_comment(line: str) -> str:
        """Strip an unquoted `#` comment (at line start or after whitespace / shell separators `;`, `(`, `{`, `|`, `&`)."""
        in_s = False
        in_d = False
        for i, ch in enumerate(line):
            if ch == "'" and not in_d:
                in_s = not in_s
            elif ch == '"' and not in_s:
                in_d = not in_d
            elif ch == "#" and not in_s and not in_d and (i == 0 or line[i - 1].isspace() or line[i - 1] in ";({|&"):
                return line[:i]
        return line

    @staticmethod
    def _split_shell_statements(chunk: str) -> list[str]:
        """Split `chunk` on unquoted `;`, `&&`, or `||` separators in exact left-to-right order."""
        out: list[str] = []
        buf: list[str] = []
        in_s = False
        in_d = False
        i = 0
        n = len(chunk)
        while i < n:
            ch = chunk[i]
            if ch == "'" and not in_d:
                in_s = not in_s
                buf.append(ch)
            elif ch == '"' and not in_s:
                in_d = not in_d
                buf.append(ch)
            elif not in_s and not in_d and ch == ";":
                out.append("".join(buf))
                buf = []
            elif not in_s and not in_d and chunk[i:i + 2] in ("&&", "||"):
                out.append("".join(buf))
                buf = []
                i += 1
            else:
                buf.append(ch)
            i += 1
        out.append("".join(buf))
        return [s.strip() for s in out if s.strip()]

    @staticmethod
    def _is_python_runner(tok: str) -> bool:
        norm = tok.replace("\\", "/")
        base = norm.rsplit("/", 1)[-1].lower()
        path_launchers = {"python", "python3", "python.exe", "python3.exe", "py", "py.exe"}
        if "/" not in norm:
            return base in path_launchers
        try:
            resolved = (ROOT / norm).resolve()
        except OSError:
            return False
        if resolved in ((ROOT / "build" / "python.cmd").resolve(), (ROOT / "scripts" / "run-python.sh").resolve()):
            return True
        return base in (path_launchers | {"python.cmd", "run-python.sh"}) and resolved.is_file()

    @staticmethod
    def _matches_script_path(tok: str, target: str) -> bool:
        """Return True only when `tok` resolves to the real `ROOT / 'scripts' / target` file."""
        norm = tok.replace("\\", "/")
        try:
            candidate = ROOT / norm
            return candidate.is_file() and candidate.resolve() == (ROOT / "scripts" / target).resolve()
        except OSError:
            return False

    @staticmethod
    def _find_script_token(tokens: list[str], target: str) -> int | None:
        """Return the index of `target` in `tokens` when the statement actually invokes the real
        `scripts/<target>` file (directly or through a valid Python launcher with non-exiting CPython
        options, optionally prefixed by PowerShell's `&` call operator)."""
        start = 1 if tokens and tokens[0] == "&" else 0
        if start >= len(tokens):
            return None
        if AgentFrontmatter._matches_script_path(tokens[start], target):
            return start
        if AgentFrontmatter._is_python_runner(tokens[start]):
            pos = start + 1
            allowed_single = {"-B", "-u", "-s", "-S", "-E", "-I", "-O", "-OO", "-P", "-b", "-bb", "-q", "-v", "-d", "-i", "-x"}
            while pos < len(tokens):
                tok = tokens[pos]
                if tok in allowed_single:
                    pos += 1
                elif tok in ("-X", "-W") and pos + 1 < len(tokens):
                    pos += 2
                elif (tok.startswith("-X") and len(tok) > 2) or (tok.startswith("-W") and len(tok) > 2):
                    pos += 1
                else:
                    break
            if pos < len(tokens) and AgentFrontmatter._matches_script_path(tokens[pos], target):
                return pos
        return None

    _SCRIPT_MODS: dict[str, object] = {}
    _PWSH_SYNTAX_CACHE: dict[str, bool] = {}

    @classmethod
    def _pwsh_parses_as_command(cls, stmt: str) -> bool:
        """Verify with PowerShell's real parser (`ParseInput`) that `stmt` parses with zero syntax errors
        and yields at least one `CommandAst` (rejecting quoted launchers without `&`, unclosed quotes,
        or other PowerShell parser errors). Falls back to True only when `pwsh` is not installed."""
        cached = cls._PWSH_SYNTAX_CACHE.get(stmt)
        if cached is not None:
            return cached
        pwsh = shutil.which("pwsh")
        if not pwsh:
            cls._PWSH_SYNTAX_CACHE[stmt] = True
            return True
        ps_code = (
            "$errs = $null; "
            "$ast = [System.Management.Automation.Language.Parser]::ParseInput($env:RECTO_PWSH_STMT, [ref]$null, [ref]$errs); "
            "$cmds = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true)); "
            "if ($errs.Count -eq 0 -and $cmds.Count -gt 0) { exit 0 } else { exit 1 }"
        )
        import os
        env = dict(os.environ, RECTO_PWSH_STMT=stmt)
        proc = subprocess.run(
            [pwsh, "-NoProfile", "-NonInteractive", "-Command", ps_code],
            capture_output=True, text=True, env=env, timeout=15,
        )
        ok = proc.returncode == 0
        cls._PWSH_SYNTAX_CACHE[stmt] = ok
        return ok

    @classmethod
    def _script_module(cls, script_name: str):
        if script_name not in cls._SCRIPT_MODS:
            script_path = ROOT / "scripts" / script_name
            spec = importlib.util.spec_from_file_location(f"_recto_{script_name.replace('-', '_').replace('.', '_')}", script_path)
            assert spec is not None and spec.loader is not None
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            cls._SCRIPT_MODS[script_name] = mod
        return cls._SCRIPT_MODS[script_name]

    @staticmethod
    def _parse_dispatch_command(line: str, script_name: str = "review-dispatch.py") -> tuple[bool, str | None, str | None, str | None]:
        """Parse a single `review-dispatch.py` or `review-prompt.py` command statement using PowerShell's
        parser (when `pwsh` is available) plus that script's own `build_parser()` (zero duplicated CLI
        contract: rejects quoted launchers without `&`, unclosed quotes, `--help`, `-h`, invalid
        `--timeout`, unknown flags, missing required args, or diagnostic-only flags). Returns
        `(is_executable_invocation, writer, kind, prompt_file)`."""
        raw = re.sub(r"\[[^\]]*\]", "", line).strip().strip("`")
        if not raw or raw.startswith(('"', "'")):
            return (False, None, None, None)
        cleaned = re.sub(r"<[^>]+>", lambda m: m.group(0).replace(" ", "_").replace("'", ""), raw.replace("\\", "/"))
        try:
            tokens = shlex.split(cleaned, posix=True)
        except ValueError:
            return (False, None, None, None)
        target = script_name.lower()
        idx = AgentFrontmatter._find_script_token(tokens, target)
        if idx is None:
            return (False, None, None, None)
        argv = tokens[idx + 1:]
        if target == "review-prompt.py":
            if not any(t.startswith(("--cand", "--out", "--gov", "--purp", "--fail", "--find", "--gat",
                                     "--ref", "--todo", "--fid", "--base", "--last", "--help", "-h")) for t in argv):
                return (False, None, None, None)
        else:
            if not any(t.startswith(("--w", "--p", "--r", "--c", "--t", "--s", "--l", "--j", "--help", "-h")) for t in argv):
                return (False, None, None, None)
        pwsh_stmt = re.sub(r"<[^>]+>", "PLACEHOLDER", raw)
        if not AgentFrontmatter._pwsh_parses_as_command(pwsh_stmt):
            return (False, None, None, None)
        def _subst_writer(raw_argv: list[str], repl: str) -> list[str]:
            out_argv: list[str] = []
            i = 0
            while i < len(raw_argv):
                tok = raw_argv[i]
                if target == "review-dispatch.py" and tok.startswith("--w") and "=" in tok:
                    k, v = tok.split("=", 1)
                    if v == "<claude|agy>":
                        out_argv.append(f"{k}={repl}")
                        i += 1
                        continue
                if (target == "review-dispatch.py" and tok.startswith("--w") and "=" not in tok
                        and i + 1 < len(raw_argv) and raw_argv[i + 1] == "<claude|agy>"):
                    out_argv.extend([tok, repl])
                    i += 2
                    continue
                out_argv.append(tok)
                i += 1
            return out_argv

        mod = AgentFrontmatter._script_module(target)
        parser = mod.build_parser()
        buf = io.StringIO()
        try:
            with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
                ns = parser.parse_args(_subst_writer(argv, "claude"))
        except (SystemExit, argparse.ArgumentError):
            return (True, None, None, None)
        if target == "review-prompt.py":
            # Substitute only documentation angle-bracket placeholders before invoking review-prompt.py's own
            # validate_template_args(), so literal invalid values like `--ref D00-T05-s1` or unsupported flags
            # for a kind are refused identically to scripts/review-prompt.py.
            check_ns = argparse.Namespace(**vars(ns))
            if check_ns.ref == "D<NN> T<NN> §<N>":
                check_ns.ref = "D00 T01 §1"
            for sha_field in ("candidate", "base", "failed", "last"):
                val = getattr(check_ns, sha_field, None)
                if val and val.startswith("<") and val.endswith(">"):
                    setattr(check_ns, sha_field, "HEAD")
            try:
                mod.validate_template_args(check_ns)
            except mod.Refused:
                return (True, None, None, None)
            return (True, None, ns.kind, ns.out)
        if ns.check_pin or ns.list_rounds:
            return (True, None, None, None)
        # Parse a second time substituting `<claude|agy>` with `agy`: only when the final effective writer
        # varies with the substitution (`claude` vs `agy`) did the placeholder actually determine `--writer`
        # (so `--writer <claude|agy> --writ=claude` resolves to `claude`, never `<claude|agy>`).
        try:
            with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
                ns_alt = parser.parse_args(_subst_writer(argv, "agy"))
        except (SystemExit, argparse.ArgumentError):
            return (True, None, None, None)
        # Validate candidate/writer/prompt using review-dispatch.py's own validate_args(), substituting only
        # angle-bracket documentation placeholders like `<full_tip_SHA>` with a 40-char hex SHA so literal
        # invalid values like `HEAD` or `abc1234` are refused identically to review-dispatch.py.
        if ns.candidate and ns.candidate.startswith("<") and ns.candidate.endswith(">"):
            ns.candidate = "0" * 40
        try:
            with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(buf):
                mod.validate_args(parser, ns)
        except (SystemExit, argparse.ArgumentError):
            return (True, None, None, None)
        has_kind = any(t.startswith("--k") for t in argv)
        kind = ns.kind if has_kind else None
        writer = "<claude|agy>" if (ns.writer == "claude" and ns_alt.writer == "agy") else ns.writer
        if not ns.record or (kind is not None and not ns.candidate):
            return (True, None, None, None)
        return (True, writer, kind, ns.prompt)

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
                # Extract every executable review-prompt.py and review-dispatch.py command in exact character-offset
                # document order across CommonMark backtick/tilde fences, indented code blocks, multi-line
                # continuations, single-line and multi-line wrapped inline code spans, semicolon/chained commands,
                # and bare prose paragraphs.
                ordered_cmds: list[tuple[str, str]] = []

                def _extract_commands(chunk: str, from_code_block: bool) -> None:
                    for stmt in AgentFrontmatter._split_shell_statements(chunk):
                        stmt_low = stmt.lower()
                        for role, script in (("prompt", "review-prompt.py"), ("dispatch", "review-dispatch.py")):
                            if script not in stmt_low:
                                continue
                            is_exec, parsed_writer, _, _ = AgentFrontmatter._parse_dispatch_command(stmt, script)
                            # In code blocks, also catch a bare invocation whose command head matches even if all
                            # flags were omitted; ignore non-invoking wrappers like `Write-Output` or `echo`.
                            cleaned = re.sub(r"\[[^\]]*\]", "", stmt).strip().strip("`").replace("\\", "/")
                            try:
                                toks = shlex.split(cleaned, posix=True)
                            except ValueError:
                                toks = cleaned.split()
                            if AgentFrontmatter._find_script_token(toks, script) is None:
                                continue
                            if from_code_block or is_exec:
                                ordered_cmds.append((role, stmt))
                                if role == "dispatch" and parsed_writer not in allowed_writers:
                                    errors.append(
                                        f"{rel}: dispatch command resolves writer {parsed_writer!r}, "
                                        f"not in {sorted(allowed_writers)}"
                                    )

                def _emit_code_block(block_lines: list[str]) -> None:
                    cleaned_block = re.sub(r"<#.*?#>", "", "\n".join(block_lines), flags=re.DOTALL)
                    pending = ""
                    for raw_ln in cleaned_block.splitlines():
                        s = AgentFrontmatter._strip_shell_comment(raw_ln).strip()
                        if not s:
                            if pending:
                                _extract_commands(pending, from_code_block=True)
                                pending = ""
                            continue
                        if pending:
                            pending = f"{pending} {s}"
                        elif "review-dispatch.py" in s.lower() or "review-prompt.py" in s.lower():
                            pending = s
                        if pending:
                            if pending.endswith(("\\", "`")):
                                pending = pending[:-1].rstrip()
                            else:
                                _extract_commands(pending, from_code_block=True)
                                pending = ""
                    if pending:
                        _extract_commands(pending, from_code_block=True)

                def _emit_prose(prose_lines: list[str]) -> None:
                    prose = "\n".join(prose_lines)
                    if "review-dispatch.py" not in prose.lower() and "review-prompt.py" not in prose.lower():
                        return
                    pos = 0
                    for m in re.finditer(r"(`+)(.+?)\1", prose, flags=re.DOTALL):
                        gap = prose[pos:m.start()]
                        pos = m.end()
                        if "review-dispatch.py" in gap.lower() or "review-prompt.py" in gap.lower():
                            for para in re.split(r"\r?\n[ \t]*\r?\n", gap):
                                _extract_commands(" ".join(para.split()), from_code_block=False)
                        _extract_commands(" ".join(m.group(2).split()), from_code_block=False)
                    tail = prose[pos:]
                    if "review-dispatch.py" in tail.lower() or "review-prompt.py" in tail.lower():
                        for para in re.split(r"\r?\n[ \t]*\r?\n", tail):
                            _extract_commands(" ".join(para.split()), from_code_block=False)

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
        semicolon_reversed_inline = dict(texts)
        semicolon_reversed_inline[".agents/skills/review-todo-section/SKILL.md"] = (
            semicolon_reversed_inline[".agents/skills/review-todo-section/SKILL.md"]
            .replace(
                f"```powershell\n{p_line}\n{d_line}\n```",
                f"`{d_line}; {p_line}`",
            )
        )
        self.assertNotEqual(
            semicolon_reversed_inline[".agents/skills/review-todo-section/SKILL.md"],
            texts[".agents/skills/review-todo-section/SKILL.md"],
        )
        self.assertTrue(self._check_ten_writer_skills(semicolon_reversed_inline))
        commented_prompt = dict(texts)
        commented_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            commented_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace(p_line, f"# {p_line}")
        )
        self.assertTrue(self._check_ten_writer_skills(commented_prompt))
        semicolon_comment_prompt = dict(texts)
        semicolon_comment_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            semicolon_comment_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace(p_line, f"Write-Verbose 'completion';# {p_line}")
        )
        self.assertTrue(self._check_ten_writer_skills(semicolon_comment_prompt))
        write_output_prompt = dict(texts)
        write_output_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            write_output_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace(p_line, f"Write-Output {p_line}")
        )
        self.assertTrue(self._check_ten_writer_skills(write_output_prompt))
        quoted_script_path = dict(texts)
        quoted_script_path[".agents/skills/review-todo-section/SKILL.md"] = (
            quoted_script_path[".agents/skills/review-todo-section/SKILL.md"]
            .replace("scripts/review-prompt.py --kind completion", '"scripts/review-prompt.py" --kind completion')
        )
        self.assertEqual(self._check_ten_writer_skills(quoted_script_path), [])
        version_flag_prompt = dict(texts)
        version_flag_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            version_flag_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace("build\\python.cmd scripts/review-prompt.py --kind completion",
                     "build\\python.cmd -V scripts/review-prompt.py --kind completion")
        )
        self.assertTrue(self._check_ten_writer_skills(version_flag_prompt))
        call_op_prompt = dict(texts)
        call_op_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            call_op_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace("build\\python.cmd scripts/review-prompt.py --kind completion",
                     "& build\\python.cmd scripts/review-prompt.py --kind completion")
        )
        self.assertEqual(self._check_ten_writer_skills(call_op_prompt), [])
        help_flag_prompt = dict(texts)
        help_flag_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            help_flag_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace("review-prompt.py --kind completion", "review-prompt.py --help --kind completion")
        )
        self.assertTrue(self._check_ten_writer_skills(help_flag_prompt))
        xutf8_prompt = dict(texts)
        xutf8_prompt[".agents/skills/review-todo-section/SKILL.md"] = (
            xutf8_prompt[".agents/skills/review-todo-section/SKILL.md"]
            .replace("build\\python.cmd scripts/review-prompt.py --kind completion",
                     "build\\python.cmd -Xutf8 scripts/review-prompt.py --kind completion")
        )
        self.assertEqual(self._check_ten_writer_skills(xutf8_prompt), [])
        missing_prompt_candidate = dict(texts)
        missing_prompt_candidate[".agents/skills/review-todo-section/SKILL.md"] = (
            missing_prompt_candidate[".agents/skills/review-todo-section/SKILL.md"]
            .replace("review-prompt.py --kind completion --candidate <full tip SHA> ",
                     "review-prompt.py --kind completion ")
        )
        self.assertTrue(self._check_ten_writer_skills(missing_prompt_candidate))
        bad_timeout_dispatch = dict(texts)
        bad_timeout_dispatch[".agents/skills/review-todo-section/SKILL.md"] = (
            bad_timeout_dispatch[".agents/skills/review-todo-section/SKILL.md"]
            .replace("review-dispatch.py --kind completion ", "review-dispatch.py --kind completion --timeout 30s ")
        )
        self.assertTrue(self._check_ten_writer_skills(bad_timeout_dispatch))
        bad_head_candidate = dict(texts)
        bad_head_candidate[".agents/skills/review-todo-section/SKILL.md"] = (
            bad_head_candidate[".agents/skills/review-todo-section/SKILL.md"]
            .replace("review-dispatch.py --kind completion --candidate <full tip SHA>",
                     "review-dispatch.py --kind completion --candidate HEAD")
        )
        self.assertTrue(self._check_ten_writer_skills(bad_head_candidate))
        bad_prompt_ref = dict(texts)
        bad_prompt_ref[".agents/skills/review-todo-section/SKILL.md"] = (
            bad_prompt_ref[".agents/skills/review-todo-section/SKILL.md"]
            .replace('review-prompt.py --kind completion --candidate <full tip SHA> --ref "D<NN> T<NN> §<N>"',
                     "review-prompt.py --kind completion --candidate <full tip SHA> --ref D00-T05-s1")
        )
        self.assertTrue(self._check_ten_writer_skills(bad_prompt_ref))
        bad_prompt_candidate = dict(texts)
        bad_prompt_candidate[".agents/skills/review-todo-section/SKILL.md"] = (
            bad_prompt_candidate[".agents/skills/review-todo-section/SKILL.md"]
            .replace("review-prompt.py --kind completion --candidate <full tip SHA>",
                     "review-prompt.py --kind completion --candidate HEDA")
        )
        self.assertTrue(self._check_ten_writer_skills(bad_prompt_candidate))
        overridden_writer_placeholder = dict(texts)
        overridden_writer_placeholder[".agents/skills/review-todo-section/SKILL.md"] = (
            overridden_writer_placeholder[".agents/skills/review-todo-section/SKILL.md"]
            .replace("--writer <claude|agy> --prompt", "--writer <claude|agy> --writ=claude --prompt")
        )
        self.assertTrue(self._check_ten_writer_skills(overridden_writer_placeholder))
        bad_script_dir = dict(texts)
        bad_script_dir[".agents/skills/review-todo-section/SKILL.md"] = (
            bad_script_dir[".agents/skills/review-todo-section/SKILL.md"]
            .replace("scripts/review-prompt.py --kind completion", "script/review-prompt.py --kind completion")
        )
        self.assertTrue(self._check_ten_writer_skills(bad_script_dir))
        bad_launcher_dir = dict(texts)
        bad_launcher_dir[".agents/skills/review-todo-section/SKILL.md"] = (
            bad_launcher_dir[".agents/skills/review-todo-section/SKILL.md"]
            .replace("build\\python.cmd scripts/review-prompt.py --kind completion",
                     "buil\\python.cmd scripts/review-prompt.py --kind completion")
        )
        self.assertTrue(self._check_ten_writer_skills(bad_launcher_dir))
        quoted_launcher_no_amp = dict(texts)
        quoted_launcher_no_amp[".agents/skills/review-todo-section/SKILL.md"] = (
            quoted_launcher_no_amp[".agents/skills/review-todo-section/SKILL.md"]
            .replace("build\\python.cmd scripts/review-prompt.py --kind completion",
                     '"build\\python.cmd" scripts/review-prompt.py --kind completion')
        )
        self.assertTrue(self._check_ten_writer_skills(quoted_launcher_no_amp))
        quoted_launcher_with_amp = dict(texts)
        quoted_launcher_with_amp[".agents/skills/review-todo-section/SKILL.md"] = (
            quoted_launcher_with_amp[".agents/skills/review-todo-section/SKILL.md"]
            .replace("build\\python.cmd scripts/review-prompt.py --kind completion",
                     '& "build\\python.cmd" scripts/review-prompt.py --kind completion')
        )
        self.assertEqual([], self._check_ten_writer_skills(quoted_launcher_with_amp))
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
