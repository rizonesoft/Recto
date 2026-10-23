// D10 T01 section 1: the recto-ui project, its module boundary and the
// headless render harness. Tests are named Ui.Project.* (see ui_test.h).
#include <recto/ui/ui.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "test_support.h"
#include "ui_render_harness.h"
#include "ui_test.h"

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Finding {
	fs::path file;
	size_t line{0};
	std::string text;
};

std::string lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

bool is_source_file(const fs::path& path)
{
	const std::string ext = lower(path.extension().string());
	return ext == ".h" || ext == ".hpp" || ext == ".inl" || ext == ".cpp" || ext == ".cc" || ext == ".cxx";
}

bool is_header_file(const fs::path& path)
{
	const std::string ext = lower(path.extension().string());
	return ext == ".h" || ext == ".hpp" || ext == ".inl";
}

std::vector<std::string> split_path(const std::string& include_path)
{
	std::string normalized = lower(include_path);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	std::vector<std::string> parts;
	std::stringstream stream(normalized);
	std::string part;
	while (std::getline(stream, part, '/')) {
		if (!part.empty() && part != ".") {
			parts.push_back(part);
		}
	}
	return parts;
}

std::string read_text(const fs::path& path)
{
	std::ifstream stream(recto::test::to_long_path(path), std::ios::binary);
	std::ostringstream buffer;
	buffer << stream.rdbuf();
	return buffer.str();
}

// For a raw string literal whose opening quote is at `quote` (the `"` after the
// R), the index one past its closing quote; npos when it is not a well-formed
// raw string (delimiters are at most 16 characters, none of space, ( ) or \).
size_t raw_string_end(const std::string& text, size_t quote)
{
	const size_t open = text.find('(', quote + 1);
	if (open == std::string::npos || open - quote - 1 > 16) {
		return std::string::npos;
	}
	const std::string delimiter = text.substr(quote + 1, open - quote - 1);
	if (delimiter.find_first_of(" ()\\\t\n\r") != std::string::npos) {
		return std::string::npos;
	}
	const std::string closing = ")" + delimiter + "\"";
	const size_t close = text.find(closing, open + 1);
	return close == std::string::npos ? std::string::npos : close + closing.size();
}

// What the compiler's preprocessor sees (translation phases 2 and 3): lines
// joined at a backslash-newline, then every comment replaced by one space. A
// quoted literal is skipped so a "//" inside it does not start a comment. A
// regex over physical lines misses `#include /* c */ "x.h"` and an include split
// across a continuation, both of which the compiler accepts.
std::string logical_source(const std::string& raw_with_bom)
{
	// A UTF-8 byte order mark is not part of the first line the compiler lexes.
	const std::string_view bom = "\xEF\xBB\xBF";
	const std::string raw = raw_with_bom.starts_with(bom) ? raw_with_bom.substr(bom.size()) : raw_with_bom;
	std::string spliced;
	spliced.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); ++i) {
		if (raw[i] == '\\') {
			size_t j = i + 1;
			if (j < raw.size() && raw[j] == '\r') {
				++j;
			}
			if (j < raw.size() && raw[j] == '\n') {
				i = j;
				continue;
			}
		}
		spliced.push_back(raw[i]);
	}
	std::string out;
	out.reserve(spliced.size());
	for (size_t i = 0; i < spliced.size(); ++i) {
		const char c = spliced[i];
		const char next = i + 1 < spliced.size() ? spliced[i + 1] : '\0';
		if (c == '/' && next == '/') {
			while (i < spliced.size() && spliced[i] != '\n') {
				++i;
			}
			out.push_back('\n');
		} else if (c == '/' && next == '*') {
			i += 2;
			while (i + 1 < spliced.size() && !(spliced[i] == '*' && spliced[i + 1] == '/')) {
				++i;
			}
			++i;
			// One space for the whole comment, newlines inside it included: a comment
			// that spans lines does not split the directive around it. Line numbers
			// in a finding are therefore those of the comment-free text.
			out.push_back(' ');
		} else if (c == '"' && !out.empty() && out.back() == 'R' && raw_string_end(spliced, i) != std::string::npos) {
			// R"delim( ... )delim": the body is opaque, whatever quotes or comment openers it holds.
			const size_t end = raw_string_end(spliced, i);
			out.append(spliced, i, end - i);
			i = end - 1;
		} else if (c == '"' || c == '\'') {
			out.push_back(c);
			for (++i; i < spliced.size() && spliced[i] != c && spliced[i] != '\n'; ++i) {
				if (spliced[i] == '\\' && i + 1 < spliced.size()) {
					out.push_back(spliced[i++]);
				}
				out.push_back(spliced[i]);
			}
			if (i < spliced.size()) {
				out.push_back(spliced[i]);
			}
		} else {
			out.push_back(c);
		}
	}
	return out;
}

