// D10 T01 section 2: Recto.exe is a plain Win32 host on recto-ui. These tests
// read the project, the two manifests and the tracked tree, check the built
// Recto.exe, and launch it. Every check runs on the real file and on a planted
// copy that must fail, so none of them can pass by matching nothing.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <recto/ui/application.h>

#include "test_support.h"
#include "ui_test.h"

#include <windows.h>
#include <objidl.h>
#include <wrl/client.h>
#include <xmllite.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

// ---------------------------------------------------------------------------
// Text and XML helpers

std::string narrow(std::wstring_view text)
{
	if (text.empty()) {
		return {};
	}
	const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
	return out;
}

std::string read_text(const fs::path& path)
{
	const std::vector<uint8_t> bytes = recto::test::read_file_bytes(path);
	return std::string(bytes.begin(), bytes.end());
}

std::string ascii_lower(std::string_view text)
{
	std::string out(text);
	std::transform(out.begin(), out.end(), out.begin(), [](char c) {
		return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
	});
	return out;
}

std::wstring trimmed(std::wstring_view text)
{
	const size_t first = text.find_first_not_of(L" \t\r\n");
	if (first == std::wstring_view::npos) {
		return {};
	}
	const size_t last = text.find_last_not_of(L" \t\r\n");
	return std::wstring(text.substr(first, last - first + 1));
}

// One element as XmlLite reads it: namespace-aware, so a prefix the manifest
// tool rewrites still matches, and blind to comments, so a comment that names
// a forbidden property is not mistaken for one.
struct XmlElement {
	std::wstring ns;
	std::wstring name;
	std::map<std::wstring, std::wstring> attributes; // local name -> value
	std::wstring text;                               // direct character data
};

void check_hr(HRESULT hr, const char* step)
{
	if (FAILED(hr)) {
		throw std::runtime_error(std::string(step) + " failed");
	}
}

std::vector<XmlElement> parse_xml(std::string_view utf8)
{
	const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, utf8.size());
	if (memory == nullptr) {
		throw std::runtime_error("GlobalAlloc failed");
	}
	void* data = GlobalLock(memory);
	std::copy(utf8.begin(), utf8.end(), static_cast<char*>(data));
	GlobalUnlock(memory);
	ComPtr<IStream> stream;
	if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
		GlobalFree(memory);
		throw std::runtime_error("CreateStreamOnHGlobal failed");
	}
	ComPtr<IXmlReader> reader;
	check_hr(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr), "CreateXmlReader");
	check_hr(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit), "SetProperty");
	check_hr(reader->SetInput(stream.Get()), "SetInput");

	std::vector<XmlElement> elements;
	std::vector<size_t> open;
	XmlNodeType type{};
	HRESULT hr = S_OK;
	while ((hr = reader->Read(&type)) == S_OK) {
		const wchar_t* value = nullptr;
		switch (type) {
		case XmlNodeType_Element: {
			XmlElement element;
			check_hr(reader->GetLocalName(&value, nullptr), "GetLocalName");
			element.name = value;
			check_hr(reader->GetNamespaceUri(&value, nullptr), "GetNamespaceUri");
			element.ns = value;
			const bool empty = reader->IsEmptyElement() != FALSE;
			for (HRESULT at = reader->MoveToFirstAttribute(); at == S_OK; at = reader->MoveToNextAttribute()) {
				if (reader->IsDefault() != FALSE) {
					continue;
				}
				const wchar_t* attribute_ns = nullptr;
				check_hr(reader->GetNamespaceUri(&attribute_ns, nullptr), "GetNamespaceUri");
				if (std::wstring_view(attribute_ns) == L"http://www.w3.org/2000/xmlns/") {
					continue;
				}
				const wchar_t* name = nullptr;
				check_hr(reader->GetLocalName(&name, nullptr), "GetLocalName");
				check_hr(reader->GetValue(&value, nullptr), "GetValue");
				element.attributes[name] = value;
			}
			reader->MoveToElement();
			elements.push_back(std::move(element));
			if (!empty) {
				open.push_back(elements.size() - 1);
			}
			break;
		}
		case XmlNodeType_Text:
		case XmlNodeType_CDATA:
			check_hr(reader->GetValue(&value, nullptr), "GetValue");
			if (!open.empty()) {
				elements[open.back()].text += value;
			}
			break;
		case XmlNodeType_EndElement:
			if (!open.empty()) {
				open.pop_back();
			}
			break;
		default:
			break;
		}
	}
	if (FAILED(hr)) {
		throw std::runtime_error("the XML does not parse");
	}
	return elements;
}

