// Unit tests for classic PDF xref tables, trailer merging, /Prev incremental
// update chains, header offset adjustment, and cycle guards (D01 T01 section 5).
#include "recto_pdf.h"
#include "test_support.h"

#include <cstdint>
#include <span>
#include <optional>
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

void verify_simple_1p_with_preamble(std::span<const uint8_t> preamble)
{
	const auto base_bytes = test::read_file_bytes(
		test::corpus_path("render/simple-1p.pdf"));
	ASSERT_FALSE(base_bytes.empty());

	std::vector<uint8_t> buf;
	buf.reserve(preamble.size() + base_bytes.size());
	buf.insert(buf.end(), preamble.begin(), preamble.end());
	buf.insert(buf.end(), base_bytes.begin(), base_bytes.end());

	auto xref_res = XrefTable::parse(buf);
	RECTO_ASSERT_OK(xref_res);
	const XrefTable& xref = *xref_res;

	EXPECT_EQ(xref.header_offset(), preamble.size());
	EXPECT_EQ(xref.pdf_version(), "1.7");
	EXPECT_EQ(xref.revision_count(), 1u);
	EXPECT_EQ(xref.root_ref(), (IndirectRef{1u, 0u}));
	EXPECT_EQ(xref.info_ref(), (IndirectRef{6u, 0u}));
	EXPECT_EQ(xref.trailer().get_int("Size"), 7);

	ASSERT_NE(xref.find(0), nullptr);
	EXPECT_TRUE(xref.find(0)->is_free());
	EXPECT_EQ(xref.find(0)->generation, 65535u);

	for (uint32_t obj_id = 1; obj_id <= 6; ++obj_id) {
		const XrefEntry* entry = xref.find(obj_id);
		ASSERT_NE(entry, nullptr) << "missing object " << obj_id;
		EXPECT_TRUE(entry->is_uncompressed());
		EXPECT_EQ(entry->generation, 0u);
		EXPECT_GT(entry->byte_offset, preamble.size());
	}

	auto root_val = xref.resolve_object(buf, IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(root_val);
	ASSERT_TRUE(root_val->is_dict());
	EXPECT_EQ(root_val->as_dict()->get_name("Type"), "Catalog");
	EXPECT_EQ(root_val->as_dict()->get_ref("Pages"), (IndirectRef{2u, 0u}));

	auto pages_val = xref.resolve_object(buf, IndirectRef{2u, 0u});
	RECTO_ASSERT_OK(pages_val);
	ASSERT_TRUE(pages_val->is_dict());
	EXPECT_EQ(pages_val->as_dict()->get_name("Type"), "Pages");
	EXPECT_EQ(pages_val->as_dict()->get_int("Count"), 1);

	auto page_val = xref.resolve_object(buf, IndirectRef{3u, 0u});
	RECTO_ASSERT_OK(page_val);
	ASSERT_TRUE(page_val->is_dict());
	EXPECT_EQ(page_val->as_dict()->get_name("Type"), "Page");

	auto stm_val = xref.resolve_object(buf, IndirectRef{4u, 0u});
	RECTO_ASSERT_OK(stm_val);
	ASSERT_TRUE(stm_val->is_stream());
	EXPECT_EQ(stm_val->as_stream()->dict.get_name("Filter"), "FlateDecode");
	EXPECT_FALSE(stm_val->as_stream()->raw_bytes.empty());

	auto font_val = xref.resolve_object(buf, IndirectRef{5u, 0u});
	RECTO_ASSERT_OK(font_val);
	ASSERT_TRUE(font_val->is_dict());
	EXPECT_EQ(font_val->as_dict()->get_name("BaseFont"), "Helvetica");

	auto info_val = xref.resolve_object(buf, IndirectRef{6u, 0u});
	RECTO_ASSERT_OK(info_val);
	ASSERT_TRUE(info_val->is_dict());
	ASSERT_NE(info_val->as_dict()->get_string("Title"), nullptr);
	EXPECT_EQ(info_val->as_dict()->get_string("Title")->as_byte_view(), "Recto Simple 1P");
}

// ISO 32000-1 Table 15: /Size is required, and an object numbered above it is ignored and treated as missing.
// An object stream's /DecodeParms may be an indirect reference: it resolves, and one pointing back into the same
// stream is a cycle that fails cleanly.
// ISO 32000-1 section 7.5.7: /N and /First may be indirect, and a compressed object consisting solely of an
// object reference is refused.
TEST(PdfXref, ObjStmIndirectCountsAndBareReferences)
{
	const auto build = [](std::string_view n_first, std::string_view payload, std::string_view obj3 = "4",
		std::string_view type = "/ObjStm") {
		std::string pdf = "%PDF-1.7\n";
		const size_t off2 = pdf.size();
		pdf += "2 0 obj\n1\nendobj\n";
		const size_t off3 = pdf.size();
		pdf += "3 0 obj\n" + std::string(obj3) + "\nendobj\n";
		const size_t off5 = pdf.size();
		pdf += "5 0 obj\n<< /Type " + std::string(type) + " " + std::string(n_first) + " /Length " +
			std::to_string(payload.size()) +
			" >>\nstream\n" + std::string(payload) + "\nendstream\nendobj\n";
		const size_t off6 = pdf.size();
		std::string rows(7 * 4, '\0');
		rows[3] = '\xFF';
		rows[4] = 2;
		rows[6] = 5;
		const auto uncompressed = [&](size_t obj, size_t off) {
			rows[obj * 4] = 1;
			rows[obj * 4 + 1] = static_cast<char>((off >> 8) & 0xFF);
			rows[obj * 4 + 2] = static_cast<char>(off & 0xFF);
		};
		uncompressed(2, off2);
		uncompressed(3, off3);
		uncompressed(5, off5);
		uncompressed(6, off6);
		pdf += "6 0 obj\n<< /Type /XRef /Size 7 /Root 1 0 R /W [1 2 1] /Length " + std::to_string(rows.size()) +
			" >>\nstream\n" + rows + "\nendstream\nendobj\nstartxref\n" + std::to_string(off6) + "\n%%EOF\n";
		return pdf;
	};

	const std::string indirect = build("/N 2 0 R /First 3 0 R", "1 0 42");
	auto xref = XrefTable::parse(as_bytes(indirect));
	RECTO_ASSERT_OK(xref);
	auto value = xref->resolve_object(as_bytes(indirect), IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(value);
	EXPECT_EQ(value->as_int(), std::optional<int64_t>(42));

	// /Type may be indirect too (object 3 here holds the name).
	const std::string indirect_type = build("/N 1 /First 4", "1 0 42", "/ObjStm", "3 0 R");
	auto type_xref = XrefTable::parse(as_bytes(indirect_type));
	RECTO_ASSERT_OK(type_xref);
	auto type_value = type_xref->resolve_object(as_bytes(indirect_type), IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(type_value);
	EXPECT_EQ(type_value->as_int(), std::optional<int64_t>(42));

	const std::string bare = build("/N 1 /First 4", "1 0 2 0 R");
	auto bare_xref = XrefTable::parse(as_bytes(bare));
	RECTO_ASSERT_OK(bare_xref);
	RECTO_EXPECT_ERR(bare_xref->read_indirect_object(as_bytes(bare), IndirectRef{1u, 0u}), ErrorCode::DamagedFormat);
}

// An xref stream entry numbered above /Size is missing, so even an offset outside the file in it is no error.
// The decoded object-stream cache every owner uses is bounded by count and by decoded bytes, oldest out first, and
// never keeps a container larger than the whole byte budget.
TEST(PdfXref, ObjStmCacheBoundsCountAndBytes)
{
	const auto stream_of = [](size_t bytes) {
		CachedObjStream s;
		s.decoded_bytes.assign(bytes, 0x20u);
		return s;
	};
	ObjStmCache cache;
	CachedObjStream overflow;
	const size_t quarter = kMaxCachedObjStmBytes / 4u;
	for (uint32_t i = 1; i <= 5; ++i) {
		static_cast<void>(cache.insert(i, stream_of(quarter), overflow));
		EXPECT_LE(cache.bytes(), kMaxCachedObjStmBytes);
	}
	EXPECT_EQ(cache.size(), 4u);
	EXPECT_EQ(cache.find(1), nullptr); // oldest left first
	EXPECT_NE(cache.find(5), nullptr);

	const CachedObjStream& huge = cache.insert(9, stream_of(kMaxCachedObjStmBytes + 1u), overflow);
	EXPECT_EQ(&huge, &overflow);
	EXPECT_EQ(cache.find(9), nullptr);
	EXPECT_EQ(cache.size(), 4u);

	// An allocation failing inside insert (reserving the order, then inserting the entry) leaves the cache exactly
	// as it was: the entry count and the byte total still agree with what is stored, and later inserts still evict.
	for (const int failing_allocation : {0, 1}) {
		ObjStmCache safe;
		static_cast<void>(safe.insert(1, stream_of(10), overflow));
		static_cast<void>(safe.insert(2, stream_of(20), overflow));
		{
			detail::ScopedObjStmCacheAllocFaultInjector fault(failing_allocation);
			EXPECT_THROW(static_cast<void>(safe.insert(3, stream_of(30), overflow)), std::bad_alloc);
		}
		EXPECT_EQ(safe.size(), 2u) << failing_allocation;
		EXPECT_EQ(safe.bytes(), 30u) << failing_allocation;
		EXPECT_EQ(safe.find(3), nullptr);
		static_cast<void>(safe.insert(3, stream_of(30), overflow));
		EXPECT_EQ(safe.bytes(), 60u);
		for (uint32_t i = 4; i <= static_cast<uint32_t>(kMaxCachedObjStreams) + 4u; ++i) {
			static_cast<void>(safe.insert(i, stream_of(1), overflow));
		}
		EXPECT_EQ(safe.size(), kMaxCachedObjStreams);
	}

	ObjStmCache counted;
	for (uint32_t i = 1; i <= static_cast<uint32_t>(kMaxCachedObjStreams) + 3u; ++i) {
		static_cast<void>(counted.insert(i, stream_of(1), overflow));
	}
	EXPECT_EQ(counted.size(), kMaxCachedObjStreams);
	EXPECT_EQ(counted.find(1), nullptr);
	counted.erase(static_cast<uint32_t>(kMaxCachedObjStreams) + 3u);
	EXPECT_EQ(counted.size(), kMaxCachedObjStreams - 1u);
}

// Recovery decodes a container whose /DecodeParms, /N or /First is an indirect reference to an object later in the
// file, so a sole compressed /Catalog survives; definitions keep the file's order (a later one wins, either way), and
// the catalog fallback prefers the latest in the file, wherever its container was unpacked (D06 T05 §29).
TEST(PdfXref, RecoveryResolvesIndirectObjStmParameters)
{
	const auto hex = [](const std::string& text) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char c : text) {
			out += kHex[c >> 4u];
			out += kHex[c & 0x0Fu];
		}
		return out + ">";
	};
	const std::string members = "3 0 << /Type /Catalog /Pages 4 0 R >>";
	const auto container = [&](const std::string& n_and_first, const std::string& parms) {
		const std::string data = hex(members);
		return "1 0 obj\n<< /Type /ObjStm " + n_and_first + " /Filter /ASCIIHexDecode" + parms + " /Length " +
			std::to_string(data.size()) + " >>\nstream\n" + data + "\nendstream\nendobj\n";
	};
	const std::string pages = "4 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
	const std::string header = "%PDF-1.7\n";

	// /DecodeParms 2 0 R, with object 2 after the container.
	const std::string by_parms =
		header + container("/N 1 /First 4", " /DecodeParms 2 0 R") + "2 0 obj\n<< >>\nendobj\n" + pages + "%%EOF\n";
	auto parms_rec = XrefTable::recover_damaged_xref(as_bytes(by_parms), 64);
	RECTO_ASSERT_OK(parms_rec);
	EXPECT_EQ(parms_rec->root_ref(), (IndirectRef{3u, 0u}));
	ASSERT_NE(parms_rec->find(3), nullptr);
	EXPECT_TRUE(parms_rec->find(3)->is_compressed());

	// /N 5 0 R /First 6 0 R, both after the container.
	const std::string by_counts = header + container("/N 5 0 R /First 6 0 R", "") + pages +
		"5 0 obj\n1\nendobj\n6 0 obj\n4\nendobj\n%%EOF\n";
	auto counts_rec = XrefTable::recover_damaged_xref(as_bytes(by_counts), 64);
	RECTO_ASSERT_OK(counts_rec);
	EXPECT_EQ(counts_rec->root_ref(), (IndirectRef{3u, 0u}));

	// A later uncompressed definition of object 3 overrides the deferred member; an earlier one is overridden by it.
	const std::string later_plain = by_parms.substr(0, by_parms.rfind("%%EOF")) +
		"3 0 obj\n<< /Type /Catalog /Pages 4 0 R /Later true >>\nendobj\n%%EOF\n";
	auto later_rec = XrefTable::recover_damaged_xref(as_bytes(later_plain), 64);
	RECTO_ASSERT_OK(later_rec);
	ASSERT_NE(later_rec->find(3), nullptr);
	EXPECT_TRUE(later_rec->find(3)->is_uncompressed());
	const std::string earlier_plain = header + "3 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" +
		by_parms.substr(header.size());
	auto earlier_rec = XrefTable::recover_damaged_xref(as_bytes(earlier_plain), 64);
	RECTO_ASSERT_OK(earlier_rec);
	ASSERT_NE(earlier_rec->find(3), nullptr);
	EXPECT_TRUE(earlier_rec->find(3)->is_compressed());

	// Two catalogs, no trailer: the one later in the file is the root, whichever is compressed.
	const std::string catalog_after =
		by_parms.substr(0, by_parms.rfind("%%EOF")) + "7 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n%%EOF\n";
	auto after_rec = XrefTable::recover_damaged_xref(as_bytes(catalog_after), 64);
	RECTO_ASSERT_OK(after_rec);
	EXPECT_EQ(after_rec->root_ref(), (IndirectRef{7u, 0u}));
	const std::string catalog_before =
		header + "7 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + by_parms.substr(header.size());
	auto before_rec = XrefTable::recover_damaged_xref(as_bytes(catalog_before), 64);
	RECTO_ASSERT_OK(before_rec);
	EXPECT_EQ(before_rec->root_ref(), (IndirectRef{3u, 0u}));
}

// The deferred path's other parameter forms and its refusals: an indirect /Filter, a /Filter array of references and
// a /DecodeParms array of references recover; a missing, wrong-generation or self-referencing target and an invalid
// indirect count leave the container out and keep an independent catalog; a parameter dictionary four containers
// share is parsed once, so a file it dominates still recovers; and the /Info fallback keeps file order like /Root.
TEST(PdfXref, RecoveryDeferredContainerEdges)
{
	const auto hex = [](const std::string& text) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char c : text) {
			out += kHex[c >> 4u];
			out += kHex[c & 0x0Fu];
		}
		return out + ">";
	};
	// `keys` holds /N, /First and the filter keys, so each case states its own.
	const auto container = [&](uint32_t num, const std::string& members, const std::string& keys) {
		const std::string data = hex(members);
		return std::to_string(num) + " 0 obj\n<< /Type /ObjStm " + keys + " /Length " + std::to_string(data.size()) +
			" >>\nstream\n" + data + "\nendstream\nendobj\n";
	};
	const std::string catalog3 = "3 0 << /Type /Catalog /Pages 4 0 R >>";
	const std::string pages = "4 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
	const std::string header = "%PDF-1.7\n";
	const auto recover = [](const std::string& pdf) { return XrefTable::recover_damaged_xref(as_bytes(pdf), 64); };

	// Indirect /Filter, a /Filter array of references, and a /DecodeParms array of references.
	for (const std::string keys : {std::string("/N 1 /First 4 /Filter 8 0 R"), std::string("/N 1 /First 4 /Filter [8 0 R]"),
			 std::string("/N 1 /First 4 /Filter [/ASCIIHexDecode] /DecodeParms [2 0 R]")}) {
		const std::string pdf = header + container(1u, catalog3, keys) + "2 0 obj\n<< >>\nendobj\n" + pages +
			"8 0 obj\n/ASCIIHexDecode\nendobj\n%%EOF\n";
		auto rec = recover(pdf);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{3u, 0u})) << keys;
	}

	// Targets that cannot serve: the container is left out, and the independent uncompressed catalog 7 is the root.
	const std::string catalog7 = "7 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n";
	for (const std::string keys : {std::string("/N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 1 0 R"),
			 std::string("/N 5 0 R /First 4 /Filter /ASCIIHexDecode"),
			 std::string("/N 1 /First 9 1 R /Filter /ASCIIHexDecode /DecodeParms [99 0 R]")}) {
		const std::string pdf = header + catalog7 + container(1u, catalog3, keys) + pages +
			"5 0 obj\n(not a count)\nendobj\n9 0 obj\n4\nendobj\n%%EOF\n";
		auto rec = recover(pdf);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{7u, 0u})) << keys;
		EXPECT_EQ(rec->find(3), nullptr) << keys;
	}

	// One 16 KiB parameter dictionary shared by four containers dominates the file; it is parsed once, so the
	// recovery parse budget (four times the file) is not spent on reparsing it.
	{
		std::string pdf = header + "2 0 obj\n<< /Unused (" + std::string(16384u, 'u') + ") >>\nendobj\n";
		for (uint32_t n = 11u; n < 15u; ++n) {
			pdf += container(n, std::to_string(n + 10u) + " 0 << /Type /Catalog /Pages 4 0 R >>",
				"/N 1 /First 5 /Filter /ASCIIHexDecode /DecodeParms 2 0 R");
		}
		pdf += pages + "%%EOF\n";
		auto rec = recover(pdf);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{24u, 0u}));
	}

	// /Info: the later of a compressed and an uncompressed metadata dictionary, in either order.
	const std::string info_member = "6 0 << /Title (Compressed) >>";
	const std::string info_plain = "9 0 obj\n<< /Title (Plain) >>\nendobj\n";
	const std::string deferred_info =
		container(1u, info_member, "/N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 2 0 R") + "2 0 obj\n<< >>\nendobj\n";
	auto plain_later = recover(header + catalog7 + pages + deferred_info + info_plain + "%%EOF\n");
	RECTO_ASSERT_OK(plain_later);
	EXPECT_EQ(plain_later->info_ref(), (IndirectRef{9u, 0u}));
	auto compressed_later = recover(header + catalog7 + pages + info_plain + deferred_info + "%%EOF\n");
	RECTO_ASSERT_OK(compressed_later);
	EXPECT_EQ(compressed_later->info_ref(), (IndirectRef{6u, 0u}));
}

