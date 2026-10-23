// D10 T01 section 1: the recto-ui project, its module boundary and the
// headless render harness. Tests are named Ui.Project.* (see ui_test.h).
#include <recto/ui/ui.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "test_support.h"
#include "ui_render_harness.h"
#include "ui_test.h"

#include <windows.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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

// Every file a directive could include, whatever its extension (`.def`, `.txt`
// or none): everything under a module folder except the MSBuild, manifest and
// binary resource files that no compiler reads as source. A deny list, so a new
// kind of fragment is scanned by default.
bool is_source_file(const fs::path& path)
{
	static const std::set<std::string> not_source = {".vcxproj", ".filters", ".user", ".props", ".targets", ".md",
		".json", ".png", ".ico", ".bmp", ".ttf", ".otf", ".manifest", ".appxmanifest", ".pdb", ".lib", ".obj"};
	return !not_source.contains(lower(path.extension().string()));
}

bool is_header_file(const fs::path& path)
{
	static const std::set<std::string> extensions = {".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp", ".inc"};
	return extensions.contains(lower(path.extension().string()));
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

// UTF-16 source text (MSVC compiles a file that starts with a UTF-16 byte
// order mark, whatever /utf-8 says) as UTF-8, so the scans read what the
// compiler reads; anything else is returned as it is.
std::string utf8_source(const std::string& bytes)
{
	const bool little = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE;
	const bool big = bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE && static_cast<unsigned char>(bytes[1]) == 0xFF;
	if (!little && !big) {
		return bytes;
	}
	std::wstring wide;
	for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
		const auto lo = static_cast<unsigned char>(bytes[little ? i : i + 1]);
		const auto hi = static_cast<unsigned char>(bytes[little ? i + 1 : i]);
		wide.push_back(static_cast<wchar_t>(lo | (hi << 8)));
	}
	if (wide.empty()) {
		return {};
	}
	const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
	return out;
}

// Every end of line (CR LF, or a CR or LF alone) as one LF. Translation phase 1
// ([lex.phases]) maps each of them to a new-line, and MSVC and Clang both end a
// line, and so a // comment, at a bare CR; a scan that stops only at LF would
// read the next line as part of the comment.
std::string normalized_newlines(const std::string& text)
{
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\r') {
			out.push_back('\n');
			if (i + 1 < text.size() && text[i + 1] == '\n') {
				++i;
			}
		} else {
			out.push_back(text[i]);
		}
	}
	return out;
}

