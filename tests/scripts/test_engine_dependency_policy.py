"""Tests for the PDF engine dependency policy (D00 T01 §2, decisions D3 and D9): Recto builds its own recto-pdf engine
in C++20 with warnings as errors, vendors no engine as a submodule or third_party tree, and names no MuPDF anywhere a
user or the build would see it."""
import html
import html.parser
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ElementTree

REPO = pathlib.Path(__file__).resolve().parents[2]
# mupdf and artifex anywhere (libmupdf.lib, mupdf.h); jbig2dec as a whole word only, because the PDF filter name
# JBIG2Decode, which recto-pdf implements itself, contains it.
MUPDF = re.compile(r"mupdf|artifex|\bjbig2dec\b", re.IGNORECASE)
CONFIGURATIONS = [(configuration, platform) for configuration in ("Debug", "Release") for platform in ("x64", "ARM64")]
REQUIRED = {"LanguageStandard": "stdcpp20", "WarningLevel": "Level4", "TreatWarningAsError": "true"}
# The other metadata that could undermine C++20 with /W4 /WX, held to the values in use: CompileAs picks the language
# (CompileAsC is /TC), DisableSpecificWarnings silences warnings (/wd), WarningVersion caps them (/Wv), and the external
# warning level could lower warnings for headers. Absent is allowed only where it means the default.
ALLOWED_METADATA = {"CompileAs": {"Default", "CompileAsCpp"}, "DisableSpecificWarnings": {None, ""},
                    "WarningVersion": {None, ""}, "ExternalWarningLevel": {"InheritWarningLevel"}}
# The only switches AdditionalOptions may carry. An allow-list, not a pattern of bad switches: quoting, prefixes and
# spellings cannot sneak an override (/std:c++17, /W0, "/WX-") past it. A new switch is added here deliberately.
ALLOWED_OPTIONS = {"/utf-8", "/bigobj", "/await:strict"}


def msbuild():
    """MSBuild from the newest Visual Studio, found the way the build finds it (vswhere)."""
    vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not vswhere.is_file():
        return None
    found = subprocess.run([str(vswhere), "-latest", "-prerelease", "-requires", "Microsoft.Component.MSBuild",
                            "-find", r"MSBuild\**\Bin\MSBuild.exe"], capture_output=True, text=True).stdout.splitlines()
    return found[0].strip() if found else None


def tracked(*pathspecs):
    out = subprocess.run(["git", "-C", str(REPO), "ls-files", "-z", "--", *pathspecs], capture_output=True,
                         text=True, encoding="utf-8", check=True).stdout
    return [name for name in out.split("\0") if name]


