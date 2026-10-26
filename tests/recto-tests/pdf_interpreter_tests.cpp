// Unit tests for the content-stream interpreter's graphics state, paths, painting, clipping and device colour
// (D01 T02 section 3). Each test writes a one-page PDF (media box 0 0 200 200) whose content stream it states, so every
// expected command, point, colour and box below follows by hand from that stream and ISO 32000-1 section 8.
#include "interpreter.h"

#include "color.h"
#include "test_support.h"
#include "lexer.h"

#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace recto::pdf {
namespace {

std::vector<uint8_t> pdf_from_objects(const std::vector<std::string>& objects);

// A one-page PDF: the page's `/Resources` text (or none), its content stream (or none), objects numbered from 5, and
// optionally `/Resources` on the page tree node for the page to inherit.
std::vector<uint8_t> one_page_pdf(std::optional<std::string_view> content, std::string_view page_resources = "<< >>",
	const std::vector<std::string>& extra_objects = {}, std::string_view inherited_resources = {})
{
	std::vector<std::string> objects;
	objects.emplace_back("<< /Type /Catalog /Pages 2 0 R >>");
	std::string pages = "<< /Type /Pages /Kids [3 0 R] /Count 1";
	if (!inherited_resources.empty()) {
		pages += " /Resources ";
		pages += inherited_resources;
	}
	objects.push_back(pages + " >>");
	std::string page = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200]";
	if (!page_resources.empty()) {
		page += " /Resources ";
		page += page_resources;
	}
	if (content.has_value()) {
		page += " /Contents 4 0 R";
	}
	objects.push_back(page + " >>");
	std::string stream = "<< /Length " + std::to_string(content.value_or("").size()) + " >>\nstream\n";
	stream += content.value_or("");
	stream += "\nendstream";
	objects.push_back(stream);
	for (const std::string& extra : extra_objects) {
		objects.push_back(extra);
	}
	return pdf_from_objects(objects);
}

std::vector<uint8_t> pdf_from_objects(const std::vector<std::string>& objects)
{
	std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
	std::vector<size_t> offsets;
	for (size_t i = 0; i < objects.size(); ++i) {
		offsets.push_back(out.size());
		out += std::to_string(i + 1u) + " 0 obj\n" + objects[i] + "\nendobj\n";
	}
	const size_t xref = out.size();
	out += "xref\n0 " + std::to_string(objects.size() + 1u) + "\n0000000000 65535 f \n";
	for (const size_t off : offsets) {
		char entry[32]{};
		std::snprintf(entry, sizeof(entry), "%010llu 00000 n \n", static_cast<unsigned long long>(off));
		out += entry;
	}
	out += "trailer\n<< /Size " + std::to_string(objects.size() + 1u) + " /Root 1 0 R >>\nstartxref\n" +
		std::to_string(xref) + "\n%%EOF\n";
	return std::vector<uint8_t>(out.begin(), out.end());
}

