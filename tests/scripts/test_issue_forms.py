"""Tests for Recto's intake files (D07 T03 §1): the issue forms, labels, SECURITY.md and the triage policy.

The forms are YAML. Recto's tooling is stdlib Python, so this module carries a strict loader for the block
YAML subset the forms use; it refuses anything outside that subset rather than guessing. When PyYAML happens
to be installed, the loader is cross-checked against it.
"""
import copy
import json
import pathlib
import re
import unittest

REPO = pathlib.Path(__file__).resolve().parents[2]
TEMPLATES = REPO / ".github" / "ISSUE_TEMPLATE"
LABELS = REPO / ".github" / "labels.yml"
SECURITY = REPO / "SECURITY.md"
TRIAGE = REPO / "docs" / "support" / "triage.md"

ISSUES_SEARCH_URL = "https://github.com/rizonesoft/Recto/issues?q="
ADVISORY_URL = "https://github.com/rizonesoft/Recto/security/advisories/new"

# Bug form fields that must be required; version, install-type and windows-build are also prefilled by
# Help -> Report a problem (D07 T03 §2) through URL query parameters, which only fill text fields.
BUG_REQUIRED = {
    "version": "input",
    "install-type": "input",
    "windows-build": "input",
    "gpu": "input",
    "steps": "textarea",
    "expected": "textarea",
    "actual": "textarea",
    "file": "textarea",
}
FEATURE_REQUIRED = {"task": "textarea", "current-app": "input"}
LABEL_GROUPS = {
    "area": {"engine", "viewer", "text", "convert", "organize", "print", "install"},
    "kind": {"bug", "feature", "regression", "data-loss", "crash", "security"},
    "state": {"needs-repro", "fixed-on-main", "released"},
}
ELEMENT_TYPES = {"markdown", "textarea", "input", "dropdown", "checkboxes", "upload"}
TRIAGE_HEADINGS = [
    "## Labels",
    "## Response targets",
    "## A person closes every issue",
    "## Duplicates link the canonical issue",
    "## Feature requests are never closed as support",
    "## Asking for a reproducer",
    "### Making a synthetic file",
    "## One owner per issue",
    "## Superseded pull requests",
    "## Closing a fixed bug",
]


class YamlSubsetError(ValueError):
    pass


_KEY = re.compile(r"^([A-Za-z0-9_-]+):(?:[ ]+(.*))?$")
_INT = re.compile(r"^-?[0-9]+$")


