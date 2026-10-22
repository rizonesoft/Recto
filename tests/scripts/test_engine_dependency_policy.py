"""Tests for the PDF engine dependency policy (D00 T01 §2, decisions D3 and D9): Recto builds its own recto-pdf engine
in C++20 with warnings as errors, vendors no engine as a submodule or third_party tree, and names no MuPDF anywhere a
user or the build would see it."""
import json
import os
import pathlib
import html
import re
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


# Office or LibreOffice presented as something Recto needs to convert (D5, D7, D10). Two rules over the boards'
# visible text, read unit by unit (each block element and each sentence or clause):
# - the suites by name, which a board never has a reason to show: LibreOffice, Microsoft Office or 365, Office COM;
# - a unit naming Office, Word, Excel or PowerPoint together with a word of dependency (install, require, need,
#   necessary, depend, through, via, using, uses, copy of), unless it is one of the reviewed sentences below, each
#   read and found to describe Recto's own conversion. Any new such sentence fails until a reviewer adds it here.
OFFICE_SUITE = re.compile(r"\b(libreoffice|microsoft (office|365)|office com)\b", re.IGNORECASE)
OFFICE_PRODUCT = re.compile(r"\b(office|word|excel|powerpoint)\b", re.IGNORECASE)
DEPENDENCY_WORD = re.compile(r"\b(install\w*|requir\w*|need\w*|necessar\w*|depend\w*|through|via|using|uses|copy of)\b",
                             re.IGNORECASE)
REVIEWED_SENTENCES = {
    # B-Convert, PDF to Word: the user may tidy the converted document in Word; Recto does not need Word to convert.
    "Pages with several columns, tables or floating pictures may need tidying in Word afterwards",
    # A-Convert and B-Convert, a failed job: the PDF being converted to Word needs a password.
    "To Word failed: the file needs a password",
}
# The suites' install pages, matched in the raw text so a link counts whatever its anchor says.
OFFICE_INSTALL_LINK = re.compile(r"libreoffice\.org|microsoft\.com/microsoft-365|office\.com", re.IGNORECASE)
# Inline elements and comments do not break words when rendered (Libre<span>Office</span>, Libre<!-- x -->Office), so
# they are removed without a space, as is a word-break opportunity (<wbr>); every other tag ends a unit. White space, newlines included, is only a space, and a
# colon does not end a unit ("Required converter: Office" is one statement).
INLINE_TAG = re.compile(r"</?(a|abbr|b|bdi|bdo|cite|code|data|dfn|em|i|kbd|mark|q|s|samp|small|span|strong|sub|sup|time"
                        r"|u|var|wbr)\b[^>]*>", re.IGNORECASE)
# A line break inside an element is a break within the same text, read as a space, not the end of a unit.
LINE_BREAK = re.compile(r"<br\b[^>]*>", re.IGNORECASE)


def visible_units(markup: str) -> list[str]:
    """The text a reader sees, as units split at element boundaries (other than inline elements) and at sentence
    punctuation (. ! ? ;), with entities decoded and white space, newlines included, collapsed to single spaces."""
    text = re.sub(r"(?s)<!--.*?-->", "", markup)
    text = re.sub(r"(?is)<(script|style)\b.*?</\1>", "\x00", text)
    text = html.unescape(re.sub(r"<[^>]+>", "\x00", LINE_BREAK.sub(" ", INLINE_TAG.sub("", text))))
    return [" ".join(unit.split()) for unit in re.split(r"[\x00.!?;]", text) if unit.strip()]


def office_dependency(markup: str) -> str | None:
    """The first unit that presents Office or LibreOffice as a converter Recto needs, or None."""
    for unit in visible_units(markup):
        if OFFICE_SUITE.search(unit) or (OFFICE_PRODUCT.search(unit) and DEPENDENCY_WORD.search(unit)
                                         and unit not in REVIEWED_SENTENCES):
            return unit
    return None


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
                self.assertIsNone(OFFICE_INSTALL_LINK.search(text))

    def test_office_dependency_wording_is_refused(self):
        for markup in ("<p>Get LibreOffice (free)</p>", "<p>Get Libre<span>Office</span></p>",
                       "<p>Get Libre<!-- branding -->Office</p>", "<p>Install Microsoft&nbsp;Office to convert.</p>",
                       "<p>Office is required to convert documents.</p>",
                       "<p>Office installation is required to convert documents.</p>",
                       "<p>Conversion requires an installed copy of Office.</p>",
                       "<p>Conversion uses Word to create the PDF.</p>", "<p>Converts through PowerPoint.</p>",
                       "<p>Office is\nrequired to convert documents.</p>", "<p>Required converter: Office.</p>",
                       "<p>Required converter:<br>Office.</p>", "<p>Install <wbr>Office to convert documents.</p>"):
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
