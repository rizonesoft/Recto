// Unit tests for `recto::pdf::Document` (D01 T01 section 10).
#include "document.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

namespace recto::pdf {
namespace {

std::vector<uint8_t> read_corpus_bytes(std::string_view relative_path)
{
	const auto full_path = recto::test::corpus_path(relative_path);
	std::ifstream ifs(full_path, std::ios::binary | std::ios::ate);
	EXPECT_TRUE(ifs.is_open()) << "Failed to open corpus file: " << full_path.string();
	if (!ifs.is_open()) {
		return {};
	}
	const auto sz = ifs.tellg();
	ifs.seekg(0, std::ios::beg);
	std::vector<uint8_t> buf(static_cast<size_t>(sz));
	if (sz > 0) {
		ifs.read(reinterpret_cast<char*>(buf.data()), sz);
	}
	return buf;
}

std::vector<uint8_t> build_synthetic_pdf_from_objects(
	std::string_view pdf_version,
	const std::vector<std::pair<uint32_t, std::string>>& objects,
	uint32_t root_obj = 1,
	std::optional<uint32_t> info_obj = std::nullopt)
{
	std::string out = "%PDF-";
	out.append(pdf_version);
	out.append("\n%\xE2\xE3\xCF\xD3\n");

	uint32_t max_obj_num = root_obj;
	for (const auto& [obj_num, body] : objects) {
		if (obj_num > max_obj_num) {
			max_obj_num = obj_num;
		}
	}
	if (info_obj.has_value() && *info_obj > max_obj_num) {
		max_obj_num = *info_obj;
	}

	std::vector<uint64_t> offsets(static_cast<size_t>(max_obj_num) + 1u, 0u);
	for (const auto& [obj_num, body] : objects) {
		offsets[obj_num] = static_cast<uint64_t>(out.size());
		out += std::to_string(obj_num) + " 0 obj\n" + body + "\nendobj\n";
	}

	const uint64_t xref_offset = static_cast<uint64_t>(out.size());
	out += "xref\n0 " + std::to_string(max_obj_num + 1u) + "\n";
	out += "0000000000 65535 f \n";
	for (uint32_t i = 1; i <= max_obj_num; ++i) {
		char entry_buf[32]{};
		if (offsets[i] != 0u) {
			std::snprintf(
				entry_buf,
				sizeof(entry_buf),
				"%010llu 00000 n \n",
				static_cast<unsigned long long>(offsets[i]));
		} else {
			std::snprintf(entry_buf, sizeof(entry_buf), "0000000000 65535 f \n");
		}
		out += entry_buf;
	}
	out += "trailer\n<< /Size " + std::to_string(max_obj_num + 1u) +
		" /Root " + std::to_string(root_obj) + " 0 R";
	if (info_obj.has_value()) {
		out += " /Info " + std::to_string(*info_obj) + " 0 R";
	}
	out += " >>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";

	return std::vector<uint8_t>(out.begin(), out.end());
}

TEST(PdfDocument, OpenAndAuthenticate)
{
	// 1. Non-existent file returns FileNotFound:
	{
		const auto missing_res = Document::open(
			recto::test::corpus_path("render/does-not-exist-anywhere.pdf"));
		ASSERT_FALSE(missing_res.has_value());
		EXPECT_EQ(missing_res.error().code, ErrorCode::FileNotFound);
	}

	// 2. Unencrypted corpus file opens directly via `open` and `open_memory`:
	{
		const auto path = recto::test::corpus_path("render/simple-1p.pdf");
		auto doc_res = Document::open(path);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;
		EXPECT_FALSE(doc.is_encrypted());
		EXPECT_TRUE(doc.is_authenticated());
		EXPECT_FALSE(doc.needs_password());
		EXPECT_FALSE(doc.is_recovered());
		EXPECT_EQ(doc.page_count(), 1u);
		EXPECT_EQ(doc.pdf_version(), "1.7");

		const auto mem_bytes = read_corpus_bytes("render/simple-1p.pdf");
		auto mem_doc_res = Document::open_memory(mem_bytes);
		ASSERT_TRUE(mem_doc_res.has_value()) << mem_doc_res.error().message.view();
		EXPECT_EQ((*mem_doc_res)->description(), doc.description());
	}

	// 3. Encrypted corpus files (`RC4-128`, `AES-128`, `AES-256`) return
	//    `ErrorCode::NeedsPassword` on `open(path)` and `open_memory(bytes)`,
	//    `ErrorCode::WrongPassword` on wrong password, and unlock cleanly with
	//    `"recto-test"` (user) or `"recto-owner"` (owner):
	struct EncryptedCase {
		const char* rel_path;
		CryptFilterMethod expected_method;
		int32_t expected_v;
		int32_t expected_r;
		int32_t expected_bits;
	};
	const EncryptedCase encrypted_cases[] = {
		{"render/encrypted-rc4-128.pdf", CryptFilterMethod::V2, 2, 3, 128},
		{"render/encrypted-aes-128.pdf", CryptFilterMethod::AESV2, 4, 4, 128},
		{"render/encrypted-aes-256.pdf", CryptFilterMethod::AESV3, 5, 6, 256},
	};

	for (const auto& tc : encrypted_cases) {
		SCOPED_TRACE(tc.rel_path);
		const auto path = recto::test::corpus_path(tc.rel_path);
		const auto bytes = read_corpus_bytes(tc.rel_path);

		// `open(path)` without password fails with `NeedsPassword`:
		auto open_res = Document::open(path);
		ASSERT_FALSE(open_res.has_value());
		EXPECT_EQ(open_res.error().code, ErrorCode::NeedsPassword);

		// `open_memory(bytes)` without password fails with `NeedsPassword`:
		auto open_mem_res = Document::open_memory(bytes);
		ASSERT_FALSE(open_mem_res.has_value());
		EXPECT_EQ(open_mem_res.error().code, ErrorCode::NeedsPassword);

		// `authenticate(path, "wrong")` fails with `WrongPassword`:
		auto wrong_res = Document::authenticate(path, "wrong-password");
		ASSERT_FALSE(wrong_res.has_value());
		EXPECT_EQ(wrong_res.error().code, ErrorCode::WrongPassword);

		// `authenticate(path, "recto-test")` unlocks as user:
		auto user_res = Document::authenticate(path, "recto-test");
		ASSERT_TRUE(user_res.has_value()) << user_res.error().message.view();
		const Document& user_doc = **user_res;
		EXPECT_TRUE(user_doc.is_encrypted());
		EXPECT_TRUE(user_doc.is_authenticated());
		EXPECT_FALSE(user_doc.needs_password());
		EXPECT_FALSE(user_doc.description().unlocked_as_owner);
		EXPECT_EQ(user_doc.description().encryption_method, tc.expected_method);
		EXPECT_EQ(user_doc.description().encryption_v, tc.expected_v);
		EXPECT_EQ(user_doc.description().encryption_r, tc.expected_r);
		EXPECT_EQ(user_doc.description().encryption_key_bits, tc.expected_bits);
		EXPECT_EQ(user_doc.page_count(), 1u);
		EXPECT_FALSE(user_doc.metadata().title.empty());

		auto contents_res = user_doc.decoded_page_contents(0);
		ASSERT_TRUE(contents_res.has_value()) << contents_res.error().message.view();
		const std::string_view cs_sv(
			reinterpret_cast<const char*>(contents_res->data()),
			contents_res->size());
		EXPECT_NE(cs_sv.find("BT"), std::string_view::npos);

		// Two-step `open_unauthenticated` -> `doc->authenticate("recto-owner")`
		// unlocks with full owner permissions:
		auto unauth_res = Document::open_unauthenticated(path);
		ASSERT_TRUE(unauth_res.has_value()) << unauth_res.error().message.view();
		Document& two_step_doc = **unauth_res;
		EXPECT_TRUE(two_step_doc.is_encrypted());
		EXPECT_FALSE(two_step_doc.is_authenticated());
		EXPECT_TRUE(two_step_doc.needs_password());

		auto bad_step_res = two_step_doc.authenticate("bad");
		ASSERT_FALSE(bad_step_res.has_value());
		EXPECT_EQ(bad_step_res.error().code, ErrorCode::WrongPassword);

		auto owner_step_res = two_step_doc.authenticate("recto-owner");
		ASSERT_TRUE(owner_step_res.has_value()) << owner_step_res.error().message.view();
		EXPECT_TRUE(two_step_doc.is_authenticated());
		EXPECT_TRUE(two_step_doc.description().unlocked_as_owner);
		EXPECT_TRUE(two_step_doc.permissions().can_modify);
		EXPECT_EQ(two_step_doc.page_count(), 1u);
	}
}

TEST(PdfDocument, PageTreeAndRotatedPages)
{
	// 1. `render/rotated-pages.pdf` has 4 pages with rotations 0, 90, 180, 270
	//    and distinct MediaBox/CropBox dimensions:
	{
		auto doc_res = Document::open(
			recto::test::corpus_path("render/rotated-pages.pdf"));
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;
		ASSERT_EQ(doc.page_count(), 4u);

		const auto& pages = doc.pages();
		ASSERT_EQ(pages.size(), 4u);

		EXPECT_EQ(pages[0].page_index, 0u);
		EXPECT_EQ(pages[0].rotation, 0);
		EXPECT_DOUBLE_EQ(pages[0].media_box.width(), 612.0);
		EXPECT_DOUBLE_EQ(pages[0].media_box.height(), 792.0);
		EXPECT_DOUBLE_EQ(pages[0].crop_box.width(), 612.0);
		EXPECT_DOUBLE_EQ(pages[0].crop_box.height(), 792.0);

		EXPECT_EQ(pages[1].page_index, 1u);
		EXPECT_EQ(pages[1].rotation, 90);
		EXPECT_DOUBLE_EQ(pages[1].media_box.width(), 612.0);
		EXPECT_DOUBLE_EQ(pages[1].media_box.height(), 792.0);
		EXPECT_DOUBLE_EQ(pages[1].crop_box.x_min, 36.0);
		EXPECT_DOUBLE_EQ(pages[1].crop_box.y_min, 36.0);
		EXPECT_DOUBLE_EQ(pages[1].crop_box.x_max, 576.0);
		EXPECT_DOUBLE_EQ(pages[1].crop_box.y_max, 756.0);

		EXPECT_EQ(pages[2].page_index, 2u);
		EXPECT_EQ(pages[2].rotation, 180);
		EXPECT_DOUBLE_EQ(pages[2].media_box.width(), 595.0);
		EXPECT_DOUBLE_EQ(pages[2].media_box.height(), 842.0);
		EXPECT_DOUBLE_EQ(pages[2].crop_box.x_min, 18.0);
		EXPECT_DOUBLE_EQ(pages[2].crop_box.y_min, 18.0);
		EXPECT_DOUBLE_EQ(pages[2].crop_box.x_max, 577.0);
		EXPECT_DOUBLE_EQ(pages[2].crop_box.y_max, 824.0);

		EXPECT_EQ(pages[3].page_index, 3u);
		EXPECT_EQ(pages[3].rotation, 270);
		EXPECT_DOUBLE_EQ(pages[3].media_box.width(), 842.0);
		EXPECT_DOUBLE_EQ(pages[3].media_box.height(), 595.0);
		EXPECT_DOUBLE_EQ(pages[3].crop_box.x_min, 24.0);
		EXPECT_DOUBLE_EQ(pages[3].crop_box.y_min, 24.0);
		EXPECT_DOUBLE_EQ(pages[3].crop_box.x_max, 818.0);
		EXPECT_DOUBLE_EQ(pages[3].crop_box.y_max, 571.0);
	}

	// 2. Inherited `/MediaBox`, `/CropBox`, `/Rotate` (including negative and
	//    >360 multiples of 90), and `/Resources` from intermediate `/Pages` nodes
	//    per ISO 32000-1 section 7.7.3.4 (explicit `/Resources` on a child replaces
	//    the ancestor's `/Resources` dictionary rather than merging it, while an
	//    omitted `/Resources` inherits the ancestor's dictionary):
	{
		const auto bytes = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2,
				 "<< /Type /Pages /Count 3 /MediaBox [0 0 500 700] /Rotate -90 "
				 "/Resources << /Font << /F1 10 0 R >> >> /Kids [3 0 R 4 0 R 6 0 R] >>"},
				{3,
				 "<< /Type /Pages /Parent 2 0 R /Count 1 /CropBox [10 20 490 680] "
				 "/Resources << /Font << /F2 11 0 R >> >> /Kids [5 0 R] >>"},
				{4,
				 "<< /Type /Page /Parent 2 0 R /Rotate 450 /MediaBox [0 0 300 400] >>"},
				{5, "<< /Type /Page /Parent 3 0 R >>"},
				{6, "<< /Type /Page /Parent 2 0 R /Resources << >> >>"},
				{10, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"},
				{11, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>"},
			});
		auto doc_res = Document::open_memory(bytes);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;
		ASSERT_EQ(doc.page_count(), 3u);

		// Page 0 (obj 5) inherits MediaBox [0 0 500 700] and Rotate -90 -> 270
		// from obj 2, and inherits CropBox [10 20 490 680] and `/Resources`
		// (`/F2` replacing `/F1`) from intermediate `/Pages` node obj 3:
		EXPECT_DOUBLE_EQ(doc.pages()[0].media_box.width(), 500.0);
		EXPECT_DOUBLE_EQ(doc.pages()[0].media_box.height(), 700.0);
		EXPECT_DOUBLE_EQ(doc.pages()[0].crop_box.width(), 480.0);
		EXPECT_DOUBLE_EQ(doc.pages()[0].crop_box.height(), 660.0);
		EXPECT_EQ(doc.pages()[0].rotation, 270);

		auto res0 = doc.page_resources(0);
		ASSERT_TRUE(res0.has_value()) << res0.error().message.view();
		const PdfDict* font_dict0 = res0->get_dict("Font");
		ASSERT_NE(font_dict0, nullptr);
		EXPECT_FALSE(font_dict0->contains("F1"));
		EXPECT_TRUE(font_dict0->contains("F2"));

		// Page 1 (obj 4) omits `/Resources` so it inherits `/F1` from obj 2, and
		// overrides MediaBox [0 0 300 400] and Rotate 450 -> 90:
		EXPECT_DOUBLE_EQ(doc.pages()[1].media_box.width(), 300.0);
		EXPECT_DOUBLE_EQ(doc.pages()[1].media_box.height(), 400.0);
		EXPECT_EQ(doc.pages()[1].rotation, 90);
		auto res1 = doc.page_resources(1);
		ASSERT_TRUE(res1.has_value()) << res1.error().message.view();
		const PdfDict* font_dict1 = res1->get_dict("Font");
		ASSERT_NE(font_dict1, nullptr);
		EXPECT_TRUE(font_dict1->contains("F1"));
		EXPECT_FALSE(font_dict1->contains("F2"));

		// Page 2 (obj 6) supplies an explicit empty `/Resources << >>`, replacing
		// obj 2's `/Resources` with an empty dictionary:
		auto res2 = doc.page_resources(2);
		ASSERT_TRUE(res2.has_value()) << res2.error().message.view();
		EXPECT_EQ(res2->get_dict("Font"), nullptr);
	}

