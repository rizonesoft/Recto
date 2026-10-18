// Unit tests for the bounded recursive-descent PDF 1.7 / 2.0 object and stream
// parser (D01 T01 section 4).
#include "recto_pdf.h"
#include "test_support.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {
namespace {

[[nodiscard]] std::span<const uint8_t> as_bytes(std::string_view sv) noexcept
{
	return {
		reinterpret_cast<const uint8_t*>(sv.data()),
		sv.size()};
}

TEST(PdfParser, PrimitivesAndComments)
{
	constexpr std::string_view kInput =
		"% leading comment\n"
		"true false null -42 3.14159 /Name#20With#20Spaces (Literal\\nString) <48656C6C6F>";

	Parser parser(as_bytes(kInput));

	auto v_true = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_true);
	EXPECT_EQ(v_true->as_bool(), true);

	auto v_false = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_false);
	EXPECT_EQ(v_false->as_bool(), false);

	auto v_null = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_null);
	EXPECT_TRUE(v_null->is_null());

	auto v_int = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_int);
	EXPECT_EQ(v_int->as_int(), -42);

	auto v_real = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_real);
	ASSERT_TRUE(v_real->as_real().has_value());
	EXPECT_DOUBLE_EQ(*v_real->as_real(), 3.14159);

	auto v_name = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_name);
	EXPECT_EQ(v_name->as_name(), "Name With Spaces");

	auto v_lit = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_lit);
	ASSERT_NE(v_lit->as_string(), nullptr);
	EXPECT_FALSE(v_lit->as_string()->is_hex);
	EXPECT_EQ(v_lit->as_string()->as_byte_view(), "Literal\nString");

	auto v_hex = parser.parse_direct_object();
	RECTO_ASSERT_OK(v_hex);
	ASSERT_NE(v_hex->as_string(), nullptr);
	EXPECT_TRUE(v_hex->as_string()->is_hex);
	EXPECT_EQ(v_hex->as_string()->as_byte_view(), "Hello");

	// Next parse at EOF must return DamagedFormat.
	RECTO_EXPECT_ERR(parser.parse_direct_object(), ErrorCode::DamagedFormat);
}

TEST(PdfParser, IndirectRefsAndIntegers)
{
	// Mixed integer and indirect reference array: [10 0 R 20 30 15 2 R 40]
	{
		constexpr std::string_view kArrayInput = "[10 0 R 20 30 15 2 R 40]";
		Parser parser(as_bytes(kArrayInput));
		auto arr_res = parser.parse_array();
		RECTO_ASSERT_OK(arr_res);
		const PdfArray& arr = *arr_res;
		ASSERT_EQ(arr.size(), 5u);

		EXPECT_EQ(arr[0].as_ref(), (IndirectRef{10u, 0u}));
		EXPECT_EQ(arr[1].as_int(), 20);
		EXPECT_EQ(arr[2].as_int(), 30);
		EXPECT_EQ(arr[3].as_ref(), (IndirectRef{15u, 2u}));
		EXPECT_EQ(arr[4].as_int(), 40);
	}

	// Trailing two integers [12 0] without 'R' must parse as two integers.
	{
		constexpr std::string_view kTwoInts = "[12 0]";
		Parser parser(as_bytes(kTwoInts));
		auto arr_res = parser.parse_array();
		RECTO_ASSERT_OK(arr_res);
		ASSERT_EQ(arr_res->size(), 2u);
		EXPECT_EQ((*arr_res)[0].as_int(), 12);
		EXPECT_EQ((*arr_res)[1].as_int(), 0);
	}

	// Dictionary with indirect reference values and integer values.
	{
		constexpr std::string_view kDictInput =
			"<< /Length 15 0 R /Count 2 /First 10 /Last 20 /Font 7 1 R >>";
		Parser parser(as_bytes(kDictInput));
		auto dict_res = parser.parse_dictionary();
		RECTO_ASSERT_OK(dict_res);
		const PdfDict& dict = *dict_res;
		EXPECT_EQ(dict.get_ref("Length"), (IndirectRef{15u, 0u}));
		EXPECT_EQ(dict.get_int("Count"), 2);
		EXPECT_EQ(dict.get_int("First"), 10);
		EXPECT_EQ(dict.get_int("Last"), 20);
		EXPECT_EQ(dict.get_ref("Font"), (IndirectRef{7u, 1u}));
	}

	// 3-token lookahead with embedded comments when Lexer::emit_comments is true.
	{
		constexpr std::string_view kCommentedRef =
			"[10 % comment 1\n 0 % comment 2\n R 25 % comment 3\n 30]";
		Lexer lexer(as_bytes(kCommentedRef), /*emit_comments=*/true);
		Parser parser(lexer);
		auto arr_res = parser.parse_array();
		RECTO_ASSERT_OK(arr_res);
		ASSERT_EQ(arr_res->size(), 3u);
		EXPECT_EQ((*arr_res)[0].as_ref(), (IndirectRef{10u, 0u}));
		EXPECT_EQ((*arr_res)[1].as_int(), 25);
		EXPECT_EQ((*arr_res)[2].as_int(), 30);
	}

	// Out-of-range object or generation numbers in N G R must return DamagedFormat.
	{
		Parser p_zero(as_bytes("0 0 R"));
		RECTO_EXPECT_ERR(p_zero.parse_direct_object(), ErrorCode::DamagedFormat);

		Parser p_neg(as_bytes("-5 0 R"));
		RECTO_EXPECT_ERR(p_neg.parse_direct_object(), ErrorCode::DamagedFormat);

		Parser p_gen_neg(as_bytes("1 -1 R"));
		RECTO_EXPECT_ERR(p_gen_neg.parse_direct_object(), ErrorCode::DamagedFormat);

		Parser p_gen_overflow(as_bytes("1 65536 R"));
		RECTO_EXPECT_ERR(p_gen_overflow.parse_direct_object(), ErrorCode::DamagedFormat);

		Parser p_obj_overflow(as_bytes("4294967296 0 R"));
		RECTO_EXPECT_ERR(p_obj_overflow.parse_direct_object(), ErrorCode::DamagedFormat);
	}
}