std::string without_whitespace(const std::string& text)
{
	std::string out;
	for (const char c : text) {
		if (!std::isspace(static_cast<unsigned char>(c))) {
			out.push_back(c);
		}
	}
	return out;
}

// Every header basename (lower case) under `dirs`.
std::set<std::string> header_basenames(const std::vector<fs::path>& dirs)
{
	std::set<std::string> names;
	for (const fs::path& dir : dirs) {
		if (!fs::exists(dir)) {
			continue;
		}
		for (const auto& entry : fs::recursive_directory_iterator(dir)) {
			if (entry.is_regular_file() && is_header_file(entry.path())) {
				names.insert(lower(entry.path().filename().string()));
			}
		}
	}
	return names;
}

// Walks `dir` and reports each #include line whose target `forbidden` rejects.
template <typename Predicate>
std::vector<Finding> scan_includes(const fs::path& dir, Predicate forbidden)
{
	static const std::regex include_line(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
	static const std::regex computed_include(R"(^\s*#\s*include\s*([^<"\s].*)$)");
	std::vector<Finding> findings;
	if (!fs::exists(dir)) {
		return findings;
	}
	for (const auto& entry : fs::recursive_directory_iterator(dir)) {
		if (!entry.is_regular_file() || !is_source_file(entry.path())) {
			continue;
		}
		std::istringstream lines(logical_source(read_text(entry.path())));
		std::string line;
		size_t number = 0;
		while (std::getline(lines, line)) {
			++number;
			std::smatch match;
			if (std::regex_search(line, match, include_line)) {
				if (forbidden(match[1].str())) {
					findings.push_back({entry.path(), number, match[1].str()});
				}
			} else if (std::regex_search(line, match, computed_include)) {
				// #include MACRO hides its target from any text scan, so inside the
				// guarded modules it is refused outright.
				findings.push_back({entry.path(), number, "computed include " + match[1].str()});
			}
		}
	}
	return findings;
}

// recto-ui may include the Windows SDK, the standard library and itself. It may
// not include a path through pdf/, core/ or services/, nor any header the
// engine libraries own (recto_pdf.h and the rest), by path or by bare name.
bool ui_includes_engine(const std::string& include_path, const std::set<std::string>& engine_headers)
{
	const std::vector<std::string> parts = split_path(include_path);
	if (parts.empty()) {
		return false;
	}
	for (const std::string& part : parts) {
		if (part == "pdf" || part == "core" || part == "services") {
			return true;
		}
	}
	return engine_headers.contains(parts.back());
}

// The engine libraries may not include recto-ui by any path.
bool engine_includes_ui(const std::string& include_path)
{
	const std::vector<std::string> parts = split_path(include_path);
	for (size_t i = 0; i < parts.size(); ++i) {
		if (parts[i] == "ui" || parts[i] == "recto_ui.h") {
			return true;
		}
	}
	return false;
}

// Boundary scan over one repository tree rooted at `root` (a checkout or a
// scratch copy). `engine_headers` is the set of names the engine libraries own.
std::vector<Finding> boundary_findings(const fs::path& root, const std::set<std::string>& engine_headers)
{
	std::vector<Finding> all;
	for (const Finding& finding : scan_includes(root / "src" / "ui", [&](const std::string& include_path) {
			 return ui_includes_engine(include_path, engine_headers);
		 })) {
		all.push_back(finding);
	}
	for (const char* engine_dir : {"pdf", "core", "services"}) {
		for (const Finding& finding : scan_includes(root / "src" / engine_dir, engine_includes_ui)) {
			all.push_back(finding);
		}
	}
	return all;
}

std::set<std::string> engine_header_names(const fs::path& root)
{
	std::set<std::string> names = header_basenames({root / "src" / "pdf", root / "src" / "core", root / "src" / "services"});
	// A name the toolkit also owns is its own header, not an engine include.
	for (const std::string& own : header_basenames({root / "src" / "ui"})) {
		names.erase(own);
	}
	return names;
}