class _Loader:
    """Block mappings, block sequences, `|` and `>` block scalars, quoted and plain scalars, flow lists of scalars."""

    def __init__(self, text):
        if "\t" in text:
            raise YamlSubsetError("tab characters are not allowed")
        self.lines = text.splitlines()
        self.i = 0

    def _fail(self, message):
        raise YamlSubsetError(f"line {self.i + 1}: {message}")

    def _peek(self):
        while self.i < len(self.lines):
            line = self.lines[self.i]
            stripped = line.strip()
            if stripped and not stripped.startswith("#"):
                return len(line) - len(line.lstrip(" ")), line.strip()
            self.i += 1
        return None

    def load(self):
        head = self._peek()
        if head is None:
            return None
        node = self._node(head[0])
        if self._peek() is not None:
            self._fail("unexpected content after the document")
        return node

    def _node(self, indent):
        _, content = self._peek()
        if content == "-" or content.startswith("- "):
            return self._sequence(indent)
        return self._mapping(indent)

    def _mapping(self, indent):
        result = {}
        while (head := self._peek()) is not None and head[0] >= indent:
            if head[0] > indent:
                self._fail("unexpected indentation")
            if head[1] == "-" or head[1].startswith("- "):
                break
            match = _KEY.match(head[1])
            if not match:
                self._fail(f"expected 'key: value', got {head[1]!r}")
            key = match.group(1)
            if key in result:
                self._fail(f"duplicate key {key!r}")
            self.i += 1
            result[key] = self._value(match.group(2), indent)
        return result

    def _sequence(self, indent):
        items = []
        while (head := self._peek()) is not None and head[0] == indent:
            content = head[1]
            if not (content == "-" or content.startswith("- ")):
                break
            rest = content[2:].strip() if content != "-" else ""
            if not rest:
                self.i += 1
                nxt = self._peek()
                if nxt is None or nxt[0] <= indent:
                    items.append(None)
                else:
                    items.append(self._node(nxt[0]))
            elif _KEY.match(rest):
                # An inline mapping item: re-read the rest of the line as the first key at the item's indent.
                self.lines[self.i] = " " * (indent + 2) + rest
                items.append(self._mapping(indent + 2))
            else:
                self.i += 1
                items.append(self._scalar(rest))
        head = self._peek()
        if head is not None and head[0] > indent:
            self._fail("unexpected indentation after a sequence")
        return items

    def _value(self, rest, indent):
        if rest is None or rest.startswith("#"):
            nxt = self._peek()
            if nxt is None or nxt[0] < indent:
                return None
            if nxt[0] > indent:
                return self._node(nxt[0])
            if nxt[1] == "-" or nxt[1].startswith("- "):
                return self._sequence(indent)
            return None
        if rest in ("|", ">"):
            return self._block_scalar(rest, indent)
        return self._scalar(rest)

    def _block_scalar(self, style, indent):
        raw = []
        while self.i < len(self.lines):
            line = self.lines[self.i]
            if line.strip() and len(line) - len(line.lstrip(" ")) <= indent:
                break
            raw.append(line)
            self.i += 1
        while raw and not raw[-1].strip():
            raw.pop()
        if not raw:
            return ""
        block = min(len(line) - len(line.lstrip(" ")) for line in raw if line.strip())
        lines = [line[block:] if line.strip() else "" for line in raw]
        if style == "|":
            return "\n".join(lines) + "\n"
        folded, paragraph = [], []
        for line in lines:
            if line:
                paragraph.append(line)
            else:
                folded.append(" ".join(paragraph))
                paragraph = []
        folded.append(" ".join(paragraph))
        return "\n".join(folded) + "\n"

    def _scalar(self, text):
        text = text.strip()
        if text.startswith('"'):
            end = _closing_quote(text)
            trailing = "" if end is None else text[end + 1 :].strip()
            if end is None or (trailing and not trailing.startswith("#")):
                self._fail(f"bad double-quoted scalar {text!r}")
            return json.loads(text[: end + 1])
        if text.startswith("'"):
            match = re.match(r"^'((?:[^']|'')*)'\s*(#.*)?$", text)
            if not match:
                self._fail(f"bad single-quoted scalar {text!r}")
            return match.group(1).replace("''", "'")
        if text.startswith("["):
            if not text.endswith("]"):
                self._fail(f"unterminated flow list {text!r}")
            inner = text[1:-1].strip()
            return [self._scalar(part) for part in inner.split(",")] if inner else []
        text = re.sub(r"\s+#.*$", "", text)
        if text[:1] in set("{&*!%@`|>") or ": " in text or text.endswith(":"):
            self._fail(f"plain scalar outside the supported subset: {text!r}")
        if text in ("true", "false"):
            return text == "true"
        if text in ("null", "~"):
            return None
        if _INT.match(text):
            return int(text)
        return text


def _closing_quote(text):
    escaped = False
    for index in range(1, len(text)):
        if escaped:
            escaped = False
        elif text[index] == "\\":
            escaped = True
        elif text[index] == '"':
            return index
    return None


def load_yaml(text):
    return _Loader(text).load()


def read_yaml(path):
    return load_yaml(path.read_text(encoding="utf-8"))


def _fields(form):
    return {element.get("id"): element for element in form.get("body", []) if isinstance(element, dict)}


