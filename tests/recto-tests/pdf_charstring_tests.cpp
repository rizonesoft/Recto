// Unit tests for Type 1 and CFF charstring fonts (D01 T02 section 29). The fixtures `tests/corpus/fonts/type1-*.pf?`
// and `tests/corpus/fonts/cff-*.cff` are written by `scripts/build-corpus.py` from the charstring programs stated there.
// The reference outlines and advances below were drawn from those files by fontTools 4.66.0 (`fontTools.t1Lib`,
// `fontTools.cffLib` and its `DecomposingRecordingPen`), an independent reader, except where a comment says a value was
// derived by hand from the specification: fontTools runs neither `sbw` nor the Type 2 arithmetic operators.
// Coordinates are text space times 1,000, rounded to 0.001. The hostile programs are assembled here.
#include "error.h"
#include "font.h"
#include "font_encoding.h"
#include "object.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace recto::pdf {
namespace {

std::shared_ptr<const std::vector<uint8_t>> corpus_font(std::string_view name)
{
	std::ifstream in(recto::test::corpus_path(std::string("fonts/") + std::string(name)), std::ios::binary);
	EXPECT_TRUE(in.good()) << name;
	return std::make_shared<const std::vector<uint8_t>>(
		std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::shared_ptr<const LoadedFont> load_charstrings_ok(
	std::shared_ptr<const std::vector<uint8_t>> bytes, CharstringFormat format, Type1Lengths lengths = {})
{
	auto font = load_charstring_font(std::move(bytes), format, lengths);
	EXPECT_TRUE(font.has_value()) << (font.has_value() ? "" : std::string(font.error().message.view()));
	return font.has_value() ? *font : nullptr;
}

// A number as the reference outlines print it: text space times 1,000, rounded to 0.001, without trailing zeros.
std::string num(double value)
{
	std::array<char, 64> text{};
	std::snprintf(text.data(), text.size(), "%.3f", std::round(value * 1000.0) / 1000.0);
	std::string out(text.data());
	while (out.back() == '0') {
		out.pop_back();
	}
	if (out.back() == '.') {
		out.pop_back();
	}
	return out == "-0" ? std::string("0") : out;
}

std::string outline_text(const GlyphOutline& outline)
{
	std::string out;
	size_t at = 0;
	const auto points = [&](size_t count) {
		for (size_t i = 0; i < count && at < outline.points.size(); ++i, ++at) {
			out += ' ' + num(outline.points[at].x * 1000.0) + ' ' + num(outline.points[at].y * 1000.0);
		}
	};
	for (const PathVerb verb : outline.verbs) {
		if (!out.empty()) {
			out += ' ';
		}
		switch (verb) {
		case PathVerb::MoveTo:
			out += 'M';
			points(1u);
			break;
		case PathVerb::LineTo:
			out += 'L';
			points(1u);
			break;
		case PathVerb::CubicBezierTo:
			out += 'C';
			points(3u);
			break;
		case PathVerb::Rect:
			out += 'R';
			points(2u);
			break;
		case PathVerb::CloseSubpath:
			out += 'Z';
			break;
		}
	}
	EXPECT_EQ(at, outline.points.size());
	return out;
}

struct Reference {
	std::string_view name;
	double advance; // text space times 1,000
	std::string_view outline;
};

// `type1-outlines.pfa` and `.pfb` (FontMatrix 0.001): fontTools, except `wide`, whose `sbw` (Type 1 Font Format 6.4)
// sets the side bearing point (40, 10) and the width 800, derived by hand.
constexpr std::array<Reference, 9> kType1Reference = {{
	{".notdef", 500, ""},
	{"A", 600, "M 60 0 L 580 0 L 320 700 Z"},
	{"acute", 300, "M 100 750 L 180 750 L 140 850 Z"},
	{"Aacute", 600, "M 60 0 L 580 0 L 320 700 Z M 200 800 L 280 800 L 240 900 Z"},
	{"box", 600, "M 70 0 L 470 0 L 470 700 L 70 700 Z"},
	{"curves", 700, "M 30 100 C 130 100 230 200 230 300 C 230 350 280 400 330 400 C 380 400 430 450 430 400 L 430 200 Z"},
	{"wide", 800, "M 40 10 L 540 10 L 540 310 Z"},
	{"subrs", 500, "M 20 0 L 220 0 L 220 200 L 20 200 Z"},
	{"flex", 700, "M 20 100 C 120 110 220 120 320 120 C 420 120 520 110 620 100 L 620 0 L 20 0 Z"},
}};

// `type1-skewed.pfa`, FontMatrix [0.002 0 0.0005 0.002 0.01 0]: fontTools, except `wide`, by hand as above.
constexpr std::array<Reference, 9> kType1SkewedReference = {{
	{".notdef", 1000, ""},
	{"A", 1200, "M 130 0 L 1170 0 L 1000 1400 Z"},
	{"acute", 600, "M 585 1500 L 745 1500 L 715 1700 Z"},
	{"Aacute", 1200, "M 130 0 L 1170 0 L 1000 1400 Z M 810 1600 L 970 1600 L 940 1800 Z"},
	{"box", 1200, "M 150 0 L 950 0 L 1300 1400 L 500 1400 Z"},
	{"curves", 1400, "M 120 200 C 320 200 570 400 620 600 C 645 700 770 800 870 800 C 970 800 1095 900 1070 800 L 970 400 Z"},
	{"wide", 1600, "M 95 20 L 1095 20 L 1245 620 Z"},
	{"subrs", 1000, "M 50 0 L 450 0 L 550 400 L 150 400 Z"},
	{"flex", 1400, "M 100 200 C 305 220 510 240 710 240 C 910 240 1105 220 1300 200 L 1250 0 L 50 0 Z"},
}};

// `cff-outlines.cff` (FontMatrix 0.001, defaultWidthX 500, nominalWidthX 600): fontTools. `arith` is checked apart.
constexpr std::array<Reference, 31> kCffReference = {{
	{".notdef", 500, ""},
	{"A", 500, "M 40 0 L 560 0 L 300 700 Z"},
	{"acute", 500, "M 200 750 L 280 750 L 240 850 Z"},
	{"lines", 500, "M 10 10 L 110 10 L 110 110 L 160 110 L 160 170 L 230 170 L 230 200 L 270 200 Z"},
	{"curves1", 500,
		"M 0 0 C 10 0 30 10 30 30 C 60 30 100 40 100 60 C 110 65 130 95 170 95 C 180 95 200 125 240 125 C 245 135 265 165 "
		"265 205 C 265 215 285 245 285 285 Z"},
	{"curves2", 500,
		"M 0 0 C 10 0 30 30 30 70 C 40 70 60 100 60 140 C 60 190 120 260 200 269 C 200 279 220 309 260 309 C 260 319 280 349 "
		"320 349 C 370 349 430 419 439 499 C 449 519 479 559 529 619 L 599 699 L 609 719 C 639 759 689 819 759 899 Z"},
	{"flexes", 500,
		"M 0 100 C 10 120 40 160 90 220 C 160 300 250 400 360 520 C 370 520 390 550 430 550 C 480 550 540 520 610 520 C 620 "
		"525 640 535 670 535 C 710 535 760 545 820 520 C 830 520 850 525 880 525 C 920 520 970 520 1030 520 C 1030 530 1035 "
		"550 1035 580 C 1030 620 1030 670 1030 730 Z"},
	{"subrs", 500, "M 0 0 L 100 0 L 100 100 L 0 100 Z"},
	{"Aacute", 500, "M 40 0 L 560 0 L 300 700 Z M 300 800 L 380 800 L 340 900 Z"},
	{"whstem", 637, "M 10 20 L 110 20 L 110 120 Z"},
	{"dhstem", 500, "M 10 20 L 110 20 L 110 120 Z"},
	{"wvstem", 637, "M 10 20 L 110 20 L 110 120 Z"},
	{"dvstem", 500, "M 10 20 L 110 20 L 110 120 Z"},
	{"whstemhm", 637, "M 10 20 L 110 20 L 110 120 Z"},
	{"dhstemhm", 500, "M 10 20 L 110 20 L 110 120 Z"},
	{"wvstemhm", 637, "M 10 20 L 110 20 L 110 120 Z"},
	{"dvstemhm", 500, "M 10 20 L 110 20 L 110 120 Z"},
	{"whintmask", 637, "M 10 20 L 110 20 L 110 120 Z"},
	{"dhintmask", 500, "M 10 20 L 110 20 L 110 120 Z"},
	{"wcntrmask", 637, "M 10 20 L 110 20 L 110 120 Z"},
	{"dcntrmask", 500, "M 10 20 L 110 20 L 110 120 Z"},
	{"wrmoveto", 637, "M 10 20 L 110 20 L 110 120 Z"},
	{"drmoveto", 500, "M 10 20 L 110 20 L 110 120 Z"},
	{"whmoveto", 637, "M 10 0 L 110 0 L 110 100 Z"},
	{"dhmoveto", 500, "M 10 0 L 110 0 L 110 100 Z"},
	{"wvmoveto", 637, "M 0 20 L 100 20 L 100 120 Z"},
	{"dvmoveto", 500, "M 0 20 L 100 20 L 100 120 Z"},
	{"wendchar", 637, ""},
	{"dendchar", 500, ""},
	{"wseac", 637, "M 40 0 L 560 0 L 300 700 Z M 300 800 L 380 800 L 340 900 Z"},
	{"dseac", 500, "M 40 0 L 560 0 L 300 700 Z M 300 800 L 380 800 L 340 900 Z"},
}};

struct CidReference {
	uint32_t cid;
	uint16_t glyph;
	double advance;
	std::string_view outline;
};

// `cff-cid.cff`: fontTools, its points mapped through each FD's matrix composed with the top-level one (FD 0
// [0.001 0 0 0.001 0 0], FD 1 [0.002 0 0 0.001 0 0], then the top-level [1 0 0.5 1 0 0], FreeType's order).
constexpr std::array<CidReference, 5> kCidReference = {{
	{0, 0, 400, ""},
	{100, 1, 550, "M 0 0 L 100 0 L 150 100 Z"},
	{101, 2, 400, "M 0 0 L 100 0 L 150 100 Z"},
	{102, 3, 1700, "M 0 0 L 50 100 L 250 100 Z"},
	{500, 4, 1400, "M 0 0 L 50 100 L 250 100 Z"},
}};

void expect_reference(const LoadedFont& font, const Reference& ref, std::string_view label)
{
	const std::optional<uint16_t> glyph = font.glyph_for_name(ref.name);
	ASSERT_TRUE(glyph.has_value()) << label << " " << ref.name;
	const auto shape = font.shape(*glyph);
	ASSERT_TRUE(shape.has_value()) << label << " " << ref.name << ": " << shape.error().message.view();
	EXPECT_EQ(num(shape->advance * 1000.0), num(ref.advance)) << label << " " << ref.name;
	EXPECT_EQ(outline_text(shape->outline), ref.outline) << label << " " << ref.name;
}

// The /Length1 to /Length3 a PDF's /FontFile stream gives for a PFA fixture: the cleartext through `eexec` and its
// newline, the hexadecimal part, and the trailer `scripts/build-corpus.py` writes (8 lines of 64 zeros, `cleartomark`).
Type1Lengths pfa_lengths(const std::vector<uint8_t>& program)
{
	const std::string_view text(reinterpret_cast<const char*>(program.data()), program.size());
	const size_t length1 = text.find("currentfile eexec\n") + std::string_view("currentfile eexec\n").size();
	const size_t length3 = 8u * 65u + std::string_view("cleartomark\n").size();
	return Type1Lengths{length1, program.size() - length1 - length3, length3};
}

// ---------------------------------------------------------------------------------------------------------------------
// Charstring and program assembly for the built cases, as scripts/build-corpus.py writes the fixtures
// ---------------------------------------------------------------------------------------------------------------------

struct NamedOperator {
	std::string_view name;
	std::array<uint8_t, 2> code;
	uint8_t size;
};

constexpr std::array<NamedOperator, 25> kType1Operators = {{
	{"hstem", {1, 0}, 1}, {"vstem", {3, 0}, 1}, {"vmoveto", {4, 0}, 1}, {"rlineto", {5, 0}, 1},
	{"hlineto", {6, 0}, 1}, {"vlineto", {7, 0}, 1}, {"rrcurveto", {8, 0}, 1}, {"closepath", {9, 0}, 1},
	{"callsubr", {10, 0}, 1}, {"return", {11, 0}, 1}, {"hsbw", {13, 0}, 1}, {"endchar", {14, 0}, 1},
	{"rmoveto", {21, 0}, 1}, {"hmoveto", {22, 0}, 1}, {"vhcurveto", {30, 0}, 1}, {"hvcurveto", {31, 0}, 1},
	{"dotsection", {12, 0}, 2}, {"vstem3", {12, 1}, 2}, {"hstem3", {12, 2}, 2}, {"seac", {12, 6}, 2},
	{"sbw", {12, 7}, 2}, {"div", {12, 12}, 2}, {"callothersubr", {12, 16}, 2}, {"pop", {12, 17}, 2},
	{"setcurrentpoint", {12, 33}, 2},
}};

constexpr std::array<NamedOperator, 25> kType2Operators = {{
	{"hstem", {1, 0}, 1}, {"vmoveto", {4, 0}, 1}, {"rlineto", {5, 0}, 1}, {"callsubr", {10, 0}, 1},
	{"return", {11, 0}, 1}, {"endchar", {14, 0}, 1}, {"hintmask", {19, 0}, 1}, {"rmoveto", {21, 0}, 1},
	{"hmoveto", {22, 0}, 1}, {"callgsubr", {29, 0}, 1}, {"add", {12, 10}, 2}, {"drop", {12, 18}, 2},
	{"rrcurveto", {8, 0}, 1}, {"div", {12, 12}, 2}, {"sqrt", {12, 26}, 2}, {"put", {12, 20}, 2}, {"roll", {12, 30}, 2},
	{"sub", {12, 11}, 2}, {"mul", {12, 24}, 2}, {"dup", {12, 27}, 2}, {"dotsection", {12, 0}, 2}, {"flex", {12, 35}, 2},
	{"hvcurveto", {31, 0}, 1}, {"rcurveline", {24, 0}, 1}, {"eq", {12, 15}, 2},
}};

void append_number(std::vector<uint8_t>& out, int32_t value, bool type2)
{
	if (value >= -107 && value <= 107) {
		out.push_back(static_cast<uint8_t>(value + 139));
	} else if (value >= 108 && value <= 1131) {
		const int32_t v = value - 108;
		out.push_back(static_cast<uint8_t>((v >> 8) + 247));
		out.push_back(static_cast<uint8_t>(v & 0xFF));
	} else if (value >= -1131 && value <= -108) {
		const int32_t v = -value - 108;
		out.push_back(static_cast<uint8_t>((v >> 8) + 251));
		out.push_back(static_cast<uint8_t>(v & 0xFF));
	} else if (type2) {
		out.push_back(28u);
		out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>(value & 0xFF));
	} else {
		out.push_back(255u);
		for (int shift = 24; shift >= 0; shift -= 8) {
			out.push_back(static_cast<uint8_t>((static_cast<uint32_t>(value) >> shift) & 0xFFu));
		}
	}
}

std::vector<uint8_t> charstring(std::string_view program, bool type2)
{
	std::vector<uint8_t> out;
	size_t at = 0;
	while (at < program.size()) {
		while (at < program.size() && program[at] == ' ') {
			++at;
		}
		const size_t end = std::min(program.find(' ', at), program.size());
		const std::string_view token = program.substr(at, end - at);
		at = end;
		if (token.empty()) {
			continue;
		}
		bool found = false;
		for (const NamedOperator& op : type2 ? std::span<const NamedOperator>(kType2Operators)
											 : std::span<const NamedOperator>(kType1Operators)) {
			if (op.name == token) {
				out.insert(out.end(), op.code.begin(), op.code.begin() + op.size);
				found = true;
				break;
			}
		}
		if (!found) {
			append_number(out, std::stoi(std::string(token)), type2);
		}
	}
	return out;
}

std::vector<uint8_t> repeat(std::string_view program, size_t times, bool type2)
{
	const std::vector<uint8_t> once = charstring(program, type2);
	std::vector<uint8_t> out;
	out.reserve(once.size() * times);
	for (size_t i = 0; i < times; ++i) {
		out.insert(out.end(), once.begin(), once.end());
	}
	return out;
}

std::vector<uint8_t> join(std::initializer_list<std::vector<uint8_t>> parts)
{
	std::vector<uint8_t> out;
	for (const auto& part : parts) {
		out.insert(out.end(), part.begin(), part.end());
	}
	return out;
}

void put_u16(std::vector<uint8_t>& out, uint32_t value)
{
	out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
	out.push_back(static_cast<uint8_t>(value & 0xFFu));
}

void put_u32(std::vector<uint8_t>& out, uint32_t value)
{
	for (int shift = 24; shift >= 0; shift -= 8) {
		out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
	}
}

std::vector<uint8_t> cff_index(const std::vector<std::vector<uint8_t>>& items)
{
	std::vector<uint8_t> out;
	put_u16(out, static_cast<uint32_t>(items.size()));
	if (items.empty()) {
		return out;
	}
	out.push_back(4u);
	uint32_t offset = 1u;
	put_u32(out, offset);
	for (const auto& item : items) {
		offset += static_cast<uint32_t>(item.size());
		put_u32(out, offset);
	}
	for (const auto& item : items) {
		out.insert(out.end(), item.begin(), item.end());
	}
	return out;
}

// A DICT integer in the 5-byte form, so a DICT keeps its size when offsets are filled in.
void dict_int(std::vector<uint8_t>& out, int32_t value)
{
	out.push_back(29u);
	put_u32(out, static_cast<uint32_t>(value));
}

void dict_op(std::vector<uint8_t>& out, std::initializer_list<int32_t> operands, uint8_t op, bool escaped = false)
{
	for (const int32_t v : operands) {
		dict_int(out, v);
	}
	if (escaped) {
		out.push_back(12u);
	}
	out.push_back(op);
}

struct CffFd {
	std::vector<std::vector<uint8_t>> subrs;
	int32_t default_width = 0;
	int32_t nominal_width = 0;
};

struct CffBuild {
	std::vector<std::vector<uint8_t>> glyphs; // glyph 0 first
	std::vector<std::vector<uint8_t>> gsubrs;
	std::vector<std::string> strings;
	int32_t charset = -1;               // a predefined charset (0 to 2), or -1 for `charset_bytes`
	std::vector<uint8_t> charset_bytes; // a custom charset
	int32_t encoding = -2;              // a predefined encoding (0, 1), -1 for `encoding_bytes`, -2 for none
	std::vector<uint8_t> encoding_bytes;
	std::vector<CffFd> fds{CffFd{}};    // the one font's private data, or each FD's with `fd_select`
	std::vector<uint8_t> fd_select;     // non-empty for a CID-keyed font
	// A CID-keyed font's FDs all reading one private DICT of these bytes, FD `i` from `i * private_step` bytes into it.
	std::vector<uint8_t> shared_private;
	size_t private_step = 0;
};

struct CffProgram {
	std::vector<uint8_t> bytes;
	size_t charstrings_at = 0;
};

CffProgram build_cff(const CffBuild& b)
{
	const bool cid = !b.fd_select.empty();
	std::vector<std::vector<uint8_t>> strings;
	for (const std::string& s : b.strings) {
		strings.emplace_back(s.begin(), s.end());
	}
	// Each private DICT, then its local subroutines right after it.
	std::vector<std::vector<uint8_t>> privates;
	for (const CffFd& fd : b.fds) {
		std::vector<uint8_t> priv;
		dict_op(priv, {fd.default_width}, 20u);
		dict_op(priv, {fd.nominal_width}, 21u);
		if (!fd.subrs.empty()) {
			dict_op(priv, {static_cast<int32_t>(priv.size() + 6u)}, 19u);
		}
		const std::vector<uint8_t> subrs = fd.subrs.empty() ? std::vector<uint8_t>{} : cff_index(fd.subrs);
		priv.insert(priv.end(), subrs.begin(), subrs.end());
		privates.push_back(std::move(priv));
	}
	const auto private_size = [&](size_t i) {
		return static_cast<int32_t>(b.fds[i].subrs.empty() ? privates[i].size() : 18u);
	};
	struct Offsets {
		int32_t charset = 0, encoding = 0, charstrings = 0, fd_select = 0, fd_array = 0, private0 = 0;
	};
	const auto top_dict = [&](const Offsets& o) {
		std::vector<uint8_t> d;
		if (cid) {
			dict_op(d, {0, 0, 0}, 30u, true);
		}
		dict_op(d, {b.charset >= 0 ? b.charset : o.charset}, 15u);
		if (b.encoding != -2) {
			dict_op(d, {b.encoding >= 0 ? b.encoding : o.encoding}, 16u);
		}
		dict_op(d, {o.charstrings}, 17u);
		if (cid) {
			dict_op(d, {o.fd_array}, 36u, true);
			dict_op(d, {o.fd_select}, 37u, true);
		} else {
			dict_op(d, {private_size(0), o.private0}, 18u);
		}
		return d;
	};
	const std::vector<uint8_t> header = {1u, 0u, 4u, 4u};
	const std::vector<uint8_t> names = cff_index({{'T', 'e', 's', 't'}});
	const std::vector<uint8_t> string_index = cff_index(strings);
	const std::vector<uint8_t> gsubr_index = cff_index(b.gsubrs);
	const std::vector<uint8_t> charstrings = cff_index(b.glyphs);
	const size_t top_size = cff_index({top_dict(Offsets{})}).size();
	const size_t base = header.size() + names.size() + top_size + string_index.size() + gsubr_index.size();
	Offsets o;
	std::vector<uint8_t> body;
	if (b.charset < 0) {
		o.charset = static_cast<int32_t>(base + body.size());
		body.insert(body.end(), b.charset_bytes.begin(), b.charset_bytes.end());
	}
	if (b.encoding == -1) {
		o.encoding = static_cast<int32_t>(base + body.size());
		body.insert(body.end(), b.encoding_bytes.begin(), b.encoding_bytes.end());
	}
	o.charstrings = static_cast<int32_t>(base + body.size());
	body.insert(body.end(), charstrings.begin(), charstrings.end());
	if (cid) {
		o.fd_select = static_cast<int32_t>(base + body.size());
		body.insert(body.end(), b.fd_select.begin(), b.fd_select.end());
		const auto fd_array = [&](size_t private_at) {
			std::vector<std::vector<uint8_t>> dicts;
			for (size_t i = 0; i < b.fds.size(); ++i) {
				std::vector<uint8_t> d;
				if (!b.shared_private.empty()) {
					const size_t skip = i * b.private_step;
					dict_op(d, {static_cast<int32_t>(b.shared_private.size() - skip), static_cast<int32_t>(private_at + skip)}, 18u);
				} else {
					dict_op(d, {private_size(i), static_cast<int32_t>(private_at)}, 18u);
					private_at += privates[i].size();
				}
				dicts.push_back(std::move(d));
			}
			return cff_index(dicts);
		};
		o.fd_array = static_cast<int32_t>(base + body.size());
		const size_t fd_array_size = fd_array(0).size();
		const std::vector<uint8_t> fds = fd_array(base + body.size() + fd_array_size);
		body.insert(body.end(), fds.begin(), fds.end());
	} else {
		o.private0 = static_cast<int32_t>(base + body.size());
	}
	if (!b.shared_private.empty()) {
		body.insert(body.end(), b.shared_private.begin(), b.shared_private.end());
	} else {
		for (const auto& priv : privates) {
			body.insert(body.end(), priv.begin(), priv.end());
		}
	}
	const std::vector<uint8_t> top = cff_index({top_dict(o)});
	EXPECT_EQ(top.size(), top_size);
	CffProgram program;
	program.bytes = join({header, names, top, string_index, gsubr_index, body});
	program.charstrings_at = static_cast<size_t>(o.charstrings);
	return program;
}

// A name-keyed CFF whose charset is format 0 over SIDs given in glyph order after .notdef.
std::vector<uint8_t> charset_format0(std::initializer_list<uint16_t> sids)
{
	std::vector<uint8_t> out{0u};
	for (const uint16_t sid : sids) {
		put_u16(out, sid);
	}
	return out;
}

std::shared_ptr<const std::vector<uint8_t>> shared(std::vector<uint8_t> bytes)
{
	return std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
}

std::vector<uint8_t> type1_encrypt(std::vector<uint8_t> plain, uint16_t key)
{
	uint16_t r = key;
	for (uint8_t& p : plain) {
		const auto c = static_cast<uint8_t>(p ^ (r >> 8u));
		r = static_cast<uint16_t>((c + r) * 52845u + 22719u);
		p = c;
	}
	return plain;
}

// A Type 1 program with a binary `eexec` part, its charstrings encrypted with lenIV 4.
std::vector<uint8_t> build_type1(const std::vector<std::vector<uint8_t>>& subrs,
	const std::vector<std::pair<std::string, std::vector<uint8_t>>>& glyphs,
	std::string_view matrix = "0.001 0 0 0.001 0 0")
{
	const std::string clear = "%!PS-AdobeFont-1.0: Test 001.000\n/FontMatrix [" + std::string(matrix) +
							  "] readonly def\n/Encoding StandardEncoding def\ncurrentfile eexec\n";
	std::vector<uint8_t> priv;
	const auto text = [&](std::string_view s) { priv.insert(priv.end(), s.begin(), s.end()); };
	const auto entry = [&](std::string head, const std::vector<uint8_t>& cs, std::string_view tail) {
		std::vector<uint8_t> plain = {0u, 0u, 0u, 0u};
		plain.insert(plain.end(), cs.begin(), cs.end());
		const std::vector<uint8_t> cipher = type1_encrypt(std::move(plain), 4330u);
		text(head + std::to_string(cipher.size()) + " RD ");
		priv.insert(priv.end(), cipher.begin(), cipher.end());
		text(" ");
		text(tail);
		text("\n");
	};
	text("dup /Private 8 dict dup begin\n/lenIV 4 def\n");
	text("/Subrs " + std::to_string(subrs.size()) + " array\n");
	for (size_t i = 0; i < subrs.size(); ++i) {
		entry("dup " + std::to_string(i) + " ", subrs[i], "NP");
	}
	text("ND\n2 index /CharStrings " + std::to_string(glyphs.size()) + " dict dup begin\n");
	for (const auto& [name, cs] : glyphs) {
		entry("/" + name + " ", cs, "ND");
	}
	text("end\nend\n");
	std::vector<uint8_t> secret = {'R', 'C', 'T', 'O'};
	secret.insert(secret.end(), priv.begin(), priv.end());
	std::vector<uint8_t> out(clear.begin(), clear.end());
	const std::vector<uint8_t> cipher = type1_encrypt(std::move(secret), 55665u);
	out.insert(out.end(), cipher.begin(), cipher.end());
	return out;
}

void expect_refused(const PdfResult<GlyphShape>& shape, ErrorCode code, std::string_view label,
	RefusalReason reason = RefusalReason::None)
{
	ASSERT_FALSE(shape.has_value()) << label;
	EXPECT_EQ(shape.error().code, code) << label << ": " << shape.error().message.view();
	EXPECT_EQ(shape.error().decoder_step, DecoderStep::NativeCharstring) << label;
	EXPECT_EQ(shape.error().refusal_reason, reason) << label;
}

// ---------------------------------------------------------------------------------------------------------------------
// The tests
// ---------------------------------------------------------------------------------------------------------------------

// A PFA program by its stream lengths, by none and by wrong ones, and the same program as PFB segments, give the
// reference outlines and advances, including a complete flex, hint replacement and an `seac` accented glyph.
TEST(PdfFont, Type1Outlines)
{
	const auto pfa = corpus_font("type1-outlines.pfa");
	const auto pfb = corpus_font("type1-outlines.pfb");
	const Type1Lengths lengths = pfa_lengths(*pfa);
	ASSERT_GT(lengths.length2, 0u);
	const std::array<std::pair<std::string_view, std::shared_ptr<const LoadedFont>>, 4> fonts = {{
		{"PFA by its lengths", load_charstrings_ok(pfa, CharstringFormat::Type1, lengths)},
		{"PFA without lengths", load_charstrings_ok(pfa, CharstringFormat::Type1)},
		{"PFA with a wrong /Length1 and /Length2", load_charstrings_ok(pfa, CharstringFormat::Type1, {10u, 5u, 3u})},
		{"PFB", load_charstrings_ok(pfb, CharstringFormat::Type1)},
	}};
	for (const auto& [label, font] : fonts) {
		ASSERT_NE(font, nullptr) << label;
		EXPECT_EQ(font->kind(), LoadedFontKind::OutlineOnly) << label;
		EXPECT_EQ(font->dwrite_face(), nullptr) << label;
		EXPECT_EQ(font->glyph_count(), kType1Reference.size()) << label;
		EXPECT_EQ(font->glyph_for_name(".notdef"), std::optional<uint16_t>(uint16_t{0u})) << label;
		EXPECT_EQ(font->glyph_for_name("missing"), std::nullopt) << label;
		EXPECT_EQ(font->glyph_for_cid(1u), std::nullopt) << label;
		for (const Reference& ref : kType1Reference) {
			expect_reference(*font, ref, label);
		}
	}
	// A charstring with lenIV -1 is not encrypted.
	{
		const std::string clear = "%!PS-AdobeFont-1.0\ncurrentfile eexec\n";
		std::string priv = "/lenIV -1 def /Subrs 0 array /CharStrings 1 dict dup begin /.notdef ";
		const std::vector<uint8_t> cs = charstring("0 300 hsbw 0 0 rmoveto 100 0 rlineto endchar", false);
		priv += std::to_string(cs.size()) + " RD ";
		std::vector<uint8_t> secret = {'R', 'C', 'T', 'O'};
		secret.insert(secret.end(), priv.begin(), priv.end());
		secret.insert(secret.end(), cs.begin(), cs.end());
		const std::string tail = " ND end";
		secret.insert(secret.end(), tail.begin(), tail.end());
		std::vector<uint8_t> program(clear.begin(), clear.end());
		const std::vector<uint8_t> cipher = type1_encrypt(std::move(secret), 55665u);
		program.insert(program.end(), cipher.begin(), cipher.end());
		const auto font = load_charstrings_ok(shared(std::move(program)), CharstringFormat::Type1);
		ASSERT_NE(font, nullptr);
		const auto shape = font->shape(0u);
		ASSERT_TRUE(shape.has_value());
		EXPECT_EQ(outline_text(shape->outline), "M 0 0 L 100 0 Z");
		EXPECT_EQ(num(shape->advance * 1000.0), "300");
	}
	// `sbw`'s width is a vector: (500, 100) through [0.001 0 0.001 0.001 0 0] advances 0.6 (Type 1 Font Format 6.4).
	{
		const auto font = load_charstrings_ok(
			shared(build_type1({}, {{".notdef", charstring("0 0 500 100 sbw endchar", false)}}, "0.001 0 0.001 0.001 0 0")),
			CharstringFormat::Type1);
		ASSERT_NE(font, nullptr);
		const auto advance = font->advance(0u);
		const auto shape = font->shape(0u);
		ASSERT_TRUE(advance.has_value() && shape.has_value());
		EXPECT_EQ(num(*advance * 1000.0), "600");
		EXPECT_EQ(num(shape->advance * 1000.0), "600");
	}
	// A flex right after `closepath` starts its subpath at the current point, which `closepath` leaves at the last
	// point drawn (Type 1 Font Format 6.4), as the reference readers do; derived by hand.
	{
		const std::vector<std::vector<uint8_t>> subrs = {charstring("3 0 callothersubr pop pop setcurrentpoint return", false),
			charstring("0 1 callothersubr return", false), charstring("0 2 callothersubr return", false)};
		const auto glyph = charstring("0 500 hsbw 0 0 rmoveto 100 0 rlineto 0 100 rlineto closepath 1 callsubr "
									  "50 0 rmoveto 2 callsubr -40 0 rmoveto 2 callsubr 10 10 rmoveto 2 callsubr "
									  "10 0 rmoveto 2 callsubr 10 0 rmoveto 2 callsubr 10 -10 rmoveto 2 callsubr "
									  "10 0 rmoveto 2 callsubr 50 160 100 0 callsubr closepath endchar",
			false);
		const auto font = load_charstrings_ok(
			shared(build_type1(subrs, {{".notdef", charstring("0 500 hsbw endchar", false)}, {"f", glyph}})),
			CharstringFormat::Type1);
		ASSERT_NE(font, nullptr);
		const auto shape = font->shape(font->glyph_for_name("f").value_or(0u));
		ASSERT_TRUE(shape.has_value());
		EXPECT_EQ(outline_text(shape->outline),
			"M 0 0 L 100 0 L 100 100 Z M 100 100 C 110 100 120 110 130 110 C 140 110 150 100 160 100 Z");
	}
}

// Every Type 2 operator: the path, hint and subroutine ones against the reference, the arithmetic, stack and storage
// ones by their definitions (Technical Note 5177, 4.4 to 4.6), and the width rule through each kind of first operator.
TEST(PdfFont, CffOutlines)
{
	const auto font = load_charstrings_ok(corpus_font("cff-outlines.cff"), CharstringFormat::Type1C);
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(font->kind(), LoadedFontKind::OutlineOnly);
	EXPECT_EQ(font->glyph_count(), kCffReference.size() + 1u);
	for (const Reference& ref : kCffReference) {
		expect_reference(*font, ref, "cff-outlines");
	}
	// `arith`: each line's dx is an operator's result; `random` gives a number in (0, 1], the same on every call.
	{
		const std::optional<uint16_t> glyph = font->glyph_for_name("arith");
		ASSERT_TRUE(glyph.has_value());
		const auto shape = font->shape(*glyph);
		ASSERT_TRUE(shape.has_value()) << shape.error().message.view();
		const std::array<std::pair<double, double>, 21> lines = {{
			{7, 1},     // 3 4 add
			{15, 2},    // 20 5 sub
			{42, 3},    // 6 7 mul
			{12.5, 4},  // 100 8 div
			{-9, 5},    // 9 neg
			{11, 6},    // -11 abs
			{8, 7},     // 64 sqrt
			{1, 0},     // 2 3 and, 0 1 and
			{0, 1},     // 0 0 or, 0 5 or
			{1, 0},     // 0 not, 4 not
			{1, 0},     // 5 5 eq, 5 6 eq
			{14, 13},   // 13 14 2 1 ifelse, 13 14 1 2 ifelse
			{32, 1},    // 16 dup add, 17 18 exch sub
			{19, 12},   // 19 20 drop
			{30, 40},   // 30 40 1 index: 30 40 30
			{30, 0},
			{4, 1},     // 1 2 3 4 4 1 roll: 4 1 2 3
			{2, 3},
			{2, 3},     // 1 2 3 4 4 -1 roll: 2 3 4 1
			{4, 1},
			{25, 13},   // 25 0 put 0 get
		}};
		ASSERT_EQ(shape->outline.verbs.size(), 1u + lines.size() + 1u + 1u);
		double x = 0.0;
		double y = 0.0;
		std::string expected = "M 0 0";
		for (const auto& [dx, dy] : lines) {
			x += dx;
			y += dy;
			expected += " L " + num(x) + " " + num(y);
		}
		const PdfPoint random_end = shape->outline.points.back();
		const double random = random_end.x * 1000.0 - x;
		EXPECT_GT(random, 0.0);
		EXPECT_LE(random, 1.0);
		expected += " L " + num(x + random) + " " + num(y + 14.0) + " Z";
		EXPECT_EQ(outline_text(shape->outline), expected);
		const auto again = font->shape(*glyph);
		ASSERT_TRUE(again.has_value());
		EXPECT_EQ(again->outline.points.back().x, random_end.x);
	}
	// The format 0 charset names standard and custom strings; the format 1 encoding with its supplement.
	const GlyphNames* encoding = font->built_in_encoding();
	ASSERT_NE(encoding, nullptr);
	EXPECT_EQ((*encoding)[65], "A");
	EXPECT_EQ((*encoding)[194], "acute");
	EXPECT_EQ((*encoding)[200], "A");
	EXPECT_EQ((*encoding)[66], "");
	EXPECT_EQ(font->glyph_for_cid(0u), std::nullopt);

	// An seac whose components carry their own explicit widths: each component reads its width, and the composite
	// keeps its own advance (Technical Note 5177, Appendix C).
	{
		CffBuild b;
		b.glyphs = {charstring("endchar", true), charstring("100 40 hmoveto 520 0 rlineto endchar", true),
			charstring("30 200 750 rmoveto 80 0 rlineto endchar", true), charstring("100 50 65 194 endchar", true)};
		b.charset_bytes = charset_format0({34u, 125u, 171u});
		b.fds[0].default_width = 450;
		const auto f = load_charstrings_ok(shared(build_cff(b).bytes), CharstringFormat::Type1C);
		ASSERT_NE(f, nullptr);
		const auto composite = f->shape(3u);
		ASSERT_TRUE(composite.has_value()) << composite.error().message.view();
		EXPECT_EQ(outline_text(composite->outline), "M 40 0 L 560 0 Z M 300 800 L 380 800 Z");
		EXPECT_EQ(num(composite->advance * 1000.0), "450");
	}
	// The predefined charsets and the other custom formats, and the predefined encodings and format 0.
	const auto glyphs = [](size_t count) { return std::vector<std::vector<uint8_t>>(count, charstring("endchar", true)); };
	const auto name_of = [](const LoadedFont& f, std::string_view name) { return f.glyph_for_name(name); };
	{
		CffBuild b;
		b.glyphs = glyphs(40u);
		b.charset = 0; // ISOAdobe: glyph g is SID g
		b.encoding = 0;
		const auto f = load_charstrings_ok(shared(build_cff(b).bytes), CharstringFormat::Type1C);
		ASSERT_NE(f, nullptr);
		EXPECT_EQ(name_of(*f, "space"), std::optional<uint16_t>(uint16_t{1u}));
		EXPECT_EQ(name_of(*f, "A"), std::optional<uint16_t>(uint16_t{34u}));
		const GlyphNames* standard = f->built_in_encoding();
		ASSERT_NE(standard, nullptr);
		EXPECT_EQ((*standard)[0x41], "A");
		EXPECT_EQ((*standard)[0xC2], "acute");
		EXPECT_EQ((*standard)[0x80], "");
	}
	{
		CffBuild b;
		b.glyphs = glyphs(3u);
		b.charset = 1; // Expert
		b.encoding = 1;
		const auto f = load_charstrings_ok(shared(build_cff(b).bytes), CharstringFormat::Type1C);
		ASSERT_NE(f, nullptr);
		EXPECT_EQ(name_of(*f, "space"), std::optional<uint16_t>(uint16_t{1u}));
		EXPECT_EQ(name_of(*f, "exclamsmall"), std::optional<uint16_t>(uint16_t{2u}));
		const GlyphNames* expert = f->built_in_encoding();
		ASSERT_NE(expert, nullptr);
		EXPECT_EQ((*expert)[32], "space");
		EXPECT_EQ((*expert)[33], "exclamsmall");
		EXPECT_EQ((*expert)[34], "Hungarumlautsmall");
		EXPECT_EQ((*expert)[65], "asuperior");
		EXPECT_EQ((*expert)[35], ""); // Appendix B assigns no glyph to code 35
	}
	{
		CffBuild b;
		b.glyphs = glyphs(3u);
		b.charset = 2; // Expert Subset
		const auto f = load_charstrings_ok(shared(build_cff(b).bytes), CharstringFormat::Type1C);
		ASSERT_NE(f, nullptr);
		EXPECT_EQ(name_of(*f, "dollaroldstyle"), std::optional<uint16_t>(uint16_t{2u}));
		// No /Encoding in the top DICT: StandardEncoding (Technical Note 5176, Table 9).
		ASSERT_NE(f->built_in_encoding(), nullptr);
		EXPECT_EQ((*f->built_in_encoding())[0x41], "A");
	}
	for (const uint8_t format : {uint8_t{1u}, uint8_t{2u}}) {
		CffBuild b;
		b.glyphs = glyphs(5u);
		// Glyphs 1 to 3 are SIDs 34 to 36 (A, B, C), glyph 4 the custom string 391.
		b.charset_bytes = {format};
		put_u16(b.charset_bytes, 34u);
		if (format == 2u) {
			put_u16(b.charset_bytes, 2u);
		} else {
			b.charset_bytes.push_back(2u);
		}
		put_u16(b.charset_bytes, 391u);
		if (format == 2u) {
			put_u16(b.charset_bytes, 0u);
		} else {
			b.charset_bytes.push_back(0u);
		}
		b.strings = {"custom"};
		b.encoding = -1;
		b.encoding_bytes = {0u, 2u, 66u, 67u}; // format 0: glyph 1 at 66, glyph 2 at 67
		const auto f = load_charstrings_ok(shared(build_cff(b).bytes), CharstringFormat::Type1C);
		ASSERT_NE(f, nullptr) << int{format};
		EXPECT_EQ(name_of(*f, "A"), std::optional<uint16_t>(uint16_t{1u}));
		EXPECT_EQ(name_of(*f, "C"), std::optional<uint16_t>(uint16_t{3u}));
		EXPECT_EQ(name_of(*f, "custom"), std::optional<uint16_t>(uint16_t{4u}));
		const GlyphNames* names = f->built_in_encoding();
		ASSERT_NE(names, nullptr);
		EXPECT_EQ((*names)[66], "A");
		EXPECT_EQ((*names)[67], "B");
		EXPECT_EQ((*names)[65], "");
	}
}

// A CID-keyed font: glyphs found by CID where CID and glyph ID differ, and each FD's subroutines, widths and matrix;
// an FDSelect in format 0 too.
TEST(PdfFont, CffCidMapping)
{
	const auto font = load_charstrings_ok(corpus_font("cff-cid.cff"), CharstringFormat::CIDFontType0C);
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(font->glyph_count(), kCidReference.size());
	for (const CidReference& ref : kCidReference) {
		EXPECT_EQ(font->glyph_for_cid(ref.cid), std::optional<uint16_t>(ref.glyph)) << ref.cid;
		const auto shape = font->shape(ref.glyph);
		ASSERT_TRUE(shape.has_value()) << ref.cid << ": " << shape.error().message.view();
		EXPECT_EQ(num(shape->advance * 1000.0), num(ref.advance)) << ref.cid;
		EXPECT_EQ(outline_text(shape->outline), ref.outline) << ref.cid;
	}
	for (const uint32_t missing : {1u, 99u, 103u, 501u, 65'535u}) {
		EXPECT_EQ(font->glyph_for_cid(missing), std::nullopt) << missing;
	}
	EXPECT_EQ(font->glyph_for_name("cid00100"), std::nullopt);
	EXPECT_EQ(font->built_in_encoding(), nullptr);

	// FDSelect format 0: glyphs 0 and 2 in FD 0, glyph 1 in FD 1, each FD's local subroutine 0 its own.
	CffBuild b;
	b.glyphs = {charstring("endchar", true), charstring("0 0 rmoveto -107 callsubr endchar", true),
		charstring("0 0 rmoveto -107 callsubr endchar", true)};
	b.charset_bytes = {0u};
	put_u16(b.charset_bytes, 7u);
	put_u16(b.charset_bytes, 9u);
	b.fds = {CffFd{{charstring("100 0 rlineto return", true)}, 300, 0}, CffFd{{charstring("0 100 rlineto return", true)}, 900, 0}};
	b.fd_select = {0u, 0u, 1u, 0u};
	const auto f = load_charstrings_ok(shared(build_cff(b).bytes), CharstringFormat::CIDFontType0C);
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->glyph_for_cid(7u), std::optional<uint16_t>(uint16_t{1u}));
	EXPECT_EQ(f->glyph_for_cid(9u), std::optional<uint16_t>(uint16_t{2u}));
	const auto in_fd1 = f->shape(1u);
	const auto in_fd0 = f->shape(2u);
	ASSERT_TRUE(in_fd1.has_value() && in_fd0.has_value());
	EXPECT_EQ(outline_text(in_fd1->outline), "M 0 0 L 0 100 Z");
	EXPECT_EQ(num(in_fd1->advance * 1000.0), "900");
	EXPECT_EQ(outline_text(in_fd0->outline), "M 0 0 L 100 0 Z");
	EXPECT_EQ(num(in_fd0->advance * 1000.0), "300");
}

// `shape`, `outline`, `advance`, `ink_box` and `check_glyph` agree for every glyph of both kinds, under default and
// non-default (scaled, skewed, translated) font matrices; an empty glyph has an advance and no outline or ink box; and
// concurrent calls on one font give the same outlines, as nothing is cached.
TEST(PdfFont, CharstringShapes)
{
	const auto pfa = corpus_font("type1-outlines.pfa");
	const std::array<std::pair<std::string_view, std::shared_ptr<const LoadedFont>>, 4> fonts = {{
		{"type1", load_charstrings_ok(pfa, CharstringFormat::Type1)},
		{"type1-skewed", load_charstrings_ok(corpus_font("type1-skewed.pfa"), CharstringFormat::Type1)},
		{"cff", load_charstrings_ok(corpus_font("cff-outlines.cff"), CharstringFormat::Type1C)},
		{"cff-cid", load_charstrings_ok(corpus_font("cff-cid.cff"), CharstringFormat::CIDFontType0C)},
	}};
	for (const auto& [label, font] : fonts) {
		ASSERT_NE(font, nullptr) << label;
		for (uint16_t g = 0; g < font->glyph_count(); ++g) {
			const auto shape = font->shape(g);
			ASSERT_TRUE(shape.has_value()) << label << " " << g;
			const auto advance = font->advance(g);
			const auto ink = font->ink_box(g);
			const auto outline = font->outline(g);
			ASSERT_TRUE(advance.has_value() && ink.has_value() && outline.has_value()) << label << " " << g;
			EXPECT_TRUE(font->check_glyph(g).has_value()) << label << " " << g;
			EXPECT_EQ(*advance, shape->advance) << label << " " << g;
			EXPECT_EQ(outline->verbs, shape->outline.verbs) << label << " " << g;
			ASSERT_EQ(outline->points.size(), shape->outline.points.size()) << label << " " << g;
			for (size_t i = 0; i < outline->points.size(); ++i) {
				EXPECT_EQ(outline->points[i].x, shape->outline.points[i].x);
				EXPECT_EQ(outline->points[i].y, shape->outline.points[i].y);
			}
			// The ink box is the box of the outline's points, control points included, or none for no outline.
			ASSERT_EQ(ink->has_value(), shape->ink_box.has_value()) << label << " " << g;
			ASSERT_EQ(ink->has_value(), !shape->outline.points.empty()) << label << " " << g;
			if (ink->has_value()) {
				PdfRect box{shape->outline.points[0].x, shape->outline.points[0].y, shape->outline.points[0].x,
					shape->outline.points[0].y};
				for (const PdfPoint& p : shape->outline.points) {
					box.x_min = std::min(box.x_min, p.x);
					box.y_min = std::min(box.y_min, p.y);
					box.x_max = std::max(box.x_max, p.x);
					box.y_max = std::max(box.y_max, p.y);
				}
				EXPECT_EQ((*ink)->x_min, box.x_min) << label << " " << g;
				EXPECT_EQ((*ink)->y_min, box.y_min) << label << " " << g;
				EXPECT_EQ((*ink)->x_max, box.x_max) << label << " " << g;
				EXPECT_EQ((*ink)->y_max, box.y_max) << label << " " << g;
				EXPECT_EQ(shape->ink_box->x_min, box.x_min);
				EXPECT_EQ(shape->ink_box->y_max, box.y_max);
			}
		}
		const auto past = font->shape(font->glyph_count());
		ASSERT_FALSE(past.has_value()) << label;
		EXPECT_EQ(past.error().code, ErrorCode::DamagedFormat) << label;
	}
	// The skewed matrix maps every glyph.
	for (const Reference& ref : kType1SkewedReference) {
		expect_reference(*fonts[1].second, ref, "type1-skewed");
	}
	// An empty glyph: .notdef of the Type 1 fixture.
	{
		const auto shape = fonts[0].second->shape(0u);
		ASSERT_TRUE(shape.has_value());
		EXPECT_EQ(shape->advance, 0.5);
		EXPECT_TRUE(shape->outline.verbs.empty());
		EXPECT_TRUE(shape->outline.points.empty());
		EXPECT_FALSE(shape->ink_box.has_value());
	}
	// Four threads drawing every glyph of one font agree with one another.
	for (const auto& [label, font] : fonts) {
		std::array<std::string, 4> drawn;
		std::vector<std::thread> threads;
		for (size_t t = 0; t < drawn.size(); ++t) {
			threads.emplace_back([&, t] {
				for (int round = 0; round < 20; ++round) {
					std::string all;
					for (uint16_t g = 0; g < font->glyph_count(); ++g) {
						const auto shape = font->shape(g);
						all += shape.has_value() ? outline_text(shape->outline) : std::string("error");
						all += '|';
					}
					drawn[t] = std::move(all);
				}
			});
		}
		for (std::thread& thread : threads) {
			thread.join();
		}
		for (const std::string& d : drawn) {
			EXPECT_EQ(d, drawn[0]) << label;
		}
	}
}

// Hostile programs are refused with typed errors and bounded work, beside the boundary cases that pass.
TEST(PdfFont, HostileCharstringsRefused)
{
	const auto end = charstring("endchar", true);
	const auto name_keyed = [&](std::vector<std::vector<uint8_t>> glyphs, std::vector<std::vector<uint8_t>> subrs = {},
								std::vector<std::vector<uint8_t>> gsubrs = {}) {
		CffBuild b;
		b.charset_bytes = {0u};
		for (size_t g = 1; g < glyphs.size(); ++g) {
			// A (34) and acute (125) first, for seac; then SIDs from 400 up, past the strings, nameless but valid.
			put_u16(b.charset_bytes, g == 1u ? 34u : (g == 2u ? 125u : static_cast<uint32_t>(400u + g)));
		}
		b.glyphs = std::move(glyphs);
		b.fds[0].subrs = std::move(subrs);
		b.gsubrs = std::move(gsubrs);
		return build_cff(b);
	};
	const auto cff_glyph = [&](const std::vector<uint8_t>& program, uint16_t glyph) {
		auto font = load_charstring_font(shared(program), CharstringFormat::Type1C);
		EXPECT_TRUE(font.has_value()) << (font.has_value() ? "" : std::string(font.error().message.view()));
		return font.has_value() ? (*font)->shape(glyph) : PdfResult<GlyphShape>(font.error());
	};
	const auto load_error = [](const std::vector<uint8_t>& program, CharstringFormat format, std::string_view label,
								ErrorCode code, RefusalReason reason = RefusalReason::None) {
		const auto font = load_charstring_font(shared(program), format);
		ASSERT_FALSE(font.has_value()) << label;
		EXPECT_EQ(font.error().code, code) << label << ": " << font.error().message.view();
		EXPECT_EQ(font.error().decoder_step, DecoderStep::NativeCharstring) << label;
		EXPECT_EQ(font.error().refusal_reason, reason) << label;
	};

	// Offsets outside the buffer, and an INDEX count past its data.
	{
		const CffProgram good = name_keyed({end, charstring("0 0 rmoveto 10 0 rlineto endchar", true)});
		ASSERT_TRUE(cff_glyph(good.bytes, 1u).has_value());
		std::vector<uint8_t> cut(good.bytes.begin(), good.bytes.end() - 4);
		load_error(cut, CharstringFormat::Type1C, "private DICT past the end", ErrorCode::DamagedFormat);
		std::vector<uint8_t> counted = good.bytes;
		counted[good.charstrings_at] = 0xFFu;
		counted[good.charstrings_at + 1u] = 0xFFu;
		load_error(counted, CharstringFormat::Type1C, "INDEX count past its data", ErrorCode::DamagedFormat);
		std::vector<uint8_t> off_size = good.bytes;
		off_size[good.charstrings_at + 2u] = 5u;
		load_error(off_size, CharstringFormat::Type1C, "INDEX offset size 5", ErrorCode::DamagedFormat);
		load_error({1u, 0u, 4u}, CharstringFormat::Type1C, "header cut short", ErrorCode::DamagedFormat);
		load_error({}, CharstringFormat::Type1C, "empty program", ErrorCode::DamagedFormat);
		std::vector<uint8_t> huge(kMaxCharstringProgramBytes + 1u, 0u);
		load_error(huge, CharstringFormat::Type1C, "program past 64 MiB", ErrorCode::ResourceLimit,
			RefusalReason::FontProgramBytes);
		// An FDSelect naming an FD the FDArray does not hold.
		CffBuild cid;
		cid.glyphs = {end, end};
		cid.charset_bytes = {0u, 0u, 5u};
		cid.fd_select = {0u, 0u, 3u};
		load_error(build_cff(cid).bytes, CharstringFormat::CIDFontType0C, "FDSelect past the FDArray",
			ErrorCode::DamagedFormat);
	}
	// Subroutine depth: 10 nested calls pass, 11 and self-recursion are refused.
	{
		const auto chain = [&](size_t depth) {
			std::vector<std::vector<uint8_t>> subrs;
			for (size_t k = 0; k + 1u < depth; ++k) {
				subrs.push_back(charstring(std::to_string(static_cast<int>(k) + 1 - 107) + " callsubr return", true));
			}
			subrs.push_back(charstring("10 0 rlineto return", true));
			return name_keyed({end, charstring("0 0 rmoveto -107 callsubr endchar", true)}, subrs).bytes;
		};
		EXPECT_TRUE(cff_glyph(chain(10u), 1u).has_value());
		expect_refused(cff_glyph(chain(11u), 1u), ErrorCode::ResourceLimit, "Type 2 depth 11", RefusalReason::SubroutineDepth);
		expect_refused(cff_glyph(name_keyed({end, charstring("-107 callsubr endchar", true)},
								  {charstring("-107 callsubr return", true)}).bytes, 1u),
			ErrorCode::ResourceLimit, "Type 2 recursion", RefusalReason::SubroutineDepth);
		expect_refused(cff_glyph(name_keyed({end, charstring("-107 callgsubr endchar", true)}, {},
								  {charstring("-107 callgsubr return", true)}).bytes, 1u),
			ErrorCode::ResourceLimit, "Type 2 global recursion", RefusalReason::SubroutineDepth);
		expect_refused(cff_glyph(name_keyed({end, charstring("5 callsubr endchar", true)},
								  {charstring("return", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 subroutine past the INDEX");
	}
	// The stack: 48 arguments pass, 49 overflow; too few underflow.
	{
		const auto args = [](size_t count) {
			std::string program = "0 0 rmoveto";
			for (size_t i = 0; i < count; ++i) {
				program += " 1";
			}
			return charstring(program + " rlineto endchar", true);
		};
		EXPECT_TRUE(cff_glyph(name_keyed({end, args(48u)}).bytes, 1u).has_value());
		expect_refused(cff_glyph(name_keyed({end, args(49u)}).bytes, 1u), ErrorCode::ResourceLimit, "Type 2 stack 49",
			RefusalReason::CharstringStack);
		// The advance alone runs only as far as the width: a glyph refused for what it draws after it still has one,
		// while `check_glyph` refuses it.
		{
			std::string program = "37 0 0 rmoveto";
			for (int i = 0; i < 49; ++i) {
				program += " 1";
			}
			auto font = load_charstring_font(
				shared(name_keyed({end, charstring(program + " rlineto endchar", true)}).bytes), CharstringFormat::Type1C);
			ASSERT_TRUE(font.has_value());
			const auto advance = (*font)->advance(1u);
			ASSERT_TRUE(advance.has_value());
			EXPECT_EQ(num(*advance * 1000.0), "37");
			const auto checked = (*font)->check_glyph(1u);
			ASSERT_FALSE(checked.has_value());
			EXPECT_EQ(checked.error().refusal_reason, RefusalReason::CharstringStack);
		}
		expect_refused(cff_glyph(name_keyed({end, charstring("5 rmoveto endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 rmoveto underflow");
		expect_refused(cff_glyph(name_keyed({end, charstring("0 0 rmoveto 5 add endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 add underflow");
		expect_refused(cff_glyph(name_keyed({end, charstring("0 0 rmoveto 1 2 3 4 5 rrcurveto endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 rrcurveto underflow");
		expect_refused(cff_glyph(name_keyed({end, charstring("0 0 rmoveto 1 0 div 0 rlineto endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 division by zero");
		expect_refused(cff_glyph(name_keyed({end, charstring("0 0 rmoveto 1 40 put endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 put past the transient array");
		expect_refused(cff_glyph(name_keyed({end, charstring("0 0 rmoveto 1 2 9 1 roll endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 roll past the stack");
		expect_refused(cff_glyph(name_keyed({end, charstring("0 0 rmoveto -4 sqrt 0 rlineto endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 sqrt of a negative");
		// Squaring 32,767 ten times overflows to infinity, and infinity minus itself is NaN: neither reaches an
		// outline or a subroutine index.
		std::string infinite = "0 0 rmoveto 1000";
		for (int i = 0; i < 10; ++i) {
			infinite += " dup mul";
		}
		expect_refused(cff_glyph(name_keyed({end, charstring(infinite + " 0 rlineto endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 infinite coordinate");
		// Infinity compared with itself is a finite 1: only the arithmetic's own check refuses it.
		expect_refused(cff_glyph(name_keyed({end, charstring(infinite + " dup eq 0 rlineto endchar", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 infinity compared");
		expect_refused(cff_glyph(name_keyed({end, charstring(infinite + " dup sub callsubr endchar", true)},
								  {charstring("return", true)}).bytes, 1u),
			ErrorCode::DamagedFormat, "Type 2 NaN subroutine index");
	}
	// Programs must end by their terminators and give each path operator exactly its arguments (Technical Note 5177,
	// 4.1 and 4.7); `dotsection`, kept as a no-op (Appendix B), changes nothing.
	{
		const auto t2 = [&](std::string_view program, std::vector<std::vector<uint8_t>> subrs = {}) {
			return cff_glyph(name_keyed({end, charstring(program, true)}, std::move(subrs)).bytes, 1u);
		};
		expect_refused(t2("0 0 rmoveto 10 0 rlineto"), ErrorCode::DamagedFormat, "Type 2 glyph without endchar");
		expect_refused(t2("0 0 rmoveto return"), ErrorCode::DamagedFormat, "Type 2 return from no subroutine");
		expect_refused(t2("0 0 rmoveto -107 callsubr endchar", {charstring("10 0 rlineto", true)}),
			ErrorCode::DamagedFormat, "Type 2 subroutine without return");
		for (const std::string_view bad : {"0 0 rmoveto 1 2 3 rlineto endchar", "0 0 rmoveto 1 2 3 4 5 6 7 rrcurveto endchar",
				 "0 0 rmoveto 1 2 3 4 5 6 hvcurveto endchar", "0 0 rmoveto 1 2 3 4 5 6 7 8 9 rcurveline endchar",
				 "0 0 rmoveto 1 2 3 4 5 6 7 8 9 10 11 12 13 14 flex endchar", "1 2 3 4 rmoveto 1 0 rlineto endchar",
				 "0 0 rmoveto 1 2 endchar"}) {
			expect_refused(t2(bad), ErrorCode::DamagedFormat, bad);
		}
		const auto plain = t2("0 0 rmoveto 10 0 rlineto endchar");
		const auto dotted = t2("0 0 rmoveto dotsection 10 0 rlineto dotsection endchar");
		ASSERT_TRUE(plain.has_value() && dotted.has_value());
		EXPECT_EQ(outline_text(dotted->outline), outline_text(plain->outline));
		EXPECT_EQ(dotted->advance, plain->advance);
		const std::pair<std::string, std::vector<uint8_t>> nd{".notdef", charstring("0 500 hsbw endchar", false)};
		const auto t1 = [&](std::string_view program, std::string_view matrix = "0.001 0 0 0.001 0 0") {
			auto font = load_charstring_font(shared(build_type1({}, {nd, {"g", charstring(program, false)}}, matrix)),
				CharstringFormat::Type1);
			EXPECT_TRUE(font.has_value());
			return font.has_value() ? (*font)->shape((*font)->glyph_for_name("g").value_or(0u))
									: PdfResult<GlyphShape>(font.error());
		};
		expect_refused(t1("0 500 hsbw 0 0 rmoveto 10 0 rlineto"), ErrorCode::DamagedFormat, "Type 1 glyph without endchar");
		expect_refused(t1("0 500 hsbw return"), ErrorCode::DamagedFormat, "Type 1 return from no subroutine");
		expect_refused(t1("0 500 hsbw pop endchar"), ErrorCode::DamagedFormat, "Type 1 pop with nothing to take");
		// A finite but enormous /FontMatrix overflows the points (here with a zero, finite advance) and the advance to
		// infinity.
		expect_refused(t1("0 0 hsbw 0 0 rmoveto 1000 0 rlineto endchar", "1e308 0 0 1e308 0 0"), ErrorCode::DamagedFormat,
			"points past the double range");
		auto huge = load_charstring_font(shared(build_type1({}, {nd}, "1e308 0 0 1e308 0 0")), CharstringFormat::Type1);
		ASSERT_TRUE(huge.has_value());
		const auto advance = (*huge)->advance(0u);
		ASSERT_FALSE(advance.has_value());
		EXPECT_EQ(advance.error().code, ErrorCode::DamagedFormat);
	}
	// A glyph name past 255 bytes names nothing, however many glyphs share it; subroutine tables are bounded across
	// FDs: 256 FDs of 4,095 subroutines (4,096 offsets each, 1,048,576 in all) load, of 4,096 are refused.
	{
		CffBuild b;
		b.glyphs = {end, end, end};
		b.strings = {std::string(300u, 'x'), "short"};
		b.charset_bytes = {0u};
		put_u16(b.charset_bytes, 391u);
		put_u16(b.charset_bytes, 392u);
		const auto f = load_charstrings_ok(shared(build_cff(b).bytes), CharstringFormat::Type1C);
		ASSERT_NE(f, nullptr);
		EXPECT_EQ(f->glyph_for_name(std::string(300u, 'x')), std::nullopt);
		EXPECT_EQ(f->glyph_for_name(""), std::nullopt);
		EXPECT_EQ(f->glyph_for_name("short"), std::optional<uint16_t>(uint16_t{2u}));
		const auto fds = [&](size_t subrs_each) {
			CffBuild cid;
			cid.glyphs = {end, end};
			cid.charset_bytes = {0u, 0u, 1u};
			cid.fd_select = {0u, 0u, 255u};
			cid.fds.assign(256u, CffFd{std::vector<std::vector<uint8_t>>(subrs_each, charstring("return", true)), 0, 0});
			return build_cff(cid).bytes;
		};
		EXPECT_TRUE(load_charstring_font(shared(fds(4'095u)), CharstringFormat::CIDFontType0C).has_value());
		// 256 FDs reading one 40,000-byte private DICT read it once; shifted 2 bytes apart they would read 256 times
		// the program's size, which only overlapping DICTs can, and are refused.
		const auto shared_dict = [&](size_t step) {
			CffBuild cid;
			cid.glyphs = {end, end};
			cid.charset_bytes = {0u, 0u, 1u};
			cid.fd_select = {0u, 0u, 255u};
			cid.fds.assign(256u, CffFd{});
			for (size_t i = 0; i < 20'000u; ++i) {
				cid.shared_private.push_back(0x8Bu); // 0
				cid.shared_private.push_back(0x14u); // defaultWidthX
			}
			cid.private_step = step;
			return build_cff(cid).bytes;
		};
		EXPECT_TRUE(load_charstring_font(shared(shared_dict(0u)), CharstringFormat::CIDFontType0C).has_value());
		load_error(shared_dict(2u), CharstringFormat::CIDFontType0C, "overlapping private DICTs", ErrorCode::DamagedFormat);
		load_error(fds(4'096u), CharstringFormat::CIDFontType0C, "subroutine tables past 1,048,576 entries",
			ErrorCode::ResourceLimit, RefusalReason::FontTableEntries);
	}
	// A charstring of 65,535 bytes passes and one of 65,536 is refused.
	{
		const auto sized = [&](size_t extra_masks) {
			// 3 (rmoveto) + 21,843 lines of 3 bytes + the masks + 1 (endchar).
			return join({charstring("0 0 rmoveto", true), repeat("0 0 rlineto", 21'843u, true),
				repeat("hintmask", extra_masks, true), end});
		};
		ASSERT_EQ(sized(2u).size(), kMaxCharstringBytes);
		EXPECT_TRUE(cff_glyph(name_keyed({end, sized(2u)}).bytes, 1u).has_value());
		expect_refused(cff_glyph(name_keyed({end, sized(3u)}).bytes, 1u), ErrorCode::ResourceLimit,
			"Type 2 charstring of 65,536 bytes", RefusalReason::CharstringBytes);
	}
	// Path operators: 100,000 pass, 100,001 are refused.
	{
		const auto ops = [&](size_t direct_lines) {
			// 1 move, 99 calls of a subroutine drawing 1,000 lines, and the lines drawn directly.
			return name_keyed({end, join({charstring("0 0 rmoveto", true), repeat("-107 callsubr", 99u, true),
									   repeat("0 0 rlineto", direct_lines, true), end})},
				{join({repeat("0 0 rlineto", 1'000u, true), charstring("return", true)})})
				.bytes;
		};
		const auto at_limit = cff_glyph(ops(999u), 1u);
		ASSERT_TRUE(at_limit.has_value()) << at_limit.error().message.view();
		EXPECT_EQ(at_limit->outline.verbs.size(), kMaxGlyphPathOperators + 1u); // and the closing CloseSubpath
		const auto past = cff_glyph(ops(1'000u), 1u);
		expect_refused(past, ErrorCode::ResourceLimit, "100,001 path operators", RefusalReason::PathOperators);
		if (!past.has_value()) {
			EXPECT_EQ(past.error().observed, std::optional<uint64_t>(kMaxGlyphPathOperators + 1u));
			EXPECT_EQ(past.error().bound, std::optional<uint64_t>(kMaxGlyphPathOperators));
		}
	}
	// Executed bytes: a fan-out through subroutines that draws nothing, and an seac whose components each stay under
	// the limit but together pass it.
	{
		// Subroutine 1: 10,000 times `0 drop` (30,000 bytes); subroutine 0 calls it 10 times.
		const std::vector<std::vector<uint8_t>> subrs = {
			join({repeat("-106 callsubr", 10u, true), charstring("return", true)}),
			join({repeat("0 drop", 10'000u, true), charstring("return", true)})};
		const auto fan_out = cff_glyph(name_keyed({end, charstring("-107 callsubr -107 callsubr -107 callsubr -107 "
																	"callsubr endchar", true)}, subrs).bytes, 1u);
		expect_refused(fan_out, ErrorCode::ResourceLimit, "fan-out past 1,000,000 bytes", RefusalReason::CharstringWork);
		// Every byte of a number counts: 34 calls of 7,500 times `1000 drop` (two number bytes and two operator
		// bytes) execute 1,020,000 bytes, though only 765,000 of them start a token.
		const auto numbers = cff_glyph(
			name_keyed({end, join({repeat("-107 callsubr", 34u, true), end})},
				{join({repeat("1000 drop", 7'500u, true), charstring("return", true)})})
				.bytes,
			1u);
		expect_refused(numbers, ErrorCode::ResourceLimit, "multi-byte numbers past 1,000,000 bytes",
			RefusalReason::CharstringWork);
		// A and acute each run subroutine 1 twenty times (about 600,000 bytes); the seac glyph runs both.
		const auto heavy = join({charstring("0 0 rmoveto", true), repeat("-106 callsubr", 20u, true), end});
		const auto program = name_keyed({end, heavy, heavy, charstring("0 0 65 194 endchar", true)}, subrs).bytes;
		EXPECT_TRUE(cff_glyph(program, 1u).has_value());
		EXPECT_TRUE(cff_glyph(program, 2u).has_value());
		expect_refused(cff_glyph(program, 3u), ErrorCode::ResourceLimit, "seac components past 1,000,000 bytes together",
			RefusalReason::CharstringWork);
	}
	// An seac whose component is itself an seac, and one naming a glyph the font lacks.
	{
		const auto nested = name_keyed(
			{end, charstring("0 0 65 194 endchar", true), charstring("0 0 rmoveto 1 0 rlineto endchar", true)}).bytes;
		expect_refused(cff_glyph(nested, 1u), ErrorCode::DamagedFormat, "Type 2 seac of an seac");
		const auto lacking = name_keyed({end, end, end, charstring("0 0 65 66 endchar", true)}).bytes;
		expect_refused(cff_glyph(lacking, 3u), ErrorCode::DamagedFormat, "Type 2 seac of a missing glyph");
	}

	// Type 1: the same bounds.
	const auto t1 = [](std::string_view program) { return charstring(program, false); };
	const auto type1_glyph = [&](const std::vector<uint8_t>& program, std::string_view name) {
		auto font = load_charstring_font(shared(program), CharstringFormat::Type1);
		EXPECT_TRUE(font.has_value()) << (font.has_value() ? "" : std::string(font.error().message.view()));
		if (!font.has_value()) {
			return PdfResult<GlyphShape>(font.error());
		}
		const std::optional<uint16_t> glyph = (*font)->glyph_for_name(name);
		EXPECT_TRUE(glyph.has_value()) << name;
		return (*font)->shape(glyph.value_or(0u));
	};
	const std::pair<std::string, std::vector<uint8_t>> notdef{".notdef", t1("0 500 hsbw endchar")};
	{
		const auto chain = [&](size_t depth) {
			std::vector<std::vector<uint8_t>> subrs;
			for (size_t k = 0; k + 1u < depth; ++k) {
				subrs.push_back(t1(std::to_string(k + 1u) + " callsubr return"));
			}
			subrs.push_back(t1("10 0 rlineto return"));
			return build_type1(subrs, {notdef, {"g", t1("0 500 hsbw 0 0 rmoveto 0 callsubr endchar")}});
		};
		EXPECT_TRUE(type1_glyph(chain(10u), "g").has_value());
		expect_refused(type1_glyph(chain(11u), "g"), ErrorCode::ResourceLimit, "Type 1 depth 11",
			RefusalReason::SubroutineDepth);
		const auto stack = [&](size_t count) {
			std::string program = "0 500 hsbw 0 0 rmoveto";
			for (size_t i = 0; i < count; ++i) {
				program += " 1";
			}
			return build_type1({}, {notdef, {"g", t1(program + " rlineto endchar")}});
		};
		EXPECT_TRUE(type1_glyph(stack(24u), "g").has_value());
		expect_refused(type1_glyph(stack(25u), "g"), ErrorCode::ResourceLimit, "Type 1 stack 25",
			RefusalReason::CharstringStack);
		{
			auto font = load_charstring_font(shared(stack(25u)), CharstringFormat::Type1);
			ASSERT_TRUE(font.has_value());
			const auto advance = (*font)->advance((*font)->glyph_for_name("g").value_or(0u));
			ASSERT_TRUE(advance.has_value());
			EXPECT_EQ(num(*advance * 1000.0), "500");
		}
		expect_refused(type1_glyph(build_type1({}, {notdef, {"g", t1("0 500 hsbw 5 rlineto endchar")}}), "g"),
			ErrorCode::DamagedFormat, "Type 1 rlineto underflow");
		expect_refused(type1_glyph(build_type1({}, {notdef, {"g", t1("0 500 hsbw 3 callsubr endchar")}}), "g"),
			ErrorCode::DamagedFormat, "Type 1 subroutine past /Subrs");
		expect_refused(type1_glyph(build_type1({}, {notdef, {"g", t1("0 500 hsbw 1 0 div endchar")}}), "g"),
			ErrorCode::DamagedFormat, "Type 1 division by zero");
		expect_refused(type1_glyph(build_type1({}, {notdef, {"g", t1("0 500 hsbw 0 1 callothersubr 0 0 0 0 callothersubr "
																		"endchar")}}), "g"),
			ErrorCode::DamagedFormat, "Type 1 flex without its seven points");
		// An seac of an seac, and an seac whose components together pass the work limit.
		const auto nested = build_type1({}, {notdef, {"A", t1("0 600 hsbw 0 0 0 65 194 seac")},
											   {"acute", t1("0 300 hsbw endchar")},
											   {"Aacute", t1("0 600 hsbw 0 0 0 65 194 seac")}});
		expect_refused(type1_glyph(nested, "Aacute"), ErrorCode::DamagedFormat, "Type 1 seac of an seac");
		const std::vector<std::vector<uint8_t>> heavy_subrs = {join({repeat("0 0 hstem", 10'000u, false), t1("return")})};
		const auto heavy = join({t1("0 500 hsbw"), repeat("0 callsubr", 20u, false), t1("endchar")});
		const auto shared_budget =
			build_type1(heavy_subrs, {notdef, {"A", heavy}, {"acute", heavy}, {"Aacute", t1("0 600 hsbw 0 0 0 65 194 seac")}});
		EXPECT_TRUE(type1_glyph(shared_budget, "A").has_value());
		expect_refused(type1_glyph(shared_budget, "Aacute"), ErrorCode::ResourceLimit,
			"Type 1 seac components past 1,000,000 bytes together", RefusalReason::CharstringWork);
	}
	// A Type 1 charstring of 65,536 bytes, and programs that are not Type 1.
	{
		std::vector<uint8_t> long_glyph = join({t1("0 500 hsbw"), repeat("0 0 hstem", 21'845u, false)});
		long_glyph.resize(kMaxCharstringBytes + 1u - 4u, 0x8Bu); // with the 4 lenIV bytes, 65,536
		load_error(build_type1({}, {notdef, {"g", long_glyph}}), CharstringFormat::Type1, "Type 1 charstring of 65,536 bytes",
			ErrorCode::ResourceLimit, RefusalReason::CharstringBytes);
		const std::string no_eexec = "%!PS-AdobeFont-1.0\n/FontMatrix [0.001 0 0 0.001 0 0] def\n";
		load_error(std::vector<uint8_t>(no_eexec.begin(), no_eexec.end()), CharstringFormat::Type1, "no eexec",
			ErrorCode::DamagedFormat);
		load_error(build_type1({}, {}), CharstringFormat::Type1, "no charstrings", ErrorCode::DamagedFormat);
		load_error({0x80u, 0x01u, 0xFFu, 0xFFu, 0x00u, 0x00u, 'x'}, CharstringFormat::Type1, "PFB segment past the end",
			ErrorCode::DamagedFormat);
	}
}

// A Type 1 program's custom encoding is the built-in encoding `SimpleEncoding` builds on (D01 T02 section 28), with a
// font dictionary's /Differences over it, and a CFF encoding's supplement reaches it too.
TEST(PdfFont, Type1BuiltInEncoding)
{
	const auto font = load_charstrings_ok(corpus_font("type1-outlines.pfa"), CharstringFormat::Type1);
	ASSERT_NE(font, nullptr);
	const GlyphNames* built_in = font->built_in_encoding();
	ASSERT_NE(built_in, nullptr);
	EXPECT_EQ((*built_in)[65], "A");
	EXPECT_EQ((*built_in)[66], "box");
	EXPECT_EQ((*built_in)[194], "acute");
	EXPECT_EQ((*built_in)[201], "Aacute");
	EXPECT_EQ((*built_in)[32], ""); // the `for` loop's .notdef entries are no glyph names

	PdfArray differences;
	differences.push_back(PdfValue(int64_t{66}));
	differences.push_back(PdfValue(PdfName("curves")));
	PdfDict dict;
	dict.set("Differences", PdfValue(std::move(differences)));
	const PdfValue value{std::move(dict)};
	// A symbolic font's /Differences apply over the built-in encoding (Table 114).
	auto symbolic = SimpleEncoding::build(&value, true, built_in);
	ASSERT_TRUE(symbolic.has_value());
	EXPECT_EQ(symbolic->name(65), "A");
	EXPECT_EQ(symbolic->name(66), "curves");
	EXPECT_EQ(symbolic->name(201), "Aacute");
	EXPECT_EQ(symbolic->name(67), "curves");
	// No /Encoding: the built-in one.
	auto plain = SimpleEncoding::build(nullptr, false, built_in);
	ASSERT_TRUE(plain.has_value());
	EXPECT_EQ(plain->name(66), "box");
	EXPECT_EQ(plain->name(201), "Aacute");
	// Every name the encoding gives finds its glyph.
	for (const int code : {65, 66, 67, 68, 69, 70, 194, 201}) {
		EXPECT_TRUE(font->glyph_for_name(symbolic->name(static_cast<uint8_t>(code))).has_value()) << code;
	}

	const auto cff = load_charstrings_ok(corpus_font("cff-outlines.cff"), CharstringFormat::Type1C);
	ASSERT_NE(cff, nullptr);
	auto from_cff = SimpleEncoding::build(nullptr, false, cff->built_in_encoding());
	ASSERT_TRUE(from_cff.has_value());
	EXPECT_EQ(from_cff->name(200), "A");
	EXPECT_EQ(cff->glyph_for_name(from_cff->name(200)), cff->glyph_for_name("A"));
}

} // namespace
} // namespace recto::pdf