// Reads a whole file as UTF-8 text with LF line ends, as the compiler's first
// translation phase sees it. A file that cannot be opened or read is a failure,
// never empty text: an unreadable file would otherwise pass every scan.
std::string read_text(const fs::path& path)
{
	std::ifstream stream(recto::test::to_long_path(path), std::ios::binary);
	if (!stream) {
		throw std::runtime_error("cannot open " + path.string() + " for the boundary scan");
	}
	std::ostringstream buffer;
	buffer << stream.rdbuf();
	if (stream.bad()) {
		throw std::runtime_error("cannot read " + path.string() + " for the boundary scan");
	}
	return normalized_newlines(utf8_source(buffer.str()));
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

// An identifier character as the lexer needs it: ASCII letters, digits and _,
// and every byte of a UTF-8 multi-byte character, since identifiers may hold
// Unicode letters ([lex.name]; MSVC reads this source as UTF-8 under /utf-8).
bool is_identifier_char(char c)
{
	const auto byte = static_cast<unsigned char>(c);
	return std::isalnum(byte) != 0 || c == '_' || byte >= 0x80;
}

// True when `text` ends with a whole raw-string prefix token (R, LR, uR, UR or
// u8R) rather than an identifier that merely ends in R: `OUTR"x("` is the
// identifier OUTR followed by an ordinary string literal ([lex.string]).
bool raw_string_prefix(const std::string& text)
{
	size_t start = text.size();
	while (start > 0 && is_identifier_char(text[start - 1])) {
		--start;
	}
	const std::string_view token(text.data() + start, text.size() - start);
	return token == "R" || token == "LR" || token == "uR" || token == "UR" || token == "u8R";
}

// What the compiler's preprocessor sees (translation phases 1 to 3), with the
// physical line of every character kept so a finding names the line a person
// opens the file at.
struct LogicalSource {
	std::string text;
	std::vector<size_t> line; // physical line (1-based) of each character of `text`
};

// Lines joined at a backslash-newline, then every comment replaced by one
// space. Literals, preprocessing numbers (`1'000` is one number, not a
// character literal) and the header name of an #include or #import directive
// are copied whole, so a comment opener inside them starts no comment, and a
// raw string's body is dropped (its newlines kept) so text inside it is never
// read as code. A regex over physical lines misses `#include /* c */ "x.h"` and
// an include split across a continuation, both of which the compiler accepts.
LogicalSource logical_source(const std::string& raw_with_bom)
{
	// A UTF-8 byte order mark is not part of the first line the compiler lexes.
	const std::string_view bom = "\xEF\xBB\xBF";
	const std::string raw = raw_with_bom.starts_with(bom) ? raw_with_bom.substr(bom.size()) : raw_with_bom;
	std::string spliced;
	std::vector<size_t> spliced_line;
	spliced.reserve(raw.size());
	size_t physical = 1;
	for (size_t i = 0; i < raw.size(); ++i) {
		// (read_text has already made every line end a single LF.)
		if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\n') {
			++i;
			++physical;
			continue;
		}
		spliced.push_back(raw[i]);
		spliced_line.push_back(physical);
		if (raw[i] == '\n') {
			++physical;
		}
	}
	LogicalSource out;
	out.text.reserve(spliced.size());
	const auto emit = [&](char c, size_t at) {
		out.text.push_back(c);
		out.line.push_back(spliced_line.empty() ? 1 : spliced_line[std::min(at, spliced_line.size() - 1)]);
	};
	// True when the current output line so far is `#include` or `#import` and
	// optional white space: the next `<` or `"` opens a header name.
	const auto at_header_name = [&]() {
		const size_t newline = out.text.rfind('\n');
		const size_t start = newline == std::string::npos ? 0 : newline + 1;
		static const std::regex directive(R"(^\s*(?:#|%:)\s*(include|import)\s*$)");
		return std::regex_match(out.text.substr(start), directive);
	};
	// Where the last preprocessing number ended in `out.text`.
	size_t number_end = std::string::npos;
	for (size_t i = 0; i < spliced.size(); ++i) {
		const char c = spliced[i];
		const char next = i + 1 < spliced.size() ? spliced[i + 1] : '\0';
		const char previous = out.text.empty() ? '\0' : out.text.back();
		if ((c == '<' || c == '"') && at_header_name()) {
			// A header name: no escapes and no comments inside it.
			const char close = c == '<' ? '>' : '"';
			emit(c, i);
			for (++i; i < spliced.size() && spliced[i] != close && spliced[i] != '\n'; ++i) {
				emit(spliced[i], i);
			}
			if (i < spliced.size()) {
				emit(spliced[i], i);
			}
		} else if (c == '/' && next == '/') {
			while (i + 1 < spliced.size() && spliced[i + 1] != '\n') {
				++i;
			}
		} else if (c == '/' && next == '*') {
			const size_t opened = i;
			i += 2;
			while (i + 1 < spliced.size() && !(spliced[i] == '*' && spliced[i + 1] == '/')) {
				++i;
			}
			++i;
			// One space for the whole comment, newlines inside it included: a comment
			// that spans lines does not split the directive around it.
			emit(' ', opened);
		} else if ((std::isdigit(static_cast<unsigned char>(c)) != 0 ||
					   (c == '.' && std::isdigit(static_cast<unsigned char>(next)) != 0)) &&
				   !is_identifier_char(previous)) {
			// A preprocessing number ([lex.ppnumber]): digits, identifier characters,
			// dots, a digit separator ' before a digit or letter, and a sign after e, E, p or P.
			emit(c, i);
			while (i + 1 < spliced.size()) {
				const char n = spliced[i + 1];
				const char after = i + 2 < spliced.size() ? spliced[i + 2] : '\0';
				const char last = spliced[i];
				if (is_identifier_char(n) || n == '.' || (n == '\'' && is_identifier_char(after)) ||
					((n == '+' || n == '-') && (last == 'e' || last == 'E' || last == 'p' || last == 'P'))) {
					++i;
					emit(n, i);
				} else {
					break;
				}
			}
			number_end = out.text.size();
		} else if (c == '"' && number_end != out.text.size() && raw_string_prefix(out.text) &&
				   raw_string_end(spliced, i) != std::string::npos) {
			// (An R that ends a preprocessing number, as in 0.R"x", is part of the
			// number, so that quote opens an ordinary string, not a raw one.)
			// R"delim( ... )delim": the body is opaque, whatever quotes, comment
			// openers or directives it holds. Only its newlines are kept.
			const size_t end = raw_string_end(spliced, i);
			emit('"', i);
			for (size_t k = i + 1; k + 1 < end; ++k) {
				if (spliced[k] == '\n') {
					emit('\n', k);
				}
			}
			emit('"', end - 1);
			i = end - 1;
		} else if (c == '"' || c == '\'') {
			emit(c, i);
			for (++i; i < spliced.size() && spliced[i] != c && spliced[i] != '\n'; ++i) {
				if (spliced[i] == '\\' && i + 1 < spliced.size()) {
					emit(spliced[i], i);
					++i;
				}
				emit(spliced[i], i);
			}
			if (i < spliced.size()) {
				emit(spliced[i], i);
			}
		} else {
			emit(c, i);
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

// Walks `dir` and reports each #include line whose target `forbidden` rejects,
// at the physical line of the included name.
template <typename Predicate>
std::vector<Finding> scan_includes(const fs::path& dir, Predicate forbidden)
{
	// `%:` is the alternative spelling of `#` ([lex.digraph]).
	static const std::regex include_line(R"(^\s*(?:#|%:)\s*(?:include|import)\s*[<"]([^>"]+)[>"])");
	static const std::regex computed_include(R"(^\s*(?:#|%:)\s*(?:include|import)\s*([^<"\s].*)$)");
	std::vector<Finding> findings;
	if (!fs::exists(dir)) {
		return findings;
	}
	for (const auto& entry : fs::recursive_directory_iterator(dir)) {
		if (!entry.is_regular_file() || !is_source_file(entry.path())) {
			continue;
		}
		const LogicalSource source = logical_source(read_text(entry.path()));
		size_t start = 0;
		while (start < source.text.size()) {
			const size_t end = std::min(source.text.find('\n', start), source.text.size());
			const std::string line = source.text.substr(start, end - start);
			std::smatch match;
			if (std::regex_search(line, match, include_line)) {
				if (forbidden(match[1].str())) {
					findings.push_back({entry.path(), source.line[start + static_cast<size_t>(match.position(1))], match[1].str()});
				}
			} else if (std::regex_search(line, match, computed_include)) {
				// #include MACRO hides its target from any text scan, so inside the
				// guarded modules it is refused outright.
				findings.push_back({entry.path(), source.line[start + static_cast<size_t>(match.position(1))],
					"computed include " + match[1].str()});
			}
			start = end + 1;
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

// The engine libraries may not include recto-ui by any path, nor any header
// the toolkit owns by bare name (ui.h, application.h and the rest).
bool engine_includes_ui(const std::string& include_path, const std::set<std::string>& ui_headers)
{
	const std::vector<std::string> parts = split_path(include_path);
	if (parts.empty()) {
		return false;
	}
	for (const std::string& part : parts) {
		if (part == "ui") {
			return true;
		}
	}
	return ui_headers.contains(parts.back());
}

// Header names one side owns and the other does not (a name both own, such as
// pch.h, is each side's own header, not a crossing).
struct HeaderNames {
	std::set<std::string> engine;
	std::set<std::string> ui;
};

HeaderNames header_names(const fs::path& root)
{
	const std::set<std::string> engine = header_basenames({root / "src" / "pdf", root / "src" / "core", root / "src" / "services"});
	const std::set<std::string> ui = header_basenames({root / "src" / "ui"});
	HeaderNames names;
	std::set_difference(engine.begin(), engine.end(), ui.begin(), ui.end(), std::inserter(names.engine, names.engine.end()));
	std::set_difference(ui.begin(), ui.end(), engine.begin(), engine.end(), std::inserter(names.ui, names.ui.end()));
	return names;
}

// One preprocessing token of a logical source ([lex.pptoken]), enough to find
// linker directives: identifiers, string and character literals (with any
// encoding prefix), numbers and punctuators. `at` indexes the logical text;
// `line_start` is true for the first token of a line, where a directive's #
// must stand.
struct PpToken {
	std::string text;
	size_t at = 0;
	bool literal = false;
	bool line_start = false;
};

std::vector<PpToken> pp_tokens(const std::string& text)
{
	static const std::set<std::string> prefixes = {"l", "u", "u8", "lr", "ur", "u8r", "r"};
	std::vector<PpToken> tokens;
	bool line_start = true;
	size_t i = 0;
	const auto quoted = [&](size_t open) {
		// From the opening quote at `open` to one past its closing quote (or the line end).
		const char close = text[open];
		size_t k = open + 1;
		for (; k < text.size() && text[k] != close && text[k] != '\n'; ++k) {
			if (text[k] == '\\' && k + 1 < text.size()) {
				++k;
			}
		}
		return k < text.size() && text[k] == close ? k + 1 : k;
	};
	while (i < text.size()) {
		const char c = text[i];
		if (c == '\n') {
			line_start = true;
			++i;
			continue;
		}
		if (std::isspace(static_cast<unsigned char>(c)) != 0) {
			++i;
			continue;
		}
		PpToken token;
		token.at = i;
		token.line_start = line_start;
		line_start = false;
		size_t end = i + 1;
		if (is_identifier_char(c) && std::isdigit(static_cast<unsigned char>(c)) == 0) {
			while (end < text.size() && is_identifier_char(text[end])) {
				++end;
			}
			if (end < text.size() && (text[end] == '"' || text[end] == '\'') && prefixes.contains(lower(text.substr(i, end - i)))) {
				end = quoted(end);
				token.literal = true;
			}
		} else if (std::isdigit(static_cast<unsigned char>(c)) != 0 || (c == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)) {
			while (end < text.size() && (is_identifier_char(text[end]) || text[end] == '.' ||
										 (text[end] == '\'' && end + 1 < text.size() && is_identifier_char(text[end + 1])))) {
				++end;
			}
		} else if (c == '"' || c == '\'') {
			end = quoted(i);
			token.literal = true;
		} else if (c == '%' && i + 1 < text.size() && text[i + 1] == ':') {
			end = i + 2;
		}
		token.text = text.substr(i, end - i);
		tokens.push_back(std::move(token));
		i = end;
	}
	return tokens;
}

// Appends code point `code` to `out` as UTF-8.
void append_utf8(std::string& out, uint32_t code)
{
	if (code < 0x80) {
		out.push_back(static_cast<char>(code));
	} else if (code < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (code >> 6)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
	} else if (code < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (code >> 12)));
		out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (code >> 18)));
		out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
	}
}