	// 3. `broken/cyclic-pages-tree.pdf` fails cleanly with `ErrorCode::DamagedFormat`:
	{
		auto cyclic_res = Document::open(
			recto::test::corpus_path("broken/cyclic-pages-tree.pdf"));
		ASSERT_FALSE(cyclic_res.has_value());
		EXPECT_EQ(cyclic_res.error().code, ErrorCode::DamagedFormat);
	}

	// 4. `/Pages` tree exceeding 64 levels of nesting fails with `RecursionLimitExceeded`:
	{
		std::vector<std::pair<uint32_t, std::string>> objs;
		objs.emplace_back(1u, "<< /Type /Catalog /Pages 2 0 R >>");
		constexpr uint32_t kDeepCount = 67u;
		for (uint32_t d = 0; d < kDeepCount; ++d) {
			const uint32_t obj_id = 2u + d;
			const uint32_t child_id = obj_id + 1u;
			objs.emplace_back(
				obj_id,
				"<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [" +
					std::to_string(child_id) + " 0 R] >>");
		}
		objs.emplace_back(
			2u + kDeepCount,
			"<< /Type /Page /Parent " + std::to_string(1u + kDeepCount) + " 0 R >>");

		const auto deep_bytes = build_synthetic_pdf_from_objects("1.7", objs);
		auto deep_res = Document::open_memory(deep_bytes);
		ASSERT_FALSE(deep_res.has_value());
		EXPECT_EQ(deep_res.error().code, ErrorCode::RecursionLimitExceeded);
	}
}

TEST(PdfDocument, PageContents)
{
	// 1. Multi-stream `/Contents` array in `render/incremental-2rev.pdf` and
	//    `seeds/seed-filters-ascii-rle.pdf` decodes cleanly:
	{
		auto inc_res = Document::open(
			recto::test::corpus_path("render/incremental-2rev.pdf"));
		ASSERT_TRUE(inc_res.has_value()) << inc_res.error().message.view();
		auto cs_res = (*inc_res)->decoded_page_contents(0);
		ASSERT_TRUE(cs_res.has_value()) << cs_res.error().message.view();
		const std::string_view sv(
			reinterpret_cast<const char*>(cs_res->data()),
			cs_res->size());
		EXPECT_NE(sv.find("Revision 1"), std::string_view::npos);
		EXPECT_NE(sv.find("Revision 2"), std::string_view::npos);
	}

	{
		auto seed_res = Document::open(
			recto::test::corpus_path("seeds/seed-filters-ascii-rle.pdf"));
		ASSERT_TRUE(seed_res.has_value()) << seed_res.error().message.view();
		auto cs_res = (*seed_res)->decoded_page_contents(0);
		ASSERT_TRUE(cs_res.has_value()) << cs_res.error().message.view();
		const std::string_view sv(
			reinterpret_cast<const char*>(cs_res->data()),
			cs_res->size());
		EXPECT_NE(sv.find("ASCIIHex"), std::string_view::npos);
		EXPECT_NE(sv.find("ASCII85"), std::string_view::npos);
	}

	// 2. Verify that adjacent streams without trailing/leading whitespace get a
	//    single whitespace byte inserted between them so tokens (`100 20` and
	//    `30 m`) do not fuse into `100 2030 m`:
	{
		const auto bytes = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R /Contents [4 0 R 5 0 R] >>"},
				{4, "<< /Length 6 >>\nstream\n100 20\nendstream"},
				{5, "<< /Length 4 >>\nstream\n30 m\nendstream"},
			});
		auto doc_res = Document::open_memory(bytes);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		auto cs_res = (*doc_res)->decoded_page_contents(0);
		ASSERT_TRUE(cs_res.has_value()) << cs_res.error().message.view();
		const std::string combined(
			reinterpret_cast<const char*>(cs_res->data()),
			cs_res->size());
		EXPECT_EQ(combined, "100 20\n30 m");
	}
}