std::wstring attribute(const XmlElement& element, std::wstring_view name)
{
	const auto found = element.attributes.find(std::wstring(name));
	return found == element.attributes.end() ? std::wstring() : found->second;
}

// Inserts `text` before the last occurrence of `marker` (a planted copy).
std::string plant_before(std::string text, std::string_view marker, std::string_view planted)
{
	const size_t at = text.rfind(marker);
	if (at == std::string::npos) {
		throw std::runtime_error("plant marker not found");
	}
	text.insert(at, planted);
	return text;
}

std::string replace_once(std::string text, std::string_view from, std::string_view to)
{
	const size_t at = text.find(from);
	if (at == std::string::npos) {
		throw std::runtime_error("replace target not found");
	}
	text.replace(at, from.size(), to);
	return text;
}

std::string describe(const std::vector<std::string>& findings)
{
	std::string text;
	for (const std::string& finding : findings) {
		text += finding + "\n";
	}
	return text;
}

bool has(const std::vector<std::string>& findings, std::string_view needle)
{
	return std::any_of(findings.begin(), findings.end(), [&](const std::string& f) { return f.find(needle) != std::string::npos; });
}

// ---------------------------------------------------------------------------
// The checks

const fs::path& app_dir()
{
	static const fs::path dir = recto::test::repo_root() / "src" / "app";
	return dir;
}

// app.manifest: per-monitor v2 DPI first, long paths, UTF-8, the Windows 10
// and 11 supported-OS entry, and no common-controls dependency.
std::vector<std::string> manifest_findings(std::string_view text)
{
	constexpr std::wstring_view k2016 = L"http://schemas.microsoft.com/SMI/2016/WindowsSettings";
	constexpr std::wstring_view k2019 = L"http://schemas.microsoft.com/SMI/2019/WindowsSettings";
	std::vector<std::string> findings;
	bool dpi = false;
	bool long_paths = false;
	bool utf8 = false;
	bool supported_os = false;
	for (const XmlElement& element : parse_xml(text)) {
		const std::wstring value = trimmed(element.text);
		if (element.name == L"dpiAwareness" && element.ns == k2016) {
			// Windows takes the first value it recognises in this list.
			const std::wstring first = trimmed(value.substr(0, value.find(L',')));
			dpi = _wcsicmp(first.c_str(), L"PerMonitorV2") == 0;
		} else if (element.name == L"longPathAware" && element.ns == k2016) {
			long_paths = _wcsicmp(value.c_str(), L"true") == 0;
		} else if (element.name == L"activeCodePage" && element.ns == k2019) {
			utf8 = _wcsicmp(value.c_str(), L"UTF-8") == 0;
		} else if (element.name == L"supportedOS") {
			supported_os = supported_os || _wcsicmp(attribute(element, L"Id").c_str(), L"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}") == 0;
		} else if (element.name == L"assemblyIdentity" && _wcsicmp(attribute(element, L"name").c_str(), L"Microsoft.Windows.Common-Controls") == 0) {
			findings.push_back("a common-controls dependency: recto-ui draws its own controls");
		}
	}
	if (!dpi) {
		findings.push_back("dpiAwareness does not lead with PerMonitorV2");
	}
	if (!long_paths) {
		findings.push_back("longPathAware is not true");
	}
	if (!utf8) {
		findings.push_back("activeCodePage is not UTF-8");
	}
	if (!supported_os) {
		findings.push_back("no Windows 10 and 11 supportedOS entry");
	}
	return findings;
}

