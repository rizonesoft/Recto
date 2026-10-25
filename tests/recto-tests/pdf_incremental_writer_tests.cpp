// Incremental PDF revisions (D01 T02 section 5): the writer's refusals, its object rules, the free list, stream
// compression, file identifiers, the classic, stream and hybrid sections, encrypted bases, signature placeholders,
// atomic failure, and strict reparsing of everything it writes.

#include "crypto.h"
#include "document.h"
#include "filters.h"
#include "incremental_writer.h"
#include "parser.h"
#include "serialize.h"
#include "test_support.h"
#include "xref.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {
namespace {

using Bytes = std::vector<uint8_t>;

[[nodiscard]] Bytes corpus(std::string_view rel)
{
	return ::recto::test::read_file_bytes(::recto::test::corpus_path(rel));
}

[[nodiscard]] Bytes bytes_of(std::string_view text)
{
	return Bytes(text.begin(), text.end());
}

[[nodiscard]] std::string text_of(std::span<const uint8_t> bytes)
{
	return std::string(bytes.begin(), bytes.end());
}

[[nodiscard]] std::unique_ptr<Document> open_doc(std::span<const uint8_t> bytes, std::string_view password = {})
{
	auto doc = password.empty() ? Document::open_memory(bytes) : Document::open_memory(bytes, password);
	EXPECT_TRUE(doc.has_value()) << (doc.has_value() ? "" : std::string(doc.error().message.view()));
	return doc.has_value() ? std::move(*doc) : nullptr;
}

[[nodiscard]] IncrementalWriter make_writer(const Document& doc)
{
	auto writer = IncrementalWriter::create(doc);
	EXPECT_TRUE(writer.has_value()) << (writer.has_value() ? "" : std::string(writer.error().message.view()));
	if (!writer.has_value()) {
		std::abort();
	}
	return std::move(*writer);
}

[[nodiscard]] Bytes write_ok(const IncrementalWriter& writer)
{
	auto out = writer.write();
	EXPECT_TRUE(out.has_value()) << (out.has_value() ? "" : std::string(out.error().message.view()));
	return out.has_value() ? std::move(*out) : Bytes{};
}

[[nodiscard]] PdfDict dict_of(std::string_view literal)
{
	const Bytes bytes = bytes_of(literal);
	Parser parser(bytes);
	auto parsed = parser.parse_dictionary();
	EXPECT_TRUE(parsed.has_value()) << literal;
	return parsed.has_value() ? std::move(*parsed) : PdfDict{};
}

[[nodiscard]] PdfValue stream_of(std::string_view dict_literal, Bytes data)
{
	return PdfValue(PdfStream(dict_of(dict_literal), std::move(data)));
}

// Parses `bytes` with recovery off and checks every in-use, uncompressed row points at its own `N G obj`: a wrong
// offset, `/Prev` or `/Index` written by the writer fails here, where `Document::open_memory` would recover from it.
[[nodiscard]] bool strict_ok(std::span<const uint8_t> bytes, size_t revisions, XrefSectionKind kind,
	std::string* why = nullptr)
{
	auto table = XrefTable::parse(bytes, kMaxXrefTotalEntries, /*allow_recovery=*/false);
	if (!table.has_value()) {
		if (why != nullptr) {
			*why = "strict parse: " + std::string(table.error().message.view());
		}
		return false;
	}
	if (table->revision_count() != revisions || table->newest_section() != kind || table->recovered()) {
		if (why != nullptr) {
			*why = "revisions " + std::to_string(table->revision_count()) + ", kind " +
				std::to_string(static_cast<int>(table->newest_section()));
		}
		return false;
	}
	// /Size is one past the highest number (ISO 32000-1 Table 15), which the parser itself does not insist on.
	const auto size = table->newest_trailer().get_int("Size");
	for (const auto& [number, entry] : table->entries()) {
		if (!size.has_value() || static_cast<int64_t>(number) >= *size) {
			if (why != nullptr) {
				*why = "object " + std::to_string(number) + " is not below /Size";
			}
			return false;
		}
		if (!entry.is_uncompressed()) {
			continue;
		}
		auto object = table->read_indirect_object(bytes, IndirectRef(number, entry.generation));
		if (!object.has_value() || object->ref.obj_num != number || object->ref.gen_num != entry.generation) {
			if (why != nullptr) {
				*why = "object " + std::to_string(number) + " does not read at its offset";
			}
			return false;
		}
	}
	return true;
}

#define EXPECT_STRICT(bytes, revisions, kind)                                  \
	do {                                                                       \
		std::string why_;                                                      \
		EXPECT_TRUE(strict_ok((bytes), (revisions), (kind), &why_)) << why_;   \
	} while (false)

[[nodiscard]] std::string string_entry(const PdfDict& dict, std::string_view key)
{
	const PdfString* s = dict.get_string(key);
	return s != nullptr ? std::string(s->as_byte_view()) : std::string("<none>");
}

[[nodiscard]] PdfDict resolve_dict(const Document& doc, IndirectRef ref)
{
	auto value = doc.resolve_object(ref);
	EXPECT_TRUE(value.has_value()) << ref.obj_num;
	if (!value.has_value() || value->as_dict() == nullptr) {
		ADD_FAILURE() << "object " << ref.obj_num << " is not a dictionary";
		return PdfDict{};
	}
	return *value->as_dict();
}

// A classic file from `objects` (number to body), with `prefix` before the header and `trailer_extra` in the trailer.
[[nodiscard]] Bytes build_pdf(const std::map<uint32_t, std::string>& objects, std::string_view trailer_extra = {},
	std::string_view prefix = {}, uint64_t size = 0)
{
	std::string out(prefix);
	const size_t header = out.size();
	out += "%PDF-1.7\n";
	std::map<uint32_t, size_t> offsets;
	for (const auto& [number, body] : objects) {
		offsets[number] = out.size() - header;
		out += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
	}
	const uint32_t highest = objects.empty() ? 0u : objects.rbegin()->first;
	const size_t xref_at = out.size() - header;
	out += "xref\n0 " + std::to_string(highest + 1u) + "\n0000000000 65535 f\r\n";
	for (uint32_t n = 1; n <= highest; ++n) {
		char row[24];
		if (const auto it = offsets.find(n); it != offsets.end()) {
			std::snprintf(row, sizeof(row), "%010zu 00000 n\r\n", it->second);
		} else {
			std::snprintf(row, sizeof(row), "0000000000 00001 f\r\n");
		}
		out += row;
	}
	out += "trailer\n<< /Size " + std::to_string(size != 0 ? size : highest + 1u) + " /Root 1 0 R ";
	out += trailer_extra;
	out += ">>\nstartxref\n" + std::to_string(xref_at) + "\n%%EOF\n";
	return bytes_of(out);
}

const std::map<uint32_t, std::string> kSmall = {
	{1u, "<< /Type /Catalog /Pages 2 0 R >>"},
	{2u, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"},
	{3u, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>"},
};

// A hybrid file (ISO 32000-1 section 7.5.8.4): a classic table for objects 1 to 3 whose trailer's /XRefStm names a
// cross-reference stream listing object 5, stored in object stream 4.
[[nodiscard]] Bytes build_hybrid()
{
	std::string out = "%PDF-1.7\n";
	std::map<uint32_t, size_t> at;
	for (const auto& [number, body] : kSmall) {
		at[number] = out.size();
		out += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
	}
	const std::string member = "<< /Marker 42 >>";
	const std::string header = "5 0 ";
	at[4] = out.size();
	out += "4 0 obj\n<< /Type /ObjStm /N 1 /First " + std::to_string(header.size()) + " /Length " +
		std::to_string(header.size() + member.size()) + " >>\nstream\n" + header + member + "\nendstream\nendobj\n";
	at[6] = out.size();
	std::string rows;
	const auto row = [&rows](uint8_t type, uint32_t field, uint8_t gen) {
		rows.push_back(static_cast<char>(type));
		rows.push_back(static_cast<char>((field >> 8u) & 0xFFu));
		rows.push_back(static_cast<char>(field & 0xFFu));
		rows.push_back(static_cast<char>(gen));
	};
	row(1, static_cast<uint32_t>(at[4]), 0);
	row(2, 4u, 0);
	row(1, static_cast<uint32_t>(at[6]), 0);
	out += "6 0 obj\n<< /Type /XRef /Size 7 /Index [4 3] /W [1 2 1] /Length " + std::to_string(rows.size()) +
		" >>\nstream\n" + rows + "\nendstream\nendobj\n";
	const size_t xref_at = out.size();
	out += "xref\n0 4\n0000000000 65535 f\r\n";
	for (uint32_t n = 1; n <= 3; ++n) {
		char line[24];
		std::snprintf(line, sizeof(line), "%010zu 00000 n\r\n", at[n]);
		out += line;
	}
	out += "trailer\n<< /Size 7 /Root 1 0 R /XRefStm " + std::to_string(at[6]) + " >>\nstartxref\n" +
		std::to_string(xref_at) + "\n%%EOF\n";
	return bytes_of(out);
}

[[nodiscard]] std::vector<std::string> json_strings(const std::string& json, std::string_view key)
{
	std::vector<std::string> found;
	const std::string needle = "\"" + std::string(key) + "\": \"";
	for (size_t at = json.find(needle); at != std::string::npos; at = json.find(needle, at + 1u)) {
		const size_t start = at + needle.size();
		found.push_back(json.substr(start, json.find('"', start) - start));
	}
	return found;
}

[[nodiscard]] std::pair<Bytes, Bytes> trailer_id(std::span<const uint8_t> file)
{
	auto table = XrefTable::parse(file, kMaxXrefTotalEntries, false);
	EXPECT_TRUE(table.has_value());
	if (!table.has_value()) {
		return {};
	}
	const PdfValue* id = table->newest_trailer().get("ID");
	if (id == nullptr || id->as_array() == nullptr || id->as_array()->size() != 2u) {
		return {};
	}
	return {(*id->as_array())[0].as_string()->bytes, (*id->as_array())[1].as_string()->bytes};
}

TEST(PdfXref, NewestSection)
{
	{
		const Bytes file = corpus("render/simple-1p.pdf");
		auto table = XrefTable::parse(file, kMaxXrefTotalEntries, false);
		RECTO_ASSERT_OK(table);
		EXPECT_EQ(table->newest_section(), XrefSectionKind::Classic);
		EXPECT_EQ(table->startxref_offset(), 786u);
		EXPECT_TRUE(table->newest_trailer().contains("Root"));
		EXPECT_FALSE(table->newest_trailer().contains("Prev"));
		EXPECT_EQ(table->total_rows(), 7u);
	}
	{
		const Bytes file = corpus("render/compressed-xref-2p.pdf");
		auto table = XrefTable::parse(file, kMaxXrefTotalEntries, false);
		RECTO_ASSERT_OK(table);
		EXPECT_EQ(table->newest_section(), XrefSectionKind::Stream);
		EXPECT_EQ(table->newest_trailer().get_name("Type"), std::optional<std::string_view>("XRef"));
		EXPECT_GT(table->total_rows(), 0u);
	}
	{
		const Bytes file = build_hybrid();
		auto table = XrefTable::parse(file, kMaxXrefTotalEntries, false);
		RECTO_ASSERT_OK(table);
		EXPECT_EQ(table->newest_section(), XrefSectionKind::Hybrid);
		EXPECT_EQ(table->total_rows(), 7u); // 4 classic rows and 3 stream rows
	}
	{
		// A startxref that points nowhere: the table is rebuilt, and says so.
		std::string text = text_of(corpus("render/simple-1p.pdf"));
		const size_t at = text.rfind("startxref\n786");
		ASSERT_NE(at, std::string::npos);
		text.replace(at + 10u, 3u, "999");
		auto doc = Document::open_memory(bytes_of(text));
		RECTO_ASSERT_OK(doc);
		EXPECT_TRUE((*doc)->is_recovered());
		EXPECT_EQ((*doc)->xref().newest_section(), XrefSectionKind::None);
	}
}

TEST(PdfIncrementalWriter, RefusesUnsafeBases)
{
	{
		std::string text = text_of(corpus("render/simple-1p.pdf"));
		text.replace(text.rfind("startxref\n786") + 10u, 3u, "999");
		auto doc = open_doc(bytes_of(text));
		ASSERT_NE(doc, nullptr);
		RECTO_EXPECT_ERR(IncrementalWriter::create(*doc), ErrorCode::DamagedFormat);
	}
	{
		const Bytes file = corpus("render/encrypted-aes-256.pdf");
		auto doc = Document::open_memory_unauthenticated(file);
		RECTO_ASSERT_OK(doc);
		RECTO_EXPECT_ERR(IncrementalWriter::create(**doc), ErrorCode::NeedsPassword);
	}
	{
		// A file already at the revisions a reader follows: 255 sections appended by hand after the base's own.
		std::string text = text_of(corpus("render/simple-1p.pdf"));
		uint64_t prev = 786u;
		for (size_t i = 1; i < kMaxXrefRevisions; ++i) {
			const uint64_t at = text.size();
			text += "xref\n0 1\n0000000000 65535 f\r\ntrailer\n<< /Size 7 /Root 1 0 R /Prev " + std::to_string(prev) +
				" >>\nstartxref\n" + std::to_string(at) + "\n%%EOF\n";
			prev = at;
		}
		auto doc = open_doc(bytes_of(text));
		ASSERT_NE(doc, nullptr);
		EXPECT_FALSE(doc->is_recovered());
		EXPECT_EQ(doc->xref().revision_count(), kMaxXrefRevisions);
		RECTO_EXPECT_ERR(IncrementalWriter::create(*doc), ErrorCode::ResourceLimit);
		// One revision fewer: the last revision a reader follows is written, and reads back strictly.
		text.resize(static_cast<size_t>(prev));
		auto below = open_doc(bytes_of(text));
		ASSERT_NE(below, nullptr);
		ASSERT_EQ(below->xref().revision_count(), kMaxXrefRevisions - 1u);
		IncrementalWriter writer = make_writer(*below);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (Last) >>"))));
		EXPECT_STRICT(write_ok(writer), kMaxXrefRevisions, XrefSectionKind::Classic);
	}
	{
		// Rows: a stream base listing all but two of the rows a reader reads accepts a writer, then refuses the
		// object that would pass the limit; one listing one more is refused at once.
		for (const uint64_t listed : {kMaxXrefTotalEntries - 2u, kMaxXrefTotalEntries - 1u}) {
			std::string out = "%PDF-1.7\n";
			std::map<uint32_t, size_t> at;
			for (const auto& [number, body] : kSmall) {
				at[number] = out.size();
				out += std::to_string(number) + " 0 obj\n" + body + "\nendobj\n";
			}
			const size_t xref_at = out.size();
			Bytes rows(static_cast<size_t>(listed) * 4u, 0u); // W [1 2 1]: type 0 rows, free
			for (uint32_t n = 1; n <= 3; ++n) {
				rows[n * 4u] = 1u;
				rows[n * 4u + 1u] = static_cast<uint8_t>(at[n] >> 8u);
				rows[n * 4u + 2u] = static_cast<uint8_t>(at[n]);
			}
			const size_t self = 4u;
			rows[self * 4u] = 1u;
			rows[self * 4u + 1u] = static_cast<uint8_t>(xref_at >> 8u);
			rows[self * 4u + 2u] = static_cast<uint8_t>(xref_at);
			auto packed = flate_compress(rows);
			RECTO_ASSERT_OK(packed);
			out += "4 0 obj\n<< /Type /XRef /Size " + std::to_string(listed) + " /Root 1 0 R /W [1 2 1] /Filter "
				"/FlateDecode /Length " + std::to_string(packed->size()) + " >>\nstream\n" + text_of(*packed) +
				"\nendstream\nendobj\nstartxref\n" + std::to_string(xref_at) + "\n%%EOF\n";
			const Bytes file = bytes_of(out);
			auto doc = open_doc(file);
			ASSERT_NE(doc, nullptr);
			ASSERT_FALSE(doc->is_recovered());
			if (listed == kMaxXrefTotalEntries - 1u) {
				RECTO_EXPECT_ERR(IncrementalWriter::create(*doc), ErrorCode::ResourceLimit);
				continue;
			}
			IncrementalWriter writer = make_writer(*doc);
			RECTO_EXPECT_ERR(writer.set_object(IndirectRef(3u), PdfValue(dict_of("<< /Type /Page >>"))),
				ErrorCode::ResourceLimit);
			RECTO_ASSERT_OK(writer.revision()); // the refused object is not staged
		}
	}
	{
		// Object numbers: the last one a reader accepts is allocated, the next is refused.
		const Bytes file = build_pdf(kSmall, {}, {}, kMaxPdfObjectNumber);
		auto doc = open_doc(file);
		ASSERT_NE(doc, nullptr);
		IncrementalWriter writer = make_writer(*doc);
		auto last = writer.allocate_object(PdfValue(int64_t{1}));
		RECTO_ASSERT_OK(last);
		EXPECT_EQ(last->obj_num, kMaxPdfObjectNumber);
		RECTO_EXPECT_ERR(writer.allocate_object(PdfValue(int64_t{2})), ErrorCode::ResourceLimit);
		RECTO_EXPECT_ERR(writer.set_object(IndirectRef(kMaxPdfObjectNumber + 1u), PdfValue(int64_t{3})),
			ErrorCode::ResourceLimit);
	}
}

TEST(PdfIncrementalWriter, StagingLimit)
{
	// Every staged value counts against the revision's limit (strings, containers and a signature's reservation,
	// not only stream payloads); a refusal keeps what was staged, and freeing room lets the next value in.
	const Bytes base = corpus("render/simple-1p.pdf");
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	detail::ScopedRevisionByteLimit limit(10'000u);
	IncrementalWriter writer = make_writer(*doc);
	const auto big = [](char c) {
		return PdfValue(PdfArray{PdfValue(PdfString(std::string(3'000, c)))});
	};
	auto a = writer.allocate_object(big('a'));
	auto b = writer.allocate_object(big('b'));
	auto c = writer.allocate_object(big('c'));
	RECTO_ASSERT_OK(a);
	RECTO_ASSERT_OK(b);
	RECTO_ASSERT_OK(c);
	RECTO_EXPECT_ERR(writer.allocate_object(big('d')), ErrorCode::ResourceLimit);
	RECTO_EXPECT_ERR(writer.set_object(IndirectRef(6u), big('e')), ErrorCode::ResourceLimit);
	RECTO_EXPECT_ERR(writer.add_signature(dict_of("<< /Type /Sig >>"), 1'000u), ErrorCode::ResourceLimit);
	// Replacing a staged value with a smaller one, or dropping one, frees its share.
	RECTO_ASSERT_OK(writer.set_object(*c, PdfValue(int64_t{1})));
	RECTO_ASSERT_OK(writer.free_object(*b));
	auto d = writer.allocate_object(big('d'));
	RECTO_ASSERT_OK(d);
	EXPECT_EQ(d->obj_num, b->obj_num); // the lowest free number again
	RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), big('e')));
	const Bytes out = write_ok(writer);
	EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
	auto reopened = open_doc(out);
	ASSERT_NE(reopened, nullptr);
	auto first = reopened->resolve_object(*a);
	RECTO_ASSERT_OK(first);
	ASSERT_NE(first->as_array(), nullptr);
	EXPECT_EQ((*first->as_array())[0].as_string()->size(), 3'000u);
	auto third = reopened->resolve_object(*c);
	RECTO_ASSERT_OK(third);
	EXPECT_EQ(third->as_int(), 1);
}

TEST(PdfIncrementalWriter, AllocationScales)
{
	// Allocation probes stay linear in the objects allocated: numbers already handed out are not rescanned.
	const Bytes base = corpus("render/simple-1p.pdf");
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	for (const size_t n : {10'000u, 20'000u}) {
		IncrementalWriter writer = make_writer(*doc);
		const uint64_t before = detail::allocation_probes();
		for (size_t i = 0; i < n; ++i) {
			auto ref = writer.allocate_object(PdfValue(static_cast<int64_t>(i)));
			ASSERT_TRUE(ref.has_value());
			ASSERT_EQ(ref->obj_num, 7u + i);
		}
		EXPECT_LE(detail::allocation_probes() - before, 2u * n) << n;
	}
	// Numbers staged ahead of the mark are skipped, and a dropped one is handed out first.
	IncrementalWriter writer = make_writer(*doc);
	RECTO_ASSERT_OK(writer.set_object(IndirectRef(8u), PdfValue(int64_t{8})));
	RECTO_ASSERT_OK(writer.set_object(IndirectRef(9u), PdfValue(int64_t{9})));
	auto first = writer.allocate_object(PdfValue(int64_t{7}));
	auto second = writer.allocate_object(PdfValue(int64_t{10}));
	RECTO_ASSERT_OK(first);
	RECTO_ASSERT_OK(second);
	EXPECT_EQ(first->obj_num, 7u);
	EXPECT_EQ(second->obj_num, 10u);
	RECTO_ASSERT_OK(writer.free_object(IndirectRef(8u)));
	auto again = writer.allocate_object(PdfValue(int64_t{8}));
	RECTO_ASSERT_OK(again);
	EXPECT_EQ(again->obj_num, 8u);
	auto next = writer.allocate_object(PdfValue(int64_t{11}));
	RECTO_ASSERT_OK(next);
	EXPECT_EQ(next->obj_num, 11u);
}

TEST(PdfIncrementalWriter, BytePrefixPreserved)
{
	for (const bool final_eol : {true, false}) {
		Bytes base = corpus("render/simple-1p.pdf");
		if (!final_eol) {
			ASSERT_EQ(base.back(), '\n');
			base.pop_back();
		}
		auto doc = open_doc(base);
		ASSERT_NE(doc, nullptr);
		IncrementalWriter writer = make_writer(*doc);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (Changed) >>"))));
		const Bytes out = write_ok(writer);
		ASSERT_GT(out.size(), base.size());
		EXPECT_EQ(std::memcmp(out.data(), base.data(), base.size()), 0);
		const std::string tail = text_of(std::span<const uint8_t>(out).subspan(base.size(), 10u));
		EXPECT_EQ(tail.substr(0, final_eol ? 7u : 8u), final_eol ? "6 0 obj" : "\n6 0 obj") << final_eol;
		EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
		auto reopened = open_doc(out);
		ASSERT_NE(reopened, nullptr);
		EXPECT_EQ(string_entry(resolve_dict(*reopened, IndirectRef(6u)), "Title"), "Changed");
		EXPECT_EQ(reopened->page_count(), 1u); // untouched objects still resolve
	}
}

TEST(PdfIncrementalWriter, ObjectRules)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	{
		IncrementalWriter writer = make_writer(*doc);
		RECTO_EXPECT_ERR(writer.set_object(IndirectRef(0u), PdfValue{}), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(writer.set_object(IndirectRef(6u, 1u), PdfValue{}), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(writer.set_object(IndirectRef(7u, 2u), PdfValue{}), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(writer.free_object(IndirectRef(1u)), ErrorCode::InvalidArgument); // the catalog
		RECTO_ASSERT_OK(writer.free_object(IndirectRef(5u)));
		RECTO_EXPECT_ERR(writer.set_object(IndirectRef(5u), PdfValue{}), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(writer.free_object(IndirectRef(5u)), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(writer.free_object(IndirectRef(40u)), ErrorCode::InvalidArgument); // nothing staged there
		const Bytes out = write_ok(writer);
		// A number freed by an earlier revision is free below /Size: it cannot be written, only allocated anew.
		auto reopened = open_doc(out);
		ASSERT_NE(reopened, nullptr);
		IncrementalWriter next = make_writer(*reopened);
		RECTO_EXPECT_ERR(next.set_object(IndirectRef(5u, 1u), PdfValue{}), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(next.set_object(IndirectRef(5u, 0u), PdfValue{}), ErrorCode::InvalidArgument);
	}
	{
		// Explicit numbers at or past /Size interleaved with allocations: every number distinct and reachable.
		IncrementalWriter writer = make_writer(*doc);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(9u), PdfValue(dict_of("<< /N 9 >>"))));
		auto a = writer.allocate_object(PdfValue(dict_of("<< /N 7 >>")));
		auto b = writer.allocate_object(PdfValue(dict_of("<< /N 8 >>")));
		auto c = writer.allocate_object(PdfValue(dict_of("<< /N 10 >>")));
		RECTO_ASSERT_OK(a);
		RECTO_ASSERT_OK(b);
		RECTO_ASSERT_OK(c);
		EXPECT_EQ(a->obj_num, 7u);
		EXPECT_EQ(b->obj_num, 8u);
		EXPECT_EQ(c->obj_num, 10u);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(1u), PdfValue(dict_of(
			"<< /Type /Catalog /Pages 2 0 R /Refs [7 0 R 8 0 R 9 0 R 10 0 R] >>"))));
		const Bytes out = write_ok(writer);
		EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
		auto reopened = open_doc(out);
		ASSERT_NE(reopened, nullptr);
		const PdfDict catalog = resolve_dict(*reopened, IndirectRef(1u));
		const PdfValue* refs = catalog.get("Refs");
		ASSERT_NE(refs, nullptr);
		ASSERT_NE(refs->as_array(), nullptr);
		for (const PdfValue& ref : *refs->as_array()) {
			ASSERT_TRUE(ref.as_ref().has_value());
			EXPECT_EQ(resolve_dict(*reopened, *ref.as_ref()).get_int("N"), static_cast<int64_t>(ref.as_ref()->obj_num));
		}
		auto table = XrefTable::parse(out, kMaxXrefTotalEntries, false);
		RECTO_ASSERT_OK(table);
		EXPECT_EQ(table->newest_trailer().get_int("Size"), 11);
	}
	{
		// An object stored in an object stream is replaced at generation 0 and written uncompressed.
		const Bytes compressed = corpus("render/compressed-xref-2p.pdf");
		auto cdoc = open_doc(compressed);
		ASSERT_NE(cdoc, nullptr);
		std::optional<uint32_t> member;
		for (const auto& [number, entry] : cdoc->xref().entries()) {
			if (entry.is_compressed() && (!member.has_value() || number < *member)) {
				member = number;
			}
		}
		ASSERT_TRUE(member.has_value());
		IncrementalWriter writer = make_writer(*cdoc);
		RECTO_EXPECT_ERR(writer.set_object(IndirectRef(*member, 1u), PdfValue{}), ErrorCode::InvalidArgument);
		auto original = cdoc->resolve_object(IndirectRef(*member));
		RECTO_ASSERT_OK(original);
		ASSERT_NE(original->as_dict(), nullptr);
		PdfDict changed = *original->as_dict();
		changed.set("Recto", PdfValue(true));
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(*member), PdfValue(changed)));
		const Bytes out = write_ok(writer);
		EXPECT_STRICT(out, 2u, XrefSectionKind::Stream);
		auto reopened = open_doc(out);
		ASSERT_NE(reopened, nullptr);
		EXPECT_TRUE(reopened->xref().find(*member)->is_uncompressed());
		EXPECT_NE(resolve_dict(*reopened, IndirectRef(*member)).get("Recto"), nullptr);
		EXPECT_EQ(reopened->page_count(), 2u);
	}
	{
		// The /Encrypt dictionary is never written or freed.
		const Bytes file = corpus("render/encrypted-aes-256.pdf");
		auto edoc = open_doc(file, "recto-test");
		ASSERT_NE(edoc, nullptr);
		IncrementalWriter writer = make_writer(*edoc);
		RECTO_EXPECT_ERR(writer.set_object(IndirectRef(7u), PdfValue{}), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(writer.free_object(IndirectRef(7u)), ErrorCode::InvalidArgument);
	}
}

// Free rows of `file`'s newest parse: number to (next free number, generation).
[[nodiscard]] std::map<uint32_t, std::pair<uint64_t, uint32_t>> free_rows(std::span<const uint8_t> file)
{
	std::map<uint32_t, std::pair<uint64_t, uint32_t>> rows;
	auto table = XrefTable::parse(file, kMaxXrefTotalEntries, false);
	EXPECT_TRUE(table.has_value());
	if (table.has_value()) {
		for (const auto& [number, entry] : table->entries()) {
			if (entry.is_free()) {
				rows[number] = {entry.byte_offset, entry.generation};
			}
		}
	}
	return rows;
}

TEST(PdfIncrementalWriter, FreeList)
{
	for (const bool stream : {false, true}) {
		Bytes file = corpus(stream ? "render/compressed-xref-2p.pdf" : "render/simple-1p.pdf");
		const XrefSectionKind kind = stream ? XrefSectionKind::Stream : XrefSectionKind::Classic;
		std::vector<uint32_t> extra;
		{
			auto doc = open_doc(file);
			ASSERT_NE(doc, nullptr);
			IncrementalWriter writer = make_writer(*doc);
			for (int i = 0; i < 3; ++i) {
				auto ref = writer.allocate_object(PdfValue(dict_of("<< /Spare true >>")));
				RECTO_ASSERT_OK(ref);
				extra.push_back(ref->obj_num);
			}
			file = write_ok(writer);
		}
		{
			auto doc = open_doc(file);
			ASSERT_NE(doc, nullptr);
			IncrementalWriter writer = make_writer(*doc);
			RECTO_ASSERT_OK(writer.free_object(IndirectRef(extra[0])));
			file = write_ok(writer);
		}
		auto rows = free_rows(file);
		EXPECT_EQ(rows[0].first, extra[0]); // the base now has a chain: 0 -> extra[0] -> 0
		EXPECT_EQ(rows[extra[0]], (std::pair<uint64_t, uint32_t>{0u, 1u}));
		// Free three more, one below the others: they join ahead of the old chain in ascending order.
		auto doc = open_doc(file);
		ASSERT_NE(doc, nullptr);
		// A base object nothing on a page needs: the font (simple) or the information dictionary (stream).
		const uint32_t low = stream ? 8u : 5u;
		IncrementalWriter writer = make_writer(*doc);
		RECTO_ASSERT_OK(writer.free_object(IndirectRef(extra[2])));
		RECTO_ASSERT_OK(writer.free_object(IndirectRef(low)));
		RECTO_ASSERT_OK(writer.free_object(IndirectRef(extra[1])));
		file = write_ok(writer);
		EXPECT_STRICT(file, 4u, kind);
		rows = free_rows(file);
		EXPECT_EQ(rows[0], (std::pair<uint64_t, uint32_t>{low, 65'535u})) << stream;
		EXPECT_EQ(rows[low], (std::pair<uint64_t, uint32_t>{extra[1], 1u})) << stream;
		EXPECT_EQ(rows[extra[1]], (std::pair<uint64_t, uint32_t>{extra[2], 1u})) << stream;
		EXPECT_EQ(rows[extra[2]], (std::pair<uint64_t, uint32_t>{extra[0], 1u})) << stream;
		EXPECT_EQ(rows[extra[0]], (std::pair<uint64_t, uint32_t>{0u, 1u})) << stream;
		auto reopened = open_doc(file);
		ASSERT_NE(reopened, nullptr);
		for (const uint32_t n : {low, extra[0], extra[1], extra[2]}) {
			auto value = reopened->resolve_object(IndirectRef(n, 1u));
			EXPECT_TRUE(!value.has_value() || value->is_null()) << n;
		}
	}
}

TEST(PdfIncrementalWriter, StreamCompression)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	std::string text;
	for (int i = 0; i < 200; ++i) {
		text += "BT /F1 12 Tf 72 " + std::to_string(700 - i) + " Td (Line) Tj ET\n";
	}
	Bytes noise(4'096);
	uint32_t seed = 12'345u;
	for (uint8_t& b : noise) {
		seed = seed * 1'103'515'245u + 12'345u;
		b = static_cast<uint8_t>(seed >> 24u);
	}
	const std::string xmp = "<?xpacket begin=\"\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\">" + std::string(400, ' ') +
		"</x:xmpmeta><?xpacket end=\"w\"?>";
	IncrementalWriter writer = make_writer(*doc);
	auto content = writer.allocate_object(stream_of("<< >>", bytes_of(text)));
	auto raw = writer.allocate_object(stream_of("<< >>", noise));
	auto meta = writer.allocate_object(stream_of("<< /Type /Metadata /Subtype /XML >>", bytes_of(xmp)));
	RECTO_ASSERT_OK(content);
	RECTO_ASSERT_OK(raw);
	RECTO_ASSERT_OK(meta);
	const Bytes out = write_ok(writer);
	EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
	EXPECT_LT(out.size() - base.size(), text.size()); // the content stream was compressed
	auto reopened = open_doc(out);
	ASSERT_NE(reopened, nullptr);
	const auto check = [&](IndirectRef ref, bool filtered, const Bytes& expected) {
		auto value = reopened->resolve_object(ref);
		RECTO_ASSERT_OK(value);
		const PdfStream* s = value->as_stream();
		ASSERT_NE(s, nullptr);
		EXPECT_EQ(s->dict.get_name("Filter").has_value(), filtered) << ref.obj_num;
		auto decoded = reopened->decode_stream(*s);
		RECTO_ASSERT_OK(decoded);
		EXPECT_EQ(*decoded, expected) << ref.obj_num;
	};
	check(*content, true, bytes_of(text));
	check(*raw, false, noise);
	check(*meta, false, bytes_of(xmp));
}

TEST(PdfIncrementalWriter, TrailerCarried)
{
	std::string text = text_of(corpus("render/simple-1p.pdf"));
	const size_t at = text.rfind("/Root 1 0 R");
	ASSERT_NE(at, std::string::npos);
	text.insert(at, "/Custom (kept) "); // in the trailer, after the table: no offset moves
	const Bytes base = bytes_of(text);
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	IncrementalWriter writer = make_writer(*doc);
	RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (New) >>"))));
	const Bytes out = write_ok(writer);
	EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
	auto table = XrefTable::parse(out, kMaxXrefTotalEntries, false);
	RECTO_ASSERT_OK(table);
	const PdfDict& trailer = table->newest_trailer();
	EXPECT_EQ(string_entry(trailer, "Custom"), "kept");
	EXPECT_EQ(trailer.get_int("Prev"), 786);
	EXPECT_EQ(trailer.get_int("Size"), 7);
	EXPECT_EQ(trailer.get_ref("Root"), std::optional<IndirectRef>(IndirectRef(1u)));
	EXPECT_EQ(trailer.get_ref("Info"), std::optional<IndirectRef>(IndirectRef(6u)));
	// Only the written object is in the new section: object 0 and object 6.
	auto reopened = open_doc(out);
	ASSERT_NE(reopened, nullptr);
	EXPECT_EQ(string_entry(resolve_dict(*reopened, IndirectRef(6u)), "Title"), "New");
	EXPECT_EQ(reopened->page_count(), 1u);
	const std::string tail = text_of(std::span<const uint8_t>(out).subspan(base.size()));
	EXPECT_NE(tail.find("xref\n0 1\n"), std::string::npos);
	EXPECT_NE(tail.find("6 1\n"), std::string::npos);
	EXPECT_EQ(tail.find("1 0 obj"), std::string::npos);
}

TEST(PdfIncrementalWriter, FileIdentifier)
{
	const Bytes base = corpus("render/simple-1p.pdf");
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	const auto [base0, base1] = trailer_id(base);
	ASSERT_EQ(base0.size(), 16u);
	const auto edit = [&](std::string_view title) {
		IncrementalWriter writer = make_writer(*doc);
		EXPECT_TRUE(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (" + std::string(title) + ") >>")))
			.has_value());
		return trailer_id(write_ok(writer));
	};
	const auto a1 = edit("A");
	const auto a2 = edit("A");
	const auto b = edit("B");
	EXPECT_EQ(a1.first, base0);
	EXPECT_NE(a1.second, base1);
	EXPECT_EQ(a1.second.size(), 16u);
	EXPECT_EQ(a1, a2);
	EXPECT_NE(a1.second, b.second);
	{
		// AES draws fresh IVs, so the same edit gives different bytes and the same identifier.
		const Bytes file = corpus("render/encrypted-aes-256.pdf");
		auto edoc = open_doc(file, "recto-test");
		ASSERT_NE(edoc, nullptr);
		const auto [e0, e1] = trailer_id(file);
		std::vector<Bytes> outs;
		for (int i = 0; i < 2; ++i) {
			IncrementalWriter writer = make_writer(*edoc);
			RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (Same) >>"))));
			outs.push_back(write_ok(writer));
		}
		EXPECT_NE(outs[0], outs[1]);
		EXPECT_EQ(trailer_id(outs[0]), trailer_id(outs[1]));
		EXPECT_EQ(trailer_id(outs[0]).first, e0);
		EXPECT_NE(trailer_id(outs[0]).second, e1);
	}
	{
		// A base without /ID gains a pair.
		std::string text = text_of(base);
		const size_t at = text.find("/ID [");
		ASSERT_NE(at, std::string::npos);
		const size_t end = text.find(']', at);
		text.replace(at, end - at + 1u, std::string(end - at + 1u, ' '));
		const Bytes plain = bytes_of(text);
		auto pdoc = open_doc(plain);
		ASSERT_NE(pdoc, nullptr);
		IncrementalWriter writer = make_writer(*pdoc);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (X) >>"))));
		const auto [n0, n1] = trailer_id(write_ok(writer));
		EXPECT_EQ(n0.size(), 16u);
		EXPECT_EQ(n1.size(), 16u);
	}
	{
		// An R=3 file keyed without /ID gets none: adding one would change the key its objects were encrypted with.
		const Bytes vectors = corpus("crypto/encrypt-vectors.json");
		const auto dicts = json_strings(text_of(vectors), "rc4_no_id_dict");
		ASSERT_EQ(dicts.size(), 1u);
		std::map<uint32_t, std::string> objects = kSmall;
		objects[4] = dicts[0];
		const Bytes file = build_pdf(objects, "/Encrypt 4 0 R ");
		auto edoc = open_doc(file, "recto-test");
		ASSERT_NE(edoc, nullptr);
		ASSERT_TRUE(edoc->is_encrypted());
		IncrementalWriter writer = make_writer(*edoc);
		auto info = writer.allocate_object(PdfValue(dict_of("<< /Title (Secret title) >>")));
		RECTO_ASSERT_OK(info);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(1u), PdfValue(dict_of(
			"<< /Type /Catalog /Pages 2 0 R /Note " + std::to_string(info->obj_num) + " 0 R >>"))));
		const Bytes out = write_ok(writer);
		EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
		auto table = XrefTable::parse(out, kMaxXrefTotalEntries, false);
		RECTO_ASSERT_OK(table);
		EXPECT_FALSE(table->newest_trailer().contains("ID"));
		EXPECT_EQ(text_of(out).find("Secret title"), std::string::npos); // encrypted
		auto reopened = open_doc(out, "recto-test");
		ASSERT_NE(reopened, nullptr);
		EXPECT_EQ(string_entry(resolve_dict(*reopened, *info), "Title"), "Secret title");
	}
}

TEST(PdfIncrementalWriter, CompressedXrefFile)
{
	const Bytes base = corpus("render/compressed-xref-2p.pdf");
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	IncrementalWriter writer = make_writer(*doc);
	auto info = writer.allocate_object(PdfValue(dict_of("<< /Title (Streamed) >>")));
	RECTO_ASSERT_OK(info);
	const Bytes out = write_ok(writer);
	EXPECT_STRICT(out, 2u, XrefSectionKind::Stream);
	const std::string tail = text_of(std::span<const uint8_t>(out).subspan(base.size()));
	EXPECT_EQ(tail.find("\nxref\n"), std::string::npos);
	EXPECT_NE(tail.find("/Type /XRef"), std::string::npos);
	auto reopened = open_doc(out);
	ASSERT_NE(reopened, nullptr);
	EXPECT_FALSE(reopened->is_recovered());
	EXPECT_EQ(reopened->page_count(), 2u);
	EXPECT_EQ(string_entry(resolve_dict(*reopened, *info), "Title"), "Streamed");
}

TEST(PdfIncrementalWriter, HybridFile)
{
	const Bytes base = build_hybrid();
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	ASSERT_EQ(resolve_dict(*doc, IndirectRef(5u)).get_int("Marker"), 42);
	IncrementalWriter writer = make_writer(*doc);
	RECTO_ASSERT_OK(writer.set_object(IndirectRef(3u), PdfValue(dict_of(
		"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] >>"))));
	const Bytes out = write_ok(writer);
	EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
	auto reopened = open_doc(out);
	ASSERT_NE(reopened, nullptr);
	EXPECT_FALSE(reopened->is_recovered());
	EXPECT_EQ(resolve_dict(*reopened, IndirectRef(5u)).get_int("Marker"), 42); // only the old /XRefStm lists it
}

TEST(PdfIncrementalWriter, EncryptedDocument)
{
	for (const std::string_view rel :
		{"render/encrypted-aes-256.pdf", "render/encrypted-aes-128.pdf", "render/encrypted-rc4-128.pdf"}) {
		const Bytes base = corpus(rel);
		auto doc = open_doc(base, "recto-test");
		ASSERT_NE(doc, nullptr);
		IncrementalWriter writer = make_writer(*doc);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (Revised title) >>"))));
		const std::string body = "BT /F1 12 Tf 72 700 Td (Appended page text) Tj ET\n";
		auto content = writer.allocate_object(stream_of("<< >>", bytes_of(body)));
		RECTO_ASSERT_OK(content);
		const Bytes out = write_ok(writer);
		EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
		const std::string tail = text_of(std::span<const uint8_t>(out).subspan(base.size()));
		EXPECT_EQ(tail.find("Revised title"), std::string::npos) << rel;
		auto reopened = open_doc(out, "recto-test");
		ASSERT_NE(reopened, nullptr);
		EXPECT_EQ(string_entry(resolve_dict(*reopened, IndirectRef(6u)), "Title"), "Revised title") << rel;
		auto stream = reopened->resolve_object(*content);
		RECTO_ASSERT_OK(stream);
		ASSERT_NE(stream->as_stream(), nullptr);
		auto decoded = reopened->decode_stream(*stream->as_stream());
		RECTO_ASSERT_OK(decoded);
		EXPECT_EQ(text_of(*decoded), body) << rel;
		EXPECT_EQ(reopened->page_count(), 1u);
	}
}

TEST(PdfIncrementalWriter, RevisionOffsets)
{
	for (const std::string_view rel : {"render/simple-1p.pdf", "render/compressed-xref-2p.pdf"}) {
		const Bytes base = corpus(rel);
		auto doc = open_doc(base);
		ASSERT_NE(doc, nullptr);
		IncrementalWriter writer = make_writer(*doc);
		RECTO_ASSERT_OK(writer.allocate_object(PdfValue(dict_of("<< /A 1 >>"))));
		RECTO_ASSERT_OK(writer.allocate_object(stream_of("<< >>", bytes_of("q Q"))));
		auto rev = writer.revision();
		RECTO_ASSERT_OK(rev);
		EXPECT_EQ(rev->base_size, base.size());
		Bytes file = base;
		file.insert(file.end(), rev->bytes.begin(), rev->bytes.end());
		EXPECT_EQ(file, write_ok(writer));
		ASSERT_GE(rev->objects.size(), 2u);
		for (const WrittenObject& object : rev->objects) {
			const std::string head = std::to_string(object.ref.obj_num) + " " + std::to_string(object.ref.gen_num) + " obj";
			ASSERT_LE(object.offset + head.size(), file.size());
			EXPECT_EQ(text_of(std::span<const uint8_t>(file).subspan(object.offset, head.size())), head) << rel;
		}
	}
}

TEST(PdfIncrementalWriter, FailuresAreAtomic)
{
	const Bytes base = corpus("render/encrypted-aes-256.pdf");
	auto doc = open_doc(base, "recto-test");
	ASSERT_NE(doc, nullptr);
	IncrementalWriter writer = make_writer(*doc);
	RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (Atomic) >>"))));
	auto content = writer.allocate_object(stream_of("<< >>", bytes_of(std::string(4'000, 'q'))));
	RECTO_ASSERT_OK(content);
	{
		detail::ScopedFlateCompressAllocFailure fail;
		EXPECT_FALSE(writer.revision().has_value());
	}
	{
		detail::ScopedSerializeAllocFailure fail(64u);
		EXPECT_FALSE(writer.revision().has_value());
	}
	for (const auto kind : {detail::ScopedEncryptionFailure::Kind::Allocation, detail::ScopedEncryptionFailure::Kind::Randomness}) {
		detail::ScopedEncryptionFailure fail(kind, 2);
		EXPECT_FALSE(writer.revision().has_value());
	}
	// The staged edits and the base are as they were: the same revision now writes.
	const Bytes out = write_ok(writer);
	EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
	auto reopened = open_doc(out, "recto-test");
	ASSERT_NE(reopened, nullptr);
	EXPECT_EQ(string_entry(resolve_dict(*reopened, IndirectRef(6u)), "Title"), "Atomic");
	EXPECT_EQ(text_of(doc->file_bytes()), text_of(base));
}

TEST(PdfIncrementalWriter, SignaturePlaceholder)
{
	for (const bool encrypted : {false, true}) {
		const Bytes base = corpus(encrypted ? "render/encrypted-aes-256.pdf" : "render/simple-1p.pdf");
		auto doc = open_doc(base, encrypted ? "recto-test" : "");
		ASSERT_NE(doc, nullptr);
		IncrementalWriter writer = make_writer(*doc);
		RECTO_EXPECT_ERR(writer.add_signature(dict_of("<< /Type /Annot >>"), 64u), ErrorCode::InvalidArgument);
		RECTO_EXPECT_ERR(writer.add_signature(dict_of("<< /Type /Sig >>"), 0u), ErrorCode::InvalidArgument);
		auto sig = writer.add_signature(dict_of(
			"<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /Reason (Approved) >>"), 64u);
		RECTO_ASSERT_OK(sig);
		RECTO_EXPECT_ERR(writer.add_signature(dict_of("<< /Type /Sig >>"), 64u), ErrorCode::InvalidArgument);
		auto rev = writer.revision();
		RECTO_ASSERT_OK(rev);
		ASSERT_TRUE(rev->signature.has_value());
		const SignaturePlaceholder p = *rev->signature;
		EXPECT_EQ(p.ref, *sig);
		EXPECT_EQ(p.contents_length, 64u * 2u + 2u);
		Bytes file = base;
		file.insert(file.end(), rev->bytes.begin(), rev->bytes.end());
		// Patch both fields at their own lengths, as a signer does.
		const uint64_t after = p.contents_offset + p.contents_length;
		std::string range = "[0 " + std::to_string(p.contents_offset) + " " + std::to_string(after) + " " +
			std::to_string(file.size() - after);
		ASSERT_LT(range.size(), p.byte_range_length);
		range.append(p.byte_range_length - 1u - range.size(), ' ');
		range.push_back(']');
		ASSERT_EQ(text_of(std::span<const uint8_t>(file).subspan(p.byte_range_offset, 1u)), "[");
		ASSERT_EQ(text_of(std::span<const uint8_t>(file).subspan(p.contents_offset, 1u)), "<");
		std::memcpy(file.data() + p.byte_range_offset, range.data(), range.size());
		std::string contents = "<";
		for (int i = 0; i < 64; ++i) {
			contents += "AB";
		}
		contents += ">";
		std::memcpy(file.data() + p.contents_offset, contents.data(), contents.size());
		EXPECT_STRICT(file, 2u, XrefSectionKind::Classic);
		auto reopened = open_doc(file, encrypted ? "recto-test" : "");
		ASSERT_NE(reopened, nullptr);
		const PdfDict dict = resolve_dict(*reopened, *sig);
		const PdfValue* br = dict.get("ByteRange");
		ASSERT_NE(br, nullptr);
		ASSERT_NE(br->as_array(), nullptr);
		ASSERT_EQ(br->as_array()->size(), 4u);
		EXPECT_EQ((*br->as_array())[1].as_int(), static_cast<int64_t>(p.contents_offset));
		EXPECT_EQ((*br->as_array())[3].as_int(), static_cast<int64_t>(file.size() - after));
		const PdfString* value = dict.get_string("Contents");
		ASSERT_NE(value, nullptr);
		EXPECT_EQ(value->bytes, Bytes(64u, 0xABu)) << encrypted;
		EXPECT_EQ(string_entry(dict, "Reason"), "Approved") << encrypted;
	}
}

TEST(PdfIncrementalWriter, MultiRevisionChain)
{
	Bytes file = corpus("render/simple-1p.pdf");
	std::vector<IndirectRef> added;
	for (int rev = 1; rev <= 3; ++rev) {
		auto doc = open_doc(file);
		ASSERT_NE(doc, nullptr);
		IncrementalWriter writer = make_writer(*doc);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (Rev " + std::to_string(rev) + ") >>"))));
		auto ref = writer.allocate_object(PdfValue(dict_of("<< /Rev " + std::to_string(rev) + " >>")));
		RECTO_ASSERT_OK(ref);
		added.push_back(*ref);
		file = write_ok(writer);
		EXPECT_STRICT(file, static_cast<size_t>(rev) + 1u, XrefSectionKind::Classic);
		auto reopened = open_doc(file);
		ASSERT_NE(reopened, nullptr);
		EXPECT_EQ(string_entry(resolve_dict(*reopened, IndirectRef(6u)), "Title"), "Rev " + std::to_string(rev));
		for (size_t i = 0; i < added.size(); ++i) {
			EXPECT_EQ(resolve_dict(*reopened, added[i]).get_int("Rev"), static_cast<int64_t>(i + 1u));
		}
		EXPECT_EQ(reopened->page_count(), 1u);
	}
	EXPECT_EQ(added[0].obj_num, 7u);
	EXPECT_EQ(added[2].obj_num, 9u);
}

TEST(PdfIncrementalWriter, StrictReparse)
{
	{
		// A base with bytes before its header: offsets are written relative to the header.
		const Bytes base = build_pdf(kSmall, {}, "garbage before the header\n");
		auto doc = open_doc(base);
		ASSERT_NE(doc, nullptr);
		ASSERT_GT(doc->xref().header_offset(), 0u);
		IncrementalWriter writer = make_writer(*doc);
		RECTO_ASSERT_OK(writer.set_object(IndirectRef(3u), PdfValue(dict_of("<< /Type /Page /Parent 2 0 R /Rotate 90 >>"))));
		const Bytes out = write_ok(writer);
		EXPECT_STRICT(out, 2u, XrefSectionKind::Classic);
		auto reopened = open_doc(out);
		ASSERT_NE(reopened, nullptr);
		EXPECT_FALSE(reopened->is_recovered());
		EXPECT_EQ(resolve_dict(*reopened, IndirectRef(3u)).get_int("Rotate"), 90);
	}
	// The checks can fail: a changed offset, /Prev or /Index in the writer's output is caught.
	const Bytes base = corpus("render/simple-1p.pdf");
	auto doc = open_doc(base);
	ASSERT_NE(doc, nullptr);
	IncrementalWriter writer = make_writer(*doc);
	RECTO_ASSERT_OK(writer.set_object(IndirectRef(6u), PdfValue(dict_of("<< /Title (Offsets) >>"))));
	const Bytes out = write_ok(writer);
	ASSERT_TRUE(strict_ok(out, 2u, XrefSectionKind::Classic));
	const std::string text = text_of(out);
	{
		std::string bad = text;
		const size_t row = bad.find("6 1\n", base.size()) + 4u;
		// The hundreds digit: an offset off by one could still land on whitespace or a comment before the object.
		bad[row + 7u] = bad[row + 7u] == '0' ? '1' : '0';
		EXPECT_FALSE(strict_ok(bytes_of(bad), 2u, XrefSectionKind::Classic));
	}
	{
		std::string bad = text;
		const size_t prev = bad.find("/Prev 786", base.size());
		ASSERT_NE(prev, std::string::npos);
		bad.replace(prev, 9u, "/Prev 787");
		EXPECT_FALSE(strict_ok(bytes_of(bad), 2u, XrefSectionKind::Classic));
	}
	{
		const Bytes cbase = corpus("render/compressed-xref-2p.pdf");
		auto cdoc = open_doc(cbase);
		ASSERT_NE(cdoc, nullptr);
		IncrementalWriter cwriter = make_writer(*cdoc);
		RECTO_ASSERT_OK(cwriter.allocate_object(PdfValue(dict_of("<< /Index 1 >>"))));
		const Bytes cout = write_ok(cwriter);
		ASSERT_TRUE(strict_ok(cout, 2u, XrefSectionKind::Stream));
		std::string bad = text_of(cout);
		const size_t index = bad.find("/Index [0 1 ", cbase.size());
		ASSERT_NE(index, std::string::npos);
		bad[index + 10u] = '2'; // object 0's subsection now claims two rows
		EXPECT_FALSE(strict_ok(bytes_of(bad), 2u, XrefSectionKind::Stream));
	}
}

} // namespace
} // namespace recto::pdf