// The value of an ordinary string literal token ("..." with no prefix), escape
// sequences decoded as the compiler decodes them ([lex.string], [lex.ccon]):
// simple escapes, octal, \x hex, \u and \U. Nothing for any other token, or
// for an escape this scan does not decode (the delimited \o{} \x{} \u{} \N{}
// forms, or an unknown one), so such an argument is refused, never misread.
std::optional<std::string> string_value(const PpToken& token)
{
	if (!token.literal || token.text.size() < 2 || token.text.front() != '"' || token.text.back() != '"') {
		return std::nullopt;
	}
	const std::string_view body = std::string_view(token.text).substr(1, token.text.size() - 2);
	static const std::string simple_from = "ntvbrfa\\?'\"";
	static const std::string simple_to = "\n\t\v\b\r\f\a\\?'\"";
	std::string out;
	const auto is_octal = [](char c) { return c >= '0' && c <= '7'; };
	for (size_t i = 0; i < body.size(); ++i) {
		if (body[i] != '\\') {
			out.push_back(body[i]);
			continue;
		}
		if (++i >= body.size()) {
			return std::nullopt;
		}
		const char e = body[i];
		if (const size_t simple = simple_from.find(e); simple != std::string::npos) {
			out.push_back(simple_to[simple]);
		} else if (is_octal(e)) {
			uint32_t value = 0;
			size_t digits = 0;
			for (; digits < 3 && i < body.size() && is_octal(body[i]); ++digits, ++i) {
				value = value * 8 + static_cast<uint32_t>(body[i] - '0');
			}
			--i;
			out.push_back(static_cast<char>(value & 0xFF));
		} else if (e == 'x' || e == 'u' || e == 'U') {
			const size_t want = e == 'u' ? 4 : e == 'U' ? 8 : 0;
			if (i + 1 < body.size() && body[i + 1] == '{') {
				return std::nullopt;
			}
			uint32_t value = 0;
			size_t digits = 0;
			for (; i + 1 < body.size() && std::isxdigit(static_cast<unsigned char>(body[i + 1])) != 0 && (want == 0 || digits < want); ++digits) {
				++i;
				const char h = static_cast<char>(std::tolower(static_cast<unsigned char>(body[i])));
				value = value * 16 + static_cast<uint32_t>(h <= '9' ? h - '0' : h - 'a' + 10);
				if (value > 0x10FFFF) {
					return std::nullopt;
				}
			}
			if (digits == 0 || (want != 0 && digits != want)) {
				return std::nullopt;
			}
			if (e == 'x') {
				out.push_back(static_cast<char>(value & 0xFF));
			} else {
				append_utf8(out, value);
			}
		} else {
			return std::nullopt;
		}
	}
	return out;
}

// A `comment(lib, ...)` or `comment(linker, ...)` pragma body starting at
// tokens[i]: its kind, and its argument's value when the argument is one or
// more ordinary string literals (adjacent literals concatenate), else nothing.
struct LinkDirective {
	std::string kind;
	std::optional<std::string> value;
	std::string spelling;
};

std::optional<LinkDirective> comment_directive(const std::vector<PpToken>& tokens, size_t i)
{
	if (i + 2 >= tokens.size() || lower(tokens[i].text) != "comment" || tokens[i + 1].text != "(") {
		return std::nullopt;
	}
	LinkDirective directive{lower(tokens[i + 2].text), std::string(), std::string()};
	if (directive.kind != "lib" && directive.kind != "linker") {
		return std::nullopt;
	}
	size_t k = i + 3;
	if (k >= tokens.size() || tokens[k].text != ",") {
		directive.value.reset();
		return directive;
	}
	bool plain = true;
	size_t literals = 0;
	for (++k; k < tokens.size() && tokens[k].text != ")"; ++k) {
		directive.spelling += tokens[k].text;
		const std::optional<std::string> value = string_value(tokens[k]);
		if (value) {
			*directive.value += *value;
			++literals;
		} else {
			plain = false;
		}
	}
	if (!plain || literals == 0) {
		directive.value.reset();
	}
	return directive;
}

// Walks `dir` and reports each linker directive in source that could link a
// library the module may not reach: `comment(lib, ...)` or
// `comment(linker, ...)` in a `#pragma` (or `%:pragma`) directive, a
// `__pragma(...)` or a `_Pragma("...")` ([cpp.pragma.op]). A directive links
// a library with no project setting at all, so the MSBuild check
// (tests/scripts/test_ui_module_boundary.py) cannot see it. Directives are
// found as tokens, so text inside an ordinary string literal is never one, and
// the argument's literals are decoded as the compiler decodes them, so an
// escape spells no name past the check. The decoded name may not be one of
// `forbidden_stems` (library names without .lib, lower case); an argument that
// is not plain string literals (a macro, a raw string, an escape not decoded
// here) hides its target and is refused outright, like a computed include.
std::vector<Finding> scan_link_directives(const fs::path& dir, const std::vector<std::string>& forbidden_stems)
{
	std::vector<Finding> findings;
	if (!fs::exists(dir)) {
		return findings;
	}
	for (const auto& entry : fs::recursive_directory_iterator(dir)) {
		if (!entry.is_regular_file() || !is_source_file(entry.path())) {
			continue;
		}
		const LogicalSource source = logical_source(read_text(entry.path()));
		const std::vector<PpToken> tokens = pp_tokens(source.text);
		const auto report = [&](size_t at, const LinkDirective& directive) {
			const size_t line = source.line.empty() ? 1 : source.line[std::min(at, source.line.size() - 1)];
			if (!directive.value) {
				findings.push_back({entry.path(), line, "computed linker directive " + directive.spelling});
				return;
			}
			const std::string named = lower(*directive.value);
			// comment(lib) names one library file (the linker adds .lib); a
			// comment(linker) option such as /DEFAULTLIB: may name it anywhere.
			const std::vector<std::string> parts = split_path(named);
			std::string library = parts.empty() ? std::string() : parts.back();
			if (library.ends_with(".lib")) {
				library.resize(library.size() - 4);
			}
			const bool hit = std::any_of(forbidden_stems.begin(), forbidden_stems.end(), [&](const std::string& stem) {
				return directive.kind == "lib" ? library == stem : named.find(stem) != std::string::npos;
			});
			if (hit) {
				findings.push_back({entry.path(), line, "linker directive " + *directive.value});
			}
		};
		for (size_t j = 0; j < tokens.size(); ++j) {
			const PpToken& token = tokens[j];
			const bool open = j + 1 < tokens.size() && tokens[j + 1].text == "(";
			std::optional<LinkDirective> directive;
			if ((token.text == "#" || token.text == "%:") && token.line_start && j + 1 < tokens.size() && tokens[j + 1].text == "pragma") {
				directive = comment_directive(tokens, j + 2);
			} else if (token.text == "__pragma" && open) {
				directive = comment_directive(tokens, j + 2);
			} else if (token.text == "_Pragma" && open && j + 2 < tokens.size() && tokens[j + 2].literal) {
				// Destringize ([cpp.pragma.op]): drop an L prefix and the quotes, read
				// \" as " and \\ as \, and lex the result as the pragma's tokens.
				std::string body = tokens[j + 2].text;
				if (body.starts_with("L")) {
					body.erase(0, 1);
				}
				if (body.size() >= 2 && body.front() == '"' && body.back() == '"') {
					std::string pragma;
					for (size_t k = 1; k + 1 < body.size(); ++k) {
						if (body[k] == '\\' && k + 2 < body.size() && (body[k + 1] == '"' || body[k + 1] == '\\')) {
							++k;
						}
						pragma.push_back(body[k]);
					}
					directive = comment_directive(pp_tokens(pragma), 0);
				}
			}
			if (directive) {
				report(token.at, *directive);
			}
		}
	}
	return findings;
}

// Boundary scan over one repository tree rooted at `root` (a checkout or a
// scratch copy), with the header names of the real tree.
std::vector<Finding> boundary_findings(const fs::path& root, const HeaderNames& names)
{
	std::vector<Finding> all;
	for (const Finding& finding : scan_includes(root / "src" / "ui", [&](const std::string& include_path) {
			 return ui_includes_engine(include_path, names.engine);
		 })) {
		all.push_back(finding);
	}
	for (const char* engine_dir : {"pdf", "core", "services"}) {
		for (const Finding& finding : scan_includes(root / "src" / engine_dir, [&](const std::string& include_path) {
				 return engine_includes_ui(include_path, names.ui);
			 })) {
			all.push_back(finding);
		}
		for (const Finding& finding : scan_link_directives(root / "src" / engine_dir, {"recto-ui"})) {
			all.push_back(finding);
		}
	}
	for (const Finding& finding : scan_link_directives(root / "src" / "ui", {"recto-pdf", "recto-core", "recto-services"})) {
		all.push_back(finding);
	}
	return all;
}

void write_text(const fs::path& path, std::string_view text)
{
	fs::create_directories(path.parent_path());
	std::ofstream stream(path, std::ios::binary);
	stream << text;
}