// Round 1 of D06 T05 §29 and §30: a huge parameter object eight deferred containers share is kept reduced, so the
// lookups after the first copy a few scalars, not the object; an intact xref row naming a deferred container's
// member does not stop that member's catalog being recognised; and an escaped /Encr#79pt key still marks the file
// as possibly encrypted, so its containers are not decoded before authentication.
// Plan review of D06 T05 §29, gap 1: a container whose /Type is an indirect reference (ISO 32000-1 section 7.5.7 lets
// any uncompressed dictionary value be indirect) is recognised once the type resolves, whether the type object comes
// before or after it; a stream whose indirect /Type is something else is not read as a container.
TEST(PdfXref, RecoveryIndirectObjStmType)
{
	const auto hex = [](const std::string& text) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char c : text) {
			out += kHex[c >> 4u];
			out += kHex[c & 0x0Fu];
		}
		return out + ">";
	};
	const std::string data = hex("3 0 << /Type /Catalog /Pages 4 0 R /Marker 77 >>");
	const std::string stream = "1 0 obj\n<< /Type 2 0 R /N 1 /First 4 /Filter /ASCIIHexDecode /Length " +
		std::to_string(data.size()) + " >>\nstream\n" + data + "\nendstream\nendobj\n";
	const std::string pages = "4 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
	const std::string header = "%PDF-1.7\n";
	const std::string objstm_type = "2 0 obj\n/ObjStm\nendobj\n";
	for (const bool type_first : {false, true}) {
		const std::string pdf = type_first ? header + objstm_type + stream + pages + "%%EOF\n"
										   : header + stream + objstm_type + pages + "%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{3u, 0u})) << type_first;
		ASSERT_NE(rec->find(3), nullptr) << type_first;
		EXPECT_TRUE(rec->find(3)->is_compressed()) << type_first;
		auto catalog = rec->resolve_object(as_bytes(pdf), IndirectRef{3u, 0u});
		RECTO_ASSERT_OK(catalog);
		ASSERT_NE(catalog->as_dict(), nullptr);
		EXPECT_EQ(catalog->as_dict()->get_int("Marker"), 77) << type_first;
	}
	// An indirect /Type that is not /ObjStm: not a container; the independent catalog 7 is the root.
	const std::string other = header + stream + "2 0 obj\n/XObject\nendobj\n" + pages +
		"7 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n%%EOF\n";
	auto rec = XrefTable::recover_damaged_xref(as_bytes(other), 64);
	RECTO_ASSERT_OK(rec);
	EXPECT_EQ(rec->root_ref(), (IndirectRef{7u, 0u}));
	EXPECT_EQ(rec->find(3), nullptr);
}

// The /Length candidate readings check the type too: a stream whose indirect /Type is /XObject, though its payload
// reads like a container defining the integer a stream's indirect /Length names, offers no length. So the stream
// stays sized at its first `endstream`, and the object after that keyword is still recovered.
TEST(PdfXref, RecoveryIndirectTypeFeedsNoLengths)
{
	const std::string payload = "AAA\nendstream\nendobj\n12 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\nBBB";
	const auto hex = [](const std::string& text) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char c : text) {
			out += kHex[c >> 4u];
			out += kHex[c & 0x0Fu];
		}
		return out + ">";
	};
	const std::string fake = hex("11 0 " + std::to_string(payload.size()));
	const std::string pdf = std::string("%PDF-1.7\n") + "7 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" +
		"10 0 obj\n<< /Length 11 0 R >>\nstream\n" + payload + "\nendstream\nendobj\n" +
		"1 0 obj\n<< /Type 2 0 R /N 1 /First 5 /Filter /ASCIIHexDecode /Length " + std::to_string(fake.size()) +
		" >>\nstream\n" + fake + "\nendstream\nendobj\n" + "2 0 obj\n/XObject\nendobj\n" +
		"4 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n%%EOF\n";
	auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
	RECTO_ASSERT_OK(rec);
	ASSERT_NE(rec->find(12), nullptr); // read as an object: no length candidate came from the /XObject stream
	EXPECT_TRUE(rec->find(12)->is_uncompressed());
	EXPECT_EQ(rec->find(11), nullptr);
}

// Plan review of D06 T05 §29, gap 2: a wrong-generation reference and a two-object reference cycle leave the container
// out with the independent catalog kept, and a real predictor (Flate with PNG Up rows, its /DecodeParms indirect and
// its /Columns an indirect integer) decodes to the member values.
TEST(PdfXref, RecoveryResolverEdges)
{
	const std::string header = "%PDF-1.7\n";
	const std::string pages = "4 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
	const std::string catalog7 = "7 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n";
	const std::string members = "3 0 << /Type /Catalog /Pages 4 0 R /Marker 91 >>";
	const auto hex_container = [&](const std::string& keys) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string data;
		for (const unsigned char c : members) {
			data += kHex[c >> 4u];
			data += kHex[c & 0x0Fu];
		}
		data += ">";
		return "1 0 obj\n<< /Type /ObjStm " + keys + " /Filter /ASCIIHexDecode /Length " + std::to_string(data.size()) +
			" >>\nstream\n" + data + "\nendstream\nendobj\n";
	};
	for (const auto& [keys, extra] : std::vector<std::pair<std::string, std::string>>{
			 {"/N 5 1 R /First 4", "5 0 obj\n1\nendobj\n"},                            // wrong generation
			 {"/N 5 0 R /First 4", "5 0 obj\n6 0 R\nendobj\n6 0 obj\n5 0 R\nendobj\n"}, // a cycle
		 }) {
		const std::string pdf = header + catalog7 + hex_container(keys) + extra + pages + "%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{7u, 0u})) << keys;
		EXPECT_EQ(rec->find(3), nullptr) << keys;
	}

	// PNG Up predictor rows of 8 columns, Flate-compressed.
	constexpr size_t kColumns = 8u;
	std::string raw = members;
	raw.append((kColumns - raw.size() % kColumns) % kColumns, ' ');
	std::vector<uint8_t> predicted;
	for (size_t row = 0; row < raw.size() / kColumns; ++row) {
		predicted.push_back(2u); // Up
		for (size_t c = 0; c < kColumns; ++c) {
			const uint8_t above = row == 0u ? 0u : static_cast<uint8_t>(raw[(row - 1u) * kColumns + c]);
			predicted.push_back(static_cast<uint8_t>(static_cast<uint8_t>(raw[row * kColumns + c]) - above));
		}
	}
	auto packed = flate_compress(predicted);
	RECTO_ASSERT_OK(packed);
	const std::string data(packed->begin(), packed->end());
	const std::string pdf = header + "1 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Filter /FlateDecode /DecodeParms 2 0 R /Length " +
		std::to_string(data.size()) + " >>\nstream\n" + data + "\nendstream\nendobj\n" +
		"2 0 obj\n<< /Predictor 12 /Columns 8 0 R >>\nendobj\n8 0 obj\n" + std::to_string(kColumns) + "\nendobj\n" +
		pages + "%%EOF\n";
	auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
	RECTO_ASSERT_OK(rec);
	EXPECT_EQ(rec->root_ref(), (IndirectRef{3u, 0u}));
	ASSERT_NE(rec->find(3), nullptr);
	EXPECT_TRUE(rec->find(3)->is_compressed());
	auto catalog = rec->resolve_object(as_bytes(pdf), IndirectRef{3u, 0u});
	RECTO_ASSERT_OK(catalog);
	ASSERT_NE(catalog->as_dict(), nullptr);
	EXPECT_EQ(catalog->as_dict()->get_int("Marker"), 91);
}

TEST(PdfXref, RecoveryRoundOneFixes)
{
	const auto hex = [](const std::string& text) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char c : text) {
			out += kHex[c >> 4u];
			out += kHex[c & 0x0Fu];
		}
		return out + ">";
	};
	const auto container = [&](uint32_t num, const std::string& members, const std::string& keys) {
		const std::string data = hex(members);
		return std::to_string(num) + " 0 obj\n<< /Type /ObjStm " + keys + " /Length " + std::to_string(data.size()) +
			" >>\nstream\n" + data + "\nendstream\nendobj\n";
	};
	const std::string pages = "4 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";

	// Eight containers share /DecodeParms 2 0 R, a dictionary holding 200,000 integers.
	{
		std::string big = "2 0 obj\n<< /Unused [";
		for (int i = 0; i < 200000; ++i) {
			big += "0 ";
		}
		big += "] >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n" + big;
		for (uint32_t n = 11u; n < 19u; ++n) {
			pdf += container(n, std::to_string(n + 10u) + " 0 << /Type /Catalog /Pages 4 0 R >>",
				"/N 1 /First 5 /Filter /ASCIIHexDecode /DecodeParms 2 0 R");
		}
		pdf += pages + "%%EOF\n";
		const test::ScopedHeapMeter meter;
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		const size_t allocated = meter.allocated_bytes();
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{28u, 0u}));
		if (test::ScopedHeapMeter::available()) {
			// The scan's own parse of object 2 and the resolver's one parse, not one more copy per container.
			const test::ScopedHeapMeter one_parse;
			auto alone = XrefTable::recover_damaged_xref(as_bytes("%PDF-1.7\n" + big + pages + "%%EOF\n"), 64);
			const size_t baseline = one_parse.allocated_bytes();
			EXPECT_FALSE(alone.has_value()); // no catalog: it only measures what one parse of object 2 costs
			EXPECT_LT(allocated, 3u * baseline + (size_t{4} << 20u)) << "baseline " << baseline;
		}
	}

	// Round 2: a shared dictionary 100,000 keys wide is kept as only the keys a filter reads, so eight lookups cost a
	// few bytes each, not a copy of every key.
	if (test::ScopedHeapMeter::available()) {
		std::string wide = "2 0 obj\n<<";
		for (int i = 0; i < 100000; ++i) {
			wide += " /K" + std::to_string(i) + " 0";
		}
		wide += " >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n" + wide;
		for (uint32_t n = 11u; n < 19u; ++n) {
			pdf += container(n, std::to_string(n + 10u) + " 0 << /Type /Catalog /Pages 4 0 R >>",
				"/N 1 /First 5 /Filter /ASCIIHexDecode /DecodeParms 2 0 R");
		}
		pdf += pages + "%%EOF\n";
		const test::ScopedHeapMeter meter;
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		const size_t allocated = meter.allocated_bytes();
		RECTO_ASSERT_OK(rec);
		const test::ScopedHeapMeter one_parse;
		auto alone = XrefTable::recover_damaged_xref(as_bytes("%PDF-1.7\n" + wide + pages + "%%EOF\n"), 64);
		const size_t baseline = one_parse.allocated_bytes();
		EXPECT_FALSE(alone.has_value());
		EXPECT_LT(allocated, 3u * baseline + (size_t{4} << 20u)) << "baseline " << baseline;
	}

	// Round 2: a later xref row that names object 3 in container 1 keeps its authority after container 1 is unpacked,
	// so container 5, between them in the file, does not take object 3 over.
	{
		std::string pdf = "%PDF-1.7\n" +
			container(1u, "3 0 << /Type /Catalog /Pages 4 0 R >>", "/N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 2 0 R") +
			"2 0 obj\n<< >>\nendobj\n" + pages +
			container(5u, "3 0 << /Type /Catalog /Pages 4 0 R /Second true >>",
				"/N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 2 0 R");
		const std::string row("\x02\x00\x00\x00\x01\x00\x00", 7); // object 3: type 2, in container 1, index 0
		pdf += "9 0 obj\n<< /Type /XRef /Size 10 /Root 3 0 R /Index [3 1] /W [1 4 2] /Length 7 >>\nstream\n" + row +
			"\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(3), nullptr);
		EXPECT_EQ(rec->find(3)->obj_stm_num, 1u);
	}

	// A deferred container's catalog member, also named by an intact xref stream's row after it.
	{
		std::string pdf = "%PDF-1.7\n" +
			container(1u, "3 0 << /Type /Catalog /Pages 4 0 R >>", "/N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 2 0 R") +
			"2 0 obj\n<< >>\nendobj\n" + pages;
		const std::string row("\x02\x00\x00\x00\x01\x00\x00", 7); // object 3: type 2, in container 1, index 0
		pdf += "9 0 obj\n<< /Type /XRef /Size 10 /Root 3 0 R /Index [3 1] /W [1 4 2] /Length 7 >>\nstream\n" + row +
			"\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{3u, 0u}));
	}

	// Three 400 KiB containers in a file whose trailer key is escaped as /Encr#79pt, under a 1 MiB decode budget:
	// they are deferred and left undecoded, so the file recovers instead of exhausting the budget on ciphertext.
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		for (uint32_t n = 40u; n < 43u; ++n) {
			const std::string cipher(409600u, static_cast<char>(0xA5));
			pdf += std::to_string(n) + " 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length 409600 >>\nstream\n" + cipher +
				"\nendstream\nendobj\n";
		}
		pdf += "9 0 obj\n<< /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>\nendobj\n"
			   "trailer\n<< /Size 43 /Root 1 0 R /Encr#79pt 9 0 R >>\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 16);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{1u, 0u}));
	}
}

