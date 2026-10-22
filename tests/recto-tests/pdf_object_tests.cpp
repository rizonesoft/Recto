// Tests for PdfValue variant object model, PdfDict/PdfArray helpers,
// PdfRect/PdfMatrix normalization, PDF string UTF-8 codecs, and bounded
// deep_clone() (D01 T01 section 2).
#include "object.h"
#include "test_support.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace recto::pdf {
namespace {

static_assert(std::is_nothrow_move_constructible_v<PdfValue>);
static_assert(std::is_nothrow_move_assignable_v<PdfValue>);
static_assert(std::is_nothrow_move_constructible_v<PdfDict>);
static_assert(std::is_nothrow_move_constructible_v<PdfStream>);
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
static_assert(sizeof(PdfValue) <= 144);
#else
static_assert(sizeof(PdfValue) <= 128);
#endif

TEST(PdfObject, AllTenVariantKindsAndAccessors)
{
	// 1. PdfNull
	PdfValue v_null;
	EXPECT_EQ(v_null.kind(), PdfValueKind::Null);
	EXPECT_TRUE(v_null.is_null());
	EXPECT_FALSE(v_null.is_bool());
	EXPECT_FALSE(v_null.as_int().has_value());

	// 2. bool
	PdfValue v_bool(true);
	EXPECT_EQ(v_bool.kind(), PdfValueKind::Bool);
	EXPECT_TRUE(v_bool.is_bool());
	ASSERT_TRUE(v_bool.as_bool().has_value());
	EXPECT_TRUE(*v_bool.as_bool());

	// 3. int64_t
	PdfValue v_int(int64_t{-123456789012LL});
	EXPECT_EQ(v_int.kind(), PdfValueKind::Int);
	EXPECT_TRUE(v_int.is_int());
	EXPECT_TRUE(v_int.is_number());
	ASSERT_TRUE(v_int.as_int().has_value());
	EXPECT_EQ(*v_int.as_int(), -123456789012LL);

	// 4. double
	PdfValue v_real(3.1415926535);
	EXPECT_EQ(v_real.kind(), PdfValueKind::Real);
	EXPECT_TRUE(v_real.is_real());
	EXPECT_TRUE(v_real.is_number());
	ASSERT_TRUE(v_real.as_real().has_value());
	EXPECT_DOUBLE_EQ(*v_real.as_real(), 3.1415926535);

	// 5. PdfName (normalizes optional leading slash; from_decoded preserves exact bytes)
	PdfValue v_name(PdfName("/MediaBox"));
	EXPECT_EQ(v_name.kind(), PdfValueKind::Name);
	EXPECT_TRUE(v_name.is_name());
	ASSERT_TRUE(v_name.as_name().has_value());
	EXPECT_EQ(*v_name.as_name(), "MediaBox");
	EXPECT_EQ(*v_name.as_name_obj(), "/MediaBox");
	EXPECT_EQ(*v_name.as_name_obj(), "MediaBox");

	const PdfName decoded_slash_name = PdfName::from_decoded("/A");
	EXPECT_EQ(decoded_slash_name.view(), "/A");
	EXPECT_NE(decoded_slash_name, PdfName("A"));

	// 6. PdfString
	PdfValue v_str(PdfString("Hello PDF", false));
	EXPECT_EQ(v_str.kind(), PdfValueKind::String);
	EXPECT_TRUE(v_str.is_string());
	ASSERT_NE(v_str.as_string(), nullptr);
	const auto str_utf8 = v_str.as_string()->to_utf8();
	RECTO_ASSERT_OK(str_utf8);
	EXPECT_EQ(str_utf8.value(), "Hello PDF");

	// 7. PdfArray
	PdfArray arr;
	arr.emplace_back(10);
	arr.emplace_back(PdfName("FlateDecode"));
	PdfValue v_arr(std::move(arr));
	EXPECT_EQ(v_arr.kind(), PdfValueKind::Array);
	EXPECT_TRUE(v_arr.is_array());
	ASSERT_NE(v_arr.as_array(), nullptr);
	ASSERT_EQ(v_arr.as_array()->size(), 2u);
	EXPECT_EQ((*v_arr.as_array())[0].as_int(), std::optional<int64_t>(10));
	EXPECT_EQ((*v_arr.as_array())[1].as_name(), std::optional<std::string_view>("FlateDecode"));

	// 8. PdfDict
	PdfDict dict;
	dict.set("/Type", PdfValue(PdfName("/Page")));
	dict.set("Count", PdfValue(int64_t{5}));
	PdfValue v_dict(std::move(dict));
	EXPECT_EQ(v_dict.kind(), PdfValueKind::Dict);
	EXPECT_TRUE(v_dict.is_dict());
	ASSERT_NE(v_dict.as_dict(), nullptr);
	EXPECT_EQ(v_dict.as_dict()->get_name("Type"), std::optional<std::string_view>("Page"));

	// 9. PdfStream
	PdfDict stream_dict;
	stream_dict.set("Length", PdfValue(int64_t{4}));
	PdfStream stream(std::move(stream_dict), std::vector<uint8_t>{'q', ' ', 'Q', '\n'});
	PdfValue v_stream(std::move(stream));
	EXPECT_EQ(v_stream.kind(), PdfValueKind::Stream);
	EXPECT_TRUE(v_stream.is_stream());
	ASSERT_NE(v_stream.as_stream(), nullptr);
	EXPECT_EQ(v_stream.as_stream()->payload_bytes().size(), 4u);

	// 10. IndirectRef
	PdfValue v_ref(IndirectRef{42, 0});
	EXPECT_EQ(v_ref.kind(), PdfValueKind::Ref);
	EXPECT_TRUE(v_ref.is_ref());
	ASSERT_TRUE(v_ref.as_ref().has_value());
	EXPECT_EQ(v_ref.as_ref()->obj_num, 42u);
	EXPECT_EQ(v_ref.as_ref()->gen_num, 0u);
}

TEST(PdfObject, NumericPromotion)
{
	const PdfValue int_val(int64_t{72});
	const PdfValue real_val(72.0);
	const PdfValue neg_int(int64_t{-36});
	const PdfValue nan_val(std::numeric_limits<double>::quiet_NaN());
	const PdfValue inf_val(std::numeric_limits<double>::infinity());
	const PdfValue name_val(PdfName("NotANumber"));

	ASSERT_TRUE(int_val.as_number().has_value());
	ASSERT_TRUE(real_val.as_number().has_value());
	EXPECT_DOUBLE_EQ(*int_val.as_number(), 72.0);
	EXPECT_DOUBLE_EQ(*real_val.as_number(), 72.0);
	EXPECT_DOUBLE_EQ(*int_val.as_number(), *real_val.as_number());

	EXPECT_TRUE(int_val.as_int().has_value());
	EXPECT_FALSE(int_val.as_real().has_value());
	EXPECT_FALSE(real_val.as_int().has_value());
	EXPECT_TRUE(real_val.as_real().has_value());

	ASSERT_TRUE(neg_int.as_number().has_value());
	EXPECT_DOUBLE_EQ(*neg_int.as_number(), -36.0);

	// Non-finite doubles (NaN / Inf) and non-numeric types return std::nullopt.
	EXPECT_FALSE(nan_val.as_number().has_value());
	EXPECT_FALSE(inf_val.as_number().has_value());
	EXPECT_FALSE(name_val.as_number().has_value());
}

TEST(PdfObject, DictHelpersAndNullValueElision)
{
	PdfDict dict;
	dict.set("/Type", PdfValue(PdfName("/Catalog")));
	dict.set("Pages", PdfValue(IndirectRef{2, 0}));
	dict.set("PageCount", PdfValue(int64_t{12}));
	dict.set("Zoom", PdfValue(1.5));
	dict.set("Tagged", PdfValue(true));
	dict.set("Title", PdfValue(PdfString("Recto Manual")));
	dict.set("MediaBox", PdfValue(PdfArray{PdfValue(0), PdfValue(0), PdfValue(612), PdfValue(792.0)}));

	PdfDict sub_dict;
	sub_dict.set("Creator", PdfValue(PdfString("Recto")));
	dict.set("Info", PdfValue(std::move(sub_dict)));

	// Deterministic lookups work with or without a leading '/'.
	EXPECT_TRUE(dict.contains("Type"));
	EXPECT_TRUE(dict.contains("/Type"));
	EXPECT_FALSE(dict.contains("MissingKey"));

	EXPECT_EQ(dict.get_name("Type"), std::optional<std::string_view>("Catalog"));
	EXPECT_EQ(dict.get_name("/Type"), std::optional<std::string_view>("Catalog"));
	EXPECT_EQ(dict.get_ref("Pages"), std::optional<IndirectRef>(IndirectRef{2, 0}));
	EXPECT_EQ(dict.get_int("PageCount"), std::optional<int64_t>(12));
	EXPECT_EQ(dict.get_number("PageCount"), std::optional<double>(12.0));
	EXPECT_EQ(dict.get_real("Zoom"), std::optional<double>(1.5));
	EXPECT_EQ(dict.get_number("Zoom"), std::optional<double>(1.5));
	EXPECT_EQ(dict.get_bool("Tagged"), std::optional<bool>(true));
	ASSERT_NE(dict.get_string("Title"), nullptr);
	const auto title_utf8 = dict.get_string("Title")->to_utf8();
	RECTO_ASSERT_OK(title_utf8);
	EXPECT_EQ(title_utf8.value(), "Recto Manual");
	ASSERT_NE(dict.get_array("MediaBox"), nullptr);
	EXPECT_EQ(dict.get_array("MediaBox")->size(), 4u);
	ASSERT_NE(dict.get_dict("Info"), nullptr);
	EXPECT_NE(dict.get_dict("Info")->get_string("Creator"), nullptr);

	// Wrong-typed or missing keys return std::nullopt / nullptr without throwing.
	EXPECT_FALSE(dict.get_int("Type").has_value());
	EXPECT_FALSE(dict.get_name("PageCount").has_value());
	EXPECT_EQ(dict.get_string("PageCount"), nullptr);
	EXPECT_EQ(dict.get_array("Type"), nullptr);
	EXPECT_EQ(dict.get_dict("Type"), nullptr);
	EXPECT_FALSE(dict.get_ref("Type").has_value());
	EXPECT_EQ(dict.get("NonExistent"), nullptr);

	// Setting a key to PdfNull removes it per ISO 32000-1 section 7.3.7.
	dict.set("Zoom", PdfValue(PdfNull{}));
	EXPECT_FALSE(dict.contains("Zoom"));
	EXPECT_EQ(dict.get("Zoom"), nullptr);

	// A dictionary containing both decoded "/A" (from /#2FA) and "A" (from /A)
	// preserves both distinct keys across PdfName lookup, equality, and erase,
	// while string_view APIs consistently normalize a leading '/' to "A".
	PdfDict escaped_dict;
	escaped_dict.set(PdfName::from_decoded("/A"), PdfValue(int64_t{10}));
	escaped_dict.set(PdfName::from_decoded("A"), PdfValue(int64_t{20}));
	EXPECT_EQ(escaped_dict.size(), 2u);
	ASSERT_NE(escaped_dict.get(PdfName::from_decoded("/A")), nullptr);
	EXPECT_EQ(escaped_dict.get(PdfName::from_decoded("/A"))->as_int(), std::optional<int64_t>(10));
	ASSERT_NE(escaped_dict.get(PdfName::from_decoded("A")), nullptr);
	EXPECT_EQ(escaped_dict.get(PdfName::from_decoded("A"))->as_int(), std::optional<int64_t>(20));

	// string_view overloads normalize "/A" -> "A" consistently across set, get,
	// get_mut, contains, typed helpers, null assignment, and erase.
	EXPECT_EQ(escaped_dict.get_int("/A"), std::optional<int64_t>(20));
	EXPECT_EQ(escaped_dict.get_int("A"), std::optional<int64_t>(20));
	escaped_dict.set("/A", PdfValue(int64_t{30}));
	EXPECT_EQ(escaped_dict.get_int("/A"), std::optional<int64_t>(30));
	EXPECT_EQ(escaped_dict.get_int("A"), std::optional<int64_t>(30));
	EXPECT_EQ(escaped_dict.get(PdfName::from_decoded("/A"))->as_int(), std::optional<int64_t>(10));

	*escaped_dict.get_mut("/A") = PdfValue(int64_t{40});
	EXPECT_EQ(escaped_dict.get_int("/A"), std::optional<int64_t>(40));
	EXPECT_EQ(escaped_dict.get(PdfName::from_decoded("/A"))->as_int(), std::optional<int64_t>(10));

	EXPECT_TRUE(escaped_dict.erase("/A"));
	EXPECT_FALSE(escaped_dict.contains("/A"));
	EXPECT_FALSE(escaped_dict.contains("A"));
	EXPECT_TRUE(escaped_dict.contains(PdfName::from_decoded("/A")));
	EXPECT_EQ(escaped_dict.get(PdfName::from_decoded("/A"))->as_int(), std::optional<int64_t>(10));

	escaped_dict.set(PdfName::from_decoded("/A"), PdfValue(PdfNull{}));
	EXPECT_FALSE(escaped_dict.contains(PdfName::from_decoded("/A")));
	EXPECT_TRUE(escaped_dict.empty());
}

TEST(PdfObject, RectAndMatrixNormalization)
{
	// Standard [0 0 612 792]
	const PdfArray normal_arr{PdfValue(0), PdfValue(0), PdfValue(612), PdfValue(792)};
	const auto r1 = PdfRect::from_array(normal_arr);
	RECTO_ASSERT_OK(r1);
	EXPECT_DOUBLE_EQ(r1.value().x_min, 0.0);
	EXPECT_DOUBLE_EQ(r1.value().y_min, 0.0);
	EXPECT_DOUBLE_EQ(r1.value().x_max, 612.0);
	EXPECT_DOUBLE_EQ(r1.value().y_max, 792.0);
	EXPECT_DOUBLE_EQ(r1.value().width(), 612.0);
	EXPECT_DOUBLE_EQ(r1.value().height(), 792.0);

	// Inverted [0 792 612 0] normalizes to [0 0 612 792]
	const PdfArray inverted_arr{PdfValue(0), PdfValue(792), PdfValue(612), PdfValue(0)};
	const auto r2 = PdfRect::from_array(inverted_arr);
	RECTO_ASSERT_OK(r2);
	EXPECT_EQ(r2.value(), PdfRect(0.0, 0.0, 612.0, 792.0));

	// Both X and Y swapped [595.28 841.89 -10.5 -20.25]
	const PdfArray swapped_both{
		PdfValue(595.28), PdfValue(841.89), PdfValue(-10.5), PdfValue(-20.25)};
	const auto r3 = PdfValue(swapped_both).as_rect();
	RECTO_ASSERT_OK(r3);
	EXPECT_DOUBLE_EQ(r3.value().x_min, -10.5);
	EXPECT_DOUBLE_EQ(r3.value().y_min, -20.25);
	EXPECT_DOUBLE_EQ(r3.value().x_max, 595.28);
	EXPECT_DOUBLE_EQ(r3.value().y_max, 841.89);

	// Malformed rect arrays fail with ErrorCode::DamagedFormat
	RECTO_EXPECT_ERR(
		PdfRect::from_array(PdfArray{PdfValue(0), PdfValue(0), PdfValue(100)}),
		ErrorCode::DamagedFormat);
	RECTO_EXPECT_ERR(
		PdfRect::from_array(PdfArray{
			PdfValue(0), PdfValue(0), PdfValue(100), PdfValue(PdfName("Bad"))}),
		ErrorCode::DamagedFormat);

	// PdfMatrix extraction with mixed int/real elements
	const PdfArray matrix_arr{
		PdfValue(1), PdfValue(0.0), PdfValue(0), PdfValue(1), PdfValue(72.5), PdfValue(144)};
	const auto m = PdfValue(matrix_arr).as_matrix();
	RECTO_ASSERT_OK(m);
	EXPECT_EQ(m.value(), PdfMatrix(1.0, 0.0, 0.0, 1.0, 72.5, 144.0));

	RECTO_EXPECT_ERR(
		PdfMatrix::from_array(PdfArray{PdfValue(1), PdfValue(0)}),
		ErrorCode::DamagedFormat);
}

TEST(PdfObject, StringEncoding)
{
	// 1. Plain ASCII round-trip via PDFDocEncoding
	const std::string ascii = "Recto PDF Engine 1.7 & 2.0";
	const auto encoded_ascii = utf8_to_pdf_string(ascii);
	RECTO_ASSERT_OK(encoded_ascii);
	EXPECT_EQ(encoded_ascii.value().size(), ascii.size());
	const auto decoded_ascii = encoded_ascii.value().to_utf8();
	RECTO_ASSERT_OK(decoded_ascii);
	EXPECT_EQ(decoded_ascii.value(), ascii);

	// 2. PDFDocEncoding table characters: bullet (U+2022 = 0x80), em dash (U+2014 = 0x84),
	// left/right curly quotes (U+201C/U+201D = 0x8D/0x8E), trademark (U+2122 = 0x92),
	// fi ligature (U+FB01 = 0x93), and euro sign (U+20AC = 0xA0).
	const std::string pdf_doc_sample =
		"\xE2\x80\xA2 \xE2\x80\x9C" "Caf\xC3\xA9\xE2\x80\x9D \xE2\x80\x94 \xE2\x82\xAC" "42\xE2\x84\xA2 \xEF\xAC\x81le";
	const auto encoded_pdf_doc = utf8_to_pdf_string(pdf_doc_sample);
	RECTO_ASSERT_OK(encoded_pdf_doc);
	ASSERT_FALSE(encoded_pdf_doc.value().empty());
	// Must encode as compact 1-byte PDFDocEncoding without a UTF-16BE BOM.
	EXPECT_NE(encoded_pdf_doc.value().bytes[0], 0xFEu);
	const auto decoded_pdf_doc = encoded_pdf_doc.value().to_utf8();
	RECTO_ASSERT_OK(decoded_pdf_doc);
	EXPECT_EQ(decoded_pdf_doc.value(), pdf_doc_sample);

	// 3. CJK + emoji outside PDFDocEncoding round-trip via UTF-16BE with surrogate pairs.
	// "日本語 📄" -> U+65E5 U+672C U+8A9E U+0020 U+1F4C4
	const std::string cjk_emoji = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x93\x84";
	const auto encoded_utf16 = utf8_to_pdf_string(cjk_emoji);
	RECTO_ASSERT_OK(encoded_utf16);
	ASSERT_GE(encoded_utf16.value().size(), 2u);
	EXPECT_EQ(encoded_utf16.value().bytes[0], 0xFEu);
	EXPECT_EQ(encoded_utf16.value().bytes[1], 0xFFu);
	const auto decoded_utf16 = encoded_utf16.value().to_utf8();
	RECTO_ASSERT_OK(decoded_utf16);
	EXPECT_EQ(decoded_utf16.value(), cjk_emoji);

	// 4. UTF-16BE with embedded ISO 32000-1 section 7.9.2.2.2 language escape
	// sequence (0x001B 'e' 'n' 0x001B) strips the escape and preserves text.
	const std::vector<uint8_t> utf16_with_lang_escape = {
		0xFE, 0xFF,
		0x00, 'H',
		0x00, 0x1B, 0x00, 'e', 0x00, 'n', 0x00, 0x1B,
		0x00, 'i',
	};
	const auto decoded_utf16_esc = pdf_string_to_utf8(utf16_with_lang_escape);
	RECTO_ASSERT_OK(decoded_utf16_esc);
	EXPECT_EQ(decoded_utf16_esc.value(), "Hi");

	// 5. PDF 2.0 UTF-8 string with leading EF BB BF BOM and embedded 0x1B 'e' 'n' 0x1B
	// language escape sequence.
	const std::vector<uint8_t> utf8_bom_bytes = {
		0xEF, 0xBB, 0xBF,
		0x1B, 'e', 'n', 0x1B,
		'P', 'D', 'F', ' ', '2', '.', '0', ' ',
		0xCE, 0xA9, // U+03A9 GREEK CAPITAL LETTER OMEGA
	};
	const auto decoded_utf8_bom = pdf_string_to_utf8(utf8_bom_bytes);
	RECTO_ASSERT_OK(decoded_utf8_bom);
	EXPECT_EQ(decoded_utf8_bom.value(), "PDF 2.0 \xCE\xA9");

	// 5b. A language escape is stripped only when well formed: ESC, 2 letters, optional 2 more, ESC
	// (ISO 32000-1 section 7.9.2.2). A malformed one keeps the text after it, its ESC shown as U+FFFD.
	const std::string fffd = "\xEF\xBF\xBD";
	struct EscapeCase {
		std::vector<uint8_t> bytes;
		std::string expected;
	};
	const std::vector<EscapeCase> escape_cases = {
		{{0xFE, 0xFF, 0x00, 'A', 0x00, 0x1B, 0x00, 'e', 0x00, 'n', 0x00, 'U', 0x00, 'S', 0x00, 0x1B, 0x00, 'B'}, "AB"},
		{{0xFE, 0xFF, 0x00, 'A', 0x00, 0x1B, 0x00, 'B'}, "A" + fffd + "B"}, // unterminated
		{{0xFE, 0xFF, 0x00, 'A', 0x00, 0x1B, 0x00, 'B', 0x00, 'C', 0x00, 'D'}, "A" + fffd + "BCD"},
		{{0xFE, 0xFF, 0x00, 0x1B, 0x00, 'e', 0x00, 0x1B, 0x00, 'B'}, fffd + "e" + fffd + "B"}, // one letter
		{{0xFE, 0xFF, 0x00, 0x1B, 0x00, '1', 0x00, '2', 0x00, 0x1B}, fffd + "12" + fffd}, // not letters
		{{0xEF, 0xBB, 0xBF, 'A', 0x1B, 'e', 'n', 'U', 'S', 0x1B, 'B'}, "AB"},
		{{0xEF, 0xBB, 0xBF, 'A', 0x1B, 'B'}, "A" + fffd + "B"}, // unterminated
		{{0xEF, 0xBB, 0xBF, 'A', 0x1B, 'a', 'b', 'c', 'd', 'e', 0x1B, 'Z'}, "A" + fffd + "abcde" + fffd + "Z"},
	};
	for (const auto& escape_case : escape_cases) {
		const auto decoded = pdf_string_to_utf8(escape_case.bytes);
		RECTO_ASSERT_OK(decoded);
		EXPECT_EQ(decoded.value(), escape_case.expected);
	}

	// 6. Latin-1 string starting with U+00FE U+00FF ("þÿ") avoids colliding
	// with the UTF-16BE BOM by upgrading to explicit UTF-16BE.
	const std::string bom_collision_str = "\xC3\xBE\xC3\xBF";
	const auto encoded_collision = utf8_to_pdf_string(bom_collision_str);
	RECTO_ASSERT_OK(encoded_collision);
	const auto decoded_collision = encoded_collision.value().to_utf8();
	RECTO_ASSERT_OK(decoded_collision);
	EXPECT_EQ(decoded_collision.value(), bom_collision_str);
}

TEST(PdfObject, DeepCloneIsolationAndDepthLimit)
{
	PdfDict root;
	root.set("Type", PdfValue(PdfName("Page")));
	root.set("MediaBox", PdfValue(PdfArray{PdfValue(0), PdfValue(0), PdfValue(612), PdfValue(792)}));
	// Include both "/A" (from /#2FA) and "A" (from /A) to verify deep_clone
	// preserves exact decoded PdfName bytes without collapsing entries.
	root.set(PdfName::from_decoded("/A"), PdfValue(PdfName::from_decoded("/SlashVal")));
	root.set(PdfName::from_decoded("A"), PdfValue(PdfName::from_decoded("PlainVal")));

	PdfDict resources;
	PdfDict font_dict;
	font_dict.set("F1", PdfValue(IndirectRef{15, 0}));
	resources.set("Font", PdfValue(std::move(font_dict)));
	root.set("Resources", PdfValue(std::move(resources)));

	PdfStream content_stream(
		std::move(root),
		std::vector<uint8_t>{'B', 'T', ' ', 'E', 'T'});
	content_stream.decoded_bytes = std::vector<uint8_t>{'B', 'T', ' ', 'E', 'T', '\n'};
	PdfValue original(std::move(content_stream));

	auto cloned_res = original.deep_clone();
	RECTO_ASSERT_OK(cloned_res);
	PdfValue cloned = std::move(cloned_res).value();
	EXPECT_EQ(original, cloned);
	EXPECT_EQ(cloned.as_stream()->dict.size(), 5u);
	ASSERT_NE(cloned.as_stream()->dict.get(PdfName::from_decoded("/A")), nullptr);
	EXPECT_EQ(
		cloned.as_stream()->dict.get(PdfName::from_decoded("/A"))->as_name(),
		std::optional<std::string_view>("/SlashVal"));
	ASSERT_NE(cloned.as_stream()->dict.get(PdfName::from_decoded("A")), nullptr);
	EXPECT_EQ(
		cloned.as_stream()->dict.get(PdfName::from_decoded("A"))->as_name(),
		std::optional<std::string_view>("PlainVal"));

	// Mutating the clone leaves the original completely unchanged (no shared pointers).
	cloned.as_stream_mut()->raw_bytes.push_back('!');
	cloned.as_stream_mut()->dict.get_array_mut("MediaBox")->at(2) = PdfValue(1024);
	cloned.as_stream_mut()
		->dict.get_dict_mut("Resources")
		->get_dict_mut("Font")
		->set("F1", PdfValue(IndirectRef{99, 0}));

	EXPECT_EQ(original.as_stream()->raw_bytes.size(), 5u);
	EXPECT_EQ(
		original.as_stream()->dict.get_array("MediaBox")->at(2).as_int(),
		std::optional<int64_t>(612));
	EXPECT_EQ(
		original.as_stream()
			->dict.get_dict("Resources")
			->get_dict("Font")
			->get_ref("F1"),
		std::optional<IndirectRef>(IndirectRef{15, 0}));

	// Depth-64 nested array succeeds; depth-65 nested array fails with
	// RecursionLimitExceeded even if caller passes max_depth > 64.
	PdfValue nested_64(int64_t{1});
	for (size_t d = 0; d < kMaxObjectCloneDepth; ++d) {
		PdfArray wrapper;
		wrapper.push_back(std::move(nested_64));
		nested_64 = PdfValue(std::move(wrapper));
	}
	RECTO_ASSERT_OK(nested_64.deep_clone());

	PdfArray wrapper_65;
	wrapper_65.push_back(std::move(nested_64));
	PdfValue nested_65(std::move(wrapper_65));
	clear_diagnostics();
	RECTO_EXPECT_ERR(nested_65.deep_clone(), ErrorCode::RecursionLimitExceeded);
	RECTO_EXPECT_ERR(nested_65.deep_clone(100), ErrorCode::RecursionLimitExceeded);
	EXPECT_EQ(diagnostics_tail(10).size(), 2u);
}

} // namespace
} // namespace recto::pdf