// Files under `dir` that contain any of `tokens`, each at the physical line
// where the token starts. Whitespace is ignored (the compiler accepts
// `Microsoft :: UI`), and in a source file so are comments, so the check reads
// the code the compiler reads.
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
		const std::string raw = read_text(entry.path());
		LogicalSource source;
		if (is_source_file(entry.path())) {
			source = logical_source(raw);
		} else {
			source.text = raw;
			size_t line = 1;
			for (const char c : raw) {
				source.line.push_back(line);
				line += c == '\n' ? 1 : 0;
			}
		}
		std::string compact;
		std::vector<size_t> compact_line;
		for (size_t i = 0; i < source.text.size(); ++i) {
			if (!std::isspace(static_cast<unsigned char>(source.text[i]))) {
				compact.push_back(source.text[i]);
				compact_line.push_back(source.line[i]);
			}
		}
		for (const std::string& token : tokens) {
			for (size_t at = compact.find(token); at != std::string::npos; at = compact.find(token, at + 1)) {
				findings.push_back({entry.path(), compact_line[at], token});
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

// Full names that occur more than once in `names`.
std::set<std::string> duplicate_names(const std::vector<std::string>& names)
{
	std::set<std::string> seen;
	std::set<std::string> duplicates;
	for (const std::string& name : names) {
		if (!seen.insert(name).second) {
			duplicates.insert(name);
		}
	}
	return duplicates;
}

// This test binary's own path.
fs::path self_path()
{
	std::wstring self(MAX_PATH, L'\0');
	for (;;) {
		const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
		if (length < self.size()) {
			self.resize(length);
			return fs::path(self);
		}
		self.resize(self.size() * 2);
	}
}

constexpr DWORD kChildDeadlineMs = 60000;

using Variables = std::vector<std::pair<std::wstring, std::wstring>>;

// The name part of an environment entry `NAME=value`. Entries such as
// `=C:=C:\dir` begin with `=`, which is part of their name.
std::wstring_view variable_name(std::wstring_view entry)
{
	const size_t equals = entry.find(L'=', 1);
	return entry.substr(0, equals == std::wstring_view::npos ? entry.size() : equals);
}

// This process's environment for a child run of the test binary, without any
// GTEST_ variable and with `variables` added. A probe states its whole
// GoogleTest configuration on its command line (plus `variables`): a parent's
// sharding, empty-selection or output settings must never change what a probe
// selects or how it exits. The block is sorted by name, case-insensitively and
// ordinally, as CreateProcessW expects.
std::wstring child_environment(const Variables& variables)
{
	std::vector<std::wstring> entries;
	wchar_t* strings = GetEnvironmentStringsW();
	for (const wchar_t* entry = strings; entry != nullptr && *entry != L'\0'; entry += std::wcslen(entry) + 1) {
		const std::wstring_view name = variable_name(entry);
		const bool google_test = name.size() >= 6 && _wcsnicmp(name.data(), L"GTEST_", 6) == 0;
		const bool replaced = std::any_of(variables.begin(), variables.end(), [&](const auto& variable) {
			return CompareStringOrdinal(name.data(), static_cast<int>(name.size()), variable.first.data(),
					   static_cast<int>(variable.first.size()), TRUE) == CSTR_EQUAL;
		});
		if (!google_test && !replaced) {
			entries.emplace_back(entry);
		}
	}
	FreeEnvironmentStringsW(strings);
	for (const auto& [name, value] : variables) {
		entries.push_back(name + L"=" + value);
	}
	std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
		const std::wstring_view left = variable_name(a);
		const std::wstring_view right = variable_name(b);
		return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()),
				   TRUE) == CSTR_LESS_THAN;
	});
	std::wstring block;
	for (const std::wstring& entry : entries) {
		block.append(entry);
		block.push_back(L'\0');
	}
	block.push_back(L'\0');
	return block;
}

// Sets one environment variable of this process for the object's lifetime,
// then restores the value it had (or removes it), even when the test fails or
// throws in between.
class ScopedVariable {
public:
	ScopedVariable(std::wstring name, const wchar_t* value) : name_(std::move(name))
	{
		std::wstring old(GetEnvironmentVariableW(name_.c_str(), nullptr, 0), L'\0');
		while (!old.empty()) {
			const DWORD length = GetEnvironmentVariableW(name_.c_str(), old.data(), static_cast<DWORD>(old.size()));
			if (length < old.size()) {
				old.resize(length);
				previous_ = old;
				break;
			}
			old.resize(length);
		}
		SetEnvironmentVariableW(name_.c_str(), value);
	}
	~ScopedVariable() { SetEnvironmentVariableW(name_.c_str(), previous_ ? previous_->c_str() : nullptr); }
	ScopedVariable(const ScopedVariable&) = delete;
	ScopedVariable& operator=(const ScopedVariable&) = delete;

private:
	std::wstring name_;
	std::optional<std::wstring> previous_;
};

// Runs this test binary with `arguments`, and `variables` in place of any
// GTEST_ variable this process has, and returns its exit code and output.
std::pair<DWORD, std::string> run_self(
	const std::wstring& arguments, DWORD deadline_ms = kChildDeadlineMs, const Variables& variables = {})
{
	SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
	HANDLE read_end = nullptr;
	HANDLE write_end = nullptr;
	if (CreatePipe(&read_end, &write_end, &inherit, 0) == FALSE) {
		throw std::runtime_error("CreatePipe failed");
	}
	SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdOutput = write_end;
	startup.hStdError = write_end;
	const fs::path exe = self_path();
	std::wstring command = L"\"" + exe.wstring() + L"\" " + arguments;
	PROCESS_INFORMATION process{};
	std::wstring environment = child_environment(variables);
	const BOOL started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(), nullptr, &startup, &process);
	CloseHandle(write_end);
	if (started == FALSE) {
		CloseHandle(read_end);
		throw std::runtime_error("could not start the test binary");
	}
	// The pipe is drained on its own thread so a child that stalls cannot block
	// this test past its deadline; a child still running at the deadline is
	// terminated and reaped, and the run fails.
	std::string output;
	std::thread reader([&] {
		std::array<char, 4096> buffer{};
		DWORD got = 0;
		while (ReadFile(read_end, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr) != FALSE && got > 0) {
			output.append(buffer.data(), got);
		}
	});
	const bool finished = WaitForSingleObject(process.hProcess, deadline_ms) == WAIT_OBJECT_0;
	if (!finished) {
		TerminateProcess(process.hProcess, 124);
		WaitForSingleObject(process.hProcess, INFINITE);
	}
	reader.join();
	CloseHandle(read_end);
	DWORD code = 0;
	GetExitCodeProcess(process.hProcess, &code);
	CloseHandle(process.hProcess);
	CloseHandle(process.hThread);
	if (!finished) {
		throw std::runtime_error("the test binary was still running at its deadline and was terminated");
	}
	return {code, output};
}

} // namespace

RECTO_UI_TEST(Project, LibraryLinks)
{
	EXPECT_EQ(recto::ui::version(), recto::ui::kVersion);
}

