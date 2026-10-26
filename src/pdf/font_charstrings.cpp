// Type 1 and CFF programs read into immutable data, and their glyphs computed per call (ISO 32000-1 sections 9.6.2 and
// 9.9; the Adobe Type 1 Font Format; Adobe Technical Notes 5176 and 5177; D01 T02 section 29). Every read is
// bounds-checked; a glyph's work is bounded by `kMaxGlyphCharstringWork` executed charstring bytes shared across its
// subroutine calls and `seac` components, `kMaxGlyphPathOperators` path operators and `kMaxCharstringSubrDepth` nested
// calls.
#include "font_charstrings.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <new>
#include <tuple>

namespace recto::pdf {

namespace {

#include "cff_data.inc"

// A font failure as font.cpp's are: its code, message and step, recorded once by the `LoadedFont` call or the loader
// that returns it.
[[nodiscard]] PdfError charstring_error(ErrorCode code, std::string_view message) noexcept
{
	PdfError error(code);
	error.message = PdfErrorMessage(message);
	error.decoder_step = DecoderStep::NativeCharstring;
	return error;
}

[[nodiscard]] PdfError damaged(std::string_view message) noexcept
{
	return charstring_error(ErrorCode::DamagedFormat, message);
}

// A bound passed: its reason, what was asked and the bound, as the glyf reader's refusals carry them.
[[nodiscard]] PdfError limited(std::string_view message, RefusalReason reason, size_t observed, size_t bound) noexcept
{
	PdfError error = charstring_error(ErrorCode::ResourceLimit, message);
	error.refusal_reason = reason;
	error.observed = observed;
	error.bound = bound;
	return error;
}

[[nodiscard]] PdfResult<double> finite_advance(double advance) noexcept
{
	if (!std::isfinite(advance)) {
		return damaged("Glyph advance is not a finite number in text space");
	}
	return advance;
}

// True for a value in [0, limit): false for NaN, which arithmetic on hostile operands can make.
[[nodiscard]] bool in_range(double value, double limit) noexcept
{
	return value >= 0.0 && value < limit;
}

[[nodiscard]] FontMatrix compose(const FontMatrix& inner, const FontMatrix& outer) noexcept
{
	// p' = outer(inner(p)).
	return FontMatrix{inner[0] * outer[0] + inner[1] * outer[2], inner[0] * outer[1] + inner[1] * outer[3],
		inner[2] * outer[0] + inner[3] * outer[2], inner[2] * outer[1] + inner[3] * outer[3],
		inner[4] * outer[0] + inner[5] * outer[2] + outer[4], inner[4] * outer[1] + inner[5] * outer[3] + outer[5]};
}

// ---------------------------------------------------------------------------------------------------------------------
// The outline a charstring draws, with the shared budgets
// ---------------------------------------------------------------------------------------------------------------------

struct Builder {
	FontMatrix matrix = kDefaultFontMatrix;
	double offset_x = 0.0; // an seac accent's placement, in charstring units
	double offset_y = 0.0;
	GlyphOutline outline{};
	bool open = false;
	size_t path_ops = 0;
	size_t work = 0;
	bool have_box = false;
	PdfRect box{};

	[[nodiscard]] PdfPoint place(double x, double y) const noexcept
	{
		const double px = x + offset_x;
		const double py = y + offset_y;
		return PdfPoint{matrix[0] * px + matrix[2] * py + matrix[4], matrix[1] * px + matrix[3] * py + matrix[5]};
	}
	void note(const PdfPoint& p) noexcept
	{
		if (!have_box) {
			box = PdfRect{p.x, p.y, p.x, p.y};
			have_box = true;
		} else {
			box.x_min = std::min(box.x_min, p.x);
			box.y_min = std::min(box.y_min, p.y);
			box.x_max = std::max(box.x_max, p.x);
			box.y_max = std::max(box.y_max, p.y);
		}
	}
	[[nodiscard]] PdfResult<void> count_op(std::initializer_list<double> coordinates) noexcept
	{
		if (++path_ops > kMaxGlyphPathOperators) {
			return limited("Glyph has more path operators than its limit", RefusalReason::PathOperators, path_ops,
				kMaxGlyphPathOperators);
		}
		for (const double c : coordinates) {
			if (!std::isfinite(c)) {
				return damaged("Glyph coordinate is not a finite number");
			}
		}
		return {};
	}
	[[nodiscard]] PdfResult<PdfPoint> placed(double x, double y) const noexcept
	{
		const PdfPoint p = place(x, y);
		if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
			return damaged("Glyph point is not a finite number in text space");
		}
		return p;
	}
	[[nodiscard]] PdfResult<void> spend(size_t bytes) noexcept
	{
		work += bytes;
		if (work > kMaxGlyphCharstringWork) {
			return limited("Glyph executes more charstring bytes than its limit", RefusalReason::CharstringWork, work,
				kMaxGlyphCharstringWork);
		}
		return {};
	}
	void close()
	{
		if (open) {
			outline.verbs.push_back(PathVerb::CloseSubpath);
			open = false;
		}
	}
	[[nodiscard]] PdfResult<void> move_to(double x, double y)
	{
		RECTO_PDF_RETURN_IF_ERROR(count_op({x, y}));
		RECTO_PDF_TRY(const PdfPoint p, placed(x, y));
		close();
		outline.verbs.push_back(PathVerb::MoveTo);
		outline.points.push_back(p);
		open = true;
		return {};
	}
	[[nodiscard]] PdfResult<void> line_to(double from_x, double from_y, double x, double y)
	{
		RECTO_PDF_RETURN_IF_ERROR(count_op({x, y}));
		if (!open) {
			RECTO_PDF_RETURN_IF_ERROR(move_to(from_x, from_y));
		}
		RECTO_PDF_TRY(const PdfPoint p, placed(x, y));
		outline.verbs.push_back(PathVerb::LineTo);
		outline.points.push_back(p);
		note(outline.points[outline.points.size() - 2u]);
		note(p);
		return {};
	}
	[[nodiscard]] PdfResult<void> curve_to(
		double from_x, double from_y, double x1, double y1, double x2, double y2, double x3, double y3)
	{
		RECTO_PDF_RETURN_IF_ERROR(count_op({x1, y1, x2, y2, x3, y3}));
		if (!open) {
			RECTO_PDF_RETURN_IF_ERROR(move_to(from_x, from_y));
		}
		RECTO_PDF_TRY(const PdfPoint p1, placed(x1, y1));
		RECTO_PDF_TRY(const PdfPoint p2, placed(x2, y2));
		RECTO_PDF_TRY(const PdfPoint p3, placed(x3, y3));
		note(outline.points.back());
		outline.verbs.push_back(PathVerb::CubicBezierTo);
		outline.points.push_back(p1);
		outline.points.push_back(p2);
		outline.points.push_back(p3);
		note(p1);
		note(p2);
		note(p3);
		return {};
	}
};

// ---------------------------------------------------------------------------------------------------------------------
// Type 1 programs
// ---------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool is_space(uint8_t c) noexcept
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0u;
}

[[nodiscard]] bool is_hex(uint8_t c) noexcept
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

[[nodiscard]] uint8_t hex_value(uint8_t c) noexcept
{
	return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 0x20u) - 'a' + 10);
}

// Decryption of the `eexec` part and of charstrings (Type 1 Font Format, section 7).
[[nodiscard]] std::vector<uint8_t> decrypt(std::span<const uint8_t> cipher, uint16_t key, size_t skip)
{
	std::vector<uint8_t> plain;
	plain.reserve(cipher.size() > skip ? cipher.size() - skip : 0u);
	uint16_t r = key;
	for (size_t i = 0; i < cipher.size(); ++i) {
		const uint8_t c = cipher[i];
		const auto p = static_cast<uint8_t>(c ^ (r >> 8u));
		r = static_cast<uint16_t>((c + r) * 52845u + 22719u);
		if (i >= skip) {
			plain.push_back(p);
		}
	}
	return plain;
}

[[nodiscard]] size_t find_token(std::span<const uint8_t> bytes, std::string_view token, size_t from = 0) noexcept
{
	if (token.size() > bytes.size()) {
		return std::string_view::npos;
	}
	const auto* begin = reinterpret_cast<const char*>(bytes.data());
	const std::string_view text(begin, bytes.size());
	return text.find(token, from);
}

// A Type 1 program's cleartext and its decrypted `eexec` part.
struct Type1Parts {
	std::vector<uint8_t> clear;
	std::vector<uint8_t> secret;
};

