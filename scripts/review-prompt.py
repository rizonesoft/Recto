#!/usr/bin/env python3
"""Fill one of the review prompt templates in docs/ and write the prompt to send to the reviewer.

    python scripts/review-prompt.py --kind round --candidate <commit> --ref "D00 T01 §7" \
        --purpose "what the change is for" --governing docs/plan.md --out <scratch>/round.md
    python scripts/review-prompt.py --kind reconciliation --candidate <repaired SHA> --failed <completion SHA> \
        --findings docs/reviews/00-core/D00-T01-s1.md --gates <scratch>/gates.txt --governing docs/plan.md \
        --out <scratch>/reconciliation.md

`--kind` picks the template (plan, round, completion or reconciliation) from the docs/ folder beside this script's
parent folder. The prompt is the template's text after its `---` line, with every placeholder (a backtick span that
starts with `<` and ends with `>`) replaced by its value. A placeholder with no value is a refusal naming it, as is a
value the kind has no place for: exit 2, a message on stderr, and nothing written, an existing `--out` untouched.
Without `--ref` the lines that name the TODO section and its file are dropped; with `--ref` and no `--todo`, the file
comes from `scripts/todo-graph.py resolve`, and with `--ref` and no `--findings` the section's findings file is
`<receipts>/<NN-domain>/D<NN>-T<NN>-s<N>.md`, `<receipts>` being `todo/plan.json`'s `layout.receipts` (`docs/reviews`
by default, read by `scripts/review_layout.py`). Every kind has a Fidelity line: `--fidelity` fills it, and with
`--ref`, no `--fidelity` and `scripts/todo-graph.py` beside this script it is the section's own `**Fidelity:**` value
on `Surface: yes` (a `Surface: yes` section naming none is refused) and `none (Surface: no)` otherwise. Prints the output path and exits 0. Stdlib only; writes are atomic.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

TEMPLATES = {"plan": "plan-review-prompt.md", "round": "review-round-prompt.md",
             "completion": "completion-review-prompt.md", "reconciliation": "reconciliation-review-prompt.md"}
SHA = re.compile(r"[0-9a-f]{40}")
REF = re.compile(r"D\d{2} T\d{2} §\d+")
SPAN = re.compile(r"`[^`\n]+`")
SECTION_TOKEN = "<DNN TNN §N>"
TODO_FILE = "<path to the TODO file>"
FIDELITY_SPAN = "`<the design source the section names>`, or `none (Surface: no)`"
CANDIDATE_SPAN = "`<FULL_SHA>` or `<BASE_SHA>..<TIP_SHA>`"
DEFAULT_FIDELITY = "none (Surface: no)"


class Refused(Exception):
    pass


def one_line(value: str | None) -> str | None:
    """A single-line value (whitespace collapsed), or None when nothing was given."""
    value = " ".join((value or "").split())
    return value or None


def ticked(value: str) -> str:
    return value if "`" in value else f"`{value}`"


def read_template(kind: str) -> str:
    path = Path(__file__).resolve().parent.parent / "docs" / TEMPLATES[kind]
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise Refused(f"template {path} cannot be read: {error.strerror or error}")
    head, rule, body = text.partition("\n---\n")
    if not rule:
        raise Refused(f"template {path} has no `---` line before the prompt")
    return body.lstrip("\n")


GRAPH = Path(__file__).resolve().parent / "todo-graph.py"


def resolve_section(ref: str, option: str) -> tuple[dict, int]:
    """What `todo-graph.py resolve` prints for the section, and its exit code. `option` is the one the writer gives
    instead when the lookup cannot answer."""
    if not GRAPH.is_file():
        raise Refused(f"{option} is needed: {GRAPH.name} is not beside this script to look up {ref}")
    try:
        result = subprocess.run([sys.executable, "-B", str(GRAPH), "resolve", ref], capture_output=True, text=True,
                                timeout=60, encoding="utf-8", errors="replace")  # the graph prints UTF-8
        data = json.loads(result.stdout)
    except (OSError, subprocess.SubprocessError, ValueError) as error:
        raise Refused(f"{option} is needed: looking up {ref} failed ({error})")
    if not isinstance(data, dict):
        raise Refused(f"{option} is needed: {GRAPH.name} resolve {ref!r} printed no section")
    return data, result.returncode


def resolve_todo(ref: str, resolved: tuple[dict, int] | None = None) -> str:
    data, code = resolved if resolved is not None else resolve_section(ref, "--todo")
    path = data.get("path")
    if not isinstance(path, str) or not path:
        raise Refused(f"--todo is needed: {GRAPH.name} resolve {ref!r} named no file (exit {code})")
    return path


def section_field(body: str, name: str) -> str | None:
    """The one non-empty value of a `**Name:** value` line of a section (matching todo_graph.py: empty or duplicate
    occurrences return None)."""
    matches = re.findall(r"^(?:> )?\*\*" + re.escape(name) + r":\*\*(?:[ \t]*(.*))?$", body, re.M)
    if len(matches) != 1:
        return None
    value = matches[0].strip()
    return value if value else None


def _extract_section_body(text: str, number: int) -> str | None:
    """Body of `## <number>. ...` up to the next `##` heading (matching todo_graph.section_text)."""
    m = re.search(rf"^## {number}\. [^\n]*\n(.*?)(?=^##(?:\s|$)|\Z)", text, re.M | re.S)
    return m.group(1) if m else None


def _find_todo_at_commit(root: Path, candidate: str, ref: str) -> str | None:
    """Locate `ref`'s TODO file inside `candidate` when the file was renamed after `candidate`."""
    m = re.fullmatch(r"D(\d{2}) T(\d{2}) §([1-9]\d*)", ref)
    if not m:
        return None
    try:
        listed = subprocess.run(
            ["git", "-C", str(root), "ls-tree", "-r", "--name-only", candidate, "--", "todo/"],
            capture_output=True, text=True, timeout=60, encoding="utf-8", errors="replace",
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise Refused(f"--fidelity is needed: listing todo/ at candidate {candidate} failed ({error})")
    if listed.returncode != 0:
        return None
    pat = re.compile(rf"^todo/{m[1]}-[^/]+/TODO-{m[2]}-[^/]+\.md$")
    matches = sorted(line.strip() for line in listed.stdout.splitlines() if pat.fullmatch(line.strip()))
    return matches[0] if len(matches) == 1 else None


_GIT_STORE_ENV_VARS = (
    "GIT_DIR",
    "GIT_WORK_TREE",
    "GIT_COMMON_DIR",
    "GIT_OBJECT_DIRECTORY",
    "GIT_ALTERNATE_OBJECT_DIRECTORIES",
)


def _git_across_fs(env: dict[str, str], c_env: dict[str, str]) -> bool:
    """True when `GIT_DISCOVERY_ACROSS_FILESYSTEM` is truthy under Git's own `--type=bool` parser."""
    val = env.get("GIT_DISCOVERY_ACROSS_FILESYSTEM")
    if not val:
        return False
    bool_env = {
        k: v for k, v in c_env.items()
        if k not in ("GIT_CONFIG", "GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT")
    }
    try:
        done = subprocess.run(
            ["git", "-c", f"review.cross={val}", "config", "--type=bool", "review.cross"],
            capture_output=True, text=True, timeout=60, env=bool_env,
        )
    except (OSError, subprocess.SubprocessError):
        return True  # conservative: keep scanning if Git cannot parse the override
    return done.returncode == 0 and done.stdout.strip().lower() == "true"


def _has_git_marker_within_discovery_boundary(root: Path, env: dict[str, str], c_env: dict[str, str]) -> bool:
    """True when `root` or an ancestor before `GIT_CEILING_DIRECTORIES` and (unless `GIT_DISCOVERY_ACROSS_FILESYSTEM`
    is truthy) before a filesystem mount boundary holds a `.git` entry (including a dangling `.git` symlink) or when
    `lstat(".git")` / `stat()` fails with an unexpected `OSError` such as `EIO` or `EACCES`."""
    ceilings = {
        os.path.normcase(os.path.realpath(entry))
        for entry in env.get("GIT_CEILING_DIRECTORIES", "").split(os.pathsep)
        if entry and os.path.isabs(entry)
    }
    across_fs = _git_across_fs(env, c_env)
    cur = Path(os.path.realpath(root))
    try:
        root_dev = os.stat(cur).st_dev
    except OSError:
        return True
    while True:
        try:
            os.lstat(cur / ".git")
            return True
        except (FileNotFoundError, NotADirectoryError):
            pass
        except OSError:
            return True
        parent = cur.parent
        if parent == cur or os.path.normcase(str(parent)) in ceilings:
            return False
        if not across_fs:
            try:
                if os.stat(parent).st_dev != root_dev:
                    return False
            except OSError:
                return True
        cur = parent


def _section_body_at_commit(root: Path, candidate: str, rel_path: str | None, ref: str,
                            explicit_todo: str | None = None) -> str | None:
    """Read `ref`'s section body from `candidate` when `root` is inside a git repository. Inside a git repository
    (including when `GIT_WORK_TREE` points elsewhere), an unavailable candidate or git failure/timeout refuses rather
    than silently falling back to the working tree. Returns None only when Git's own `rev-parse --git-dir` (both with
    caller env and with repo/store overrides stripped) and a boundary-aware `.git` check (respecting
    `GIT_CEILING_DIRECTORIES` and filesystem mount boundaries) prove `root` is outside any git repository."""
    c_env = {**os.environ, "LC_ALL": "C", "LANG": "C", "LANGUAGE": "C"}
    try:
        probe_repo = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--git-dir"],
            capture_output=True, text=True, timeout=60, env=c_env,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise Refused(f"--fidelity is needed: checking git repository for candidate {candidate} failed ({error})")
    if probe_repo.returncode != 0:
        err = (probe_repo.stderr or "").strip()
        if "not a git repository (or any" not in err.lower():
            raise Refused(
                f"--fidelity is needed: git repository check failed for {root} "
                f"(exit {probe_repo.returncode}: {err or 'no stderr'})"
            )
        if _has_git_marker_within_discovery_boundary(root, os.environ, c_env):
            raise Refused(
                f"--fidelity is needed: git repository check failed for {root} "
                f"(exit {probe_repo.returncode}: {err or 'no stderr'})"
            )
        if any(os.environ.get(var) for var in _GIT_STORE_ENV_VARS):
            clean_env = {k: v for k, v in c_env.items() if k not in _GIT_STORE_ENV_VARS}
            try:
                clean_probe = subprocess.run(
                    ["git", "-C", str(root), "rev-parse", "--git-dir"],
                    capture_output=True, text=True, timeout=60, env=clean_env,
                )
            except (OSError, subprocess.SubprocessError) as error:
                raise Refused(
                    f"--fidelity is needed: checking git repository for candidate {candidate} failed ({error})"
                )
            clean_err = (clean_probe.stderr or "").strip()
            if clean_probe.returncode == 0 or "not a git repository (or any" not in clean_err.lower():
                raise Refused(
                    f"--fidelity is needed: git repository check failed for {root} "
                    f"(exit {probe_repo.returncode}: {err or 'no stderr'})"
                )
        return None
    try:
        probe = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--verify", "--quiet", f"{candidate}^{{commit}}"],
            capture_output=True, text=True, timeout=60,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise Refused(f"--fidelity is needed: verifying candidate {candidate} in git failed ({error})")
    if probe.returncode != 0:
        raise Refused(f"--fidelity is needed: candidate {candidate} is not an available commit in {root}")
    candidates_to_try = []
    for p in (explicit_todo, rel_path):
        if isinstance(p, str) and p and p not in candidates_to_try:
            candidates_to_try.append(p)
    shown = None
    used_path = None
    for p in candidates_to_try:
        try:
            res = subprocess.run(
                ["git", "-C", str(root), "show", f"{candidate}:{p}"],
                capture_output=True, timeout=60,
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise Refused(f"--fidelity is needed: reading {p} at {candidate} failed ({error})")
        if res.returncode == 0:
            shown = res
            used_path = p
            break
    if shown is None:
        # Handle a TODO file renamed after `candidate` while keeping its DNN TNN identity.
        historical = _find_todo_at_commit(root, candidate, ref)
        if historical:
            try:
                res = subprocess.run(
                    ["git", "-C", str(root), "show", f"{candidate}:{historical}"],
                    capture_output=True, timeout=60,
                )
            except (OSError, subprocess.SubprocessError) as error:
                raise Refused(f"--fidelity is needed: reading {historical} at {candidate} failed ({error})")
            if res.returncode == 0:
                shown = res
                used_path = historical
    if shown is None:
        named = explicit_todo or rel_path or ref
        raise Refused(f"--fidelity is needed: {named} is not in candidate {candidate}")
    num_match = re.search(r"§(\d+)$", ref)
    if not num_match:
        raise Refused(f"--fidelity is needed: cannot parse section number from {ref!r}")
    body = _extract_section_body(shown.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n"),
                                 int(num_match.group(1)))
    if body is None:
        raise Refused(f"--fidelity is needed: {ref} is not in {used_path} at candidate {candidate}")
    return body


def resolve_fidelity(ref: str, candidate: str | None = None, resolved: tuple[dict, int] | None = None,
                     explicit_todo: str | None = None) -> str | None:
    """The design source the section itself names, so the reviewer reads the contract the section was planned
    against without the writer retyping it: its `**Fidelity:**` value on `Surface: yes`, None (the template's
    `none (Surface: no)`) only on an explicit, unambiguous `Surface: no`. Always requires the graph response to carry
    a non-empty `body` (so graphs without body support refuse instead of silently defaulting), and when `root` is a
    git repository reads the section from `candidate` rather than the working tree. Without todo-graph.py beside this
    script, there is nothing to look up and the default stands."""
    if not GRAPH.is_file():
        return None
    data, _code = resolved if resolved is not None else resolve_section(ref, "--fidelity")
    graph_body = data.get("body")
    if not isinstance(graph_body, str) or not graph_body.strip():
        raise Refused(f"--fidelity is needed: {GRAPH.name} resolve {ref!r} printed no section body")
    body = None
    if candidate:
        body = _section_body_at_commit(
            Path(__file__).resolve().parent.parent, candidate, data.get("path"), ref, explicit_todo=explicit_todo
        )
    if body is None:
        body = graph_body
    surface = section_field(body, "Surface")
    if surface == "no":
        return None
    if surface != "yes":
        raise Refused(f"--fidelity is needed: {ref} has no single unambiguous Surface: yes or Surface: no field")
    fidelity = section_field(body, "Fidelity")
    if not fidelity:
        raise Refused(f"--fidelity is needed: {ref} has Surface: yes and names no one Fidelity design source")
    return fidelity


def section_findings(ref: str) -> str:
    """The section's findings file for --ref: `<receipts>/<NN-domain>/D<NN>-T<NN>-s<N>.md`, the domain the one
    `todo/<NN-domain>/` folder its number names, the folder todo/plan.json's layout.receipts."""
    here = Path(__file__).resolve()
    spec = importlib.util.spec_from_file_location("review_layout", here.with_name("review_layout.py"))
    if spec is None or not here.with_name("review_layout.py").is_file():
        raise Refused(f"--findings is needed: review_layout.py is not beside this script to find {ref}'s file")
    layout = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(layout)
    root = here.parent.parent
    try:
        folder = layout.receipts_folder(root)
    except layout.LayoutError as error:
        raise Refused(str(error)) from None
    m = re.fullmatch(r"D(\d{2}) T(\d{2}) §(\d+)", ref)
    todo = root / "todo"
    domains = sorted(d.name for d in todo.iterdir() if d.is_dir() and d.name.startswith(f"{m[1]}-")) \
        if todo.is_dir() else []
    if len(domains) != 1:
        raise Refused(f"--findings is needed: no single todo/{m[1]}-<domain> folder for {ref}")
    return f"{folder}/{domains[0]}/D{m[1]}-T{m[2]}-s{m[3]}.md"


def fence(content: str) -> str:
    ticks = "`" * max(3, max((len(run) for run in re.findall(r"`+", content)), default=0) + 1)
    return f"{ticks}\n{content}\n{ticks}"


def fill(body: str, args: argparse.Namespace, todo: str | None, gates: str | None) -> tuple[str, set[str], list[str]]:
    """Returns the filled prompt, the options whose values landed, and the placeholders left without a value."""
    used: set[str] = set()
    unresolved: list[str] = []
    sha = {"<FULL_SHA>": args.candidate, "<COMPLETION_SHA>": args.failed, "<LAST_SHA>": args.last,
           "<BASE_SHA>": args.base, "<TIP_SHA>": args.candidate}
    option = {"<FULL_SHA>": "--candidate", "<COMPLETION_SHA>": "--failed", "<LAST_SHA>": "--last",
              "<BASE_SHA>": "--base", "<TIP_SHA>": "--candidate"}
    purpose, governing = one_line(args.purpose), one_line(args.governing)
    fidelity = one_line(args.fidelity)
    findings = one_line(args.findings)

    def need(value, flag, placeholder):
        if value:
            used.add(flag)
            return value
        unresolved.append(f"`{placeholder}` (give {flag})")
        return None

    def span(match: re.Match) -> str:
        text = match.group(0)
        inner = text[1:-1]
        for token, value in sha.items():
            if token in inner and value:
                inner = inner.replace(token, value)
                used.add(option[token])
        for token in sha:
            if token in inner:
                missing = f"`{token}` (give {option[token]})"
                if missing not in unresolved:
                    unresolved.append(missing)
        if SECTION_TOKEN in inner and args.ref:
            inner = inner.replace(SECTION_TOKEN, args.ref)
            used.add("--ref")
        if not (inner.startswith("<") and inner.endswith(">")):
            if inner == text[1:-1]:
                return text
            return f"`{inner}`"
        if inner == TODO_FILE:
            value = need(todo, "--todo", inner)
            return ticked(value) if value else text
        if inner.startswith("<one or two sentences"):
            value = need(purpose, "--purpose", inner)
            return value.rstrip(".") if value else text
        if inner.endswith("that applies>"):
            value = need(governing, "--governing", inner)
            return ticked(value) if value else text
        if inner.startswith("<docs/reviews/"):
            value = need(findings, "--findings", inner)
            return ticked(value) if value else text
        if inner == "<quoted output>":
            if gates:
                used.add("--gates")
                gate_hit[0] = True
                return gate_mark
            unresolved.append(f"`{inner}` (give --gates)")
            return text
        if f"`{inner}`" not in unresolved:
            unresolved.append(f"`{inner}`")
        return text

    out_lines: list[str] = []
    gate_mark = "\0" + os.urandom(8).hex() + "\0"   # NUL cannot arrive in an argument or a template line
    gate_hit = [False]
    for line in body.splitlines():
        if not args.ref and SECTION_TOKEN in line:
            continue
        if not args.ref and not todo and TODO_FILE in line:
            continue
        if CANDIDATE_SPAN in line:
            used.add("--candidate")
            if args.base:
                used.add("--base")
                line = line.replace(CANDIDATE_SPAN, f"`{args.base}..{args.candidate}`, tip `{args.candidate}`")
            else:
                line = line.replace(CANDIDATE_SPAN, f"`{args.candidate}`")
        if FIDELITY_SPAN in line:
            used.add("--fidelity")
            line = line.replace(FIDELITY_SPAN, ticked(fidelity or DEFAULT_FIDELITY))
        gate_hit[0] = False
        line = SPAN.sub(span, line)
        # Only where the template's own gate placeholder was just replaced, by a mark made for this run: a value the
        # writer supplied is never rescanned for it, whatever text it holds.
        if gate_hit[0]:
            if "\n" in gates or "`" in gates:
                line = line.replace(gate_mark, "").rstrip().rstrip(".").rstrip() + "\n\n" + fence(gates) + "\n"
            else:
                line = line.replace(gate_mark, f"`{gates}`")
        out_lines.append(line)
    return "\n".join(out_lines).rstrip("\n") + "\n", used, unresolved


def atomic_write(path: Path, text: str) -> None:
    fd, temp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.", suffix=".tmp")
    try:
        try:
            handle = os.fdopen(fd, "w", encoding="utf-8", newline="\n")
        except BaseException:
            os.close(fd)  # the descriptor is still ours: left open, Windows could not remove the temporary file
            raise
        with handle:
            handle.write(text)
        os.replace(temp, path)
    except BaseException:
        try:
            os.unlink(temp)
        except FileNotFoundError:
            pass  # the replace already moved it
        raise


def resolve_commit(name: str, value: str) -> str:
    """The full SHA a commit name stands for: a full lowercase SHA as given, any other name (an abbreviated SHA, HEAD,
    a branch) through `git rev-parse --verify <name>^{commit}` in this project. Only a name git cannot resolve to a
    commit is refused: a short SHA once cost a whole rerun (D02 T01 §3)."""
    if SHA.fullmatch(value):
        return value
    root = Path(__file__).resolve().parent.parent
    try:
        done = subprocess.run(["git", "-C", str(root), "rev-parse", "--verify", "--quiet", f"{value}^{{commit}}"],
                              capture_output=True, text=True, timeout=60) if value and not value.startswith("-") else None
    except (OSError, subprocess.SubprocessError) as error:
        raise Refused(f"--{name} {value!r} is not a full 40-character commit SHA, and git could not be asked to "
                      f"resolve it ({error})")
    resolved = done.stdout.strip() if done is not None and done.returncode == 0 else ""
    if not SHA.fullmatch(resolved):
        raise Refused(f"--{name} must be a full 40-character commit SHA or a name git resolves to a commit in "
                      f"{root}, got {value!r}")
    return resolved


def validate_template_args(args: argparse.Namespace) -> None:
    """Validate post-parse arguments (including git commit resolution, --ref format, --fidelity and template
    placeholder/extra-flag checks) without writing --out."""
    effective = argparse.Namespace(**vars(args))
    for name in ("candidate", "base", "failed", "last"):
        value = getattr(effective, name)
        if value is not None:
            setattr(effective, name, resolve_commit(name, value))
    if effective.ref is not None and not REF.fullmatch(effective.ref):
        raise Refused(f"--ref must look like 'D00 T01 §7', got {effective.ref!r}")
    if effective.fidelity is not None and not one_line(effective.fidelity):
        raise Refused("--fidelity must not be blank (omit it to let --ref read the section's Surface and Fidelity)")
    body = read_template(effective.kind)
    given_last = effective.last
    if effective.kind == "reconciliation" and effective.failed and not effective.last:
        effective.last = effective.failed
    todo = one_line(effective.todo)
    if not todo and effective.ref and TODO_FILE in body:
        todo = "todo/00-workspace/TODO-01.md"
    if not effective.findings and effective.ref and "`<docs/reviews/" in body:
        effective.findings = section_findings(effective.ref)
    if effective.fidelity is None and effective.ref and FIDELITY_SPAN in body:
        effective.fidelity = DEFAULT_FIDELITY
    gates = "ok" if effective.gates is not None else None
    _, used, unresolved = fill(body, effective, todo, gates)
    if unresolved:
        raise Refused("no value for " + "; ".join(unresolved))
    given = {"--base": args.base, "--ref": args.ref, "--todo": args.todo, "--fidelity": args.fidelity,
             "--purpose": args.purpose, "--governing": args.governing, "--gates": args.gates,
             "--findings": args.findings, "--failed": args.failed, "--last": given_last}
    extra = sorted(flag for flag, value in given.items() if value is not None and flag not in used)
    if extra:
        raise Refused(f"the {args.kind} prompt has no place for {', '.join(extra)}")


def run(args: argparse.Namespace) -> Path:
    for name in ("candidate", "base", "failed", "last"):
        value = getattr(args, name)
        if value is not None:
            setattr(args, name, resolve_commit(name, value))
    if args.ref is not None and not REF.fullmatch(args.ref):
        raise Refused(f"--ref must look like 'D00 T01 §7', got {args.ref!r}")
    given_last = args.last
    if args.kind == "reconciliation" and args.failed and not args.last:
        args.last = args.failed
    body = read_template(args.kind)
    todo = one_line(args.todo)
    explicit_todo = todo or None
    resolved = None
    if not todo and args.ref and TODO_FILE in body:
        resolved = resolve_section(args.ref, "--todo")
        todo = resolve_todo(args.ref, resolved=resolved)
    if not args.findings and args.ref and "`<docs/reviews/" in body:
        args.findings = section_findings(args.ref)
    given_fidelity = args.fidelity
    if args.fidelity is not None and not one_line(args.fidelity):
        raise Refused("--fidelity must not be blank (omit it to let --ref read the section's Surface and Fidelity)")
    if args.fidelity is None and args.ref and FIDELITY_SPAN in body:
        if resolved is None and GRAPH.is_file():
            resolved = resolve_section(args.ref, "--fidelity")
        args.fidelity = resolve_fidelity(args.ref, args.candidate, resolved=resolved, explicit_todo=explicit_todo)
    gates = None
    if args.gates is not None:
        try:
            gates = Path(args.gates).read_text(encoding="utf-8").strip("\n")
        except (OSError, UnicodeError) as error:
            raise Refused(f"--gates {args.gates} cannot be read: {getattr(error, 'strerror', None) or error}")
        if not gates.strip():
            raise Refused(f"--gates {args.gates} is empty")
    text, used, unresolved = fill(body, args, todo, gates)
    if unresolved:
        raise Refused("no value for " + "; ".join(unresolved))
    given = {"--base": args.base, "--ref": args.ref, "--todo": args.todo, "--fidelity": given_fidelity,
             "--purpose": args.purpose, "--governing": args.governing, "--gates": args.gates,
             "--findings": args.findings, "--failed": args.failed, "--last": given_last}
    # A value the template has no place for is refused, never dropped silently.
    extra = sorted(flag for flag, value in given.items() if value is not None and flag not in used)
    if extra:
        raise Refused(f"the {args.kind} prompt has no place for {', '.join(extra)}")
    out = Path(args.out)
    try:
        if out.is_dir():
            raise IsADirectoryError("is a directory")
        atomic_write(out, text)
    except OSError as error:
        raise Refused(f"--out {out} cannot be written: {error.strerror or error}")
    return out


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--kind", required=True, choices=sorted(TEMPLATES))
    parser.add_argument("--candidate", required=True,
                        help="the commit under review: a full SHA, or any name git resolves (an abbreviated SHA, HEAD)")
    parser.add_argument("--out", required=True, help="the file the filled prompt is written to")
    parser.add_argument("--base", help="the first commit before the range: the prompt names <base>..<candidate>")
    parser.add_argument("--ref", help="the TODO section, 'D00 T01 §7'; without it the TODO lines are dropped")
    parser.add_argument("--todo", help="the TODO file; found with todo-graph.py resolve when --ref is given")
    parser.add_argument("--fidelity",
                        help="the design source the section names, on every kind; with --ref and todo-graph.py it is "
                             f"read from the section's Fidelity field (default: {DEFAULT_FIDELITY})")
    parser.add_argument("--purpose", help="one or two sentences on what the change is for (round, completion)")
    parser.add_argument("--governing", help="the spec, plan or design file that applies (all but round need it too)")
    parser.add_argument("--gates", help="a file holding the gate output (reconciliation)")
    parser.add_argument("--findings", help="the findings file of the section (reconciliation)")
    parser.add_argument("--failed", help="the failed completion's candidate SHA (reconciliation)")
    parser.add_argument("--last", help="the last reviewed candidate SHA (reconciliation; default --failed)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        print(run(args))
    except Refused as error:
        print(f"review-prompt refused: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