// Package.appxmanifest: a full-trust Win32 desktop app with no framework
// package, launching Recto.exe, with every logo it names present on disk.
std::vector<std::string> appx_findings(std::string_view text, const fs::path& assets_root)
{
	std::vector<std::string> findings;
	if (text.find("$target") != std::string_view::npos) {
		findings.push_back("a $target token from the removed packaging build");
	}
	bool full_trust = false;
	bool application = false;
	bool desktop = false;
	for (const XmlElement& element : parse_xml(text)) {
		if (element.name == L"PackageDependency") {
			findings.push_back("PackageDependency " + narrow(attribute(element, L"Name")));
		} else if (element.name == L"Capability" && attribute(element, L"Name") == L"runFullTrust") {
			full_trust = true;
		} else if (element.name == L"TargetDeviceFamily") {
			desktop = desktop || attribute(element, L"Name") == L"Windows.Desktop";
		} else if (element.name == L"Application") {
			application = true;
			if (attribute(element, L"Executable") != L"Recto.exe") {
				findings.push_back("Application Executable is " + narrow(attribute(element, L"Executable")));
			}
			if (attribute(element, L"EntryPoint") != L"Windows.FullTrustApplication") {
				findings.push_back("Application EntryPoint is " + narrow(attribute(element, L"EntryPoint")));
			}
		}
		std::vector<std::wstring> assets;
		if (element.name == L"Logo") {
			assets.push_back(trimmed(element.text));
		}
		for (const auto& [name, value] : element.attributes) {
			if (value.starts_with(L"Assets\\")) {
				assets.push_back(value);
			}
		}
		for (const std::wstring& asset : assets) {
			if (!fs::exists(assets_root / asset)) {
				findings.push_back("missing asset " + narrow(asset));
			}
		}
	}
	if (!full_trust) {
		findings.push_back("no runFullTrust capability");
	}
	if (!application) {
		findings.push_back("no Application element");
	}
	if (!desktop) {
		findings.push_back("no Windows.Desktop target device family");
	}
	return findings;
}

const std::set<std::wstring>& packaging_only_properties()
{
	// What made the scaffold a WinUI packaged app; none of it belongs in a plain
	// Win32 project.
	static const std::set<std::wstring> names = {L"ApplicationType", L"ApplicationTypeRevision", L"AppxPackage",
		L"AppContainerApplication", L"UseWinUI", L"EnableMsixTooling", L"MinimalCoreWin", L"WindowsAppSDKSelfContained",
		L"WindowsPackageType", L"NuGetTargetMoniker", L"NuGetTargetPlatformIdentifier", L"NuGetTargetPlatformVersion",
		L"RuntimeIdentifiers", L"CppWinRTOptimized", L"CppWinRTRootNamespaceAutoMerge", L"CppWinRTGenerateWindowsMetadata",
		L"DesktopCompatible", L"GenerateWindowsMetadata", L"Midl", L"Page", L"ApplicationDefinition", L"AppxManifest",
		L"Image", L"PRIResource"};
	return names;
}