RECTO_UI_TEST(Project, IncludeBoundary)
{
	const fs::path root = recto::test::repo_root();
	const HeaderNames names = header_names(root);
	ASSERT_TRUE(names.engine.contains("recto_pdf.h")) << "the engine header set must come from the real tree";
	ASSERT_TRUE(names.ui.contains("ui.h")) << "the toolkit header set must come from the real tree";
	ASSERT_FALSE(names.engine.contains("pch.h")) << "a name both sides own is neither side's crossing";

	// The real tree is clean.
	const std::vector<Finding> real = boundary_findings(root, names);
	EXPECT_TRUE(real.empty()) << describe(real);

	// A scratch copy of src/ui, with every forbidden shape planted, is caught at
	// the physical line of the included name, and nothing else is.
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
	// A raw string's body is text, not code; the include after it is code.
	write_text(scratch.path() / "src" / "ui" / "planted_raw.cpp",
		"const char* text = R\"x(\n#include \"recto_pdf.h\"\n)x\";\n#include \"recto_core.h\"\n");
	// A comment that ends on the directive's line: the finding names line 2.
	write_text(scratch.path() / "src" / "ui" / "planted_lines.cpp", "/* a\nb */ #include \"recto_pdf.h\"\n");
	write_text(scratch.path() / "src" / "pdf" / "planted_pdf.cpp",
		"#include <recto/ui/ui.h>\n#include \"document.h\"\n#include /* toolkit */ \"../ui/include/recto/ui/ui.h\"\n"
		"#include /* a comment\nspanning lines */ \"../ui/include/recto/ui/ui.h\"\n");
	// Toolkit headers by bare name, from an engine library.
	write_text(scratch.path() / "src" / "pdf" / "planted_bare.cpp", "#include \"ui.h\"\n#include <application.h>\n");
	write_text(scratch.path() / "src" / "core" / "planted_core.cpp", "#include \"../ui/application.h\"\n");
	// A digit separator is part of a number, not a character literal that hides
	// a comment opener ([lex.ppnumber]).
	write_text(scratch.path() / "src" / "core" / "planted_digits.cpp",
		"[[maybe_unused]] constexpr auto n = 1'000; // ' /*\n#include \"../ui/include/recto/ui/ui.h\"\n");
	// `//` inside a header name is part of the name ([lex.header]).
	write_text(scratch.path() / "src" / "core" / "planted_angle.cpp", "#include <../ui/include/recto//ui/ui.h>\n");
	write_text(scratch.path() / "src" / "services" / "planted_services.h",
		"// #include <recto/ui/ui.h> in a comment is not an include\n#  include \"recto/ui/theme.h\"\n");
	// An include fragment is scanned like any source file.
	write_text(scratch.path() / "src" / "services" / "planted_fragment.inc", "#import <recto/ui/ui.h>\n");
	// An R that ends a preprocessing number is not a raw-string prefix ([lex.pptoken]).
	write_text(scratch.path() / "src" / "core" / "planted_ppnum.cpp",
		"#define DROP(...)\nDROP(0.R\"(ordinary\")\n#include \"../ui/include/recto/ui/ui.h\"\n[[maybe_unused]] const char* b = \")\";\n");
	// A Unicode identifier ending in R (λR) is one identifier, not a raw-string prefix ([lex.name]).
	write_text(scratch.path() / "src" / "core" / "planted_unicode.cpp",
		"#define \xCE\xBBR\n[[maybe_unused]] const char* a = \xCE\xBBR\"(ordinary\";\n#include \"../ui/include/recto/ui/ui.h\"\n"
		"[[maybe_unused]] const char* b = \")\";\n");
	// A bare CR ends a line, and so a // comment ([lex.phases] phase 1).
	write_text(scratch.path() / "src" / "core" / "planted_cr.cpp", "// comment\r#include \"../ui/include/recto/ui/ui.h\"\r");
	// A linker directive links the other side's library with no project setting:
	// each spelling is caught, a computed argument is refused, and a system
	// library or a directive in a comment is not a crossing.
	write_text(scratch.path() / "src" / "core" / "planted_pragma.cpp",
		"#pragma comment(lib, \"windowscodecs.lib\")\n"
		"#pragma comment( lib , \"..\\\\..\\\\build\\\\bin\\\\x64\\\\Debug\\\\recto-ui\\\\Recto-UI.lib\" )\n"
		"__pragma(comment(lib, \"recto-ui\"))\n"
		"_Pragma(\"comment(lib, \\\"recto-ui.lib\\\")\")\n"
		"#pragma comment(linker, \"/DEFAULTLIB:recto-ui\")\n"
		"#define UI_LIB \"recto-ui.lib\"\n"
		"#pragma comment(lib, UI_LIB)\n"
		"// #pragma comment(lib, \"recto-ui.lib\")\n"
		// Escapes and adjacent literals are decoded as the compiler decodes them.
		"#pragma comment(lib, \"recto-\\165i.lib\")\n"
		"#pragma comment(lib, \"recto-\" \"\\x75i\")\n"
		// Directive text inside an ordinary string literal is data, not a directive.
		"[[maybe_unused]] constexpr const char* help = \"comment(lib, windowscodecs.lib)\";\n"
		"[[maybe_unused]] constexpr const char* text = \"#pragma comment(lib, \\\"recto-ui.lib\\\")\";\n"
		"#pragma comment(lib, \"recto-\\u0075i\")\n");
	write_text(scratch.path() / "src" / "ui" / "planted_pragma_ui.cpp",
		"#pragma comment(lib, \"recto-pdf.lib\")\n[[maybe_unused]] constexpr const char* help = \"comment(lib, windowscodecs.lib)\";\n");
	// The digraph spelling of #include ([lex.digraph]).
	write_text(scratch.path() / "src" / "services" / "planted_digraph.cpp", "%:include \"../ui/include/recto/ui/ui.h\"\n");
	// `//` inside the header name of a toolkit file that reaches an engine header.
	write_text(scratch.path() / "src" / "ui" / "planted_angle_ui.cpp", "#include <../../pdf//error.h>\n");
	// UTF-16 source with a byte order mark, which MSVC compiles.
	write_text(scratch.path() / "src" / "core" / "planted_utf16.cpp",
		std::string_view("\xFF\xFE#\0i\0n\0c\0l\0u\0d\0e\0 \0\"\0u\0i\0.\0h\0\"\0\n\0", 34));

	const std::vector<Finding> planted = boundary_findings(scratch.path(), names);
	std::set<std::string> caught;
	for (const Finding& finding : planted) {
		caught.insert(finding.file.filename().string() + ":" + std::to_string(finding.line));
	}
	const std::set<std::string> expected = {"planted_ui.cpp:4", "planted_ui.cpp:5", "planted_ui.cpp:6", "planted_ui.cpp:7",
		"planted_ui.cpp:8", "planted_ui.cpp:9", "planted_ui.cpp:10", "planted_ui.cpp:12", "planted_ui.cpp:14",
		"planted_ui.cpp:16", "planted_bom.cpp:1", "planted_splice.cpp:2", "planted_raw.cpp:4", "planted_lines.cpp:2",
		"planted_pdf.cpp:1", "planted_pdf.cpp:3", "planted_pdf.cpp:5", "planted_bare.cpp:1", "planted_bare.cpp:2",
		"planted_core.cpp:1", "planted_digits.cpp:2", "planted_angle.cpp:1", "planted_services.h:2",
		"planted_fragment.inc:1", "planted_digraph.cpp:1", "planted_angle_ui.cpp:1", "planted_utf16.cpp:1",
		"planted_ppnum.cpp:3", "planted_unicode.cpp:3", "planted_cr.cpp:2", "planted_pragma.cpp:2", "planted_pragma.cpp:3",
		"planted_pragma.cpp:4", "planted_pragma.cpp:5", "planted_pragma.cpp:7", "planted_pragma.cpp:9",
		"planted_pragma.cpp:10", "planted_pragma.cpp:13", "planted_pragma_ui.cpp:1"};
	EXPECT_EQ(caught, expected) << describe(planted);
}

RECTO_UI_TEST(Project, ScanRefusesUnreadableFile)
{
	// A file the scan cannot read fails the scan; it is never read as empty.
	recto::test::TempDir scratch;
	const fs::path locked = scratch.path() / "src" / "ui" / "locked.cpp";
	write_text(locked, "#include \"recto_pdf.h\"\n");
	const HANDLE handle = CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	ASSERT_NE(handle, INVALID_HANDLE_VALUE);
	const HeaderNames names = header_names(recto::test::repo_root());
	EXPECT_THROW({ const auto findings = boundary_findings(scratch.path(), names); (void)findings; }, std::runtime_error);
	CloseHandle(handle);
	EXPECT_EQ(boundary_findings(scratch.path(), names).size(), 1U);
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
	write_text(scratch.path() / "src" / "ui" / "planted_e.cpp", "constexpr auto n = 1'000; // ' /*\nwinrt::Microsoft::UI::Xaml::Window* w;\n");
	write_text(scratch.path() / "src" / "ui" / "planted_f.cpp", "// Microsoft::UI in a comment is not code\nint x;\n");
	write_text(scratch.path() / "src" / "ui" / "planted_g.cpp",
		std::string_view("\xFE\xFF\0M\0i\0c\0r\0o\0s\0o\0f\0t\0:\0:\0U\0I\0\n", 30));
	// A bare CR ends the // comment, so the next line is code.
	write_text(scratch.path() / "src" / "ui" / "planted_h.cpp", "// comment\rusing namespace Microsoft::UI;\r");
	const std::vector<Finding> planted = scan_tokens(scratch.path() / "src" / "ui", winrt_ui_tokens());
	std::set<std::string> lines;
	for (const Finding& finding : planted) {
		lines.insert(finding.file.filename().string() + ":" + std::to_string(finding.line));
	}
	EXPECT_EQ(lines, (std::set<std::string>{"planted_a.h:1", "planted_b.cpp:1", "planted_b.cpp:2", "planted_c.cpp:1",
						 "planted_d.cpp:1", "planted_e.cpp:2", "planted_g.cpp:1", "planted_h.cpp:2"}))
		<< describe(planted);
}