# Office or LibreOffice presented as something Recto needs to convert (D5, D7, D10). Over the boards' visible text,
# read unit by unit (each element other than inline ones, and each sentence):
# - the suites by name, which a board never has a reason to show, are refused: LibreOffice, Microsoft Office or 365,
#   Office COM;
# - every unit that names Office, Word, Excel or PowerPoint must be one of REVIEWED_OFFICE_UNITS, each read and found
#   to describe Recto's own conversion or interface. A closed set rather than a list of forbidden phrasings: any new
#   mention, in element text or a visible attribute, fails until a reviewer reads it and adds it here.
# What this cannot see: a sentence that states a dependency without naming Office, Word, Excel, PowerPoint or a suite
# ("install the desktop suite first") is not tied to Office by any text check; that is design review's to catch.
OFFICE_SUITE = re.compile(r"\b(libreoffice|microsoft (office|365)|office com)\b", re.IGNORECASE)
OFFICE_PRODUCT = re.compile(r"\b(office|word|excel|powerpoint)\b", re.IGNORECASE)
REVIEWED_OFFICE_UNITS = frozenset({
    # Conversion features and their settings (Recto's built-in engines, foundation plan section 2 and SPEC 9.5, 9.7).
    "3-mode PDF to Word (Flowing, Exact, Continuous), PDF to Excel, PDF to HTML5",
    "3-mode PDF to Word, PDF to Excel/Data, PDF to Web/Docs, PDF to and from images, searchable PDF (OCR), 100% native"
    " C++20 Office, HTML, Markdown, XPS and SVG to PDF",
    "Converted 86 pages to Word",
    "Default Office to PDF page setup",
    "Default Office to PDF page setupUsed when converting Word, Excel and PowerPoint files",  # two inline spans
    "Default PDF to Word reconstruction mode",
    "Excel & Data Tables to",
    "Images, text, merge, split, Office to PDF, PDF to Word",
    "Office to PDF",
    "Office to PDF Word, Excel, PowerPoint and OpenDocument files, with Recto's built-in layout engines",
    "Office to PDF is built in",
    "PDF from Office Add Word, Excel or PowerPoint files to use this",
    "PDF from Office, HTML, Markdown & Images Add Word, Excel, PowerPoint, HTML or Markdown files to use this",
    "PDF to Word",
    "PDF to Word Editable DOCX",
    "PowerPoint presentation One editable",
    "To Word (DOCX) · page 53 of 86",
    "Word document Editable",
    "Word document Editable DOCX, one per PDF",
    "Word works best on text documents",
    "Word, Excel, PowerPoint and OpenDocument files convert with no other software",
    "v1 Office import",
    "v1 Word export",
    # The user may tidy the converted document in Word afterwards; Recto does not need Word to convert.
    "Pages with several columns, tables or floating pictures may need tidying in Word afterwards",
    # A failed job's status: the PDF being converted to Word needs a password.
    "To Word failed: the file needs a password",
    # The find options' "Whole word", not Microsoft Word.
    "Whole word",
    # The same tiles and captions as read with every element boundary a break (see office_dependency).
    "Add Word, Excel or PowerPoint files to use this",
    "Add Word, Excel, PowerPoint, HTML or Markdown files to use this",
    "Excel & Data",
    "PDF from Office",
    "PDF from Office, HTML, Markdown & Images",
    "PowerPoint presentation",
    "Used when converting Word, Excel and PowerPoint files",
    "Word document",
    "Word, Excel, PowerPoint and OpenDocument files, with Recto's built-in layout engines",
})
# The suites' pages, read from every link a board holds (href, src, action, formaction), entities decoded, and each
# resolved by Node's WHATWG URL parser, the algorithm browsers use, so an entity, a percent-encoded or full-width host,
# extra slashes, backslashes or user info cannot disguise one: any LibreOffice, Office, Microsoft 365 or Office 365
# host, or a Microsoft page whose path names Microsoft 365 or Office (localized paths included).
OFFICE_HOSTS = ("libreoffice.org", "office.com", "microsoft365.com", "office365.com")
LINK_ATTRIBUTES = {"href", "src", "action", "formaction"}
NODE = shutil.which("node")
_RESOLVE_URLS = (
    "const urls = JSON.parse(require('fs').readFileSync(0, 'utf8'));"
    "const decode = s => { try { return decodeURIComponent(s); } catch { return s; } };"
    "process.stdout.write(JSON.stringify(urls.map(u => { let p; try { p = new URL(u, 'https://recto.invalid/'); }"
    " catch { return null; } return [p.hostname, decode(p.pathname)]; })));")


def resolve_urls(urls: list[str]) -> list[tuple[str, str] | None]:
    """(hostname, path) for each URL as a browser resolves it, relative ones against a placeholder host; the path is
    percent-decoded where it can be and kept as written where it cannot; None for a URL a browser cannot parse."""
    if not urls:
        return []
    run = subprocess.run([NODE, "-e", _RESOLVE_URLS], input=json.dumps(urls), capture_output=True, text=True,
                         encoding="utf-8", timeout=60, check=True)
    return [tuple(pair) if pair is not None else None for pair in json.loads(run.stdout)]


def office_install_links(urls: list[str]) -> list[str]:
    """The URLs among urls that lead to an Office suite's pages, or that a browser cannot parse at all (a board has no
    reason to hold a broken link, and one could not be judged)."""
    found = []
    for url, resolved in zip(urls, resolve_urls(urls)):
        if resolved is None:
            found.append(url)
            continue
        host, path = resolved
        host = host.lower().rstrip(".")
        if any(host == h or host.endswith("." + h) for h in OFFICE_HOSTS) or (
                (host == "microsoft.com" or host.endswith(".microsoft.com"))
                and re.search(r"(^|/)(microsoft-365|office|office-365)(/|$)", path.lower())):
            found.append(url)
    return found