// Recto.vcxproj: a Win32 Application on the Windows subsystem that builds
// main.cpp, references recto-ui and keeps Package.appxmanifest as a None item.
std::vector<std::string> project_findings(std::string_view text)
{
	std::vector<std::string> findings;
	bool application = false;
	bool windows_subsystem = false;
	bool main_cpp = false;
	bool references_ui = false;
	bool appx_is_none = false;
	for (const XmlElement& element : parse_xml(text)) {
		const std::wstring include = attribute(element, L"Include");
		if (packaging_only_properties().contains(element.name)) {
			findings.push_back("packaging element " + narrow(element.name));
		} else if (element.name == L"ConfigurationType") {
			if (trimmed(element.text) == L"Application") {
				application = true;
			} else {
				findings.push_back("ConfigurationType " + narrow(trimmed(element.text)));
			}
		} else if (element.name == L"SubSystem") {
			windows_subsystem = trimmed(element.text) == L"Windows";
		} else if (element.name == L"ClCompile") {
			const std::string lower = ascii_lower(narrow(include));
			if (lower.find("module.g.cpp") != std::string::npos || lower.find(".xaml") != std::string::npos) {
				findings.push_back("generated XAML source " + narrow(include));
			}
			main_cpp = main_cpp || include == L"main.cpp";
		} else if (element.name == L"ProjectReference") {
			references_ui = references_ui || include.ends_with(L"recto-ui.vcxproj");
		} else if (element.name == L"None") {
			appx_is_none = appx_is_none || include == L"Package.appxmanifest";
		}
	}
	if (!application) {
		findings.push_back("not an Application");
	}
	if (!windows_subsystem) {
		findings.push_back("not on the Windows subsystem");
	}
	if (!main_cpp) {
		findings.push_back("main.cpp is not compiled");
	}
	if (!references_ui) {
		findings.push_back("no ProjectReference to recto-ui");
	}
	if (!appx_is_none) {
		findings.push_back("Package.appxmanifest is not a None item");
	}
	return findings;
}

// The NuGet packages Recto.vcxproj restores: exactly the two toolchain pins.
std::vector<std::string> package_findings(std::string_view text)
{
	std::set<std::wstring> packages;
	for (const XmlElement& element : parse_xml(text)) {
		if (element.name == L"PackageReference") {
			packages.insert(attribute(element, L"Include"));
		}
	}
	const std::set<std::wstring> expected = {L"Microsoft.Windows.CppWinRT", L"Microsoft.Windows.SDK.BuildTools"};
	std::vector<std::string> findings;
	for (const std::wstring& package : packages) {
		if (!expected.contains(package)) {
			findings.push_back("unexpected package " + narrow(package));
		}
	}
	for (const std::wstring& package : expected) {
		if (!packages.contains(package)) {
			findings.push_back("missing package " + narrow(package));
		}
	}
	return findings;
}

// ---------------------------------------------------------------------------
// The residue scan

// Dated history keeps its words: the plan, the specs, the review records, the
// TODO tree and the rejected design explorations.
constexpr std::array<std::string_view, 5> kHistoryPrefixes = {"docs/plans/", "docs/specs/", "docs/reviews/", "todo/", "design/mockups/"};

struct Allowed {
	std::string_view path;
	std::string_view reason;
};

// Each file allowed to name the retired stack, and why. A new entry needs a
// reason a reviewer accepts.
constexpr std::array<Allowed, 4> kAllowed = {{
	{"design/SPEC.md", "names WinUI 3 as the Fluent look recto-ui reproduces"},
	{"tests/recto-tests/ui_project_tests.cpp", "the Ui.Project.NoWinRtUiTypes guard tokens"},
	{"tests/recto-tests/ui_app_tests.cpp", "this guard's own tokens"},
	{"tests/scripts/test_merge_prs.py", "the refused winui/ pin branch regression case"},
}};

constexpr std::array<std::string_view, 7> kResidueTokens = {
	"winui", "windowsappsdk", "windows app sdk", "microsoft.ui", "microsoft::ui", "xaml", "webview2"};