def form_faults(form, required):
    """Structural faults in an issue form, plus any field in `required` that is missing or not required."""
    faults = []
    if not isinstance(form, dict):
        return ["the form is not a mapping"]
    for key in ("name", "description", "body"):
        if not form.get(key):
            faults.append(f"top-level {key!r} is missing")
    seen = set()
    for index, element in enumerate(form.get("body") or []):
        where = f"body[{index}]"
        if not isinstance(element, dict):
            faults.append(f"{where} is not a mapping")
            continue
        kind = element.get("type")
        if kind not in ELEMENT_TYPES:
            faults.append(f"{where} has unknown type {kind!r}")
        attributes = element.get("attributes")
        if not isinstance(attributes, dict):
            faults.append(f"{where} has no attributes")
            continue
        if kind == "markdown":
            if not attributes.get("value"):
                faults.append(f"{where} markdown has no value")
            continue
        if not attributes.get("label"):
            faults.append(f"{where} has no label")
        element_id = element.get("id")
        if element_id is not None:
            if not re.fullmatch(r"[A-Za-z0-9_-]+", str(element_id)):
                faults.append(f"{where} id {element_id!r} uses characters GitHub refuses")
            if element_id in seen:
                faults.append(f"{where} repeats id {element_id!r}")
            seen.add(element_id)
    fields = _fields(form)
    for field_id, kind in required.items():
        element = fields.get(field_id)
        if element is None:
            faults.append(f"required field {field_id!r} is missing")
            continue
        if element.get("type") != kind:
            faults.append(f"field {field_id!r} is {element.get('type')!r}, expected {kind!r}")
        if (element.get("validations") or {}).get("required") is not True:
            faults.append(f"field {field_id!r} lacks 'validations: required: true'")
    return faults


def _form_labels(form):
    labels = form.get("labels") or []
    return [labels] if isinstance(labels, str) else list(labels)


class YamlSubsetLoaderTest(unittest.TestCase):
    def test_loads_the_shapes_the_forms_use(self):
        text = (
            "name: Bug\n"
            "labels: [bug, crash]\n"
            "body:\n"
            "  - type: markdown\n"
            "    attributes:\n"
            "      value: |\n"
            "        Line one.\n"
            "\n"
            "        Line two.\n"
            "  - type: input\n"
            "    id: version\n"
            "    validations:\n"
            "      required: true\n"
            "list:\n"
            "- a\n"
            "- \"b: c\"\n"
        )
        self.assertEqual(
            load_yaml(text),
            {
                "name": "Bug",
                "labels": ["bug", "crash"],
                "body": [
                    {"type": "markdown", "attributes": {"value": "Line one.\n\nLine two.\n"}},
                    {"type": "input", "id": "version", "validations": {"required": True}},
                ],
                "list": ["a", "b: c"],
            },
        )

    def test_refuses_what_it_does_not_understand(self):
        for text in ("a: {b: c}\n", "a: b: c\n", "a:\n\tb: c\n", "a: &anchor x\n", "a: 1\na: 2\n"):
            with self.subTest(text=text), self.assertRaises(YamlSubsetError):
                load_yaml(text)

    def test_agrees_with_pyyaml_on_every_intake_file(self):
        try:
            import yaml
        except ImportError:
            self.skipTest("PyYAML is not installed; the strict loader alone checks the files")
        for path in sorted(TEMPLATES.glob("*.yml")) + [LABELS]:
            with self.subTest(path=path.name):
                text = path.read_text(encoding="utf-8")
                self.assertEqual(load_yaml(text), yaml.safe_load(text))