TEST(PdfDocument, Metadata)
{
	// 1. Direct unit tests for `parse_pdf_date`:
	{
		const auto d1 = parse_pdf_date("D:20261013120000+02'30'");
		ASSERT_TRUE(d1.has_value());
		EXPECT_EQ(d1->year, 2026);
		EXPECT_EQ(d1->month, 10);
		EXPECT_EQ(d1->day, 13);
		EXPECT_EQ(d1->hour, 12);
		EXPECT_EQ(d1->minute, 0);
		EXPECT_EQ(d1->second, 0);
		EXPECT_TRUE(d1->has_timezone);
		EXPECT_EQ(d1->utc_offset_minutes, 150);
		EXPECT_EQ(d1->iso8601, "2026-10-13T12:00:00+02:30");

		const auto d2 = parse_pdf_date("D:20261013120000Z");
		ASSERT_TRUE(d2.has_value());
		EXPECT_TRUE(d2->has_timezone);
		EXPECT_EQ(d2->utc_offset_minutes, 0);
		EXPECT_EQ(d2->iso8601, "2026-10-13T12:00:00Z");

		const auto d3 = parse_pdf_date("D:20261013081530-05'00'");
		ASSERT_TRUE(d3.has_value());
		EXPECT_EQ(d3->utc_offset_minutes, -300);
		EXPECT_EQ(d3->iso8601, "2026-10-13T08:15:30-05:00");

		const auto d4 = parse_pdf_date("2026-10-13T12:00:00Z");
		ASSERT_TRUE(d4.has_value());
		EXPECT_EQ(d4->year, 2026);
		EXPECT_EQ(d4->month, 10);
		EXPECT_EQ(d4->day, 13);
		EXPECT_EQ(d4->iso8601, "2026-10-13T12:00:00Z");

		EXPECT_FALSE(parse_pdf_date("not-a-date").has_value());
		EXPECT_FALSE(parse_pdf_date("D:20261301000000Z").has_value()); // month 13
		EXPECT_FALSE(parse_pdf_date("D:20260230000000Z").has_value()); // Feb 30
	}

	// 2. Read `/Info` metadata from `render/simple-1p.pdf`:
	{
		auto doc_res = Document::open(
			recto::test::corpus_path("render/simple-1p.pdf"));
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;
		EXPECT_EQ(doc.pdf_version(), "1.7");
		EXPECT_EQ(doc.metadata().title, "Recto Simple 1P");
		EXPECT_EQ(doc.metadata().author, "Recto Corpus Generator");
		EXPECT_EQ(doc.metadata().creator, "scripts/build-corpus.py");
		ASSERT_TRUE(doc.metadata().parsed_creation_date.has_value());
		EXPECT_EQ(doc.metadata().parsed_creation_date->year, 2026);
		EXPECT_EQ(doc.metadata().parsed_creation_date->month, 10);
		EXPECT_EQ(doc.metadata().parsed_creation_date->day, 13);
		EXPECT_EQ(doc.metadata().parsed_creation_date->iso8601, "2026-10-13T00:00:00Z");
	}

	// 3. UTF-16BE `/Info` strings, `/Catalog /Version` override (`2.0`), and
	//    `/Catalog /Metadata` XMP fallback (`dc:title`, `dc:creator`,
	//    `dc:description`, `pdf:Keywords`, `xmp:CreatorTool`, `pdf:Producer`):
	{
		const std::string xmp_packet =
			"<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'?>\n"
			"<x:xmpmeta xmlns:x='adobe:ns:meta/'>\n"
			" <rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>\n"
			"  <rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/'\n"
			"                   xmlns:xmp='http://ns.adobe.com/xap/1.0/'\n"
			"                   xmlns:pdf='http://ns.adobe.com/pdf/1.3/'>\n"
			"   <dc:title><rdf:Alt><rdf:li xml:lang='x-default'>XMP Fallback Title &amp; Co</rdf:li></rdf:Alt></dc:title>\n"
			"   <dc:creator><rdf:Seq><rdf:li>Ada Lovelace</rdf:li><rdf:li>Grace Hopper</rdf:li></rdf:Seq></dc:creator>\n"
			"   <dc:description><rdf:Alt><rdf:li xml:lang='x-default'>XMP Subject Line</rdf:li></rdf:Alt></dc:description>\n"
			"   <pdf:Keywords>xmp, fallback, metadata</pdf:Keywords>\n"
			"   <xmp:CreatorTool>Recto XMP Generator</xmp:CreatorTool>\n"
			"   <pdf:Producer>Recto Native Producer</pdf:Producer>\n"
			"   <xmp:CreateDate>2026-10-19T14:30:00+02:00</xmp:CreateDate>\n"
			"  </rdf:Description>\n"
			" </rdf:RDF>\n"
			"</x:xmpmeta>\n";

		const std::string meta_stream_obj =
			"<< /Type /Metadata /Subtype /XML /Length " +
			std::to_string(xmp_packet.size()) + " >>\nstream\n" +
			xmp_packet + "\nendstream";

		const auto bytes = build_synthetic_pdf_from_objects(
			"1.4",
			{
				{1, "<< /Type /Catalog /Version /2.0 /Pages 2 0 R /Metadata 4 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{4, meta_stream_obj},
			});
		auto doc_res = Document::open_memory(bytes);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;
		EXPECT_EQ(doc.pdf_version(), "2.0");
		EXPECT_EQ(doc.metadata().title, "XMP Fallback Title & Co");
		EXPECT_EQ(doc.metadata().author, "Ada Lovelace, Grace Hopper");
		EXPECT_EQ(doc.metadata().subject, "XMP Subject Line");
		EXPECT_EQ(doc.metadata().keywords, "xmp, fallback, metadata");
		EXPECT_EQ(doc.metadata().creator, "Recto XMP Generator");
		EXPECT_EQ(doc.metadata().producer, "Recto Native Producer");
		ASSERT_TRUE(doc.metadata().parsed_creation_date.has_value());
		EXPECT_EQ(doc.metadata().parsed_creation_date->iso8601, "2026-10-19T14:30:00+02:00");
	}
}

TEST(PdfDocument, OutlinesAndNamedDests)
{
	// 1. `text/two-column-links-outline.pdf` has a multi-level outline with
	//    explicit `[page /XYZ left top zoom]` destinations and named destination
	//    (`sec-summary` in `/Catalog /Names /Dests`):
	{
		auto doc_res = Document::open(
			recto::test::corpus_path("text/two-column-links-outline.pdf"));
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;

		const auto& outline = doc.outline();
		ASSERT_EQ(outline.size(), 2u);

		EXPECT_EQ(outline[0].title, "1. Introduction");
		EXPECT_EQ(outline[0].page_index, 0);
		EXPECT_DOUBLE_EQ(outline[0].dest_x_pt, 72.0);
		EXPECT_DOUBLE_EQ(outline[0].dest_y_pt, 730.0);
		EXPECT_TRUE(outline[0].is_open);
		ASSERT_EQ(outline[0].children.size(), 1u);
		EXPECT_EQ(outline[0].children[0].title, "1.1 Architecture");
		EXPECT_EQ(outline[0].children[0].page_index, 0);
		EXPECT_DOUBLE_EQ(outline[0].children[0].dest_x_pt, 332.0);
		EXPECT_DOUBLE_EQ(outline[0].children[0].dest_y_pt, 686.0);

		EXPECT_EQ(outline[1].title, "2. Second Page Details");
		EXPECT_EQ(outline[1].page_index, 1);
		EXPECT_DOUBLE_EQ(outline[1].dest_x_pt, 72.0);
		EXPECT_DOUBLE_EQ(outline[1].dest_y_pt, 730.0);
		EXPECT_TRUE(outline[1].children.empty());
	}

	// 2. `seeds/seed-outlines-links.pdf` tests named destination lookup
	//    (`(sec1)` in `/Catalog /Names /Dests`):
	{
		auto seed_res = Document::open(
			recto::test::corpus_path("seeds/seed-outlines-links.pdf"));
		ASSERT_TRUE(seed_res.has_value()) << seed_res.error().message.view();
		const Document& doc = **seed_res;
		const auto& outline = doc.outline();
		ASSERT_EQ(outline.size(), 1u);
		EXPECT_EQ(outline[0].title, "Section 1");
		EXPECT_EQ(outline[0].page_index, 0);
		EXPECT_DOUBLE_EQ(outline[0].dest_x_pt, 48.0);
		EXPECT_DOUBLE_EQ(outline[0].dest_y_pt, 750.0);
		EXPECT_TRUE(outline[0].is_open);
	}

	// 3. Closed subtrees (`/Count < 0`), `/Catalog /Dests` dictionary lookup,
	//    and cyclic `/Outlines` (`/Next` pointing back to `/First`) terminate
	//    cleanly without infinite looping:
	{
		const auto bytes = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1,
				 "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R "
				 "/Dests << /named_top [3 0 R /FitH 710] >> >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{4, "<< /Type /Outlines /First 5 0 R /Last 6 0 R /Count 2 >>"},
				{5, "<< /Title (Node A) /Parent 4 0 R /Next 6 0 R /Count -1 /Dest /named_top >>"},
				{6, "<< /Title (Node B) /Parent 4 0 R /Next 5 0 R /Dest [3 0 R /Fit] >>"},
			});
		auto doc_res = Document::open_memory(bytes);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		ASSERT_EQ((*doc_res)->outline().size(), 2u);
		EXPECT_EQ((*doc_res)->outline()[0].title, "Node A");
		EXPECT_FALSE((*doc_res)->outline()[0].is_open);
		EXPECT_EQ((*doc_res)->outline()[0].page_index, 0);
		EXPECT_DOUBLE_EQ((*doc_res)->outline()[0].dest_y_pt, 710.0);
		EXPECT_EQ((*doc_res)->outline()[1].title, "Node B");
	}
}