// `data` as a zlib stream of stored (uncompressed) deflate blocks of at most 65,535 bytes (RFC 1950 and 1951), so
// the bytes inside it stay as written: a FlateDecode payload that holds " EI " and operator text.
std::string stored_zlib(std::string_view data)
{
	std::string out = "\x78\x01";
	size_t at = 0;
	do {
		const size_t chunk = std::min<size_t>(data.size() - at, 65'535u);
		const bool last = at + chunk == data.size();
		out += static_cast<char>(last ? 0x01 : 0x00);
		const auto len = static_cast<uint16_t>(chunk);
		out += static_cast<char>(len & 0xFFu);
		out += static_cast<char>(len >> 8);
		out += static_cast<char>(~len & 0xFFu);
		out += static_cast<char>((~len >> 8) & 0xFFu);
		out += data.substr(at, chunk);
		at += chunk;
	} while (at < data.size());
	uint32_t a = 1;
	uint32_t b = 0;
	for (const char c : data) {
		a = (a + static_cast<uint8_t>(c)) % 65521u;
		b = (b + a) % 65521u;
	}
	const uint32_t adler = (b << 16) | a;
	for (int shift = 24; shift >= 0; shift -= 8) {
		out += static_cast<char>((adler >> shift) & 0xFFu);
	}
	return out;
}

struct Interpreted {
	std::vector<uint8_t> bytes;
	std::unique_ptr<Document> document;
	std::shared_ptr<const DisplayList> list;
};

Interpreted interpret(std::vector<uint8_t> bytes, const DisplayListLimits& limits = {})
{
	Interpreted out{std::move(bytes), nullptr, nullptr};
	auto doc = Document::open_memory(out.bytes);
	EXPECT_TRUE(doc.has_value()) << (doc.has_value() ? "" : doc.error().message.view());
	if (!doc.has_value()) {
		return out;
	}
	out.document = std::move(*doc);
	auto list = interpret_page(*out.document, 0u, limits);
	EXPECT_TRUE(list.has_value()) << (list.has_value() ? "" : list.error().message.view());
	if (list.has_value()) {
		out.list = std::move(*list);
	}
	return out;
}

Interpreted interpret(std::string_view content, std::string_view resources = "<< >>",
	const std::vector<std::string>& extra_objects = {})
{
	return interpret(one_page_pdf(content, resources, extra_objects));
}

std::vector<DisplayCommandKind> kinds(const DisplayList& list)
{
	std::vector<DisplayCommandKind> out;
	for (const DisplayCommand& c : list.commands()) {
		out.push_back(c.kind());
	}
	return out;
}

template <typename CmdT>
std::vector<CmdT> all_of(const DisplayList& list)
{
	std::vector<CmdT> out;
	for (const DisplayCommand& c : list.commands()) {
		if (const CmdT* cmd = c.as<CmdT>()) {
			out.push_back(*cmd);
		}
	}
	return out;
}

std::vector<PathVerb> verbs_of(const DisplayList& list, uint32_t path_index)
{
	const auto path = list.path(path_index);
	EXPECT_TRUE(path.has_value());
	if (!path.has_value()) {
		return {};
	}
	const auto verbs = list.path_verbs(*path);
	EXPECT_TRUE(verbs.has_value());
	return verbs.has_value() ? std::vector<PathVerb>(verbs->begin(), verbs->end()) : std::vector<PathVerb>{};
}

std::vector<PdfPoint> points_of(const DisplayList& list, uint32_t path_index)
{
	const auto path = list.path(path_index);
	EXPECT_TRUE(path.has_value());
	if (!path.has_value()) {
		return {};
	}
	const auto points = list.path_points(*path);
	EXPECT_TRUE(points.has_value());
	return points.has_value() ? std::vector<PdfPoint>(points->begin(), points->end()) : std::vector<PdfPoint>{};
}

// The painted box of the `index`-th command.
PdfRect bounds_of(const DisplayList& list, size_t index)
{
	const auto bounds = list.command_bounds();
	EXPECT_LT(index, bounds.size());
	if (index >= bounds.size()) {
		return {};
	}
	EXPECT_TRUE(bounds[index].has_bounds) << "command " << index;
	return bounds[index].bounds_pt;
}

void expect_rect(const PdfRect& r, double x0, double y0, double x1, double y1)
{
	EXPECT_NEAR(r.x_min, x0, 1e-9);
	EXPECT_NEAR(r.y_min, y0, 1e-9);
	EXPECT_NEAR(r.x_max, x1, 1e-9);
	EXPECT_NEAR(r.y_max, y1, 1e-9);
}

void expect_color(const ColorRgba& c, float r, float g, float b, float a = 1.0f)
{
	EXPECT_NEAR(c.r, r, 1e-6f);
	EXPECT_NEAR(c.g, g, 1e-6f);
	EXPECT_NEAR(c.b, b, 1e-6f);
	EXPECT_NEAR(c.a, a, 1e-6f);
}

using K = DisplayCommandKind;

TEST(PdfInterpreter, OperatorDispatch)
{
	// A fill, an unknown operator with operands, a width with a name operand, a stray `]` that spoils the `m` after it
	// (so the `l`s have no current point and the `f` paints nothing), a line with one operand, then a stroke.
	const auto r = interpret(
		"0 0 m 10 0 l 10 10 l f\n"
		"1 2 3 foo\n"
		"/X w\n"
		"] 20 20 m 30 20 l 30 30 l f\n"
		"40 40 m 50 l 5 w 60 40 l S\n");
	ASSERT_NE(r.list, nullptr);
	EXPECT_EQ(kinds(*r.list), (std::vector<K>{K::DrawPath, K::SetStrokeStyle, K::DrawPath}));
	const auto styles = all_of<SetStrokeStyleCommand>(*r.list);
	ASSERT_EQ(styles.size(), 1u);
	EXPECT_FLOAT_EQ(styles[0].line_width, 5.0f); // `/X w` changed nothing; `5 w` did
	const auto draws = all_of<DrawPathCommand>(*r.list);
	ASSERT_EQ(draws.size(), 2u);
	EXPECT_TRUE(draws[0].fill);
	EXPECT_FALSE(draws[0].stroke);
	EXPECT_TRUE(draws[1].stroke);
	// The stroke's path is `40 40 m 60 40 l`: the one-operand `l` was skipped.
	EXPECT_EQ(verbs_of(*r.list, draws[1].path_index), (std::vector<PathVerb>{PathVerb::MoveTo, PathVerb::LineTo}));
	EXPECT_EQ(points_of(*r.list, draws[1].path_index), (std::vector<PdfPoint>{{40.0, 40.0}, {60.0, 40.0}}));

	// An inline image's data is skipped to its `EI`, so the operators inside it are never read.
	const auto inline_image = interpret("BI /W 13 /H 1 /BPC 8 /CS /G ID 0 0 m 5 5 l S EI 0 0 10 10 re f");
	ASSERT_NE(inline_image.list, nullptr);
	EXPECT_EQ(kinds(*inline_image.list), (std::vector<K>{K::DrawPath}));
	EXPECT_TRUE(all_of<DrawPathCommand>(*inline_image.list)[0].fill);

	// Pixel data holding " EI " and operator-shaped bytes: an unfiltered image is skipped by its length (24 bytes of
	// 8-bit gray), so neither the false `EI` nor the `re f` inside the data is read.
	const std::string pixels = " EI 0 0 100 100 re f    ";
	ASSERT_EQ(pixels.size(), 24u);
	const auto by_length = interpret("BI /W 24 /H 1 /BPC 8 /CS /G ID " + pixels + "\nEI 0 0 10 10 re S");
	ASSERT_NE(by_length.list, nullptr);
	EXPECT_EQ(kinds(*by_length.list), (std::vector<K>{K::DrawPath}));
	EXPECT_FALSE(all_of<DrawPathCommand>(*by_length.list)[0].fill);
	// A writer that put CR LF after `ID` still has its data measured from the byte after the LF.
	const auto crlf = interpret("BI /W 2 /H 1 /BPC 8 /CS /G ID\r\nab EI 0 0 10 10 re f");
	ASSERT_NE(crlf.list, nullptr);
	EXPECT_EQ(kinds(*crlf.list), (std::vector<K>{K::DrawPath}));

	// A Flate image whose decoded pixels hold " EI " and `re f` is framed by decoding: the `EI` inside the data truncates
	// the stream, so only the `EI` after it, where the data decodes to all 62 samples, ends the image.
	const std::string flate_pixels = std::string(" EI 0 0 100 100 re f ") + std::string(40, ' ') + "\x80";
	ASSERT_EQ(flate_pixels.size(), 62u);
	const auto framed = interpret("BI /W 62 /H 1 /BPC 8 /CS /G /F /Fl ID " + stored_zlib(flate_pixels) + "\nEI 0 0 10 10 re S");
	ASSERT_NE(framed.list, nullptr);
	EXPECT_EQ(kinds(*framed.list), (std::vector<K>{K::DrawPath}));
	EXPECT_FALSE(all_of<DrawPathCommand>(*framed.list)[0].fill);

	// However many false `EI`s the pixels hold, each one truncates the stream and the real one still ends the image.
	const std::string many_pixels = std::string(" EI 0 0 100 100 re f ") + std::string(40, ' ') + [] {
		std::string s;
		for (int i = 0; i < 16; ++i) {
			s += " EI ";
		}
		return s;
	}() + "\x80";
	ASSERT_EQ(many_pixels.size(), 126u);
	const auto many = interpret("BI /W 126 /H 1 /BPC 8 /CS /G /F /Fl ID " + stored_zlib(many_pixels) + "\nEI 0 0 10 10 re S");
	ASSERT_NE(many.list, nullptr);
	EXPECT_EQ(kinds(*many.list), (std::vector<K>{K::DrawPath}));
	EXPECT_FALSE(all_of<DrawPathCommand>(*many.list)[0].fill);

	// Data longer than its dictionary says (5,062 bytes for one sample) still frames by decoding, inside the padding
	// allowance, so the false `EI` in it is not taken.
	const std::string long_pixels = std::string(" EI 0 0 100 100 re f ") + std::string(40, ' ') + "\x80" + std::string(5'000, 'x');
	const auto padded = interpret("BI /W 1 /H 1 /BPC 8 /CS /G /F /Fl ID " + stored_zlib(long_pixels) + " EI 0 0 10 10 re S");
	ASSERT_NE(padded.list, nullptr);
	EXPECT_EQ(kinds(*padded.list), (std::vector<K>{K::DrawPath}));
	EXPECT_FALSE(all_of<DrawPathCommand>(*padded.list)[0].fill);

	// An operator that takes no operands, given one, is skipped like any operator with the wrong operands: each case
	// below would add a save, a restore, a close, a clip or a paint before the `2 w S` if it ran.
	for (const std::string_view op : {"q", "Q", "h", "W", "W*", "S", "s", "f", "F", "f*", "B", "B*", "b", "b*", "n"}) {
		SCOPED_TRACE(std::string(op));
		const auto stray = interpret("q 0 0 m 10 0 l 10 10 l 7 " + std::string(op) + " 2 w S Q");
		ASSERT_NE(stray.list, nullptr);
		EXPECT_EQ(kinds(*stray.list), (std::vector<K>{K::SaveState, K::SetStrokeStyle, K::DrawPath, K::RestoreState}));
		const auto stray_draws = all_of<DrawPathCommand>(*stray.list);
		ASSERT_EQ(stray_draws.size(), 1u);
		EXPECT_FALSE(stray_draws[0].fill);
		EXPECT_EQ(verbs_of(*stray.list, stray_draws[0].path_index), (std::vector<PathVerb>{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo}));
	}

	// A width past the float range the display list keeps is skipped, so the stroke keeps the default width.
	const auto wide = interpret("350000000000000000000000000000000000000.0 w 0 0 m 10 0 l S");
	ASSERT_NE(wide.list, nullptr);
	EXPECT_EQ(kinds(*wide.list), (std::vector<K>{K::DrawPath}));

	// A matrix whose product overflows the page's coordinates: the builder refuses that fill, which is skipped like a
	// malformed operator, and the page goes on.
	const std::string huge = "3" + std::string(307, '0') + ".0";
	const auto overflow = interpret("q " + huge + " 0 0 " + huge + " 0 0 cm 0 0 10 10 re f Q 0 0 10 10 re f");
	ASSERT_NE(overflow.list, nullptr);
	EXPECT_EQ(kinds(*overflow.list), (std::vector<K>{K::SaveState, K::ConcatMatrix, K::RestoreState, K::DrawPath}));
}

TEST(PdfInterpreter, EntryContract)
{
	// No `/Contents`, and an empty one: a valid empty list.
	for (const std::optional<std::string_view> content : {std::optional<std::string_view>{}, std::optional<std::string_view>{""}}) {
		const auto r = interpret(one_page_pdf(content));
		ASSERT_NE(r.list, nullptr);
		EXPECT_TRUE(r.list->commands().empty());
		expect_rect(r.list->page_box_pt(), 0.0, 0.0, 200.0, 200.0);
	}

	const std::vector<uint8_t> bytes = one_page_pdf("0 0 10 10 re f");
	auto doc = Document::open_memory(bytes);
	ASSERT_TRUE(doc.has_value());
	// An index past the last page is the caller's mistake.
	const auto past = interpret_page(**doc, 1u);
	ASSERT_FALSE(past.has_value());
	EXPECT_EQ(past.error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(past.error().refusal_reason, RefusalReason::PageIndex);
	EXPECT_EQ(past.error().observed, std::optional<uint64_t>{1u});
	EXPECT_EQ(past.error().bound, std::optional<uint64_t>{0u});

	// A content stream that does not decode returns its error, not an empty page. Page 0's contents are decoded when
	// the document opens, so the broken stream is page 1's.
	const std::vector<uint8_t> broken = pdf_from_objects({
		"<< /Type /Catalog /Pages 2 0 R >>",
		"<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>",
		"<< /Length 13 >>\nstream\n0 0 10 10 re f\nendstream",
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 6 0 R >>",
		"<< /Length 22 /Filter /FlateDecode >>\nstream\nthis is not flate data\nendstream",
	});
	auto broken_doc = Document::open_memory(broken);
	ASSERT_TRUE(broken_doc.has_value());
	ASSERT_TRUE(interpret_page(**broken_doc, 0u).has_value());
	const auto undecodable = interpret_page(**broken_doc, 1u);
	ASSERT_FALSE(undecodable.has_value());
	EXPECT_EQ(undecodable.error().code, ErrorCode::DamagedFormat);

	// A lexer that runs out of memory stops the page with `OutOfMemory`, not a skipped token.
	const std::vector<uint8_t> long_string = one_page_pdf("(" + std::string(4096, 'a') + ") Tj 0 0 10 10 re f");
	auto long_doc = Document::open_memory(long_string);
	ASSERT_TRUE(long_doc.has_value());
	{
		detail::ScopedLexerAllocFaultInjector fail_first(0);
		const auto starved = interpret_page(**long_doc, 0u);
		ASSERT_FALSE(starved.has_value());
		EXPECT_EQ(starved.error().code, ErrorCode::OutOfMemory);
	}
	ASSERT_TRUE(interpret_page(**long_doc, 0u).has_value());

	// A refusal that is not damaged content (operands nested past the parser's depth) stops the page with its code.
	const std::vector<uint8_t> nested = one_page_pdf("0 0 10 10 re f " + std::string(300, '[') + std::string(300, ']') + " 0 0 5 5 re f");
	auto nested_doc = Document::open_memory(nested);
	ASSERT_TRUE(nested_doc.has_value());
	const auto too_deep = interpret_page(**nested_doc, 0u);
	ASSERT_FALSE(too_deep.has_value());
	EXPECT_EQ(too_deep.error().code, ErrorCode::RecursionLimitExceeded);

	// An inline image whose end cannot be verified refuses the page instead of guessing an `EI`, which could run its
	// pixels as operators: filtered data that decodes at no `EI` (garbage, and the review's stored block with a broken
	// checksum, whose embedded `EI` is followed by printable text), and an image whose size is unknown.
	std::string damaged_checksum = stored_zlib(std::string(" EI 0 0 100 100 re f ") + std::string(40, ' ') + "\x80");
	damaged_checksum.back() = static_cast<char>(damaged_checksum.back() ^ 1);
	for (const std::string& content : {
			 std::string("BI /W 2 /H 2 /BPC 8 /CS /G /F /Fl ID \x01 EI \x80 0 0 100 100 re f \x90 EI\n0 0 10 10 re S"),
			 "BI /W 62 /H 1 /BPC 8 /CS /G /F /Fl ID " + damaged_checksum + " EI 0 0 10 10 re S",
			 std::string("BI /W 4 /H 1 /CS /G ID 0 0 m EI 0 0 10 10 re S"),
		 }) {
		const std::vector<uint8_t> unverified = one_page_pdf(content);
		auto unverified_doc = Document::open_memory(unverified);
		ASSERT_TRUE(unverified_doc.has_value());
		const auto guessed = interpret_page(**unverified_doc, 0u);
		ASSERT_FALSE(guessed.has_value()) << content.substr(0, 40);
		EXPECT_EQ(guessed.error().code, ErrorCode::DamagedFormat);
	}

	// A filtered inline image whose end no candidate confirms within the 64 MiB decoding budget refuses the page, never
	// falls back to an unchecked `EI`.
	std::string candidates;
	for (int i = 0; i < 13'000; ++i) {
		candidates += " EI";
	}
	const std::vector<uint8_t> unframed = one_page_pdf("BI /W 1 /H 1 /BPC 8 /CS /G /F /Fl ID" + candidates + " 0 0 10 10 re f");
	auto unframed_doc = Document::open_memory(unframed);
	ASSERT_TRUE(unframed_doc.has_value());
	const auto refused = interpret_page(**unframed_doc, 0u);
	ASSERT_FALSE(refused.has_value());
	EXPECT_EQ(refused.error().code, ErrorCode::ResourceLimit);
	EXPECT_EQ(refused.error().refusal_reason, RefusalReason::InlineImageFraming);

	// Data that decodes past the padding allowance (70,000 bytes for one sample) refuses the page with the decoder's
	// limit instead of framing the image unchecked.
	const std::vector<uint8_t> overlong = one_page_pdf("BI /W 1 /H 1 /BPC 8 /CS /G /F /Fl ID " + stored_zlib(std::string(70'000, 'x')) +
		" EI 0 0 10 10 re S");
	auto overlong_doc = Document::open_memory(overlong);
	ASSERT_TRUE(overlong_doc.has_value());
	const auto over = interpret_page(**overlong_doc, 0u);
	ASSERT_FALSE(over.has_value());
	EXPECT_EQ(over.error().code, ErrorCode::DecompressionLimitExceeded);

	// A resource that fails to resolve for a reason other than damage (nesting past the parser's depth) stops the page,
	// whether `gs` names it or an inline image's colour space does.
	const std::string deep_object = std::string(300, '[') + std::string(300, ']');
	for (const std::string_view content : {std::string_view("/A gs 0 0 10 10 re f"), std::string_view("BI /W 1 /H 1 /BPC 8 /CS /C ID x EI")}) {
		SCOPED_TRACE(std::string(content));
		const std::vector<uint8_t> deep_bytes = one_page_pdf(content, "<< /ExtGState << /A 5 0 R >> /ColorSpace << /C 5 0 R >> >>", {deep_object});
		auto deep_doc = Document::open_memory(deep_bytes);
		ASSERT_TRUE(deep_doc.has_value());
		const auto failed = interpret_page(**deep_doc, 0u);
		ASSERT_FALSE(failed.has_value());
		EXPECT_EQ(failed.error().code, ErrorCode::RecursionLimitExceeded);
	}

	// A recording failure is returned, never a partial page reported as success.
	DisplayListLimits failing{};
	failing.fail_allocation_after_ops = 3u;
	const auto injected = interpret_page(**doc, 0u, failing);
	ASSERT_FALSE(injected.has_value());
	EXPECT_EQ(injected.error().code, ErrorCode::OutOfMemory);
	const std::vector<uint8_t> three = one_page_pdf("0 0 10 10 re f 0 0 10 10 re f 0 0 10 10 re f");
	auto three_doc = Document::open_memory(three);
	ASSERT_TRUE(three_doc.has_value());
	DisplayListLimits two_commands{};
	two_commands.max_commands = 2u;
	const auto limited = interpret_page(**three_doc, 0u, two_commands);
	ASSERT_FALSE(limited.has_value());
	EXPECT_EQ(limited.error().code, ErrorCode::ResourceLimit);
}

TEST(PdfInterpreter, GraphicsStateStack)
{
	// Every field the state keeps is changed inside `q ... Q`, then a stroke after `Q` paints with the initial state.
	const auto r = interpret(
		"q 2 w 1 J 1 j 4 M [3 1] 2 d /A gs 1 0 0 RG 0 0 1 rg 1 0 0 1 50 50 cm\n"
		"0 0 m 10 0 l S Q\n"
		"0 0 m 10 0 l B\n",
		"<< /ExtGState << /A << /CA 0.5 /ca 0.25 /BM /Multiply >> >> >>");
	ASSERT_NE(r.list, nullptr);
	EXPECT_EQ(kinds(*r.list), (std::vector<K>{K::SaveState, K::ConcatMatrix, K::SetStrokeStyle, K::SetAlphaAndBlend,
								  K::DrawPath, K::RestoreState, K::DrawPath}));
	const auto draws = all_of<DrawPathCommand>(*r.list);
	ASSERT_EQ(draws.size(), 2u);
	expect_color(draws[0].stroke_color, 1.0f, 0.0f, 0.0f);
	expect_color(draws[1].stroke_color, 0.0f, 0.0f, 0.0f);
	expect_color(draws[1].fill_color, 0.0f, 0.0f, 0.0f);
	// Inside: translated by (50, 50) and widened by 1 (half of 2) with round caps; after Q: untransformed, half of 1.
	expect_rect(bounds_of(*r.list, 4), 49.0, 49.0, 61.0, 51.0);
	expect_rect(bounds_of(*r.list, 6), 0.0, 0.0, 10.0, 0.5);

	// `cm` order: `T S` (translate, then scale) maps x to 2x + 10; `S T` maps it to 2(x + 10).
	const auto ts = interpret("1 0 0 1 10 0 cm 2 0 0 2 0 0 cm 0 0 10 10 re f");
	ASSERT_NE(ts.list, nullptr);
	expect_rect(bounds_of(*ts.list, 2), 10.0, 0.0, 30.0, 20.0);
	const auto st = interpret("2 0 0 2 0 0 cm 1 0 0 1 10 0 cm 0 0 10 10 re f");
	ASSERT_NE(st.list, nullptr);
	expect_rect(bounds_of(*st.list, 2), 20.0, 0.0, 40.0, 20.0);
	// A quarter turn about the origin then (100, 0): (x, y) maps to (100 - y, x); a clip placed the same way.
	const auto rot = interpret("q 0 1 -1 0 100 0 cm 0 0 10 20 re W n 0 0 10 20 re f Q 0 0 10 20 re f");
	ASSERT_NE(rot.list, nullptr);
	EXPECT_EQ(kinds(*rot.list), (std::vector<K>{K::SaveState, K::ConcatMatrix, K::PushClipPath, K::DrawPath,
									K::RestoreState, K::DrawPath}));
	expect_rect(bounds_of(*rot.list, 3), 80.0, 0.0, 100.0, 10.0);
	expect_rect(bounds_of(*rot.list, 5), 0.0, 0.0, 10.0, 20.0); // Q undid both the matrix and the clip

	// 70 nested `q`: the 64 kept are recorded and the 6 past the cap are ignored with their `Q`s, so the state stays
	// balanced; an unmatched `Q` after them is ignored too.
	std::string nested;
	for (int i = 0; i < 70; ++i) {
		nested += "q ";
	}
	nested += "3 w ";
	for (int i = 0; i < 70; ++i) {
		nested += "Q ";
	}
	nested += "Q 0 0 m 10 0 l S";
	const auto deep = interpret(nested);
	ASSERT_NE(deep.list, nullptr);
	EXPECT_EQ(all_of<SaveStateCommand>(*deep.list).size(), 64u);
	EXPECT_EQ(all_of<RestoreStateCommand>(*deep.list).size(), 64u);
	EXPECT_TRUE(all_of<SetStrokeStyleCommand>(*deep.list).empty()); // `3 w` was undone before the stroke
	expect_rect(bounds_of(*deep.list, deep.list->commands().size() - 1u), 0.0, 0.0, 10.0, 0.5);

	// The `Q`s that match ignored `q`s restore nothing: after 64 kept and 6 ignored levels, six `Q`s leave the state
	// at the innermost kept level, so the stroke has the width set there and no frame was popped.
	std::string inside;
	for (int i = 0; i < 64; ++i) {
		inside += "q ";
	}
	inside += "2 w q q q q q q 3 w Q Q Q Q Q Q 0 0 m 10 0 l S";
	const auto held = interpret(inside);
	ASSERT_NE(held.list, nullptr);
	EXPECT_TRUE(all_of<RestoreStateCommand>(*held.list).empty());
	const auto held_styles = all_of<SetStrokeStyleCommand>(*held.list);
	ASSERT_EQ(held_styles.size(), 1u);
	EXPECT_FLOAT_EQ(held_styles[0].line_width, 3.0f);

	// A caller that lowers the builder's save depth lowers the cap with it: the extra `q`s are ignored, not a failure.
	DisplayListLimits shallow{};
	shallow.max_save_depth = 4u;
	const auto lowered = interpret(one_page_pdf("q q q q q q 0 0 10 10 re f Q Q Q Q Q Q"), shallow);
	ASSERT_NE(lowered.list, nullptr);
	EXPECT_EQ(all_of<SaveStateCommand>(*lowered.list).size(), 4u);
	EXPECT_EQ(all_of<RestoreStateCommand>(*lowered.list).size(), 4u);
}

TEST(PdfInterpreter, ExtGState)
{
	const auto r = interpret("/G1 gs 0 0 m 10 0 l S /G2 gs /Missing gs /G3 gs 0 0 m 10 0 l S",
		"<< /ExtGState << /G1 << /LW 4 /LC 1 /LJ 2 /ML 3 /D [[2 1] 1] /CA 0.5 /ca 0.25 /BM /Multiply >> "
		"/G2 5 0 R /G3 << /BM /Bogus /CA 2 >> >> >>",
		{"<< /BM [/Bogus /Screen] /LW 6 0 R >>", "7"});
	ASSERT_NE(r.list, nullptr);
	EXPECT_EQ(kinds(*r.list), (std::vector<K>{K::SetStrokeStyle, K::SetAlphaAndBlend, K::DrawPath, K::SetStrokeStyle,
								  K::SetAlphaAndBlend, K::DrawPath}));
	const auto styles = all_of<SetStrokeStyleCommand>(*r.list);
	ASSERT_EQ(styles.size(), 2u);
	EXPECT_FLOAT_EQ(styles[0].line_width, 4.0f);
	EXPECT_EQ(styles[0].line_cap, LineCap::Round);
	EXPECT_EQ(styles[0].line_join, LineJoin::Bevel);
	EXPECT_FLOAT_EQ(styles[0].miter_limit, 3.0f);
	EXPECT_FLOAT_EQ(styles[0].dash_phase, 1.0f);
	const auto dash = r.list->dash_values(styles[0]);
	ASSERT_TRUE(dash.has_value());
	EXPECT_EQ(std::vector<float>(dash->begin(), dash->end()), (std::vector<float>{2.0f, 1.0f}));
	EXPECT_FLOAT_EQ(styles[1].line_width, 7.0f); // through the indirect `/G2` and its indirect `/LW`
	const auto alphas = all_of<SetAlphaAndBlendCommand>(*r.list);
	ASSERT_EQ(alphas.size(), 2u);
	EXPECT_FLOAT_EQ(alphas[0].stroke_alpha, 0.5f);
	EXPECT_FLOAT_EQ(alphas[0].fill_alpha, 0.25f);
	EXPECT_EQ(alphas[0].blend_mode, BlendMode::Multiply);
	// `/G3`: an unknown blend name is Normal, and an alpha past 1 is 1; `/Missing` is not in the resources.
	EXPECT_FLOAT_EQ(alphas[1].stroke_alpha, 1.0f);
	EXPECT_EQ(alphas[1].blend_mode, BlendMode::Normal);

	// Indirect elements inside `/D` and `/BM` arrays (ISO 32000-1 7.3.10): the dash array, one dash value and a blend
	// name each through a reference.
	const auto indirect = interpret("/A gs 10 10 m 100 10 l S", "<< /ExtGState << /A << /D [5 0 R 1] /BM [6 0 R /Normal] >> >> >>",
		{"[7 0 R 2]", "/Multiply", "3"});
	ASSERT_NE(indirect.list, nullptr);
	const auto indirect_styles = all_of<SetStrokeStyleCommand>(*indirect.list);
	ASSERT_EQ(indirect_styles.size(), 1u);
	const auto indirect_dash = indirect.list->dash_values(indirect_styles[0]);
	ASSERT_TRUE(indirect_dash.has_value());
	EXPECT_EQ(std::vector<float>(indirect_dash->begin(), indirect_dash->end()), (std::vector<float>{3.0f, 2.0f}));
	EXPECT_FLOAT_EQ(indirect_styles[0].dash_phase, 1.0f);
	const auto indirect_alpha = all_of<SetAlphaAndBlendCommand>(*indirect.list);
	ASSERT_EQ(indirect_alpha.size(), 1u);
	EXPECT_EQ(indirect_alpha[0].blend_mode, BlendMode::Multiply);

	// `/G2` alone: the first blend name this reader knows in the array.
	const auto screen = interpret("/G2 gs 0 0 10 10 re f", "<< /ExtGState << /G2 << /BM [/Bogus /Screen /Multiply] >> >> >>");
	ASSERT_NE(screen.list, nullptr);
	const auto screen_alpha = all_of<SetAlphaAndBlendCommand>(*screen.list);
	ASSERT_EQ(screen_alpha.size(), 1u);
	EXPECT_EQ(screen_alpha[0].blend_mode, BlendMode::Screen);
}

TEST(PdfInterpreter, PathOperators)
{
	// `l` before any `m` is skipped; `v` takes the current point as its first control point and `y` its end point as
	// the second; `h` returns to (10, 20); `re` is one closed subpath and the `l` after it starts at its origin.
	const auto r = interpret("99 99 l 10 20 m 30 40 l 1 2 3 4 5 6 c 7 8 9 10 v 11 12 13 14 y h 50 60 70 80 re 0 0 l f");
	ASSERT_NE(r.list, nullptr);
	const auto draws = all_of<DrawPathCommand>(*r.list);
	ASSERT_EQ(draws.size(), 1u);
	EXPECT_EQ(verbs_of(*r.list, draws[0].path_index),
		(std::vector<PathVerb>{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::CubicBezierTo, PathVerb::CubicBezierTo,
			PathVerb::CubicBezierTo, PathVerb::CloseSubpath, PathVerb::Rect, PathVerb::MoveTo, PathVerb::LineTo}));
	EXPECT_EQ(points_of(*r.list, draws[0].path_index),
		(std::vector<PdfPoint>{{10, 20}, {30, 40}, {1, 2}, {3, 4}, {5, 6}, {5, 6}, {7, 8}, {9, 10}, {11, 12}, {13, 14},
			{13, 14}, {50, 60}, {70, 80}, {50, 60}, {0, 0}}));

	// A subpath closed by `h` is not closed again by `s`, and a segment after `h` starts at the closed subpath's start.
	const auto closed_once = interpret("10 w 1 J 50 50 m 100 50 l h s");
	ASSERT_NE(closed_once.list, nullptr);
	const auto once_draws = all_of<DrawPathCommand>(*closed_once.list);
	ASSERT_EQ(once_draws.size(), 1u);
	EXPECT_EQ(verbs_of(*closed_once.list, once_draws[0].path_index),
		(std::vector<PathVerb>{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::CloseSubpath}));
	const auto after_close = interpret("0 0 m 10 0 l h 10 10 l S");
	ASSERT_NE(after_close.list, nullptr);
	const auto after_draws = all_of<DrawPathCommand>(*after_close.list);
	ASSERT_EQ(after_draws.size(), 1u);
	EXPECT_EQ(verbs_of(*after_close.list, after_draws[0].path_index),
		(std::vector<PathVerb>{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::CloseSubpath, PathVerb::MoveTo, PathVerb::LineTo}));
	EXPECT_EQ(points_of(*after_close.list, after_draws[0].path_index), (std::vector<PdfPoint>{{0, 0}, {10, 0}, {0, 0}, {10, 10}}));

	// A rectangle is already closed: `h` and `s` after it add no close.
	const auto closed = interpret("0 0 10 10 re h s");
	ASSERT_NE(closed.list, nullptr);
	const auto closed_draws = all_of<DrawPathCommand>(*closed.list);
	ASSERT_EQ(closed_draws.size(), 1u);
	EXPECT_EQ(verbs_of(*closed.list, closed_draws[0].path_index), (std::vector<PathVerb>{PathVerb::Rect}));
}

TEST(PdfInterpreter, ClippingPaths)
{
	// `W n`: a clip and no paint.
	const auto clip_only = interpret("0 0 100 100 re W n");
	ASSERT_NE(clip_only.list, nullptr);
	EXPECT_EQ(kinds(*clip_only.list), (std::vector<K>{K::PushClipPath}));

	// `W S`: the stroke is painted with the clip in force before it, so it keeps its ink outside the new clip; the
	// fill after it is bounded by the clip.
	const auto stroke = interpret("20 20 50 50 re W 10 w 1 j S 60 60 30 30 re f");
	ASSERT_NE(stroke.list, nullptr);
	EXPECT_EQ(kinds(*stroke.list), (std::vector<K>{K::SetStrokeStyle, K::DrawPath, K::PushClipPath, K::DrawPath}));
	expect_rect(bounds_of(*stroke.list, 1), 15.0, 15.0, 75.0, 75.0);
	expect_rect(bounds_of(*stroke.list, 3), 60.0, 60.0, 70.0, 70.0);
	const auto clips = all_of<PushClipPathCommand>(*stroke.list);
	ASSERT_EQ(clips.size(), 1u);
	EXPECT_EQ(clips[0].fill_rule, FillRule::NonZero);
	EXPECT_EQ(clips[0].path_index, all_of<DrawPathCommand>(*stroke.list)[0].path_index);

	// `W* f`: an even-odd clip, a non-zero fill.
	const auto even_odd = interpret("0 0 100 100 re 25 25 50 50 re W* f");
	ASSERT_NE(even_odd.list, nullptr);
	EXPECT_EQ(kinds(*even_odd.list), (std::vector<K>{K::DrawPath, K::PushClipPath}));
	EXPECT_EQ(all_of<DrawPathCommand>(*even_odd.list)[0].fill_rule, FillRule::NonZero);
	EXPECT_EQ(all_of<PushClipPathCommand>(*even_odd.list)[0].fill_rule, FillRule::EvenOdd);

	// A pending clip ends with its path: the `n` after the next path pushes nothing.
	const auto once = interpret("0 0 10 10 re W n 0 0 20 20 re n");
	ASSERT_NE(once.list, nullptr);
	EXPECT_EQ(kinds(*once.list), (std::vector<K>{K::PushClipPath}));
}

TEST(PdfInterpreter, PaintingOperators)
{
	struct Case {
		std::string_view op;
		bool fill;
		bool stroke;
		FillRule rule;
		bool closes;
	};
	const Case cases[] = {
		{"S", false, true, FillRule::NonZero, false},
		{"s", false, true, FillRule::NonZero, true},
		{"f", true, false, FillRule::NonZero, false},
		{"F", true, false, FillRule::NonZero, false},
		{"f*", true, false, FillRule::EvenOdd, false},
		{"B", true, true, FillRule::NonZero, false},
		{"B*", true, true, FillRule::EvenOdd, false},
		{"b", true, true, FillRule::NonZero, true},
		{"b*", true, true, FillRule::EvenOdd, true},
	};
	for (const Case& c : cases) {
		SCOPED_TRACE(std::string(c.op));
		// Two subpaths, then the operator, then a second paint of the same kind that must find the path cleared.
		const std::string content = "0 0 m 100 0 l 100 100 l 0 0 m 50 0 l 50 50 l " + std::string(c.op) + " " + std::string(c.op);
		const auto r = interpret(content);
		ASSERT_NE(r.list, nullptr);
		const auto draws = all_of<DrawPathCommand>(*r.list);
		ASSERT_EQ(draws.size(), 1u);
		EXPECT_EQ(draws[0].fill, c.fill);
		EXPECT_EQ(draws[0].stroke, c.stroke);
		if (c.fill) {
			EXPECT_EQ(draws[0].fill_rule, c.rule);
		}
		std::vector<PathVerb> expected{PathVerb::MoveTo, PathVerb::LineTo, PathVerb::LineTo, PathVerb::MoveTo,
			PathVerb::LineTo, PathVerb::LineTo};
		if (c.closes) {
			expected.push_back(PathVerb::CloseSubpath);
		}
		EXPECT_EQ(verbs_of(*r.list, draws[0].path_index), expected);
	}
	// A path ended by `n` with no clip is not stored: a thousand of them leave only the visible path's geometry, and fit
	// a list allowed a single path.
	std::string discarded;
	for (int i = 0; i < 1000; ++i) {
		discarded += "0 0 10 10 re n ";
	}
	discarded += "0 0 5 5 re f";
	DisplayListLimits one_path{};
	one_path.max_paths = 1u;
	const auto kept = interpret(one_page_pdf(discarded), one_path);
	ASSERT_NE(kept.list, nullptr);
	EXPECT_EQ(kinds(*kept.list), (std::vector<K>{K::DrawPath}));
	EXPECT_EQ(kept.list->path_points().size(), 2u);

	// `n` ends the path without painting, so a fill after it has nothing to paint.
	const auto ended = interpret("0 0 10 10 re n f");
	ASSERT_NE(ended.list, nullptr);
	EXPECT_TRUE(ended.list->commands().empty());
}

TEST(PdfInterpreter, DeviceColors)
{
	// The device conversions themselves: DeviceCMYK through the formula, as when no CMYK profile loads (section 32
	// draws it through a profile when one does, tested in PdfInterpreter.CalibratedCmyk).
	detail::set_default_cmyk_profile_enabled_for_testing(false);
	struct Restore {
		~Restore()
		{
			detail::set_default_cmyk_profile_enabled_for_testing(true);
		}
	} restore;
	struct Case {
		std::string_view ops;
		bool stroking;
		ColorRgba expected;
	};
	const Case cases[] = {
		{"0.5 g", false, {0.5f, 0.5f, 0.5f}},
		{"0.2 G", true, {0.2f, 0.2f, 0.2f}},
		{"1 0 0 rg", false, {1.0f, 0.0f, 0.0f}},
		{"0 1 0 RG", true, {0.0f, 1.0f, 0.0f}},
		{"0 0 0 1 k", false, {0.0f, 0.0f, 0.0f}},
		{"1 0 0 0 K", true, {0.0f, 1.0f, 1.0f}},           // cyan: 1 - min(1, c + k) per channel
		{"0.25 0.5 0 0.25 k", false, {0.5f, 0.25f, 0.75f}}, // magenta and black together
		{"0.5 0.5 0.5 rg /DeviceRGB cs", false, {0.0f, 0.0f, 0.0f}}, // `cs` resets to the space's initial colour
		{"0 1 0 rg /DeviceCMYK cs", false, {0.0f, 0.0f, 0.0f}},     // CMYK starts at 0 0 0 1
		{"/DeviceGray CS 0.7 SC", true, {0.7f, 0.7f, 0.7f}},
		{"/DeviceRGB cs 0.1 0.2 0.3 scn", false, {0.1f, 0.2f, 0.3f}},
		{"/DeviceRGB cs 0.1 0.2 sc", false, {0.0f, 0.0f, 0.0f}}, // the wrong number of components is skipped
		{"/C1 cs 0.4 0.5 0.6 sc", false, {0.4f, 0.5f, 0.6f}},   // a named space naming DeviceRGB
		{"/C2 CS 0 0 0 0.5 SC", true, {0.5f, 0.5f, 0.5f}},      // an indirect one naming DeviceCMYK
		{"0.5 g /I cs 0 sc", false, {0.0f, 0.0f, 0.0f}},          // a space section 33 reads paints black until then
	};
	for (const Case& c : cases) {
		SCOPED_TRACE(std::string(c.ops));
		const auto r = interpret(std::string(c.ops) + " 0 0 m 10 0 l 10 10 l B",
			"<< /ColorSpace << /C1 /DeviceRGB /C2 5 0 R /I [/Indexed /DeviceRGB 0 <FF0000>] >> >>", {"/DeviceCMYK"});
		ASSERT_NE(r.list, nullptr);
		const auto draws = all_of<DrawPathCommand>(*r.list);
		ASSERT_EQ(draws.size(), 1u);
		const ColorRgba& got = c.stroking ? draws[0].stroke_color : draws[0].fill_color;
		expect_color(got, c.expected.r, c.expected.g, c.expected.b);
		const ColorRgba& other = c.stroking ? draws[0].fill_color : draws[0].stroke_color;
		expect_color(other, 0.0f, 0.0f, 0.0f);
	}

	// Inherited resources: the page has none of its own and takes the page tree node's `/ColorSpace`.
	const auto inherited = interpret(one_page_pdf("/C1 cs 0.4 0.5 0.6 sc 0 0 10 10 re f", {}, {}, "<< /ColorSpace << /C1 /DeviceRGB >> >>"));
	ASSERT_NE(inherited.list, nullptr);
	const auto draws = all_of<DrawPathCommand>(*inherited.list);
	ASSERT_EQ(draws.size(), 1u);
	expect_color(draws[0].fill_color, 0.4f, 0.5f, 0.6f);
}


// ---------------------------------------------------------------------------------------------------------------------
// Colour spaces (D01 T02 section 32). Reference values: LittleCMS 2.19 (Pillow's ImageCms, its transforms built with
// `Flags.NOOPTIMIZE`, so no precalculated table rounds them) for Lab and for the
// generated profiles in `tests/corpus/color/`, to within 1/255 where Recto computes the colour itself and 2/255 where
// Windows' colour engine does; closed forms of ISO 32000-1 8.6.5 elsewhere, stated beside each case.
// ---------------------------------------------------------------------------------------------------------------------

std::string corpus_text(std::string_view relative)
{
	std::ifstream in(recto::test::corpus_path(relative), std::ios::binary);
	EXPECT_TRUE(in.good()) << relative;
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A stream object holding `data`, with `dict_entries` in its dictionary.
std::string stream_object(std::string_view dict_entries, std::string_view data)
{
	return "<< " + std::string(dict_entries) + " /Length " + std::to_string(data.size()) + " >>\nstream\n" +
		std::string(data) + "\nendstream";
}

struct Rgb8 {
	int r;
	int g;
	int b;
};

void expect_rgb8(const ColorRgba& c, Rgb8 want, int tolerance, std::string_view label)
{
	EXPECT_LE(std::abs(static_cast<int>(std::lround(c.r * 255.0f)) - want.r), tolerance) << label << " red " << c.r * 255.0f;
	EXPECT_LE(std::abs(static_cast<int>(std::lround(c.g * 255.0f)) - want.g), tolerance) << label << " green " << c.g * 255.0f;
	EXPECT_LE(std::abs(static_cast<int>(std::lround(c.b * 255.0f)) - want.b), tolerance) << label << " blue " << c.b * 255.0f;
}

// The one painted path's colours.
DrawPathCommand painted(const Interpreted& r)
{
	EXPECT_NE(r.list, nullptr);
	if (r.list == nullptr) {
		return {};
	}
	const auto draws = all_of<DrawPathCommand>(*r.list);
	EXPECT_EQ(draws.size(), 1u);
	return draws.empty() ? DrawPathCommand{} : draws[0];
}

double srgb_encoded(double linear)
{
	return linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

int byte_of(double unit_value)
{
	return static_cast<int>(std::lround(std::clamp(unit_value, 0.0, 1.0) * 255.0));
}

std::string component(int byte)
{
	char text[32]{};
	std::snprintf(text, sizeof(text), "%.10f", byte / 255.0);
	return text;
}

TEST(PdfInterpreter, CalibratedColors)
{
	const std::string spaces = "<< /ColorSpace << "
							   "/LabD50 [/Lab << /WhitePoint [0.9642 1 0.8249] /Range [-128 127 -128 127] >>] "
							   "/GrayD65 [/CalGray << /WhitePoint [0.9505 1 1.089] /Gamma 2.2 >>] "
							   "/GrayD50 [/CalGray << /WhitePoint [0.9642 1 0.8249] /Gamma 2.2 >>] "
							   "/GrayBlack [/CalGray << /WhitePoint [0.9505 1 1.089] /BlackPoint [0.09505 0.1 0.1089] >>] "
							   "/RgbD65 [/CalRGB << /WhitePoint [0.9505 1 1.089] /Matrix [0.4124 0.2126 0.0193 0.3576 0.7152 "
							   "0.1192 0.1805 0.0722 0.9505] >>] "
							   "/BadWhite [/CalRGB << /WhitePoint [1 0.9 1] >>] "
							   "/LabNarrow [/Lab << /WhitePoint [0.9642 1 0.8249] /Range [-20 20 -20 20] >>] >> >>";
	const auto fill = [&](const std::string& ops) { return painted(interpret(ops + " 0 0 10 10 re f", spaces)).fill_color; };

	// Lab (D50 white) against LittleCMS's Lab D50 to sRGB transform; L* is the byte times 100/255, a* and b* the byte
	// less 128.
	struct LabCase {
		int l, a, b;
		Rgb8 want;
	};
	for (const LabCase& c : {LabCase{128, 128, 128, {119, 119, 119}}, LabCase{255, 128, 128, {255, 255, 255}},
			 LabCase{128, 168, 100, {167, 92, 168}}, LabCase{77, 100, 170, {34, 82, 0}}, LabCase{200, 140, 60, {145, 191, 255}}}) {
		char ops[128]{};
		std::snprintf(ops, sizeof(ops), "/LabD50 cs %.10f %d %d scn", c.l * 100.0 / 255.0, c.a - 128, c.b - 128);
		expect_rgb8(fill(ops), c.want, 1, ops);
	}
	// CalGray with a D65 white: Y = A^2.2, encoded by sRGB's curve; a D50 white adapts to the same grey.
	const int gray = byte_of(srgb_encoded(std::pow(0.5, 2.2)));
	expect_rgb8(fill("/GrayD65 cs 0.5 sc"), {gray, gray, gray}, 1, "CalGray D65");
	expect_rgb8(fill("/GrayD50 cs 0.5 sc"), {gray, gray, gray}, 1, "CalGray D50, adapted");
	// A black point at a tenth of the white: Y scaled from [0.1, 1] to [0, 1].
	const int compensated = byte_of(srgb_encoded((0.55 - 0.1) / 0.9));
	expect_rgb8(fill("/GrayBlack cs 0.55 sc"), {compensated, compensated, compensated}, 1, "CalGray black point");
	// CalRGB with sRGB's own matrix and white and gamma 1: sRGB's encoding of each component.
	expect_rgb8(fill("/RgbD65 cs 0.2 0.5 0.8 sc"),
		{byte_of(srgb_encoded(0.2)), byte_of(srgb_encoded(0.5)), byte_of(srgb_encoded(0.8))}, 1, "CalRGB");
	// A malformed /WhitePoint: the device space of its component count.
	expect_rgb8(fill("/BadWhite cs 0.2 0.5 0.8 sc"), {51, 128, 204}, 0, "malformed white point");
	// Lab's /Range clamps a* and b*: a* 40 in [-20 20] draws as a* 20.
	const ColorRgba clamped = fill("/LabNarrow cs 60 20 0 scn");
	const ColorRgba beyond = fill("/LabNarrow cs 60 40 0 scn");
	EXPECT_EQ(beyond.r, clamped.r);
	EXPECT_EQ(beyond.g, clamped.g);
	EXPECT_EQ(beyond.b, clamped.b);
	EXPECT_NE(fill("/LabD50 cs 60 40 0 scn").r, clamped.r);
	// Painting right after `cs`: Lab's initial colour (L* 0) is black.
	expect_rgb8(fill("/LabD50 cs"), {0, 0, 0}, 0, "initial Lab");

	// Stroke and fill apart; a calibrated stroke beside a device fill, and `q`/`Q` restoring the spaces.
	const auto both = painted(interpret("1 0 0 rg /RgbD65 CS 0.2 0.5 0.8 SCN 0 0 m 10 0 l 10 10 l B", spaces));
	expect_rgb8(both.stroke_color, {byte_of(srgb_encoded(0.2)), byte_of(srgb_encoded(0.5)), byte_of(srgb_encoded(0.8))}, 1,
		"calibrated stroke");
	expect_rgb8(both.fill_color, {255, 0, 0}, 0, "device fill beside it");
	const auto restored = painted(interpret("/GrayD65 cs 0.5 sc q /DeviceRGB cs 1 0 0 sc Q 0 0 10 10 re f", spaces));
	expect_rgb8(restored.fill_color, {gray, gray, gray}, 1, "space restored by Q");
	const auto lab_stroke = painted(interpret("/LabD50 CS 50.1960784314 40 -28 SCN 0 0 m 10 0 l S", spaces));
	expect_rgb8(lab_stroke.stroke_color, {167, 92, 168}, 1, "Lab stroke");
	const auto gray_stroke = painted(interpret("/GrayD65 CS 0.5 SC 0 0 m 10 0 l S", spaces));
	expect_rgb8(gray_stroke.stroke_color, {gray, gray, gray}, 1, "CalGray stroke");
	const auto restored_stroke = painted(interpret("/GrayD65 CS 0.5 SC q /DeviceRGB CS 1 0 0 SC Q 0 0 m 10 0 l S", spaces));
	expect_rgb8(restored_stroke.stroke_color, {gray, gray, gray}, 1, "stroke space restored by Q");
	// The device abbreviations belong to inline images: a resource named /RGB is that resource.
	const auto named_rgb = painted(interpret("/RGB cs 50 0 0 scn 0 0 10 10 re f",
		"<< /ColorSpace << /RGB [/Lab << /WhitePoint [0.9642 1 0.8249] >>] >> >>"));
	expect_rgb8(named_rgb.fill_color, {119, 119, 119}, 1, "a resource named /RGB");
	const auto switched = painted(interpret("/GrayD65 CS 0.5 SC /DeviceRGB CS 0 0 1 SC 0 0 m 10 0 l S", spaces));
	expect_rgb8(switched.stroke_color, {0, 0, 255}, 0, "calibrated to device on one side");
}

// The generated profiles' references through LittleCMS (relative colorimetric unless named).
constexpr Rgb8 kRgbMatrix128_64_200 = {129, 62, 201};
constexpr Rgb8 kRgbMatrix30_180_90 = {24, 181, 90};
constexpr Rgb8 kGray64 = {81, 81, 81};
constexpr Rgb8 kCmykRelative = {190, 158, 182};  // C 0, M 128, Y 0, K 64 (bytes)
constexpr Rgb8 kCmykPerceptual = {142, 175, 153};

std::string icc_resources()
{
	return "<< /ColorSpace << /RGBP [/ICCBased 5 0 R] /GrayP [/ICCBased 6 0 R] /CMYKP [/ICCBased 7 0 R] >> "
		   "/ExtGState << /Perc << /RI /Perceptual >> >> >>";
}

std::vector<std::string> icc_objects()
{
	return {stream_object("/N 3 /Alternate /DeviceRGB", corpus_text("color/rgb-matrix.icc")),
		stream_object("/N 1", corpus_text("color/gray-trc.icc")), stream_object("/N 4", corpus_text("color/cmyk-lut.icc"))};
}

TEST(PdfInterpreter, IccBasedColors)
{
	// The content stream is object 4, so the profiles are objects 5 to 7 (extra objects numbered from 5).
	const auto fill = [&](const std::string& ops) {
		return painted(interpret(ops + " 0 0 10 10 re f", icc_resources(), icc_objects())).fill_color;
	};
	const std::string cmyk = "/CMYKP cs 0 " + component(128) + " 0 " + component(64) + " scn";
	expect_rgb8(fill("/RGBP cs " + component(128) + " " + component(64) + " " + component(200) + " scn"), kRgbMatrix128_64_200, 2, "RGB");
	expect_rgb8(fill("/GrayP cs " + component(64) + " scn"), kGray64, 2, "Gray");
	expect_rgb8(fill(cmyk), kCmykRelative, 2, "CMYK relative colorimetric");
	expect_rgb8(fill("/Perceptual ri " + cmyk), kCmykPerceptual, 2, "CMYK perceptual by ri");
	expect_rgb8(fill("/Perc gs " + cmyk), kCmykPerceptual, 2, "CMYK perceptual by /ExtGState /RI");
	expect_rgb8(fill(cmyk + " /Perceptual ri"), kCmykPerceptual, 2, "intent changed between scn and the paint");
	expect_rgb8(fill("q /Perceptual ri Q " + cmyk), kCmykRelative, 2, "intent restored by Q");
	const auto stroked = painted(interpret("/RGBP CS " + component(30) + " " + component(180) + " " + component(90) +
			" SCN 0 0 m 10 0 l S", icc_resources(), icc_objects()));
	expect_rgb8(stroked.stroke_color, kRgbMatrix30_180_90, 2, "RGB stroke");

	// Twenty fills in one ICC colour convert it once.
	{
		std::string many = cmyk;
		for (int i = 0; i < 20; ++i) {
			many += " 0 0 10 10 re f";
		}
		const size_t before = detail::translate_calls_for_testing();
		const auto r = interpret(many, icc_resources(), icc_objects());
		ASSERT_NE(r.list, nullptr);
		EXPECT_EQ(all_of<DrawPathCommand>(*r.list).size(), 20u);
		EXPECT_EQ(detail::translate_calls_for_testing() - before, 1u);
	}
	// Painting right after selecting a four-component ICC space: its components start at 0 (white), not black.
	expect_rgb8(fill("/CMYKP cs"), {255, 255, 255}, 2, "initial ICC CMYK fill");
	expect_rgb8(painted(interpret("/CMYKP CS 0 0 m 10 0 l S", icc_resources(), icc_objects())).stroke_color, {255, 255, 255}, 2,
		"initial ICC CMYK stroke");
	// A Lab ICC profile: its data are L*, a* and b* (its /Range, or Lab's when it gives none).
	for (const std::string_view range : {std::string_view(" /Range [0 100 -128 127 -128 127]"), std::string_view("")}) {
		const auto lab_icc = painted(interpret("/L cs 50.1960784314 40 -28 scn 0 0 10 10 re f", "<< /ColorSpace << /L [/ICCBased 5 0 R] >> >>",
			{stream_object("/N 3" + std::string(range), corpus_text("color/lab-identity.icc"))}));
		expect_rgb8(lab_icc.fill_color, {167, 92, 168}, 2, "Lab ICC profile");
	}
	// A reversed /Range is ignored (the default 0 to 1), with a diagnostic.
	clear_diagnostics();
	expect_rgb8(painted(interpret("/P cs " + component(30) + " " + component(180) + " " + component(90) + " scn 0 0 10 10 re f",
					"<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
					{stream_object("/N 3 /Range [1 0 0 1 0 1]", corpus_text("color/rgb-matrix.icc"))}))
					.fill_color,
		kRgbMatrix30_180_90, 2, "reversed /Range ignored");
	{
		const auto tail = diagnostics_tail(32u);
		// The ring keeps each diagnostic's code, not its message (error.h).
		EXPECT_TRUE(std::any_of(tail.begin(), tail.end(), [](const DiagnosticEntry& e) { return e.code == ErrorCode::DamagedFormat; }));
	}
	// Twenty selections of one space and twenty fills: one profile decode, one translation.
	{
		std::string many;
		for (int i = 0; i < 20; ++i) {
			many += cmyk + " 0 0 10 10 re f ";
		}
		const size_t decodes = detail::icc_profile_decodes_for_testing();
		const size_t translations = detail::translate_calls_for_testing();
		const auto r = interpret(many, icc_resources(), icc_objects());
		ASSERT_NE(r.list, nullptr);
		EXPECT_EQ(detail::icc_profile_decodes_for_testing() - decodes, 1u);
		EXPECT_EQ(detail::translate_calls_for_testing() - translations, 1u);
	}
	// One transform per profile and intent, across interpretations of one document.
	{
		auto doc = Document::open_memory(one_page_pdf(cmyk + " 0 0 10 10 re f /Perceptual ri 0 0 5 5 re f", icc_resources(), icc_objects()));
		ASSERT_TRUE(doc.has_value());
		ASSERT_TRUE(interpret_page(**doc, 0u).has_value());
		ASSERT_TRUE(interpret_page(**doc, 0u).has_value());
		EXPECT_EQ((*doc)->color_profiles().transform_count(), 2u);
	}

	// Fallbacks, each to the device space of /N (or the /Alternate): raw components.
	const std::string raw_ops = "/P cs " + component(30) + " " + component(180) + " " + component(90) + " scn 0 0 10 10 re f";
	const Rgb8 raw = {30, 180, 90};
	const auto with_profile = [&](const std::string& stream) {
		return painted(interpret(raw_ops, "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>", {stream})).fill_color;
	};
	std::string damaged = corpus_text("color/rgb-matrix.icc");
	damaged[36] = 'X'; // not `acsp`
	clear_diagnostics();
	expect_rgb8(with_profile(stream_object("/N 3", damaged)), raw, 0, "damaged profile");
	{
		const auto tail = diagnostics_tail(32u);
		EXPECT_TRUE(std::any_of(tail.begin(), tail.end(), [](const DiagnosticEntry& e) { return e.code == ErrorCode::DamagedFormat; }))
			<< "a fallback records its diagnostic";
	}
	// A damaged profile with a calibrated /Alternate draws through the alternate.
	const auto cal_alternate = painted(interpret("/P cs 0.5 scn 0 0 10 10 re f", "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
		{stream_object("/N 1 /Alternate [/CalGray << /WhitePoint [0.9505 1 1.089] /Gamma 2.2 >>]", damaged)}));
	const int cal_gray = byte_of(srgb_encoded(std::pow(0.5, 2.2)));
	expect_rgb8(cal_alternate.fill_color, {cal_gray, cal_gray, cal_gray}, 1, "a calibrated alternate");
	expect_rgb8(painted(interpret("/P cs " + component(77) + " scn 0 0 10 10 re f", "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
					{stream_object("/N 1 /Alternate /DeviceGray", corpus_text("color/rgb-matrix.icc"))}))
					.fill_color,
		{77, 77, 77}, 0, "/N mismatched with the profile");
	std::string oversized = corpus_text("color/rgb-matrix.icc");
	oversized.resize(kMaxIccProfileBytes + 1u, '\0');
	expect_rgb8(with_profile(stream_object("/N 3", oversized)), raw, 0, "profile past 16 MiB");
	for (const auto failure : {detail::IccFailureForTesting::SrgbProfile, detail::IccFailureForTesting::Transform,
			 detail::IccFailureForTesting::Translate}) {
		detail::set_icc_failure_for_testing(failure);
		expect_rgb8(with_profile(stream_object("/N 3", corpus_text("color/rgb-matrix.icc"))), raw, 0, "injected failure");
		detail::set_icc_failure_for_testing(detail::IccFailureForTesting::None);
	}
	expect_rgb8(with_profile(stream_object("/N 3", corpus_text("color/rgb-matrix.icc"))), kRgbMatrix30_180_90, 2, "after the failures");
	// A good profile whose transform fails draws through its calibrated /Alternate (gamma 1: sRGB's encoding of 0.5).
	detail::set_icc_failure_for_testing(detail::IccFailureForTesting::Transform);
	const auto through_alternate = painted(interpret("/P cs 0.5 scn 0 0 10 10 re f", "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
		{stream_object("/N 1 /Alternate [/CalGray << /WhitePoint [0.9505 1 1.089] >>]", corpus_text("color/gray-trc.icc"))}));
	detail::set_icc_failure_for_testing(detail::IccFailureForTesting::None);
	const int linear_half = byte_of(srgb_encoded(0.5));
	expect_rgb8(through_alternate.fill_color, {linear_half, linear_half, linear_half}, 1, "the alternate after a failed transform");

	// Cycles: an /Alternate chain back to itself (both profiles damaged), and resources naming one another.
	expect_rgb8(painted(interpret(raw_ops, "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
					{stream_object("/N 3 /Alternate [/ICCBased 6 0 R]", damaged),
						stream_object("/N 3 /Alternate [/ICCBased 5 0 R]", damaged)}))
					.fill_color,
		raw, 0, "an /Alternate cycle");
	expect_rgb8(painted(interpret("/A cs 0.3 sc 0 0 10 10 re f", "<< /ColorSpace << /A /B /B /A >> >>")).fill_color,
		{77, 77, 77}, 0, "resources naming one another");

	// A profile padded past its declared size keeps only its declared bytes: what the budget charges is what is held.
	{
		std::string padded = corpus_text("color/rgb-matrix.icc");
		const size_t declared = padded.size();
		padded.resize(4u << 20u, '0');
		auto profile = IccProfile::make(std::vector<uint8_t>(padded.begin(), padded.end()), 3u, 0u);
		ASSERT_TRUE(profile.has_value());
		EXPECT_EQ((*profile)->bytes.size(), declared);
		EXPECT_LE((*profile)->bytes.capacity(), declared + 64u);
	}
	// /Range elements through references read as the same numbers given directly: 0.5 in [0 0.5] is the top of the
	// range, white.
	for (const std::string_view range : {std::string_view("[0 0.5]"), std::string_view("[0 6 0 R]")}) {
		expect_rgb8(painted(interpret("/P cs 0.5 scn 0 0 10 10 re f", "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
						{stream_object("/N 1 /Range " + std::string(range), corpus_text("color/gray-trc.icc")), "0.5"}))
						.fill_color,
			{255, 255, 255}, 2, range);
	}
	// A rejected profile named through a hundred resources is decoded once.
	{
		std::string damaged_rgb = corpus_text("color/rgb-matrix.icc");
		damaged_rgb[36] = 'X';
		std::string resources_text = "<< /ColorSpace << ";
		std::string content;
		for (int i = 0; i < 100; ++i) {
			resources_text += "/A" + std::to_string(i) + " [/ICCBased 5 0 R] ";
			content += "/A" + std::to_string(i) + " cs 0.1 0.2 0.3 scn 0 0 10 10 re f ";
		}
		const size_t decodes = detail::icc_profile_decodes_for_testing();
		const auto r = interpret(content, resources_text + ">> >>", {stream_object("/N 3", damaged_rgb)});
		ASSERT_NE(r.list, nullptr);
		EXPECT_EQ(all_of<DrawPathCommand>(*r.list).size(), 100u);
		EXPECT_EQ(detail::icc_profile_decodes_for_testing() - decodes, 1u);
	}
	// /N through a reference.
	expect_rgb8(painted(interpret("/P cs " + component(64) + " scn 0 0 10 10 re f", "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
					{stream_object("/N 6 0 R", corpus_text("color/gray-trc.icc")), "1"}))
					.fill_color,
		kGray64, 2, "an indirect /N");
	// A profile past the document's profile budget is not used, by the cache or by a space holding it.
	{
		auto doc = Document::open_memory(one_page_pdf("/P cs " + component(30) + " " + component(180) + " " + component(90) +
				" scn 0 0 10 10 re f", "<< /ColorSpace << /P [/ICCBased 5 0 R] >> >>",
			{stream_object("/N 3", corpus_text("color/rgb-matrix.icc"))}));
		ASSERT_TRUE(doc.has_value());
		const std::string big_base = corpus_text("color/gray-trc.icc");
		for (uint32_t i = 0; i < 4u; ++i) {
			std::vector<uint8_t> big(big_base.begin(), big_base.end());
			big.resize(kMaxIccProfileBytes, 0u);
			const uint32_t size = static_cast<uint32_t>(big.size());
			big[0] = static_cast<uint8_t>(size >> 24u);
			big[1] = static_cast<uint8_t>(size >> 16u);
			big[2] = static_cast<uint8_t>(size >> 8u);
			big[3] = static_cast<uint8_t>(size);
			auto profile_i = IccProfile::make(std::move(big), 1u, 1000u + i);
			ASSERT_TRUE(profile_i.has_value());
			auto kept = (*doc)->color_profiles().remember(*profile_i);
			ASSERT_TRUE(kept.has_value() && *kept);
		}
		const size_t decodes = detail::icc_profile_decodes_for_testing();
		auto list = interpret_page(**doc, 0u);
		ASSERT_TRUE(list.has_value());
		const auto draws = all_of<DrawPathCommand>(**list);
		ASSERT_EQ(draws.size(), 1u);
		expect_rgb8(draws[0].fill_color, raw, 0, "a profile past the budget");
		EXPECT_EQ((*doc)->color_profiles().profile(5u), nullptr);
		EXPECT_EQ(detail::icc_profile_decodes_for_testing() - decodes, 1u);
	}
	// Two pages of one document share one transform.
	{
		std::vector<std::string> objects;
		objects.emplace_back("<< /Type /Catalog /Pages 2 0 R >>");
		objects.emplace_back("<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 /Resources << /ColorSpace << /RGBP [/ICCBased 6 0 R] >> >> >>");
		objects.emplace_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>");
		objects.push_back(stream_object("", "/RGBP cs 0.1 0.2 0.3 scn 0 0 10 10 re f"));
		objects.emplace_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R >>");
		objects.push_back(stream_object("/N 3", corpus_text("color/rgb-matrix.icc")));
		auto doc = Document::open_memory(pdf_from_objects(objects));
		ASSERT_TRUE(doc.has_value());
		const size_t decodes = detail::icc_profile_decodes_for_testing();
		ASSERT_TRUE(interpret_page(**doc, 0u).has_value());
		ASSERT_TRUE(interpret_page(**doc, 1u).has_value());
		EXPECT_EQ((*doc)->color_profiles().transform_count(), 1u);
		EXPECT_EQ(detail::icc_profile_decodes_for_testing() - decodes, 1u); // the profile read once for both pages
	}
	// Failed builds count toward the budget too: 64 profiles Windows refuses spend it, and a good 65th falls back.
	{
		// A header Recto accepts over an empty tag table, which Windows cannot build a transform from.
		std::string refused = corpus_text("color/rgb-matrix.icc");
		refused[128] = refused[129] = refused[130] = refused[131] = '\0';
		std::string content;
		std::string spaces_text = "<< /ColorSpace << ";
		std::vector<std::string> objects;
		for (int i = 0; i < 65; ++i) {
			spaces_text += "/P" + std::to_string(i) + " [/ICCBased " + std::to_string(5 + i) + " 0 R] ";
			objects.push_back(stream_object("/N 3", i < 64 ? refused : corpus_text("color/rgb-matrix.icc")));
			content += "/P" + std::to_string(i) + " cs " + component(30) + " " + component(180) + " " + component(90) +
				" scn 0 0 10 10 re f ";
		}
		const auto r = interpret(content, spaces_text + ">> >>", objects);
		ASSERT_NE(r.list, nullptr);
		const auto draws = all_of<DrawPathCommand>(*r.list);
		ASSERT_EQ(draws.size(), 65u);
		expect_rgb8(draws[0].fill_color, raw, 0, "a refused profile");
		expect_rgb8(draws[64].fill_color, raw, 0, "a good profile after 64 refused builds");
	}
	// The cache's budget: 64 transforms per document; the 65th profile takes its fallback.
	{
		std::string content;
		std::string spaces_text = "<< /ColorSpace << ";
		std::vector<std::string> objects;
		for (int i = 0; i < 65; ++i) {
			spaces_text += "/P" + std::to_string(i) + " [/ICCBased " + std::to_string(5 + i) + " 0 R] ";
			objects.push_back(stream_object("/N 3", corpus_text("color/rgb-matrix.icc")));
			content += "/P" + std::to_string(i) + " cs " + component(30) + " " + component(180) + " " + component(90) +
				" scn 0 0 10 10 re f ";
		}
		const auto r = interpret(content, spaces_text + ">> >>", objects);
		ASSERT_NE(r.list, nullptr);
		const auto draws = all_of<DrawPathCommand>(*r.list);
		ASSERT_EQ(draws.size(), 65u);
		expect_rgb8(draws[63].fill_color, kRgbMatrix30_180_90, 2, "the 64th profile");
		expect_rgb8(draws[64].fill_color, raw, 0, "the 65th profile, past the budget");
	}
}

TEST(PdfInterpreter, CalibratedCmyk)
{
	const std::string ops = "0 " + component(128) + " 0 " + component(64) + " k 0 0 10 10 re f";
	// The document's output intent.
	{
		std::vector<std::string> objects;
		objects.emplace_back("<< /Type /Catalog /Pages 2 0 R /OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX "
							 "/DestOutputProfile 5 0 R >>] >>");
		objects.emplace_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
		objects.emplace_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R >>");
		objects.push_back(stream_object("", ops));
		objects.push_back(stream_object("/N 4", corpus_text("color/cmyk-lut.icc")));
		expect_rgb8(painted(interpret(pdf_from_objects(objects))).fill_color, kCmykRelative, 2, "output intent");
	}
	// Output intents naming one damaged profile a thousand times try it once, then find the good one after them.
	{
		std::string damaged_cmyk = corpus_text("color/cmyk-lut.icc");
		damaged_cmyk[36] = 'X';
		std::string intents = "[";
		for (int i = 0; i < 1000; ++i) {
			intents += "6 0 R ";
		}
		intents += "<< /Type /OutputIntent /S /GTS_PDFX /DestOutputProfile 7 0 R >>]";
		std::vector<std::string> objects;
		objects.emplace_back("<< /Type /Catalog /Pages 2 0 R /OutputIntents " + intents + " >>");
		objects.emplace_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
		objects.emplace_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R >>");
		objects.push_back(stream_object("", ops));
		objects.push_back(stream_object("/N 4", damaged_cmyk));
		objects.emplace_back("<< /Type /OutputIntent /S /GTS_PDFX /DestOutputProfile 5 0 R >>");
		objects.push_back(stream_object("/N 4", corpus_text("color/cmyk-lut.icc")));
		const size_t decodes = detail::icc_profile_decodes_for_testing();
		expect_rgb8(painted(interpret(pdf_from_objects(objects))).fill_color, kCmykRelative, 2, "the good intent after the repeats");
		EXPECT_EQ(detail::icc_profile_decodes_for_testing() - decodes, 2u);
	}
	// No output intent: Windows' RSWOP.icm when present, its own Windows transform, not the formula.
	const Rgb8 formula = {191, 63, 191}; // 1 - min(1, colourant + k) per channel: C 0, M 0.502, Y 0, K 0.251
	const auto plain = painted(interpret(ops)).fill_color;
	std::array<wchar_t, MAX_PATH + 1> dir{};
	GetSystemDirectoryW(dir.data(), static_cast<UINT>(dir.size()));
	const std::filesystem::path rswop = std::filesystem::path(dir.data()) / "spool" / "drivers" / "color" / "RSWOP.icm";
	if (std::filesystem::exists(rswop)) {
		std::ifstream in(rswop, std::ios::binary);
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		auto profile = IccProfile::make(std::move(bytes), 4u, 0u);
		ASSERT_TRUE(profile.has_value());
		auto doc = Document::open_memory(one_page_pdf(std::nullopt));
		ASSERT_TRUE(doc.has_value());
		const std::array<double, 4> c = {0.0, 128.0 / 255.0, 0.0, 64.0 / 255.0};
		std::array<double, 3> rgb{};
		auto converted = (*doc)->color_profiles().to_srgb(**profile, RenderingIntent::RelativeColorimetric, c, 1u, rgb);
		ASSERT_TRUE(converted.has_value() && *converted);
		expect_rgb8(plain, {byte_of(rgb[0]), byte_of(rgb[1]), byte_of(rgb[2])}, 1, "RSWOP.icm");
		EXPECT_GT(std::abs(static_cast<int>(std::lround(plain.g * 255.0f)) - formula.g), 2) << "differs from the formula";
	}
	// No profile loads: the section 3 formula.
	detail::set_default_cmyk_profile_enabled_for_testing(false);
	expect_rgb8(painted(interpret(ops)).fill_color, formula, 1, "the formula without a profile");
	detail::set_default_cmyk_profile_enabled_for_testing(true);
}

TEST(PdfInterpreter, DefaultColorSpaces)
{
	const std::string defaults = "<< /ColorSpace << /DefaultRGB [/ICCBased 5 0 R] /DefaultGray [/ICCBased 6 0 R] "
								 "/DefaultCMYK [/ICCBased 7 0 R] >> >>";
	const auto fill = [&](const std::string& ops, const std::string& resources) {
		return painted(interpret(ops + " 0 0 10 10 re f", resources, icc_objects())).fill_color;
	};
	const std::string rgb = component(30) + " " + component(180) + " " + component(90);
	expect_rgb8(fill(rgb + " rg", defaults), kRgbMatrix30_180_90, 2, "rg through /DefaultRGB");
	expect_rgb8(fill("/DeviceRGB cs " + rgb + " sc", defaults), kRgbMatrix30_180_90, 2, "cs /DeviceRGB through /DefaultRGB");
	expect_rgb8(fill(component(64) + " g", defaults), kGray64, 2, "g through /DefaultGray");
	expect_rgb8(fill("0 " + component(128) + " 0 " + component(64) + " k", defaults), kCmykRelative, 2, "k through /DefaultCMYK");
	const auto stroked = painted(interpret(rgb + " RG 0 0 m 10 0 l S", defaults, icc_objects()));
	expect_rgb8(stroked.stroke_color, kRgbMatrix30_180_90, 2, "RG through /DefaultRGB");
	expect_rgb8(painted(interpret(component(64) + " G 0 0 m 10 0 l S", defaults, icc_objects())).stroke_color, kGray64, 2,
		"G through /DefaultGray");
	expect_rgb8(painted(interpret("0 " + component(128) + " 0 " + component(64) + " K 0 0 m 10 0 l S", defaults, icc_objects())).stroke_color,
		kCmykRelative, 2, "K through /DefaultCMYK");
	// No default: the device space; a default naming its own device space: the device space.
	expect_rgb8(fill(rgb + " rg", "<< >>"), {30, 180, 90}, 0, "no default");
	expect_rgb8(fill(rgb + " rg", "<< /ColorSpace << /DefaultRGB /DeviceRGB >> >>"), {30, 180, 90}, 0, "a default naming itself");
	// The same resolution for an image's device space.
	auto doc = Document::open_memory(one_page_pdf(std::nullopt, defaults, icc_objects()));
	ASSERT_TRUE(doc.has_value());
	auto page_resources = (*doc)->resolve_value(PdfValue(IndirectRef(3u)));
	ASSERT_TRUE(page_resources.has_value());
	const PdfDict* page = page_resources->as_dict();
	ASSERT_NE(page, nullptr);
	auto resources = (*doc)->resolve_value(*page->get("Resources"));
	ASSERT_TRUE(resources.has_value());
	auto color_spaces = (*doc)->resolve_value(*resources->as_dict()->get("ColorSpace"));
	ASSERT_TRUE(color_spaces.has_value());
	ColorSpaces resolver(**doc, color_spaces->as_dict());
	auto image_space = resolver.resolve(PdfValue(PdfName("DeviceRGB")));
	ASSERT_TRUE(image_space.has_value() && *image_space != nullptr);
	EXPECT_EQ((*image_space)->kind(), ColorSpaceKind::ICCBased);
}

TEST(PdfColor, ImageSamples)
{
	auto doc = Document::open_memory(one_page_pdf(std::nullopt));
	ASSERT_TRUE(doc.has_value());
	const Document& d = **doc;
	const auto row = [&](const ColorSpace& space, std::vector<uint8_t> bytes, size_t width, uint32_t bits,
						 std::vector<double> decode = {}) {
		auto out = space.convert_samples(bytes, width, bits, decode, RenderingIntent::RelativeColorimetric, d);
		EXPECT_TRUE(out.has_value()) << (out.has_value() ? "" : out.error().message.view());
		return out.has_value() ? *out : std::vector<uint8_t>{};
	};
	const auto& gray = *ColorSpace::device(ColorSpaceKind::DeviceGray);
	const auto& rgb = *ColorSpace::device(ColorSpaceKind::DeviceRGB);
	// Packed samples, most significant bits first.
	EXPECT_EQ(row(gray, {0b10110000u}, 4u, 1u), (std::vector<uint8_t>{255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255}));
	EXPECT_EQ(row(gray, {0b00011011u}, 4u, 2u), (std::vector<uint8_t>{0, 0, 0, 255, 85, 85, 85, 255, 170, 170, 170, 255, 255, 255, 255, 255}));
	EXPECT_EQ(row(rgb, {0xF0u, 0x08u, 0xFFu}, 2u, 4u), (std::vector<uint8_t>{255, 0, 0, 255, 136, 255, 255, 255}));
	// 16-bit samples, big-endian.
	EXPECT_EQ(row(gray, {0x80u, 0x00u, 0xFFu, 0xFFu}, 2u, 16u), (std::vector<uint8_t>{128, 128, 128, 255, 255, 255, 255, 255}));
	// A reversed /Decode, an omitted one, and a malformed one (ignored).
	EXPECT_EQ(row(gray, {0u, 255u}, 2u, 8u, {1.0, 0.0}), (std::vector<uint8_t>{255, 255, 255, 255, 0, 0, 0, 255}));
	EXPECT_EQ(row(gray, {0u, 255u}, 2u, 8u), (std::vector<uint8_t>{0, 0, 0, 255, 255, 255, 255, 255}));
	EXPECT_EQ(row(gray, {0u, 255u}, 2u, 8u, {1.0, 0.0, 0.5}), (std::vector<uint8_t>{0, 0, 0, 255, 255, 255, 255, 255}));
	// A row shorter than its samples.
	const auto truncated = rgb.convert_samples(std::vector<uint8_t>{1u, 2u}, 1u, 8u, {}, RenderingIntent::RelativeColorimetric, d);
	ASSERT_FALSE(truncated.has_value());
	EXPECT_EQ(truncated.error().code, ErrorCode::DamagedFormat);

	// An ICC row: the single-colour path's values, LittleCMS's within 2/255, in one Windows call.
	const std::string rgb_profile = corpus_text("color/rgb-matrix.icc");
	auto profile = IccProfile::make(std::vector<uint8_t>(rgb_profile.begin(), rgb_profile.end()), 3u, 0u);
	ASSERT_TRUE(profile.has_value());
	const auto icc = ColorSpace::icc(*profile, {}, ColorSpace::device(ColorSpaceKind::DeviceRGB));
	const size_t calls = detail::translate_calls_for_testing();
	const auto icc_row = row(*icc, {128u, 64u, 200u, 30u, 180u, 90u}, 2u, 8u);
	EXPECT_EQ(detail::translate_calls_for_testing() - calls, 1u);
	ASSERT_EQ(icc_row.size(), 8u);
	const std::array<double, 3> first = {128.0 / 255.0, 64.0 / 255.0, 200.0 / 255.0};
	auto single = icc->rgba(first, RenderingIntent::RelativeColorimetric, d);
	ASSERT_TRUE(single.has_value());
	EXPECT_LE(std::abs(icc_row[0] - byte_of(single->r)), 1);
	EXPECT_LE(std::abs(icc_row[0] - kRgbMatrix128_64_200.r), 2);
	EXPECT_LE(std::abs(icc_row[5] - kRgbMatrix30_180_90.g), 2);

	// A calibrated row matches the single-colour path.
	CieParams lab;
	lab.white = {0.9642, 1.0, 0.8249};
	lab.range = {-128.0, 127.0, -128.0, 127.0};
	const auto lab_space = ColorSpace::cie(ColorSpaceKind::Lab, lab);
	const auto lab_row = row(*lab_space, {128u, 168u, 100u}, 1u, 8u, {0.0, 100.0, -128.0, 127.0, -128.0, 127.0});
	ASSERT_EQ(lab_row.size(), 4u);
	EXPECT_LE(std::abs(lab_row[0] - 167), 1);
	EXPECT_LE(std::abs(lab_row[1] - 92), 1);

	// An image's own profile, and one that cannot be read.
	const std::string gray_profile = corpus_text("color/gray-trc.icc");
	const auto own = ColorSpace::from_icc_profile(
		std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(gray_profile.data()), gray_profile.size()), 1u);
	ASSERT_TRUE(own.has_value());
	EXPECT_EQ((*own)->kind(), ColorSpaceKind::ICCBased);
	EXPECT_LE(std::abs(row(**own, {64u}, 1u, 8u)[0] - kGray64.r), 2);
	const std::vector<uint8_t> junk(200u, 0x41u);
	auto junk_space = ColorSpace::from_icc_profile(junk, 1u);
	ASSERT_TRUE(junk_space.has_value());
	EXPECT_EQ((*junk_space)->kind(), ColorSpaceKind::DeviceGray);

	// Rows in the calibrated spaces and through an ICC CMYK profile match the single-colour path.
	CieParams gray_params;
	gray_params.white = {0.9505, 1.0, 1.089};
	gray_params.gamma = {2.2, 2.2, 2.2};
	const auto cal_gray = ColorSpace::cie(ColorSpaceKind::CalGray, gray_params);
	const int cal_value = byte_of(srgb_encoded(std::pow(128.0 / 255.0, 2.2)));
	EXPECT_LE(std::abs(row(*cal_gray, {128u}, 1u, 8u)[0] - cal_value), 1);
	CieParams rgb_params;
	rgb_params.white = {0.9505, 1.0, 1.089};
	rgb_params.matrix = {0.4124, 0.2126, 0.0193, 0.3576, 0.7152, 0.1192, 0.1805, 0.0722, 0.9505};
	const auto cal_rgb = ColorSpace::cie(ColorSpaceKind::CalRGB, rgb_params);
	const auto cal_rgb_row = row(*cal_rgb, {51u, 128u, 204u}, 1u, 8u);
	EXPECT_LE(std::abs(cal_rgb_row[0] - byte_of(srgb_encoded(0.2))), 1);
	EXPECT_LE(std::abs(cal_rgb_row[2] - byte_of(srgb_encoded(0.8))), 1);
	const std::string cmyk_profile = corpus_text("color/cmyk-lut.icc");
	auto cmyk = IccProfile::make(std::vector<uint8_t>(cmyk_profile.begin(), cmyk_profile.end()), 4u, 0u);
	ASSERT_TRUE(cmyk.has_value());
	const auto cmyk_space = ColorSpace::icc(*cmyk, {}, ColorSpace::device(ColorSpaceKind::DeviceCMYK));
	const auto cmyk_row = row(*cmyk_space, {0u, 128u, 0u, 64u}, 1u, 8u);
	EXPECT_LE(std::abs(cmyk_row[0] - kCmykRelative.r), 2);
	EXPECT_LE(std::abs(cmyk_row[1] - kCmykRelative.g), 2);
	// A whole row whose translation fails takes the alternate for the whole row, never part of it.
	detail::set_icc_failure_for_testing(detail::IccFailureForTesting::Translate);
	const auto failed_row = row(*icc, {128u, 64u, 200u, 30u, 180u, 90u}, 2u, 8u);
	detail::set_icc_failure_for_testing(detail::IccFailureForTesting::None);
	EXPECT_EQ(failed_row, (std::vector<uint8_t>{128, 64, 200, 255, 30, 180, 90, 255}));

	// Memory running out while reading an image's profile is OutOfMemory, never a device-space fallback.
	if (recto::test::ScopedAllocationFailure::available()) {
		const recto::test::ScopedAllocationFailure fail(1u, gray_profile.size());
		const auto failed = ColorSpace::from_icc_profile(
			std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(gray_profile.data()), gray_profile.size()), 1u);
		ASSERT_FALSE(failed.has_value());
		EXPECT_EQ(failed.error().code, ErrorCode::OutOfMemory);
	}

	// The profile byte budget: four 16 MiB profiles fit in 64 MiB, a fifth does not.
	auto budget_doc = Document::open_memory(one_page_pdf(std::nullopt));
	ASSERT_TRUE(budget_doc.has_value());
	const std::string big_base = corpus_text("color/gray-trc.icc");
	const std::array<double, 1> mid = {0.5};
	std::array<double, 3> out{};
	for (uint32_t i = 0; i < 5u; ++i) {
		std::vector<uint8_t> big(big_base.begin(), big_base.end());
		big.resize(kMaxIccProfileBytes, 0u);
		const uint32_t size = static_cast<uint32_t>(big.size());
		big[0] = static_cast<uint8_t>(size >> 24u);
		big[1] = static_cast<uint8_t>(size >> 16u);
		big[2] = static_cast<uint8_t>(size >> 8u);
		big[3] = static_cast<uint8_t>(size);
		auto profile_i = IccProfile::make(std::move(big), 1u, 1000u + i);
		ASSERT_TRUE(profile_i.has_value()) << i;
		auto converted = (*budget_doc)->color_profiles().to_srgb(**profile_i, RenderingIntent::RelativeColorimetric, mid, 1u, out);
		ASSERT_TRUE(converted.has_value());
		EXPECT_EQ(*converted, i < 4u) << "profile " << i;
	}
}

} // namespace
} // namespace recto::pdf