TEST(PdfParser, ArraysAndDicts)
{
	// Nested dictionary and array structure with null value elision per ISO 32000-1 section 7.3.7.
	constexpr std::string_view kComplexDict =
		"<<\n"
		"  /Type /Page\n"
		"  /MediaBox [0 0 612.0 792]\n"
		"  /NullEntry null\n"
		"  /OverwrittenByNull (initial)\n"
		"  /OverwrittenByNull null\n"
		"  /Resources <<\n"
		"    /Font << /F1 5 0 R /F2 null >>\n"
		"    /ProcSet [/PDF /Text]\n"
		"  >>\n"
		"  /#2FSlashKey (escaped slash key)\n"
		">>";

	Parser parser(as_bytes(kComplexDict));
	auto val_res = parser.parse_direct_object();
	RECTO_ASSERT_OK(val_res);
	ASSERT_TRUE(val_res->is_dict());
	const PdfDict& dict = *val_res->as_dict();

	EXPECT_EQ(dict.get_name("Type"), "Page");
	EXPECT_FALSE(dict.contains("NullEntry"));
	EXPECT_EQ(dict.get("NullEntry"), nullptr);
	EXPECT_FALSE(dict.contains("OverwrittenByNull"));

	const auto rect = dict.get_rect("MediaBox");
	ASSERT_TRUE(rect.has_value());
	EXPECT_DOUBLE_EQ(rect->x_min, 0.0);
	EXPECT_DOUBLE_EQ(rect->y_min, 0.0);
	EXPECT_DOUBLE_EQ(rect->x_max, 612.0);
	EXPECT_DOUBLE_EQ(rect->y_max, 792.0);

	const PdfDict* resources = dict.get_dict("Resources");
	ASSERT_NE(resources, nullptr);
	const PdfDict* font = resources->get_dict("Font");
	ASSERT_NE(font, nullptr);
	EXPECT_EQ(font->get_ref("F1"), (IndirectRef{5u, 0u}));
	EXPECT_FALSE(font->contains("F2"));

	const PdfArray* procset = resources->get_array("ProcSet");
	ASSERT_NE(procset, nullptr);
	ASSERT_EQ(procset->size(), 2u);
	EXPECT_EQ((*procset)[0].as_name(), "PDF");
	EXPECT_EQ((*procset)[1].as_name(), "Text");

	// Key "/#2FSlashKey" decodes to "/SlashKey" (preserving the decoded leading '/').
	const PdfValue* slash_val = dict.get(PdfName::from_decoded("/SlashKey"));
	ASSERT_NE(slash_val, nullptr);
	ASSERT_NE(slash_val->as_string(), nullptr);
	EXPECT_EQ(slash_val->as_string()->as_byte_view(), "escaped slash key");

	// Container element ceiling enforcement.
	{
		Parser limited_arr(as_bytes("[1 2 3 4]"), kMaxParserRecursionDepth, 3);
		RECTO_EXPECT_ERR(limited_arr.parse_array(), ErrorCode::DamagedFormat);

		Parser limited_dict(as_bytes("<< /A 1 /B 2 /C 3 /D 4 >>"), kMaxParserRecursionDepth, 3);
		RECTO_EXPECT_ERR(limited_dict.parse_dictionary(), ErrorCode::DamagedFormat);
	}
}