// Findings for `listed` (repository-relative, forward slashes) under `root`.
std::vector<std::string> residue_findings(const fs::path& root, const std::vector<std::string>& listed)
{
	std::vector<std::string> findings;
	for (const std::string& path : listed) {
		if (std::any_of(kHistoryPrefixes.begin(), kHistoryPrefixes.end(), [&](std::string_view p) { return path.starts_with(p); }) ||
			std::any_of(kAllowed.begin(), kAllowed.end(), [&](const Allowed& a) { return path == a.path; })) {
			continue;
		}
		const fs::path file = root / fs::path(std::u8string(path.begin(), path.end()));
		if (!fs::is_regular_file(recto::test::to_long_path(file))) {
			continue; // listed but deleted in the working tree
		}
		const std::string lower_path = ascii_lower(path);
		if (lower_path.ends_with(".xaml") || lower_path.find("winui") != std::string::npos) {
			findings.push_back(path + " (path)");
		}
		const std::string text = read_text(file);
		// Git's own binary test: a NUL byte in the first 8000 bytes.
		if (text.substr(0, 8000).find('\0') != std::string::npos) {
			continue;
		}
		const std::string lower = ascii_lower(text);
		size_t line = 1;
		size_t start = 0;
		while (start <= lower.size()) {
			const size_t end = std::min(lower.find('\n', start), lower.size());
			const std::string_view row(lower.data() + start, end - start);
			for (const std::string_view token : kResidueTokens) {
				if (row.find(token) != std::string_view::npos) {
					findings.push_back(path + ":" + std::to_string(line) + " " + std::string(token));
				}
			}
			start = end + 1;
			++line;
		}
	}
	return findings;
}

// ---------------------------------------------------------------------------
// Processes

class Process {
public:
	Process() = default;
	Process(const Process&) = delete;
	Process& operator=(const Process&) = delete;
	~Process()
	{
		if (info_.hProcess != nullptr) {
			if (WaitForSingleObject(info_.hProcess, 0) == WAIT_TIMEOUT) {
				TerminateProcess(info_.hProcess, 99);
				WaitForSingleObject(info_.hProcess, 5000);
			}
			CloseHandle(info_.hProcess);
			CloseHandle(info_.hThread);
		}
	}
	PROCESS_INFORMATION& info() noexcept { return info_; }
	// The exit code, or nothing when the process is still running after `ms`.
	std::optional<DWORD> wait(DWORD ms) const
	{
		if (WaitForSingleObject(info_.hProcess, ms) != WAIT_OBJECT_0) {
			return std::nullopt;
		}
		DWORD code = 0;
		GetExitCodeProcess(info_.hProcess, &code);
		return code;
	}

private:
	PROCESS_INFORMATION info_{};
};

// `git ls-files` in `root`: tracked files plus untracked ones that are not
// ignored, so residue is caught before it is committed.
std::vector<std::string> git_listing(const fs::path& root)
{
	SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
	HANDLE read_end = nullptr;
	HANDLE write_end = nullptr;
	if (CreatePipe(&read_end, &write_end, &inherit, 0) == FALSE) {
		throw std::runtime_error("CreatePipe failed");
	}
	SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
	const HANDLE null_device = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = write_end;
	startup.hStdError = null_device;
	std::wstring command = L"git ls-files -z --cached --others --exclude-standard";
	Process git;
	const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
		root.c_str(), &startup, &git.info());
	CloseHandle(write_end);
	CloseHandle(null_device);
	if (started == FALSE) {
		CloseHandle(read_end);
		throw std::runtime_error("git could not be started; the residue scan needs git on PATH");
	}
	// Drained on its own thread, so a git that stalls cannot hold the test past
	// its deadline; at the deadline Process's destructor terminates and reaps it.
	std::string output;
	std::thread reader([&] {
		std::array<char, 65536> buffer{};
		DWORD got = 0;
		while (ReadFile(read_end, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) != FALSE && got > 0) {
			output.append(buffer.data(), got);
		}
	});
	const std::optional<DWORD> code = git.wait(60000);
	if (!code.has_value()) {
		TerminateProcess(git.info().hProcess, 124);
		WaitForSingleObject(git.info().hProcess, INFINITE);
	}
	reader.join();
	CloseHandle(read_end);
	if (!code.has_value()) {
		throw std::runtime_error("git ls-files was still running after 60 s and was terminated");
	}
	if (*code != 0) {
		throw std::runtime_error("git ls-files failed");
	}
	std::vector<std::string> paths;
	size_t start = 0;
	while (start < output.size()) {
		const size_t end = output.find('\0', start);
		paths.push_back(output.substr(start, (end == std::string::npos ? output.size() : end) - start));
		start = end == std::string::npos ? output.size() : end + 1;
	}
	return paths;
}