[[nodiscard]] PdfResult<Type1Parts> split_type1(std::span<const uint8_t> bytes, Type1Lengths lengths)
{
	Type1Parts parts;
	std::vector<uint8_t> cipher;
	const bool pfb = !bytes.empty() && bytes[0] == 0x80u;
	if (pfb) {
		// PFB segments: 0x80, a type (1 text, 2 binary, 3 end) and a little-endian length.
		size_t at = 0;
		while (at < bytes.size()) {
			if (bytes.size() - at < 2u || bytes[at] != 0x80u) {
				return damaged("Type 1 PFB segment header is malformed");
			}
			const uint8_t type = bytes[at + 1u];
			if (type == 3u) {
				break;
			}
			if (bytes.size() - at < 6u) {
				return damaged("Type 1 PFB segment header is truncated");
			}
			const uint32_t length = static_cast<uint32_t>(bytes[at + 2u]) | (static_cast<uint32_t>(bytes[at + 3u]) << 8u) |
				(static_cast<uint32_t>(bytes[at + 4u]) << 16u) | (static_cast<uint32_t>(bytes[at + 5u]) << 24u);
			at += 6u;
			if (length > bytes.size() - at || (type != 1u && type != 2u)) {
				return damaged("Type 1 PFB segment runs past the program");
			}
			// Text before the binary part is the cleartext; text after it is the trailer of zeros, not read.
			if (type == 2u || cipher.empty()) {
				auto& target = type == 1u ? parts.clear : cipher;
				target.insert(target.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at),
					bytes.begin() + static_cast<std::ptrdiff_t>(at + length));
			}
			at += length;
		}
	} else {
		size_t clear_end = lengths.length1;
		// The stream's lengths are used only when /Length1 ends a cleartext holding `eexec`; a wrong /Length1 makes
		// /Length2 suspect too, so both are then found from the program.
		bool trusted = true;
		if (clear_end == 0u || clear_end > bytes.size() ||
			find_token(bytes.first(clear_end), "eexec") == std::string_view::npos) {
			trusted = false;
			const size_t eexec = find_token(bytes, "eexec");
			if (eexec == std::string_view::npos) {
				return damaged("Type 1 program has no eexec part");
			}
			clear_end = eexec + 5u;
		}
		while (clear_end < bytes.size() &&
			   (bytes[clear_end] == '\r' || bytes[clear_end] == '\n' || bytes[clear_end] == ' ' || bytes[clear_end] == '\t')) {
			++clear_end;
		}
		parts.clear.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(clear_end));
		size_t secret_end = bytes.size();
		if (trusted && lengths.length2 != 0u && lengths.length2 <= bytes.size() - clear_end) {
			secret_end = clear_end + lengths.length2;
		}
		cipher.assign(bytes.begin() + static_cast<std::ptrdiff_t>(clear_end),
			bytes.begin() + static_cast<std::ptrdiff_t>(secret_end));
	}
	// Hexadecimal when the first four bytes are hex digits (Type 1 Font Format, section 7.2); a PFB's binary segment is
	// binary by its type.
	size_t first = 0;
	while (first < cipher.size() && is_space(cipher[first])) {
		++first;
	}
	const bool hex = !pfb && cipher.size() - first >= 4u && std::all_of(cipher.begin() + static_cast<std::ptrdiff_t>(first),
														 cipher.begin() + static_cast<std::ptrdiff_t>(first + 4u), is_hex);
	if (hex) {
		std::vector<uint8_t> binary;
		binary.reserve(cipher.size() / 2u);
		int high = -1;
		for (size_t i = first; i < cipher.size(); ++i) {
			if (is_space(cipher[i])) {
				continue;
			}
			if (!is_hex(cipher[i])) {
				break; // the zeros' cleartext `cleartomark` part, or garbage after the hex
			}
			if (high < 0) {
				high = hex_value(cipher[i]);
			} else {
				binary.push_back(static_cast<uint8_t>((high << 4) | hex_value(cipher[i])));
				high = -1;
			}
		}
		cipher = std::move(binary);
	}
	if (cipher.size() < 4u) {
		return damaged("Type 1 eexec part is shorter than its four random bytes");
	}
	parts.secret = decrypt(cipher, 55665u, 4u);
	return parts;
}

// PostScript-like tokens over a Type 1 program part, skipping the binary data a `RD` token introduces.
struct Type1Tokens {
	std::span<const uint8_t> bytes;
	size_t at = 0;

	[[nodiscard]] std::string_view next() noexcept
	{
		while (at < bytes.size()) {
			if (is_space(bytes[at])) {
				++at;
			} else if (bytes[at] == '%') {
				while (at < bytes.size() && bytes[at] != '\r' && bytes[at] != '\n') {
					++at;
				}
			} else {
				break;
			}
		}
		if (at >= bytes.size()) {
			return {};
		}
		const size_t start = at;
		const uint8_t c = bytes[at];
		if (c == '[' || c == ']' || c == '{' || c == '}') {
			++at;
		} else {
			if (c == '/') {
				++at;
			}
			while (at < bytes.size() && !is_space(bytes[at]) && bytes[at] != '[' && bytes[at] != ']' &&
				   bytes[at] != '{' && bytes[at] != '}' && bytes[at] != '/' && bytes[at] != '%') {
				++at;
			}
		}
		return std::string_view(reinterpret_cast<const char*>(bytes.data() + start), at - start);
	}
};

[[nodiscard]] std::optional<int64_t> int_of(std::string_view token) noexcept
{
	int64_t value = 0;
	const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
	if (ec != std::errc{} || end != token.data() + token.size()) {
		return std::nullopt;
	}
	return value;
}

[[nodiscard]] std::optional<double> real_of(std::string_view token) noexcept
{
	double value = 0.0;
	const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
	if (ec != std::errc{} || end != token.data() + token.size() || !std::isfinite(value)) {
		return std::nullopt;
	}
	return value;
}

// The binary data after `n RD `: the single space after the token, then `n` bytes.
[[nodiscard]] PdfResult<std::span<const uint8_t>> rd_data(Type1Tokens& tokens, int64_t length)
{
	if (length < 0) {
		return damaged("Type 1 charstring length is negative");
	}
	if (static_cast<uint64_t>(length) > kMaxCharstringBytes) {
		return limited("Type 1 charstring is longer than its limit", RefusalReason::CharstringBytes,
			static_cast<size_t>(std::min<uint64_t>(static_cast<uint64_t>(length), SIZE_MAX)), kMaxCharstringBytes);
	}
	if (tokens.at >= tokens.bytes.size()) {
		return damaged("Type 1 charstring data is cut short");
	}
	++tokens.at; // the one space after RD
	if (static_cast<uint64_t>(length) > tokens.bytes.size() - tokens.at) {
		return damaged("Type 1 charstring data runs past the program");
	}
	const auto data = tokens.bytes.subspan(tokens.at, static_cast<size_t>(length));
	tokens.at += static_cast<size_t>(length);
	return data;
}

[[nodiscard]] PdfResult<std::vector<uint8_t>> charstring_plain(std::span<const uint8_t> data, int64_t len_iv)
{
	if (len_iv < 0) {
		return std::vector<uint8_t>(data.begin(), data.end());
	}
	if (static_cast<uint64_t>(len_iv) > data.size()) {
		return damaged("Type 1 charstring is shorter than its lenIV");
	}
	return decrypt(data, 4330u, static_cast<size_t>(len_iv));
}