# How rendered text breaks into units: block elements (BLOCK_ELEMENTS) end a unit, a line break (<br>) is a space
# within the same text, every other element runs on without a break, and sentence punctuation also ends a unit
# (. ! ? ;). Comments, scripts and styles are not shown. Text in attributes (a button's value, an option's label, a
# field's placeholder, an image's alt, a tooltip, an accessible name) is a unit of its own. Parsed with html.parser,
# which reads quoting, unquoted attributes, entities and comments as a browser does.
# The elements that end a unit: HTML's block, list, table and form-control elements, and each SVG text element.
# Every other element (span, label, a, tspan, wbr and any element not listed) runs on with the text around it, so
# markup cannot split a word.
BLOCK_ELEMENTS = {
    "address", "article", "aside", "blockquote", "body", "button", "caption", "col", "colgroup", "dd", "details",
    "dialog", "div", "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5",
    "h6", "head", "header", "hgroup", "hr", "html", "img", "input", "legend", "li", "main", "menu", "meter", "nav",
    "ol", "optgroup", "option", "output", "p", "pre", "progress", "section", "select", "summary", "table", "tbody",
    "td", "textarea", "tfoot", "th", "thead", "title", "tr", "ul", "svg", "text", "g", "sc-for", "sc-if"}
# Attributes that cannot carry text a reader sees or hears: structure, styling, geometry, link targets (checked on
# their own), states and references. Every other attribute's value (value, label, placeholder, alt, title,
# aria-label, aria-description, and any attribute not listed here) is read as text, so a new text-bearing attribute
# is covered by default. data-* and event handlers (on*) are not text either.
NON_TEXT_ATTRIBUTES = {
    "class", "style", "id", "href", "src", "action", "formaction", "d", "viewbox", "xmlns", "fill", "stroke",
    "stroke-width", "stroke-linecap", "stroke-linejoin", "width", "height", "x", "y", "x1", "x2", "y1", "y2", "rx",
    "ry", "cx", "cy", "r", "points", "transform", "role", "type", "name", "for", "tabindex", "lang", "dir", "rel",
    "target", "colspan", "rowspan", "hidden", "disabled", "checked", "selected", "readonly", "multiple", "required",
    "charset", "content", "http-equiv", "list", "as", "value-type", "aria-hidden", "aria-checked", "aria-selected",
    "aria-expanded", "aria-pressed", "aria-current", "aria-controls", "aria-labelledby", "aria-describedby",
    "aria-orientation", "aria-level", "aria-valuenow", "aria-valuemin", "aria-valuemax", "aria-haspopup",
    "aria-modal", "aria-live", "aria-atomic", "aria-busy", "aria-disabled", "aria-invalid", "aria-setsize",
    "aria-posinset", "aria-colcount", "aria-rowcount", "aria-colindex", "aria-rowindex", "aria-multiselectable",
    "aria-sort", "aria-owns", "aria-activedescendant", "aria-errormessage", "aria-flowto", "aria-details",
    "aria-keyshortcuts", "aria-autocomplete", "aria-readonly", "aria-required"}


def _is_text_attribute(name: str) -> bool:
    return name not in NON_TEXT_ATTRIBUTES and not name.startswith(("data-", "on", "hint-"))


class _VisibleText(html.parser.HTMLParser):
    def __init__(self, split_every_element: bool = False):
        super().__init__(convert_charrefs=True)
        self.split_every_element = split_every_element
        self.parts: list[str] = []
        self.marked: list[str] = []  # the text with \x01 at every block-element boundary (see SPLIT_NAME)
        self.attributes: list[str] = []
        self.links: list[str] = []
        self.hidden = 0

    def _tag(self, tag, attrs, closing=False):
        if tag in ("script", "style"):
            self.hidden += -1 if closing else 1
            self.parts.append("\x00")
            return
        for name, value in attrs:
            if _is_text_attribute(name) and value:
                self.attributes.append(value)
            if name in LINK_ATTRIBUTES and value:
                self.links.append(value)
        if tag in BLOCK_ELEMENTS:
            self.marked.append("\x01")
        if tag == "br":
            self.parts.append(" ")
        elif tag in BLOCK_ELEMENTS or self.split_every_element:
            self.parts.append("\x00")

    def handle_starttag(self, tag, attrs):
        self._tag(tag, attrs)

    def handle_startendtag(self, tag, attrs):
        self._tag(tag, attrs)

    def handle_endtag(self, tag):
        self._tag(tag, [], closing=True)

    def handle_data(self, data):
        if not self.hidden:
            self.parts.append(data)
            self.marked.append(data)