RECTO_UI_TEST(Project, ToolkitInstrumentedLikeTests)
{
	// The toolkit's own objects carry AddressSanitizer exactly when this test
	// binary does, and when the build says which (RECTO_EXPECT_ASAN), as it says.
#if defined(__SANITIZE_ADDRESS__)
	constexpr bool kTestsInstrumented = true;
#else
	constexpr bool kTestsInstrumented = false;
#endif
	EXPECT_EQ(recto::ui::built_with_address_sanitizer(), kTestsInstrumented);
	wchar_t expect[8]{};
	const DWORD length = GetEnvironmentVariableW(L"RECTO_EXPECT_ASAN", expect, 8);
	if (length == 1) {
		EXPECT_EQ(recto::ui::built_with_address_sanitizer(), expect[0] == L'1');
	}
}

RECTO_UI_TEST(Project, UniqueTestIds)
{
	// RECTO_UI_TEST registers file-local symbols, so two files could register the
	// same Ui.<Area>.<Name> without a link error; this catches it.
	std::vector<std::string> names;
	const ::testing::UnitTest& unit = *::testing::UnitTest::GetInstance();
	for (int s = 0; s < unit.total_test_suite_count(); ++s) {
		const ::testing::TestSuite& suite = *unit.GetTestSuite(s);
		for (int t = 0; t < suite.total_test_count(); ++t) {
			names.push_back(std::string(suite.name()) + "." + suite.GetTestInfo(t)->name());
		}
	}
	const std::set<std::string> duplicates = duplicate_names(names);
	EXPECT_TRUE(duplicates.empty()) << *duplicates.begin();
	EXPECT_EQ(duplicate_names({"Ui.Project.A", "Ui.Project.B", "Ui.Project.A"}), (std::set<std::string>{"Ui.Project.A"}));
	// The registration-time refusal: a second claim of the same name fails.
	std::set<std::string> seen;
	EXPECT_TRUE(recto::ui::test::claim_name(seen, "Project.A"));
	EXPECT_TRUE(recto::ui::test::claim_name(seen, "Project.B"));
	EXPECT_FALSE(recto::ui::test::claim_name(seen, "Project.A"));
}

// Registered for Ui.Project.DisabledTestsRunOnlyWhenAsked: never runs by default.
RECTO_UI_TEST(Project, DISABLED_Probe)
{
	SUCCEED();
}

RECTO_UI_TEST(Project, DisabledTestsRunOnlyWhenAsked)
{
	// A disabled test is Ui.DISABLED_<Area>.<Name> (ui_test.h): `Ui.<Area>.*`
	// never lists it, `Ui.DISABLED_<Area>.*` does, and it runs only with
	// --gtest_also_run_disabled_tests.
	const auto [area_code, area_list] = run_self(L"--gtest_list_tests --gtest_filter=Ui.Project.*");
	EXPECT_EQ(area_code, 0U);
	EXPECT_EQ(area_list.find("DISABLED_Project.Probe"), std::string::npos);
	const auto [disabled_code, disabled_list] = run_self(L"--gtest_list_tests --gtest_filter=Ui.DISABLED_Project.*");
	EXPECT_EQ(disabled_code, 0U);
	EXPECT_NE(disabled_list.find("DISABLED_Project.Probe"), std::string::npos);
	const auto [skip_code, skipped] = run_self(L"--gtest_filter=Ui.DISABLED_Project.Probe --gtest_fail_if_no_test_selected=false");
	EXPECT_EQ(skip_code, 0U);
	EXPECT_NE(skipped.find("DISABLED TEST"), std::string::npos) << skipped;
	EXPECT_EQ(skipped.find("[ RUN      ] Ui.DISABLED_Project.Probe"), std::string::npos) << skipped;
	const auto [run_code, ran] = run_self(L"--gtest_filter=Ui.DISABLED_Project.Probe --gtest_also_run_disabled_tests");
	EXPECT_EQ(run_code, 0U);
	EXPECT_NE(ran.find("[  PASSED  ] 1 test."), std::string::npos) << ran;
}

// Registered for Ui.Project.StalledChildIsReaped: a child that never finishes.
RECTO_UI_TEST(Project, DISABLED_Stall)
{
	Sleep(30000);
}

RECTO_UI_TEST(Project, StalledChildIsReaped)
{
	// A child test run that stalls is terminated at its deadline and the run
	// fails; nothing waits forever on its output.
	const auto started = std::chrono::steady_clock::now();
	EXPECT_THROW(
		{ const auto result = run_self(L"--gtest_filter=Ui.DISABLED_Project.Stall --gtest_also_run_disabled_tests", 1000); (void)result; },
		std::runtime_error);
	EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(10));
}

RECTO_UI_TEST(Project, ChildRunsIgnoreParentSharding)
{
	// A parent run on shard 1 of 2 still gets its probe's single test run.
	const ScopedVariable total(L"GTEST_TOTAL_SHARDS", L"2");
	const ScopedVariable index(L"gtest_shard_index", L"1");
	const auto [code, output] = run_self(L"--gtest_filter=Ui.Project.LibraryLinks");
	EXPECT_EQ(code, 0U) << output;
	EXPECT_NE(output.find("[  PASSED  ] 1 test."), std::string::npos) << output;
}

RECTO_UI_TEST(Project, EmptySelectionFails)
{
	// A checkpoint filter that matches nothing fails instead of passing with no
	// tests run (gtest_defaults.cpp); asking for the old behaviour still works.
	const auto [code, output] = run_self(L"--gtest_filter=Ui.Project.NoSuchTest");
	EXPECT_NE(code, 0U);
	EXPECT_NE(output.find("No tests ran"), std::string::npos) << output;
	const auto [permitted, ignored] = run_self(L"--gtest_filter=Ui.Project.NoSuchTest --gtest_fail_if_no_test_selected=false");
	EXPECT_EQ(permitted, 0U) << ignored;
	const auto [listed, listing] = run_self(L"--gtest_list_tests --gtest_filter=Ui.Project.NoSuchTest");
	EXPECT_EQ(listed, 0U) << listing;
	// Only the flag itself decides, never a filter that happens to contain its name.
	const auto [lookalike, lookalike_output] = run_self(L"--gtest_filter=Ui.Project.NoSuch_gtest_fail_if_no_test_selected");
	EXPECT_NE(lookalike, 0U) << lookalike_output;
	// Three quotes keep the C runtime inside one argument, so the "override" is
	// part of the filter GoogleTest reads, and the run must still fail.
	const auto [quoted, quoted_output] = run_self(L"--gtest_filter=Ui.Project.NoSuch\"\"\" --gtest_fail_if_no_test_selected=false");
	EXPECT_NE(quoted, 0U) << quoted_output;
	// GTEST_FAIL_IF_NO_TEST_SELECTED=0 in a run's own environment also permits the empty run...
	const auto [variable, variable_output] =
		run_self(L"--gtest_filter=Ui.Project.NoSuchTest", kChildDeadlineMs, {{L"GTEST_FAIL_IF_NO_TEST_SELECTED", L"0"}});
	EXPECT_EQ(variable, 0U) << variable_output;
	// ...but a parent's setting never reaches a probe, which keeps the default.
	const ScopedVariable parent(L"GTEST_FAIL_IF_NO_TEST_SELECTED", L"0");
	const auto [inherited, inherited_output] = run_self(L"--gtest_filter=Ui.Project.NoSuchTest");
	EXPECT_NE(inherited, 0U) << inherited_output;
}