[[nodiscard]] PdfResult<std::unique_ptr<LoadedFont::CharstringProgram>> parse_type1(
	std::span<const uint8_t> bytes, Type1Lengths lengths)
{
	RECTO_PDF_TRY(Type1Parts parts, split_type1(bytes, lengths));
	auto program = std::make_unique<LoadedFont::CharstringProgram>();
	program->kind = LoadedFont::CharstringProgram::Kind::Type1;

	// The cleartext: /FontMatrix and /Encoding.
	{
		Type1Tokens tokens{parts.clear};
		for (std::string_view token = tokens.next(); !token.empty(); token = tokens.next()) {
			if (token == "/FontMatrix") {
				const std::string_view open = tokens.next();
				if (open != "[" && open != "{") {
					continue;
				}
				FontMatrix matrix{};
				bool ok = true;
				for (double& v : matrix) {
					const std::optional<double> n = real_of(tokens.next());
					ok = ok && n.has_value();
					v = n.value_or(0.0);
				}
				if (ok && (matrix[0] * matrix[3] - matrix[1] * matrix[2]) != 0.0) {
					program->type1_matrix = matrix;
				}
			} else if (token == "/Encoding") {
				const std::string_view kind = tokens.next();
				GlyphNames names{};
				if (kind == "StandardEncoding") {
					for (size_t code = 0; code < 256u; ++code) {
						names[code] = base_encoding_name(BaseEncoding::Standard, static_cast<uint8_t>(code));
					}
					program->built_in = std::move(names);
					continue;
				}
				if (!int_of(kind).has_value()) {
					continue;
				}
				// `256 array`, then `dup <code> /<name> put`, until `def`.
				for (std::string_view t = tokens.next(); !t.empty() && t != "def" && t != "readonly"; t = tokens.next()) {
					if (t != "dup") {
						continue;
					}
					const std::optional<int64_t> code = int_of(tokens.next());
					const std::string_view name = tokens.next();
					if (code.has_value() && *code >= 0 && *code <= 255 && name.size() > 1u && name[0] == '/' &&
						name.size() <= kMaxGlyphNameBytes + 1u) {
						names[static_cast<size_t>(*code)] = std::string(name.substr(1u));
					}
				}
				program->built_in = std::move(names);
			}
		}
	}

	// The private part: /lenIV, /Subrs and /CharStrings.
	int64_t len_iv = 4;
	bool saw_subrs = false;
	std::vector<std::pair<std::string, std::vector<uint8_t>>> glyphs;
	Type1Tokens tokens{parts.secret};
	for (std::string_view token = tokens.next(); !token.empty(); token = tokens.next()) {
		if (token == "/lenIV") {
			len_iv = int_of(tokens.next()).value_or(4);
		} else if (token == "/Subrs") {
			const std::optional<int64_t> count = int_of(tokens.next());
			if (!count.has_value() || *count < 0 || *count > 65'535) {
				return damaged("Type 1 /Subrs count is malformed");
			}
			program->type1_subrs.assign(static_cast<size_t>(*count), {});
			saw_subrs = true;
			int64_t read = 0;
			while (read < *count) {
				const size_t mark = tokens.at;
				const std::string_view t = tokens.next();
				if (t.empty()) {
					break;
				}
				if (t == "/CharStrings") {
					tokens.at = mark;
					break;
				}
				if (t != "dup") {
					continue;
				}
				const std::optional<int64_t> index = int_of(tokens.next());
				const std::optional<int64_t> length = int_of(tokens.next());
				(void)tokens.next(); // RD or -|
				if (!index.has_value() || !length.has_value() || *index < 0 || *index >= *count) {
					return damaged("Type 1 subroutine entry is malformed");
				}
				RECTO_PDF_TRY(const std::span<const uint8_t> data, rd_data(tokens, *length));
				RECTO_PDF_TRY(program->type1_subrs[static_cast<size_t>(*index)], charstring_plain(data, len_iv));
				++read;
			}
		} else if (token == "/CharStrings" && glyphs.empty()) {
			const std::optional<int64_t> count = int_of(tokens.next());
			if (!count.has_value() || *count < 1 || *count > 65'535) {
				return damaged("Type 1 /CharStrings count is malformed");
			}
			glyphs.reserve(static_cast<size_t>(*count));
			while (static_cast<int64_t>(glyphs.size()) < *count) {
				const std::string_view t = tokens.next();
				if (t.empty() || t == "end") {
					break;
				}
				if (t.size() < 2u || t[0] != '/') {
					continue;
				}
				const std::optional<int64_t> length = int_of(tokens.next());
				(void)tokens.next(); // RD or -|
				if (!length.has_value()) {
					return damaged("Type 1 charstring entry is malformed");
				}
				RECTO_PDF_TRY(const std::span<const uint8_t> data, rd_data(tokens, *length));
				RECTO_PDF_TRY(std::vector<uint8_t> plain, charstring_plain(data, len_iv));
				glyphs.emplace_back(std::string(t.substr(1u)), std::move(plain));
			}
			if (saw_subrs) {
				break; // what follows is the font's closing code and, without /Length2, the trailer read as cipher
			}
		}
	}
	if (glyphs.empty()) {
		return damaged("Type 1 program has no charstrings");
	}
	// .notdef is glyph 0, as PDF fonts expect.
	const auto notdef = std::find_if(glyphs.begin(), glyphs.end(), [](const auto& g) { return g.first == ".notdef"; });
	if (notdef != glyphs.end() && notdef != glyphs.begin()) {
		std::iter_swap(glyphs.begin(), notdef);
	}
	program->glyph_count = static_cast<uint16_t>(glyphs.size());
	for (size_t gid = 0; gid < glyphs.size(); ++gid) {
		if (glyphs[gid].first.size() <= kMaxGlyphNameBytes) {
			program->names.emplace_back(glyphs[gid].first, static_cast<uint16_t>(gid));
		}
		program->type1_glyphs.push_back(std::move(glyphs[gid].second));
	}
	std::stable_sort(program->names.begin(), program->names.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	return program;
}

// ---------------------------------------------------------------------------------------------------------------------
// Type 1 charstrings (Type 1 Font Format, chapter 6)
// ---------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::optional<uint16_t> glyph_named(const LoadedFont::CharstringProgram& program, std::string_view name) noexcept
{
	const auto it = std::lower_bound(program.names.begin(), program.names.end(), name,
		[](const auto& entry, std::string_view key) { return entry.first < key; });
	if (it == program.names.end() || it->first != name) {
		return std::nullopt;
	}
	return it->second;
}

struct Type1Run {
	const LoadedFont::CharstringProgram& program;
	Builder& out;
	std::vector<double> stack;
	std::vector<double> ps; // the PostScript stack callothersubr leaves for pop
	double x = 0.0;
	double y = 0.0;
	double sbx = 0.0;
	double width = 0.0;
	double width_y = 0.0; // sbw's vertical width; hsbw's is 0
	bool width_set = false;
	bool flexing = false;
	std::vector<std::pair<double, double>> flex;
	bool after_flex = false;
	bool component = false; // drawing an seac component: no nested seac
	bool ended = false;
	struct Seac {
		double asb;
		double adx;
		double ady;
		double bchar;
		double achar;
	};
	std::optional<Seac> seac;

	[[nodiscard]] PdfResult<void> push(double v)
	{
		if (stack.size() >= kMaxType1Stack) {
			return limited("Type 1 charstring overflows its operand stack", RefusalReason::CharstringStack,
				stack.size() + 1u, kMaxType1Stack);
		}
		if (!std::isfinite(v)) {
			return damaged("Type 1 charstring arithmetic gave a number that is not finite");
		}
		stack.push_back(v);
		return {};
	}
	[[nodiscard]] PdfResult<double> pop()
	{
		if (stack.empty()) {
			return damaged("Type 1 charstring underflows its operand stack");
		}
		const double v = stack.back();
		stack.pop_back();
		return v;
	}
	[[nodiscard]] PdfResult<void> exact(size_t n) const
	{
		if (stack.size() != n) {
			return damaged("Type 1 charstring operator has the wrong number of operands");
		}
		return {};
	}
	[[nodiscard]] PdfResult<void> need(size_t n) const
	{
		if (stack.size() < n) {
			return damaged("Type 1 charstring operator lacks its operands");
		}
		return {};
	}
	[[nodiscard]] PdfResult<void> move(double dx, double dy)
	{
		x += dx;
		y += dy;
		if (flexing) {
			return {}; // a flex point: recorded by callothersubr 2, not drawn
		}
		return out.move_to(x, y);
	}
	[[nodiscard]] PdfResult<void> line(double dx, double dy)
	{
		const double fx = x;
		const double fy = y;
		x += dx;
		y += dy;
		return out.line_to(fx, fy, x, y);
	}
	[[nodiscard]] PdfResult<void> curve(double dx1, double dy1, double dx2, double dy2, double dx3, double dy3)
	{
		const double fx = x;
		const double fy = y;
		const double x1 = x + dx1;
		const double y1 = y + dy1;
		const double x2 = x1 + dx2;
		const double y2 = y1 + dy2;
		x = x2 + dx3;
		y = y2 + dy3;
		return out.curve_to(fx, fy, x1, y1, x2, y2, x, y);
	}

	[[nodiscard]] PdfResult<void> run(std::span<const uint8_t> code, size_t depth)
	{
		if (depth > kMaxCharstringSubrDepth) {
			return limited("Type 1 charstring nests subroutines past its limit", RefusalReason::SubroutineDepth, depth,
				kMaxCharstringSubrDepth);
		}
		size_t i = 0;
		while (i < code.size() && !ended) {
			const size_t start = i;
			const uint8_t v = code[i++];
			RECTO_PDF_RETURN_IF_ERROR(out.spend(1u));
			if (v >= 32u) {
				double n = 0.0;
				if (v <= 246u) {
					n = static_cast<double>(v) - 139.0;
				} else if (v <= 254u) {
					if (i >= code.size()) {
						return damaged("Type 1 charstring number is cut short");
					}
					const uint8_t w = code[i++];
					n = v <= 250u ? (v - 247.0) * 256.0 + w + 108.0 : -(v - 251.0) * 256.0 - w - 108.0;
				} else {
					if (code.size() - i < 4u) {
						return damaged("Type 1 charstring number is cut short");
					}
					const auto u = (static_cast<uint32_t>(code[i]) << 24u) | (static_cast<uint32_t>(code[i + 1u]) << 16u) |
						(static_cast<uint32_t>(code[i + 2u]) << 8u) | code[i + 3u];
					i += 4u;
					n = static_cast<double>(static_cast<int32_t>(u));
				}
				RECTO_PDF_RETURN_IF_ERROR(out.spend(i - start - 1u));
				RECTO_PDF_RETURN_IF_ERROR(push(n));
				continue;
			}
			switch (v) {
			case 1u: // hstem
			case 3u: // vstem
				RECTO_PDF_RETURN_IF_ERROR(exact(2u));
				stack.clear();
				break;
			case 4u: // vmoveto
				RECTO_PDF_RETURN_IF_ERROR(exact(1u));
				RECTO_PDF_RETURN_IF_ERROR(move(0.0, stack[0]));
				stack.clear();
				break;
			case 5u: // rlineto
				RECTO_PDF_RETURN_IF_ERROR(exact(2u));
				RECTO_PDF_RETURN_IF_ERROR(line(stack[0], stack[1]));
				stack.clear();
				break;
			case 6u: // hlineto
				RECTO_PDF_RETURN_IF_ERROR(exact(1u));
				RECTO_PDF_RETURN_IF_ERROR(line(stack[0], 0.0));
				stack.clear();
				break;
			case 7u: // vlineto
				RECTO_PDF_RETURN_IF_ERROR(exact(1u));
				RECTO_PDF_RETURN_IF_ERROR(line(0.0, stack[0]));
				stack.clear();
				break;
			case 8u: // rrcurveto
				RECTO_PDF_RETURN_IF_ERROR(exact(6u));
				RECTO_PDF_RETURN_IF_ERROR(curve(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]));
				stack.clear();
				break;
			case 9u: // closepath
				RECTO_PDF_RETURN_IF_ERROR(exact(0u));
				out.close();
				stack.clear();
				break;
			case 10u: { // callsubr
				RECTO_PDF_TRY(const double index, pop());
				if (!in_range(index, static_cast<double>(program.type1_subrs.size()))) {
					return damaged("Type 1 callsubr names a subroutine the font does not have");
				}
				RECTO_PDF_RETURN_IF_ERROR(run(program.type1_subrs[static_cast<size_t>(index)], depth + 1u));
				break;
			}
			case 11u: // return
				if (depth == 0u) {
					return damaged("Type 1 charstring returns from no subroutine");
				}
				return {};
			case 13u: // hsbw
				RECTO_PDF_RETURN_IF_ERROR(exact(2u));
				sbx = stack[0];
				x = stack[0];
				y = 0.0;
				if (!width_set) {
					width = stack[1];
					width_set = true;
				}
				ended = ended || width_only;
				stack.clear();
				break;
			case 14u: // endchar
				RECTO_PDF_RETURN_IF_ERROR(exact(0u));
				out.close();
				ended = true;
				break;
			case 21u: // rmoveto
				RECTO_PDF_RETURN_IF_ERROR(exact(2u));
				RECTO_PDF_RETURN_IF_ERROR(move(stack[0], stack[1]));
				stack.clear();
				break;
			case 22u: // hmoveto
				RECTO_PDF_RETURN_IF_ERROR(exact(1u));
				RECTO_PDF_RETURN_IF_ERROR(move(stack[0], 0.0));
				stack.clear();
				break;
			case 30u: // vhcurveto
				RECTO_PDF_RETURN_IF_ERROR(exact(4u));
				RECTO_PDF_RETURN_IF_ERROR(curve(0.0, stack[0], stack[1], stack[2], stack[3], 0.0));
				stack.clear();
				break;
			case 31u: // hvcurveto
				RECTO_PDF_RETURN_IF_ERROR(exact(4u));
				RECTO_PDF_RETURN_IF_ERROR(curve(stack[0], 0.0, stack[1], stack[2], 0.0, stack[3]));
				stack.clear();
				break;
			case 12u: {
				if (i >= code.size()) {
					return damaged("Type 1 charstring escape is cut short");
				}
				RECTO_PDF_RETURN_IF_ERROR(out.spend(1u));
				const uint8_t e = code[i++];
				switch (e) {
				case 0u: // dotsection
					RECTO_PDF_RETURN_IF_ERROR(exact(0u));
					break;
				case 1u: // vstem3
				case 2u: // hstem3
					RECTO_PDF_RETURN_IF_ERROR(exact(6u));
					stack.clear();
					break;
				case 6u: // seac
					RECTO_PDF_RETURN_IF_ERROR(exact(5u));
					if (component) {
						return damaged("Type 1 seac component is itself an seac");
					}
					seac = Seac{stack[0], stack[1], stack[2], stack[3], stack[4]};
					stack.clear();
					ended = true;
					break;
				case 7u: // sbw
					RECTO_PDF_RETURN_IF_ERROR(exact(4u));
					sbx = stack[0];
					x = stack[0];
					y = stack[1];
					if (!width_set) {
						width = stack[2];
						width_y = stack[3];
						width_set = true;
					}
					ended = ended || width_only;
					stack.clear();
					break;
				case 12u: { // div
					RECTO_PDF_TRY(const double b, pop());
					RECTO_PDF_TRY(const double a, pop());
					if (b == 0.0) {
						return damaged("Type 1 charstring divides by zero");
					}
					RECTO_PDF_RETURN_IF_ERROR(push(a / b));
					break;
				}
				case 16u: { // callothersubr
					RECTO_PDF_TRY(const double number, pop());
					RECTO_PDF_TRY(const double count_value, pop());
					if (!in_range(count_value, static_cast<double>(stack.size()) + 1.0)) {
						return damaged("Type 1 callothersubr has fewer arguments than it names");
					}
					const auto count = static_cast<size_t>(count_value);
					std::vector<double> args(stack.end() - static_cast<std::ptrdiff_t>(count), stack.end());
					stack.resize(stack.size() - count);
					ps.clear();
					if (number == 1.0) { // start of flex
						flexing = true;
						flex.clear();
						flex_start_x = x;
						flex_start_y = y;
					} else if (number == 2.0) { // a flex point
						if (!flexing) {
							return damaged("Type 1 flex point outside a flex");
						}
						flex.emplace_back(x, y);
					} else if (number == 0.0) { // end of flex: two curves through the seven points
						if (!flexing || flex.size() != 7u || count != 3u) {
							return damaged("Type 1 flex does not end with its seven points");
						}
						flexing = false;
						RECTO_PDF_RETURN_IF_ERROR(out.curve_to(flex_start_x, flex_start_y, flex[1].first, flex[1].second, flex[2].first,
							flex[2].second, flex[3].first, flex[3].second));
						RECTO_PDF_RETURN_IF_ERROR(out.curve_to(flex[3].first, flex[3].second, flex[4].first, flex[4].second,
							flex[5].first, flex[5].second, flex[6].first, flex[6].second));
						x = flex[6].first;
						y = flex[6].second;
						after_flex = true;
						ps = {args[2], args[1]}; // the end point, for `pop pop setcurrentpoint`
					} else if (number == 3.0) { // hint replacement: the subroutine number for `pop callsubr`
						if (count != 1u) {
							return damaged("Type 1 hint replacement does not take one subroutine number");
						}
						ps = {args[0]};
					} else {
						for (auto it = args.rbegin(); it != args.rend(); ++it) {
							ps.push_back(*it); // an unknown OtherSubr hands its arguments back
						}
					}
					break;
				}
				case 17u: // pop
					if (ps.empty()) {
						return damaged("Type 1 pop has no value from callothersubr to take");
					}
					RECTO_PDF_RETURN_IF_ERROR(push(ps.back()));
					ps.pop_back();
					break;
				case 33u: // setcurrentpoint
					RECTO_PDF_RETURN_IF_ERROR(exact(2u));
					if (!after_flex) {
						x = stack[0];
						y = stack[1];
					}
					after_flex = false;
					stack.clear();
					break;
				default:
					return damaged("Type 1 charstring has an unknown escape operator");
				}
				break;
			}
			default:
				return damaged("Type 1 charstring has an unknown operator");
			}
		}
		if (!ended) {
			return damaged(depth == 0u ? "Type 1 charstring ends without endchar" : "Type 1 subroutine ends without return");
		}
		return {};
	}

	double flex_start_x = 0.0;
	double flex_start_y = 0.0;
	bool width_only = false; // stop once `hsbw` or `sbw` gives the width
};