// Recto.exe from the same build as this test binary:
// build\bin\<platform>\<configuration>\Recto\Recto.exe beside ...\recto-tests\.
fs::path recto_exe()
{
	std::wstring self(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
		if (length < self.size()) {
			self.resize(length);
			break;
		}
		self.resize(self.size() * 2);
	}
	return fs::path(self).parent_path().parent_path() / "Recto" / "Recto.exe";
}

void launch(Process& process, const fs::path& exe, std::wstring_view arguments)
{
	std::wstring command = L"\"" + exe.wstring() + L"\"";
	if (!arguments.empty()) {
		command += L" ";
		command += arguments;
	}
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESHOWWINDOW;
	startup.wShowWindow = SW_SHOWNOACTIVATE; // shown, so it paints, without taking focus from the person at the desk
	ASSERT_NE(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(),
				  &startup, &process.info()),
		FALSE)
		<< "could not start " << exe.string();
}

struct WindowSearch {
	DWORD process_id;
	HWND found;
};

BOOL CALLBACK find_visible_window(HWND window, LPARAM lparam)
{
	auto* search = reinterpret_cast<WindowSearch*>(lparam);
	DWORD owner = 0;
	GetWindowThreadProcessId(window, &owner);
	if (owner == search->process_id && IsWindowVisible(window) != FALSE && GetWindow(window, GW_OWNER) == nullptr) {
		search->found = window;
		return FALSE;
	}
	return TRUE;
}

} // namespace

// ---------------------------------------------------------------------------

RECTO_UI_TEST(App, ParsesArguments)
{
	using recto::ui::parse_arguments;
	const std::array<const wchar_t*, 1> flag = {L"--self-test-exit"};
	EXPECT_TRUE(parse_arguments(flag).self_test_exit);
	EXPECT_FALSE(parse_arguments({}).self_test_exit);
	const std::array<const wchar_t*, 2> file_then_flag = {L"C:\\docs\\a.pdf", L"--self-test-exit"};
	EXPECT_TRUE(parse_arguments(file_then_flag).self_test_exit);
	// A quoted path that contains the flag's text is one argument, not the flag.
	const std::array<const wchar_t*, 1> quoted = {L"C:\\x --self-test-exit y.pdf"};
	EXPECT_FALSE(parse_arguments(quoted).self_test_exit);
	const std::array<const wchar_t*, 3> near_misses = {L"--self-test-exit=1", L"--SELF-TEST-EXIT", nullptr};
	EXPECT_FALSE(parse_arguments(near_misses).self_test_exit);
}

RECTO_UI_TEST(App, ManifestDpiAwareness)
{
	const std::string manifest = read_text(app_dir() / "app.manifest");
	const std::vector<std::string> real = manifest_findings(manifest);
	EXPECT_TRUE(real.empty()) << describe(real);

	EXPECT_TRUE(has(manifest_findings(replace_once(manifest, ">PerMonitorV2<", ">PerMonitor, PerMonitorV2<")), "PerMonitorV2"));
	EXPECT_TRUE(has(manifest_findings(replace_once(manifest, ">UTF-8<", ">Legacy<")), "UTF-8"));
	EXPECT_TRUE(has(manifest_findings(replace_once(manifest, ">true</longPathAware>", ">false</longPathAware>")), "longPathAware"));
	EXPECT_TRUE(has(manifest_findings(plant_before(manifest, "</assembly>",
						"<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
						"version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\" />"
						"</dependentAssembly></dependency>")),
		"common-controls"));
}