TEST(PdfParser, RecursionLimit)
{
	// 1. Exactly 64-deep nested arrays must succeed.
	{
		std::string nested_64(64, '[');
		nested_64 += "42";
		nested_64.append(64, ']');

		Parser parser(as_bytes(nested_64));
		auto res = parser.parse_direct_object();
		RECTO_ASSERT_OK(res);
		const PdfValue* cur = &(*res);
		for (size_t d = 0; d < 64; ++d) {
			ASSERT_TRUE(cur->is_array()) << "depth " << d;
			ASSERT_EQ(cur->as_array()->size(), 1u);
			cur = &((*cur->as_array())[0]);
		}
		EXPECT_EQ(cur->as_int(), 42);
	}

	// 2. 65-deep nested arrays must fail with ErrorCode::RecursionLimitExceeded.
	{
		std::string nested_65(65, '[');
		nested_65 += "42";
		nested_65.append(65, ']');

		Parser parser(as_bytes(nested_65));
		RECTO_EXPECT_ERR(parser.parse_direct_object(), ErrorCode::RecursionLimitExceeded);
	}

	// 3. Exactly 64-deep nested dictionaries must succeed.
	{
		std::string dict_64;
		for (size_t d = 0; d < 64; ++d) {
			dict_64 += "<< /K ";
		}
		dict_64 += "99";
		for (size_t d = 0; d < 64; ++d) {
			dict_64 += " >>";
		}

		Parser parser(as_bytes(dict_64));
		auto res = parser.parse_direct_object();
		RECTO_ASSERT_OK(res);
		const PdfValue* cur = &(*res);
		for (size_t d = 0; d < 64; ++d) {
			ASSERT_TRUE(cur->is_dict()) << "depth " << d;
			cur = cur->as_dict()->get("K");
			ASSERT_NE(cur, nullptr);
		}
		EXPECT_EQ(cur->as_int(), 99);
	}

	// 4. 65-deep nested dictionaries must fail with ErrorCode::RecursionLimitExceeded.
	{
		std::string dict_65;
		for (size_t d = 0; d < 65; ++d) {
			dict_65 += "<< /K ";
		}
		dict_65 += "99";
		for (size_t d = 0; d < 65; ++d) {
			dict_65 += " >>";
		}

		Parser parser(as_bytes(dict_65));
		RECTO_EXPECT_ERR(parser.parse_direct_object(), ErrorCode::RecursionLimitExceeded);
	}

	// 5. Corpus fixture tests/corpus/broken/deep-nested-array-70.pdf:
	// Object 6 0 obj holds a 70-level nested array inside a dictionary and must
	// be rejected with ErrorCode::RecursionLimitExceeded.
	{
		const auto bytes = test::read_file_bytes(
			test::corpus_path("broken/deep-nested-array-70.pdf"));
		ASSERT_FALSE(bytes.empty());

		const std::string_view file_view(
			reinterpret_cast<const char*>(bytes.data()),
			bytes.size());
		const size_t obj6_pos = file_view.find("6 0 obj");
		ASSERT_NE(obj6_pos, std::string_view::npos);

		Parser parser(bytes);
		parser.seek(obj6_pos);
		auto obj6_res = parser.parse_indirect_object();
		RECTO_EXPECT_ERR(obj6_res, ErrorCode::RecursionLimitExceeded);
		EXPECT_EQ(obj6_res.error().object_num, 6u);
		EXPECT_EQ(obj6_res.error().generation_num, 0u);
	}
}