// Round 3 of D06 T05 §29 and §30: a later xref row that names a definition the scan replaced restores it with what
// the scan learned of it, a type-1 row vouches for the definition it names, and a mere mention of /Encrypt does not
// defer an ordinary file's containers.
TEST(PdfXref, RecoveryRoundThreeFixes)
{
	const auto hex = [](const std::string& text) {
		static constexpr char kHex[] = "0123456789ABCDEF";
		std::string out;
		for (const unsigned char c : text) {
			out += kHex[c >> 4u];
			out += kHex[c & 0x0Fu];
		}
		return out + ">";
	};
	const auto container = [&](uint32_t num, const std::string& members, const std::string& keys) {
		const std::string data = hex(members);
		return std::to_string(num) + " 0 obj\n<< /Type /ObjStm " + keys + " /Length " + std::to_string(data.size()) +
			" >>\nstream\n" + data + "\nendstream\nendobj\n";
	};
	const auto type1_row = [](size_t offset) {
		std::string row("\x01", 1);
		for (int shift = 24; shift >= 0; shift -= 8) {
			row.push_back(static_cast<char>((offset >> shift) & 0xFFu));
		}
		return row + std::string(2u, '\0');
	};
	const std::string pages = "4 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";

	// Both containers decode during the scan, container 5 replacing container 1's catalog 3; the intact row that names
	// container 1 again restores that catalog, so the file recovers with root 3 in container 1.
	{
		std::string pdf = "%PDF-1.7\n" + container(1u, "3 0 << /Type /Catalog /Pages 4 0 R >>", "/N 1 /First 4 /Filter /ASCIIHexDecode") +
			pages + container(5u, "3 0 << /Type /Catalog /Pages 4 0 R /Second true >>", "/N 1 /First 4 /Filter /ASCIIHexDecode");
		const std::string row("\x02\x00\x00\x00\x01\x00\x00", 7); // object 3: type 2, in container 1, index 0
		pdf += "9 0 obj\n<< /Type /XRef /Size 10 /Root 3 0 R /Index [3 1] /W [1 4 2] /Length 7 >>\nstream\n" + row +
			"\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{3u, 0u}));
		ASSERT_NE(rec->find(3), nullptr);
		EXPECT_EQ(rec->find(3)->obj_stm_num, 1u);
	}

	// Object 3 uncompressed, then xref stream 9 naming it in container 20, then xref stream 10 naming the uncompressed
	// definition again with a type-1 row: the last row wins (ISO 32000-1 7.5.8.3).
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n" +
			container(20u, "3 0 << /Type /Pages /Kids [] /Count 0 >>", "/N 1 /First 4 /Filter /ASCIIHexDecode");
		const size_t uncompressed_at = pdf.size();
		pdf += "3 0 obj\n<< /Type /Pages /Kids [] /Count 0 /Second true >>\nendobj\n";
		const std::string row_a("\x02\x00\x00\x00\x14\x00\x00", 7); // object 3: type 2, in container 20, index 0
		pdf += "9 0 obj\n<< /Type /XRef /Size 21 /Root 1 0 R /Index [3 1] /W [1 4 2] /Length 7 >>\nstream\n" + row_a +
			"\nendstream\nendobj\n";
		pdf += "10 0 obj\n<< /Type /XRef /Size 21 /Root 1 0 R /Index [3 1] /W [1 4 2] /Length 7 >>\nstream\n" +
			type1_row(uncompressed_at) + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(3), nullptr);
		EXPECT_TRUE(rec->find(3)->is_uncompressed());
		EXPECT_EQ(rec->find(3)->byte_offset, uncompressed_at);
	}

	// Object 3 uncompressed, then a deferred container holding object 3 (its parameters are indirect), then a type-1
	// row naming the uncompressed definition: the row's position keeps it, so the container does not take it over.
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			"2 0 obj\n<< >>\nendobj\n";
		const size_t uncompressed_at = pdf.size();
		pdf += "3 0 obj\n42\nendobj\n" +
			container(20u, "3 0 7", "/N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 2 0 R");
		pdf += "10 0 obj\n<< /Type /XRef /Size 21 /Root 1 0 R /Index [3 1] /W [1 4 2] /Length 7 >>\nstream\n" +
			type1_row(uncompressed_at) + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(3), nullptr);
		EXPECT_TRUE(rec->find(3)->is_uncompressed());
		EXPECT_EQ(rec->find(3)->byte_offset, uncompressed_at);
	}

	// A file that mentions /Encrypt in a comment or a string keeps its containers decoded during the scan, so a
	// stream whose /Length is a compressed integer is skipped by that length, and the catalog written inside its data
	// is never read as a definition.
	for (const std::string mention : {"% notes on /Encrypt 5 0 R\n", "6 0 obj\n(see /Encrypt)\nendobj\n",
			 "6 0 obj\n(see (nested) /Encrypt 5 0 R)\nendobj\n",
			 "7 0 obj\n<< /Length 14 >>\nstream\n/Encrypt 5 0 R\nendstream\nendobj\n",
			 "8 0 obj\n[/Encrypt 5 0 R]\nendobj\n",
			 "7 0 obj\n<< /Length 47 >>\nstream\nx\nendstream\ntrailer\n<< /Encrypt 5 0 R >> %%EOF\n\nendstream\nendobj\n"}) {
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n" + mention;
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at) << mention;
	}

	// Round 4: a trailer key with a comment between its tokens still defers an encrypted file's containers: three
	// 400 KiB containers under a 1 MiB decode budget recover instead of exhausting it on ciphertext.
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		for (uint32_t n = 40u; n < 43u; ++n) {
			const std::string cipher(409600u, static_cast<char>(0xA5));
			pdf += std::to_string(n) + " 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length 409600 >>\nstream\n" + cipher +
				"\nendstream\nendobj\n";
		}
		pdf += "9 0 obj\n<< /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>\nendobj\n"
			   "trailer\n<< /Size 43 /Root 1 0 R /Encrypt % the handler\n9 0 R >>\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 16);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{1u, 0u}));
	}

	// Round 4: when the early warning misses the key (an unbalanced parenthesis before the trailer reads as one long
	// string), the parsed trailer still decides: what the container decoded during the scan taught is discarded, and
	// only the member an intact xref row names keeps its entry.
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "21 0 22 2 7 8", "/N 2 /First 10 /Filter /ASCIIHexDecode");
		const std::string row("\x02\x00\x00\x00\x14\x00\x01", 7); // object 22: type 2, in container 20, index 1
		pdf += "30 0 obj\n<< /Type /XRef /Size 31 /Index [22 1] /W [1 4 2] /Length 7 >>\nstream\n" + row +
			"\nendstream\nendobj\n";
		pdf += "junk (unbalanced\n9 0 obj\n<< /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>\nendobj\n"
			   "trailer\n<< /Size 31 /Root 1 0 R /Encrypt 9 0 R >>\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->find(21), nullptr);
		ASSERT_NE(rec->find(22), nullptr);
		EXPECT_EQ(rec->find(22)->obj_stm_num, 20u);
	}

	// Round 5: an encrypted file whose three 400 KiB containers would pass the 1 MiB decode budget of a 16-entry
	// recovery: nothing is decoded before the parsed trailer decides, and a stream whose /Length names a member of one
	// reads what the budget allows and then stops, so the file recovers.
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		for (uint32_t n = 40u; n < 43u; ++n) {
			const std::string cipher(409600u, static_cast<char>(0xA5));
			pdf += std::to_string(n) + " 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length 409600 >>\nstream\n" + cipher +
				"\nendstream\nendobj\n";
		}
		pdf += "50 0 obj\n<< /Length 60 0 R >>\nstream\nabcd\nendstream\nendobj\n"
			   "junk (unbalanced\n9 0 obj\n<< /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>\nendobj\n"
			   "trailer\n<< /Size 61 /Root 1 0 R /Encrypt 9 0 R >>\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 16);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{1u, 0u}));
	}

	// Round 6: a probe reads a container whose parameters are indirect, through the objects scanned so far, so the
	// stream after it is skipped by its true /Length and the catalog in its data is never read.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages + "2 0 obj\n<< >>\nendobj\n" +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode /DecodeParms 2 0 R") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 6: two containers hold object 88, and an intact xref row selects the first: the stream's /Length is that
	// member's value, not the later container's.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			container(21u, "88 0 3", "/N 1 /First 5 /Filter /ASCIIHexDecode");
		const std::string row("\x02\x00\x00\x00\x14\x00\x00", 7); // object 88: type 2, in container 20, index 0
		pdf += "40 0 obj\n<< /Type /XRef /Size 89 /Root 1 0 R /Index [88 1] /W [1 4 2] /Length 7 >>\nstream\n" + row +
			"\nendstream\nendobj\n";
		pdf += "30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 6: probes have their own allowance, so ciphertext they decode never leaves an intact xref stream after
	// them without the budget to read it.
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		for (uint32_t n = 40u; n < 42u; ++n) { // two 512 KiB containers: the whole 1 MiB of a 16-entry recovery
			const std::string cipher(524288u, static_cast<char>(0xA5));
			pdf += std::to_string(n) + " 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length 524288 >>\nstream\n" + cipher +
				"\nendstream\nendobj\n";
		}
		pdf += "50 0 obj\n<< /Length 60 0 R >>\nstream\nabcd\nendstream\nendobj\n";
		const std::string row("\x01\x00\x00\x00\x09\x00\x00", 7); // object 1: type 1 at offset 9
		pdf += "70 0 obj\n<< /Type /XRef /Size 71 /Root 1 0 R /Encrypt 9 0 R /Index [1 1] /W [1 4 2] /Length 7 >>\nstream\n" +
			row + "\nendstream\nendobj\n9 0 obj\n<< /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 16);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{1u, 0u}));
	}

	// Round 7: a probe that meets a container before the scan has defined its parameter object waits instead of giving
	// up, and reads the parameter afresh once it is defined, so a later stream's /Length still comes from it.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter 2 0 R") +
			"25 0 obj\n<< /Length 99 0 R >>\nstream\nab\nendstream\nendobj\n" // probes before object 2 exists
			"2 0 obj\n/ASCIIHexDecode\nendobj\n" +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 8: a container probed with a parameter that a later definition replaces is probed again with the new one,
	// so a stream after the redefinition reads its /Length from it.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages + "2 0 obj\n/ASCII85Decode\nendobj\n" +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter 2 0 R") +
			"25 0 obj\n<< /Length 99 0 R >>\nstream\nab\nendstream\nendobj\n" // probes with the first object 2
			"2 0 obj\n/ASCIIHexDecode\nendobj\n" +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 8: a container waiting for a parameter is not parsed again until that parameter is defined, so ten probes
	// after it, each after a new definition, stay within the parse budget (four times the file).
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::string(100000u, '7'), "/N 1 /First 5 /Filter 2 0 R");
		for (uint32_t n = 0; n < 10u; ++n) {
			pdf += std::to_string(50u + n) + " 0 obj\n7\nendobj\n" + std::to_string(70u + n) +
				" 0 obj\n<< /Length 99 0 R >>\nstream\nab\nendstream\nendobj\n";
		}
		pdf += "%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{1u, 0u}));
	}

	// Round 9: what a probe waits on includes references inside a resolved parameter: /Filter 2 0 R names [3 0 R], and
	// object 3 is defined after the first probe.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter 2 0 R") + "2 0 obj\n[3 0 R]\nendobj\n" +
			"25 0 obj\n<< /Length 99 0 R >>\nstream\nab\nendstream\nendobj\n" + "3 0 obj\n/ASCIIHexDecode\nendobj\n" +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 9: a container definition the table no longer holds is not probed: object 20 is a container, then a plain
	// object, so object 88's uncompressed definition (the right length) stands.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages + "88 0 obj\n" + std::to_string(fake.size()) +
			"\nendobj\n" + container(20u, "88 0 3", "/N 1 /First 5 /Filter /ASCIIHexDecode") + "20 0 obj\n<< >>\nendobj\n" +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 9: an xref row that restores a parameter object's earlier definition reaches the probe as a scanned
	// definition does: object 2 is /ASCIIHexDecode, then /ASCII85Decode; the container is probed with the second; a row
	// then restores the first, and the container is probed again with it.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		const size_t first_two = pdf.size();
		pdf += "2 0 obj\n/ASCIIHexDecode\nendobj\n2 0 obj\n/ASCII85Decode\nendobj\n" +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter 2 0 R") +
			"25 0 obj\n<< /Length 99 0 R >>\nstream\nab\nendstream\nendobj\n";
		pdf += "40 0 obj\n<< /Type /XRef /Size 89 /Root 1 0 R /Index [2 1] /W [1 4 2] /Length 7 >>\nstream\n" +
			type1_row(first_two) + "\nendstream\nendobj\n";
		pdf += "30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 10: two containers hold object 88 and the later one is replaced by a plain object 21, so the earlier
	// container's value sizes the stream (the final table decides, in the second pass).
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			container(21u, "88 0 3", "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"25 0 obj\n<< /Length 99 0 R >>\nstream\nab\nendstream\nendobj\n" + // a lookup while both containers stand
			"21 0 obj\n<< >>\nendobj\n" +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 10: a row restoring an earlier definition of container 20 restores its members, so its object 88 (not the
	// replaced definition's) sizes the stream.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		const size_t first_container = pdf.size();
		pdf += container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			container(20u, "88 0 3", "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"25 0 obj\n<< /Length 99 0 R >>\nstream\nab\nendstream\nendobj\n"; // a lookup before the row
		pdf += "40 0 obj\n<< /Type /XRef /Size 89 /Root 1 0 R /Index [20 1] /W [1 4 2] /Length 7 >>\nstream\n" +
			type1_row(first_container) + "\nendstream\nendobj\n";
		pdf += "30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 11: an uncompressed integer the scan meets first is replaced by a container's after the stream; the
	// container's value, which lands on the stream's own `endstream`, sizes it.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages + "88 0 obj\n0\nendobj\n" +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
		ASSERT_NE(rec->find(88), nullptr);
		EXPECT_TRUE(rec->find(88)->is_compressed());
	}

	// Round 11: the stream's payload defines object 88 itself (after its embedded `endstream`); the container's value
	// still sizes the stream, and the payload's definition is stream data.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n"
			"88 0 obj\n0\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
		ASSERT_NE(rec->find(88), nullptr);
		EXPECT_TRUE(rec->find(88)->is_compressed());
	}

	// Round 11: the payload redefines the container itself, so the scan's table replaces it; its integer still sizes
	// the stream, and the container stands.
	{
		const std::string fake = "\nendstream\nendobj\n20 0 obj\n<< >>\nendobj\n"
			"1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		const size_t container_at = pdf.size();
		pdf += container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
		ASSERT_NE(rec->find(20), nullptr);
		EXPECT_EQ(rec->find(20)->byte_offset, container_at);
	}

	// Round 12: the payload defines object 88 as an integer longer than the file; that impossible candidate is skipped
	// and the container's value still sizes the stream.
	{
		const std::string fake = "\nendstream\nendobj\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n"
			"88 0 obj\n9223372036854775807\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 12: the payload redefines the container's indirect /N, so the container no longer unpacks through the
	// first pass's table; read through the definition before it, it still yields the integer that sizes the stream.
	{
		const std::string fake = "\nendstream\nendobj\n2 0 obj\n0\nendobj\n"
			"1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages + "2 0 obj\n1\nendobj\n" +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 2 0 R /First 5 /Filter /ASCIIHexDecode") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
		ASSERT_NE(rec->find(88), nullptr);
		EXPECT_TRUE(rec->find(88)->is_compressed());
	}

	// Round 12: the payload redefines the container's indirect /Filter instead; the same reading recovers it.
	{
		const std::string fake = "\nendstream\nendobj\n2 0 obj\n0\nendobj\n"
			"1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages + "2 0 obj\n/ASCIIHexDecode\nendobj\n" +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter 2 0 R") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
	}

	// Round 12: a trailer inside the payload claims /Encrypt, which would leave every container unread; the
	// candidates are still read, the second scan hides that trailer, and the file is not encrypted.
	{
		const std::string fake = "\nendstream\nendobj\ntrailer\n<< /Encrypt 99 0 R >>\n"
			"1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter /ASCIIHexDecode") +
			"30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n" +
			"trailer\n<< /Root 1 0 R /Size 89 >>\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at);
		EXPECT_FALSE(rec->trailer().contains("Encrypt"));
	}

	// Round 13: the redefined parameter is nested (`/Filter [2 0 R]`), or defined after the container; every reading
	// of the container's named objects is tried, so the integer that sizes the stream is still found.
	for (const bool nested : {true, false}) {
		const std::string fake = "\nendstream\nendobj\n2 0 obj\n0\nendobj\n"
			"1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Fake true >>\nendobj\n";
		std::string pdf = "%PDF-1.7\n";
		const size_t genuine_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages;
		if (nested) {
			pdf += "2 0 obj\n/ASCIIHexDecode\nendobj\n" +
				container(20u, "88 0 " + std::to_string(fake.size()), "/N 1 /First 5 /Filter [2 0 R]");
		} else {
			pdf += container(20u, "88 0 " + std::to_string(fake.size()), "/N 2 0 R /First 5 /Filter /ASCIIHexDecode") +
				"2 0 obj\n1\nendobj\n";
		}
		pdf += "30 0 obj\n<< /Length 88 0 R >>\nstream\n" + fake + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, genuine_at) << (nested ? "nested" : "forward");
	}

	// Round 13: an obsolete definition of the length object, which an update replaced, lands exactly on a later
	// stream's `endstream`; a rescan with it would swallow the replacement catalog, but in that rescan's own table the
	// length object is still the update's value, so the longer length is inconsistent and the first pass stands.
	{
		const auto build = [&](uint64_t m, size_t& replacement_at, size_t& payload_start, size_t& y_end) {
			char obsolete[48];
			std::snprintf(obsolete, sizeof(obsolete), "88 0 obj\n%06llu\nendobj\n", static_cast<unsigned long long>(m));
			std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages + obsolete +
				"88 0 obj\n1\nendobj\n30 0 obj\n<< /Length 88 0 R >>\nstream\n";
			payload_start = pdf.size();
			pdf += "x\nendstream\nendobj\n";
			replacement_at = pdf.size();
			pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R /Replaced true >>\nendobj\n31 0 obj\n<< /Length 1 >>\nstream\n";
			y_end = pdf.size() + 1u;
			pdf += "y\nendstream\nendobj\n%%EOF\n";
			return pdf;
		};
		size_t replacement_at = 0;
		size_t payload_start = 0;
		size_t y_end = 0;
		(void)build(0u, replacement_at, payload_start, y_end);
		const std::string pdf = build(y_end - payload_start, replacement_at, payload_start, y_end);
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, replacement_at);
	}

	// Round 4: object 1 defined three times in the file (a catalog, then two other dictionaries); an intact type-1
	// row naming the first restores it, catalog and all, so root 1 is valid again.
	{
		std::string pdf = "%PDF-1.7\n";
		const size_t first_at = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n" + pages +
			"1 0 obj\n<< /Type /Bogus >>\nendobj\n1 0 obj\n<< /Type /Bogus /Again true >>\nendobj\n";
		pdf += "10 0 obj\n<< /Type /XRef /Size 11 /Root 1 0 R /Index [1 1] /W [1 4 2] /Length 7 >>\nstream\n" +
			type1_row(first_at) + "\nendstream\nendobj\n%%EOF\n";
		auto rec = XrefTable::recover_damaged_xref(as_bytes(pdf), 64);
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{1u, 0u}));
		ASSERT_NE(rec->find(1), nullptr);
		EXPECT_EQ(rec->find(1)->byte_offset, first_at);
	}
}

// Recovered cross-reference streams (D06 T05 §30): their rows count against one budget for the whole recovery, their
// decoding stops at what the cumulative budget has left, an unusable one is recorded with its object number and
// offset, and a file that may be encrypted keeps no container bytes while it is scanned.
TEST(PdfXref, RecoveryXrefStreamRowsAndBudgets)
{
	const std::string head = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
							 "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
	const auto free_rows_stream = [](uint32_t num) {
		const std::string rows(10u * 7u, '\0'); // ten type-0 rows
		return std::to_string(num) + " 0 obj\n<< /Type /XRef /Size 110 /Index [100 10] /W [1 4 2] /Length " +
			std::to_string(rows.size()) + " >>\nstream\n" + rows + "\nendstream\nendobj\n";
	};

	// Thirty rows in three streams under a 16-entry recovery: refused as a budget, not dropped stream by stream.
	std::string rows_pdf = head;
	for (uint32_t n = 10; n < 13; ++n) {
		rows_pdf += free_rows_stream(n);
	}
	rows_pdf += "%%EOF\n";
	RECTO_ASSERT_OK(XrefTable::recover_damaged_xref(as_bytes(head + free_rows_stream(10) + "%%EOF\n"), 16));
	RECTO_EXPECT_ERR(XrefTable::recover_damaged_xref(as_bytes(rows_pdf), 16), ErrorCode::DamagedFormat);

	// An xref stream that does not decode is recorded as damage at its object number and offset.
	const std::string broken = "20 0 obj\n<< /Type /XRef /Size 3 /W [1 4 2] /Filter /FlateDecode /Length 4 >>\nstream\n"
							   "\x01\x02\x03\x04\nendstream\nendobj\n";
	const std::string broken_pdf = head + broken + "%%EOF\n";
	clear_diagnostics();
	RECTO_ASSERT_OK(XrefTable::recover_damaged_xref(as_bytes(broken_pdf), 64));
	bool named = false;
	for (const auto& d : diagnostics_tail()) {
		named = named || (d.diagnostic_id == DiagnosticId::DamagedXRef && d.object_num == std::optional<uint32_t>(20u) &&
							 d.byte_offset == std::optional<uint64_t>(head.size()));
	}
	EXPECT_TRUE(named);
	clear_diagnostics();

	// Three xref streams that inflate about 400 KiB each and then fail their checksum, under the 1 MiB budget of a
	// 16-entry recovery: refused, and the decoding done never exceeds that budget.
	const auto inflating_xref = [](uint32_t num) {
		std::string body(410000u, '\0');
		std::string z = "\x78\x01";
		for (size_t at = 0; at < body.size(); at += 60000u) {
			const size_t len = std::min<size_t>(60000u, body.size() - at);
			z.push_back(at + len == body.size() ? '\x01' : '\x00');
			z.push_back(static_cast<char>(len & 0xFF));
			z.push_back(static_cast<char>((len >> 8) & 0xFF));
			z.push_back(static_cast<char>(~len & 0xFF));
			z.push_back(static_cast<char>((~len >> 8) & 0xFF));
			z.append(body, at, len);
		}
		z.append("\x00\x00\x00\x00", 4);
		return std::to_string(num) + " 0 obj\n<< /Type /XRef /Size 3 /W [1 4 2] /Filter /FlateDecode /Length " +
			std::to_string(z.size()) + " >>\nstream\n" + z + "\nendstream\nendobj\n";
	};
	std::string inflating_pdf = head;
	for (uint32_t n = 30; n < 33; ++n) {
		inflating_pdf += inflating_xref(n);
	}
	inflating_pdf += "%%EOF\n";
	{
		detail::ScopedDecodeWorkMeter work;
		RECTO_EXPECT_ERR(
			XrefTable::recover_damaged_xref(as_bytes(inflating_pdf), 16), ErrorCode::DecompressionLimitExceeded);
		EXPECT_LE(work.bytes(), uint64_t{16} * 65536u);
	}

	// Twenty 64 KiB containers in a file whose trailer has /Encrypt: none is decoded and none is kept while scanning.
	std::string encrypted = head;
	for (uint32_t n = 40; n < 60; ++n) {
		const std::string cipher(65536u, static_cast<char>(0xA5));
		encrypted += std::to_string(n) + " 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length 65536 >>\nstream\n" + cipher +
			"\nendstream\nendobj\n";
	}
	encrypted += "9 0 obj\n<< /Filter /Standard /V 1 /R 2 /O <00> /U <00> /P -4 >>\nendobj\n"
				 "trailer\n<< /Size 60 /Root 1 0 R /Encrypt 9 0 R >>\n%%EOF\n";
	{
		const test::ScopedHeapMeter meter;
		auto rec = XrefTable::recover_damaged_xref(as_bytes(encrypted), 64);
		const size_t peak = meter.peak_bytes();
		RECTO_ASSERT_OK(rec);
		EXPECT_EQ(rec->root_ref(), (IndirectRef{1u, 0u}));
		if (test::ScopedHeapMeter::available()) {
			EXPECT_LT(peak, size_t{512} << 10u);
		}
	}
}