RECTO_UI_TEST(App, ExeEmbedsManifest)
{
	// The XML on disk is not what Windows reads: the manifest the linker embedded
	// in Recto.exe is (resource RT_MANIFEST 1).
	const fs::path exe = recto_exe();
	ASSERT_TRUE(fs::exists(exe)) << exe.string() << " is not built";
	const HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
	ASSERT_NE(module, nullptr);
	const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), RT_MANIFEST);
	ASSERT_NE(resource, nullptr) << "Recto.exe carries no application manifest";
	const HGLOBAL loaded = LoadResource(module, resource);
	const auto* bytes = static_cast<const char*>(LockResource(loaded));
	const std::string embedded(bytes, SizeofResource(module, resource));
	FreeLibrary(module);
	const std::vector<std::string> findings = manifest_findings(embedded);
	EXPECT_TRUE(findings.empty()) << describe(findings);
}

RECTO_UI_TEST(App, AppxNoFramework)
{
	const std::string appx = read_text(app_dir() / "Package.appxmanifest");
	const std::vector<std::string> real = appx_findings(appx, app_dir());
	EXPECT_TRUE(real.empty()) << describe(real);

	EXPECT_TRUE(has(appx_findings(plant_before(appx, "</Dependencies>",
						"<PackageDependency Name=\"Microsoft.WindowsAppRuntime.2.5\" MinVersion=\"0.0.0.0\" "
						"Publisher=\"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US\" />"),
						app_dir()),
		"PackageDependency"));
	EXPECT_TRUE(has(appx_findings(replace_once(appx, "Executable=\"Recto.exe\"", "Executable=\"$targetnametoken$.exe\""), app_dir()), "$target"));
	EXPECT_TRUE(has(appx_findings(replace_once(appx, "Name=\"runFullTrust\"", "Name=\"internetClient\""), app_dir()), "runFullTrust"));
	EXPECT_TRUE(has(appx_findings(replace_once(appx, "Assets\\Square44x44Logo.png", "Assets\\Gone.png"), app_dir()), "missing asset"));
}

RECTO_UI_TEST(App, NoWindowsAppSdkPackage)
{
	const std::string project = read_text(app_dir() / "Recto.vcxproj");
	const std::vector<std::string> real = package_findings(project);
	EXPECT_TRUE(real.empty()) << describe(real);

	EXPECT_TRUE(has(package_findings(plant_before(project, "</Project>",
						"<ItemGroup><PackageReference Include=\"Microsoft.WindowsAppSDK\" Version=\"2.5.1\" /></ItemGroup>")),
		"unexpected package Microsoft.WindowsAppSDK"));
}

RECTO_UI_TEST(App, ProjectIsPlainWin32)
{
	const std::string project = read_text(app_dir() / "Recto.vcxproj");
	const std::vector<std::string> real = project_findings(project);
	EXPECT_TRUE(real.empty()) << describe(real);

	EXPECT_TRUE(has(project_findings(plant_before(project, "</Project>", "<PropertyGroup><UseWinUI>true</UseWinUI></PropertyGroup>")), "UseWinUI"));
	EXPECT_TRUE(has(project_findings(plant_before(project, "</Project>", "<ItemGroup><Midl Include=\"MainWindow.idl\" /></ItemGroup>")), "Midl"));
	EXPECT_TRUE(has(project_findings(plant_before(project, "</Project>",
						"<ItemGroup><ClCompile Include=\"$(GeneratedFilesDir)module.g.cpp\" /></ItemGroup>")),
		"module.g.cpp"));
	// A comment naming a property is not the property.
	EXPECT_TRUE(project_findings(plant_before(project, "</Project>", "<!-- <UseWinUI>true</UseWinUI> -->")).empty());
}