// The horizontal advance in text space: the width vector (wx, wy) through the font matrix (Type 1 Font Format 6.4).
[[nodiscard]] double type1_advance_x(const LoadedFont::CharstringProgram& program, const Type1Run& run) noexcept
{
	return program.type1_matrix[0] * run.width + program.type1_matrix[2] * run.width_y;
}

[[nodiscard]] PdfResult<GlyphShape> type1_shape(const LoadedFont::CharstringProgram& program, uint16_t glyph)
{
	Builder out;
	out.matrix = program.type1_matrix;
	Type1Run run{program, out};
	RECTO_PDF_RETURN_IF_ERROR(run.run(program.type1_glyphs[glyph], 0u));
	out.close();
	if (run.seac.has_value()) {
		const Type1Run::Seac s = *run.seac;
		if (!in_range(s.bchar, 256.0) || !in_range(s.achar, 256.0)) {
			return damaged("Type 1 seac names a code outside StandardEncoding");
		}
		const auto base = glyph_named(program, base_encoding_name(BaseEncoding::Standard, static_cast<uint8_t>(s.bchar)));
		const auto accent = glyph_named(program, base_encoding_name(BaseEncoding::Standard, static_cast<uint8_t>(s.achar)));
		if (!base.has_value() || !accent.has_value()) {
			return damaged("Type 1 seac names a glyph the font does not have");
		}
		Type1Run base_run{program, out};
		base_run.component = true;
		base_run.width_set = true;
		RECTO_PDF_RETURN_IF_ERROR(base_run.run(program.type1_glyphs[*base], 0u));
		out.close();
		// The accent at the composite's side bearing plus `adx - asb` (with its own `hsbw` giving `asb`), as rasterizers
		// place it.
		out.offset_x = run.sbx + s.adx - s.asb;
		out.offset_y = s.ady;
		Type1Run accent_run{program, out};
		accent_run.component = true;
		accent_run.width_set = true;
		RECTO_PDF_RETURN_IF_ERROR(accent_run.run(program.type1_glyphs[*accent], 0u));
		out.close();
	}
	GlyphShape shape;
	RECTO_PDF_TRY(shape.advance, finite_advance(type1_advance_x(program, run)));
	if (out.have_box) {
		shape.ink_box = out.box;
	}
	shape.outline = std::move(out.outline);
	return shape;
}

// ---------------------------------------------------------------------------------------------------------------------
// CFF programs (Technical Note 5176)
// ---------------------------------------------------------------------------------------------------------------------

struct CffReader {
	std::span<const uint8_t> bytes;

	[[nodiscard]] bool fits(size_t at, size_t count) const noexcept
	{
		return at <= bytes.size() && count <= bytes.size() - at;
	}
	[[nodiscard]] uint32_t uint(size_t at, size_t size) const noexcept
	{
		uint32_t v = 0;
		for (size_t i = 0; i < size; ++i) {
			v = (v << 8u) | bytes[at + i];
		}
		return v;
	}
};

[[nodiscard]] PdfResult<std::pair<CffIndex, size_t>> read_index(const CffReader& cff, size_t at)
{
	if (!cff.fits(at, 2u)) {
		return damaged("CFF INDEX is cut short");
	}
	const uint32_t count = cff.uint(at, 2u);
	CffIndex index;
	if (count == 0u) {
		return std::pair{std::move(index), at + 2u};
	}
	if (!cff.fits(at + 2u, 1u)) {
		return damaged("CFF INDEX is cut short");
	}
	const uint8_t off_size = cff.bytes[at + 2u];
	if (off_size < 1u || off_size > 4u) {
		return damaged("CFF INDEX offset size is not 1 to 4");
	}
	const size_t offsets_at = at + 3u;
	if (!cff.fits(offsets_at, (size_t{count} + 1u) * off_size)) {
		return damaged("CFF INDEX offsets run past the program");
	}
	const size_t data = offsets_at + (size_t{count} + 1u) * off_size - 1u; // offsets count from 1
	index.offsets.reserve(size_t{count} + 1u);
	uint32_t previous = 1u;
	for (size_t i = 0; i <= count; ++i) {
		const uint32_t off = cff.uint(offsets_at + i * off_size, off_size);
		if ((i == 0u && off != 1u) || off < previous || off - 1u > cff.bytes.size() - (data + 1u)) {
			return damaged("CFF INDEX offsets are out of order or past the program");
		}
		previous = off;
		index.offsets.push_back(static_cast<uint32_t>(data + off));
	}
	return std::pair{std::move(index), static_cast<size_t>(index.offsets.back())};
}

using CffDict = std::map<uint16_t, std::vector<double>>;

[[nodiscard]] PdfResult<CffDict> read_dict(std::span<const uint8_t> d)
{
	CffDict dict;
	std::vector<double> operands;
	size_t i = 0;
	while (i < d.size()) {
		const uint8_t b = d[i++];
		if (b <= 21u) {
			uint16_t op = b;
			if (b == 12u) {
				if (i >= d.size()) {
					return damaged("CFF DICT escape is cut short");
				}
				op = static_cast<uint16_t>(1200u + d[i++]);
			}
			dict[op] = std::move(operands);
			operands.clear();
			continue;
		}
		if (operands.size() >= kMaxType2Stack) {
			return limited("CFF DICT operator has more operands than its limit", RefusalReason::CharstringStack,
				operands.size() + 1u, kMaxType2Stack);
		}
		if (b == 28u) {
			if (d.size() - i < 2u) {
				return damaged("CFF DICT number is cut short");
			}
			operands.push_back(static_cast<int16_t>((d[i] << 8u) | d[i + 1u]));
			i += 2u;
		} else if (b == 29u) {
			if (d.size() - i < 4u) {
				return damaged("CFF DICT number is cut short");
			}
			const uint32_t u = (static_cast<uint32_t>(d[i]) << 24u) | (static_cast<uint32_t>(d[i + 1u]) << 16u) |
				(static_cast<uint32_t>(d[i + 2u]) << 8u) | d[i + 3u];
			operands.push_back(static_cast<int32_t>(u));
			i += 4u;
		} else if (b == 30u) {
			std::string text;
			bool done = false;
			while (i < d.size() && !done && text.size() < 64u) {
				const uint8_t nibbles = d[i++];
				for (const uint8_t n : {static_cast<uint8_t>(nibbles >> 4u), static_cast<uint8_t>(nibbles & 0x0Fu)}) {
					if (done) {
						break;
					}
					if (n <= 9u) {
						text.push_back(static_cast<char>('0' + n));
					} else if (n == 0xAu) {
						text.push_back('.');
					} else if (n == 0xBu) {
						text.push_back('E');
					} else if (n == 0xCu) {
						text += "E-";
					} else if (n == 0xEu) {
						text.push_back('-');
					} else if (n == 0xFu) {
						done = true;
					} else {
						return damaged("CFF DICT real number has a reserved nibble");
					}
				}
			}
			const std::optional<double> v = real_of(text);
			if (!done || !v.has_value()) {
				return damaged("CFF DICT real number is malformed");
			}
			operands.push_back(*v);
		} else if (b >= 32u && b <= 246u) {
			operands.push_back(static_cast<double>(b) - 139.0);
		} else if (b >= 247u && b <= 254u) {
			if (i >= d.size()) {
				return damaged("CFF DICT number is cut short");
			}
			const uint8_t w = d[i++];
			operands.push_back(b <= 250u ? (b - 247.0) * 256.0 + w + 108.0 : -(b - 251.0) * 256.0 - w - 108.0);
		} else {
			return damaged("CFF DICT holds a byte that is neither an operator nor an operand");
		}
	}
	return dict;
}