// Recovery charges a container that inflates and then fails its checksum to the cumulative decoded budget too:
// three such containers of about 400 KiB each exceed the 1 MiB budget of a 16-entry recovery.
TEST(PdfXref, RecoveryChargesFailedObjStmDecodes)
{
	const auto failing_container = [](uint32_t num) {
		// zlib with stored deflate blocks holding "1 0 42 " and spaces, then a wrong Adler-32.
		std::string body = "1 0 42 ";
		body.append(410000u - body.size(), ' ');
		std::string z = "\x78\x01";
		for (size_t at = 0; at < body.size(); at += 60000u) {
			const size_t len = std::min<size_t>(60000u, body.size() - at);
			z.push_back(at + len == body.size() ? '\x01' : '\x00');
			z.push_back(static_cast<char>(len & 0xFF));
			z.push_back(static_cast<char>((len >> 8) & 0xFF));
			z.push_back(static_cast<char>(~len & 0xFF));
			z.push_back(static_cast<char>((~len >> 8) & 0xFF));
			z.append(body, at, len);
		}
		z.append("\x00\x00\x00\x00", 4);
		return std::to_string(num) + " 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Filter /FlateDecode /Length " +
			std::to_string(z.size()) + " >>\nstream\n" + z + "\nendstream\nendobj\n";
	};
	std::string pdf = "%PDF-1.7\n";
	pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
	pdf += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
	const std::string base = pdf + "trailer\n<< /Size 3 /Root 1 0 R >>\n%%EOF\n";
	RECTO_ASSERT_OK(XrefTable::recover_damaged_xref(as_bytes(base), 16));
	for (uint32_t n = 5; n < 8; ++n) {
		pdf += failing_container(n);
	}
	pdf += "trailer\n<< /Size 8 /Root 1 0 R >>\n%%EOF\n";
	RECTO_EXPECT_ERR(XrefTable::recover_damaged_xref(as_bytes(pdf), 16), ErrorCode::DecompressionLimitExceeded);

	// The same containers with an indirect /DecodeParms are unpacked after the scan (D06 T05 §29) and charged to the
	// same budget.
	std::string deferred = base.substr(0, base.find("trailer"));
	for (uint32_t n = 5; n < 8; ++n) {
		std::string c = failing_container(n);
		c.replace(c.find("/Filter /FlateDecode"), 20, "/Filter /FlateDecode /DecodeParms 9 0 R");
		deferred += c;
	}
	deferred += "9 0 obj\n<< >>\nendobj\ntrailer\n<< /Size 10 /Root 1 0 R >>\n%%EOF\n";
	RECTO_EXPECT_ERR(
		XrefTable::recover_damaged_xref(as_bytes(deferred), 16), ErrorCode::DecompressionLimitExceeded);

	// A container behind an image filter (JBIG2 here: a 2048 x 2048 page, then a truncated segment) is never decoded
	// by recovery, so no unmetered raster allocation happens; the catalog still recovers.
	std::string jbig2;
	const auto be32 = [&](uint32_t v) {
		for (int s = 24; s >= 0; s -= 8) {
			jbig2.push_back(static_cast<char>((v >> s) & 0xFF));
		}
	};
	be32(1);
	jbig2 += std::string("\x30\x00\x01", 3);
	be32(19);
	be32(2048);
	be32(2048);
	be32(0);
	be32(0);
	jbig2 += std::string("\x00\x00\x00\x00", 4);
	std::string image_pdf = base.substr(0, base.find("trailer"));
	for (uint32_t n = 5; n < 8; ++n) {
		image_pdf += std::to_string(n) + " 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Filter /JBIG2Decode /Length " +
			std::to_string(jbig2.size()) + " >>\nstream\n" + jbig2 + "\nendstream\nendobj\n";
	}
	image_pdf += "trailer\n<< /Size 8 /Root 1 0 R >>\n%%EOF\n";
	{
		detail::ScopedDecodeWorkMeter outer;
		auto image_rec = XrefTable::recover_damaged_xref(as_bytes(image_pdf), 16);
		RECTO_ASSERT_OK(image_rec);
		EXPECT_EQ(image_rec->root_ref(), (IndirectRef{1u, 0u}));
		EXPECT_EQ(outer.bytes(), 0u);
	}
	{
		// Decoded directly, the same payload fails after allocating its page, and the meter sees it: the zero above
		// is recovery not decoding, not the work going unmeasured.
		detail::ScopedDecodeWorkMeter direct;
		const std::vector<uint8_t> payload(jbig2.begin(), jbig2.end());
		EXPECT_FALSE(decode_jbig2(payload).has_value());
		EXPECT_GE(direct.bytes(), 2048u * 2048u / 8u);
	}
	PdfDict lossless;
	lossless.set(PdfName::from_decoded("Filter"),
		PdfValue(PdfArray{PdfValue(PdfName::from_decoded("AHx")), PdfValue(PdfName::from_decoded("FlateDecode"))}));
	EXPECT_TRUE(filters_are_metered_lossless(lossless));
	lossless.set(PdfName::from_decoded("Filter"), PdfValue(PdfName::from_decoded("DCTDecode")));
	EXPECT_FALSE(filters_are_metered_lossless(lossless));
	EXPECT_TRUE(filters_are_metered_lossless(PdfDict{}));

	// The meter itself counts a decode that fails after expanding.
	std::string one = failing_container(5);
	const size_t z_at = one.find("stream\n") + 7;
	const size_t z_end = one.rfind("\nendstream");
	const std::vector<uint8_t> z(one.begin() + static_cast<std::ptrdiff_t>(z_at),
		one.begin() + static_cast<std::ptrdiff_t>(z_end));
	detail::ScopedDecodeWorkMeter meter;
	RECTO_EXPECT_ERR(decode_flate(z), ErrorCode::DamagedFormat);
	EXPECT_GE(meter.bytes(), 410000u);
}

TEST(PdfXref, XrefStreamEntriesAboveSizeAreMissing)
{
	std::string pdf = "%PDF-1.7\n";
	const size_t off1 = pdf.size();
	pdf += "1 0 obj\n<< /Type /Catalog >>\nendobj\n";
	const size_t off3 = pdf.size();
	std::string rows;
	const auto row = [&](uint8_t type, uint32_t f2, uint8_t f3) {
		rows.push_back(static_cast<char>(type));
		rows.push_back(static_cast<char>((f2 >> 16) & 0xFF));
		rows.push_back(static_cast<char>((f2 >> 8) & 0xFF));
		rows.push_back(static_cast<char>(f2 & 0xFF));
		rows.push_back(static_cast<char>(f3));
	};
	row(0, 0, 0xFF);
	row(1, static_cast<uint32_t>(off1), 0);
	row(0, 0, 0);
	row(1, static_cast<uint32_t>(off3), 0);
	row(1, 0xFFFFFFu, 0); // object 99: far outside the file, and above /Size
	pdf += "3 0 obj\n<< /Type /XRef /Size 4 /Index [0 4 99 1] /Root 1 0 R /W [1 3 1] /Length " +
		std::to_string(rows.size()) + " >>\nstream\n" + rows + "\nendstream\nendobj\nstartxref\n" +
		std::to_string(off3) + "\n%%EOF\n";
	auto xref = XrefTable::parse(as_bytes(pdf));
	RECTO_ASSERT_OK(xref);
	EXPECT_FALSE(xref->recovered());
	EXPECT_EQ(xref->find(99), nullptr);
	EXPECT_NE(xref->find(1), nullptr);
}

TEST(PdfXref, ObjStmIndirectDecodeParmsResolve)
{
	const auto build = [](std::string_view parms_ref, std::string_view parms_body = "<< >>",
		std::string_view filter = "/ASCIIHexDecode", std::string_view hex_payload = "312030203432>") {
		const std::string payload(hex_payload); // decodes to "1 0 42": object 1 is the integer 42
		std::string pdf = "%PDF-1.7\n";
		const size_t off2 = pdf.size();
		pdf += "2 0 obj\n" + std::string(parms_body) + "\nendobj\n";
		const size_t off5 = pdf.size();
		pdf += "5 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Filter " + std::string(filter) + " /DecodeParms ";
		pdf += parms_ref;
		pdf += " /Length " + std::to_string(payload.size()) + " >>\nstream\n" + payload + "\nendstream\nendobj\n";
		const size_t off6 = pdf.size();
		std::string rows(7 * 4, '\0');
		rows[3] = '\xFF';                                     // 0: free
		rows[4] = 2;                                          // 1: in object stream 5, index 0
		rows[6] = 5;
		rows[8] = 1;                                          // 2: uncompressed
		rows[9] = static_cast<char>((off2 >> 8) & 0xFF);
		rows[10] = static_cast<char>(off2 & 0xFF);
		rows[20] = 1;                                         // 5: uncompressed
		rows[21] = static_cast<char>((off5 >> 8) & 0xFF);
		rows[22] = static_cast<char>(off5 & 0xFF);
		rows[24] = 1;                                         // 6: this xref stream
		rows[25] = static_cast<char>((off6 >> 8) & 0xFF);
		rows[26] = static_cast<char>(off6 & 0xFF);
		pdf += "6 0 obj\n<< /Type /XRef /Size 7 /Root 1 0 R /W [1 2 1] /Length " + std::to_string(rows.size()) +
			" >>\nstream\n" + rows + "\nendstream\nendobj\nstartxref\n" + std::to_string(off6) + "\n%%EOF\n";
		return pdf;
	};

	const std::string ok_pdf = build("2 0 R");
	auto xref = XrefTable::parse(as_bytes(ok_pdf));
	RECTO_ASSERT_OK(xref);
	auto value = xref->resolve_object(as_bytes(ok_pdf), IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(value);
	EXPECT_EQ(value->as_int(), std::optional<int64_t>(42));

	const std::string cyclic_pdf = build("1 0 R"); // object 1 lives in this very stream
	auto cyclic_xref = XrefTable::parse(as_bytes(cyclic_pdf));
	RECTO_ASSERT_OK(cyclic_xref);
	RECTO_EXPECT_ERR(cyclic_xref->resolve_object(as_bytes(cyclic_pdf), IndirectRef{1u, 0u}), ErrorCode::DamagedFormat);

	// A key this stage's filter does not define is never resolved, whether no filter reads it or another filter
	// would (ASCIIHex has no parameters), so even a reference back into the stream does not affect decoding.
	for (const std::string_view parms_body : {"<< /VendorData 1 0 R >>", "<< /Columns 1 0 R >>"}) {
		const std::string other_pdf = build("2 0 R", parms_body);
		auto other_xref = XrefTable::parse(as_bytes(other_pdf));
		RECTO_ASSERT_OK(other_xref);
		auto other_value = other_xref->resolve_object(as_bytes(other_pdf), IndirectRef{1u, 0u});
		RECTO_ASSERT_OK(other_value);
		EXPECT_EQ(other_value->as_int(), std::optional<int64_t>(42)) << parms_body;
	}

	// Flate with the default /Predictor 1: /Columns shapes no predictor, so it is not resolved either.
	const std::string flate_pdf = build("[null << /Columns 1 0 R >>]", "<< >>", "[/ASCIIHexDecode /FlateDecode]",
		"789C335430503031020003860108>");
	auto flate_xref = XrefTable::parse(as_bytes(flate_pdf));
	RECTO_ASSERT_OK(flate_xref);
	auto flate_value = flate_xref->resolve_object(as_bytes(flate_pdf), IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(flate_value);
	EXPECT_EQ(flate_value->as_int(), std::optional<int64_t>(42));
}

TEST(PdfXref, ClassicEntriesAboveSizeAreMissing)
{
	const auto build = [](std::string_view trailer, std::optional<size_t> seven_offset = std::nullopt) {
		std::string pdf = "%PDF-1.7\n";
		const size_t off1 = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";
		const size_t off3 = pdf.size();
		pdf += "3 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		const size_t off7 = pdf.size();
		pdf += "7 2 obj\n(GenerationTwoObject)\nendobj\n";
		const size_t xref_off = pdf.size();
		char row[64]{};
		pdf += "xref\n0 2\n0000000000 65535 f \n";
		std::snprintf(row, sizeof(row), "%010zu 00000 n \n", off1);
		pdf += row;
		pdf += "3 1\n";
		std::snprintf(row, sizeof(row), "%010zu 00000 n \n", off3);
		pdf += row;
		pdf += "7 1\n";
		std::snprintf(row, sizeof(row), "%010zu 00002 n \n", seven_offset.value_or(off7));
		pdf += row;
		pdf += "trailer\n";
		pdf += trailer;
		pdf += "\nstartxref\n" + std::to_string(xref_off) + "\n%%EOF\n";
		return pdf;
	};

	{
		const std::string pdf = build("<< /Size 4 /Root 1 0 R >>");
		auto xref = XrefTable::parse(as_bytes(pdf));
		RECTO_ASSERT_OK(xref);
		EXPECT_NE(xref->find(3), nullptr);
		EXPECT_EQ(xref->find(7), nullptr); // 7 > /Size 4: missing
		EXPECT_FALSE(xref->resolve_object(as_bytes(pdf), IndirectRef{7u, 2u}).has_value() &&
			xref->resolve_object(as_bytes(pdf), IndirectRef{7u, 2u})->is_string());
	}
	{
		const std::string pdf = build("<< /Size 7 /Root 1 0 R >>"); // 7 is not greater than 7: kept
		auto xref = XrefTable::parse(as_bytes(pdf));
		RECTO_ASSERT_OK(xref);
		EXPECT_NE(xref->find(7), nullptr);
	}
	{
		const std::string pdf = build("<< /Root 1 0 R >>"); // /Size missing
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf)), ErrorCode::DamagedFormat);
	}
	{
		// A row above /Size is missing, so an offset outside the file in it cannot fail the parse...
		const std::string pdf = build("<< /Size 4 /Root 1 0 R >>", size_t{9999999999ULL});
		auto xref = XrefTable::parse(as_bytes(pdf));
		RECTO_ASSERT_OK(xref);
		EXPECT_EQ(xref->find(7), nullptr);
		EXPECT_FALSE(xref->recovered());
	}
	{
		// ...while one within /Size still does.
		const std::string pdf = build("<< /Size 8 /Root 1 0 R >>", size_t{9999999999ULL});
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf)), ErrorCode::DamagedFormat);
	}
}

TEST(PdfXref, ClassicSimple1pAndMultiSubsection)
{
	// 1. Standard simple-1p.pdf with 0-byte preamble.
	verify_simple_1p_with_preamble({});

	// 2. Multi-subsection classic xref table with mixed \r\n, " \n", and single-space rows.
	std::string pdf = "%PDF-1.7\n";
	const size_t off1 = pdf.size();
	pdf += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";
	const size_t off3 = pdf.size();
	pdf += "3 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
	const size_t off7 = pdf.size();
	pdf += "7 2 obj\n(GenerationTwoObject)\nendobj\n";

	const size_t xref_off = pdf.size();
	char row_buf[64]{};
	pdf += "xref\r\n";
	pdf += "0 2\r\n";
	pdf += "0000000000 65535 f\r\n";
	std::snprintf(row_buf, sizeof(row_buf), "%010zu 00000 n \n", off1);
	pdf += row_buf;
	pdf += "3 1\n";
	// Non-conformant single-space variation without 10-digit zero padding.
	pdf += std::to_string(off3) + " 0 n\n";
	pdf += "7 2\r\n";
	std::snprintf(row_buf, sizeof(row_buf), "%010zu 00002 n\r\n", off7);
	pdf += row_buf;
	pdf += "0000000000 00001 f \n";
	pdf += "trailer\n<< /Size 9 /Root 1 0 R >>\n";
	pdf += "startxref\n" + std::to_string(xref_off) + "\n%%EOF\n   \t\r\n";

	auto xref_res = XrefTable::parse(as_bytes(pdf));
	RECTO_ASSERT_OK(xref_res);
	const XrefTable& xref = *xref_res;

	ASSERT_NE(xref.find(0), nullptr);
	EXPECT_TRUE(xref.find(0)->is_free());
	ASSERT_NE(xref.find(1), nullptr);
	EXPECT_TRUE(xref.find(1)->is_uncompressed());
	EXPECT_EQ(xref.find(1)->byte_offset, off1);
	EXPECT_EQ(xref.find(2), nullptr);
	ASSERT_NE(xref.find(3), nullptr);
	EXPECT_TRUE(xref.find(3)->is_uncompressed());
	EXPECT_EQ(xref.find(3)->byte_offset, off3);
	ASSERT_NE(xref.find(7), nullptr);
	EXPECT_TRUE(xref.find(7)->is_uncompressed());
	EXPECT_EQ(xref.find(7)->generation, 2u);
	ASSERT_NE(xref.find(8), nullptr);
	EXPECT_TRUE(xref.find(8)->is_free());

	auto obj7 = xref.resolve_object(as_bytes(pdf), IndirectRef{7u, 2u});
	RECTO_ASSERT_OK(obj7);
	ASSERT_NE(obj7->as_string(), nullptr);
	EXPECT_EQ(obj7->as_string()->as_byte_view(), "GenerationTwoObject");

	// Reference with mismatched generation or to a free object resolves to PdfNull per ISO 32000-1 7.3.10.
	auto obj7_wrong_gen = xref.resolve_object(as_bytes(pdf), IndirectRef{7u, 0u});
	RECTO_ASSERT_OK(obj7_wrong_gen);
	EXPECT_TRUE(obj7_wrong_gen->is_null());

	auto obj8_free = xref.resolve_object(as_bytes(pdf), IndirectRef{8u, 1u});
	RECTO_ASSERT_OK(obj8_free);
	EXPECT_TRUE(obj8_free->is_null());
}