RECTO_UI_TEST(App, NoWinUiResidue)
{
	const fs::path root = recto::test::repo_root();
	const std::vector<std::string> real = residue_findings(root, git_listing(root));
	EXPECT_TRUE(real.empty()) << describe(real);

	// A planted tree: each shape is caught, and history, allowed and binary files are not.
	recto::test::TempDir scratch;
	const auto plant = [&](std::string_view path, std::string_view text) {
		const fs::path file = scratch.path() / fs::path(std::u8string(path.begin(), path.end()));
		fs::create_directories(file.parent_path());
		recto::test::write_file_bytes(file, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
	};
	plant("src/app/App.xaml", "<Application />\n");
	plant("src/app/Recto.vcxproj", "<PackageReference Include=\"Microsoft.WindowsAppSDK\" />\n");
	plant("NOTICE.md", "ok\n| WebView2 SDK |\n");
	plant("scripts/check-winui-pin.py", "pass\n");
	plant("src/app/pch.h", "#include <winrt/Microsoft.UI.Xaml.h>\n");
	plant("docs/plans/history.md", "WinUI 3 was dropped.\n");
	plant("design/SPEC.md", "reproduces the WinUI 3 controls\n");
	plant("resources/blob.bin", std::string_view("\0winui", 6));
	const std::vector<std::string> listed = {"src/app/App.xaml", "src/app/Recto.vcxproj", "NOTICE.md", "scripts/check-winui-pin.py",
		"src/app/pch.h", "docs/plans/history.md", "design/SPEC.md", "resources/blob.bin", "deleted/in/tree.md"};
	const std::vector<std::string> planted = residue_findings(scratch.path(), listed);
	const std::set<std::string> caught(planted.begin(), planted.end());
	const std::set<std::string> expected = {"src/app/App.xaml (path)", "src/app/Recto.vcxproj:1 windowsappsdk", "NOTICE.md:2 webview2",
		"scripts/check-winui-pin.py (path)", "src/app/pch.h:1 microsoft.ui", "src/app/pch.h:1 xaml"};
	EXPECT_EQ(caught, expected) << describe(planted);
}

RECTO_UI_TEST(App, SelfTestExit)
{
	// Recto.exe --self-test-exit shows its window, waits for the first frame to
	// be presented, checks the DPI awareness and code page the manifest promises,
	// and exits 0 by itself, with no person at the desk.
	const fs::path exe = recto_exe();
	ASSERT_TRUE(fs::exists(exe)) << exe.string() << " is not built";
	const auto started = std::chrono::steady_clock::now();
	Process process;
	ASSERT_NO_FATAL_FAILURE(launch(process, exe, L"--self-test-exit"));
	const std::optional<DWORD> code = process.wait(5000);
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	ASSERT_TRUE(code.has_value()) << "Recto.exe --self-test-exit was still running after 5 s";
	EXPECT_EQ(*code, static_cast<DWORD>(recto::ui::kExitOk)) << "exit code " << *code << " (2: not per-monitor v2 DPI aware, 3: not UTF-8)";
	::testing::Test::RecordProperty("launch_to_exit_ms", static_cast<int>(elapsed));
	std::printf("Recto.exe --self-test-exit: launch to exit %lld ms\n", static_cast<long long>(elapsed));
}

RECTO_UI_TEST(App, ClosesWithExitCodeZero)
{
	// Without the flag Recto.exe stays open until its window is closed, then
	// exits 0: the self-test is the only automatic exit.
	const fs::path exe = recto_exe();
	ASSERT_TRUE(fs::exists(exe)) << exe.string() << " is not built";
	Process process;
	ASSERT_NO_FATAL_FAILURE(launch(process, exe, L""));
	ASSERT_EQ(WaitForInputIdle(process.info().hProcess, 5000), 0U) << "Recto.exe did not reach its message loop";
	WindowSearch search{process.info().dwProcessId, nullptr};
	for (int attempt = 0; attempt < 50 && search.found == nullptr; ++attempt) {
		EnumWindows(find_visible_window, reinterpret_cast<LPARAM>(&search));
		if (search.found == nullptr) {
			Sleep(100);
		}
	}
	ASSERT_NE(search.found, nullptr) << "Recto.exe showed no window";
	EXPECT_FALSE(process.wait(500).has_value()) << "Recto.exe exited without being asked";
	PostMessageW(search.found, WM_CLOSE, 0, 0);
	const std::optional<DWORD> code = process.wait(5000);
	ASSERT_TRUE(code.has_value()) << "Recto.exe did not exit after WM_CLOSE";
	EXPECT_EQ(*code, static_cast<DWORD>(recto::ui::kExitOk));
}
