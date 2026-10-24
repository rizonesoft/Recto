// Unit tests for the content-stream interpreter's graphics state, paths, painting, clipping and device colour
// (D01 T02 section 3). Each test writes a one-page PDF (media box 0 0 200 200) whose content stream it states, so every
// expected command, point, colour and box below follows by hand from that stream and ISO 32000-1 section 8.
#include "interpreter.h"
#include "lexer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
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
		{"0.5 rg /DeviceRGB cs", false, {0.0f, 0.0f, 0.0f}}, // `cs` sets the space's initial colour
		{"/DeviceCMYK cs", false, {0.0f, 0.0f, 0.0f}},       // CMYK starts at 0 0 0 1
		{"/DeviceGray CS 0.7 SC", true, {0.7f, 0.7f, 0.7f}},
		{"/DeviceRGB cs 0.1 0.2 0.3 scn", false, {0.1f, 0.2f, 0.3f}},
		{"/DeviceRGB cs 0.1 0.2 sc", false, {0.0f, 0.0f, 0.0f}}, // the wrong number of components is skipped
		{"/C1 cs 0.4 0.5 0.6 sc", false, {0.4f, 0.5f, 0.6f}},   // a named space naming DeviceRGB
		{"/C2 CS 0 0 0 0.5 SC", true, {0.5f, 0.5f, 0.5f}},      // an indirect one naming DeviceCMYK
		{"0.5 g /L cs 50 0 0 sc", false, {0.0f, 0.0f, 0.0f}},    // a space section 32 reads paints black until then
	};
	for (const Case& c : cases) {
		SCOPED_TRACE(std::string(c.ops));
		const auto r = interpret(std::string(c.ops) + " 0 0 m 10 0 l 10 10 l B",
			"<< /ColorSpace << /C1 /DeviceRGB /C2 5 0 R /L [/Lab << /WhitePoint [0.95 1 1.09] >>] >> >>", {"/DeviceCMYK"});
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

} // namespace
} // namespace recto::pdf