RECTO_UI_TEST(Project, HarnessSolid)
{
	constexpr uint32_t kSize = 64;
	// Premultiplied RGBA of an opaque colour: 0x20 red, 0x80 green, 0xE0 blue.
	const std::array<uint8_t, 4> fill_rgba{0x20, 0x80, 0xE0, 0xFF};

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
			std::copy(fill_rgba.begin(), fill_rgba.end(), expected.begin() + (static_cast<size_t>(y) * kSize + x) * 4);
		}
	}

	const recto::ui::test::Bitmap bitmap = render(0x20 / 255.0F, 0x80 / 255.0F, 0xE0 / 255.0F);
	ASSERT_EQ(bitmap.width, kSize);
	ASSERT_EQ(bitmap.height, kSize);
	ASSERT_GE(bitmap.stride, static_cast<size_t>(kSize) * 4);
	const recto::test::PixelDiffResult same =
		recto::test::expect_rgba_equal(recto::ui::test::rgba_bytes(bitmap), expected, kSize, kSize, kSize * 4, kSize * 4, 0);
	EXPECT_TRUE(same.matches);

	// The test can fail: a different fill colour is a different image.
	const recto::ui::test::Bitmap other = render(0xE0 / 255.0F, 0x80 / 255.0F, 0x20 / 255.0F);
	const recto::test::PixelDiffResult different =
		recto::test::compare_rgba(recto::ui::test::rgba_bytes(other), expected, kSize, kSize, kSize * 4, kSize * 4, 0);
	EXPECT_FALSE(different.matches);
	EXPECT_EQ(different.first_x, 16U);
	EXPECT_EQ(different.first_y, 16U);
	// The diagnostic names the channels in RGBA order, so red reads as red.
	EXPECT_NE(recto::test::format_rgba_diff(different).find("actual [224, 128, 32, 255]"), std::string::npos) << recto::test::format_rgba_diff(different);
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
	// A non-square bitmap whose every row differs, red on the left and blue on
	// the right with a moving boundary: a reader that assumes stride == width * 4
	// sees rows shifted, and one that swaps red and blue sees other colours.
	constexpr uint32_t kWidth = 48;
	constexpr uint32_t kHeight = 20;
	constexpr size_t kPadding = 80;
	const auto draw = [](const recto::ui::test::RenderContext& context) {
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> red;
		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> blue;
		ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(1.0F, 0.0F, 0.0F, 1.0F), &red));
		ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(0.0F, 0.0F, 1.0F, 1.0F), &blue));
		context.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
		for (uint32_t y = 0; y < kHeight; ++y) {
			const float split = static_cast<float>(4 + y);
			const float top = static_cast<float>(y);
			context.target->FillRectangle(D2D1::RectF(0.0F, top, split, top + 1.0F), red.Get());
			context.target->FillRectangle(D2D1::RectF(split, top, static_cast<float>(kWidth), top + 1.0F), blue.Get());
		}
	};

	const recto::ui::test::Bitmap natural = recto::ui::test::render_to_bitmap(draw, kWidth, kHeight, 1.0F);
	recto::ui::test::RenderOptions options;
	options.min_stride = static_cast<size_t>(kWidth) * 4 + kPadding;
	const recto::ui::test::Bitmap padded = recto::ui::test::render_to_bitmap(draw, kWidth, kHeight, 1.0F, options);

	ASSERT_GT(padded.stride, static_cast<size_t>(kWidth) * 4) << "the harness must honour a wider stride";
	ASSERT_EQ(padded.bytes.size(), padded.stride * kHeight);

	// Read through the adapter, which honours each bitmap's own stride, the
	// images are equal and their first pixel is red in RGBA order.
	const std::vector<uint8_t> natural_rgba = recto::ui::test::rgba_bytes(natural);
	const std::vector<uint8_t> padded_rgba = recto::ui::test::rgba_bytes(padded);
	EXPECT_EQ(padded_rgba, natural_rgba);
	EXPECT_EQ(padded_rgba[0], 255);
	EXPECT_EQ(padded_rgba[2], 0);

	// Assuming stride == width * 4 does not: this is the cheaper substitute the
	// section forbids, and it must fail, in the adapter as in a direct reader.
	recto::ui::test::Bitmap assumed = padded;
	assumed.stride = static_cast<size_t>(kWidth) * 4;
	EXPECT_NE(recto::ui::test::rgba_bytes(assumed), natural_rgba);
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

RECTO_UI_TEST(Project, HarnessRefusesBeforeDrawing)
{
	// Every refused request throws before the callback runs.
	int calls = 0;
	const recto::ui::test::DrawRoot count = [&](const recto::ui::test::RenderContext&) { ++calls; };
	recto::ui::test::RenderOptions wide;
	wide.min_stride = (size_t{1} << 24) + 1;
	const float infinity = std::numeric_limits<float>::infinity();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	struct Request {
		uint32_t width;
		uint32_t height;
		float scale;
		recto::ui::test::RenderOptions options;
	};
	const std::array<Request, 8> refused = {{{0, 8, 1.0F, {}}, {8, 0, 1.0F, {}}, {16385, 8, 1.0F, {}}, {8, 8, infinity, {}},
		{8, 8, nan, {}}, {8, 8, 0.0F, {}}, {8, 8, 32.0F, {}}, {8, 8, 1.0F, wide}}};
	for (const Request& request : refused) {
		EXPECT_THROW(
			{ const auto bitmap = recto::ui::test::render_to_bitmap(count, request.width, request.height, request.scale, request.options); (void)bitmap; },
			std::runtime_error);
	}
	EXPECT_EQ(calls, 0);
	const recto::ui::test::Bitmap accepted = recto::ui::test::render_to_bitmap(count, 8, 8, 16.0F);
	EXPECT_EQ(calls, 1);
	EXPECT_EQ(accepted.width, 8U);
}

RECTO_UI_TEST(Project, HarnessReportsDrawFailure)
{
	// A failed EndDraw names its HRESULT, the tags of the failing call and the request.
	try {
		const auto bitmap = recto::ui::test::render_to_bitmap(
			[](const recto::ui::test::RenderContext& context) {
				context.target->SetTags(7, 9);
				context.target->PopAxisAlignedClip(); // no matching push: the target enters an error state
			},
			64, 32, 1.5F);
		(void)bitmap;
		ADD_FAILURE() << "an unmatched PopAxisAlignedClip must fail EndDraw";
	} catch (const std::runtime_error& error) {
		const std::string what = error.what();
		EXPECT_NE(what.find("EndDraw failed, HRESULT 0x"), std::string::npos) << what;
		EXPECT_NE(what.find("tags 7, 9"), std::string::npos) << what;
		EXPECT_NE(what.find("64 x 32 px at scale 1.5"), std::string::npos) << what;
	}
	// The harness recovers: the next render succeeds.
	EXPECT_EQ(recto::ui::test::render_to_bitmap({}, 4, 4, 1.0F).width, 4U);
}

RECTO_UI_TEST(Project, HarnessPassesCallbackExceptions)
{
	// The callback's own exception reaches the caller unchanged, and the next render works.
	try {
		const auto bitmap = recto::ui::test::render_to_bitmap(
			[](const recto::ui::test::RenderContext&) { throw std::logic_error("callback failed"); }, 8, 8, 1.0F);
		(void)bitmap;
		ADD_FAILURE() << "the callback's exception was swallowed";
	} catch (const std::logic_error& error) {
		EXPECT_STREQ(error.what(), "callback failed");
	}
	EXPECT_EQ(recto::ui::test::render_to_bitmap({}, 4, 4, 1.0F).height, 4U);
}

namespace {

struct Apartment {
	HRESULT hr{E_FAIL};
	APTTYPE type{APTTYPE_CURRENT};
	APTTYPEQUALIFIER qualifier{APTTYPEQUALIFIER_NONE};
	bool operator==(const Apartment&) const = default;
};

Apartment current_apartment()
{
	Apartment apartment;
	apartment.hr = CoGetApartmentType(&apartment.type, &apartment.qualifier);
	return apartment;
}

recto::ui::test::Bitmap render_split()
{
	return recto::ui::test::render_to_bitmap(
		[](const recto::ui::test::RenderContext& context) {
			Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
			if (SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(0.2F, 0.6F, 0.9F, 1.0F), &brush))) {
				context.target->FillRectangle(D2D1::RectF(3.0F, 5.0F, 27.0F, 19.0F), brush.Get());
			}
		},
		32, 24, 1.0F);
}

} // namespace