TEST(PdfDocument, LinksAndRefusedActions)
{
	// 1. `text/two-column-links-outline.pdf` page 0 has an internal `/Dest` link
	//    to page 1 (`y = 730`) and an external `https://example.com/recto-docs`
	//    `/URI` link:
	{
		auto doc_res = Document::open(
			recto::test::corpus_path("text/two-column-links-outline.pdf"));
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;

		auto links0_res = doc.page_links(0);
		ASSERT_TRUE(links0_res.has_value()) << links0_res.error().message.view();
		const auto& links0 = *links0_res;
		ASSERT_EQ(links0.size(), 2u);

		EXPECT_EQ(links0[0].kind, LinkKind::Internal);
		EXPECT_FALSE(links0[0].is_inert);
		EXPECT_EQ(links0[0].page_index, 1);
		EXPECT_DOUBLE_EQ(links0[0].y_pt, 730.0);
		EXPECT_DOUBLE_EQ(links0[0].rect_pt.x_min, 72.0);
		EXPECT_DOUBLE_EQ(links0[0].rect_pt.y_min, 648.0);

		EXPECT_EQ(links0[1].kind, LinkKind::ExternalUri);
		EXPECT_FALSE(links0[1].is_inert);
		EXPECT_EQ(links0[1].uri, "https://example.com/recto-docs");

		auto links1_res = doc.page_links(1);
		ASSERT_TRUE(links1_res.has_value()) << links1_res.error().message.view();
		EXPECT_TRUE(links1_res->empty());
	}

	// 2. `seeds/seed-outlines-links.pdf` and crafted `/Launch`, `/JavaScript`,
	//    chained `/Next` `/Launch`, and `javascript:` URI links are flagged as
	//    `LinkKind::RefusedAction` (`InertAction`) and `is_inert == true`:
	{
		auto seed_res = Document::open(
			recto::test::corpus_path("seeds/seed-outlines-links.pdf"));
		ASSERT_TRUE(seed_res.has_value()) << seed_res.error().message.view();
		auto links_res = (*seed_res)->page_links(0);
		ASSERT_TRUE(links_res.has_value()) << links_res.error().message.view();
		const auto& links = *links_res;
		ASSERT_EQ(links.size(), 3u);

		EXPECT_EQ(links[0].kind, LinkKind::ExternalUri);
		EXPECT_EQ(links[0].uri, "https://example.com/recto");

		EXPECT_EQ(links[1].kind, LinkKind::RefusedAction);
		EXPECT_EQ(links[1].kind, LinkKind::InertAction);
		EXPECT_TRUE(links[1].is_inert);
		EXPECT_EQ(links[1].action_type, "JavaScript");

		EXPECT_EQ(links[2].kind, LinkKind::RefusedAction);
		EXPECT_TRUE(links[2].is_inert);
		EXPECT_EQ(links[2].action_type, "Launch");
	}

	// 3. Chained `/Next` action smuggling `/Launch` (directly, inside a `/Next`
	//    array's child `/Next`, or after 17+ `/GoTo` hops) and unsafe
	//    `javascript:` URI schemes (both top-level and chained under `/GoTo`) are
	//    all refused as `LinkKind::RefusedAction` (`is_inert == true`):
	{
		std::vector<std::pair<uint32_t, std::string>> objs = {
			{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
			{3, "<< /Type /Page /Parent 2 0 R /Annots [4 0 R 5 0 R 6 0 R 7 0 R 8 0 R] >>"},
			{4,
			 "<< /Type /Annot /Subtype /Link /Rect [10 10 100 30] "
			 "/A << /S /GoTo /D [3 0 R /Fit] /Next << /S /Launch /F (cmd.exe) >> >> >>"},
			{5,
			 "<< /Type /Annot /Subtype /Link /Rect [10 40 100 60] "
			 "/A << /S /URI /URI (javascript:alert(1)) >> >>"},
			{6,
			 "<< /Type /Annot /Subtype /Link /Rect [10 70 100 90] "
			 "/A << /S /GoTo /D [3 0 R /Fit] /Next [<< /S /GoTo /D [3 0 R /Fit] /Next << /S /Launch /F (powershell.exe) >> >>] >> >>"},
			{7,
			 "<< /Type /Annot /Subtype /Link /Rect [10 100 100 120] "
			 "/A << /S /GoTo /D [3 0 R /Fit] /Next << /S /URI /URI (javascript:alert(2)) >> >> >>"},
			{8,
			 "<< /Type /Annot /Subtype /Link /Rect [10 130 100 150] "
			 "/A 20 0 R >>"},
		};
		// Build a 17-step `/GoTo` chain (`20 0 R` -> `21 0 R` -> ... -> `36 0 R`)
		// followed by `/Launch` at `37 0 R`:
		for (uint32_t id = 20u; id < 37u; ++id) {
			objs.emplace_back(
				id,
				"<< /S /GoTo /D [3 0 R /Fit] /Next " + std::to_string(id + 1u) + " 0 R >>");
		}
		objs.emplace_back(37u, "<< /S /Launch /F (calc.exe) >>");

		const auto bytes = build_synthetic_pdf_from_objects("1.7", objs);
		auto doc_res = Document::open_memory(bytes);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		auto links_res = (*doc_res)->page_links(0);
		ASSERT_TRUE(links_res.has_value()) << links_res.error().message.view();
		ASSERT_EQ(links_res->size(), 5u);
		for (size_t i = 0; i < links_res->size(); ++i) {
			SCOPED_TRACE(i);
			EXPECT_EQ((*links_res)[i].kind, LinkKind::RefusedAction);
			EXPECT_TRUE((*links_res)[i].is_inert);
		}
	}

	// 4. Bounded allocation and node-budget enforcement on wide `/Next` arrays
	//    (`128` indirect aliases within budget vs `2,048` aliases and two-level
	//    `16 x 20` fan-out exceeding the 256-node budget):
	{
		auto get_working_set_bytes = []() -> size_t {
			PROCESS_MEMORY_COUNTERS pmc{};
			pmc.cb = sizeof(pmc);
			if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc)) != FALSE) {
				return static_cast<size_t>(pmc.WorkingSetSize);
			}
			return 0u;
		};

		const std::string payload_64k(65536u, 'A');

		// 4a. 128 indirect aliases to a 64 KiB `/GoTo` dictionary are within the
		//     256-node budget, deduplicate the target dictionary, and remain an
		//     active `LinkKind::Internal` link:
		{
			std::string next_arr_128 = "[";
			for (uint32_t id = 20u; id < 20u + 128u; ++id) {
				next_arr_128 += std::to_string(id) + " 0 R ";
			}
			next_arr_128 += "]";

			std::vector<std::pair<uint32_t, std::string>> objs_128 = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>"},
				{4,
				 "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] "
				 "/A << /S /GoTo /D [3 0 R /Fit] /Next " + next_arr_128 + " >> >>"},
				{10, "<< /S /GoTo /D [3 0 R /Fit] /Payload (" + payload_64k + ") >>"},
			};
			for (uint32_t id = 20u; id < 20u + 128u; ++id) {
				objs_128.emplace_back(id, "10 0 R");
			}

			const auto bytes_128 = build_synthetic_pdf_from_objects("1.7", objs_128);
			auto doc_128 = Document::open_memory(bytes_128);
			ASSERT_TRUE(doc_128.has_value()) << doc_128.error().message.view();
			auto links_128 = (*doc_128)->page_links(0);
			ASSERT_TRUE(links_128.has_value()) << links_128.error().message.view();
			ASSERT_EQ(links_128->size(), 1u);
			EXPECT_EQ((*links_128)[0].kind, LinkKind::Internal);
			EXPECT_FALSE((*links_128)[0].is_inert);
		}

		// 4b. 2,048 indirect aliases to a 64 KiB dictionary (plus a trailing
		//     `/Launch` sentinel past the 256-node budget) and a two-level
		//     `16 x 20` fan-out (`337` total nodes) must fail closed before
		//     resolving children beyond the 256-node budget, keeping working-set
		//     growth bounded (< 32 MiB) and never reaching the trailing `/Launch`
		//     sentinel:
		{
			std::string next_arr_2048 = "[";
			for (uint32_t id = 20u; id < 20u + 2048u; ++id) {
				next_arr_2048 += std::to_string(id) + " 0 R ";
			}
			next_arr_2048 += "]";

			std::vector<std::pair<uint32_t, std::string>> objs_2048 = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R /Annots [4 0 R 5 0 R] >>"},
				{4,
				 "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] "
				 "/A << /S /GoTo /D [3 0 R /Fit] /Next " + next_arr_2048 + " >> >>"},
				{10, "<< /S /GoTo /D [3 0 R /Fit] /Payload (" + payload_64k + ") >>"},
				{11, "<< /S /Launch /F (cmd.exe) >>"},
			};
			for (uint32_t id = 20u; id < 20u + 2047u; ++id) {
				objs_2048.emplace_back(id, "10 0 R");
			}
			objs_2048.emplace_back(20u + 2047u, "11 0 R");

			// Second link (5 0 R): two-level fan-out with 16 child actions
			// (3000..3015), each having a 20-element `/Next` array (total 337
			// nodes), with the final branch pointing to `/Launch` (`11 0 R`):
			std::string level1_arr = "[";
			for (uint32_t g = 0u; g < 16u; ++g) {
				const uint32_t parent_id = 3000u + g;
				level1_arr += std::to_string(parent_id) + " 0 R ";
				std::string level2_arr = "[";
				for (uint32_t k = 0u; k < 20u; ++k) {
					const uint32_t leaf_alias = (g == 15u && k == 19u) ? 11u : 10u;
					level2_arr += std::to_string(leaf_alias) + " 0 R ";
				}
				level2_arr += "]";
				objs_2048.emplace_back(
					parent_id,
					"<< /S /GoTo /D [3 0 R /Fit] /Next " + level2_arr + " >>");
			}
			level1_arr += "]";
			objs_2048.emplace_back(
				5u,
				"<< /Type /Annot /Subtype /Link /Rect [20 20 30 30] "
				"/A << /S /GoTo /D [3 0 R /Fit] /Next " + level1_arr + " >> >>");

			const auto bytes_2048 = build_synthetic_pdf_from_objects("1.7", objs_2048);
			clear_diagnostics();
			const size_t ws_before = get_working_set_bytes();

			auto doc_2048 = Document::open_memory(bytes_2048);
			ASSERT_TRUE(doc_2048.has_value()) << doc_2048.error().message.view();

			const size_t ws_after = get_working_set_bytes();
			const size_t ws_delta = (ws_after > ws_before) ? (ws_after - ws_before) : 0u;
			EXPECT_LT(ws_delta, 32u * 1024u * 1024u)
				<< "Unbounded /Next array expansion caused excessive working-set growth: "
				<< ws_delta << " bytes";

			auto links_2048 = (*doc_2048)->page_links(0);
			ASSERT_TRUE(links_2048.has_value()) << links_2048.error().message.view();
			ASSERT_EQ(links_2048->size(), 2u);
			EXPECT_EQ((*links_2048)[0].kind, LinkKind::RefusedAction);
			EXPECT_TRUE((*links_2048)[0].is_inert);
			EXPECT_EQ((*links_2048)[1].kind, LinkKind::RefusedAction);
			EXPECT_TRUE((*links_2048)[1].is_inert);

			// Verify that the trailing `/Launch` sentinel (`11 0 R`, placed past
			// the 256-node budget in both links) was never resolved or inspected:
			const auto diags = diagnostics_tail();
			for (const auto& d : diags) {
				EXPECT_NE(d.diagnostic_id, DiagnosticId::BlockedLaunchAction)
					<< "Node past the 256-node budget should not have been resolved";
			}
		}
	}
}