[[nodiscard]] std::optional<double> dict_number(const CffDict& dict, uint16_t op, size_t at = 0) noexcept
{
	const auto it = dict.find(op);
	if (it == dict.end() || it->second.size() <= at) {
		return std::nullopt;
	}
	return it->second[at];
}

[[nodiscard]] std::optional<FontMatrix> dict_matrix(const CffDict& dict) noexcept
{
	const auto it = dict.find(1207u);
	if (it == dict.end() || it->second.size() != 6u) {
		return std::nullopt;
	}
	FontMatrix m{};
	std::copy(it->second.begin(), it->second.end(), m.begin());
	if (m[0] * m[3] - m[1] * m[2] == 0.0) {
		return std::nullopt;
	}
	return m;
}

[[nodiscard]] PdfResult<size_t> dict_offset(const CffDict& dict, uint16_t op, const CffReader& cff, size_t base = 0)
{
	const std::optional<double> v = dict_number(dict, op);
	if (!v.has_value()) {
		return size_t{0};
	}
	if (!in_range(*v, static_cast<double>(cff.bytes.size()) + 1.0) || base + static_cast<size_t>(*v) > cff.bytes.size()) {
		return damaged("CFF offset points outside the program");
	}
	return base + static_cast<size_t>(*v);
}

// A private DICT and its local subroutines.
// What reading the private DICTs has cost: each distinct one is read once, the DICT bytes read across them may not
// pass the program's size (only overlapping DICTs can), and the subroutine offsets kept are bounded, so FDs sharing or
// overlapping one large private DICT cannot multiply the work.
struct PrivateReads {
	std::map<std::pair<size_t, size_t>, LoadedFont::CharstringProgram::Fd> seen;
	size_t dict_bytes = 0;
	size_t subr_entries = 0;

	[[nodiscard]] PdfResult<void> keep_subrs(size_t entries) noexcept
	{
		subr_entries += entries;
		if (subr_entries > kMaxCffSubrEntries) {
			return limited("CFF subroutine tables hold more entries than their limit", RefusalReason::FontTableEntries,
				subr_entries, kMaxCffSubrEntries);
		}
		return {};
	}
};

[[nodiscard]] PdfResult<LoadedFont::CharstringProgram::Fd> read_private(
	const CffReader& cff, const CffDict& font_dict, PrivateReads& reads)
{
	LoadedFont::CharstringProgram::Fd fd;
	const auto it = font_dict.find(18u);
	if (it == font_dict.end() || it->second.size() != 2u) {
		return fd; // no private DICT: no local subroutines, widths 0
	}
	const double size = it->second[0];
	const double offset = it->second[1];
	const double limit = static_cast<double>(cff.bytes.size()) + 1.0;
	if (!in_range(size, limit) || !in_range(offset, limit) ||
		!cff.fits(static_cast<size_t>(offset), static_cast<size_t>(size))) {
		return damaged("CFF private DICT lies outside the program");
	}
	const size_t at = static_cast<size_t>(offset);
	const std::pair<size_t, size_t> key{at, static_cast<size_t>(size)};
	if (const auto seen = reads.seen.find(key); seen != reads.seen.end()) {
		RECTO_PDF_RETURN_IF_ERROR(reads.keep_subrs(seen->second.local_subrs.offsets.size()));
		return seen->second;
	}
	reads.dict_bytes += key.second;
	if (reads.dict_bytes > cff.bytes.size()) {
		return damaged("CFF private DICTs overlap one another");
	}
	RECTO_PDF_TRY_REF(priv, read_dict(cff.bytes.subspan(at, key.second)));
	fd.default_width = dict_number(priv, 20u).value_or(0.0);
	fd.nominal_width = dict_number(priv, 21u).value_or(0.0);
	if (const std::optional<double> subrs = dict_number(priv, 19u)) {
		if (!in_range(*subrs, static_cast<double>(cff.bytes.size()) + 1.0) ||
			at + static_cast<size_t>(*subrs) > cff.bytes.size()) {
			return damaged("CFF local subroutines lie outside the program");
		}
		RECTO_PDF_TRY(auto index, read_index(cff, at + static_cast<size_t>(*subrs)));
		RECTO_PDF_RETURN_IF_ERROR(reads.keep_subrs(index.first.offsets.size()));
		fd.local_subrs = std::move(index.first);
	}
	reads.seen.emplace(key, fd);
	return fd;
}

[[nodiscard]] std::string sid_string(uint32_t sid, const std::vector<std::string_view>& strings)
{
	if (sid < kCffStandardStrings.size()) {
		return std::string(kCffStandardStrings[sid]);
	}
	const size_t custom = sid - kCffStandardStrings.size();
	if (custom >= strings.size() || strings[custom].size() > kMaxGlyphNameBytes) {
		return std::string();
	}
	return std::string(strings[custom]);
}