TEST(PdfXref, HeaderOffsetAdjustment)
{
	// Prepending 32 header junk bytes to simple-1p.pdf must adjust all xref offsets
	// automatically and resolve every object 1..6.
	constexpr std::string_view kPreamble32 = "0123456789ABCDEF0123456789ABCDEF";
	static_assert(kPreamble32.size() == 32);
	verify_simple_1p_with_preamble(as_bytes(kPreamble32));
}

TEST(PdfXref, ClassicHeaderOffsetAdjustment)
{
	// 1. Standard 32-byte preamble on simple-1p.pdf.
	constexpr std::string_view kPreamble32 = "JUNK_PREAMBLE_32_BYTES_PADDING!\n";
	static_assert(kPreamble32.size() == 32);
	verify_simple_1p_with_preamble(as_bytes(kPreamble32));

	// 2. Preamble containing an invalid %PDF- prefix followed by a valid %PDF-1.7 header.
	constexpr std::string_view kFalsePrefixPreamble = "JUNK %PDF-invalid\n%PDF-2.9\n";
	verify_simple_1p_with_preamble(as_bytes(kFalsePrefixPreamble));

	// 3. Valid %PDF-2.0 header succeeds, while %PDF-2.1 or %PDF-1.7x without a valid header fails.
	{
		std::string ver;
		auto ok20 = XrefTable::locate_header_offset(as_bytes("PRE%PDF-2.0\n"), &ver);
		RECTO_ASSERT_OK(ok20);
		EXPECT_EQ(*ok20, 3u);
		EXPECT_EQ(ver, "2.0");

		RECTO_EXPECT_ERR(
			XrefTable::locate_header_offset(as_bytes("%PDF-2.1\n")),
			ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(
			XrefTable::locate_header_offset(as_bytes("%PDF-1.70\n")),
			ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(
			XrefTable::locate_header_offset(as_bytes("NO_HEADER_HERE")),
			ErrorCode::NotAPdf);
	}

	// 4. Preamble combined with /Prev incremental revisions (render/incremental-2rev.pdf).
	{
		const auto inc_bytes = test::read_file_bytes(
			test::corpus_path("render/incremental-2rev.pdf"));
		ASSERT_FALSE(inc_bytes.empty());

		std::vector<uint8_t> buf;
		buf.insert(buf.end(), kPreamble32.begin(), kPreamble32.end());
		buf.insert(buf.end(), inc_bytes.begin(), inc_bytes.end());

		auto xref_res = XrefTable::parse(buf);
		RECTO_ASSERT_OK(xref_res);
		EXPECT_EQ(xref_res->header_offset(), 32u);
		EXPECT_EQ(xref_res->revision_count(), 2u);

		auto info_val = xref_res->resolve_object(buf, IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(info_val);
		ASSERT_TRUE(info_val->is_dict());
		ASSERT_NE(info_val->as_dict()->get_string("Title"), nullptr);
		EXPECT_EQ(
			info_val->as_dict()->get_string("Title")->as_byte_view(),
			"Incremental Revision 2");

		auto cat_val = xref_res->resolve_object(buf, IndirectRef{1u, 0u});
		RECTO_ASSERT_OK(cat_val);
		EXPECT_EQ(cat_val->as_dict()->get_name("Type"), "Catalog");
	}
}

TEST(PdfXref, IncrementalTwoRevisions)
{
	// 1. Corpus fixture render/incremental-2rev.pdf.
	const auto bytes = test::read_file_bytes(
		test::corpus_path("render/incremental-2rev.pdf"));
	ASSERT_FALSE(bytes.empty());

	auto xref_res = XrefTable::parse(bytes);
	RECTO_ASSERT_OK(xref_res);
	const XrefTable& xref = *xref_res;

	EXPECT_EQ(xref.revision_count(), 2u);
	EXPECT_EQ(xref.root_ref(), (IndirectRef{1u, 0u}));
	EXPECT_EQ(xref.info_ref(), (IndirectRef{6u, 0u}));
	EXPECT_EQ(xref.trailer().get_int("Size"), 8);

	// Object 6 (/Info) was updated in revision 2 to "Incremental Revision 2".
	auto info_val = xref.resolve_object(bytes, IndirectRef{6u, 0u});
	RECTO_ASSERT_OK(info_val);
	ASSERT_TRUE(info_val->is_dict());
	ASSERT_NE(info_val->as_dict()->get_string("Title"), nullptr);
	EXPECT_EQ(
		info_val->as_dict()->get_string("Title")->as_byte_view(),
		"Incremental Revision 2");
	EXPECT_TRUE(info_val->as_dict()->contains("ModDate"));

	// Object 3 (/Page) was updated in revision 2 so /Contents is [4 0 R 7 0 R].
	auto page_val = xref.resolve_object(bytes, IndirectRef{3u, 0u});
	RECTO_ASSERT_OK(page_val);
	ASSERT_TRUE(page_val->is_dict());
	const PdfArray* contents = page_val->as_dict()->get_array("Contents");
	ASSERT_NE(contents, nullptr);
	ASSERT_EQ(contents->size(), 2u);
	EXPECT_EQ((*contents)[0].as_ref(), (IndirectRef{4u, 0u}));
	EXPECT_EQ((*contents)[1].as_ref(), (IndirectRef{7u, 0u}));

	// Untouched objects from revision 1 (1, 2, 4, 5) and new object 7 from revision 2 all resolve.
	auto cat_val = xref.resolve_object(bytes, IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(cat_val);
	EXPECT_EQ(cat_val->as_dict()->get_name("Type"), "Catalog");

	auto stm4_val = xref.resolve_object(bytes, IndirectRef{4u, 0u});
	RECTO_ASSERT_OK(stm4_val);
	EXPECT_TRUE(stm4_val->is_stream());

	auto stm7_val = xref.resolve_object(bytes, IndirectRef{7u, 0u});
	RECTO_ASSERT_OK(stm7_val);
	EXPECT_TRUE(stm7_val->is_stream());

	// 2. Trailer merging fallback & newest-revision precedence for /Root, /Info, /Encrypt, /ID, /Size,
	// plus incremental free ('f') entry overriding an older live ('n') entry.
	{
		std::string pdf_base = "%PDF-1.7\n";
		const size_t off1 = pdf_base.size();
		pdf_base += "1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t off2 = pdf_base.size();
		pdf_base += "2 0 obj\n(LiveInRev1)\nendobj\n";
		const size_t off3 = pdf_base.size();
		pdf_base += "3 0 obj\n(UntouchedRev1)\nendobj\n";

		const size_t xref1_off = pdf_base.size();
		pdf_base += "xref\n0 4\n0000000000 65535 f \n";
		pdf_base += std::to_string(off1) + " 0 n\n";
		pdf_base += std::to_string(off2) + " 0 n\n";
		pdf_base += std::to_string(off3) + " 0 n\n";
		pdf_base += "trailer\n<< /Size 4 /Root 1 0 R /Info 10 0 R /Encrypt 11 0 R /ID [(id1a) (id1b)] >>\n";
		pdf_base += "startxref\n" + std::to_string(xref1_off) + "\n%%EOF\n";

		// Case A: Revision 2 omits /Info, /Encrypt, /ID and frees object 2 (0000000000 00001 f).
		std::string pdf_fallback = pdf_base;
		const size_t xref2_a = pdf_fallback.size();
		pdf_fallback += "xref\n2 1\n0000000000 00001 f \n";
		pdf_fallback += "trailer\n<< /Size 5 /Root 1 0 R /Prev " + std::to_string(xref1_off) + " >>\n";
		pdf_fallback += "startxref\n" + std::to_string(xref2_a) + "\n%%EOF\n";

		auto res_a = XrefTable::parse(as_bytes(pdf_fallback));
		RECTO_ASSERT_OK(res_a);
		EXPECT_EQ(res_a->revision_count(), 2u);
		EXPECT_EQ(res_a->root_ref(), (IndirectRef{1u, 0u}));
		EXPECT_EQ(res_a->info_ref(), (IndirectRef{10u, 0u}));
		EXPECT_EQ(res_a->encrypt_ref(), (IndirectRef{11u, 0u}));
		EXPECT_EQ(res_a->trailer().get_int("Size"), 5);
		const PdfArray* id_arr_a = res_a->trailer().get_array("ID");
		ASSERT_NE(id_arr_a, nullptr);
		ASSERT_EQ(id_arr_a->size(), 2u);
		EXPECT_EQ((*id_arr_a)[0].as_string()->as_byte_view(), "id1a");

		// Object 2 was marked free in revision 2 and must resolve to PdfNull.
		ASSERT_NE(res_a->find(2), nullptr);
		EXPECT_TRUE(res_a->find(2)->is_free());
		auto resolved_2 = res_a->resolve_object(as_bytes(pdf_fallback), IndirectRef{2u, 0u});
		RECTO_ASSERT_OK(resolved_2);
		EXPECT_TRUE(resolved_2->is_null());

		// Object 3 from revision 1 remains live.
		auto resolved_3 = res_a->resolve_object(as_bytes(pdf_fallback), IndirectRef{3u, 0u});
		RECTO_ASSERT_OK(resolved_3);
		EXPECT_EQ(resolved_3->as_string()->as_byte_view(), "UntouchedRev1");

		// Case B: Revision 2 overrides /Root, /Info, /Encrypt, /ID, and /Size.
		std::string pdf_override = pdf_base;
		const size_t off4 = pdf_override.size();
		pdf_override += "4 0 obj\n<< /Type /Catalog /Version /2.0 >>\nendobj\n";
		const size_t xref2_b = pdf_override.size();
		pdf_override += "xref\n4 1\n" + std::to_string(off4) + " 0 n\n";
		pdf_override +=
			"trailer\n<< /Size 6 /Root 4 0 R /Info 20 0 R /Encrypt 21 0 R /ID [(id2a) (id2b)] /Prev " +
			std::to_string(xref1_off) + " >>\n";
		pdf_override += "startxref\n" + std::to_string(xref2_b) + "\n%%EOF\n";

		auto res_b = XrefTable::parse(as_bytes(pdf_override));
		RECTO_ASSERT_OK(res_b);
		EXPECT_EQ(res_b->root_ref(), (IndirectRef{4u, 0u}));
		EXPECT_EQ(res_b->info_ref(), (IndirectRef{20u, 0u}));
		EXPECT_EQ(res_b->encrypt_ref(), (IndirectRef{21u, 0u}));
		EXPECT_EQ(res_b->trailer().get_int("Size"), 6);
		const PdfArray* id_arr_b = res_b->trailer().get_array("ID");
		ASSERT_NE(id_arr_b, nullptr);
		ASSERT_EQ(id_arr_b->size(), 2u);
		EXPECT_EQ((*id_arr_b)[0].as_string()->as_byte_view(), "id2a");
	}
}

TEST(PdfXref, CyclicPrevAndRevisionCeiling)
{
	// 1. Corpus fixture broken/cyclic-xref-prev.pdf must fail immediately with DamagedFormat.
	{
		clear_diagnostics();
		const auto bytes = test::read_file_bytes(
			test::corpus_path("broken/cyclic-xref-prev.pdf"));
		ASSERT_FALSE(bytes.empty());

		auto res = XrefTable::parse(bytes);
		RECTO_EXPECT_ERR(res, ErrorCode::DamagedFormat);

		const auto diags = diagnostics_tail();
		ASSERT_FALSE(diags.empty());
		EXPECT_EQ(diags.back().diagnostic_id, DiagnosticId::DamagedXRef);
	}

	// 2. Mutual 2-node /Prev cycle (xref A -> /Prev B -> /Prev A).
	{
		std::string pdf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t xref_a = pdf.size();
		// Placeholder for B offset (padded to 6 digits so offsets stay fixed).
		const std::string xref_a_prefix =
			"xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 2 /Root 1 0 R /Prev ";
		const size_t xref_b = xref_a + xref_a_prefix.size() + 6 + 4; // " >>\n"
		char buf_b[16]{};
		std::snprintf(buf_b, sizeof(buf_b), "%06zu", xref_b);
		pdf += xref_a_prefix + buf_b + " >>\n";
		ASSERT_EQ(pdf.size(), xref_b);
		pdf += "xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 2 /Root 1 0 R /Prev " +
			std::to_string(xref_a) + " >>\nstartxref\n" + std::to_string(xref_b) + "\n%%EOF\n";

		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf)), ErrorCode::DamagedFormat);
	}

	// 3. Revision chain ceiling: 256 revisions succeed, 257 revisions fail with DamagedFormat.
	auto build_n_revisions = [](size_t num_revs) -> std::string {
		std::string pdf = "%PDF-1.7\n";
		const size_t obj1_off = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

		size_t prev_off = 0;
		char row1[32]{};
		std::snprintf(row1, sizeof(row1), "%010zu 00000 n \n", obj1_off);

		for (size_t r = 0; r < num_revs; ++r) {
			const size_t cur_xref = pdf.size();
			pdf += "xref\n0 2\n0000000000 65535 f \n";
			pdf += row1;
			pdf += "trailer\n<< /Size 2 /Root 1 0 R";
			if (r > 0) {
				pdf += " /Prev " + std::to_string(prev_off);
			}
			pdf += " >>\n";
			prev_off = cur_xref;
		}
		pdf += "startxref\n" + std::to_string(prev_off) + "\n%%EOF\n";
		return pdf;
	};

	{
		const std::string pdf_256 = build_n_revisions(256);
		auto res_256 = XrefTable::parse(as_bytes(pdf_256));
		RECTO_ASSERT_OK(res_256);
		EXPECT_EQ(res_256->revision_count(), 256u);
	}

	{
		const std::string pdf_257 = build_n_revisions(257);
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf_257)), ErrorCode::DamagedFormat);
	}

	// 4. Hybrid /XRefStm guards and valid hybrid /XRefStm unpacking (ISO 32000-1 7.5.8.4).
	{
		std::string pdf_base = "%PDF-1.7\n";
		const size_t obj1_off = pdf_base.size();
		pdf_base += "1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t stm_off = pdf_base.size();
		pdf_base += "2 0 obj\n<< /Type /XRef /Size 3 >>\nendobj\n";
		const size_t xref_off = pdf_base.size();

		// Cyclic /XRefStm pointing to current_xref_offset -> DamagedFormat.
		std::string pdf_cyclic_stm = pdf_base +
			"xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 3 /Root 1 0 R /XRefStm " +
			std::to_string(xref_off) + " >>\nstartxref\n" + std::to_string(xref_off) + "\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf_cyclic_stm)), ErrorCode::DamagedFormat);

		// Out-of-bounds /XRefStm -> DamagedFormat.
		std::string pdf_oob_stm = pdf_base +
			"xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 3 /Root 1 0 R /XRefStm 999999 >>\nstartxref\n" +
			std::to_string(xref_off) + "\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf_oob_stm)), ErrorCode::DamagedFormat);

		// Non-integer /XRefStm -> DamagedFormat.
		std::string pdf_bad_stm = pdf_base +
			"xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 3 /Root 1 0 R /XRefStm (bad) >>\nstartxref\n" +
			std::to_string(xref_off) + "\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf_bad_stm)), ErrorCode::DamagedFormat);

		// /XRefStm pointing to a non-stream indirect object -> DamagedFormat.
		std::string pdf_non_stream_stm = pdf_base +
			"xref\n0 2\n0000000000 65535 f \n" + std::to_string(obj1_off) +
			" 0 n\ntrailer\n<< /Size 3 /Root 1 0 R /XRefStm " + std::to_string(stm_off) +
			" >>\nstartxref\n" + std::to_string(xref_off) + "\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf_non_stream_stm)), ErrorCode::DamagedFormat);
	}

	// 5. Cumulative allocation ceiling across subsections and across /Prev revisions,
	// plus malformed numeric boundaries.
	{
		// 5a. Valid multi-subsection PDF with 2 subsections of 2 entries each (4 rows total):
		// succeeds with max_total_entries = 4, fails with max_total_entries = 3 (whereas each
		// subsection individually has 2 <= 3 rows, so removing cumulative tracking would pass).
		std::string pdf_valid_2sub = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t xref_2sub = pdf_valid_2sub.size();
		pdf_valid_2sub += "xref\n0 2\n0000000000 65535 f \n0000000009 00000 n \n";
		pdf_valid_2sub += "5 2\n0000000000 00001 f \n0000000000 00001 f \n";
		pdf_valid_2sub += "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n" +
			std::to_string(xref_2sub) + "\n%%EOF\n";

		RECTO_ASSERT_OK(XrefTable::parse(as_bytes(pdf_valid_2sub), 4));
		auto err_2sub = XrefTable::parse(as_bytes(pdf_valid_2sub), 3);
		RECTO_EXPECT_ERR(err_2sub, ErrorCode::DamagedFormat);
		EXPECT_EQ(
			err_2sub.error().message,
			"Cumulative xref entry count across subsections exceeds maximum budget");

		// 5b. Valid 2-revision /Prev PDF (render/incremental-2rev.pdf: 4 rows in rev 2 + 7 rows in rev 1 = 11 rows):
		// succeeds with max_total_entries = 11, fails with max_total_entries = 10 (whereas each revision
		// individually has <= 7 <= 10 rows, so resetting total_rows_parsed per revision would pass).
		const auto inc_bytes = test::read_file_bytes(
			test::corpus_path("render/incremental-2rev.pdf"));
		ASSERT_FALSE(inc_bytes.empty());
		RECTO_ASSERT_OK(XrefTable::parse(inc_bytes, 11));
		auto err_inc_budget = XrefTable::parse(inc_bytes, 10);
		RECTO_EXPECT_ERR(err_inc_budget, ErrorCode::DamagedFormat);
		EXPECT_EQ(
			err_inc_budget.error().message,
			"Cumulative xref entry count across subsections exceeds maximum budget");

		// 5c. Default kMaxXrefTotalEntries (1,000,000) ceiling check across subsections:
		// 1 row in subsection 0 + 1,000,000 in subsection 1 = 1,000,001 > kMaxXrefTotalEntries.
		std::string pdf_cum = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t xref_cum = pdf_cum.size();
		pdf_cum += "xref\n0 1\n0000000000 65535 f \n1 1000000\n0000000009 00000 n \n";
		pdf_cum += "trailer\n<< /Size 1000001 /Root 1 0 R >>\nstartxref\n" +
			std::to_string(xref_cum) + "\n%%EOF\n";
		auto err_cum = XrefTable::parse(as_bytes(pdf_cum));
		RECTO_EXPECT_ERR(err_cum, ErrorCode::DamagedFormat);
		EXPECT_EQ(
			err_cum.error().message,
			"Cumulative xref entry count across subsections exceeds maximum budget");

		// 5d. Malformed numeric boundaries: 32-bit object ID overflow (4294967295 + 2) and generation > 65535.
		std::string pdf_id_ovf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t xref_ovf = pdf_id_ovf.size();
		pdf_id_ovf += "xref\n4294967295 2\n0000000000 65535 f \n0000000000 65535 f \n";
		pdf_id_ovf += "trailer\n<< /Size 2 /Root 1 0 R >>\nstartxref\n" +
			std::to_string(xref_ovf) + "\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf_id_ovf)), ErrorCode::DamagedFormat);

		std::string pdf_gen_ovf = "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t xref_gen = pdf_gen_ovf.size();
		pdf_gen_ovf += "xref\n0 1\n0000000000 65536 f \n";
		pdf_gen_ovf += "trailer\n<< /Size 1 /Root 1 0 R >>\nstartxref\n" +
			std::to_string(xref_gen) + "\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf_gen_ovf)), ErrorCode::DamagedFormat);
	}
}

