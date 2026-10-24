// Unit tests for the canonical PdfValue serializer (D01 T02 section 35). Every value written is parsed back by the
// engine's own parser and compared by kind and content; stream framing and `/Length` are checked on the bytes
// themselves, because the parser recovers a wrong length by scanning for `endstream`.
#include "document.h"
#include "filters.h"
#include "parser.h"
#include "serialize.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {
namespace {

std::span<const uint8_t> bytes_of(const std::string& s)
{
	return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

std::string serialized(const PdfValue& v)
{
	std::string out;
	const auto done = serialize_value(v, out);
	EXPECT_TRUE(done.has_value()) << (done.has_value() ? "" : done.error().message.view());
	return out;
}

PdfValue parsed(const std::string& text)
{
	Parser parser(bytes_of(text));
	auto v = parser.parse_direct_object();
	EXPECT_TRUE(v.has_value()) << text.substr(0, 80) << ": " << (v.has_value() ? "" : v.error().message.view());
	return v.has_value() ? std::move(*v) : PdfValue{};
}

PdfDict dict_of(std::initializer_list<std::pair<std::string_view, PdfValue>> entries)
{
	PdfDict d;
	for (const auto& [k, v] : entries) {
		d.set(k, v);
	}
	return d;
}

PdfValue nested_arrays(size_t depth)
{
	PdfValue v{int64_t{7}};
	for (size_t i = 0; i < depth; ++i) {
		v = PdfValue{PdfArray{std::move(v)}};
	}
	return v;
}

TEST(PdfSerialize, ValuesRoundTrip)
{
	std::vector<uint8_t> every_byte(256);
	for (size_t i = 0; i < 256u; ++i) {
		every_byte[i] = static_cast<uint8_t>(i);
	}
	const std::vector<PdfValue> values = {
		PdfValue{},
		PdfValue{true},
		PdfValue{false},
		PdfValue{int64_t{0}},
		PdfValue{std::numeric_limits<int64_t>::min() + 1},
		PdfValue{std::numeric_limits<int64_t>::max()},
		PdfValue{0.5},
		PdfValue{-1.0 / 3.0},
		PdfValue{PdfName("Type")},
		PdfValue{PdfName::from_decoded("")},
		PdfValue{PdfName::from_decoded("A B#(/)<>[]{}%\x01\xE9")},
		PdfValue{PdfString(std::string_view("plain text (with parens) \\ and \r\n\t\b\f end"))},
		PdfValue{PdfString(std::string_view("unbalanced ((("))},
		PdfValue{PdfString(std::span<const uint8_t>(every_byte))},
		PdfValue{PdfString(std::string_view("short"), true)},
		PdfValue{PdfString(std::string_view(""))},
		PdfValue{IndirectRef{12u, 3u}},
		PdfValue{PdfArray{PdfValue{int64_t{1}}, PdfValue{PdfName("N")}, PdfValue{PdfString(std::string_view("s"))},
			PdfValue{PdfArray{}}, PdfValue{IndirectRef{4u, 0u}}}},
		PdfValue{dict_of({{"Type", PdfValue{PdfName("Page")}}, {"Kids", PdfValue{PdfArray{PdfValue{IndirectRef{3u, 0u}}}}},
			{"Count", PdfValue{int64_t{1}}}, {"Empty", PdfValue{PdfDict{}}}})},
		nested_arrays(kMaxSerializedDepth),
	};
	for (const PdfValue& v : values) {
		const std::string text = serialized(v);
		EXPECT_EQ(parsed(text), v) << text.substr(0, 120);
	}

	// Every object of every corpus file that opens: direct values back through the parser, streams through
	// `parse_indirect_object` with their encoded bytes and dictionaries.
	size_t objects = 0;
	size_t streams = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(recto::test::corpus_path())) {
		if (entry.path().extension() != ".pdf") {
			continue;
		}
		auto doc = Document::open(entry.path());
		if (!doc.has_value()) {
			continue; // the broken and password files
		}
		for (const auto& [num, xe] : (*doc)->xref().entries()) {
			if (xe.is_free()) {
				continue;
			}
			auto value = (*doc)->resolve_value(PdfValue{IndirectRef{num, xe.generation}});
			if (!value.has_value()) {
				continue;
			}
			std::string text;
			ASSERT_TRUE(serialize_indirect(IndirectRef{num, xe.generation}, *value, text).has_value())
				<< entry.path().filename().string() << " object " << num;
			Parser parser(bytes_of(text));
			auto back = parser.parse_indirect_object();
			ASSERT_TRUE(back.has_value()) << entry.path().filename().string() << " object " << num;
			EXPECT_EQ(back->ref, (IndirectRef{num, xe.generation}));
			if (const PdfStream* s = value->as_stream()) {
				const PdfStream* t = back->value.as_stream();
				ASSERT_NE(t, nullptr);
				EXPECT_EQ(t->raw_bytes, s->raw_bytes);
				for (const auto& [k, v] : s->dict) {
					if (!(k == "Length")) {
						const PdfValue* got = t->dict.get(k.view());
						ASSERT_NE(got, nullptr) << k.view();
						EXPECT_EQ(*got, v) << k.view();
					}
				}
				++streams;
			} else {
				EXPECT_EQ(back->value, *value) << entry.path().filename().string() << " object " << num;
			}
			++objects;
		}
	}
	std::printf("PdfSerialize: %zu corpus objects round-tripped, %zu of them streams\n", objects, streams);
	EXPECT_GE(objects, 130u); // 134 when written; fewer means the corpus walk lost files
	EXPECT_GE(streams, 20u);
}

TEST(PdfSerialize, RealsAndNames)
{
	struct Case {
		double value;
		std::string_view text;
	};
	const Case cases[] = {
		{0.1, "0.1"},
		{-2.5, "-2.5"},
		{3.0, "3.0"}, // a real stays a real
		{1e-7, "0.0000001"},
		{1e20, "100000000000000000000.0"},
		{-0.0, "-0.0"},
	};
	for (const Case& c : cases) {
		const std::string text = serialized(PdfValue{c.value});
		EXPECT_EQ(text, c.text);
		EXPECT_EQ(text.find_first_of("eE"), std::string::npos);
		const PdfValue back = parsed(text);
		ASSERT_TRUE(back.is_real()) << text;
		EXPECT_EQ(*back.as_real(), c.value);
	}
	// The shortest digits that read back exactly, for values with no short decimal form.
	for (const double v : {1.0 / 3.0, 612.0 * 0.0254, 1e-300, 1.7976931348623157e308}) {
		const std::string text = serialized(PdfValue{v});
		EXPECT_EQ(text.find_first_of("eE"), std::string::npos);
		EXPECT_EQ(*parsed(text).as_real(), v) << text.substr(0, 40);
	}
	for (const double bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
		std::string out = "kept";
		const auto refused = serialize_value(PdfValue{bad}, out);
		ASSERT_FALSE(refused.has_value());
		EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
		EXPECT_EQ(out, "kept");
	}
	EXPECT_EQ(serialized(PdfValue{PdfName::from_decoded("A B#(/)\xE9")}), "/A#20B#23#28#2F#29#E9");
	EXPECT_EQ(serialized(PdfValue{PdfName::from_decoded("Plain_Name-1.2")}), "/Plain_Name-1.2");
}

TEST(PdfSerialize, Failures)
{
	// Nesting: 64 levels are written, 65 refused, and the caller's buffer is as it was.
	std::string out = "prefix ";
	ASSERT_TRUE(serialize_value(nested_arrays(kMaxSerializedDepth), out).has_value());
	out = "prefix ";
	const PdfValue too_deep = nested_arrays(kMaxSerializedDepth + 1u);
	const auto deep = serialize_value(too_deep, out);
	ASSERT_FALSE(deep.has_value());
	EXPECT_EQ(deep.error().code, ErrorCode::RecursionLimitExceeded);
	EXPECT_EQ(out, "prefix ");

	// The budget: 1,000 parentheses escape to 2,002 bytes, past a 1,500-byte limit.
	const PdfValue parens{PdfString(std::string(1'000u, '('))};
	const auto over = serialize_value(parens, out, 1'500u);
	ASSERT_FALSE(over.has_value());
	EXPECT_EQ(over.error().code, ErrorCode::ResourceLimit);
	EXPECT_EQ(over.error().refusal_reason, RefusalReason::WorkingBytes);
	EXPECT_EQ(out, "prefix ");
	ASSERT_TRUE(serialize_value(parens, out, 2'002u).has_value());
	EXPECT_EQ(out.size(), std::string("prefix ").size() + 2'002u);

	// An allocation failure part-way: `OutOfMemory`, nothing appended, the value unchanged.
	PdfArray many;
	for (int64_t i = 0; i < 1'000; ++i) {
		many.push_back(PdfValue{i});
	}
	const PdfValue array{many};
	const PdfValue copy = array;
	out = "prefix ";
	{
		detail::ScopedSerializeAllocFailure fail(100u);
		const auto starved = serialize_value(array, out);
		ASSERT_FALSE(starved.has_value());
		EXPECT_EQ(starved.error().code, ErrorCode::OutOfMemory);
		std::string indirect = "prefix ";
		const auto starved_indirect = serialize_indirect(IndirectRef{1u, 0u}, array, indirect);
		ASSERT_FALSE(starved_indirect.has_value());
		EXPECT_EQ(indirect, "prefix ");
	}
	EXPECT_EQ(out, "prefix ");
	EXPECT_EQ(array, copy);

	// A stream is an indirect object, never a direct value.
	PdfStream stream;
	const auto direct = serialize_value(PdfValue{stream}, out);
	ASSERT_FALSE(direct.has_value());
	EXPECT_EQ(direct.error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(out, "prefix ");
}

TEST(PdfSerialize, IndirectAndStreams)
{
	std::string text;
	ASSERT_TRUE(serialize_indirect(IndirectRef{7u, 2u}, PdfValue{dict_of({{"A", PdfValue{int64_t{1}}}})}, text).has_value());
	EXPECT_EQ(text, "7 2 obj\n<</A 1>>\nendobj\n");

	// Framing on the bytes: `/Length` is the payload's size and the payload sits between `stream` LF and LF
	// `endstream`, for an empty payload, one holding "endstream", and one ending in CR LF.
	const std::vector<std::string> payloads = {std::string(), std::string("abc\nendstream\nendobj\x00\xFF", 23u),
		std::string("line one\r\nline two\r\n")};
	for (const std::string& payload : payloads) {
		PdfStream stream;
		stream.dict = dict_of({{"Type", PdfValue{PdfName("XObject")}}, {"Length", PdfValue{int64_t{999}}}});
		stream.raw_bytes.assign(payload.begin(), payload.end());
		std::string out;
		ASSERT_TRUE(serialize_indirect(IndirectRef{5u, 0u}, PdfValue{stream}, out).has_value());
		const std::string head = "5 0 obj\n<</Type /XObject /Length " + std::to_string(payload.size()) + ">>\nstream\n";
		ASSERT_EQ(out.substr(0, head.size()), head);
		EXPECT_EQ(out.substr(head.size(), payload.size()), payload);
		EXPECT_EQ(out.substr(head.size() + payload.size()), "\nendstream\nendobj\n");
		EXPECT_EQ(*stream.dict.get("Length"), PdfValue{int64_t{999}}); // the input is not changed
	}

	// A FlateDecode stream whose decoded cache is filled: the encoded bytes are written, with the filter, and read
	// back to the same content.
	const std::string content = "BT /F1 12 Tf 72 720 Td (Hello) Tj ET\n";
	auto compressed = flate_compress(bytes_of(content));
	ASSERT_TRUE(compressed.has_value());
	PdfStream stream;
	stream.dict = dict_of({{"Filter", PdfValue{PdfName("FlateDecode")}}, {"Length", PdfValue{int64_t{1}}}});
	stream.raw_bytes = *compressed;
	stream.decoded_bytes = std::vector<uint8_t>(content.begin(), content.end());
	std::string out;
	ASSERT_TRUE(serialize_indirect(IndirectRef{9u, 0u}, PdfValue{stream}, out).has_value());
	EXPECT_NE(out.find("/Filter /FlateDecode /Length " + std::to_string(compressed->size()) + ">>"), std::string::npos);
	Parser parser(bytes_of(out));
	auto back = parser.parse_indirect_object();
	ASSERT_TRUE(back.has_value());
	const PdfStream* s = back->value.as_stream();
	ASSERT_NE(s, nullptr);
	EXPECT_EQ(s->raw_bytes, *compressed);
	auto decoded = decode_stream(*s);
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(std::string(decoded->begin(), decoded->end()), content);
}

} // namespace
} // namespace recto::pdf
