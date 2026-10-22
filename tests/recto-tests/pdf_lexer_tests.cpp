// Tests for the binary-safe PDF 1.7 and 2.0 lexer (D01 T01 section 3).
#include "lexer.h"
#include "parser.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {
namespace {

std::vector<uint8_t> to_bytes(std::string_view sv)
{
	return std::vector<uint8_t>(
		reinterpret_cast<const uint8_t*>(sv.data()),
		reinterpret_cast<const uint8_t*>(sv.data()) + sv.size());
}

std::string bytes_to_ascii(std::span<const uint8_t> span)
{
	return std::string(reinterpret_cast<const char*>(span.data()), span.size());
}

TEST(PdfLexer, WhitespaceAndComments)
{
	// Embedded 0x00 NUL bytes, all 6 PDF whitespace bytes (0x00, 0x09, 0x0A,
	// 0x0C, 0x0D, 0x20), delimiters, and comments containing arbitrary binary
	// bytes including 0x00 and 0xFF.
	std::vector<uint8_t> buf;
	auto append_str = [&](std::string_view s) {
		buf.insert(
			buf.end(),
			reinterpret_cast<const uint8_t*>(s.data()),
			reinterpret_cast<const uint8_t*>(s.data()) + s.size());
	};

	append_str("%PDF-1.7\r\n");
	buf.push_back('%');
	buf.push_back(0xE2u);
	buf.push_back(0xE3u);
	buf.push_back(0x00u);
	buf.push_back(0xFFu);
	buf.push_back('\r');
	buf.push_back(0x00u);
	buf.push_back(0x09u);
	buf.push_back(0x0Cu);
	buf.push_back(0x20u);
	append_str("12");
	buf.push_back(0x00u);
	append_str("0");
	buf.push_back(0x00u);
	append_str("obj<< /Type/Catalog >>[true false null]{}endobj\n%%EOF");

	// 1. Default mode skips comments transparently.
	Lexer lex(buf, false);
	auto t1 = lex.next_token();
	RECTO_ASSERT_OK(t1);
	EXPECT_EQ(t1.value().kind, TokenKind::Integer);
	EXPECT_EQ(t1.value().int_value, 12);

	// Peek ahead 2 tokens (0, obj) without consuming or moving logical position().
	const size_t pos_after_12 = lex.position();
	auto p0 = lex.peek_token(0);
	auto p1 = lex.peek_token(1);
	RECTO_ASSERT_OK(p0);
	RECTO_ASSERT_OK(p1);
	EXPECT_EQ(lex.position(), pos_after_12);
	EXPECT_EQ(p0.value().kind, TokenKind::Integer);
	EXPECT_EQ(p0.value().int_value, 0);
	EXPECT_TRUE(p1.value().is_keyword("obj"));

	EXPECT_EQ(lex.next_token().value().int_value, 0);
	EXPECT_TRUE(lex.next_token().value().is_keyword("obj"));
	EXPECT_EQ(lex.next_token().value().kind, TokenKind::DictOpen);
	EXPECT_EQ(lex.next_token().value().text, "Type");
	EXPECT_EQ(lex.next_token().value().text, "Catalog");
	EXPECT_EQ(lex.next_token().value().kind, TokenKind::DictClose);
	EXPECT_EQ(lex.next_token().value().kind, TokenKind::ArrayOpen);
	auto tb1 = lex.next_token();
	RECTO_ASSERT_OK(tb1);
	EXPECT_EQ(tb1.value().kind, TokenKind::Boolean);
	EXPECT_TRUE(tb1.value().bool_value);
	auto tb2 = lex.next_token();
	RECTO_ASSERT_OK(tb2);
	EXPECT_EQ(tb2.value().kind, TokenKind::Boolean);
	EXPECT_FALSE(tb2.value().bool_value);
	EXPECT_EQ(lex.next_token().value().kind, TokenKind::Null);
	EXPECT_EQ(lex.next_token().value().kind, TokenKind::ArrayClose);
	// Braces are regular characters in PDF syntax, so they join the keyword that follows them.
	EXPECT_TRUE(lex.next_token().value().is_keyword("{}endobj"));
	EXPECT_TRUE(lex.next_token().value().is_eof());

	// 2. emit_comments = true preserves %PDF-1.7, binary comment, and %%EOF.
	Lexer comment_lex(buf, true);
	auto c1 = comment_lex.next_token();
	RECTO_ASSERT_OK(c1);
	EXPECT_EQ(c1.value().kind, TokenKind::Comment);
	EXPECT_EQ(c1.value().text, "PDF-1.7");

	auto c2 = comment_lex.next_token();
	RECTO_ASSERT_OK(c2);
	EXPECT_EQ(c2.value().kind, TokenKind::Comment);
	EXPECT_EQ(c2.value().text.size(), 4u);

	// 3. Peeking on a malformed token rolls back cursor_ so a subsequent
	// next_token() observes the exact same error instead of silently returning EOF.
	const auto bad_literal = to_bytes("(unterminated");
	Lexer err_lex(bad_literal);
	RECTO_EXPECT_ERR(err_lex.peek_token(0), ErrorCode::DamagedFormat);
	RECTO_EXPECT_ERR(err_lex.next_token(), ErrorCode::DamagedFormat);

	// 4. Repeated peek_token beyond EOF reuses the single cached EOF token,
	// and peeking past kMaxLexerLookahead on a live stream returns
	// DecompressionLimitExceeded without consuming tokens.
	const auto empty_bytes = to_bytes("");
	Lexer eof_lex(empty_bytes);
	for (int i = 0; i < 100; ++i) {
		auto eof_peek = eof_lex.peek_token(1000);
		RECTO_ASSERT_OK(eof_peek);
		EXPECT_TRUE(eof_peek.value().is_eof());
	}
	EXPECT_TRUE(eof_lex.next_token().value().is_eof());

	std::string many_ints;
	for (size_t i = 0; i < kMaxLexerLookahead + 8; ++i) {
		many_ints += std::to_string(i + 1) + " ";
	}
	const auto many_bytes = to_bytes(many_ints);
	Lexer bound_lex(many_bytes);
	RECTO_EXPECT_ERR(
		bound_lex.peek_token(kMaxLexerLookahead),
		ErrorCode::DecompressionLimitExceeded);
	auto first_after_bound = bound_lex.next_token();
	RECTO_ASSERT_OK(first_after_bound);
	EXPECT_EQ(first_after_bound.value().int_value, 1);

	// 5. Seeking or reconfiguring after peeking a large token releases
	// cached lookahead payloads immediately and leaves the lexer at the new offset.
	std::string large_name_doc = "/" + std::string(64 * 1024, 'A') + " 42";
	const auto large_name_bytes = to_bytes(large_name_doc);
	{
		Lexer reclaim_lex(large_name_bytes);
		const size_t baseline_bytes = detail::lexer_live_allocated_bytes();
		{
			auto peeked_large = reclaim_lex.peek_token(0);
			RECTO_ASSERT_OK(peeked_large);
			EXPECT_EQ(peeked_large.value().text.size(), 64u * 1024u);
		}
		EXPECT_GE(detail::lexer_live_allocated_bytes() - baseline_bytes, 64u * 1024u);
		reclaim_lex.seek(large_name_bytes.size() - 2);
		EXPECT_EQ(detail::lexer_live_allocated_bytes(), baseline_bytes);
		auto after_seek = reclaim_lex.next_token();
		RECTO_ASSERT_OK(after_seek);
		EXPECT_EQ(after_seek.value().kind, TokenKind::Integer);
		EXPECT_EQ(after_seek.value().int_value, 42);
		EXPECT_TRUE(reclaim_lex.next_token().value().is_eof());
		EXPECT_EQ(detail::lexer_live_allocated_bytes(), baseline_bytes);
	}

	// 6. Allocation fault-injection in both peek_token() and next_token()
	// returns ErrorCode::OutOfMemory without terminating in Debug (/MDd) or
	// Release (/MD), rolls back the cursor, and recovers cleanly once
	// allocations succeed again.
	{
		const auto oom_doc = to_bytes("/Name1 (Literal) <48656C6C6F>");
		for (int fail_idx = 0; fail_idx < 4; ++fail_idx) {
			Lexer oom_lex(oom_doc);
			{
				detail::ScopedLexerAllocFaultInjector fault(fail_idx);
				auto peek_res = oom_lex.peek_token(0);
				if (!peek_res.has_value()) {
					EXPECT_EQ(peek_res.error().code, ErrorCode::OutOfMemory);
					EXPECT_EQ(oom_lex.position(), 0u);
				}
			}
			auto tok1 = oom_lex.next_token();
			RECTO_ASSERT_OK(tok1);
			EXPECT_EQ(tok1.value().kind, TokenKind::Name);
			EXPECT_EQ(tok1.value().text, "Name1");

			{
				const size_t pos_before = oom_lex.position();
				detail::ScopedLexerAllocFaultInjector fault(fail_idx);
				auto next_res = oom_lex.next_token();
				if (!next_res.has_value()) {
					EXPECT_EQ(next_res.error().code, ErrorCode::OutOfMemory);
					EXPECT_EQ(oom_lex.position(), pos_before);
					detail::set_lexer_alloc_fail_countdown(-1);
					auto retry_tok2 = oom_lex.next_token();
					RECTO_ASSERT_OK(retry_tok2);
					EXPECT_EQ(retry_tok2.value().kind, TokenKind::LiteralString);
					EXPECT_EQ(bytes_to_ascii(retry_tok2.value().bytes), "Literal");
				} else {
					EXPECT_EQ(next_res.value().kind, TokenKind::LiteralString);
					EXPECT_EQ(bytes_to_ascii(next_res.value().bytes), "Literal");
				}
			}

			auto tok3 = oom_lex.next_token();
			RECTO_ASSERT_OK(tok3);
			EXPECT_EQ(tok3.value().kind, TokenKind::HexString);
			EXPECT_EQ(bytes_to_ascii(tok3.value().bytes), "Hello");
		}
	}

	// 7. Proxy-free buffer edge cases: PdfErrorMessage aliased self-assignment
	// across heap/inline boundaries and TokenText SIZE_MAX capacity overflow guard.
	{
		PdfErrorMessage msg(std::string(200, 'A'));
		msg = msg.view().substr(1);
		EXPECT_EQ(msg.size(), 199u);
		EXPECT_EQ(msg.view(), std::string(199, 'A'));
		msg = msg.view().substr(150);
		EXPECT_EQ(msg.size(), 49u);
		EXPECT_EQ(msg.view(), std::string(49, 'A'));

		TokenText text("abc");
		EXPECT_THROW(text.reserve(std::numeric_limits<size_t>::max()), std::bad_alloc);
		EXPECT_EQ(text, "abc");
	}
}

TEST(PdfLexer, Numbers)
{
	const auto valid_buf = to_bytes(
		"0 +0 -0 123 -456 +789 9223372036854775807 -9223372036854775808 "
		"99999999999999999999999 -99999999999999999999999 "
		"34.5 -3.62 +123.6 4. -.002 0.0 .5");
	Lexer lex(valid_buf);

	EXPECT_EQ(lex.next_token().value().int_value, 0);
	EXPECT_EQ(lex.next_token().value().int_value, 0);
	EXPECT_EQ(lex.next_token().value().int_value, 0);
	EXPECT_EQ(lex.next_token().value().int_value, 123);
	EXPECT_EQ(lex.next_token().value().int_value, -456);
	EXPECT_EQ(lex.next_token().value().int_value, 789);
	EXPECT_EQ(lex.next_token().value().int_value, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(lex.next_token().value().int_value, std::numeric_limits<int64_t>::min());

	// Overflowing 64-bit integers clamp to int64_t::max / min without UB and
	// record a D16 diagnostic entry.
	clear_diagnostics();
	EXPECT_EQ(lex.next_token().value().int_value, std::numeric_limits<int64_t>::max());
	EXPECT_EQ(lex.next_token().value().int_value, std::numeric_limits<int64_t>::min());
	EXPECT_EQ(diagnostics_tail(10).size(), 2u);

	auto r1 = lex.next_token();
	RECTO_ASSERT_OK(r1);
	EXPECT_EQ(r1.value().kind, TokenKind::Real);
	EXPECT_DOUBLE_EQ(r1.value().real_value, 34.5);
	EXPECT_DOUBLE_EQ(lex.next_token().value().real_value, -3.62);
	EXPECT_DOUBLE_EQ(lex.next_token().value().real_value, 123.6);
	EXPECT_DOUBLE_EQ(lex.next_token().value().real_value, 4.0);
	EXPECT_DOUBLE_EQ(lex.next_token().value().real_value, -0.002);
	EXPECT_DOUBLE_EQ(lex.next_token().value().real_value, 0.0);
	EXPECT_DOUBLE_EQ(lex.next_token().value().real_value, 0.5);
	EXPECT_TRUE(lex.next_token().value().is_eof());

	// Malformed numeric tokens return ErrorCode::DamagedFormat.
	for (std::string_view bad_num : {"--5", "1.2.3", "+", "-", ".", "+.", "-."}) {
		const auto bad_bytes = to_bytes(bad_num);
		Lexer bad_lex(bad_bytes);
		RECTO_EXPECT_ERR(bad_lex.next_token(), ErrorCode::DamagedFormat);
	}
}

// `{` and `}` delimit only in a Type 4 function's PostScript calculator syntax (ISO 32000-2 section 7.2.2 errata).
TEST(PdfLexer, BracesDelimitOnlyInCalculatorSyntax)
{
	const auto pdf_buf = to_bytes("/A{B} /C}D {x}");
	Lexer pdf_lex(pdf_buf);
	EXPECT_EQ(pdf_lex.syntax(), LexerSyntax::Pdf);
	auto n1 = pdf_lex.next_token();
	RECTO_ASSERT_OK(n1);
	EXPECT_EQ(n1.value().kind, TokenKind::Name);
	EXPECT_EQ(n1.value().text, "A{B}");
	auto n2 = pdf_lex.next_token();
	RECTO_ASSERT_OK(n2);
	EXPECT_EQ(n2.value().text, "C}D");
	EXPECT_TRUE(pdf_lex.next_token().value().is_keyword("{x}"));
	EXPECT_TRUE(pdf_lex.next_token().value().is_eof());

	const auto calc_buf = to_bytes("{ 2 mul {x}if }");
	Lexer calc_lex(calc_buf);
	calc_lex.set_syntax(LexerSyntax::PostScriptCalculator);
	EXPECT_EQ(calc_lex.next_token().value().kind, TokenKind::BraceOpen);
	EXPECT_EQ(calc_lex.next_token().value().int_value, 2);
	EXPECT_TRUE(calc_lex.next_token().value().is_keyword("mul"));
	EXPECT_EQ(calc_lex.next_token().value().kind, TokenKind::BraceOpen);
	EXPECT_TRUE(calc_lex.next_token().value().is_keyword("x"));
	EXPECT_EQ(calc_lex.next_token().value().kind, TokenKind::BraceClose);
	EXPECT_TRUE(calc_lex.next_token().value().is_keyword("if"));
	EXPECT_EQ(calc_lex.next_token().value().kind, TokenKind::BraceClose);
	EXPECT_TRUE(calc_lex.next_token().value().is_eof());

	// The parser reads such a dictionary key as one name.
	const auto dict_buf = to_bytes("<< /A{B} 1 >>");
	Parser parser(dict_buf);
	auto dict = parser.parse_dictionary();
	RECTO_ASSERT_OK(dict);
	EXPECT_EQ(dict.value().get_int("A{B}"), std::optional<int64_t>(1));
}

TEST(PdfLexer, LiteralStrings)
{
	// ISO 32000-1 section 7.3.4.2 examples:
	// 1. Nested balanced parentheses and basic escapes
	{
		const auto buf = to_bytes(
			"(This is a string) "
			"(Strings may contain balanced parentheses ( ) and (a(b(c)d)e)!) "
			"() "
			"(\\n\\r\\t\\b\\f\\(\\)\\\\)");
		Lexer lex(buf);

		auto s1 = lex.next_token();
		RECTO_ASSERT_OK(s1);
		EXPECT_EQ(bytes_to_ascii(s1.value().bytes), "This is a string");

		auto s2 = lex.next_token();
		RECTO_ASSERT_OK(s2);
		EXPECT_EQ(
			bytes_to_ascii(s2.value().bytes),
			"Strings may contain balanced parentheses ( ) and (a(b(c)d)e)!");

		auto s3 = lex.next_token();
		RECTO_ASSERT_OK(s3);
		EXPECT_TRUE(s3.value().bytes.empty());

		auto s4 = lex.next_token();
		RECTO_ASSERT_OK(s4);
		EXPECT_EQ(bytes_to_ascii(s4.value().bytes), "\n\r\t\b\f()\\");
	}

	// 2. Backslash-newline continuation (\LF, \CR, \CRLF) and unescaped CR / CRLF -> LF
	{
		const auto buf = to_bytes(
			"(These \\\ntwo strings \\\r\nare the \\\rsame.) "
			"(Line1\rLine2\r\nLine3\nLine4)");
		Lexer lex(buf);

		auto s1 = lex.next_token();
		RECTO_ASSERT_OK(s1);
		EXPECT_EQ(bytes_to_ascii(s1.value().bytes), "These two strings are the same.");

		auto s2 = lex.next_token();
		RECTO_ASSERT_OK(s2);
		EXPECT_EQ(bytes_to_ascii(s2.value().bytes), "Line1\nLine2\nLine3\nLine4");
	}

	// 3. Octal escapes: 1, 2, and 3 octal digits (\245, \307, \053, \53, \53a)
	{
		const auto buf = to_bytes("(\\245\\307) (\\053) (\\53) (\\539) (\\0053)");
		Lexer lex(buf);

		auto s1 = lex.next_token();
		RECTO_ASSERT_OK(s1);
		const std::vector<uint8_t> expected1 = {0xA5u, 0xC7u};
		EXPECT_EQ(s1.value().bytes, expected1);

		EXPECT_EQ(bytes_to_ascii(lex.next_token().value().bytes), "+");
		EXPECT_EQ(bytes_to_ascii(lex.next_token().value().bytes), "+");
		EXPECT_EQ(bytes_to_ascii(lex.next_token().value().bytes), "+9");
		const std::vector<uint8_t> expected_0053 = {0x05u, '3'};
		EXPECT_EQ(lex.next_token().value().bytes, expected_0053);
	}

	// 3b. ISO 32000-1 section 7.3.4.2, examples 3 and 4 as printed: an end-of-line kept at the end of a string,
	// written raw and as \n, and octal escapes inside text.
	{
		const auto buf = to_bytes(
			"(This string has an end-of-line at the end of it.\n) "
			"(So does this one.\\n) "
			"(This string contains \\245two octal characters\\307.)");
		Lexer lex(buf);
		EXPECT_EQ(bytes_to_ascii(lex.next_token().value().bytes), "This string has an end-of-line at the end of it.\n");
		EXPECT_EQ(bytes_to_ascii(lex.next_token().value().bytes), "So does this one.\n");
		const std::string_view octal_text = "This string contains \xA5two octal characters\xC7.";
		const std::vector<uint8_t> expected_octal(octal_text.begin(), octal_text.end());
		EXPECT_EQ(lex.next_token().value().bytes, expected_octal);
	}

	// 4. Token byte ceiling enforced before growth.
	{
		const auto buf = to_bytes("(1234) (12345)");
		Lexer lex(buf, false, 4);
		RECTO_ASSERT_OK(lex.next_token());
		RECTO_EXPECT_ERR(lex.next_token(), ErrorCode::DecompressionLimitExceeded);
	}
}

TEST(PdfLexer, HexStrings)
{
	static constexpr char kHexSample[] =
		"<48656C6C6F> <901FA3> <901FA> <41 42\r\n\t43\x00" " 4> <>";
	const auto buf = to_bytes(std::string_view(kHexSample, sizeof(kHexSample) - 1));
	Lexer lex(buf);

	auto h1 = lex.next_token();
	RECTO_ASSERT_OK(h1);
	EXPECT_EQ(h1.value().kind, TokenKind::HexString);
	EXPECT_EQ(bytes_to_ascii(h1.value().bytes), "Hello");

	auto h2 = lex.next_token();
	RECTO_ASSERT_OK(h2);
	const std::vector<uint8_t> exp2 = {0x90u, 0x1Fu, 0xA3u};
	EXPECT_EQ(h2.value().bytes, exp2);

	// Odd trailing hex digit padded with 0 (<901FA> -> 0x90, 0x1F, 0xA0).
	auto h3 = lex.next_token();
	RECTO_ASSERT_OK(h3);
	const std::vector<uint8_t> exp3 = {0x90u, 0x1Fu, 0xA0u};
	EXPECT_EQ(h3.value().bytes, exp3);

	// Embedded whitespace (including NUL) ignored and odd digit padded.
	auto h4 = lex.next_token();
	RECTO_ASSERT_OK(h4);
	const std::vector<uint8_t> exp4 = {'A', 'B', 'C', 0x40u};
	EXPECT_EQ(h4.value().bytes, exp4);

	// Empty <> hex string.
	auto h5 = lex.next_token();
	RECTO_ASSERT_OK(h5);
	EXPECT_TRUE(h5.value().bytes.empty());

	// Non-hex character inside <...> returns ErrorCode::DamagedFormat.
	const auto bad_hex = to_bytes("<41G2>");
	Lexer bad_lex(bad_hex);
	RECTO_EXPECT_ERR(bad_lex.next_token(), ErrorCode::DamagedFormat);

	// Ceiling check applies both to full byte pairs and to final odd-nibble padding.
	const auto ceil_hex = to_bytes("<4142> <4142A> <414243>");
	Lexer ceil_lex(ceil_hex, false, 2);
	RECTO_ASSERT_OK(ceil_lex.next_token());
	RECTO_EXPECT_ERR(ceil_lex.next_token(), ErrorCode::DecompressionLimitExceeded);
}

TEST(PdfLexer, Names)
{
	// ISO 32000-1 section 7.3.5 Table 4 examples:
	const auto buf = to_bytes(
		"/Name1 /ASomewhatLongerName /A;Name_With-Various***Characters? "
		"/1.2 /$$ /@pattern /.notdef /Lime#20Green /paired#28#29parentheses "
		"/The_Key_of_F#23_Minor /A#42 /#2FA /");
	Lexer lex(buf);

	EXPECT_EQ(lex.next_token().value().text, "Name1");
	EXPECT_EQ(lex.next_token().value().text, "ASomewhatLongerName");
	EXPECT_EQ(lex.next_token().value().text, "A;Name_With-Various***Characters?");
	EXPECT_EQ(lex.next_token().value().text, "1.2");
	EXPECT_EQ(lex.next_token().value().text, "$$");
	EXPECT_EQ(lex.next_token().value().text, "@pattern");
	EXPECT_EQ(lex.next_token().value().text, ".notdef");
	EXPECT_EQ(lex.next_token().value().text, "Lime Green");
	EXPECT_EQ(lex.next_token().value().text, "paired()parentheses");
	EXPECT_EQ(lex.next_token().value().text, "The_Key_of_F#_Minor");
	EXPECT_EQ(lex.next_token().value().text, "AB");
	EXPECT_EQ(lex.next_token().value().text, "/A");
	EXPECT_EQ(lex.next_token().value().text, "");

	// Reject /#00 null escape and truncated / invalid #XX escapes.
	for (std::string_view bad_name : {"/#00", "/#2", "/#2G", "/#"}) {
		const auto bad_bytes = to_bytes(bad_name);
		Lexer bad_lex(bad_bytes);
		RECTO_EXPECT_ERR(bad_lex.next_token(), ErrorCode::DamagedFormat);
	}
}

TEST(PdfLexer, StreamEolRule)
{
	// 1. stream\n\nDATA -> first \n is EOL, second \n is preserved as byte 0 of stream,
	// even when peek_token(1) was called before consuming `stream`!
	{
		const auto buf = to_bytes("stream\n\nDATA");
		Lexer lex(buf);
		RECTO_ASSERT_OK(lex.peek_token(0));
		RECTO_ASSERT_OK(lex.peek_token(1));
		auto kw = lex.next_token();
		RECTO_ASSERT_OK(kw);
		EXPECT_TRUE(kw.value().is_keyword("stream"));
		EXPECT_EQ(lex.position(), 6u);
		auto offset = lex.consume_stream_eol();
		RECTO_ASSERT_OK(offset);
		EXPECT_EQ(offset.value(), 7u);
		EXPECT_EQ(buf[offset.value()], '\n');
	}

	// 2. stream\r\nDATA -> \r\n consumed as 2-byte EOL, byte 0 is 'D'
	{
		const auto buf = to_bytes("stream\r\nDATA");
		Lexer lex(buf);
		RECTO_ASSERT_OK(lex.peek_token(1));
		RECTO_ASSERT_OK(lex.next_token());
		auto offset = lex.consume_stream_eol();
		RECTO_ASSERT_OK(offset);
		EXPECT_EQ(offset.value(), 8u);
		EXPECT_EQ(buf[offset.value()], 'D');
	}

	// 3. stream  \t \r\n\nDATA -> horizontal spaces skipped, \r\n consumed, \n is byte 0
	{
		const auto buf = to_bytes("stream  \t \r\n\nDATA");
		Lexer lex(buf);
		RECTO_ASSERT_OK(lex.next_token());
		auto offset = lex.consume_stream_eol();
		RECTO_ASSERT_OK(offset);
		EXPECT_EQ(buf[offset.value()], '\n');
	}

	// 4. Bare \r (not followed by \n) after stream keyword is rejected per
	// ISO 32000-1 section 7.3.8.1 ("not by a single CARRIAGE RETURN alone").
	{
		const auto buf = to_bytes("stream\rXDATA");
		Lexer lex(buf);
		RECTO_ASSERT_OK(lex.next_token());
		RECTO_EXPECT_ERR(lex.consume_stream_eol(), ErrorCode::DamagedFormat);
	}
}

TEST(PdfLexer, TruncationSweep)
{
	static constexpr char kComplexSample[] =
		"%PDF-2.0\r\n"
		"%Comment\x00" "with\xFF" "binary\n"
		"15 0 obj\n"
		"<< /Type /Page /NameWith#20Esc#28#29 /Literal (nested (a(b)c) \\n \\053 \\\r\n cont) "
		"/Hex <48 65 6C 6C 6F 9> /Nums [0 -42 +3.14159 -.5 42. true false null] >>\n"
		"stream\r\n\nPayload\r\nendstream\nendobj\n%%EOF";
	const auto complex_buffer = to_bytes(
		std::string_view(kComplexSample, sizeof(kComplexSample) - 1));

	for (size_t cut = 0; cut <= complex_buffer.size(); ++cut) {
		// Copy into an exact-sized vector so AddressSanitizer guards the exact
		// end of the buffer at offset `cut`.
		const std::vector<uint8_t> slice(
			complex_buffer.begin(),
			complex_buffer.begin() + static_cast<std::ptrdiff_t>(cut));
		Lexer lex(slice, cut % 2 == 0);
		for (size_t steps = 0; steps < 128; ++steps) {
			(void)lex.peek_token(0);
			(void)lex.peek_token(1);
			auto res = lex.next_token();
			if (!res.has_value() || res.value().is_eof()) {
				break;
			}
			EXPECT_LE(res.value().byte_offset + res.value().byte_length, slice.size());
			if (res.value().is_keyword("stream")) {
				auto eol_res = lex.consume_stream_eol();
				if (!eol_res.has_value()) {
					break;
				}
				EXPECT_LE(eol_res.value(), slice.size());
			}
		}
	}

	// Verify the full untruncated complex_buffer lexes end-to-end with zero errors.
	Lexer full_lex(complex_buffer, false);
	bool saw_stream = false;
	while (true) {
		RECTO_ASSERT_OK(full_lex.peek_token(0));
		RECTO_ASSERT_OK(full_lex.peek_token(1));
		auto tok_res = full_lex.next_token();
		RECTO_ASSERT_OK(tok_res);
		if (tok_res.value().is_eof()) {
			break;
		}
		if (tok_res.value().is_keyword("stream")) {
			saw_stream = true;
			auto eol_offset = full_lex.consume_stream_eol();
			RECTO_ASSERT_OK(eol_offset);
			ASSERT_LT(eol_offset.value(), complex_buffer.size());
			EXPECT_EQ(complex_buffer[eol_offset.value()], '\n');
		}
	}
	EXPECT_TRUE(saw_stream);
}

} // namespace
} // namespace recto::pdf