TEST(PdfDocument, FontInventory)
{
	// 1. Corpus font inventory on `render/simple-1p.pdf`,
	//    `text/two-column-links-outline.pdf`, and `convert/tables-and-forms.pdf`:
	{
		auto doc_res = Document::open(
			recto::test::corpus_path("render/simple-1p.pdf"));
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const auto& fonts = (*doc_res)->fonts();
		ASSERT_EQ(fonts.size(), 1u);
		EXPECT_EQ(fonts[0].name, "Helvetica");
		EXPECT_EQ(fonts[0].subtype, "Type1");
		EXPECT_EQ(fonts[0].encoding, "WinAnsiEncoding");
		EXPECT_FALSE(fonts[0].is_embedded);
		EXPECT_FALSE(fonts[0].is_subset);
	}

	{
		auto tc_res = Document::open(
			recto::test::corpus_path("text/two-column-links-outline.pdf"));
		ASSERT_TRUE(tc_res.has_value()) << tc_res.error().message.view();
		const auto& fonts = (*tc_res)->fonts();
		ASSERT_EQ(fonts.size(), 1u);
		EXPECT_EQ(fonts[0].name, "Helvetica");
		EXPECT_EQ(fonts[0].subtype, "Type1");
		EXPECT_EQ(fonts[0].encoding, "WinAnsiEncoding");
		EXPECT_TRUE(fonts[0].has_to_unicode);
		EXPECT_TRUE(fonts[0].has_differences);
	}

	{
		auto tf_res = Document::open(
			recto::test::corpus_path("convert/tables-and-forms.pdf"));
		ASSERT_TRUE(tf_res.has_value()) << tf_res.error().message.view();
		const auto& fonts = (*tf_res)->fonts();
		ASSERT_EQ(fonts.size(), 2u);
		EXPECT_EQ(fonts[0].name, "Helvetica");
		EXPECT_EQ(fonts[1].name, "ZapfDingbats");
	}

	// 2. Subset prefix `ABCDEF+`, embedded `/FontDescriptor /FontFile2`, and
	//    nested Form XObject `/Resources /Font` deduplication:
	{
		const auto bytes = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3,
				 "<< /Type /Page /Parent 2 0 R /Resources << "
				 "/Font << /F1 4 0 R >> /XObject << /Fm1 7 0 R >> >> >>"},
				{4,
				 "<< /Type /Font /Subtype /TrueType /BaseFont /ABCDEF+Inter-Regular "
				 "/Encoding /WinAnsiEncoding /FontDescriptor 5 0 R >>"},
				{5, "<< /Type /FontDescriptor /FontName /ABCDEF+Inter-Regular /FontFile2 6 0 R >>"},
				{6, "<< /Length 4 >>\nstream\nTTF!\nendstream"},
				{7,
				 "<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] "
				 "/Resources << /Font << /F1 4 0 R /F2 8 0 R >> >> /Length 2 >>\nstream\nQ\nendstream"},
				{8,
				 "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman "
				 "/Encoding << /Type /Encoding /BaseEncoding /MacRomanEncoding /Differences [128 /Euro] >> >>"},
			});
		auto doc_res = Document::open_memory(bytes);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const auto& fonts = (*doc_res)->fonts();
		ASSERT_EQ(fonts.size(), 2u);

		EXPECT_EQ(fonts[0].name, "ABCDEF+Inter-Regular");
		EXPECT_EQ(fonts[0].clean_name, "Inter-Regular");
		EXPECT_EQ(fonts[0].subtype, "TrueType");
		EXPECT_EQ(fonts[0].encoding, "WinAnsiEncoding");
		EXPECT_TRUE(fonts[0].is_embedded);
		EXPECT_TRUE(fonts[0].is_subset);

		EXPECT_EQ(fonts[1].name, "Times-Roman");
		EXPECT_EQ(fonts[1].clean_name, "Times-Roman");
		EXPECT_EQ(fonts[1].subtype, "Type1");
		EXPECT_EQ(fonts[1].encoding, "MacRomanEncoding");
		EXPECT_FALSE(fonts[1].is_embedded);
		EXPECT_FALSE(fonts[1].is_subset);
		EXPECT_TRUE(fonts[1].has_differences);
	}
}