RECTO_UI_TEST(Project, HarnessKeepsCallerApartment)
{
	// A thread with no apartment, an STA caller and an MTA caller each leave the
	// render in the apartment they entered it with.
	const auto in_thread = [](DWORD init) {
		Apartment before;
		Apartment after;
		std::thread worker([&] {
			const HRESULT entered = init == 0xFFFFFFFF ? S_FALSE : CoInitializeEx(nullptr, init);
			before = current_apartment();
			const recto::ui::test::Bitmap bitmap = render_split();
			after = current_apartment();
			if (entered == S_OK) {
				CoUninitialize();
			}
			(void)bitmap;
		});
		worker.join();
		return std::pair{before, after};
	};
	for (const DWORD init : {DWORD{0xFFFFFFFF}, DWORD{COINIT_APARTMENTTHREADED}, DWORD{COINIT_MULTITHREADED}}) {
		const auto [before, after] = in_thread(init);
		EXPECT_EQ(before, after) << "init " << init;
	}
	const Apartment sta = in_thread(COINIT_APARTMENTTHREADED).first;
	// The first STA in a process is the main STA, whichever thread it is on.
	EXPECT_TRUE(sta.hr == S_OK && (sta.type == APTTYPE_STA || sta.type == APTTYPE_MAINSTA))
		<< "the STA case must run in an STA (hr " << sta.hr << ", type " << sta.type << ")";
}

RECTO_UI_TEST(Project, HarnessBalancesComReferences)
{
	// A caller that initialised COM twice, renders (once normally, once with a
	// throwing callback) and uninitialises twice is back to no apartment of its
	// own: the harness kept no reference, not even on the exception path.
	const auto fresh_state = [] {
		Apartment state;
		std::thread probe([&] { state = current_apartment(); });
		probe.join();
		return state;
	};
	for (const DWORD init : {DWORD{COINIT_MULTITHREADED}, DWORD{COINIT_APARTMENTTHREADED}}) {
		Apartment after;
		Apartment baseline;
		bool threw = false;
		std::thread worker([&] {
			const HRESULT first = CoInitializeEx(nullptr, init);
			const HRESULT second = CoInitializeEx(nullptr, init);
			const recto::ui::test::Bitmap bitmap = render_split();
			try {
				const auto failed = recto::ui::test::render_to_bitmap(
					[](const recto::ui::test::RenderContext&) { throw std::logic_error("unwind"); }, 8, 8, 1.0F);
				(void)failed;
			} catch (const std::logic_error& error) {
				threw = std::string_view(error.what()) == "unwind"; // the expected exception, checked below
			}
			if (SUCCEEDED(second)) {
				CoUninitialize();
			}
			if (SUCCEEDED(first)) {
				CoUninitialize();
			}
			after = current_apartment();
			baseline = fresh_state();
			(void)bitmap;
		});
		worker.join();
		EXPECT_TRUE(threw) << "the callback's exception did not reach the caller";
		EXPECT_EQ(after, baseline) << "init " << init << ": hr " << after.hr << ", type " << after.type;
	}
}

RECTO_UI_TEST(Project, HarnessRendersConcurrently)
{
	// Independent renders on four threads produce the same image as one alone.
	const recto::ui::test::Bitmap reference = render_split();
	std::array<recto::ui::test::Bitmap, 4> results;
	std::vector<std::thread> threads;
	for (size_t i = 0; i < results.size(); ++i) {
		threads.emplace_back([&results, i] { results[i] = render_split(); });
	}
	for (std::thread& thread : threads) {
		thread.join();
	}
	for (const recto::ui::test::Bitmap& result : results) {
		EXPECT_EQ(result.bytes, reference.bytes);
	}
}

RECTO_UI_TEST(Project, HarnessScaleMatrix)
{
	// A non-square bitmap at 125, 150 and 175 percent: the epx extents and every
	// pixel of an aliased epx rectangle follow the scale.
	for (const float scale : {1.25F, 1.5F, 1.75F}) {
		constexpr uint32_t kWidth = 96;
		constexpr uint32_t kHeight = 40;
		const recto::ui::test::Bitmap bitmap = recto::ui::test::render_to_bitmap(
			[&](const recto::ui::test::RenderContext& context) {
				EXPECT_FLOAT_EQ(context.width_epx, static_cast<float>(kWidth) / scale);
				EXPECT_FLOAT_EQ(context.height_epx, static_cast<float>(kHeight) / scale);
				Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
				ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(1.0F, 1.0F, 1.0F, 1.0F), &brush));
				context.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
				context.target->FillRectangle(D2D1::RectF(8.0F, 4.0F, 24.0F, 12.0F), brush.Get());
			},
			kWidth, kHeight, scale);
		size_t wrong = 0;
		for (uint32_t y = 0; y < kHeight; ++y) {
			for (uint32_t x = 0; x < kWidth; ++x) {
				const float cx = static_cast<float>(x) + 0.5F;
				const float cy = static_cast<float>(y) + 0.5F;
				const bool inside = cx >= 8.0F * scale && cx < 24.0F * scale && cy >= 4.0F * scale && cy < 12.0F * scale;
				const uint8_t alpha = bitmap.bytes[static_cast<size_t>(y) * bitmap.stride + static_cast<size_t>(x) * 4 + 3];
				wrong += (alpha == (inside ? 255 : 0)) ? 0 : 1;
			}
		}
		EXPECT_EQ(wrong, 0U) << "scale " << scale;
	}
}

RECTO_UI_TEST(Project, HarnessRgbaAdapter)
{
	// Half-transparent red, premultiplied: BGRA from the harness, RGBA for the
	// comparison helpers, and red and blue never trade places.
	const recto::ui::test::Bitmap bitmap = recto::ui::test::render_to_bitmap(
		[](const recto::ui::test::RenderContext& context) {
			Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
			ASSERT_HRESULT_SUCCEEDED(context.target->CreateSolidColorBrush(D2D1::ColorF(1.0F, 0.0F, 0.0F, 0.5F), &brush));
			context.target->FillRectangle(D2D1::RectF(0.0F, 0.0F, 4.0F, 4.0F), brush.Get());
		},
		4, 4, 1.0F);
	const std::vector<uint8_t> rgba = recto::ui::test::rgba_bytes(bitmap);
	ASSERT_EQ(rgba.size(), 4U * 4U * 4U);
	EXPECT_NEAR(rgba[0], 128, 1); // red, premultiplied by one half
	EXPECT_EQ(rgba[1], 0);
	EXPECT_EQ(rgba[2], 0);
	EXPECT_NEAR(rgba[3], 128, 1);
	EXPECT_EQ(bitmap.bytes[0], 0);       // BGRA: blue first
	EXPECT_EQ(bitmap.bytes[2], rgba[0]); // red third
	EXPECT_TRUE(recto::test::expect_rgba_equal(rgba, rgba, 4, 4, 16, 16, 0).matches);
	std::vector<uint8_t> blue = rgba;
	std::swap(blue[0], blue[2]);
	EXPECT_FALSE(recto::test::compare_rgba(rgba, blue, 4, 4, 16, 16, 0).matches);
	// A bitmap whose fields disagree is refused before any byte is read.
	EXPECT_THROW({ const auto r = recto::ui::test::rgba_bytes(recto::ui::test::Bitmap{1, 1, 4, {}}); (void)r; }, std::runtime_error);
	EXPECT_THROW({ const auto r = recto::ui::test::rgba_bytes(recto::ui::test::Bitmap{4, 4, 8, bitmap.bytes}); (void)r; }, std::runtime_error);
	recto::ui::test::Bitmap truncated = bitmap;
	truncated.bytes.resize(truncated.bytes.size() - 1);
	EXPECT_THROW({ const auto r = recto::ui::test::rgba_bytes(truncated); (void)r; }, std::runtime_error);
	EXPECT_TRUE(recto::ui::test::rgba_bytes(recto::ui::test::Bitmap{0, 4, 0, {}}).empty());
}

RECTO_UI_TEST(Project, DisabledMarkerStaysAtTheFront)
{
	EXPECT_EQ(recto::ui::test::registered_name("Project", "IncludeBoundary"), "Project.IncludeBoundary");
	// GoogleTest disables on a DISABLED_ prefix of the case name, so it must lead.
	EXPECT_EQ(recto::ui::test::registered_name("Project", "DISABLED_Probe"), "DISABLED_Project.Probe");
}