TEST(PdfXref, CyclicIndirectRef)
{
	// 1. 2-node indirect reference cycle: 1 0 obj -> 2 0 R, 2 0 obj -> 1 0 R,
	// plus 3 0 obj -> 3 0 R self-cycle, plus 4 0 obj stream with cyclic /Length 1 0 R.
	{
		std::string pdf = "%PDF-1.7\n";
		const size_t off1 = pdf.size();
		pdf += "1 0 obj\n2 0 R\nendobj\n";
		const size_t off2 = pdf.size();
		pdf += "2 0 obj\n1 0 R\nendobj\n";
		const size_t off3 = pdf.size();
		pdf += "3 0 obj\n3 0 R\nendobj\n";
		const size_t off4 = pdf.size();
		pdf += "4 0 obj\n<< /Length 1 0 R >>\nstream\nhello\nendstream\nendobj\n";

		const size_t xref_off = pdf.size();
		pdf += "xref\n0 5\n0000000000 65535 f \n";
		pdf += std::to_string(off1) + " 0 n\n";
		pdf += std::to_string(off2) + " 0 n\n";
		pdf += std::to_string(off3) + " 0 n\n";
		pdf += std::to_string(off4) + " 0 n\n";
		pdf += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" +
			std::to_string(xref_off) + "\n%%EOF\n";

		auto xref_res = XrefTable::parse(as_bytes(pdf));
		RECTO_ASSERT_OK(xref_res);
		const XrefTable& xref = *xref_res;

		RECTO_EXPECT_ERR(
			xref.resolve_object(as_bytes(pdf), IndirectRef{1u, 0u}),
			ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(
			xref.resolve_object(as_bytes(pdf), IndirectRef{2u, 0u}),
			ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(
			xref.resolve_object(as_bytes(pdf), IndirectRef{3u, 0u}),
			ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(
			xref.resolve_object(as_bytes(pdf), IndirectRef{4u, 0u}),
			ErrorCode::DamagedFormat);
	}

	// 2. Indirect reference chain depth ceiling: 32 hops succeed, 33 hops fail with DamagedFormat.
	{
		std::string pdf = "%PDF-1.7\n";
		std::vector<size_t> offs(35, 0);
		for (uint32_t id = 1; id <= 33; ++id) {
			offs[id] = pdf.size();
			if (id < 33) {
				pdf += std::to_string(id) + " 0 obj\n" + std::to_string(id + 1) + " 0 R\nendobj\n";
			} else {
				pdf += "33 0 obj\n(TerminalValue)\nendobj\n";
			}
		}

		const size_t xref_off = pdf.size();
		pdf += "xref\n0 34\n0000000000 65535 f \n";
		for (uint32_t id = 1; id <= 33; ++id) {
			pdf += std::to_string(offs[id]) + " 0 n\n";
		}
		pdf += "trailer\n<< /Size 34 /Root 1 0 R >>\nstartxref\n" +
			std::to_string(xref_off) + "\n%%EOF\n";

		auto xref_res = XrefTable::parse(as_bytes(pdf));
		RECTO_ASSERT_OK(xref_res);
		const XrefTable& xref = *xref_res;

		// Starting at object 2 visits 32 objects (2..33) -> succeeds.
		auto ok_32 = xref.resolve_object(as_bytes(pdf), IndirectRef{2u, 0u});
		RECTO_ASSERT_OK(ok_32);
		ASSERT_NE(ok_32->as_string(), nullptr);
		EXPECT_EQ(ok_32->as_string()->as_byte_view(), "TerminalValue");

		// Starting at object 1 visits 33 objects (1..33) -> exceeds depth 32 -> DamagedFormat.
		RECTO_EXPECT_ERR(
			xref.resolve_object(as_bytes(pdf), IndirectRef{1u, 0u}),
			ErrorCode::DamagedFormat);
	}
}

TEST(PdfXref, XrefStreamUnpack)
{
	auto parse_dict_str = [](std::string_view src) -> PdfDict {
		Parser p(as_bytes(src));
		auto res = p.parse_dictionary();
		EXPECT_TRUE(res.has_value()) << res.error().message.view();
		return res.has_value() ? std::move(*res) : PdfDict{};
	};

	// 1. Multi-subsection /Index [0 2 5 2 10 1] with /W [1 4 2] (7 bytes per row):
	//    - obj 0: type 0 (free), next=0, gen=65535
	//    - obj 1: type 1 (uncompressed), offset=0x100, gen=3
	//    - obj 5: type 2 (compressed), obj_stm=9, index=4
	//    - obj 6: type 0 (free), next=8, gen=2
	//    - obj 10: type 3 (unknown future type -> treated as free per ISO 32000-1 7.5.8.3)
	{
		const PdfDict dict = parse_dict_str(
			"<< /Type /XRef /Size 11 /Root 1 3 R /Info 5 0 R "
			"/W [1 4 2] /Index [0 2 5 2 10 1] >>");

		const uint8_t payload[] = {
			// obj 0: type 0, next 0, gen 65535
			0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
			// obj 1: type 1, offset 0x00000100 (256), gen 3
			0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x03,
			// obj 5: type 2, obj_stm 9, index 4
			0x02, 0x00, 0x00, 0x00, 0x09, 0x00, 0x04,
			// obj 6: type 0, next 8, gen 2
			0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x02,
			// obj 10: type 3 (unknown), f2=123, f3=45
			0x03, 0x00, 0x00, 0x00, 0x7B, 0x00, 0x2D,
		};

		XrefTable table;
		uint64_t total_rows = 0;
		RECTO_ASSERT_OK(XrefTable::unpack_xref_stream(
			dict,
			payload,
			/*file_size=*/4096,
			/*header_offset=*/16,
			table,
			total_rows));
		EXPECT_EQ(total_rows, 5u);
		EXPECT_EQ(table.root_ref(), (IndirectRef{1u, 3u}));
		EXPECT_EQ(table.info_ref(), (IndirectRef{5u, 0u}));

		ASSERT_NE(table.find(0), nullptr);
		EXPECT_TRUE(table.find(0)->is_free());
		EXPECT_EQ(table.find(0)->generation, 65535u);

		ASSERT_NE(table.find(1), nullptr);
		EXPECT_TRUE(table.find(1)->is_uncompressed());
		EXPECT_EQ(table.find(1)->byte_offset, 256u + 16u);
		EXPECT_EQ(table.find(1)->generation, 3u);

		EXPECT_EQ(table.find(2), nullptr);
		EXPECT_EQ(table.find(3), nullptr);
		EXPECT_EQ(table.find(4), nullptr);

		ASSERT_NE(table.find(5), nullptr);
		EXPECT_TRUE(table.find(5)->is_compressed());
		EXPECT_EQ(table.find(5)->obj_stm_num, 9u);
		EXPECT_EQ(table.find(5)->obj_stm_index, 4u);
		EXPECT_EQ(table.find(5)->generation, 0u);

		ASSERT_NE(table.find(6), nullptr);
		EXPECT_TRUE(table.find(6)->is_free());
		EXPECT_EQ(table.find(6)->byte_offset, 8u);
		EXPECT_EQ(table.find(6)->generation, 2u);

		ASSERT_NE(table.find(10), nullptr);
		EXPECT_TRUE(table.find(10)->is_free());
	}

	// 2. Implicit type 1 when w1 == 0 (/W [0 8 0] -> 8-byte offset, default type 1, default gen 0)
	//    and default /Index [0 /Size].
	{
		const PdfDict dict = parse_dict_str(
			"<< /Type /XRef /Size 3 /Root 1 0 R /W [0 8 0] >>");

		const uint8_t payload[] = {
			// obj 0 (implicit type 1 on obj 0 is treated as free entry 0)
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			// obj 1: 8-byte big-endian offset 0x0000000000000240 (576), implicit gen 0
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40,
			// obj 2: 8-byte big-endian offset 0x0000000000000480 (1152), implicit gen 0
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x80,
		};

		XrefTable table;
		uint64_t total_rows = 0;
		RECTO_ASSERT_OK(XrefTable::unpack_xref_stream(
			dict,
			payload,
			/*file_size=*/4096,
			/*header_offset=*/0,
			table,
			total_rows));
		EXPECT_EQ(total_rows, 3u);

		ASSERT_NE(table.find(0), nullptr);
		EXPECT_TRUE(table.find(0)->is_free());

		ASSERT_NE(table.find(1), nullptr);
		EXPECT_TRUE(table.find(1)->is_uncompressed());
		EXPECT_EQ(table.find(1)->byte_offset, 0x240u);
		EXPECT_EQ(table.find(1)->generation, 0u);

		ASSERT_NE(table.find(2), nullptr);
		EXPECT_TRUE(table.find(2)->is_uncompressed());
		EXPECT_EQ(table.find(2)->byte_offset, 0x480u);
		EXPECT_EQ(table.find(2)->generation, 0u);
	}

	// 3. Truncated /Type /XRef stream payload shorter than sum(count_i) * (w1 + w2 + w3) -> DamagedFormat.
	{
		const PdfDict dict = parse_dict_str(
			"<< /Type /XRef /Size 4 /Root 1 0 R /W [1 2 2] /Index [0 2 3 2] >>");
		// sum(count_i) = 4, w1 + w2 + w3 = 5 -> requires 20 bytes; supply 19 bytes.
		const std::vector<uint8_t> short_payload(19, 0u);
		XrefTable table;
		uint64_t total_rows = 0;
		RECTO_EXPECT_ERR(
			XrefTable::unpack_xref_stream(dict, short_payload, 4096, 0, table, total_rows),
			ErrorCode::DamagedFormat);

		// Also verify full XrefTable::parse rejects a PDF whose /Type /XRef stream is truncated.
		std::string pdf = "%PDF-1.6\n";
		const size_t off1 = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog >>\nendobj\n";
		const size_t xref_off = pdf.size();
		// /Size 3 with /W [1 2 1] requires 3 * 4 = 12 bytes, but stream has only 8 bytes.
		std::string raw_8(8, '\0');
		raw_8[4] = 1;
		raw_8[6] = static_cast<char>(off1);
		pdf += "2 0 obj\n<< /Type /XRef /Size 3 /Root 1 0 R /W [1 2 1] /Length 8 >>\nstream\n";
		pdf += raw_8;
		pdf += "\nendstream\nendobj\nstartxref\n" + std::to_string(xref_off) + "\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::parse(as_bytes(pdf)), ErrorCode::DamagedFormat);
	}

	// 4. Malformed /Index arrays (odd element count, empty, negative first ID, negative count,
	//    non-integer element, 32-bit overflow) and invalid /W arrays -> DamagedFormat.
	{
		const std::vector<uint8_t> valid_buf(64, 0u);
		const char* bad_dicts[] = {
			// Odd /Index element count
			"<< /Type /XRef /Size 4 /Root 1 0 R /W [1 2 1] /Index [0 2 3] >>",
			// Empty /Index
			"<< /Type /XRef /Size 4 /Root 1 0 R /W [1 2 1] /Index [] >>",
			// Negative first ID in /Index
			"<< /Type /XRef /Size 4 /Root 1 0 R /W [1 2 1] /Index [-1 2] >>",
			// Negative count in /Index
			"<< /Type /XRef /Size 4 /Root 1 0 R /W [1 2 1] /Index [0 -2] >>",
			// Non-integer in /Index
			"<< /Type /XRef /Size 4 /Root 1 0 R /W [1 2 1] /Index [0 (two)] >>",
			// 32-bit object number overflow in /Index
			"<< /Type /XRef /Size 4 /Root 1 0 R /W [1 2 1] /Index [4294967295 2] >>",
			// w1 + w2 + w3 == 0
			"<< /Type /XRef /Size 2 /Root 1 0 R /W [0 0 0] >>",
			// wi > 8
			"<< /Type /XRef /Size 2 /Root 1 0 R /W [1 9 1] >>",
			// wi < 0
			"<< /Type /XRef /Size 2 /Root 1 0 R /W [-1 2 1] >>",
			// /W wrong element count
			"<< /Type /XRef /Size 2 /Root 1 0 R /W [1 2] >>",
			// Missing /Type /XRef
			"<< /Size 2 /Root 1 0 R /W [1 2 1] >>",
			// Invalid /Size 0
			"<< /Type /XRef /Size 0 /Root 1 0 R /W [1 2 1] >>",
		};

		for (const char* d_str : bad_dicts) {
			const PdfDict d = parse_dict_str(d_str);
			XrefTable table;
			uint64_t total_rows = 0;
			RECTO_EXPECT_ERR(
				XrefTable::unpack_xref_stream(d, valid_buf, 4096, 0, table, total_rows),
				ErrorCode::DamagedFormat);
		}
	}

	// 5. Hybrid-reference PDF (`/XRefStm`, ISO 32000-1 7.5.8.4): supplemental `/XRefStm`
	//    entries override compatibility free entries in the same revision's classic `xref`
	//    table, while entries from a newer incremental revision override `/XRefStm`.
	{
		const std::string objstm_body = "2 0 3 3\n42\n100";
		std::string pdf = "%PDF-1.6\n";
		const size_t off1 = pdf.size();
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 4 0 R >>\nendobj\n";
		const size_t off9 = pdf.size();
		pdf += "9 0 obj\n<< /Type /ObjStm /N 2 /First 8 /Length " +
			std::to_string(objstm_body.size()) + " >>\nstream\n" +
			objstm_body + "\nendstream\nendobj\n";
		const size_t xstm_off = pdf.size();
		// Supplemental /XRefStm defines compressed objects 2 (index 0 in ObjStm 9) and 3 (index 1 in ObjStm 9).
		// /W [1 2 1], /Index [2 2] -> 2 rows * 4 bytes = 8 bytes:
		//   obj 2: type=2, field2=9 (0x0009), field3=0 (0x00)
		//   obj 3: type=2, field2=9 (0x0009), field3=1 (0x01)
		const std::string xstm_payload("\x02\x00\x09\x00\x02\x00\x09\x01", 8u);
		pdf += "10 0 obj\n<< /Type /XRef /Size 11 /W [1 2 1] /Index [2 2] /Length 8 >>\nstream\n";
		pdf += xstm_payload;
		pdf += "\nendstream\nendobj\n";

		const size_t xref1_off = pdf.size();
		auto fmt_row = [](size_t off, uint16_t gen, char flag) {
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%010zu %05u %c \n", off, static_cast<unsigned>(gen), flag);
			return std::string(buf);
		};
		pdf += "xref\n0 4\n";
		pdf += fmt_row(0, 65535, 'f');
		pdf += fmt_row(off1, 0, 'n');
		// Classic table marks compressed objects 2 and 3 free for PDF 1.4 compatibility:
		pdf += fmt_row(0, 65535, 'f');
		pdf += fmt_row(0, 65535, 'f');
		pdf += "9 1\n";
		pdf += fmt_row(off9, 0, 'n');
		pdf += "trailer\n<< /Size 11 /Root 1 0 R /XRefStm " + std::to_string(xstm_off) + " >>\n";
		pdf += "startxref\n" + std::to_string(xref1_off) + "\n%%EOF\n";

		// Revision 2: classic incremental update overrides object 3 with uncompressed 200.
		const size_t off3_v2 = pdf.size();
		pdf += "3 0 obj\n200\nendobj\n";
		const size_t xref2_off = pdf.size();
		pdf += "xref\n3 1\n";
		pdf += fmt_row(off3_v2, 0, 'n');
		pdf += "trailer\n<< /Size 11 /Root 1 0 R /Prev " + std::to_string(xref1_off) + " >>\n";
		pdf += "startxref\n" + std::to_string(xref2_off) + "\n%%EOF\n";

		auto parsed = XrefTable::parse(as_bytes(pdf));
		RECTO_ASSERT_OK(parsed);

		// Object 2 must resolve via Revision 1's /XRefStm (42), NOT Revision 1's classic free entry.
		auto v2 = parsed->resolve_object(as_bytes(pdf), IndirectRef{2u, 0u});
		RECTO_ASSERT_OK(v2);
		EXPECT_EQ(v2->as_int(), 42);

		// Object 3 must resolve via Revision 2's classic update (200), overriding Revision 1's /XRefStm (100).
		auto v3 = parsed->resolve_object(as_bytes(pdf), IndirectRef{3u, 0u});
		RECTO_ASSERT_OK(v3);
		EXPECT_EQ(v3->as_int(), 200);
	}
}

TEST(PdfXref, ObjectStream)
{
	// 1. Corpus fixture render/compressed-xref-2p.pdf (/Type /XRef with PNG Up predictor + /Type /ObjStm).
	{
		const auto bytes = test::read_file_bytes(
			test::corpus_path("render/compressed-xref-2p.pdf"));
		ASSERT_FALSE(bytes.empty());

		auto xref_res = XrefTable::parse(bytes);
		RECTO_ASSERT_OK(xref_res);
		const XrefTable& xref = *xref_res;

		EXPECT_EQ(xref.pdf_version(), "1.6");
		EXPECT_EQ(xref.revision_count(), 1u);
		EXPECT_FALSE(xref.recovered());
		EXPECT_EQ(xref.root_ref(), (IndirectRef{1u, 0u}));
		EXPECT_EQ(xref.info_ref(), (IndirectRef{8u, 0u}));
		EXPECT_EQ(xref.cached_obj_stream_count(), 0u);

		// Objects 1, 2, 3, 4, 7, 8 are compressed in ObjStm 9 at indices 0..5.
		const uint32_t comp_ids[] = {1u, 2u, 3u, 4u, 7u, 8u};
		for (size_t idx = 0; idx < 6u; ++idx) {
			const XrefEntry* e = xref.find(comp_ids[idx]);
			ASSERT_NE(e, nullptr) << "missing compressed object " << comp_ids[idx];
			EXPECT_TRUE(e->is_compressed());
			EXPECT_EQ(e->obj_stm_num, 9u);
			EXPECT_EQ(e->obj_stm_index, idx);
		}

		// Resolve all 6 compressed objects and verify ObjStm 9 is decoded and cached once.
		auto cat = xref.resolve_object(bytes, IndirectRef{1u, 0u});
		RECTO_ASSERT_OK(cat);
		ASSERT_TRUE(cat->is_dict());
		EXPECT_EQ(cat->as_dict()->get_name("Type"), "Catalog");
		EXPECT_EQ(cat->as_dict()->get_ref("Pages"), (IndirectRef{2u, 0u}));
		EXPECT_EQ(xref.cached_obj_stream_count(), 1u);

		auto pages = xref.resolve_object(bytes, IndirectRef{2u, 0u});
		RECTO_ASSERT_OK(pages);
		ASSERT_TRUE(pages->is_dict());
		EXPECT_EQ(pages->as_dict()->get_name("Type"), "Pages");
		EXPECT_EQ(pages->as_dict()->get_int("Count"), 2);

		auto p1 = xref.resolve_object(bytes, IndirectRef{3u, 0u});
		RECTO_ASSERT_OK(p1);
		ASSERT_TRUE(p1->is_dict());
		EXPECT_EQ(p1->as_dict()->get_name("Type"), "Page");
		EXPECT_EQ(p1->as_dict()->get_ref("Contents"), (IndirectRef{5u, 0u}));

		auto p2 = xref.resolve_object(bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(p2);
		ASSERT_TRUE(p2->is_dict());
		EXPECT_EQ(p2->as_dict()->get_name("Type"), "Page");
		EXPECT_EQ(p2->as_dict()->get_ref("Contents"), (IndirectRef{6u, 0u}));

		auto font = xref.resolve_object(bytes, IndirectRef{7u, 0u});
		RECTO_ASSERT_OK(font);
		ASSERT_TRUE(font->is_dict());
		EXPECT_EQ(font->as_dict()->get_name("BaseFont"), "Helvetica");

		auto info = xref.resolve_object(bytes, IndirectRef{8u, 0u});
		RECTO_ASSERT_OK(info);
		ASSERT_TRUE(info->is_dict());
		ASSERT_NE(info->as_dict()->get_string("Title"), nullptr);
		EXPECT_EQ(
			info->as_dict()->get_string("Title")->as_byte_view(),
			"Recto Compressed XRef 2P");
		EXPECT_EQ(xref.cached_obj_stream_count(), 1u);

		// Uncompressed page content streams 5 and 6 also resolve cleanly.
		auto stm5 = xref.resolve_object(bytes, IndirectRef{5u, 0u});
		RECTO_ASSERT_OK(stm5);
		EXPECT_TRUE(stm5->is_stream());
		auto stm6 = xref.resolve_object(bytes, IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(stm6);
		EXPECT_TRUE(stm6->is_stream());
	}

	// 2. Seed fixture seeds/seed-xref-objstm.pdf.
	{
		const auto seed_bytes = test::read_file_bytes(
			test::corpus_path("seeds/seed-xref-objstm.pdf"));
		ASSERT_FALSE(seed_bytes.empty());

		auto xref_res = XrefTable::parse(seed_bytes);
		RECTO_ASSERT_OK(xref_res);
		for (uint32_t oid : {1u, 2u, 3u, 4u, 5u, 6u}) {
			RECTO_ASSERT_OK(xref_res->resolve_object(seed_bytes, IndirectRef{oid, 0u}));
		}
	}

	// 3. Refuse nested /ObjStm -> /ObjStm (type-2 XRef entry pointing to an object that is itself type-2).
	{
		const auto bytes = test::read_file_bytes(
			test::corpus_path("render/compressed-xref-2p.pdf"));
		auto xref_res = XrefTable::parse(bytes);
		RECTO_ASSERT_OK(xref_res);
		XrefTable xref = std::move(*xref_res);

		// Object 12 compressed inside object 11, while object 11 is compressed inside object 9.
		xref.set_entry(11u, XrefEntry{XrefEntryKind::Compressed, 0u, 9u, 0u, 0u});
		xref.set_entry(12u, XrefEntry{XrefEntryKind::Compressed, 0u, 11u, 0u, 0u});

		RECTO_EXPECT_ERR(
			xref.resolve_object(bytes, IndirectRef{12u, 0u}),
			ErrorCode::DamagedFormat);
	}

	// 4. Refuse indirect `stream` objects, `N G obj` headers, and nested `/Type /ObjStm` dictionaries
	//    stored inside an `/ObjStm` stream payload per ISO 32000-1 section 7.5.7.
	{
		auto build_pdf_with_objstm_body = [](std::string_view inner_obj_text) -> std::string {
			const std::string hdr = "1 0 ";
			const std::string stm_payload = hdr + std::string(inner_obj_text);
			std::string pdf = "%PDF-1.7\n";
			const size_t off5 = pdf.size();
			pdf += "5 0 obj\n<< /Type /ObjStm /N 1 /First " + std::to_string(hdr.size()) +
				" /Length " + std::to_string(stm_payload.size()) + " >>\nstream\n" +
				stm_payload + "\nendstream\nendobj\n";
			const size_t off6 = pdf.size();
			// /Type /XRef stream with /W [1 2 1], /Size 7:
			// 0: free, 1: compressed in 5 idx 0, 2..4: free, 5: uncompressed at off5, 6: uncompressed at off6
			std::string xref_bytes(7 * 4, '\0');
			// 0: free gen 255
			xref_bytes[3] = '\xFF';
			// 1: type 2, stm 5, idx 0
			xref_bytes[4] = 2;
			xref_bytes[6] = 5;
			xref_bytes[7] = 0;
			// 5: type 1, off5, gen 0
			xref_bytes[20] = 1;
			xref_bytes[21] = static_cast<char>((off5 >> 8) & 0xFF);
			xref_bytes[22] = static_cast<char>(off5 & 0xFF);
			// 6: type 1, off6, gen 0
			xref_bytes[24] = 1;
			xref_bytes[25] = static_cast<char>((off6 >> 8) & 0xFF);
			xref_bytes[26] = static_cast<char>(off6 & 0xFF);

			pdf += "6 0 obj\n<< /Type /XRef /Size 7 /Root 1 0 R /W [1 2 1] /Length " +
				std::to_string(xref_bytes.size()) + " >>\nstream\n" + xref_bytes +
				"\nendstream\nendobj\nstartxref\n" + std::to_string(off6) + "\n%%EOF\n";
			return pdf;
		};

		// 4a. Stream object inside /ObjStm -> DamagedFormat.
		{
			const std::string bad_stream_pdf = build_pdf_with_objstm_body(
				"<< /Length 4 >>\nstream\nabcd\nendstream");
			auto xref_res = XrefTable::parse(as_bytes(bad_stream_pdf));
			RECTO_ASSERT_OK(xref_res);
			RECTO_EXPECT_ERR(
				xref_res->resolve_object(as_bytes(bad_stream_pdf), IndirectRef{1u, 0u}),
				ErrorCode::DamagedFormat);
		}

		// 4b. Indirect `1 0 obj ... endobj` wrapper inside /ObjStm -> DamagedFormat.
		{
			const std::string bad_obj_pdf = build_pdf_with_objstm_body(
				"1 0 obj << /Type /Catalog >> endobj");
			auto xref_res = XrefTable::parse(as_bytes(bad_obj_pdf));
			RECTO_ASSERT_OK(xref_res);
			RECTO_EXPECT_ERR(
				xref_res->resolve_object(as_bytes(bad_obj_pdf), IndirectRef{1u, 0u}),
				ErrorCode::DamagedFormat);
		}

		// 4c. Dictionary with /Type /ObjStm inside /ObjStm -> DamagedFormat.
		{
			const std::string bad_nested_dict_pdf = build_pdf_with_objstm_body(
				"<< /Type /ObjStm /N 1 /First 4 >>");
			auto xref_res = XrefTable::parse(as_bytes(bad_nested_dict_pdf));
			RECTO_ASSERT_OK(xref_res);
			RECTO_EXPECT_ERR(
				xref_res->resolve_object(as_bytes(bad_nested_dict_pdf), IndirectRef{1u, 0u}),
				ErrorCode::DamagedFormat);
		}
	}
}

TEST(PdfXref, DamagedRecovery)
{
	// 1. Corpus fixture tests/corpus/broken/corrupt-xref-recoverable.pdf:
	//    strict XrefTable::parse fails, while XrefTable::recover_damaged_xref and
	//    XrefTable::parse(..., /*allow_recovery=*/true) recover all objects 1..6.
	{
		const auto bytes = test::read_file_bytes(
			test::corpus_path("broken/corrupt-xref-recoverable.pdf"));
		ASSERT_FALSE(bytes.empty());

		RECTO_EXPECT_ERR(XrefTable::parse(bytes), ErrorCode::DamagedFormat);

		auto rec_res = XrefTable::recover_damaged_xref(bytes);
		RECTO_ASSERT_OK(rec_res);
		const XrefTable& xref = *rec_res;

		EXPECT_TRUE(xref.recovered());
		EXPECT_EQ(xref.pdf_version(), "1.7");
		EXPECT_EQ(xref.root_ref(), (IndirectRef{1u, 0u}));
		EXPECT_EQ(xref.info_ref(), (IndirectRef{6u, 0u}));
		EXPECT_EQ(xref.trailer().get_int("Size"), 7);

		for (uint32_t obj_id = 1u; obj_id <= 6u; ++obj_id) {
			auto val = xref.resolve_object(bytes, IndirectRef{obj_id, 0u});
			RECTO_ASSERT_OK(val);
		}

		auto cat = xref.resolve_object(bytes, IndirectRef{1u, 0u});
		RECTO_ASSERT_OK(cat);
		EXPECT_EQ(cat->as_dict()->get_name("Type"), "Catalog");

		auto info = xref.resolve_object(bytes, IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(info);
		EXPECT_EQ(
			info->as_dict()->get_string("Title")->as_byte_view(),
			"Recoverable Xref Fixture");

		// Also verify XrefTable::parse with allow_recovery = true recovers cleanly.
		auto parse_rec = XrefTable::parse(bytes, kMaxXrefTotalEntries, /*allow_recovery=*/true);
		RECTO_ASSERT_OK(parse_rec);
		EXPECT_TRUE(parse_rec->recovered());
		EXPECT_EQ(parse_rec->root_ref(), (IndirectRef{1u, 0u}));
	}

	// 2. Damaged PDF where no trailer dictionary survives at all:
	//    recover_damaged_xref locates /Root from the indirect object with /Type /Catalog
	//    and /Info from metadata keys, and respects newer revisions of the same object.
	{
		std::string pdf = "%PDF-1.7\n";
		pdf += "% 99 0 obj << /Type /Catalog >> endobj inside a comment line must be ignored\n";
		pdf += "1 0 obj\n<< /Type /Pages /Kids [2 0 R] /Count 1 >>\nendobj\n";
		pdf += "2 0 obj\n<< /Type /Page /Parent 1 0 R /MediaBox [0 0 612 792] >>\nendobj\n";
		pdf += "5 0 obj\n<< /Type /Catalog /Pages 1 0 R >>\nendobj\n";
		pdf += "6 0 obj\n<< /Title (First Title) /Author (Recto) >>\nendobj\n";
		// Later revision of object 6 overrides earlier definition.
		pdf += "6 0 obj\n<< /Title (Updated Recovery Title) /Author (Recto) >>\nendobj\n";
		// Truncated xref table with no surviving trailer dictionary.
		pdf += "xref\n0 7\nGARBAGE_NO_TRAILER\n";

		auto rec_res = XrefTable::recover_damaged_xref(as_bytes(pdf));
		RECTO_ASSERT_OK(rec_res);
		const XrefTable& xref = *rec_res;

		EXPECT_TRUE(xref.recovered());
		EXPECT_EQ(xref.root_ref(), (IndirectRef{5u, 0u}));
		EXPECT_EQ(xref.info_ref(), (IndirectRef{6u, 0u}));
		EXPECT_EQ(xref.trailer().get_int("Size"), 7);
		EXPECT_EQ(xref.find(99u), nullptr);

		auto info = xref.resolve_object(as_bytes(pdf), IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(info);
		EXPECT_EQ(
			info->as_dict()->get_string("Title")->as_byte_view(),
			"Updated Recovery Title");
	}

	// 3. Revision transition where an earlier uncompressed object is overridden by a later
	//    `/Type /ObjStm` stream during recovery.
	{
		const std::string objstm_payload =
			"6 0\n<< /Title (Compressed Revision Title) /Author (Recto) >>";
		std::string pdf = "%PDF-1.7\n";
		pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
		pdf += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf += "6 0 obj\n<< /Title (Old Uncompressed Title) /Author (Recto) >>\nendobj\n";
		pdf += "9 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length " +
			std::to_string(objstm_payload.size()) + " >>\nstream\n" +
			objstm_payload + "\nendstream\nendobj\n";
		pdf += "startxref\n999999\n%%EOF\n";

		auto rec_res = XrefTable::recover_damaged_xref(as_bytes(pdf));
		RECTO_ASSERT_OK(rec_res);
		const XrefEntry* e6 = rec_res->find(6u);
		ASSERT_NE(e6, nullptr);
		EXPECT_TRUE(e6->is_compressed());
		EXPECT_EQ(e6->obj_stm_num, 9u);

		auto info = rec_res->resolve_object(as_bytes(pdf), IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(info);
		EXPECT_EQ(
			info->as_dict()->get_string("Title")->as_byte_view(),
			"Compressed Revision Title");
	}

	// 4. Surviving trailer `/Root` validation: rejects wrong generation (`/Root 5 1 R` vs `5 0 obj`)
	//    or non-catalog target (`/Root 1 0 R` where `1 0 obj` is `/Type /Pages`), falling back to
	//    discovered `/Type /Catalog` when present or failing with `ErrorCode::DamagedFormat` when none exists.
	{
		std::string pdf_wrong_gen = "%PDF-1.7\n";
		pdf_wrong_gen += "1 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_wrong_gen += "5 0 obj\n<< /Type /Catalog /Pages 1 0 R >>\nendobj\n";
		pdf_wrong_gen += "trailer\n<< /Size 6 /Root 5 1 R >>\nstartxref\n999999\n%%EOF\n";

		auto rec_wrong_gen = XrefTable::recover_damaged_xref(as_bytes(pdf_wrong_gen));
		RECTO_ASSERT_OK(rec_wrong_gen);
		EXPECT_EQ(rec_wrong_gen->root_ref(), (IndirectRef{5u, 0u}));

		std::string pdf_non_catalog = "%PDF-1.7\n";
		pdf_non_catalog += "1 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_non_catalog += "trailer\n<< /Size 2 /Root 1 0 R >>\nstartxref\n999999\n%%EOF\n";

		auto rec_non_cat = XrefTable::recover_damaged_xref(as_bytes(pdf_non_catalog));
		RECTO_EXPECT_ERR(rec_non_cat, ErrorCode::DamagedFormat);
	}

	// 5. Hostile repeated malformed `<N> <G> obj (` candidates without closing parentheses
	//    are bounded by both candidate-attempt and cumulative parse-byte budgets.
	{
		std::string hostile = "%PDF-1.7\n";
		for (int i = 0; i < 600; ++i) {
			hostile += "2 0 obj (\n";
		}
		hostile += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";

		auto rec_budget = XrefTable::recover_damaged_xref(as_bytes(hostile));
		RECTO_EXPECT_ERR(rec_budget, ErrorCode::DamagedFormat);

		auto rec_attempts = XrefTable::recover_damaged_xref(as_bytes(hostile), /*max_total_entries=*/8u);
		RECTO_EXPECT_ERR(rec_attempts, ErrorCode::DamagedFormat);

		std::string hostile_dict = "%PDF-1.7\n";
		for (int i = 0; i < 600; ++i) {
			hostile_dict += "2 0 obj << (\n";
		}
		hostile_dict += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";
		auto rec_dict_budget = XrefTable::recover_damaged_xref(as_bytes(hostile_dict));
		RECTO_EXPECT_ERR(rec_dict_budget, ErrorCode::DamagedFormat);

		std::string hostile_ref_lookahead = "%PDF-1.7\n";
		for (int i = 0; i < 600; ++i) {
			hostile_ref_lookahead += "2 0 obj << /A 1 2 (\n";
		}
		hostile_ref_lookahead += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";
		auto rec_ref_budget = XrefTable::recover_damaged_xref(as_bytes(hostile_ref_lookahead));
		RECTO_EXPECT_ERR(rec_ref_budget, ErrorCode::DamagedFormat);

		std::string hostile_trailer = "%PDF-1.7\n";
		for (int i = 0; i < 600; ++i) {
			hostile_trailer += "trailer << (\n";
		}
		hostile_trailer += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";
		auto rec_trailer_budget = XrefTable::recover_damaged_xref(as_bytes(hostile_trailer));
		RECTO_EXPECT_ERR(rec_trailer_budget, ErrorCode::DamagedFormat);

		// Malformed non-stream objects whose literal string happens to contain "endstream"
		// only charge the bytes actually scanned by Lexer/Parser, not the entire remaining file.
		std::string literal_endstream_pdf = "%PDF-1.7\n";
		for (int i = 0; i < 5; ++i) {
			literal_endstream_pdf += "2 0 obj (endstream) nope\n";
		}
		literal_endstream_pdf += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";
		literal_endstream_pdf += std::string(102400u, ' ');
		auto rec_literal_endstream = XrefTable::recover_damaged_xref(as_bytes(literal_endstream_pdf));
		RECTO_ASSERT_OK(rec_literal_endstream);
		EXPECT_EQ(rec_literal_endstream->root_ref(), (IndirectRef{1u, 0u}));
	}

	// 6. Single-pass catalog fallback without secondary reparsing, and UINT32_MAX object IDs
	//    synthesizing `/Size` as 4294967296 without 32-bit unsigned wraparound.
	{
		const std::string objstm_non_cat = "4 0\n<< /Key (CompressedNonCatalog) >>";
		std::string single_pass_pdf = "%PDF-1.7\n";
		single_pass_pdf += "1 0 obj\n<< /Type /Catalog /Pages 3 0 R >>\nendobj\n";
		single_pass_pdf += "88 0 obj\n3\nendobj\n";
		for (int i = 0; i < 500; ++i) {
			single_pass_pdf += "2 0 obj null endobj\n";
		}
		single_pass_pdf += "2 0 obj\n<< /Length 88 0 R >>\nstream\nabc\nendstream\nendobj\n";
		single_pass_pdf += "88 0 obj\n(overwritten_non_int)\nendobj\n";
		single_pass_pdf += "9 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length " +
			std::to_string(objstm_non_cat.size()) + " >>\nstream\n" +
			objstm_non_cat + "\nendstream\nendobj\n";
		single_pass_pdf += "trailer\n<< /Size 100 /Root 99 0 R >>\nstartxref\n999999\n%%EOF\n";

		clear_diagnostics();
		auto rec_single = XrefTable::recover_damaged_xref(as_bytes(single_pass_pdf));
		RECTO_ASSERT_OK(rec_single);
		EXPECT_EQ(rec_single->root_ref(), (IndirectRef{1u, 0u}));
		// Zero secondary `resolve_object` calls occurred during fallback: ObjStm 9 was not
		// populated into `obj_stm_cache_`, and no secondary `DamagedStream` diagnostic was emitted.
		EXPECT_EQ(rec_single->cached_obj_stream_count(), 0u);
		const auto diags = diagnostics_tail();
		ASSERT_EQ(diags.size(), 1u);
		EXPECT_EQ(diags.front().diagnostic_id, DiagnosticId::DamagedXRef);

		const std::string max_id_pdf =
			"%PDF-1.7\n4294967295 0 obj\n<< /Type /Catalog >>\nendobj\n";
		auto rec_max_id = XrefTable::recover_damaged_xref(as_bytes(max_id_pdf));
		RECTO_ASSERT_OK(rec_max_id);
		EXPECT_EQ(
			rec_max_id->root_ref(),
			(IndirectRef{std::numeric_limits<uint32_t>::max(), 0u}));
		EXPECT_EQ(rec_max_id->trailer().get_int("Size"), 4294967296LL);
	}

	// 7. Overwriting a `/Type /ObjStm` container in a later revision invalidates the compressed
	//    objects and catalog metadata that were stored inside that container, while a rejected
	//    inner object inside another `/ObjStm` does NOT invalidate an intact container.
	{
		const std::string cat_objstm = "1 0\n<< /Type /Catalog /Pages 2 0 R >>";
		std::string pdf_overwritten_stm = "%PDF-1.7\n";
		pdf_overwritten_stm += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_overwritten_stm += "9 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length " +
			std::to_string(cat_objstm.size()) + " >>\nstream\n" +
			cat_objstm + "\nendstream\nendobj\n";
		// Later revision redefines object 9 as `null`, invalidating compressed object 1.
		pdf_overwritten_stm += "9 0 obj\nnull\nendobj\n";
		pdf_overwritten_stm += "startxref\n999999\n%%EOF\n";

		auto rec_invalidated = XrefTable::recover_damaged_xref(as_bytes(pdf_overwritten_stm));
		RECTO_EXPECT_ERR(rec_invalidated, ErrorCode::DamagedFormat);

		// Conversely, if ObjStm 10 attempts to define inner object 9 with an invalid direct
		// object (`9 0 (`), the inner object is rejected and container 9's catalog object 1 survives.
		const std::string bad_inner_stm = "9 0\n(";
		std::string pdf_rejected_inner = "%PDF-1.7\n";
		pdf_rejected_inner += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_rejected_inner += "9 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length " +
			std::to_string(cat_objstm.size()) + " >>\nstream\n" +
			cat_objstm + "\nendstream\nendobj\n";
		pdf_rejected_inner += "10 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length " +
			std::to_string(bad_inner_stm.size()) + " >>\nstream\n" +
			bad_inner_stm + "\nendstream\nendobj\n";
		pdf_rejected_inner += "startxref\n999999\n%%EOF\n";

		auto rec_preserved = XrefTable::recover_damaged_xref(as_bytes(pdf_rejected_inner));
		RECTO_ASSERT_OK(rec_preserved);
		EXPECT_EQ(rec_preserved->root_ref(), (IndirectRef{1u, 0u}));

		// The same holds for a member that is a bare object reference, which ISO 32000-1 section 7.5.7 forbids:
		// recovery skips it, as every other path refuses it, and container 9's catalog survives.
		const std::string bare_inner_stm = "9 0\n2 0 R";
		std::string pdf_bare_inner = pdf_rejected_inner;
		const size_t bad_at = pdf_bare_inner.find("10 0 obj");
		pdf_bare_inner.erase(bad_at);
		pdf_bare_inner += "10 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length " +
			std::to_string(bare_inner_stm.size()) + " >>\nstream\n" + bare_inner_stm +
			"\nendstream\nendobj\nstartxref\n999999\n%%EOF\n";
		auto rec_bare = XrefTable::recover_damaged_xref(as_bytes(pdf_bare_inner));
		RECTO_ASSERT_OK(rec_bare);
		EXPECT_EQ(rec_bare->root_ref(), (IndirectRef{1u, 0u}));

		// A catalog whose /Type is an indirect reference to an object later in the file is still recognized, once
		// the scan has found that object (no object stream involved).
		std::string pdf_indirect_type = "%PDF-1.7\n";
		pdf_indirect_type += "1 0 obj\n<< /Type 4 0 R /Pages 2 0 R >>\nendobj\n";
		pdf_indirect_type += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_indirect_type += "4 0 obj\n/Catalog\nendobj\n";
		pdf_indirect_type += "trailer\n<< /Size 5 /Root 1 0 R >>\n%%EOF\n";
		auto rec_type = XrefTable::recover_damaged_xref(as_bytes(pdf_indirect_type));
		RECTO_ASSERT_OK(rec_type);
		EXPECT_EQ(rec_type->root_ref(), (IndirectRef{1u, 0u}));

		// One level, as every indirect value: a /Type that points at an alias of the /Catalog name names no catalog,
		// so with no trailer the earlier direct-type catalog 1 stays the root rather than the later dictionary 13.
		std::string pdf_alias = "%PDF-1.7\n";
		pdf_alias += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
		pdf_alias += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_alias += "8 0 obj\n/Catalog\nendobj\n";
		pdf_alias += "9 0 obj\n8 0 R\nendobj\n";
		pdf_alias += "13 0 obj\n<< /Type 9 0 R /Pages 2 0 R >>\nendobj\n%%EOF\n";
		auto rec_alias = XrefTable::recover_damaged_xref(as_bytes(pdf_alias));
		RECTO_ASSERT_OK(rec_alias);
		EXPECT_EQ(rec_alias->root_ref(), (IndirectRef{1u, 0u}));
		// And the name itself, redefined later as something else, no longer makes a catalog.
		std::string pdf_redefined = pdf_indirect_type;
		pdf_redefined.erase(pdf_redefined.rfind("trailer"));
		pdf_redefined += "4 0 obj\n/Pages\nendobj\ntrailer\n<< /Size 5 /Root 1 0 R >>\n%%EOF\n";
		RECTO_EXPECT_ERR(XrefTable::recover_damaged_xref(as_bytes(pdf_redefined)), ErrorCode::DamagedFormat);

		// File order still decides between catalogs: with no trailer, the later direct-type catalog 6 wins over the
		// earlier indirect-type catalog 1, as between two direct-type catalogs.
		std::string pdf_order = "%PDF-1.7\n";
		pdf_order += "1 0 obj\n<< /Type 4 0 R /Pages 2 0 R >>\nendobj\n";
		pdf_order += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_order += "4 0 obj\n/Catalog\nendobj\n";
		pdf_order += "5 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_order += "6 0 obj\n<< /Type /Catalog /Pages 5 0 R >>\nendobj\n%%EOF\n";
		auto rec_order = XrefTable::recover_damaged_xref(as_bytes(pdf_order));
		RECTO_ASSERT_OK(rec_order);
		EXPECT_EQ(rec_order->root_ref(), (IndirectRef{6u, 0u}));
		// And the reverse: the later indirect-type catalog wins over the earlier direct-type one.
		std::string pdf_order_rev = "%PDF-1.7\n";
		pdf_order_rev += "6 0 obj\n<< /Type /Catalog /Pages 5 0 R >>\nendobj\n";
		pdf_order_rev += "5 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_order_rev += "1 0 obj\n<< /Type 4 0 R /Pages 2 0 R >>\nendobj\n";
		pdf_order_rev += "2 0 obj\n<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_order_rev += "4 0 obj\n/Catalog\nendobj\n%%EOF\n";
		auto rec_order_rev = XrefTable::recover_damaged_xref(as_bytes(pdf_order_rev));
		RECTO_ASSERT_OK(rec_order_rev);
		EXPECT_EQ(rec_order_rev->root_ref(), (IndirectRef{1u, 0u}));
	}

	// 8. Compressed `/Type /ObjStm` streams whose decoded payload exceeds `max(file_size * 4, 65536)`
	//    (but stays within `kDefaultMaxDecodedStreamBytes` and `kDefaultMaxCompressionRatio`)
	//    recover cleanly.
	{
		// Build a RunLengthDecode stream that decodes to:
		// `1 0\n<< /Type /Catalog /Padding (` + 100,000 'a' bytes + `) >>`
		// RunLength encoding:
		//   - literal run for prefix `1 0\n<< /Type /Catalog /Padding (` (32 bytes -> length byte 31)
		//   - 800 repeat runs of 125 'a' bytes (each run is [257 - 125 = 132, 'a'] -> 100,000 'a' bytes)
		//   - literal run for suffix `) >>` (4 bytes -> length byte 3)
		//   - EOD byte 128
		const std::string prefix = "1 0\n<< /Type /Catalog /Padding (";
		const std::string suffix = ") >>";
		std::string rle_bytes;
		rle_bytes.push_back(static_cast<char>(prefix.size() - 1u));
		rle_bytes += prefix;
		for (int i = 0; i < 800; ++i) {
			rle_bytes.push_back(static_cast<char>(257 - 125));
			rle_bytes.push_back('a');
		}
		rle_bytes.push_back(static_cast<char>(suffix.size() - 1u));
		rle_bytes += suffix;
		rle_bytes.push_back(static_cast<char>(128));

		std::string pdf_rle = "%PDF-1.7\n";
		pdf_rle += "9 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Filter /RunLengthDecode /Length " +
			std::to_string(rle_bytes.size()) + " >>\nstream\n";
		pdf_rle += rle_bytes;
		pdf_rle += "\nendstream\nendobj\nstartxref\n999999\n%%EOF\n";

		auto rec_rle = XrefTable::recover_damaged_xref(as_bytes(pdf_rle));
		RECTO_ASSERT_OK(rec_rle);
		EXPECT_EQ(rec_rle->root_ref(), (IndirectRef{1u, 0u}));
		auto cat_val = rec_rle->resolve_object(as_bytes(pdf_rle), IndirectRef{1u, 0u});
		RECTO_ASSERT_OK(cat_val);
		EXPECT_EQ(cat_val->as_dict()->get_name("Type"), "Catalog");
		EXPECT_EQ(cat_val->as_dict()->get_string("Padding")->as_byte_view().size(), 100000u);

		// When `max_total_entries` is tight (1 -> 64 KiB decoded ObjStm budget),
		// `recover_damaged_xref` returns `DecompressionLimitExceeded`, and
		// `XrefTable::parse(..., allow_recovery=true)` propagates that resource error
		// rather than masking it with the primary `DamagedFormat` error.
		auto rec_rle_tight = XrefTable::recover_damaged_xref(as_bytes(pdf_rle), 1u);
		RECTO_EXPECT_ERR(rec_rle_tight, ErrorCode::DecompressionLimitExceeded);
		auto parse_rle_tight = XrefTable::parse(as_bytes(pdf_rle), 1u, /*allow_recovery=*/true);
		RECTO_EXPECT_ERR(parse_rle_tight, ErrorCode::DecompressionLimitExceeded);
	}

	// 9. Recovery accepts indirect object headers containing PDF comments between
	//    `<N>`, `<G>`, and `obj` (per ISO 32000-1 section 7.2.3).
	{
		std::string pdf_hdr_comments = "%PDF-1.7\n";
		pdf_hdr_comments += "1 % comment after obj num\n";
		pdf_hdr_comments += "0 % comment after gen num\n";
		pdf_hdr_comments += "obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
		pdf_hdr_comments += "2%no-space-comment\n0%no-space-gen-comment\nobj%after-obj\n";
		pdf_hdr_comments += "<< /Type /Pages /Kids [] /Count 0 >>\nendobj\n";
		pdf_hdr_comments += "startxref\n999999\n%%EOF\n";

		auto rec_comments = XrefTable::recover_damaged_xref(as_bytes(pdf_hdr_comments));
		RECTO_ASSERT_OK(rec_comments);
		EXPECT_EQ(rec_comments->root_ref(), (IndirectRef{1u, 0u}));
		auto cat_val = rec_comments->resolve_object(as_bytes(pdf_hdr_comments), IndirectRef{1u, 0u});
		RECTO_ASSERT_OK(cat_val);
		EXPECT_EQ(cat_val->as_dict()->get_name("Type"), "Catalog");
		auto pages_val = rec_comments->resolve_object(as_bytes(pdf_hdr_comments), IndirectRef{2u, 0u});
		RECTO_ASSERT_OK(pages_val);
		EXPECT_EQ(pages_val->as_dict()->get_name("Type"), "Pages");
	}
}

TEST(PdfXref, RejectsBrokenFiles)
{
	struct BrokenCase {
		const char* rel_path;
		ErrorCode expected_code;
		DiagnosticId expected_diag;
	};

	const BrokenCase cases[] = {
		{"broken/empty-0b.pdf", ErrorCode::NotAPdf, DiagnosticId::NotAPdfHeader},
		{"broken/non-pdf-header.pdf", ErrorCode::NotAPdf, DiagnosticId::NotAPdfHeader},
		{"broken/truncated-header.pdf", ErrorCode::DamagedFormat, DiagnosticId::DamagedHeader},
		{"broken/truncated-stream.pdf", ErrorCode::DamagedFormat, DiagnosticId::DamagedTrailer},
	};

	for (const BrokenCase& tc : cases) {
		const auto bytes = test::read_file_bytes(test::corpus_path(tc.rel_path));
		if (std::string_view(tc.rel_path) == "broken/empty-0b.pdf") {
			EXPECT_TRUE(bytes.empty());
		} else {
			ASSERT_FALSE(bytes.empty()) << tc.rel_path;
		}

		clear_diagnostics();
		auto parse_res = XrefTable::parse(bytes);
		RECTO_EXPECT_ERR(parse_res, tc.expected_code);
		const auto diags = diagnostics_tail();
		ASSERT_FALSE(diags.empty()) << tc.rel_path;
		EXPECT_EQ(diags.front().diagnostic_id, tc.expected_diag) << tc.rel_path;

		clear_diagnostics();
		auto rec_res = XrefTable::recover_damaged_xref(bytes);
		RECTO_EXPECT_ERR(rec_res, tc.expected_code);
		const auto rec_diags = diagnostics_tail();
		ASSERT_FALSE(rec_diags.empty()) << tc.rel_path;
	}
}

} // namespace
} // namespace recto::pdf
