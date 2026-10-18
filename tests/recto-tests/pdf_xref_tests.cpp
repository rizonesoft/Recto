// Unit tests for classic PDF xref tables, trailer merging, /Prev incremental
// update chains, header offset adjustment, and cycle guards (D01 T01 section 5).
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
	constexpr std::string_view kPreamble32 = "JUNK_PREAMBLE_32_BYTES_PADDING!\n";
	static_assert(kPreamble32.size() == 32);
	verify_simple_1p_with_preamble(as_bytes(kPreamble32));
}

TEST(PdfXref, IncrementalTwoRevisions)
{
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

} // namespace
} // namespace recto::pdf