[[nodiscard]] PdfResult<std::unique_ptr<LoadedFont::CharstringProgram>> parse_cff(std::span<const uint8_t> bytes)
{
	const CffReader cff{bytes};
	if (!cff.fits(0u, 4u) || bytes[0] != 1u) {
		return damaged("CFF header is not a version 1 header");
	}
	const size_t header = bytes[2];
	RECTO_PDF_TRY(auto name_index, read_index(cff, header));
	RECTO_PDF_TRY(auto top_dicts, read_index(cff, name_index.second));
	RECTO_PDF_TRY(auto string_index, read_index(cff, top_dicts.second));
	RECTO_PDF_TRY(auto global_subrs, read_index(cff, string_index.second));
	if (top_dicts.first.count() < 1u) {
		return damaged("CFF program has no top DICT");
	}
	const auto& to = top_dicts.first.offsets;
	RECTO_PDF_TRY_REF(top, read_dict(bytes.subspan(to[0], to[1] - to[0])));
	std::vector<std::string_view> strings;
	for (size_t i = 0; i < string_index.first.count(); ++i) {
		const auto& so = string_index.first.offsets;
		strings.emplace_back(reinterpret_cast<const char*>(bytes.data() + so[i]), so[i + 1u] - so[i]);
	}
	if (const std::optional<double> type = dict_number(top, 1206u); type.has_value() && *type != 2.0) {
		return damaged("CFF CharstringType is not 2");
	}
	auto program = std::make_unique<LoadedFont::CharstringProgram>();
	const bool cid = top.contains(1230u);
	program->kind = cid ? LoadedFont::CharstringProgram::Kind::CffCid : LoadedFont::CharstringProgram::Kind::Cff;
	program->global_subrs = std::move(global_subrs.first);
	PrivateReads reads;
	RECTO_PDF_RETURN_IF_ERROR(reads.keep_subrs(program->global_subrs.offsets.size()));
	RECTO_PDF_TRY(const size_t charstrings_at, dict_offset(top, 17u, cff));
	if (charstrings_at == 0u) {
		return damaged("CFF top DICT has no CharStrings");
	}
	RECTO_PDF_TRY(auto charstrings, read_index(cff, charstrings_at));
	program->charstrings = std::move(charstrings.first);
	if (program->charstrings.count() < 1u || program->charstrings.count() > 65'535u) {
		return damaged("CFF CharStrings count is 0 or past 65,535");
	}
	const auto glyphs = static_cast<uint16_t>(program->charstrings.count());
	program->glyph_count = glyphs;
	const std::optional<FontMatrix> top_matrix = dict_matrix(top);

	if (cid) {
		RECTO_PDF_TRY(const size_t fd_array_at, dict_offset(top, 1236u, cff));
		if (fd_array_at == 0u) {
			return damaged("CID-keyed CFF has no FDArray");
		}
		RECTO_PDF_TRY(auto fd_array, read_index(cff, fd_array_at));
		if (fd_array.first.count() < 1u || fd_array.first.count() > 256u) {
			return damaged("CID-keyed CFF FDArray holds no FD or more than 256");
		}
		for (size_t i = 0; i < fd_array.first.count(); ++i) {
			const auto& fo = fd_array.first.offsets;
			RECTO_PDF_TRY_REF(fd_dict, read_dict(bytes.subspan(fo[i], fo[i + 1u] - fo[i])));
			RECTO_PDF_TRY(LoadedFont::CharstringProgram::Fd fd, read_private(cff, fd_dict, reads));
			// An FD's matrix is applied before the top one; either absent is the identity unless both are, which is
			// the CFF default (Technical Note 5176, 18).
			const std::optional<FontMatrix> fd_matrix = dict_matrix(fd_dict);
			constexpr FontMatrix identity = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
			if (!fd_matrix.has_value() && !top_matrix.has_value()) {
				fd.matrix = kDefaultFontMatrix;
			} else {
				fd.matrix = compose(fd_matrix.value_or(identity), top_matrix.value_or(identity));
			}
			program->fds.push_back(std::move(fd));
		}
		RECTO_PDF_TRY(const size_t select_at, dict_offset(top, 1237u, cff));
		if (select_at == 0u || !cff.fits(select_at, 1u)) {
			return damaged("CID-keyed CFF has no FDSelect");
		}
		program->fd_of_glyph.assign(glyphs, 0u);
		const uint8_t format = bytes[select_at];
		if (format == 0u) {
			if (!cff.fits(select_at + 1u, glyphs)) {
				return damaged("CFF FDSelect format 0 runs past the program");
			}
			for (size_t g = 0; g < glyphs; ++g) {
				program->fd_of_glyph[g] = bytes[select_at + 1u + g];
			}
		} else if (format == 3u) {
			if (!cff.fits(select_at + 1u, 2u)) {
				return damaged("CFF FDSelect format 3 is cut short");
			}
			const uint32_t ranges = cff.uint(select_at + 1u, 2u);
			if (!cff.fits(select_at + 3u, size_t{ranges} * 3u + 2u) || ranges == 0u) {
				return damaged("CFF FDSelect format 3 ranges run past the program");
			}
			for (size_t r = 0; r < ranges; ++r) {
				const size_t at = select_at + 3u + r * 3u;
				const uint32_t first = cff.uint(at, 2u);
				const uint8_t fd = bytes[at + 2u];
				const uint32_t next = cff.uint(at + 3u, 2u); // the next range's first, or the sentinel
				if ((r == 0u && first != 0u) || next <= first || next > glyphs) {
					return damaged("CFF FDSelect format 3 ranges are out of order");
				}
				std::fill(program->fd_of_glyph.begin() + first, program->fd_of_glyph.begin() + next, fd);
			}
			if (cff.uint(select_at + 3u + size_t{ranges} * 3u, 2u) != glyphs) {
				return damaged("CFF FDSelect format 3 sentinel is not the glyph count");
			}
		} else {
			return damaged("CFF FDSelect format is neither 0 nor 3");
		}
		for (const uint8_t fd : program->fd_of_glyph) {
			if (fd >= program->fds.size()) {
				return damaged("CFF FDSelect names an FD the FDArray does not hold");
			}
		}
	} else {
		RECTO_PDF_TRY(LoadedFont::CharstringProgram::Fd fd, read_private(cff, top, reads));
		fd.matrix = top_matrix.value_or(kDefaultFontMatrix);
		program->fds.push_back(std::move(fd));
	}

	// The charset: each glyph's SID (name-keyed) or CID.
	std::vector<uint32_t> charset(glyphs, 0u);
	const std::optional<double> charset_value = dict_number(top, 15u);
	const double charset_offset = charset_value.value_or(0.0);
	if (cid && charset_offset <= 2.0) {
		for (size_t g = 1; g < glyphs; ++g) {
			charset[g] = static_cast<uint32_t>(g);
		}
	} else if (charset_offset <= 2.0) {
		for (size_t g = 1; g < glyphs; ++g) {
			if (charset_offset == 0.0) {
				charset[g] = g < 229u ? static_cast<uint32_t>(g) : 0u; // ISOAdobe: SIDs 1 to 228 in order
			} else {
				const auto& predefined = charset_offset == 1.0 ? std::span<const uint16_t>(kCffExpertCharset)
															   : std::span<const uint16_t>(kCffExpertSubsetCharset);
				charset[g] = g - 1u < predefined.size() ? predefined[g - 1u] : 0u;
			}
		}
	} else {
		if (!in_range(charset_offset, static_cast<double>(bytes.size()))) {
			return damaged("CFF charset lies outside the program");
		}
		size_t at = static_cast<size_t>(charset_offset);
		const uint8_t format = bytes[at++];
		size_t g = 1;
		if (format == 0u) {
			if (!cff.fits(at, (size_t{glyphs} - 1u) * 2u)) {
				return damaged("CFF charset format 0 runs past the program");
			}
			for (; g < glyphs; ++g, at += 2u) {
				charset[g] = cff.uint(at, 2u);
			}
		} else if (format == 1u || format == 2u) {
			const size_t left_size = format == 1u ? 1u : 2u;
			while (g < glyphs) {
				if (!cff.fits(at, 2u + left_size)) {
					return damaged("CFF charset range runs past the program");
				}
				const uint32_t first = cff.uint(at, 2u);
				const uint32_t left = cff.uint(at + 2u, left_size);
				at += 2u + left_size;
				for (uint32_t k = 0; k <= left && g < glyphs; ++k, ++g) {
					charset[g] = first + k;
				}
			}
		} else {
			return damaged("CFF charset format is not 0, 1 or 2");
		}
	}
	if (cid) {
		for (uint16_t g = 0; g < glyphs; ++g) {
			program->cids.emplace_back(charset[g], g);
		}
		std::stable_sort(program->cids.begin(), program->cids.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	} else {
		for (uint16_t g = 0; g < glyphs; ++g) {
			std::string name = sid_string(charset[g], strings);
			if (!name.empty()) {
				program->names.emplace_back(std::move(name), g);
			}
		}
		std::stable_sort(program->names.begin(), program->names.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		// The encoding: each code's glyph, as its glyph name.
		GlyphNames names{};
		const double encoding_offset = dict_number(top, 16u).value_or(0.0);
		if (encoding_offset == 0.0) {
			for (size_t code = 0; code < 256u; ++code) {
				names[code] = base_encoding_name(BaseEncoding::Standard, static_cast<uint8_t>(code));
			}
		} else if (encoding_offset == 1.0) {
			for (size_t code = 0; code < 256u; ++code) {
				if (kCffExpertEncoding[code] != 0u) {
					names[code] = std::string(kCffStandardStrings[kCffExpertEncoding[code]]);
				}
			}
		} else {
			if (!in_range(encoding_offset, static_cast<double>(bytes.size())) ||
				!cff.fits(static_cast<size_t>(encoding_offset), 2u)) {
				return damaged("CFF encoding lies outside the program");
			}
			size_t at = static_cast<size_t>(encoding_offset);
			const uint8_t format = bytes[at++];
			const auto name_of = [&](size_t g) { return g < glyphs ? sid_string(charset[g], strings) : std::string(); };
			if ((format & 0x7Fu) == 0u) {
				const uint8_t codes = bytes[at++];
				if (!cff.fits(at, codes)) {
					return damaged("CFF encoding format 0 runs past the program");
				}
				for (size_t k = 0; k < codes; ++k) {
					names[bytes[at + k]] = name_of(k + 1u);
				}
				at += codes;
			} else if ((format & 0x7Fu) == 1u) {
				const uint8_t ranges = bytes[at++];
				if (!cff.fits(at, size_t{ranges} * 2u)) {
					return damaged("CFF encoding format 1 runs past the program");
				}
				size_t g = 1;
				for (size_t r = 0; r < ranges; ++r) {
					const uint32_t first = bytes[at + r * 2u];
					const uint32_t left = bytes[at + r * 2u + 1u];
					for (uint32_t k = 0; k <= left && first + k <= 255u; ++k, ++g) {
						names[first + k] = name_of(g);
					}
				}
				at += size_t{ranges} * 2u;
			} else {
				return damaged("CFF encoding format is neither 0 nor 1");
			}
			if ((format & 0x80u) != 0u) {
				if (!cff.fits(at, 1u)) {
					return damaged("CFF encoding supplements are cut short");
				}
				const uint8_t sups = bytes[at++];
				if (!cff.fits(at, size_t{sups} * 3u)) {
					return damaged("CFF encoding supplements run past the program");
				}
				for (size_t s = 0; s < sups; ++s) {
					names[bytes[at + s * 3u]] = sid_string(cff.uint(at + s * 3u + 1u, 2u), strings);
				}
			}
		}
		program->built_in = std::move(names);
	}
	return program;
}

// ---------------------------------------------------------------------------------------------------------------------
// Type 2 charstrings (Technical Note 5177)
// ---------------------------------------------------------------------------------------------------------------------

[[nodiscard]] double subr_bias(size_t count) noexcept
{
	return count < 1240u ? 107.0 : (count < 33900u ? 1131.0 : 32768.0);
}

struct Type2Run {
	const LoadedFont::CharstringProgram& program;
	std::span<const uint8_t> bytes;
	const LoadedFont::CharstringProgram::Fd& fd;
	Builder& out;
	std::vector<double> stack;
	std::array<double, 32> transient{};
	double x = 0.0;
	double y = 0.0;
	size_t stems = 0;
	bool width_read = false;
	double width = 0.0;
	bool ended = false;
	bool component = false;
	std::optional<std::array<double, 4>> seac; // adx ady bchar achar
	uint32_t random_state = 1u;
	bool width_only = false; // stop once the first stack-clearing operator gives the width

	[[nodiscard]] PdfResult<void> push(double v)
	{
		if (stack.size() >= kMaxType2Stack) {
			return limited("Type 2 charstring overflows its argument stack", RefusalReason::CharstringStack,
				stack.size() + 1u, kMaxType2Stack);
		}
		if (!std::isfinite(v)) {
			return damaged("Type 2 charstring arithmetic gave a number that is not finite");
		}
		stack.push_back(v);
		return {};
	}
	[[nodiscard]] PdfResult<void> need(size_t n) const
	{
		if (stack.size() < n) {
			return damaged("Type 2 charstring operator lacks its arguments");
		}
		return {};
	}
	// The width, read at the first stack-clearing operator when it has one argument more than it takes.
	void take_width(bool has_extra)
	{
		if (width_read) {
			return;
		}
		width_read = true;
		if (has_extra && !stack.empty()) {
			width = stack.front() + fd.nominal_width;
			stack.erase(stack.begin());
		} else {
			width = fd.default_width;
		}
		ended = ended || width_only;
	}
	[[nodiscard]] PdfResult<void> line(double dx, double dy)
	{
		const double fx = x;
		const double fy = y;
		x += dx;
		y += dy;
		return out.line_to(fx, fy, x, y);
	}
	[[nodiscard]] PdfResult<void> curve(double dx1, double dy1, double dx2, double dy2, double dx3, double dy3)
	{
		const double fx = x;
		const double fy = y;
		const double x1 = x + dx1;
		const double y1 = y + dy1;
		const double x2 = x1 + dx2;
		const double y2 = y1 + dy2;
		x = x2 + dx3;
		y = y2 + dy3;
		return out.curve_to(fx, fy, x1, y1, x2, y2, x, y);
	}
	[[nodiscard]] PdfResult<void> stem_hints()
	{
		take_width(stack.size() % 2u == 1u);
		if (stack.size() % 2u != 0u) {
			return damaged("Type 2 stem hint operator has an unmatched argument");
		}
		stems += stack.size() / 2u;
		if (stems > kMaxType2Stems) {
			return damaged("Type 2 charstring has more than 96 stem hints");
		}
		stack.clear();
		return {};
	}

	[[nodiscard]] PdfResult<void> call(const CffIndex& subrs, size_t depth)
	{
		if (stack.empty()) {
			return damaged("Type 2 call has no subroutine number");
		}
		const double index = stack.back() + subr_bias(subrs.count());
		stack.pop_back();
		if (!in_range(index, static_cast<double>(subrs.count()))) {
			return damaged("Type 2 call names a subroutine the font does not have");
		}
		const auto k = static_cast<size_t>(index);
		return run(bytes.subspan(subrs.offsets[k], subrs.offsets[k + 1u] - subrs.offsets[k]), depth + 1u);
	}

	[[nodiscard]] PdfResult<void> run(std::span<const uint8_t> code, size_t depth)
	{
		if (depth > kMaxCharstringSubrDepth) {
			return limited("Type 2 charstring nests subroutines past its limit", RefusalReason::SubroutineDepth, depth,
				kMaxCharstringSubrDepth);
		}
		if (code.size() > kMaxCharstringBytes) {
			return limited("Type 2 charstring is longer than its limit", RefusalReason::CharstringBytes, code.size(),
				kMaxCharstringBytes);
		}
		size_t i = 0;
		while (i < code.size() && !ended) {
			const size_t start = i;
			const uint8_t v = code[i++];
			RECTO_PDF_RETURN_IF_ERROR(out.spend(1u));
			if (v == 28u || v >= 32u) {
				double n = 0.0;
				if (v == 28u) {
					if (code.size() - i < 2u) {
						return damaged("Type 2 charstring number is cut short");
					}
					n = static_cast<int16_t>((code[i] << 8u) | code[i + 1u]);
					i += 2u;
				} else if (v <= 246u) {
					n = static_cast<double>(v) - 139.0;
				} else if (v <= 254u) {
					if (i >= code.size()) {
						return damaged("Type 2 charstring number is cut short");
					}
					const uint8_t w = code[i++];
					n = v <= 250u ? (v - 247.0) * 256.0 + w + 108.0 : -(v - 251.0) * 256.0 - w - 108.0;
				} else {
					if (code.size() - i < 4u) {
						return damaged("Type 2 charstring number is cut short");
					}
					const auto u = (static_cast<uint32_t>(code[i]) << 24u) | (static_cast<uint32_t>(code[i + 1u]) << 16u) |
						(static_cast<uint32_t>(code[i + 2u]) << 8u) | code[i + 3u];
					i += 4u;
					n = static_cast<double>(static_cast<int32_t>(u)) / 65536.0;
				}
				RECTO_PDF_RETURN_IF_ERROR(out.spend(i - start - 1u));
				RECTO_PDF_RETURN_IF_ERROR(push(n));
				continue;
			}
			const auto& s = stack;
			switch (v) {
			case 1u: // hstem
			case 3u: // vstem
			case 18u: // hstemhm
			case 23u: // vstemhm
				RECTO_PDF_RETURN_IF_ERROR(stem_hints());
				break;
			case 19u: // hintmask
			case 20u: { // cntrmask
				RECTO_PDF_RETURN_IF_ERROR(stem_hints()); // stems given before the mask are vstems
				const size_t mask_bytes = (stems + 7u) / 8u;
				if (code.size() - i < mask_bytes) {
					return damaged("Type 2 hintmask is cut short");
				}
				i += mask_bytes;
				RECTO_PDF_RETURN_IF_ERROR(out.spend(mask_bytes));
				break;
			}
			case 21u: // rmoveto
				take_width(s.size() > 2u);
				if (s.size() != 2u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				x += s[s.size() - 2u];
				y += s[s.size() - 1u];
				RECTO_PDF_RETURN_IF_ERROR(out.move_to(x, y));
				stack.clear();
				break;
			case 22u: // hmoveto
				take_width(s.size() > 1u);
				if (s.size() != 1u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				x += s.back();
				RECTO_PDF_RETURN_IF_ERROR(out.move_to(x, y));
				stack.clear();
				break;
			case 4u: // vmoveto
				take_width(s.size() > 1u);
				if (s.size() != 1u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				y += s.back();
				RECTO_PDF_RETURN_IF_ERROR(out.move_to(x, y));
				stack.clear();
				break;
			case 5u: // rlineto
				if (s.empty() || s.size() % 2u != 0u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				for (size_t k = 0; k + 1u < s.size(); k += 2u) {
					RECTO_PDF_RETURN_IF_ERROR(line(s[k], s[k + 1u]));
				}
				stack.clear();
				break;
			case 6u: // hlineto
			case 7u: { // vlineto
				RECTO_PDF_RETURN_IF_ERROR(need(1u));
				bool horizontal = v == 6u;
				for (const double d : s) {
					RECTO_PDF_RETURN_IF_ERROR(horizontal ? line(d, 0.0) : line(0.0, d));
					horizontal = !horizontal;
				}
				stack.clear();
				break;
			}
			case 8u: // rrcurveto
				if (s.empty() || s.size() % 6u != 0u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				for (size_t k = 0; k + 5u < s.size(); k += 6u) {
					RECTO_PDF_RETURN_IF_ERROR(curve(s[k], s[k + 1u], s[k + 2u], s[k + 3u], s[k + 4u], s[k + 5u]));
				}
				stack.clear();
				break;
			case 24u: { // rcurveline
				if (s.size() < 8u || (s.size() - 2u) % 6u != 0u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				size_t k = 0;
				for (; k + 6u + 2u <= s.size(); k += 6u) {
					RECTO_PDF_RETURN_IF_ERROR(curve(s[k], s[k + 1u], s[k + 2u], s[k + 3u], s[k + 4u], s[k + 5u]));
				}
				RECTO_PDF_RETURN_IF_ERROR(line(s[k], s[k + 1u]));
				stack.clear();
				break;
			}
			case 25u: { // rlinecurve
				if (s.size() < 8u || (s.size() - 6u) % 2u != 0u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				size_t k = 0;
				for (; k + 2u + 6u <= s.size(); k += 2u) {
					RECTO_PDF_RETURN_IF_ERROR(line(s[k], s[k + 1u]));
				}
				RECTO_PDF_RETURN_IF_ERROR(curve(s[k], s[k + 1u], s[k + 2u], s[k + 3u], s[k + 4u], s[k + 5u]));
				stack.clear();
				break;
			}
			case 26u: { // vvcurveto
				if (s.size() < 4u || s.size() % 4u > 1u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				size_t k = 0;
				double dx1 = 0.0;
				if (s.size() % 4u == 1u) {
					dx1 = s[0];
					k = 1u;
				}
				for (; k + 3u < s.size(); k += 4u) {
					RECTO_PDF_RETURN_IF_ERROR(curve(dx1, s[k], s[k + 1u], s[k + 2u], 0.0, s[k + 3u]));
					dx1 = 0.0;
				}
				stack.clear();
				break;
			}
			case 27u: { // hhcurveto
				if (s.size() < 4u || s.size() % 4u > 1u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				size_t k = 0;
				double dy1 = 0.0;
				if (s.size() % 4u == 1u) {
					dy1 = s[0];
					k = 1u;
				}
				for (; k + 3u < s.size(); k += 4u) {
					RECTO_PDF_RETURN_IF_ERROR(curve(s[k], dy1, s[k + 1u], s[k + 2u], s[k + 3u], 0.0));
					dy1 = 0.0;
				}
				stack.clear();
				break;
			}
			case 30u: // vhcurveto
			case 31u: { // hvcurveto
				if (s.size() < 4u || s.size() % 4u > 1u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				bool horizontal = v == 31u;
				size_t k = 0;
				while (k + 3u < s.size()) {
					const bool last = k + 4u == s.size() - 1u;
					const double extra = last ? s[k + 4u] : 0.0;
					if (horizontal) {
						RECTO_PDF_RETURN_IF_ERROR(curve(s[k], 0.0, s[k + 1u], s[k + 2u], extra, s[k + 3u]));
					} else {
						RECTO_PDF_RETURN_IF_ERROR(curve(0.0, s[k], s[k + 1u], s[k + 2u], s[k + 3u], extra));
					}
					horizontal = !horizontal;
					k += last ? 5u : 4u;
				}
				stack.clear();
				break;
			}
			case 10u: // callsubr
				RECTO_PDF_RETURN_IF_ERROR(call(fd.local_subrs, depth));
				break;
			case 29u: // callgsubr
				RECTO_PDF_RETURN_IF_ERROR(call(program.global_subrs, depth));
				break;
			case 11u: // return
				if (depth == 0u) {
					return damaged("Type 2 charstring returns from no subroutine");
				}
				return {};
			case 14u: // endchar
				take_width(s.size() == 1u || s.size() == 5u);
				if (!s.empty() && s.size() != 4u) {
					return damaged("Type 2 path operator has the wrong number of arguments");
				}
				if (stack.size() == 4u) {
					if (component) {
						return damaged("Type 2 seac component is itself an seac");
					}
					seac = std::array<double, 4>{s[0], s[1], s[2], s[3]};
				}
				stack.clear();
				out.close();
				ended = true;
				break;
			case 12u: {
				if (i >= code.size()) {
					return damaged("Type 2 charstring escape is cut short");
				}
				RECTO_PDF_RETURN_IF_ERROR(out.spend(1u));
				const uint8_t e = code[i++];
				RECTO_PDF_RETURN_IF_ERROR(escape(e));
				break;
			}
			default:
				return damaged("Type 2 charstring has a reserved operator");
			}
		}
		if (!ended) {
			return damaged(depth == 0u ? "Type 2 charstring ends without endchar" : "Type 2 subroutine ends without return");
		}
		return {};
	}

	[[nodiscard]] PdfResult<void> escape(uint8_t e)
	{
		auto& s = stack;
		const auto pop = [&]() -> double {
			const double v = s.back();
			s.pop_back();
			return v;
		};
		switch (e) {
		case 0u: // dotsection: a no-op
			s.clear();
			return {};
		case 3u: // and
		case 4u: { // or
			RECTO_PDF_RETURN_IF_ERROR(need(2u));
			const double b = pop();
			const double a = pop();
			return push(e == 3u ? ((a != 0.0 && b != 0.0) ? 1.0 : 0.0) : ((a != 0.0 || b != 0.0) ? 1.0 : 0.0));
		}
		case 5u: // not
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			s.back() = s.back() == 0.0 ? 1.0 : 0.0;
			return {};
		case 9u: // abs
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			s.back() = std::fabs(s.back());
			return {};
		case 10u: // add
		case 11u: // sub
		case 24u: { // mul
			RECTO_PDF_RETURN_IF_ERROR(need(2u));
			const double b = pop();
			const double a = pop();
			return push(e == 10u ? a + b : (e == 11u ? a - b : a * b));
		}
		case 12u: { // div
			RECTO_PDF_RETURN_IF_ERROR(need(2u));
			const double b = pop();
			const double a = pop();
			if (b == 0.0) {
				return damaged("Type 2 charstring divides by zero");
			}
			return push(a / b);
		}
		case 14u: // neg
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			s.back() = -s.back();
			return {};
		case 15u: { // eq
			RECTO_PDF_RETURN_IF_ERROR(need(2u));
			const double b = pop();
			const double a = pop();
			return push(a == b ? 1.0 : 0.0);
		}
		case 18u: // drop
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			s.pop_back();
			return {};
		case 20u: { // put
			RECTO_PDF_RETURN_IF_ERROR(need(2u));
			const double index = pop();
			const double value = pop();
			if (!in_range(index, static_cast<double>(transient.size()))) {
				return damaged("Type 2 put names a slot past the transient array");
			}
			transient[static_cast<size_t>(index)] = value;
			return {};
		}
		case 21u: { // get
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			const double index = pop();
			if (!in_range(index, static_cast<double>(transient.size()))) {
				return damaged("Type 2 get names a slot past the transient array");
			}
			return push(transient[static_cast<size_t>(index)]);
		}
		case 22u: { // ifelse
			RECTO_PDF_RETURN_IF_ERROR(need(4u));
			const double v2 = pop();
			const double v1 = pop();
			const double s2 = pop();
			const double s1 = pop();
			return push(v1 <= v2 ? s1 : s2);
		}
		case 23u: { // random: a reproducible value in (0, 1]
			random_state = random_state * 1103515245u + 12345u;
			return push(static_cast<double>((random_state >> 16u) % 32767u + 1u) / 32768.0);
		}
		case 26u: // sqrt
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			if (s.back() < 0.0) {
				return damaged("Type 2 sqrt of a negative number");
			}
			s.back() = std::sqrt(s.back());
			return {};
		case 27u: // dup
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			return push(s.back());
		case 28u: // exch
			RECTO_PDF_RETURN_IF_ERROR(need(2u));
			std::swap(s[s.size() - 1u], s[s.size() - 2u]);
			return {};
		case 29u: { // index
			RECTO_PDF_RETURN_IF_ERROR(need(1u));
			double k = pop();
			if (k < 0.0) {
				k = 0.0; // a negative index duplicates the top
			}
			if (!in_range(k, static_cast<double>(s.size()))) {
				return damaged("Type 2 index reaches past the stack");
			}
			return push(s[s.size() - 1u - static_cast<size_t>(k)]);
		}
		case 30u: { // roll
			RECTO_PDF_RETURN_IF_ERROR(need(2u));
			const double j = pop();
			const double n = pop();
			if (!in_range(n, static_cast<double>(s.size()) + 1.0) || !std::isfinite(j)) {
				return damaged("Type 2 roll reaches past the stack");
			}
			const auto count = static_cast<int64_t>(n);
			if (count > 0) {
				auto shift = static_cast<int64_t>(std::fmod(std::trunc(j), static_cast<double>(count)));
				if (shift < 0) {
					shift += count;
				}
				std::rotate(s.end() - count, s.end() - shift, s.end());
			}
			return {};
		}
		case 34u: { // hflex
			if (s.size() != 7u) {
				return damaged("Type 2 path operator has the wrong number of arguments");
			}
			const double y0 = y;
			RECTO_PDF_RETURN_IF_ERROR(curve(s[0], 0.0, s[1], s[2], s[3], 0.0));
			RECTO_PDF_RETURN_IF_ERROR(curve(s[4], 0.0, s[5], y0 - y, s[6], 0.0));
			s.clear();
			return {};
		}
		case 35u: // flex
			if (s.size() != 13u) {
				return damaged("Type 2 path operator has the wrong number of arguments");
			}
			RECTO_PDF_RETURN_IF_ERROR(curve(s[0], s[1], s[2], s[3], s[4], s[5]));
			RECTO_PDF_RETURN_IF_ERROR(curve(s[6], s[7], s[8], s[9], s[10], s[11]));
			s.clear();
			return {};
		case 36u: { // hflex1
			if (s.size() != 9u) {
				return damaged("Type 2 path operator has the wrong number of arguments");
			}
			const double y0 = y;
			RECTO_PDF_RETURN_IF_ERROR(curve(s[0], s[1], s[2], s[3], s[4], 0.0));
			RECTO_PDF_RETURN_IF_ERROR(curve(s[5], 0.0, s[6], s[7], s[8], y0 - y - s[7]));
			s.clear();
			return {};
		}
		case 37u: { // flex1
			if (s.size() != 11u) {
				return damaged("Type 2 path operator has the wrong number of arguments");
			}
			const double x0 = x;
			const double y0 = y;
			double dx = 0.0;
			double dy = 0.0;
			for (size_t k = 0; k < 10u; k += 2u) {
				dx += s[k];
				dy += s[k + 1u];
			}
			const bool horizontal = std::fabs(dx) > std::fabs(dy);
			RECTO_PDF_RETURN_IF_ERROR(curve(s[0], s[1], s[2], s[3], s[4], s[5]));
			const double d6 = s[10];
			const double x5 = x + s[6] + s[8];
			const double y5 = y + s[7] + s[9];
			RECTO_PDF_RETURN_IF_ERROR(curve(s[6], s[7], s[8], s[9], horizontal ? d6 : x0 - x5, horizontal ? y0 - y5 : d6));
			s.clear();
			return {};
		}
		default:
			return damaged("Type 2 charstring has a reserved escape operator");
		}
	}
};

// The advance alone, from the charstring as far as its width: a glyph's drawing past that point is not run.
[[nodiscard]] PdfResult<double> type1_advance(const LoadedFont::CharstringProgram& program, uint16_t glyph)
{
	Builder out;
	out.matrix = program.type1_matrix;
	Type1Run run{program, out};
	run.width_only = true;
	RECTO_PDF_RETURN_IF_ERROR(run.run(program.type1_glyphs[glyph], 0u));
	return finite_advance(type1_advance_x(program, run));
}

[[nodiscard]] PdfResult<double> cff_advance(
	const LoadedFont::CharstringProgram& program, std::span<const uint8_t> bytes, uint16_t glyph)
{
	const LoadedFont::CharstringProgram::Fd& fd = program.fds[program.fd_of_glyph.empty() ? 0u : program.fd_of_glyph[glyph]];
	Builder out;
	out.matrix = fd.matrix;
	const auto& co = program.charstrings.offsets;
	Type2Run run{program, bytes, fd, out};
	run.width_only = true;
	RECTO_PDF_RETURN_IF_ERROR(run.run(bytes.subspan(co[glyph], co[glyph + 1u] - co[glyph]), 0u));
	return finite_advance((run.width_read ? run.width : fd.default_width) * fd.matrix[0]);
}

[[nodiscard]] PdfResult<GlyphShape> cff_shape(
	const LoadedFont::CharstringProgram& program, std::span<const uint8_t> bytes, uint16_t glyph)
{
	const size_t fd_index = program.fd_of_glyph.empty() ? 0u : program.fd_of_glyph[glyph];
	const LoadedFont::CharstringProgram::Fd& fd = program.fds[fd_index];
	Builder out;
	out.matrix = fd.matrix;
	const auto& co = program.charstrings.offsets;
	Type2Run run{program, bytes, fd, out};
	RECTO_PDF_RETURN_IF_ERROR(run.run(bytes.subspan(co[glyph], co[glyph + 1u] - co[glyph]), 0u));
	if (!run.width_read) {
		run.width = fd.default_width;
	}
	out.close();
	if (run.seac.has_value()) {
		// `endchar`'s accent form (Technical Note 5177, Appendix C): base and accent by their StandardEncoding codes.
		const auto& a = *run.seac;
		if (program.kind != LoadedFont::CharstringProgram::Kind::Cff || !in_range(a[2], 256.0) ||
			!in_range(a[3], 256.0)) {
			return damaged("Type 2 seac names a code outside StandardEncoding or a CID-keyed font");
		}
		const auto base = glyph_named(program, base_encoding_name(BaseEncoding::Standard, static_cast<uint8_t>(a[2])));
		const auto accent = glyph_named(program, base_encoding_name(BaseEncoding::Standard, static_cast<uint8_t>(a[3])));
		if (!base.has_value() || !accent.has_value()) {
			return damaged("Type 2 seac names a glyph the font does not have");
		}
		for (const auto& [component, dx, dy] : {std::tuple{*base, 0.0, 0.0}, std::tuple{*accent, a[0], a[1]}}) {
			out.offset_x = dx;
			out.offset_y = dy;
			Type2Run part{program, bytes, fd, out};
			part.component = true;
			// The component reads its own optional width (its first operator's extra argument); the composite keeps its
			// advance.
			RECTO_PDF_RETURN_IF_ERROR(part.run(bytes.subspan(co[component], co[component + 1u] - co[component]), 0u));
			out.close();
		}
	}
	GlyphShape shape;
	RECTO_PDF_TRY(shape.advance, finite_advance(run.width * fd.matrix[0]));
	if (out.have_box) {
		shape.ink_box = out.box;
	}
	shape.outline = std::move(out.outline);
	return shape;
}

} // namespace

namespace charstrings {

PdfResult<std::unique_ptr<const LoadedFont::CharstringProgram>> parse(
	std::span<const uint8_t> bytes, CharstringFormat format, Type1Lengths lengths) noexcept
{
	try {
		if (bytes.size() > kMaxCharstringProgramBytes) {
			return limited("Charstring font program exceeds its size limit", RefusalReason::FontProgramBytes,
				bytes.size(), kMaxCharstringProgramBytes);
		}
		if (format == CharstringFormat::Type1) {
			RECTO_PDF_TRY(auto program, parse_type1(bytes, lengths));
			return std::unique_ptr<const LoadedFont::CharstringProgram>(std::move(program));
		}
		RECTO_PDF_TRY(auto program, parse_cff(bytes));
		return std::unique_ptr<const LoadedFont::CharstringProgram>(std::move(program));
	} catch (const std::bad_alloc&) {
		return charstring_error(ErrorCode::OutOfMemory, "Out of memory reading a Type 1 or CFF font program");
	}
}

PdfResult<GlyphShape> shape(const LoadedFont::CharstringProgram& program, std::span<const uint8_t> bytes, uint16_t glyph) noexcept
{
	try {
		if (glyph >= program.glyph_count) {
			return damaged("Glyph index past the font's glyph count");
		}
		if (program.kind == LoadedFont::CharstringProgram::Kind::Type1) {
			return type1_shape(program, glyph);
		}
		return cff_shape(program, bytes, glyph);
	} catch (const std::bad_alloc&) {
		return charstring_error(ErrorCode::OutOfMemory, "Out of memory drawing a Type 1 or CFF glyph");
	}
}

PdfResult<double> advance(const LoadedFont::CharstringProgram& program, std::span<const uint8_t> bytes, uint16_t glyph) noexcept
{
	try {
		if (glyph >= program.glyph_count) {
			return damaged("Glyph index past the font's glyph count");
		}
		if (program.kind == LoadedFont::CharstringProgram::Kind::Type1) {
			return type1_advance(program, glyph);
		}
		return cff_advance(program, bytes, glyph);
	} catch (const std::bad_alloc&) {
		return charstring_error(ErrorCode::OutOfMemory, "Out of memory reading a Type 1 or CFF glyph's advance");
	}
}

} // namespace charstrings

} // namespace recto::pdf