TEST(PdfParser, IndirectObjects)
{
	// Valid indirect object definitions.
	{
		constexpr std::string_view kObj =
			"12 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj";
		Parser parser(as_bytes(kObj));
		auto res = parser.parse_indirect_object();
		RECTO_ASSERT_OK(res);
		EXPECT_EQ(res->ref, (IndirectRef{12u, 0u}));
		ASSERT_TRUE(res->value.is_dict());
		EXPECT_EQ(res->value.as_dict()->get_name("Type"), "Catalog");
		EXPECT_EQ(res->value.as_dict()->get_ref("Pages"), (IndirectRef{2u, 0u}));
	}

	// Maximum valid generation number 65535.
	{
		constexpr std::string_view kMaxGen = "1 65535 obj (hello) endobj";
		Parser parser(as_bytes(kMaxGen));
		auto res = parser.parse_indirect_object();
		RECTO_ASSERT_OK(res);
		EXPECT_EQ(res->ref, (IndirectRef{1u, 65535u}));
		ASSERT_NE(res->value.as_string(), nullptr);
		EXPECT_EQ(res->value.as_string()->as_byte_view(), "hello");
	}

	// Reject object number <= 0 or > UINT32_MAX, and generation < 0 or > 65535.
	{
		Parser p_zero(as_bytes("0 0 obj null endobj"));
		RECTO_EXPECT_ERR(p_zero.parse_indirect_object(), ErrorCode::DamagedFormat);

		Parser p_neg(as_bytes("-1 0 obj null endobj"));
		RECTO_EXPECT_ERR(p_neg.parse_indirect_object(), ErrorCode::DamagedFormat);

		Parser p_overflow(as_bytes("4294967296 0 obj null endobj"));
		RECTO_EXPECT_ERR(p_overflow.parse_indirect_object(), ErrorCode::DamagedFormat);

		Parser p_gen_neg(as_bytes("1 -1 obj null endobj"));
		RECTO_EXPECT_ERR(p_gen_neg.parse_indirect_object(), ErrorCode::DamagedFormat);

		Parser p_gen_overflow(as_bytes("1 65536 obj null endobj"));
		RECTO_EXPECT_ERR(p_gen_overflow.parse_indirect_object(), ErrorCode::DamagedFormat);

		Parser p_missing_obj(as_bytes("1 0 (body) endobj"));
		RECTO_EXPECT_ERR(p_missing_obj.parse_indirect_object(), ErrorCode::DamagedFormat);

		Parser p_missing_endobj(as_bytes("1 0 obj (body)"));
		RECTO_EXPECT_ERR(p_missing_endobj.parse_indirect_object(), ErrorCode::DamagedFormat);
	}
}