void write_text(const fs::path& path, std::string_view text)
{
	fs::create_directories(path.parent_path());
	std::ofstream stream(path, std::ios::binary);
	stream << text;
}

// Files under `dir` that contain any of `tokens`. Whitespace is ignored (the
// compiler accepts `Microsoft :: UI`), and in a source file so are comments, so
// the check reads the code the compiler reads. The finding carries no line:
// whitespace removal discards them.
std::vector<Finding> scan_tokens(const fs::path& dir, const std::vector<std::string>& tokens)
{
	std::vector<Finding> findings;
	if (!fs::exists(dir)) {
		return findings;
	}
	for (const auto& entry : fs::recursive_directory_iterator(dir)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		std::string text = read_text(entry.path());
		if (is_source_file(entry.path())) {
			text = logical_source(text);
		}
		const std::string compact = without_whitespace(text);
		for (const std::string& token : tokens) {
			if (compact.find(token) != std::string::npos) {
				findings.push_back({entry.path(), 0, token});
			}
		}
	}
	return findings;
}

std::string describe(const std::vector<Finding>& findings)
{
	std::string text;
	for (const Finding& finding : findings) {
		text += finding.file.string() + ":" + std::to_string(finding.line) + " " + finding.text + "\n";
	}
	return text;
}

const std::vector<std::string>& winrt_ui_tokens()
{
	static const std::vector<std::string> tokens = {"Microsoft.UI", "winrt/Microsoft.UI", "Microsoft::UI"};
	return tokens;
}

} // namespace

RECTO_UI_TEST(Project, LibraryLinks)
{
	EXPECT_EQ(recto::ui::version(), recto::ui::kVersion);
}

RECTO_UI_TEST(Project, IncludeBoundary)
{
	const fs::path root = recto::test::repo_root();
	const std::set<std::string> engine_headers = engine_header_names(root);
	ASSERT_TRUE(engine_headers.contains("recto_pdf.h")) << "the engine header set must come from the real tree";

	// The real tree is clean.
	const std::vector<Finding> real = boundary_findings(root, engine_headers);
	EXPECT_TRUE(real.empty()) << describe(real);

	// A scratch copy of src/ui, with every forbidden shape planted, is caught.
	recto::test::TempDir scratch;
	fs::create_directories(scratch.path() / "src");
	fs::copy(root / "src" / "ui", scratch.path() / "src" / "ui", fs::copy_options::recursive);
	write_text(scratch.path() / "src" / "ui" / "planted_ui.cpp",
		"#include <windows.h>\n"
		"#include \"pch.h\"\n"
		"#include <recto/ui/ui.h>\n"
		"#include \"../pdf/document.h\"\n"
		"#include <core/anything.h>\n"
		"#include \"services\\anything.h\"\n"
		"#include \"recto_pdf.h\"\n"
		"#include \"recto_core.h\"\n"
		"#include \"recto_services.h\"\n"
		"#include /* a comment */ \"../pdf/document.h\"\n"
		"const char* literal = \"// not a comment\"; /* nor is this a hiding place */\n"
		"#include \"recto_pdf.h\" // reached after a literal\n"
		"const char* raw = R\"tag(\"/*)tag\";\n"
		"#include \"recto_core.h\"\n"
		"#define UI_HEADER \"recto_services.h\"\n"
		"#include UI_HEADER\n");
	write_text(scratch.path() / "src" / "ui" / "planted_bom.cpp", "\xEF\xBB\xBF#include \"recto_pdf.h\"\n");
	write_text(scratch.path() / "src" / "ui" / "planted_splice.cpp", "#inc\\\nlude \"recto_pdf.h\"\n");
	write_text(scratch.path() / "src" / "pdf" / "planted_pdf.cpp", "#include <recto/ui/ui.h>\n#include \"document.h\"\n#include /* toolkit */ \"../ui/include/recto/ui/ui.h\"\n#include /* a comment\nspanning lines */ \"../ui/include/recto/ui/ui.h\"\n");
	write_text(scratch.path() / "src" / "core" / "planted_core.cpp", "#include \"../ui/application.h\"\n");
	write_text(scratch.path() / "src" / "services" / "planted_services.h", "// #include <recto/ui/ui.h> in a comment is not an include\n#  include \"recto/ui/theme.h\"\n");

	const std::vector<Finding> planted = boundary_findings(scratch.path(), engine_headers);
	std::set<std::string> caught;
	for (const Finding& finding : planted) {
		caught.insert(finding.file.filename().string() + ":" + std::to_string(finding.line));
	}
	const std::set<std::string> expected = {
		"planted_ui.cpp:4", "planted_ui.cpp:5", "planted_ui.cpp:6", "planted_ui.cpp:7", "planted_ui.cpp:8", "planted_ui.cpp:9",
		"planted_ui.cpp:10", "planted_ui.cpp:12", "planted_ui.cpp:14", "planted_ui.cpp:16", "planted_bom.cpp:1", "planted_splice.cpp:1", "planted_pdf.cpp:1", "planted_pdf.cpp:3", "planted_pdf.cpp:4","planted_core.cpp:1",
		"planted_services.h:2"};
	EXPECT_EQ(caught, expected) << describe(planted);
}