def visible_units(markup: str, split_every_element: bool = False) -> list[str]:
    """The text a reader sees or hears, as units split at element boundaries (other than inline elements) and at
    sentence punctuation, with white space, newlines included, collapsed to single spaces; visible attribute text
    follows as units of its own."""
    parser = _VisibleText(split_every_element)
    parser.feed(markup)
    parser.close()
    text = "\x00".join(["".join(parser.parts), *parser.attributes])
    return [" ".join(unit.split()) for unit in re.split(r"[\x00.!?;]", text) if unit.strip()]


def board_links(markup: str) -> list[str]:
    """Every link target the markup holds, entities decoded."""
    parser = _VisibleText()
    parser.feed(markup)
    parser.close()
    return parser.links


# A product or suite name broken by a block element (Wo<div style="display:inline">rd</div>): the joined reading
# treats that boundary as a break, but CSS can render the pieces as one word, and no board has a reason to split a
# name with a block element, so such a name is refused. A name split by an inline element (a highlighted prefix,
# <b>PDF to W</b>ord) is already read whole by the joined reading.
SPLIT_NAME = re.compile(
    "|".join("\x01*".join(re.escape(ch) for ch in name) for name in
             ("libreoffice", "office", "word", "excel", "powerpoint")), re.IGNORECASE)


def split_office_name(markup: str) -> str | None:
    """The first Office product or suite name that an element boundary breaks, or None."""
    parser = _VisibleText()
    parser.feed(markup)
    parser.close()
    marked = "".join(parser.marked)
    for m in SPLIT_NAME.finditer(marked):
        before, after = marked[m.start() - 1 : m.start()], marked[m.end() : m.end() + 1]
        if "\x01" in m.group(0) and not before.isalnum() and not after.isalnum():
            return m.group(0).replace("\x01", "")
    return None


def office_dependency(markup: str) -> str | None:
    """The first unit that names an Office suite, or names Office, Word, Excel or PowerPoint without being one of
    the reviewed units, or None."""
    # Read both ways, since a stylesheet decides whether an element breaks the line (a flex row of spans renders as
    # separate words): with inline elements joining their text, and with every element boundary a break. A unit from
    # either reading must be reviewed or name no Office product.
    for split in (False, True):
        for unit in visible_units(markup, split_every_element=split):
            if OFFICE_SUITE.search(unit) or (OFFICE_PRODUCT.search(unit) and unit not in REVIEWED_OFFICE_UNITS):
                return unit
    return split_office_name(markup)