TEST(PdfParser, Streams)
{
	// 1. Direct /Length with exact byte count (including binary and embedded newlines).
	{
		constexpr std::string_view kStreamObj =
			"4 0 obj\n"
			"<< /Length 12 >>\n"
			"stream\r\n"
			"Hello\nWorld!\r\n"
			"endstream\n"
			"endobj";
		Parser parser(as_bytes(kStreamObj));
		auto res = parser.parse_indirect_object();
		RECTO_ASSERT_OK(res);
		EXPECT_EQ(res->ref, (IndirectRef{4u, 0u}));
		ASSERT_TRUE(res->value.is_stream());
		const PdfStream* stm = res->value.as_stream();
		ASSERT_NE(stm, nullptr);
		EXPECT_EQ(stm->dict.get_int("Length"), 12);
		EXPECT_EQ(stm->raw_length, 12u);
		const std::string_view payload(
			reinterpret_cast<const char*>(stm->raw_bytes.data()),
			stm->raw_bytes.size());
		EXPECT_EQ(payload, "Hello\nWorld!");
	}

	// 2. Indirect /Length 15 0 R resolved via callback.
	{
		constexpr std::string_view kIndirectLenObj =
			"7 0 obj\n"
			"<< /Length 15 0 R >>\n"
			"stream\n"
			"IndirectLengthPayload"
			"\nendstream\n"
			"endobj";
		Parser parser(as_bytes(kIndirectLenObj));
		bool callback_called = false;
		auto res = parser.parse_indirect_object(
			[&](IndirectRef ref) -> PdfResult<int64_t> {
				callback_called = true;
				EXPECT_EQ(ref, (IndirectRef{15u, 0u}));
				return 21;
			});
		RECTO_ASSERT_OK(res);
		EXPECT_TRUE(callback_called);
		ASSERT_TRUE(res->value.is_stream());
		const PdfStream* stm = res->value.as_stream();
		const std::string_view payload(
			reinterpret_cast<const char*>(stm->raw_bytes.data()),
			stm->raw_bytes.size());
		EXPECT_EQ(payload, "IndirectLengthPayload");
	}

	// 3. Fallback scan when /Length is wrong (too large, too small, negative, or missing).
	{
		clear_diagnostics();
		constexpr std::string_view kWrongLenObj =
			"8 0 obj\n"
			"<< /Length 99999 >>\n"
			"stream\n"
			"RecoveredPayloadBytes\r\n"
			"endstream\n"
			"endobj";
		Parser parser(as_bytes(kWrongLenObj));
		auto res = parser.parse_indirect_object();
		RECTO_ASSERT_OK(res);
		ASSERT_TRUE(res->value.is_stream());
		const PdfStream* stm = res->value.as_stream();
		const std::string_view payload(
			reinterpret_cast<const char*>(stm->raw_bytes.data()),
			stm->raw_bytes.size());
		EXPECT_EQ(payload, "RecoveredPayloadBytes");

		const auto diags = diagnostics_tail();
		ASSERT_FALSE(diags.empty());
		EXPECT_EQ(diags.back().diagnostic_id, DiagnosticId::DamagedStream);
	}

	// 4. Fallback scan when payload itself contains "endstream" inside data before the real "endstream\nendobj".
	{
		constexpr std::string_view kEmbeddedEndstreamObj =
			"9 0 obj\n"
			"<< /Length 2 >>\n"
			"stream\n"
			"line1\nendstream not_endobj\nline2\n"
			"endstream\n"
			"endobj";
		Parser parser(as_bytes(kEmbeddedEndstreamObj));
		auto res = parser.parse_indirect_object();
		RECTO_ASSERT_OK(res);
		ASSERT_TRUE(res->value.is_stream());
		const PdfStream* stm = res->value.as_stream();
		const std::string_view payload(
			reinterpret_cast<const char*>(stm->raw_bytes.data()),
			stm->raw_bytes.size());
		EXPECT_EQ(payload, "line1\nendstream not_endobj\nline2");
	}

	// 5. Direct parse_stream_slice call after parse_dictionary.
	{
		constexpr std::string_view kDirectSlice =
			"<< /Length 6 >>\nstream\nABCDEF\nendstream";
		Parser parser(as_bytes(kDirectSlice));
		auto dict_res = parser.parse_dictionary();
		RECTO_ASSERT_OK(dict_res);
		auto slice_res = parser.parse_stream_slice(*dict_res);
		RECTO_ASSERT_OK(slice_res);
		const std::string_view payload(
			reinterpret_cast<const char*>(slice_res->data()),
			slice_res->size());
		EXPECT_EQ(payload, "ABCDEF");
	}
}

TEST(PdfParser, TruncationSweep)
{
	// Byte-by-byte truncation across sample indirect objects, nested dictionaries,
	// arrays, indirect references, and streams under ASan.
	const std::vector<std::string_view> samples = {
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R /Names [(a) <4142> [1 2 3 0 R << /Nested true >>]] >>\nendobj",
		"5 0 obj\n<< /Length 16 /Filter /FlateDecode >>\nstream\r\n0123456789ABCDEF\r\nendstream\nendobj",
		"<< /A [10 0 R 20 << /B (hello\\nworld) /C <DEADBEEF> /D null >>] >>",
	};

	for (const std::string_view sample : samples) {
		const auto full_bytes = as_bytes(sample);

		// Verify the full sample parses cleanly first.
		{
			Parser full_parser(full_bytes);
			if (sample.starts_with("<<")) {
				RECTO_ASSERT_OK(full_parser.parse_direct_object());
			} else {
				RECTO_ASSERT_OK(full_parser.parse_indirect_object());
			}
		}

		// Every truncated prefix 0 .. N-1 must fail with ErrorCode::DamagedFormat
		// (since `endobj` or closing `>>` at the end of each sample is incomplete).
		for (size_t cut = 0; cut < full_bytes.size(); ++cut) {
			const auto prefix = full_bytes.subspan(0, cut);
			Parser parser(prefix);
			if (sample.starts_with("<<")) {
				auto res = parser.parse_direct_object();
				RECTO_EXPECT_ERR(res, ErrorCode::DamagedFormat);
			} else {
				auto res = parser.parse_indirect_object();
				RECTO_EXPECT_ERR(res, ErrorCode::DamagedFormat);
			}
		}
	}
}

} // namespace
} // namespace recto::pdf