RECTO_UI_TEST(Project, NoWinRtUiTypes)
{
	const fs::path root = recto::test::repo_root();
	const std::vector<Finding> real = scan_tokens(root / "src" / "ui", winrt_ui_tokens());
	EXPECT_TRUE(real.empty()) << describe(real);

	recto::test::TempDir scratch;
	fs::create_directories(scratch.path() / "src");
	fs::copy(root / "src" / "ui", scratch.path() / "src" / "ui", fs::copy_options::recursive);
	write_text(scratch.path() / "src" / "ui" / "planted_a.h", "#include <winrt/Microsoft.UI.Xaml.h>\n");
	write_text(scratch.path() / "src" / "ui" / "planted_b.cpp", "using namespace winrt::Microsoft::UI::Xaml;\nMicrosoft::UI::Xaml::Window w;\n");
	write_text(scratch.path() / "src" / "ui" / "planted_c.cpp", "winrt::Microsoft  ::\n\tUI::Xaml::Window w; // spaced out\n");
	write_text(scratch.path() / "src" / "ui" / "planted_d.cpp", "using namespace Microsoft . UI;\n");
	const std::vector<Finding> planted = scan_tokens(scratch.path() / "src" / "ui", winrt_ui_tokens());
	std::set<std::string> files;
	for (const Finding& finding : planted) {
		files.insert(finding.file.filename().string());
	}
	EXPECT_EQ(files, (std::set<std::string>{"planted_a.h", "planted_b.cpp", "planted_c.cpp", "planted_d.cpp"})) << describe(planted);
}

RECTO_UI_TEST(Project, HarnessSolid)
{
	constexpr uint32_t kSize = 64;
	// Premultiplied BGRA of an opaque colour: 0xE0 blue, 0x80 green, 0x20 red.
	const std::array<uint8_t, 4> fill_bgra{0xE0, 0x80, 0x20, 0xFF};

	const auto render = [&](float r, float g, float b) {
		return recto::ui::test::render_to_bitmap(
			[&](const recto::ui::test::RenderContext& context) {
				Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
				ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(r, g, b, 1.0F), &brush));
				context.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
				context.target->FillRectangle(D2D1::RectF(16.0F, 16.0F, 48.0F, 48.0F), brush.Get());
			},
			kSize, kSize, 1.0F);
	};

	// Expected: transparent black everywhere except the 32 x 32 square in the middle.
	std::vector<uint8_t> expected(static_cast<size_t>(kSize) * kSize * 4, 0);
	for (uint32_t y = 16; y < 48; ++y) {
		for (uint32_t x = 16; x < 48; ++x) {
			std::copy(fill_bgra.begin(), fill_bgra.end(), expected.begin() + (static_cast<size_t>(y) * kSize + x) * 4);
		}
	}

	const recto::ui::test::Bitmap bitmap = render(0x20 / 255.0F, 0x80 / 255.0F, 0xE0 / 255.0F);
	ASSERT_EQ(bitmap.width, kSize);
	ASSERT_EQ(bitmap.height, kSize);
	ASSERT_GE(bitmap.stride, static_cast<size_t>(kSize) * 4);
	const recto::test::PixelDiffResult same =
		recto::test::expect_rgba_equal(bitmap.bytes, expected, kSize, kSize, bitmap.stride, 0, 0);
	EXPECT_TRUE(same.matches);

	// The test can fail: a different fill colour is a different image.
	const recto::ui::test::Bitmap other = render(0xE0 / 255.0F, 0x80 / 255.0F, 0x20 / 255.0F);
	const recto::test::PixelDiffResult different =
		recto::test::compare_rgba(other.bytes, expected, kSize, kSize, other.stride, 0, 0);
	EXPECT_FALSE(different.matches);
	EXPECT_EQ(different.first_x, 16U);
	EXPECT_EQ(different.first_y, 16U);
}