class IssueFormsTest(unittest.TestCase):
    def setUp(self):
        self.bug = read_yaml(TEMPLATES / "bug_report.yml")
        self.feature = read_yaml(TEMPLATES / "feature_request.yml")
        self.labels = {entry["name"] for entry in read_yaml(LABELS)}

    def test_only_the_two_forms_and_config_exist(self):
        names = sorted(path.name for path in TEMPLATES.iterdir())
        self.assertEqual(names, ["bug_report.yml", "config.yml", "feature_request.yml"])

    def test_bug_form_requires_every_field(self):
        self.assertEqual(form_faults(self.bug, BUG_REQUIRED), [])

    def test_bug_form_offers_optional_diagnostics_and_scrub(self):
        diagnostics = _fields(self.bug).get("diagnostics")
        self.assertIsNotNone(diagnostics, "the optional diagnostics paste from D07 T03 §2 is missing")
        self.assertIsNot((diagnostics.get("validations") or {}).get("required"), True)
        self.assertIn("recto-cli scrub", _fields(self.bug)["file"]["attributes"]["description"])
        self.assertIn("About", _fields(self.bug)["version"]["attributes"]["description"])
        install = _fields(self.bug)["install-type"]["attributes"]["description"]
        for kind in ("Installer", "Portable", "MSIX"):
            self.assertIn(kind, install)

    def test_feature_form_starts_with_a_duplicate_search(self):
        self.assertEqual(form_faults(self.feature, FEATURE_REQUIRED), [])
        first = self.feature["body"][0]
        self.assertEqual(first["type"], "markdown")
        self.assertIn(ISSUES_SEARCH_URL, first["attributes"]["value"])
        self.assertIn("is%3Aopen", first["attributes"]["value"])
        self.assertIn("docs/user-demand.md", first["attributes"]["value"])

    def test_form_labels_exist_in_the_label_file(self):
        for form in (self.bug, self.feature):
            for label in _form_labels(form):
                self.assertIn(label, self.labels)

    def test_removing_a_required_validation_is_caught(self):
        for field_id in BUG_REQUIRED:
            with self.subTest(field=field_id):
                mutated = copy.deepcopy(self.bug)
                del _fields(mutated)[field_id]["validations"]
                self.assertTrue(any(field_id in fault for fault in form_faults(mutated, BUG_REQUIRED)))
        mutated = copy.deepcopy(self.bug)
        mutated["body"] = [e for e in mutated["body"] if e.get("id") != "steps"]
        self.assertIn("required field 'steps' is missing", form_faults(mutated, BUG_REQUIRED))

    def test_config_disables_blank_issues_and_leaves_security_to_github(self):
        config = read_yaml(TEMPLATES / "config.yml")
        self.assertIs(config["blank_issues_enabled"], False)
        # GitHub's chooser lists "Report a security vulnerability" from SECURITY.md on its own; a contact
        # link to the same place shows the entry twice.
        self.assertTrue(SECURITY.is_file())
        for link in config.get("contact_links") or []:
            self.assertNotIn("/security", link.get("url", "").lower(), link)
            self.assertNotIn("SECURITY.md", link.get("url", ""), link)


class LabelsTest(unittest.TestCase):
    def test_every_label_is_described_and_grouped(self):
        entries = read_yaml(LABELS)
        names = [entry["name"] for entry in entries]
        self.assertEqual(len(names), len(set(names)), "a label is listed twice")
        self.assertEqual(set(names), set().union(*LABEL_GROUPS.values()))
        for entry in entries:
            with self.subTest(label=entry["name"]):
                self.assertTrue(entry.get("description"))
                self.assertLessEqual(len(entry["description"]), 100, "GitHub caps label descriptions at 100")
                self.assertRegex(str(entry.get("color")), r"^[0-9a-f]{6}$")


class SecurityPolicyTest(unittest.TestCase):
    def test_security_md_covers_the_four_promises(self):
        text = SECURITY.read_text(encoding="utf-8")
        self.assertIn("## Supported versions", text)
        self.assertIn("Report a vulnerability", text)
        self.assertIn(ADVISORY_URL, text)
        self.assertIn("within 3 working days", text)
        self.assertRegex(text, r"[Ee]very security release ships with a GitHub security advisory")


class TriagePolicyTest(unittest.TestCase):
    def test_triage_has_every_rule_heading(self):
        text = TRIAGE.read_text(encoding="utf-8")
        for heading in TRIAGE_HEADINGS:
            self.assertIn(f"\n{heading}\n", text)
        self.assertIn("recto-cli scrub", text)

    def test_triage_lists_every_label(self):
        text = TRIAGE.read_text(encoding="utf-8")
        for entry in read_yaml(LABELS):
            self.assertIn(f"| `{entry['name']}` |", text)


if __name__ == "__main__":
    unittest.main()