TEST(PdfDocument, BrokenCorpusSweep)
{
	// 1. Every file in `tests/corpus/broken/` except `corrupt-xref-recoverable.pdf`
	//    must return a typed `PdfError` without crashing or leaking under ASan:
	struct BrokenExpected {
		const char* rel_path;
		ErrorCode expected_code;
	};
	const BrokenExpected broken_files[] = {
		{"broken/cyclic-pages-tree.pdf", ErrorCode::DamagedFormat},
		{"broken/cyclic-xref-prev.pdf", ErrorCode::DamagedFormat},
		{"broken/deep-nested-array-70.pdf", ErrorCode::RecursionLimitExceeded},
		{"broken/deflate-bomb-10kb.pdf", ErrorCode::DecompressionLimitExceeded},
		{"broken/empty-0b.pdf", ErrorCode::NotAPdf},
		{"broken/non-pdf-header.pdf", ErrorCode::NotAPdf},
		{"broken/truncated-header.pdf", ErrorCode::DamagedFormat},
		{"broken/truncated-stream.pdf", ErrorCode::DamagedFormat},
	};

	for (const auto& bf : broken_files) {
		SCOPED_TRACE(bf.rel_path);
		auto res = Document::open(recto::test::corpus_path(bf.rel_path));
		ASSERT_FALSE(res.has_value()) << "Expected failure on " << bf.rel_path;
		EXPECT_EQ(res.error().code, bf.expected_code);
	}

	// `broken/corrupt-xref-recoverable.pdf` recovers cleanly via `recover_damaged_xref`:
	{
		auto rec_res = Document::open(
			recto::test::corpus_path("broken/corrupt-xref-recoverable.pdf"));
		ASSERT_TRUE(rec_res.has_value()) << rec_res.error().message.view();
		const Document& rec_doc = **rec_res;
		EXPECT_TRUE(rec_doc.is_recovered());
		EXPECT_TRUE(rec_doc.description().is_recovered);
		EXPECT_EQ(rec_doc.page_count(), 1u);
		auto cs_res = rec_doc.decoded_page_contents(0);
		ASSERT_TRUE(cs_res.has_value()) << cs_res.error().message.view();
		const std::string_view sv(
			reinterpret_cast<const char*>(cs_res->data()),
			cs_res->size());
		EXPECT_NE(sv.find("Recovered from damaged xref"), std::string_view::npos);
	}

	// 2. Sweep all valid unencrypted corpus PDF files across `render/`, `text/`,
	//    `convert/`, and `seeds/`:
	const char* const valid_unencrypted_files[] = {
		"render/simple-1p.pdf",
		"render/rotated-pages.pdf",
		"render/images-and-vectors.pdf",
		"render/incremental-2rev.pdf",
		"render/compressed-xref-2p.pdf",
		"text/two-column-links-outline.pdf",
		"text/scanned.pdf",
		"convert/tables-and-forms.pdf",
		"seeds/seed-classic-xref.pdf",
		"seeds/seed-xref-objstm.pdf",
		"seeds/seed-filters-ascii-rle.pdf",
		"seeds/seed-filters-lzw-ccitt.pdf",
		"seeds/seed-outlines-links.pdf",
		"seeds/seed-render-ops.pdf",
	};

	for (const char* rel_path : valid_unencrypted_files) {
		SCOPED_TRACE(rel_path);
		auto doc_res = Document::open(recto::test::corpus_path(rel_path));
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const Document& doc = **doc_res;
		EXPECT_GE(doc.page_count(), 1u);
		for (size_t p = 0; p < doc.page_count(); ++p) {
			auto cs = doc.decoded_page_contents(p);
			EXPECT_TRUE(cs.has_value()) << cs.error().message.view();
			auto links = doc.page_links(p);
			EXPECT_TRUE(links.has_value()) << links.error().message.view();
			auto res = doc.page_resources(p);
			EXPECT_TRUE(res.has_value()) << res.error().message.view();
		}
	}
}

} // namespace
} // namespace recto::pdf