RECTO_UI_TEST(Project, HarnessScalesEffectivePixels)
{
	// 8..24 epx at 200 percent is 16..48 device pixels: the harness applies the DPI.
	const recto::ui::test::Bitmap bitmap = recto::ui::test::render_to_bitmap(
		[](const recto::ui::test::RenderContext& context) {
			EXPECT_FLOAT_EQ(context.width_epx, 32.0F);
			Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
			ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(1.0F, 1.0F, 1.0F, 1.0F), &brush));
			context.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
			context.target->FillRectangle(D2D1::RectF(8.0F, 8.0F, 24.0F, 24.0F), brush.Get());
		},
		64, 64, 2.0F);
	const auto pixel = [&](uint32_t x, uint32_t y) { return bitmap.bytes[static_cast<size_t>(y) * bitmap.stride + static_cast<size_t>(x) * 4 + 3]; };
	EXPECT_EQ(pixel(15, 32), 0);
	EXPECT_EQ(pixel(16, 32), 255);
	EXPECT_EQ(pixel(47, 32), 255);
	EXPECT_EQ(pixel(48, 32), 0);
}

RECTO_UI_TEST(Project, HarnessStride)
{
	constexpr uint32_t kSize = 64;
	constexpr size_t kPadding = 80;
	const auto draw = [](const recto::ui::test::RenderContext& context) {
		// Left half one colour, right half another: a reader that assumes
		// stride == width * 4 sees every row shifted and cannot match.
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> left;
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> right;
		ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(1.0F, 0.0F, 0.0F, 1.0F), &left));
		ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(0.0F, 0.0F, 1.0F, 1.0F), &right));
		context.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
		context.target->FillRectangle(D2D1::RectF(0.0F, 0.0F, 32.0F, 64.0F), left.Get());
		context.target->FillRectangle(D2D1::RectF(32.0F, 0.0F, 64.0F, 64.0F), right.Get());
	};

	const recto::ui::test::Bitmap natural = recto::ui::test::render_to_bitmap(draw, kSize, kSize, 1.0F);
	recto::ui::test::RenderOptions options;
	options.min_stride = static_cast<size_t>(kSize) * 4 + kPadding;
	const recto::ui::test::Bitmap padded = recto::ui::test::render_to_bitmap(draw, kSize, kSize, 1.0F, options);

	ASSERT_GT(padded.stride, static_cast<size_t>(kSize) * 4) << "the harness must honour a wider stride";
	ASSERT_EQ(padded.bytes.size(), padded.stride * kSize);

	// Respecting each side's own stride, the pixels are equal.
	const recto::test::PixelDiffResult respected =
		recto::test::expect_rgba_equal(padded.bytes, natural.bytes, kSize, kSize, padded.stride, natural.stride, 0);
	EXPECT_TRUE(respected.matches);

	// Assuming stride == width * 4 does not: this is the cheaper substitute the
	// section forbids, and it must fail.
	const recto::test::PixelDiffResult assumed =
		recto::test::compare_rgba(padded.bytes, natural.bytes, kSize, kSize, static_cast<size_t>(kSize) * 4, natural.stride, 0);
	EXPECT_FALSE(assumed.matches);
}

RECTO_UI_TEST(Project, HarnessRejectsAbsurdStride)
{
	// A stride this wide would wrap stride * height and write past the allocation.
	recto::ui::test::RenderOptions options;
	options.min_stride = size_t{1} << 63;
	EXPECT_THROW(
		{ const auto bitmap = recto::ui::test::render_to_bitmap({}, 1, 2, 1.0F, options); (void)bitmap; },
		std::runtime_error);
}

RECTO_UI_TEST(Project, DisabledMarkerStaysAtTheFront)
{
	EXPECT_EQ(recto::ui::test::registered_name("Project", "IncludeBoundary"), "Project.IncludeBoundary");
	// GoogleTest disables on a DISABLED_ prefix of the case name, so it must lead.
	EXPECT_EQ(recto::ui::test::registered_name("Project", "DISABLED_Probe"), "DISABLED_Project.Probe");
}