class EngineDependencyPolicyTest(unittest.TestCase):
    def test_no_submodule_or_vendored_engine(self):
        index = subprocess.run(["git", "-C", str(REPO), "ls-files", "--stage"], capture_output=True, text=True,
                               encoding="utf-8", check=True).stdout
        gitlinks = [line for line in index.splitlines() if line.startswith("160000 ")]
        self.assertEqual(gitlinks, [], "a submodule gitlink is in the index")
        self.assertFalse((REPO / ".gitmodules").exists())
        self.assertEqual(tracked("third_party"), [])

    def test_code_and_build_trees_name_no_mupdf(self):
        # Every tracked file in the code and build trees, whatever its extension: a #pragma comment(lib, ...) in any
        # source, a library name in any project or script, or a workflow step can bring MuPDF in. This test's own
        # file names MuPDF to look for it, so it is the one exception. The scan catches a dependency written down
        # plainly or added by mistake, in any encoding and through XML character references; a committer who
        # deliberately obfuscates one (string concatenation, macros, files generated during the build) is a code
        # review matter, which no static scan can rule out.
        files = tracked("src", "fuzz", "tests", "scripts", ".github", "build", "*.props", "*.targets", "*.sln",
                        "vcpkg.json", "*.cmd")
        self.assertTrue(files)
        for name in files:
            if name == "tests/scripts/test_engine_dependency_policy.py":
                continue
            with self.subTest(file=name):
                data = (REPO / name).read_bytes()
                # MSBuild and Windows tools accept UTF-16 files too: search every decoding a tool might apply, at both
                # byte alignments, so an encoding cannot hide the name.
                texts = [data.decode("utf-8", errors="replace"), data.decode("utf-16-le", errors="ignore"),
                         data[1:].decode("utf-16-le", errors="ignore"), data.decode("utf-16-be", errors="ignore"),
                         data[1:].decode("utf-16-be", errors="ignore")]
                if name.endswith((".vcxproj", ".props", ".targets", ".filters", ".xml", ".manifest", ".appxmanifest")):
                    # The text an XML parser produces, with character references expanded (libmu&#112;df.lib).
                    root = ElementTree.fromstring(data)
                    texts += [value for node in root.iter() for value in (node.text, node.tail, *node.attrib.values())
                              if value]
                for text in texts:
                    self.assertIsNone(MUPDF.search(text))

    @unittest.skipUnless(NODE, "needs Node.js for browser URL parsing")
    def test_design_boards_name_no_mupdf_agpl_or_office_dependency(self):
        # The mockup sources and their rendered HTML describe Recto: its engine is recto-pdf (D3) and its licence
        # GPL-3.0-or-later (D2), so no board may present MuPDF, its components or AGPL as Recto's (D06 T05 §25).
        files = [name for name in tracked("design/mockups") if name.endswith((".html", ".json"))]
        self.assertTrue(files)
        for name in files:
            with self.subTest(file=name):
                text = (REPO / name).read_text(encoding="utf-8")
                self.assertIsNone(MUPDF.search(text))
                # The licence by acronym or by name, in any case (agpl-3.0, GNU Affero General Public License).
                self.assertIsNone(re.search(r"agpl|affero", text, re.IGNORECASE))
                self.assertIsNone(re.search(r"\bfz_", text, re.IGNORECASE))  # MuPDF's API prefix, whatever follows it
                # Recto converts Office files with its own engines (D5, D7, D10): no board may present Office or
                # LibreOffice as a converter Recto needs (D06 T05 §28).
                self.assertIsNone(office_dependency(text))
                self.assertEqual(office_install_links(board_links(text)), [])

    @unittest.skipUnless(NODE, "needs Node.js for browser URL parsing")
    def test_office_install_links_are_found_however_written(self):
        for markup in ('<a href="https://www.microsoft.com/en-us/microsoft-365">Install converter</a>',
                       '<a href="https://www.libreoffice&#46;org/">Install converter</a>',
                       "<a href=https://www.office.com>Get it</a>", '<form action="https://download.libreoffice.org/x">',
                       '<a href="https://www.%6cibreoffice.org/download/download-libreoffice/">Install converter</a>',
                       '<a href="https://WWW.LIBREOFFICE\uff0eORG/">Install converter</a>',
                       '<a href="https:///www.libreoffice.org/download/">Install converter</a>',
                       '<a href="https://www.libreoffice.org\\@example.com/download/">Install converter</a>',
                       '<a href="https://www.libreoffice.org/download/%FF">Install converter</a>',
                       '<a href="https://[not a host]/">Broken</a>'):
            with self.subTest(markup=markup):
                self.assertTrue(office_install_links(board_links(markup)))
        for markup in ('<a href="https://learn.microsoft.com/windows/apps/windows-app-sdk/">Docs</a>',
                       '<a href="https://github.com/rizonesoft/Recto">Source</a>'):
            with self.subTest(markup=markup):
                self.assertFalse(office_install_links(board_links(markup)))

    def test_office_dependency_wording_is_refused(self):
        for markup in ("<p>Get LibreOffice (free)</p>", "<p>Get Libre<span>Office</span></p>",
                       "<p>Get Libre<!-- branding -->Office</p>", "<p>Install Microsoft&nbsp;Office to convert.</p>",
                       "<p>Office is required to convert documents.</p>",
                       "<p>Office installation is required to convert documents.</p>",
                       "<p>Conversion requires an installed copy of Office.</p>",
                       "<p>Conversion uses Word to create the PDF.</p>", "<p>Converts through PowerPoint.</p>",
                       "<p>Office is\nrequired to convert documents.</p>", "<p>Required converter: Office.</p>",
                       "<p>Required converter:<br>Office.</p>", "<p>Install <wbr>Office to convert documents.</p>",
                       "<p>Install <button>Office</button> to convert documents.</p>",
                       "<p>Office must be present to convert documents.</p>",
                       '<input type="button" value="Install Microsoft Office to convert">',
                       '<button aria-label="Requires Word">Convert</button>',
                       """<button title="You'll need Microsoft Office to convert">Convert</button>""",
                       "<button title=LibreOffice>Convert</button>",
                       "<p>Get Libre<!-- branding -->Office</p>", "<p>Get Libre<span class='x'>Office</span></p>",
                       '<select><option label="Install Microsoft Office to convert">Converter</option></select>',
                       '<div aria-description="Requires Word">Convert</div>',
                       "<p>Install <label>Wo</label>rd to convert documents.</p>",
                       '<svg><text x="0" y="20">Install Wo<tspan>rd</tspan> to convert documents.</text></svg>',
                       '<div style="display:flex;gap:8px"><span>Install</span><span>Word</span>'
                       '<span>to convert documents.</span></div>',
                       '<div>Install <div style="display:inline">Wo</div>rd to convert documents.</div>',
                       '<div>Install <div style="display:inline">Office</div> to convert documents.</div>'):
            with self.subTest(markup=markup):
                self.assertIsNotNone(office_dependency(markup))
        for markup in ("<p>Office to PDF is built in.</p>", "<td>PDF to Word</td><td>needs a password</td>",
                       "<p>To Word failed: the file needs a password</p>",
                       "<p>Updates come through the Microsoft Store.</p>",
                       "<p>Pages with several columns, tables or floating pictures may need tidying in Word afterwards.</p>"):
            with self.subTest(markup=markup):
                self.assertIsNone(office_dependency(markup))

    @unittest.skipUnless(sys.platform == "win32", "the C++ build and MSBuild are Windows-only (foundation plan D15)")
    def test_every_module_compiles_as_cpp20_with_warnings_as_errors(self):
        # MSBuild itself evaluates each project, conditions included, and reports every source file's ClCompile
        # metadata at evaluation time, so a setting commented out, conditioned away or overridden per file fails, and
        # so does a switch in AdditionalOptions that would override it. Targets that rewrite flags during a build are
        # outside what evaluation shows; the build itself enforces the result (/WX fails it on any warning).
        tool = msbuild()
        self.assertIsNotNone(tool, "MSBuild was not found through vswhere; the policy cannot be checked")
        projects = tracked("*.vcxproj")
        self.assertTrue(projects)
        for project in projects:
            for configuration, platform in CONFIGURATIONS:
                with self.subTest(project=project, configuration=configuration, platform=platform):
                    out = subprocess.run([tool, str(REPO / project), f"-p:Configuration={configuration}",
                                          f"-p:Platform={platform}", "-getItem:ClCompile", "-nologo"],
                                         capture_output=True, text=True, encoding="utf-8", errors="replace")
                    self.assertEqual(out.returncode, 0, out.stdout[-500:] + out.stderr[-500:])
                    items = json.loads(out.stdout)["Items"].get("ClCompile", [])
                    self.assertTrue(items, f"{project} has no ClCompile items")
                    for item in items:
                        for name, value in REQUIRED.items():
                            self.assertEqual(item.get(name), value, f"{item.get('Identity')}: {name}")
                        for name, allowed in ALLOWED_METADATA.items():
                            self.assertIn(item.get(name), allowed, f"{item.get('Identity')}: {name}")
                        if item.get("CompileAs") == "Default":
                            # Default compiles by extension: .c is C, so every source must carry a C++ extension.
                            suffix = pathlib.PurePath(item.get("Identity", "")).suffix.lower()
                            self.assertIn(suffix, {".cpp", ".cxx", ".cc"}, f"{item.get('Identity')}: compiled as C")
                        options = {token.strip('"').lower() for token in item.get("AdditionalOptions", "").split()}
                        self.assertLessEqual(options, ALLOWED_OPTIONS,
                                             f"{item.get('Identity')}: AdditionalOptions has unlisted switches")

if __name__ == "__main__":
    unittest.main()
