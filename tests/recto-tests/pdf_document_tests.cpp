// Unit tests for `recto::pdf::Document` (D01 T01 section 10).
#include "crypto.h"
#include "document.h"

#include "test_support.h"

#include <gtest/gtest.h>

#include <chrono>
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

// The RC4 corpus file with an incremental update: two RC4-encrypted object streams (20 holding 21 = 7 and 22 = 8,
// 23 holding a forbidden stream member 24) and an xref stream (25) with /Prev. `sh` is authenticated with the
// file's password and is the handler that encrypted the containers.
std::vector<uint8_t> encrypted_objstm_update(SecurityHandler& sh)
{
	std::vector<uint8_t> bytes = read_corpus_bytes("render/encrypted-rc4-128.pdf");

	const auto encrypt = [&](std::string_view plain, uint32_t obj_num) {
		const std::span<const uint8_t> span(reinterpret_cast<const uint8_t*>(plain.data()), plain.size());
		auto cipher = sh.decrypt_bytes(span, IndirectRef{obj_num, 0u}, CryptFilterMethod::V2); // RC4 is symmetric
		EXPECT_TRUE(cipher.has_value());
		return cipher.has_value() ? *cipher : std::vector<uint8_t>{};
	};
	const auto append = [&](std::string_view sv) { bytes.insert(bytes.end(), sv.begin(), sv.end()); };
	const auto append_objstm = [&](uint32_t num, std::string_view plain, size_t first, size_t n) {
		const std::vector<uint8_t> cipher = encrypt(plain, num);
		const uint64_t off = bytes.size();
		append(std::to_string(num) + " 0 obj\n<< /Type /ObjStm /N " + std::to_string(n) + " /First " +
			std::to_string(first) + " /Length " + std::to_string(cipher.size()) + " >>\nstream\n");
		bytes.insert(bytes.end(), cipher.begin(), cipher.end());
		append("\nendstream\nendobj\n");
		return off;
	};
	append("\n");
	const uint64_t off20 = append_objstm(20, "21 0 22 2 7 8", 10, 2);
	const uint64_t off23 = append_objstm(23, "24 0 << /Length 4 >>\nstream\nabcd\nendstream", 5, 1);
	const uint64_t off25 = bytes.size();
	std::string rows;
	const auto row = [&](uint8_t type, uint32_t f2, uint16_t f3) {
		rows.push_back(static_cast<char>(type));
		for (int s = 24; s >= 0; s -= 8) {
			rows.push_back(static_cast<char>((f2 >> s) & 0xFFu));
		}
		rows.push_back(static_cast<char>((f3 >> 8) & 0xFFu));
		rows.push_back(static_cast<char>(f3 & 0xFFu));
	};
	row(1, static_cast<uint32_t>(off20), 0); // 20: object stream
	row(2, 20, 0);                           // 21 in 20, index 0
	row(2, 20, 1);                           // 22 in 20, index 1
	row(1, static_cast<uint32_t>(off23), 0); // 23: object stream with a forbidden member
	row(2, 23, 0);                           // 24 in 23, index 0
	row(1, static_cast<uint32_t>(off25), 0); // 25: this xref stream
	append("25 0 obj\n<< /Type /XRef /Size 26 /Root 1 0 R /Info 6 0 R /Encrypt 7 0 R "
		"/ID [<0CC47D6E8331B0D9BBEBAFED46D1A791> <0CC47D6E8331B0D9BBEBAFED46D1A791>] /Prev 961 "
		"/Index [20 6] /W [1 4 2] /Length " + std::to_string(rows.size()) + " >>\nstream\n");
	append(rows);
	append("\nendstream\nendobj\nstartxref\n" + std::to_string(off25) + "\n%%EOF\n");
	return bytes;
}

// An encrypted document's object streams are read by the same routine as an unencrypted one's: sibling objects
// share one decrypted, decoded container, and forbidden members (a stream, here) are refused. Built as an
// incremental update of the RC4 corpus file: two RC4-encrypted object streams and an xref stream with /Prev.
TEST(PdfDocument, EncryptedObjectStreamsShareOneDecodeAndRefuseForbiddenMembers)
{
	const std::vector<uint8_t> base = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto base_xref = XrefTable::parse(base);
	ASSERT_TRUE(base_xref.has_value());
	auto sh_opt = SecurityHandler::from_xref(*base_xref, base);
	ASSERT_TRUE(sh_opt.has_value() && sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	ASSERT_TRUE(sh.authenticate("recto-test").has_value());
	const std::vector<uint8_t> bytes = encrypted_objstm_update(sh);

	auto doc = Document::open_memory(bytes, "recto-test");
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	auto a = (*doc)->resolve_object(IndirectRef{21u, 0u});
	auto b = (*doc)->resolve_object(IndirectRef{22u, 0u});
	ASSERT_TRUE(a.has_value()) << a.error().message.view();
	ASSERT_TRUE(b.has_value()) << b.error().message.view();
	EXPECT_EQ(a->as_int(), std::optional<int64_t>(7));
	EXPECT_EQ(b->as_int(), std::optional<int64_t>(8));
	EXPECT_EQ((*doc)->cached_decrypted_obj_stream_count(), 1u); // one decode for both siblings

	auto forbidden = (*doc)->resolve_object(IndirectRef{24u, 0u});
	ASSERT_FALSE(forbidden.has_value());
	EXPECT_EQ(forbidden.error().code, ErrorCode::DamagedFormat);

	// SecurityHandler's own resolution refuses the same member.
	auto updated_xref = XrefTable::parse(bytes);
	ASSERT_TRUE(updated_xref.has_value());
	sh.clear_decrypted_cache();
	auto handler_forbidden = sh.resolve_and_decrypt_object(*updated_xref, bytes, IndirectRef{24u, 0u});
	ASSERT_FALSE(handler_forbidden.has_value());
	EXPECT_EQ(handler_forbidden.error().code, ErrorCode::DamagedFormat);
}

// The same file with its final startxref offset wrong, so the cross-reference data is recovered by scanning: the
// containers are ciphertext until authentication, so recovery takes their members from the xref stream's rows, and
// after opening with the password the compressed objects resolve (D06 T05 §30).
TEST(PdfDocument, RecoveredEncryptedObjectStreamsResolve)
{
	const std::vector<uint8_t> base = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto base_xref = XrefTable::parse(base);
	ASSERT_TRUE(base_xref.has_value());
	auto sh_opt = SecurityHandler::from_xref(*base_xref, base);
	ASSERT_TRUE(sh_opt.has_value() && sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	ASSERT_TRUE(sh.authenticate("recto-test").has_value());
	std::vector<uint8_t> bytes = encrypted_objstm_update(sh);
	const std::string text(bytes.begin(), bytes.end());
	const size_t number_at = text.rfind("startxref\n") + 10u;
	const size_t number_end = text.find('\n', number_at);
	bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(number_at), bytes.begin() + static_cast<std::ptrdiff_t>(number_end));
	bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(number_at), '1');

	auto recovered = XrefTable::parse(bytes, kMaxXrefTotalEntries, /*allow_recovery=*/true);
	ASSERT_TRUE(recovered.has_value()) << recovered.error().message.view();
	EXPECT_TRUE(recovered->recovered());
	ASSERT_NE(recovered->find(21), nullptr);
	EXPECT_TRUE(recovered->find(21)->is_compressed());
	EXPECT_EQ(recovered->find(21)->obj_stm_num, 20u);

	auto doc = Document::open_memory(bytes, "recto-test");
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	auto a = (*doc)->resolve_object(IndirectRef{21u, 0u});
	auto b = (*doc)->resolve_object(IndirectRef{22u, 0u});
	ASSERT_TRUE(a.has_value()) << a.error().message.view();
	ASSERT_TRUE(b.has_value()) << b.error().message.view();
	EXPECT_EQ(a->as_int(), std::optional<int64_t>(7));
	EXPECT_EQ(b->as_int(), std::optional<int64_t>(8));

	// The recovered stream member is still refused, and the wrong password still fails.
	auto forbidden = (*doc)->resolve_object(IndirectRef{24u, 0u});
	ASSERT_FALSE(forbidden.has_value());
	EXPECT_EQ(forbidden.error().code, ErrorCode::DamagedFormat);
	auto wrong = Document::open_memory(bytes, "wrong");
	ASSERT_FALSE(wrong.has_value());
	EXPECT_EQ(wrong.error().code, ErrorCode::WrongPassword);

	// A later xref stream whose row frees object 22 deletes it, and recovery honours that: 22 resolves to null.
	std::string text_freed(bytes.begin(), bytes.end());
	text_freed.erase(text_freed.rfind("startxref"));
	const std::string free_row("\x00\x00\x00\x00\x00\x00\x01", 7); // type 0, next free 0, generation 1
	text_freed += "26 0 obj\n<< /Type /XRef /Size 27 /Root 1 0 R /Info 6 0 R /Encrypt 7 0 R "
		"/ID [<0CC47D6E8331B0D9BBEBAFED46D1A791> <0CC47D6E8331B0D9BBEBAFED46D1A791>] /Index [22 1] /W [1 4 2] "
		"/Length 7 >>\nstream\n" + free_row + "\nendstream\nendobj\nstartxref\n1\n%%EOF\n";
	const std::vector<uint8_t> freed(text_freed.begin(), text_freed.end());
	auto freed_doc = Document::open_memory(freed, "recto-test");
	ASSERT_TRUE(freed_doc.has_value()) << freed_doc.error().message.view();
	auto freed_22 = (*freed_doc)->resolve_object(IndirectRef{22u, 0u});
	ASSERT_TRUE(freed_22.has_value()) << freed_22.error().message.view();
	EXPECT_TRUE(freed_22->is_null());
	auto kept_21 = (*freed_doc)->resolve_object(IndirectRef{21u, 0u});
	ASSERT_TRUE(kept_21.has_value());
	EXPECT_EQ(kept_21->as_int(), std::optional<int64_t>(7));
}

// Plan review of D06 T05 §30, gap 2 (docs/reviews/06-quality/D06-T05-s30.md): an encrypted file whose catalog and page
// tree live only in an encrypted object stream, its cross-reference lost. Recovery takes the compressed root on the
// intact xref stream's word; the document refuses without the password, refuses the wrong one, then opens with the
// right one after that refusal, with the base file's page and content. A compressed root that is not a catalog is a
// typed error, not a document.
TEST(PdfDocument, RecoveredEncryptedCompressedCatalog)
{
	const std::vector<uint8_t> base = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto base_doc = Document::open_memory(base, "recto-test");
	ASSERT_TRUE(base_doc.has_value());
	auto base_content = (*base_doc)->decoded_page_contents(0u);
	ASSERT_TRUE(base_content.has_value());
	auto base_xref = XrefTable::parse(base);
	ASSERT_TRUE(base_xref.has_value());
	auto sh_opt = SecurityHandler::from_xref(*base_xref, base);
	ASSERT_TRUE(sh_opt.has_value() && sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	ASSERT_TRUE(sh.authenticate("recto-test").has_value());

	const auto build = [&](std::string_view catalog) {
		std::vector<uint8_t> bytes = base;
		const auto append = [&](std::string_view sv) { bytes.insert(bytes.end(), sv.begin(), sv.end()); };
		const std::string pages = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
		const std::string header = "31 0 32 " + std::to_string(catalog.size() + 1u) + " ";
		const std::string plain = header + std::string(catalog) + " " + pages;
		const std::span<const uint8_t> span(reinterpret_cast<const uint8_t*>(plain.data()), plain.size());
		auto cipher = sh.decrypt_bytes(span, IndirectRef{30u, 0u}, CryptFilterMethod::V2); // RC4 is symmetric
		EXPECT_TRUE(cipher.has_value());
		append("\n");
		const uint64_t off30 = bytes.size();
		append("30 0 obj\n<< /Type /ObjStm /N 2 /First " + std::to_string(header.size()) + " /Length " +
			std::to_string(cipher->size()) + " >>\nstream\n");
		bytes.insert(bytes.end(), cipher->begin(), cipher->end());
		append("\nendstream\nendobj\n");
		const uint64_t off33 = bytes.size();
		std::string rows;
		const auto row = [&](uint8_t type, uint32_t f2, uint16_t f3) {
			rows.push_back(static_cast<char>(type));
			for (int s = 24; s >= 0; s -= 8) {
				rows.push_back(static_cast<char>((f2 >> s) & 0xFFu));
			}
			rows.push_back(static_cast<char>((f3 >> 8) & 0xFFu));
			rows.push_back(static_cast<char>(f3 & 0xFFu));
		};
		row(1, static_cast<uint32_t>(off30), 0);
		row(2, 30, 0);
		row(2, 30, 1);
		row(1, static_cast<uint32_t>(off33), 0);
		append("33 0 obj\n<< /Type /XRef /Size 34 /Root 31 0 R /Info 6 0 R /Encrypt 7 0 R "
			"/ID [<0CC47D6E8331B0D9BBEBAFED46D1A791> <0CC47D6E8331B0D9BBEBAFED46D1A791>] /Prev 961 "
			"/Index [30 4] /W [1 4 2] /Length " + std::to_string(rows.size()) + " >>\nstream\n");
		append(rows);
		append("\nendstream\nendobj\nstartxref\n1\n%%EOF\n"); // the offset is wrong: recovery
		return bytes;
	};

	const std::vector<uint8_t> bytes = build("<< /Type /Catalog /Pages 32 0 R >>");
	auto locked = Document::open_memory(bytes);
	ASSERT_FALSE(locked.has_value());
	EXPECT_EQ(locked.error().code, ErrorCode::NeedsPassword);
	auto pending = Document::open_memory_unauthenticated(bytes);
	ASSERT_TRUE(pending.has_value()) << pending.error().message.view();
	EXPECT_TRUE((*pending)->is_recovered());
	auto wrong = (*pending)->authenticate("wrong");
	ASSERT_FALSE(wrong.has_value());
	EXPECT_EQ(wrong.error().code, ErrorCode::WrongPassword);
	auto right = (*pending)->authenticate("recto-test");
	ASSERT_TRUE(right.has_value()) << right.error().message.view();
	EXPECT_EQ((*pending)->xref().root_ref(), (std::optional<IndirectRef>(IndirectRef{31u, 0u})));
	ASSERT_EQ((*pending)->page_count(), 1u);
	auto content = (*pending)->decoded_page_contents(0u);
	ASSERT_TRUE(content.has_value()) << content.error().message.view();
	EXPECT_EQ(*content, *base_content);

	// The compressed root is not a catalog by name (another type, a non-name type, no type, a type that does not
	// resolve): once its container can be read, the document is refused with a typed error (ISO 32000-1 Table 28).
	for (const std::string_view catalog : {std::string_view("<< /Type /Pages /Kids [] /Count 0 >>"),
			 std::string_view("<< /Type 42 /Pages 32 0 R >>"), std::string_view("<< /Pages 32 0 R >>"),
			 std::string_view("<< /Type 99 0 R /Pages 32 0 R >>")}) {
		const std::vector<uint8_t> bad = build(catalog);
		auto refused = Document::open_memory(bad, "recto-test");
		ASSERT_FALSE(refused.has_value()) << catalog;
		EXPECT_EQ(refused.error().code, ErrorCode::DamagedFormat) << catalog;
	}
}

// The audit of D01 T01 §9, round 2 (D06 T05 §23): in an encrypted file (Identity crypt filters, empty user password),
// a chain of streams each naming the next one eight times in /Filter is not copied into each dictionary while it is
// decrypted: only a resolved name is written in, so resolving the first costs a few objects, not 8^5 copies.
TEST(PdfDocument, EncryptedFilterReferenceChainIsNotInlined)
{
	std::string pdf = "%PDF-1.7\n"
					  "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
					  "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
					  "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] /Contents 10 0 R >>\nendobj\n";
	for (uint32_t n = 10u; n < 15u; ++n) {
		std::string refs;
		for (int i = 0; i < 8; ++i) {
			refs += " " + std::to_string(n + 1u) + " 0 R";
		}
		pdf += std::to_string(n) + " 0 obj\n<< /Length 0 /Filter [" + refs + " ] >>\nstream\n\nendstream\nendobj\n";
	}
	pdf += "15 0 obj\n<< /Length 0 >>\nstream\n\nendstream\nendobj\n"
		   "9 0 obj\n<< /Filter /Standard /V 4 /R 4 /Length 128 /P -3904 "
		   "/CF << /StdCF << /CFM /V2 /Length 16 >> >> /StmF /Identity /StrF /Identity "
		   "/O <CE9D51ECC479711BE707AD379A54A0F4B30916D3240087ECA5E23F15B28393F3> "
		   "/U <B51910F4BB473091A4774F51E269263700000000000000000000000000000000> >>\nendobj\n"
		   "trailer\n<< /Size 16 /Root 1 0 R /Encrypt 9 0 R "
		   "/ID [<000102030405060708090A0B0C0D0E0F> <000102030405060708090A0B0C0D0E0F>] >>\n%%EOF\n";
	const std::vector<uint8_t> bytes(pdf.begin(), pdf.end());
	// The first page's content has streams for filters, so opening refuses it; what matters is what that costs.
	const test::ScopedHeapMeter meter;
	const auto started = std::chrono::steady_clock::now();
	auto doc = Document::open_memory(bytes);
	const auto elapsed = std::chrono::steady_clock::now() - started;
	const size_t allocated = meter.allocated_bytes();
	ASSERT_FALSE(doc.has_value());
	EXPECT_EQ(doc.error().code, ErrorCode::DamagedFormat);
	EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2000);
	if (test::ScopedHeapMeter::available()) {
		EXPECT_LT(allocated, size_t{4} << 20u);
	}
}

// Round 3 of the same audit: a decryption nested inside that resolution still resolves its own indirect filter. A V4
// AES-128 file (/StmF /StdCF, empty user password) whose content stream names its filter through object 20, which
// sits in object stream 30, whose own /Filter is object 40: both name /Crypt with the Identity crypt filter, so the
// cleartext container must not be read as AES.
TEST(PdfDocument, NestedDecryptionResolvesItsOwnIndirectCryptFilter)
{
	std::string pdf = "%PDF-1.7\n";
	std::vector<size_t> offsets(51u, 0u);
	const auto object = [&](uint32_t num, const std::string& body) {
		offsets[num] = pdf.size();
		pdf += std::to_string(num) + " 0 obj\n" + body + "\nendobj\n";
	};
	object(1u, "<< /Type /Catalog /Pages 2 0 R >>");
	object(2u, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
	object(3u, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] /Contents 5 0 R >>");
	object(5u, "<< /Length 3 /Filter 20 0 R /DecodeParms << /Name /Identity >> >>\nstream\nq Q\nendstream");
	object(9u, "<< /Filter /Standard /V 4 /R 4 /Length 128 /P -3904 "
			   "/CF << /StdCF << /CFM /AESV2 /Length 16 >> >> /StmF /StdCF /StrF /StdCF "
			   "/O <CE9D51ECC479711BE707AD379A54A0F4B30916D3240087ECA5E23F15B28393F3> "
			   "/U <B51910F4BB473091A4774F51E269263700000000000000000000000000000000> >>");
	object(30u, "<< /Type /ObjStm /N 1 /First 5 /Filter 40 0 R /DecodeParms << /Name /Identity >> /Length 11 >>\n"
				"stream\n20 0 /Crypt\nendstream");
	object(40u, "/Crypt");
	std::string rows;
	const auto row = [&](uint8_t type, uint32_t f2, uint16_t f3) {
		rows.push_back(static_cast<char>(type));
		for (int s = 24; s >= 0; s -= 8) {
			rows.push_back(static_cast<char>((f2 >> s) & 0xFFu));
		}
		rows.push_back(static_cast<char>((f3 >> 8) & 0xFFu));
		rows.push_back(static_cast<char>(f3 & 0xFFu));
	};
	offsets[50] = pdf.size();
	for (uint32_t num = 0u; num < 51u; ++num) {
		if (num == 20u) {
			row(2u, 30u, 0u);
		} else if (offsets[num] != 0u) {
			row(1u, static_cast<uint32_t>(offsets[num]), 0u);
		} else {
			row(0u, 0u, num == 0u ? 0xFFFFu : 0u);
		}
	}
	pdf += "50 0 obj\n<< /Type /XRef /Size 51 /W [1 4 2] /Root 1 0 R /Encrypt 9 0 R "
		   "/ID [<000102030405060708090A0B0C0D0E0F> <000102030405060708090A0B0C0D0E0F>] /Length " +
		std::to_string(rows.size()) + " >>\nstream\n" + rows + "\nendstream\nendobj\nstartxref\n" +
		std::to_string(offsets[50]) + "\n%%EOF\n";
	const std::vector<uint8_t> bytes(pdf.begin(), pdf.end());
	auto doc = Document::open_memory(bytes);
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	auto content = (*doc)->resolve_object(IndirectRef{5u, 0u});
	ASSERT_TRUE(content.has_value()) << content.error().message.view();
	ASSERT_NE(content->as_stream(), nullptr);
	// The Identity crypt filter left the bytes as they are, and its /Crypt entry is gone once applied.
	EXPECT_EQ(std::string(content->as_stream()->raw_bytes.begin(), content->as_stream()->raw_bytes.end()), "q Q");
	EXPECT_EQ(content->as_stream()->dict.get("Filter"), nullptr);
}

// An unencrypted file whose catalog is compressed, with only its startxref offset wrong, opens with both pages:
// the xref stream's rows name the same objects the scan already unpacked, and what the scan learned stands.
TEST(PdfDocument, RecoveredCompressedCatalogKeepsItsPages)
{
	std::vector<uint8_t> bytes = read_corpus_bytes("render/compressed-xref-2p.pdf");
	auto intact = Document::open_memory(bytes);
	ASSERT_TRUE(intact.has_value()) << intact.error().message.view();
	const size_t pages = (*intact)->page_count();
	std::string text(bytes.begin(), bytes.end());
	const size_t number_at = text.rfind("startxref") + 10u;
	text.replace(number_at, text.find('\n', number_at) - number_at, "1");
	const std::vector<uint8_t> damaged(text.begin(), text.end());
	auto recovered = XrefTable::parse(damaged, kMaxXrefTotalEntries, /*allow_recovery=*/true);
	ASSERT_TRUE(recovered.has_value()) << recovered.error().message.view();
	EXPECT_TRUE(recovered->recovered());
	auto doc = Document::open_memory(damaged);
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	EXPECT_EQ((*doc)->page_count(), pages);
	EXPECT_EQ(pages, 2u);
}

// A damaged file (no cross-reference data at all) whose only catalog sits in an object stream with an indirect
// /DecodeParms opens as a document with both of its pages (D06 T05 §29).
TEST(PdfDocument, RecoveredDeferredContainerOpensWithItsPages)
{
	const std::string members = "3 0 << /Type /Catalog /Pages 4 0 R >>";
	std::string data;
	static constexpr char kHex[] = "0123456789ABCDEF";
	for (const unsigned char c : members) {
		data += kHex[c >> 4u];
		data += kHex[c & 0x0Fu];
	}
	data += ">";
	const std::string pdf = "%PDF-1.7\n"
		"1 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 2 0 R /Length " +
		std::to_string(data.size()) + " >>\nstream\n" + data + "\nendstream\nendobj\n" +
		"2 0 obj\n<< >>\nendobj\n"
		"4 0 obj\n<< /Type /Pages /Kids [5 0 R 6 0 R] /Count 2 >>\nendobj\n"
		"5 0 obj\n<< /Type /Page /Parent 4 0 R /MediaBox [0 0 200 100] >>\nendobj\n"
		"6 0 obj\n<< /Type /Page /Parent 4 0 R /MediaBox [0 0 100 200] >>\nendobj\n%%EOF\n";
	const std::vector<uint8_t> bytes(pdf.begin(), pdf.end());
	auto doc = Document::open_memory(bytes);
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	EXPECT_EQ((*doc)->page_count(), 2u);
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

		// 4. Retrying `authenticate` after `load_document_structure()` fails on an
		//    encrypted malformed PDF must never report success or leave the
		//    document marked authenticated:
		std::vector<uint8_t> malformed_enc = bytes;
		const std::string_view pages_needle = "/Pages";
		const std::string_view pages_replacement = "/PageX";
		bool patched_catalog = false;
		for (size_t pos = 0u; pos + pages_needle.size() <= malformed_enc.size(); ++pos) {
			if (std::memcmp(
					malformed_enc.data() + pos,
					pages_needle.data(),
					pages_needle.size()) == 0) {
				std::memcpy(
					malformed_enc.data() + pos,
					pages_replacement.data(),
					pages_replacement.size());
				patched_catalog = true;
			}
		}
		ASSERT_TRUE(patched_catalog);

		auto mal_unauth_res = Document::open_memory_unauthenticated(malformed_enc);
		ASSERT_TRUE(mal_unauth_res.has_value()) << mal_unauth_res.error().message.view();
		Document& mal_doc = **mal_unauth_res;
		EXPECT_TRUE(mal_doc.is_encrypted());
		EXPECT_FALSE(mal_doc.is_authenticated());
		EXPECT_TRUE(mal_doc.needs_password());

		auto auth1 = mal_doc.authenticate("recto-test");
		ASSERT_FALSE(auth1.has_value());
		EXPECT_EQ(auth1.error().code, ErrorCode::DamagedFormat);
		EXPECT_FALSE(mal_doc.is_authenticated());
		EXPECT_TRUE(mal_doc.needs_password());
		EXPECT_EQ(mal_doc.page_count(), 0u);

		auto auth2 = mal_doc.authenticate("recto-test");
		ASSERT_FALSE(auth2.has_value());
		EXPECT_EQ(auth2.error().code, ErrorCode::DamagedFormat);
		EXPECT_FALSE(mal_doc.is_authenticated());
		EXPECT_TRUE(mal_doc.needs_password());
		EXPECT_EQ(mal_doc.page_count(), 0u);

		auto auth3 = mal_doc.authenticate("recto-owner");
		ASSERT_FALSE(auth3.has_value());
		EXPECT_EQ(auth3.error().code, ErrorCode::DamagedFormat);
		EXPECT_FALSE(mal_doc.is_authenticated());
		EXPECT_TRUE(mal_doc.needs_password());
		EXPECT_EQ(mal_doc.page_count(), 0u);
	}
}

// BleedBox, TrimBox and ArtBox are page-local (ISO 32000-1 Table 30): a /Pages node's values never reach its pages,
// and each page keeps its own (D06 T05 section 24, the audit of D01 T01 section 10).
TEST(PdfDocument, ProductionBoxesArePageLocal)
{
	const auto bytes = build_synthetic_pdf_from_objects(
		"1.7",
		{
			{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			{2,
			 "<< /Type /Pages /Count 2 /MediaBox [0 0 600 800] /BleedBox [1 1 599 799] /TrimBox [9 9 591 791] "
			 "/ArtBox [20 20 580 780] /Kids [3 0 R 4 0 R] >>"},
			{3, "<< /Type /Page /Parent 2 0 R >>"},
			{4, "<< /Type /Page /Parent 2 0 R /TrimBox [30 30 570 770] >>"},
		});
	auto doc_res = Document::open_memory(bytes);
	ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
	const Document& doc = **doc_res;
	ASSERT_EQ(doc.page_count(), 2u);
	EXPECT_FALSE(doc.pages()[0].bleed_box.has_value());
	EXPECT_FALSE(doc.pages()[0].trim_box.has_value());
	EXPECT_FALSE(doc.pages()[0].art_box.has_value());
	EXPECT_DOUBLE_EQ(doc.pages()[0].media_box.width(), 600.0); // MediaBox still inherits
	ASSERT_TRUE(doc.pages()[1].trim_box.has_value());
	EXPECT_DOUBLE_EQ(doc.pages()[1].trim_box->width(), 540.0);
	EXPECT_FALSE(doc.pages()[1].bleed_box.has_value());
}

// A destination given as a name resolves through Catalog /Dests and one given as a string through the /Names /Dests
// tree, even when both spell the same word (ISO 32000-1 12.3.2.3; D06 T05 section 24, round 2).
TEST(PdfDocument, NamedDestinationSpacesStayApart)
{
	const auto bytes = build_synthetic_pdf_from_objects(
		"1.7",
		{
			{1,
			 "<< /Type /Catalog /Pages 2 0 R /Outlines 5 0 R /Dests << /chapter [3 0 R /Fit] >> "
			 "/Names << /Dests << /Names [(chapter) [4 0 R /Fit]] >> >> >>"},
			{2, "<< /Type /Pages /Count 2 /MediaBox [0 0 100 100] /Kids [3 0 R 4 0 R] >>"},
			{3, "<< /Type /Page /Parent 2 0 R >>"},
			{4, "<< /Type /Page /Parent 2 0 R >>"},
			{5, "<< /Type /Outlines /First 6 0 R /Last 7 0 R /Count 2 >>"},
			{6, "<< /Title (By name) /Parent 5 0 R /Next 7 0 R /Dest /chapter >>"},
			{7, "<< /Title (By string) /Parent 5 0 R /Prev 6 0 R /Dest (chapter) >>"},
		});
	auto doc_res = Document::open_memory(bytes);
	ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
	const auto& outline = (*doc_res)->outline();
	ASSERT_EQ(outline.size(), 2u);
	EXPECT_EQ(outline[0].page_index, 0);
	EXPECT_EQ(outline[1].page_index, 1);
}

// A name-tree key is a string compared byte for byte, so <41> and <FEFF0041>, which both read as "A", name two
// destinations (ISO 32000-1 sections 7.9.6 and 12.3.2.3; D06 T05 section 24, audit round 1).
TEST(PdfDocument, NameTreeKeysCompareAsBytes)
{
	const auto bytes = build_synthetic_pdf_from_objects(
		"1.7",
		{
			{1,
			 "<< /Type /Catalog /Pages 2 0 R /Outlines 5 0 R "
			 "/Names << /Dests << /Names [<41> [3 0 R /Fit] <FEFF0041> [4 0 R /Fit]] >> >> >>"},
			{2, "<< /Type /Pages /Count 2 /MediaBox [0 0 100 100] /Kids [3 0 R 4 0 R] >>"},
			{3, "<< /Type /Page /Parent 2 0 R >>"},
			{4, "<< /Type /Page /Parent 2 0 R >>"},
			{5, "<< /Type /Outlines /First 6 0 R /Last 7 0 R /Count 2 >>"},
			{6, "<< /Title (Bytes) /Parent 5 0 R /Next 7 0 R /Dest <41> >>"},
			{7, "<< /Title (UTF-16) /Parent 5 0 R /Prev 6 0 R /Dest <FEFF0041> >>"},
		});
	auto doc_res = Document::open_memory(bytes);
	ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
	const auto& outline = (*doc_res)->outline();
	ASSERT_EQ(outline.size(), 2u);
	EXPECT_EQ(outline[0].page_index, 0);
	EXPECT_EQ(outline[1].page_index, 1);
}

// Any value may be indirect unless its entry says otherwise (ISO 32000-1 section 7.3.10): an annotation's /Subtype, an
// action's /S and a font's /Subtype and /BaseFont are read through their references, so the link is found, the GoTo
// is followed, a Launch reached by reference is still refused, and the font is named (D06 T05 section 24, round 1).
TEST(PdfDocument, IndirectNamesInAnnotationsActionsAndFonts)
{
	const auto bytes = build_synthetic_pdf_from_objects(
		"1.7",
		{
			{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			{2, "<< /Type /Pages /Count 2 /MediaBox [0 0 100 100] /Kids [3 0 R 10 0 R] >>"},
			{3,
			 "<< /Type /Page /Parent 2 0 R /Annots [4 0 R 6 0 R 8 0 R] "
			 "/Resources << /Font << /F1 11 0 R >> >> >>"},
			{4, "<< /Type /Annot /Subtype 5 0 R /Rect [0 0 10 10] /Dest [10 0 R /Fit] >>"},
			{5, "/Link"},
			{6, "<< /Type /Annot /Subtype /Link /Rect [20 0 30 10] /A << /S 7 0 R /D [10 0 R /Fit] >> >>"},
			{7, "/GoTo"},
			{8, "<< /Type /Annot /Subtype /Link /Rect [40 0 50 10] /A << /S 9 0 R /F (calc.exe) >> >>"},
			{9, "/Launch"},
			{10, "<< /Type /Page /Parent 2 0 R >>"},
			{11, "<< /Type /Font /Subtype 12 0 R /BaseFont 13 0 R >>"},
			{12, "/Type1"},
			{13, "/Helvetica"},
		});
	auto doc_res = Document::open_memory(bytes);
	ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
	auto links = (*doc_res)->page_links(0);
	ASSERT_TRUE(links.has_value()) << links.error().message.view();
	ASSERT_EQ(links->size(), 3u);
	EXPECT_EQ((*links)[0].kind, LinkKind::Internal);
	EXPECT_EQ((*links)[0].page_index, 1);
	EXPECT_EQ((*links)[1].kind, LinkKind::Internal);
	EXPECT_EQ((*links)[1].page_index, 1);
	EXPECT_EQ((*links)[2].kind, LinkKind::RefusedAction);
	EXPECT_TRUE((*links)[2].is_inert);

	const auto& fonts = (*doc_res)->fonts();
	ASSERT_EQ(fonts.size(), 1u);
	EXPECT_EQ(fonts[0].name, "Helvetica");
	EXPECT_EQ(fonts[0].subtype, "Type1");
}

// `pages` pages, each with a direct /Pad array of `elements` zeros: every element is one retained PdfValue, so a small
// file builds large page dictionaries.
std::vector<uint8_t> pages_with_padding_arrays(uint32_t pages, size_t elements)
{
	std::string pad;
	pad.reserve(elements * 2u);
	for (size_t i = 0u; i < elements; ++i) {
		pad += "0 ";
	}
	std::vector<std::pair<uint32_t, std::string>> objects = {{1, "<< /Type /Catalog /Pages 2 0 R >>"}};
	std::string kids;
	for (uint32_t p = 0u; p < pages; ++p) {
		kids += std::to_string(10u + p) + " 0 R ";
		objects.emplace_back(10u + p, "<< /Type /Page /Parent 2 0 R /Pad [" + pad + "] >>");
	}
	objects.emplace_back(
		2u, "<< /Type /Pages /Count " + std::to_string(pages) + " /MediaBox [0 0 100 100] /Kids [" + kids + "] >>");
	return build_synthetic_pdf_from_objects("1.7", objects);
}

// Every page keeps its own dictionary, so the copies share one budget (kMaxDecodedStreamBytes in all): 400 pages with
// a 20,000-element /Pad array each (a 16 MiB file) are refused with ResourceLimit before they are retained, while the
// same file with 100-element arrays opens. Peak memory is about the budget (D06 T05 section 24, round 1).
TEST(PdfDocument, RetainedPageDictionariesShareOneBudget)
{
	static_assert(400u * 20'000u * sizeof(PdfValue) > kMaxDecodedStreamBytes);
	{
		auto small = Document::open_memory(pages_with_padding_arrays(400u, 100u));
		ASSERT_TRUE(small.has_value()) << small.error().message.view();
		EXPECT_EQ((*small)->page_count(), 400u);
	}
	auto large = Document::open_memory(pages_with_padding_arrays(400u, 20'000u));
	ASSERT_FALSE(large.has_value());
	EXPECT_EQ(large.error().code, ErrorCode::ResourceLimit);
}

// An outline is read one item at a time: each level keeps only the reference it reads next, so 64 nested items with
// a 10,000-element /Pad array each (about 64 times one item's dictionary if every ancestor stayed alive) open within a
// few items' memory (D06 T05 section 24, audit round 2). The heap meter measures in the Debug and Release runs.
// A resolve gives the same value or the same typed error cold (parsed), from the window (second) and from the main
// cache (third), so caching never changes what a reference means (D01 T01 section 11).
TEST(PdfDocument, ObjectCacheSemantics)
{
	const auto same = [](const PdfResult<PdfValue>& a, const PdfResult<PdfValue>& b) {
		if (a.has_value() != b.has_value()) {
			return false;
		}
		return a.has_value() ? *a == *b : a.error().code == b.error().code;
	};
	const auto three_times = [&](const Document& doc, IndirectRef ref, const std::string& what) {
		const auto cold = doc.resolve_object(ref);
		const auto window = doc.resolve_object(ref);
		const auto main = doc.resolve_object(ref);
		EXPECT_TRUE(same(cold, window)) << what;
		EXPECT_TRUE(same(cold, main)) << what;
		return cold;
	};
	// Every object of the corpus files that exercise object streams, encryption, incremental updates and recovery,
	// at generation 0 and 1.
	for (const char* file : {"render/compressed-xref-2p.pdf", "render/encrypted-aes-128.pdf", "render/encrypted-rc4-128.pdf",
			 "render/incremental-2rev.pdf", "broken/corrupt-xref-recoverable.pdf", "render/simple-1p.pdf"}) {
		const auto bytes = read_corpus_bytes(file);
		const bool encrypted = std::string_view(file).find("encrypted") != std::string_view::npos;
		const auto open = [&] { return encrypted ? Document::authenticate(bytes, "recto-test") : Document::open_memory(bytes); };
		auto doc = open();
		ASSERT_TRUE(doc.has_value()) << file << ": " << doc.error().message.view();
		auto fresh = open();
		ASSERT_TRUE(fresh.has_value());
		for (uint32_t num = 1; num < 64u; ++num) {
			for (uint16_t gen : {uint16_t{0}, uint16_t{1}}) {
				const std::string what = std::string(file) + " " + std::to_string(num) + " " + std::to_string(gen);
				const auto cold = three_times(**doc, IndirectRef{num, gen}, what);
				EXPECT_TRUE(same(cold, (*fresh)->resolve_object(IndirectRef{num, gen}))) << what;
			}
		}
	}
	// A two-object cycle, a 33-hop chain whose end is already cached, a missing object and a generation mismatch.
	std::vector<std::pair<uint32_t, std::string>> objects = {
		{1, "<< /Type /Catalog /Pages 2 0 R >>"},
		{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [3 0 R] >>"},
		{3, "<< /Type /Page /Parent 2 0 R >>"},
		{5, "6 0 R"},
		{6, "5 0 R"},
		{100, "<< /End true >>"},
	};
	for (uint32_t i = 0; i < 33u; ++i) {
		objects.emplace_back(10u + i, std::to_string(i + 1u < 33u ? 11u + i : 100u) + " 0 R");
	}
	const auto bytes = build_synthetic_pdf_from_objects("1.7", objects);
	auto doc = Document::open_memory(bytes);
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	const auto cycle = three_times(**doc, IndirectRef{5u, 0u}, "cycle");
	EXPECT_FALSE(cycle.has_value());
	(void)three_times(**doc, IndirectRef{100u, 0u}, "chain end");
	ASSERT_GE((*doc)->cached_object_count(), 1u);
	const auto chain = three_times(**doc, IndirectRef{10u, 0u}, "33-hop chain");
	EXPECT_FALSE(chain.has_value()) << "a cached end let an over-long chain resolve";
	const auto short_chain = three_times(**doc, IndirectRef{12u, 0u}, "31-hop chain");
	ASSERT_TRUE(short_chain.has_value());
	EXPECT_TRUE(short_chain->as_dict() != nullptr);
	EXPECT_TRUE(three_times(**doc, IndirectRef{9'999u, 0u}, "missing")->is_null());
	EXPECT_TRUE(three_times(**doc, IndirectRef{100u, 1u}, "generation mismatch")->is_null());
}

// The cache's budget: every bound holds, the reported bytes are the sum of what it keeps and match the heap it holds,
// the least recently used entry goes first, and a value too large is returned but never kept.
TEST(PdfDocument, ObjectCacheBudget)
{
	const auto string_value = [](size_t bytes) {
		return std::make_shared<const PdfValue>(PdfString(std::vector<uint8_t>(bytes, 'x')));
	};
	// Seen twice, as a shared object is: into the main cache (from the window, or past it when it is too large for it).
	const auto admit = [](ResolvedObjectCache& cache, uint64_t key, const std::shared_ptr<const PdfValue>& value) {
		ASSERT_TRUE(cache.insert(key, value).has_value());
		ASSERT_TRUE(cache.insert(key, value).has_value());
		(void)cache.find(key);
	};
	// Bytes: 200 values of 512 KiB pass the 64 MiB main budget.
	{
		ResolvedObjectCache cache;
		const recto::test::ScopedHeapMeter meter;
		size_t held = 0;
		for (uint64_t key = 1; key <= 200u; ++key) {
			admit(cache, key, string_value(size_t{512} << 10u));
			EXPECT_LE(cache.bytes(), ResolvedObjectCache::kMaxObjectCacheBytes) << "the tiers together passed the budget";
		}
		for (uint64_t key = 1; key <= 200u; ++key) {
			if (std::shared_ptr<const PdfValue> v = cache.find(key)) {
				held += ResolvedObjectCache::entry_bytes(*v);
			}
		}
		EXPECT_EQ(cache.bytes(), held + cache.sightings_bytes()) << "the reported bytes are not the entries and the tables";
		EXPECT_GT(cache.sightings_bytes(), 0u);
		EXPECT_LT(cache.main_size(), 200u);
		if (recto::test::ScopedHeapMeter::available()) {
			const auto live = static_cast<size_t>(meter.live_bytes());
			EXPECT_GE(cache.bytes(), live) << "the cache undercounts the heap it holds";
			EXPECT_LE(cache.bytes(), live + live / 10u);
		}
	}
	// Least recently used first: with the main cache full, a promoted early entry stays and the next oldest goes.
	{
		ResolvedObjectCache cache;
		const size_t per = size_t{1} << 20u;
		const size_t tables = 2u * ResolvedObjectCache::kSightingSlots * sizeof(uint64_t); // the sighting tables' share
		const size_t fit = (ResolvedObjectCache::kMaxObjectCacheBytes - tables) / ResolvedObjectCache::entry_bytes(*string_value(per - 4'096u));
		for (uint64_t key = 1; key <= fit; ++key) {
			admit(cache, key, string_value(per - 4'096u));
		}
		ASSERT_NE(cache.find(1u), nullptr); // promoted
		admit(cache, 10'000u, string_value(per - 4'096u));
		EXPECT_NE(cache.find(1u), nullptr) << "the promoted entry was evicted";
		EXPECT_EQ(cache.find(2u), nullptr) << "the least recently used entry was kept";
	}
	// Entries: 65,537 small values in the main cache keep 65,536.
	{
		ResolvedObjectCache cache;
		for (uint64_t key = 1; key <= ResolvedObjectCache::kMaxObjectCacheEntries + 1u; ++key) {
			admit(cache, key, std::make_shared<const PdfValue>(int64_t{1}));
		}
		EXPECT_EQ(cache.main_size(), ResolvedObjectCache::kMaxObjectCacheEntries);
		EXPECT_EQ(cache.find(1u), nullptr);
		// The window's entries count toward the same total.
		for (uint64_t key = 1; key <= ResolvedObjectCache::kMaxWindowEntries; ++key) {
			ASSERT_TRUE(cache.insert((uint64_t{1} << 40u) + key, std::make_shared<const PdfValue>(int64_t{2})).has_value());
		}
		EXPECT_EQ(cache.size(), ResolvedObjectCache::kMaxObjectCacheEntries) << "the tiers together passed the entry limit";
		EXPECT_EQ(cache.main_size(), ResolvedObjectCache::kMaxObjectCacheEntries - ResolvedObjectCache::kMaxWindowEntries);
	}
	// A one-pass traversal of 20,000 objects keeps no more than the window: no first sighting passes for a second.
	// A key seen again within two generations (8,192 to 16,384 sightings) is admitted.
	{
		ResolvedObjectCache cache;
		for (uint64_t key = 1; key <= 20'000u; ++key) {
			ASSERT_TRUE(cache.insert(key << 16u, std::make_shared<const PdfValue>(PdfString(std::vector<uint8_t>(64u, 'x')))).has_value());
		}
		EXPECT_EQ(cache.main_size(), 0u) << "a first sighting was taken for a second";
		EXPECT_LE(cache.size(), ResolvedObjectCache::kMaxWindowEntries);
		ASSERT_TRUE(cache.insert(uint64_t{15'000} << 16u, std::make_shared<const PdfValue>(int64_t{3})).has_value());
		EXPECT_EQ(cache.main_size(), 1u) << "a key seen 5,000 sightings ago was not admitted";
	}
	// The window keeps 16 entries; a value over a quarter of the budget, or a stream body over 64 KiB, is never kept.
	{
		ResolvedObjectCache cache;
		for (uint64_t key = 1; key <= 17u; ++key) {
			ASSERT_TRUE(cache.insert(key << 20u, std::make_shared<const PdfValue>(int64_t{1})).has_value());
		}
		EXPECT_EQ(cache.size(), ResolvedObjectCache::kMaxWindowEntries);
		EXPECT_EQ(cache.find(uint64_t{1} << 20u), nullptr);
		admit(cache, 5'000u, string_value(size_t{17} << 20u));
		EXPECT_EQ(cache.find(5'000u), nullptr) << "a value over a quarter of the budget was kept";
		PdfDict dict;
		admit(cache, 5'001u, std::make_shared<const PdfValue>(PdfStream(dict, std::vector<uint8_t>(size_t{65} << 10u, 1u))));
		EXPECT_EQ(cache.find(5'001u), nullptr) << "a large stream body was kept";
		admit(cache, 5'002u, std::make_shared<const PdfValue>(PdfStream(dict, std::vector<uint8_t>(size_t{1} << 10u, 1u))));
		EXPECT_NE(cache.find(5'002u), nullptr) << "a small stream was not kept";
	}
	// A document resolving each of 100 one-use objects keeps no more than the window.
	{
		std::vector<std::pair<uint32_t, std::string>> objects = {
			{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [3 0 R] >>"},
			{3, "<< /Type /Page /Parent 2 0 R >>"},
		};
		for (uint32_t i = 0; i < 100u; ++i) {
			objects.emplace_back(10u + i, "<< /N " + std::to_string(i) + " >>");
		}
		const auto bytes = build_synthetic_pdf_from_objects("1.7", objects);
		auto doc = Document::open_memory(bytes);
		ASSERT_TRUE(doc.has_value());
		for (uint32_t i = 0; i < 100u; ++i) {
			ASSERT_TRUE((*doc)->resolve_object(IndirectRef{10u + i, 0u}).has_value());
		}
		EXPECT_LE((*doc)->cached_object_count(), ResolvedObjectCache::kMaxWindowEntries + 8u);
	}
}

// Eviction drops only the cache's reference: a value a caller holds stays readable, and the cache's bytes fall.
TEST(PdfDocument, ObjectCacheEviction)
{
	ResolvedObjectCache cache;
	const auto big = std::make_shared<const PdfValue>(PdfString(std::vector<uint8_t>(size_t{8} << 20u, 'h')));
	ASSERT_TRUE(cache.insert(1u, big).has_value()); // too large for the window: not kept on first sight
	EXPECT_EQ(cache.find(1u), nullptr);
	ASSERT_TRUE(cache.insert(1u, big).has_value()); // seen again: into the main cache
	std::shared_ptr<const PdfValue> held = cache.find(1u);
	ASSERT_NE(held, nullptr);
	const size_t before = cache.bytes();
	const size_t one = ResolvedObjectCache::entry_bytes(*held);
	// Fill the main cache past its budget with other values (without touching the held one, which would promote it).
	const auto filler = std::make_shared<const PdfValue>(PdfString(std::vector<uint8_t>((size_t{1} << 20u) - 4'096u, 'f')));
	for (uint64_t key = 2; key < 2u + 2u * ResolvedObjectCache::kMaxObjectCacheBytes / ResolvedObjectCache::entry_bytes(*filler); ++key) {
		ASSERT_TRUE(cache.insert(key, filler).has_value());
		(void)cache.find(key);
	}
	EXPECT_EQ(cache.find(1u), nullptr);
	ASSERT_NE(held->as_string(), nullptr);
	EXPECT_EQ(held->as_string()->bytes.size(), size_t{8} << 20u);
	EXPECT_EQ(held->as_string()->bytes.back(), 'h');
	EXPECT_LE(cache.bytes() + one, before + ResolvedObjectCache::kMaxObjectCacheBytes) << "the evicted value is still charged";
}

// Memory running out while resolving, caching or copying is OutOfMemory, with nothing half cached, and the next
// resolve succeeds.
TEST(PdfDocument, ObjectCacheAllocationFailure)
{
	if (!recto::test::ScopedAllocationFailure::available()) {
		GTEST_SKIP() << "allocation failure injection is unavailable in this build";
	}
	std::string pad;
	for (int i = 0; i < 2'000; ++i) {
		pad += std::to_string(i) + " ";
	}
	const auto bytes = build_synthetic_pdf_from_objects("1.7", {
		{1, "<< /Type /Catalog /Pages 2 0 R >>"},
		{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [3 0 R] >>"},
		{3, "<< /Type /Page /Parent 2 0 R >>"},
		{10, "<< /Pad [" + pad + "] /Name /Shared >>"},
	});
	const auto expected = [&] {
		auto doc = Document::open_memory(bytes);
		EXPECT_TRUE(doc.has_value());
		return *(*doc)->resolve_object(IndirectRef{10u, 0u});
	}();
	for (int warm = 0; warm < 3; ++warm) { // cold, from the window, from the main cache
		bool failed_once = false;
		for (uint32_t nth = 1; nth < 200u; ++nth) {
			auto doc = Document::open_memory(bytes);
			ASSERT_TRUE(doc.has_value());
			for (int i = 0; i < warm; ++i) {
				ASSERT_TRUE((*doc)->resolve_object(IndirectRef{10u, 0u}).has_value());
			}
			const size_t entries = (*doc)->cached_object_count();
			const size_t charged = (*doc)->cached_object_bytes();
			PdfResult<PdfValue> r = make_pdf_error(ErrorCode::Ok);
			{
				const recto::test::ScopedAllocationFailure fail(nth, 32u);
				r = (*doc)->resolve_object(IndirectRef{10u, 0u});
			}
			if (r.has_value()) {
				EXPECT_EQ(*r, expected);
				if (failed_once) {
					break; // every allocation of this resolve has had its turn to fail
				}
				continue;
			}
			failed_once = true;
			EXPECT_EQ(r.error().code, ErrorCode::OutOfMemory) << "warm " << warm << " nth " << nth;
			// Nothing half kept: either the cache is as it was, or (only the caller's copy having failed) it holds the
			// complete value, charged exactly once.
			const size_t count_now = (*doc)->cached_object_count();
			const size_t bytes_now = (*doc)->cached_object_bytes();
			const bool unchanged = count_now == entries && bytes_now == charged;
			const auto cached = (*doc)->resolve_object_shared(IndirectRef{10u, 0u}); // a hit when it was kept
			const bool kept_whole = count_now == entries + 1u && cached.has_value() && **cached == expected &&
				bytes_now == charged + ResolvedObjectCache::entry_bytes(**cached);
			EXPECT_TRUE(unchanged || kept_whole) << "warm " << warm << " nth " << nth << ": " << count_now << " entries, " << bytes_now << " bytes";
			const auto retry = (*doc)->resolve_object(IndirectRef{10u, 0u});
			ASSERT_TRUE(retry.has_value()) << "warm " << warm << " nth " << nth;
			EXPECT_EQ(*retry, expected);
		}
		EXPECT_TRUE(failed_once) << "no allocation of the resolve was made to fail, warm " << warm;
	}
	// Inserts that evict, into a full window and a full main cache: a failure leaves every entry and byte as it was,
	// and the retry succeeds.
	const auto big = std::make_shared<const PdfValue>(PdfString(std::vector<uint8_t>((size_t{1} << 20u) - 8'192u, 'm')));
	const auto small = std::make_shared<const PdfValue>(PdfString(std::vector<uint8_t>(256u, 's')));
	for (const bool into_main : {false, true}) {
		bool failed_once = false;
		for (uint32_t nth = 1; nth < 40u; ++nth) {
			ResolvedObjectCache cache;
			for (uint64_t key = 1; key <= 64u; ++key) { // the main cache full of 1 MiB values
				ASSERT_TRUE(cache.insert(key, big).has_value());
				ASSERT_NE(cache.find(key), nullptr); // asked for again: into the main cache
			}
			for (uint64_t key = 100; key < 100u + ResolvedObjectCache::kMaxWindowEntries; ++key) { // the window full
				ASSERT_TRUE(cache.insert(key, small).has_value());
			}
			const uint64_t key = 9'999u;
			if (into_main) {
				ASSERT_TRUE(cache.insert(key, small).has_value()); // seen once, then pushed out of the window
				for (uint64_t k = 200; k < 200u + ResolvedObjectCache::kMaxWindowEntries; ++k) {
					ASSERT_TRUE(cache.insert(k, small).has_value());
				}
				ASSERT_EQ(cache.find(key), nullptr);
			}
			const size_t entries = cache.size();
			const size_t charged = cache.bytes();
			PdfResult<void> r;
			{
				const recto::test::ScopedAllocationFailure fail(nth, 32u);
				r = cache.insert(key, into_main ? big : small);
			}
			if (r.has_value()) {
				EXPECT_NE(cache.find(key), nullptr);
				if (failed_once) {
					break;
				}
				continue;
			}
			failed_once = true;
			EXPECT_EQ(r.error().code, ErrorCode::OutOfMemory);
			EXPECT_EQ(cache.size(), entries) << (into_main ? "main" : "window") << " nth " << nth;
			EXPECT_EQ(cache.bytes(), charged) << (into_main ? "main" : "window") << " nth " << nth;
			EXPECT_NE(cache.find(into_main ? 64u : 115u), nullptr) << "an entry was lost";
			ASSERT_TRUE(cache.insert(key, into_main ? big : small).has_value());
			EXPECT_NE(cache.find(key), nullptr);
		}
		EXPECT_TRUE(failed_once) << "no allocation of an evicting insert was made to fail, " << (into_main ? "main" : "window");
	}
}

// resolve_object hands out a copy: changing it never changes what the next resolve returns.
TEST(PdfDocument, ObjectCacheCopies)
{
	const auto bytes = build_synthetic_pdf_from_objects("1.7", {
		{1, "<< /Type /Catalog /Pages 2 0 R >>"},
		{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [3 0 R] >>"},
		{3, "<< /Type /Page /Parent 2 0 R >>"},
		{10, "[1 2 3]"},
	});
	auto doc = Document::open_memory(bytes);
	ASSERT_TRUE(doc.has_value());
	for (int i = 0; i < 3; ++i) {
		auto value = (*doc)->resolve_object(IndirectRef{10u, 0u});
		ASSERT_TRUE(value.has_value());
		ASSERT_NE(value->as_array(), nullptr);
		ASSERT_EQ(value->as_array()->size(), 3u) << "a caller's change reached the cache, pass " << i;
		value->as_array_mut()->push_back(PdfValue(int64_t{4}));
	}
	const auto shared = (*doc)->resolve_object_shared(IndirectRef{10u, 0u});
	ASSERT_TRUE(shared.has_value());
	EXPECT_EQ((*shared)->as_array()->size(), 3u);
	EXPECT_EQ((*doc)->resolve_object_shared(IndirectRef{10u, 0u}).value().get(), shared->get()) << "the shared value was copied";
}

TEST(PdfDocument, OutlineTraversalHoldsOneItemAtATime)
{
	constexpr uint32_t kLevels = 64u;
	std::string pad;
	for (size_t i = 0u; i < 10'000u; ++i) {
		pad += "0 ";
	}
	std::vector<std::pair<uint32_t, std::string>> objects = {
		{1, "<< /Type /Catalog /Pages 2 0 R /Outlines 5 0 R >>"},
		{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [3 0 R] >>"},
		{3, "<< /Type /Page /Parent 2 0 R >>"},
		{5, "<< /Type /Outlines /First 10 0 R /Last 10 0 R /Count 1 >>"},
	};
	for (uint32_t level = 0u; level < kLevels; ++level) {
		const uint32_t num = 10u + level;
		std::string item = "<< /Title (Level " + std::to_string(level) + ") /Parent " +
			std::to_string(level == 0u ? 5u : num - 1u) + " 0 R /Dest [3 0 R /Fit] /Pad [" + pad + "]";
		if (level + 1u < kLevels) {
			item += " /First " + std::to_string(num + 1u) + " 0 R /Last " + std::to_string(num + 1u) + " 0 R /Count 1";
		}
		objects.emplace_back(num, item + " >>");
	}
	const auto bytes = build_synthetic_pdf_from_objects("1.7", objects);
	const size_t one_item_bytes = 10'000u * sizeof(PdfValue);

	const test::ScopedHeapMeter meter;
	auto doc = Document::open_memory(bytes);
	const size_t peak = meter.peak_bytes();
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	const std::vector<OutlineNode>* level_nodes = &(*doc)->outline();
	for (uint32_t level = 0u; level < kLevels; ++level) {
		ASSERT_EQ(level_nodes->size(), 1u) << "level " << level;
		EXPECT_EQ((*level_nodes)[0].title, "Level " + std::to_string(level));
		EXPECT_EQ((*level_nodes)[0].page_index, 0);
		level_nodes = &(*level_nodes)[0].children;
	}
	EXPECT_TRUE(level_nodes->empty());
	if (test::ScopedHeapMeter::available()) {
		// The file's own copy plus a few items; every ancestor kept alive would be 64 items.
		EXPECT_LT(peak, bytes.size() + 8u * one_item_bytes);
	}
}

// Pages that share an /Annots array read it once, and an annotation found not to be a link is not read again: 1,000
// pages sharing one array of 1,000 Text annotations and a link, and 300 pages each listing the same 300 Text
// annotations directly, open with every page's link and a few thousand annotation reads instead of 1,090,000. Another
// page entry naming a shared object is validated once too (D06 T05 section 24, audit round 2).
TEST(PdfDocument, SharedAnnotationsAreReadOnce)
{
	constexpr uint32_t kSharedPages = 1'000u;
	constexpr uint32_t kSharedAnnots = 1'000u;
	constexpr uint32_t kDirectPages = 300u;
	constexpr uint32_t kDirectAnnots = 300u;
	const uint32_t first_annot = 100u;
	const uint32_t link_annot = first_annot + kSharedAnnots;
	const uint32_t first_page = link_annot + 1u;

	std::vector<std::pair<uint32_t, std::string>> objects = {{1, "<< /Type /Catalog /Pages 2 0 R >>"}};
	std::string shared = "[";
	for (uint32_t a = 0u; a < kSharedAnnots; ++a) {
		shared += std::to_string(first_annot + a) + " 0 R ";
		objects.emplace_back(first_annot + a, "<< /Type /Annot /Subtype /Text /Rect [0 0 5 5] /Contents (note) >>");
	}
	shared += std::to_string(link_annot) + " 0 R]";
	objects.emplace_back(4u, shared);
	objects.emplace_back(
		link_annot, "<< /Type /Annot /Subtype /Link /Rect [10 10 20 20] /Dest [" + std::to_string(first_page) + " 0 R /Fit] >>");
	std::string direct = "[";
	for (uint32_t a = 0u; a < kDirectAnnots; ++a) {
		direct += std::to_string(first_annot + a) + " 0 R ";
	}
	direct += "]";
	std::string kids;
	for (uint32_t p = 0u; p < kSharedPages + kDirectPages; ++p) {
		kids += std::to_string(first_page + p) + " 0 R ";
		objects.emplace_back(
			first_page + p,
			"<< /Type /Page /Parent 2 0 R /Annots " + (p < kSharedPages ? std::string("4 0 R") : direct) + " >>");
	}
	objects.emplace_back(
		2u,
		"<< /Type /Pages /Count " + std::to_string(kSharedPages + kDirectPages) + " /MediaBox [0 0 100 100] /Kids [" +
			kids + "] >>");
	const auto bytes = build_synthetic_pdf_from_objects("1.7", objects);

	const auto allocated_to_open = [&](std::string_view shared_entry) {
		auto variant = objects;
		for (auto& [num, body] : variant) {
			if (const size_t at = body.find("/Annots 4 0 R"); at != std::string::npos) {
				body.replace(at, 13u, shared_entry);
			} else if (const size_t direct_at = body.find("/Annots "); direct_at != std::string::npos) {
				body.replace(direct_at, 8u, "/Pad ");
			}
		}
		const auto variant_bytes = build_synthetic_pdf_from_objects("1.7", variant);
		const test::ScopedHeapMeter variant_meter;
		auto variant_doc = Document::open_memory(variant_bytes);
		EXPECT_TRUE(variant_doc.has_value());
		return variant_meter.allocated_bytes();
	};
	// The same pages with the arrays under /Pad: page-tree validation reads the shared array once, not once per page,
	// and nothing reads an annotation. With the key gone the shared array is not read at all.
	const size_t baseline_allocated = allocated_to_open("/Pad 4 0 R");
	const size_t unshared_allocated = allocated_to_open("/Pad null");

	const test::ScopedHeapMeter meter;
	const auto started = std::chrono::steady_clock::now();
	auto doc = Document::open_memory(bytes);
	const auto elapsed = std::chrono::steady_clock::now() - started;
	const size_t allocated = meter.allocated_bytes();
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	for (uint32_t p = 0u; p < kSharedPages + kDirectPages; ++p) {
		auto links = (*doc)->page_links(p);
		ASSERT_TRUE(links.has_value()) << links.error().message.view();
		ASSERT_EQ(links->size(), p < kSharedPages ? 1u : 0u) << "page " << p;
		if (p < kSharedPages) {
			EXPECT_EQ((*links)[0].kind, LinkKind::Internal);
			EXPECT_EQ((*links)[0].page_index, 0);
		}
	}
	// About 4 s under AddressSanitizer with the fix; reading every page's annotations again took 7 s without it in a
	// Release build. The heap meter is the precise check where it measures.
	EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 15000);
	if (test::ScopedHeapMeter::available()) {
		// Reading the annotations adds a few thousand dictionaries to the baseline; reading every page's annotations
		// again would add 1,090,000 of them (13 GB allocated in a Release build without the fix).
		EXPECT_LT(allocated, baseline_allocated + (size_t{32} << 20u))
			<< "baseline " << baseline_allocated << " bytes";
		EXPECT_LT(baseline_allocated, unshared_allocated + (size_t{32} << 20u))
			<< "without the shared array " << unshared_allocated << " bytes";
	}
}

// The /Names /Dests tree is read one node at a time: 64 nested nodes with a 10,000-element /Pad array each open within
// a few nodes' memory, without recursion, and the leaf's destination still resolves (D06 T05 section 24, audit round 3).
TEST(PdfDocument, NameTreeTraversalHoldsOneNodeAtATime)
{
	constexpr uint32_t kLevels = 64u;
	std::string pad;
	for (size_t i = 0u; i < 10'000u; ++i) {
		pad += "0 ";
	}
	std::vector<std::pair<uint32_t, std::string>> objects = {
		{1, "<< /Type /Catalog /Pages 2 0 R /Outlines 5 0 R /Names << /Dests 10 0 R >> >>"},
		{2, "<< /Type /Pages /Count 2 /MediaBox [0 0 100 100] /Kids [3 0 R 4 0 R] >>"},
		{3, "<< /Type /Page /Parent 2 0 R >>"},
		{4, "<< /Type /Page /Parent 2 0 R >>"},
		{5, "<< /Type /Outlines /First 6 0 R /Last 6 0 R /Count 1 >>"},
		{6, "<< /Title (Deep) /Parent 5 0 R /Dest (target) >>"},
	};
	for (uint32_t level = 0u; level < kLevels; ++level) {
		const uint32_t num = 10u + level;
		std::string node = "<< /Pad [" + pad + "]";
		node += level + 1u < kLevels ? " /Kids [" + std::to_string(num + 1u) + " 0 R]" : " /Names [(target) [4 0 R /Fit]]";
		objects.emplace_back(num, node + " >>");
	}
	const auto bytes = build_synthetic_pdf_from_objects("1.7", objects);
	const size_t one_node_bytes = 10'000u * sizeof(PdfValue);

	const test::ScopedHeapMeter meter;
	auto doc = Document::open_memory(bytes);
	const size_t peak = meter.peak_bytes();
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	ASSERT_EQ((*doc)->outline().size(), 1u);
	EXPECT_EQ((*doc)->outline()[0].page_index, 1);
	if (test::ScopedHeapMeter::available()) {
		// The file's own copy plus a few nodes; every ancestor kept alive would be 64 nodes.
		EXPECT_LT(peak, bytes.size() + 8u * one_node_bytes);
	}
}

// Nested Form XObjects are scanned for fonts one stream at a time: 32 levels of forms with a 2 MiB stream each open
// within the file's own copy and a few streams, and the font of the deepest form is found (D06 T05 section 24, audit
// round 3).
TEST(PdfDocument, FormXObjectFontScanHoldsOneStreamAtATime)
{
	constexpr uint32_t kLevels = 32u;
	const std::string body(2u * 1024u * 1024u, ' ');
	std::vector<std::pair<uint32_t, std::string>> objects = {
		{1, "<< /Type /Catalog /Pages 2 0 R >>"},
		{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [3 0 R] >>"},
		{3, "<< /Type /Page /Parent 2 0 R /Resources << /XObject << /X 10 0 R >> >> >>"},
		{4, "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>"},
	};
	for (uint32_t level = 0u; level < kLevels; ++level) {
		const uint32_t num = 10u + level;
		const std::string resources = level + 1u < kLevels
			? "<< /XObject << /X " + std::to_string(num + 1u) + " 0 R >> >>"
			: std::string("<< /Font << /F1 4 0 R >> >>");
		objects.emplace_back(
			num,
			"<< /Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources " + resources + " /Length " +
				std::to_string(body.size()) + " >>\nstream\n" + body + "\nendstream");
	}
	const auto bytes = build_synthetic_pdf_from_objects("1.7", objects);

	const test::ScopedHeapMeter meter;
	auto doc = Document::open_memory(bytes);
	const size_t peak = meter.peak_bytes();
	ASSERT_TRUE(doc.has_value()) << doc.error().message.view();
	ASSERT_EQ((*doc)->fonts().size(), 1u);
	EXPECT_EQ((*doc)->fonts()[0].name, "Courier");
	if (test::ScopedHeapMeter::available()) {
		// The file's own copy plus a few streams; every ancestor stream kept alive would be 32 of them.
		EXPECT_LT(peak, bytes.size() + 8u * body.size());
	}
}

// Every open level of the page tree keeps its /Kids array, so the arrays on the stack share one budget: 64 nested
// /Pages nodes whose /Kids each carry a 40,000-element array besides the next node are refused with ResourceLimit
// while they are open, instead of holding them all (D06 T05 section 24, audit round 3).
TEST(PdfDocument, OpenPageTreeLevelsShareOneBudget)
{
	static_assert(64u * 40'000u * sizeof(PdfValue) > kMaxDecodedStreamBytes);
	constexpr uint32_t kLevels = 64u;
	std::string filler = "[";
	for (size_t i = 0u; i < 40'000u; ++i) {
		filler += "0 ";
	}
	filler += "]";
	std::vector<std::pair<uint32_t, std::string>> objects = {{1, "<< /Type /Catalog /Pages 10 0 R >>"}};
	for (uint32_t level = 0u; level < kLevels; ++level) {
		const uint32_t num = 10u + level;
		const std::string kid = level + 1u < kLevels ? std::to_string(num + 1u) + " 0 R" : std::string("200 0 R");
		objects.emplace_back(num, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [" + kid + " " + filler + "] >>");
	}
	objects.emplace_back(200u, "<< /Type /Page /Parent 73 0 R >>");
	auto doc = Document::open_memory(build_synthetic_pdf_from_objects("1.7", objects));
	ASSERT_FALSE(doc.has_value());
	EXPECT_EQ(doc.error().code, ErrorCode::ResourceLimit);
}

// An action chain's pending /Next values are copies held until inspected, so they share one small budget: a link
// whose 32 chained GoTo actions each leave a padded direct sibling pending fails closed (inert), while the same chain
// with small siblings stays a working internal link (D06 T05 section 24, audit round 4).
TEST(PdfDocument, PendingActionSiblingsShareOneBudget)
{
	const auto link_for = [](size_t pad_elements) {
		std::string pad;
		for (size_t i = 0u; i < pad_elements; ++i) {
			pad += "0 ";
		}
		std::vector<std::pair<uint32_t, std::string>> objects = {
			{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 100 100] /Kids [3 0 R] >>"},
			{3, "<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>"},
			{4, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A 10 0 R >>"},
		};
		constexpr uint32_t kChain = 32u;
		for (uint32_t i = 0u; i < kChain; ++i) {
			std::string action = "<< /S /GoTo /D [3 0 R /Fit]";
			if (i + 1u < kChain) {
				action += " /Next [" + std::to_string(11u + i) + " 0 R << /S /GoTo /D [3 0 R /Fit] /Pad [" + pad + "] >>]";
			}
			objects.emplace_back(10u + i, action + " >>");
		}
		auto doc = Document::open_memory(build_synthetic_pdf_from_objects("1.7", objects));
		EXPECT_TRUE(doc.has_value());
		auto links = doc.has_value() ? (*doc)->page_links(0) : PdfResult<std::vector<PageLink>>(std::vector<PageLink>{});
		EXPECT_TRUE(links.has_value());
		return links.has_value() && links->size() == 1u ? std::optional<PageLink>((*links)[0]) : std::nullopt;
	};

	const auto small = link_for(1u);
	ASSERT_TRUE(small.has_value());
	EXPECT_EQ(small->kind, LinkKind::Internal);
	EXPECT_FALSE(small->is_inert);

	const auto padded = link_for(2'000u);
	ASSERT_TRUE(padded.has_value());
	EXPECT_EQ(padded->kind, LinkKind::RefusedAction);
	EXPECT_TRUE(padded->is_inert);
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

	// 5. Repeated `/Kids` references (`4,096` repeated `3 0 R` entries and
	//    `4,096` indirect aliases `20 0 R .. 4115 0 R` -> `3 0 R`) with a 64 KiB
	//    inherited `/Resources` payload reject on the second child without
	//    duplicating `current_attrs.resources` across queued siblings:
	{
		auto get_working_set_bytes = []() -> size_t {
			PROCESS_MEMORY_COUNTERS pmc{};
			pmc.cb = sizeof(pmc);
			if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc)) != FALSE) {
				return static_cast<size_t>(pmc.WorkingSetSize);
			}
			return 0u;
		};

		const std::string payload_64k(65536u, 'R');
		std::string repeated_kids = "[";
		for (uint32_t i = 0; i < 4096u; ++i) {
			repeated_kids += "3 0 R ";
		}
		repeated_kids += "]";

		const auto repeated_bytes = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2,
				 "<< /Type /Pages /Count 4096 /MediaBox [0 0 612 792] "
				 "/Resources << /Payload (" + payload_64k + ") >> "
				 "/Kids " + repeated_kids + " >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
			});

		const size_t ws_before = get_working_set_bytes();
		auto repeated_res = Document::open_memory(repeated_bytes);
		const size_t ws_after = get_working_set_bytes();
		const size_t ws_delta = (ws_after > ws_before) ? (ws_after - ws_before) : 0u;

		ASSERT_FALSE(repeated_res.has_value());
		EXPECT_EQ(repeated_res.error().code, ErrorCode::DamagedFormat);
		EXPECT_EQ(repeated_res.error().object_num, std::optional<uint32_t>(3u));
		EXPECT_LT(ws_delta, 32u * 1024u * 1024u)
			<< "Page-tree sibling expansion caused excessive working-set growth: "
			<< ws_delta << " bytes";

		// Also verify indirect aliases (`20 0 R -> 3 0 R`, `21 0 R -> 3 0 R`, ...)
		// reject on the second alias (`21 0 R -> 3 0 R`) without expanding the
		// remaining 4,094 siblings:
		std::string aliased_kids = "[";
		for (uint32_t id = 20u; id < 20u + 4096u; ++id) {
			aliased_kids += std::to_string(id) + " 0 R ";
		}
		aliased_kids += "]";

		std::vector<std::pair<uint32_t, std::string>> alias_objs = {
			{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			{2,
			 "<< /Type /Pages /Count 4096 /MediaBox [0 0 612 792] "
			 "/Resources << /Payload (" + payload_64k + ") >> "
			 "/Kids " + aliased_kids + " >>"},
			{3, "<< /Type /Page /Parent 2 0 R >>"},
		};
		for (uint32_t id = 20u; id < 20u + 4096u; ++id) {
			alias_objs.emplace_back(id, "3 0 R");
		}
		const auto alias_bytes = build_synthetic_pdf_from_objects("1.7", alias_objs);
		const size_t ws_alias_before = get_working_set_bytes();
		auto alias_res = Document::open_memory(alias_bytes);
		const size_t ws_alias_after = get_working_set_bytes();
		const size_t ws_alias_delta =
			(ws_alias_after > ws_alias_before) ? (ws_alias_after - ws_alias_before) : 0u;

		ASSERT_FALSE(alias_res.has_value());
		EXPECT_EQ(alias_res.error().code, ErrorCode::DamagedFormat);
		EXPECT_EQ(alias_res.error().object_num, std::optional<uint32_t>(3u));
		EXPECT_LT(ws_alias_delta, 32u * 1024u * 1024u)
			<< "Aliased page-tree sibling expansion caused excessive working-set growth: "
			<< ws_alias_delta << " bytes";

		// 3c. 1,024 distinct `/Type /Page` leaves explicitly referencing the
		//     same indirect `/Resources 10 0 R` dictionary (with a 64 KiB
		//     payload) share one resolved `std::shared_ptr<const PdfDict>`
		//     without duplicating the 64 KiB dictionary per page (< 32 MiB
		//     working-set delta):
		std::string kids_explicit = "[";
		std::vector<std::pair<uint32_t, std::string>> objs_explicit = {
			{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			{10, "<< /Font << /F1 11 0 R /Payload (" + payload_64k + ") >> >>"},
			{11, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"},
		};
		for (uint32_t i = 0u; i < 1024u; ++i) {
			const uint32_t page_id = 20u + i;
			kids_explicit += std::to_string(page_id) + " 0 R ";
			objs_explicit.emplace_back(
				page_id,
				"<< /Type /Page /Parent 2 0 R /Resources 10 0 R >>");
		}
		kids_explicit += "]";
		objs_explicit.emplace_back(
			2u,
			"<< /Type /Pages /Count 1024 /MediaBox [0 0 612 792] /Kids " + kids_explicit + " >>");

		const auto bytes_explicit = build_synthetic_pdf_from_objects("1.7", objs_explicit);
		const size_t ws_exp_before = get_working_set_bytes();
		auto exp_res = Document::open_memory(bytes_explicit);
		ASSERT_TRUE(exp_res.has_value()) << exp_res.error().message.view();
		const size_t ws_exp_after = get_working_set_bytes();
		const size_t ws_exp_delta =
			(ws_exp_after > ws_exp_before) ? (ws_exp_after - ws_exp_before) : 0u;
		EXPECT_EQ((*exp_res)->page_count(), 1024u);
		EXPECT_EQ((*exp_res)->fonts().size(), 1u);
		EXPECT_LT(ws_exp_delta, 32u * 1024u * 1024u)
			<< "Explicit shared /Resources references across 1,024 pages duplicated memory: "
			<< ws_exp_delta << " bytes";

		// 3d. 10 distinct pages whose `/Resources` dictionaries each point to a
		//     31-hop `/Font` category alias chain converging on a single shared
		//     64 KiB `/Font` category dictionary (`10 0 R`) share one
		//     `std::shared_ptr<const PdfValue>` across all 300 alias hops without
		//     re-parsing or duplicating the 64 KiB category dictionary per hop,
		//     and exceeding the 16 MiB aggregate retained resource budget across
		//     distinct pages fails deterministically:
		{
			std::string cat_64k =
				"<< /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
				"/Note (" + std::string(65536u, 'c') + ") >> >>";
			std::string kids_cat_alias = "[";
			std::vector<std::pair<uint32_t, std::string>> objs_cat_alias = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{10, cat_64k},
			};
			uint32_t next_id = 20u;
			for (uint32_t p = 0u; p < 10u; ++p) {
				const uint32_t page_id = next_id++;
				const uint32_t res_id = next_id++;
				const uint32_t first_hop = next_id;
				kids_cat_alias += std::to_string(page_id) + " 0 R ";
				objs_cat_alias.emplace_back(
					page_id,
					"<< /Type /Page /Parent 2 0 R /Resources " +
						std::to_string(res_id) + " 0 R >>");
				objs_cat_alias.emplace_back(
					res_id,
					"<< /Font " + std::to_string(first_hop) + " 0 R >>");
				for (uint32_t hop = 0u; hop < 30u; ++hop) {
					const uint32_t cur_hop = next_id++;
					const uint32_t next_target = (hop == 29u) ? 10u : next_id;
					objs_cat_alias.emplace_back(
						cur_hop,
						std::to_string(next_target) + " 0 R");
				}
			}
			kids_cat_alias += "]";
			objs_cat_alias.emplace_back(
				2u,
				"<< /Type /Pages /Count 10 /MediaBox [0 0 612 792] /Kids " +
					kids_cat_alias + " >>");

			const auto bytes_cat_alias =
				build_synthetic_pdf_from_objects("1.7", objs_cat_alias);
			const size_t ws_cat_before = get_working_set_bytes();
			auto cat_res = Document::open_memory(bytes_cat_alias);
			ASSERT_TRUE(cat_res.has_value()) << cat_res.error().message.view();
			const size_t ws_cat_after = get_working_set_bytes();
			const size_t ws_cat_delta =
				(ws_cat_after > ws_cat_before) ? (ws_cat_after - ws_cat_before) : 0u;
			EXPECT_EQ((*cat_res)->page_count(), 10u);
			EXPECT_EQ((*cat_res)->fonts().size(), 1u);
			EXPECT_LT(ws_cat_delta, 32u * 1024u * 1024u)
				<< "Resource category alias chain duplicated 64 KiB category dictionary: "
				<< ws_cat_delta << " bytes";

			// Aggregate retained resource budget (> 16 MiB across distinct pages)
			// fails cleanly with DamagedFormat:
			std::string kids_over = "[";
			std::vector<std::pair<uint32_t, std::string>> objs_over = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
			};
			for (uint32_t p = 0u; p < 280u; ++p) {
				const uint32_t page_id = 10u + p * 2u;
				const uint32_t cat_id = 11u + p * 2u;
				kids_over += std::to_string(page_id) + " 0 R ";
				objs_over.emplace_back(
					cat_id,
					"<< /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
					"/Pad (" + std::string(65536u, 'x') + ") >> >>");
				objs_over.emplace_back(
					page_id,
					"<< /Type /Page /Parent 2 0 R /Resources << /Font " +
						std::to_string(cat_id) + " 0 R >> >>");
			}
			kids_over += "]";
			objs_over.emplace_back(
				2u,
				"<< /Type /Pages /Count 280 /MediaBox [0 0 612 792] /Kids " +
					kids_over + " >>");
			const auto bytes_over = build_synthetic_pdf_from_objects("1.7", objs_over);
			auto over_res = Document::open_memory(bytes_over);
			ASSERT_FALSE(over_res.has_value());
			EXPECT_EQ(over_res.error().code, ErrorCode::DamagedFormat);
		}

		// 3e. Commented (`% comment`) and `/ObjStm`-compressed `/Resources` and
		//     category alias chains still hit the cache and share a single
		//     retained dictionary (proven by staying within the 16 MiB resource
		//     budget across 300 `/Resources`-alias pages and 10 category-alias
		//     pages):
		{
			std::string res_commented =
				"<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Courier >> >> "
				"/Pad (" + std::string(65536u, 'm') + ") >>";
			std::string cat_commented =
				"<< /F1 << /Type /Font /Subtype /Type1 /BaseFont /Courier "
				"/Pad (" + std::string(900u * 1024u, 'n') + ") >> >>";
			// Object stream 5 holds the compressed `/Resources` dictionary 10 and the
			// compressed category dictionary 11 (a compressed object cannot be a bare
			// reference, ISO 32000-1 section 7.5.7, so the aliases stay uncompressed):
			const std::string objstm_header =
				"10 0 11 " + std::to_string(res_commented.size() + 1u) + " ";
			const std::string objstm_body = res_commented + " " + cat_commented;
			const std::string objstm_data = objstm_header + objstm_body;
			const std::string objstm_obj =
				"<< /Type /ObjStm /N 2 /First " +
				std::to_string(objstm_header.size()) +
				" /Length " + std::to_string(objstm_data.size()) +
				" >>\nstream\n" + objstm_data + "\nendstream";

			// Build hybrid PDF with:
			// - 300 pages referencing commented aliases of the compressed `/Resources`
			//   (`6 0 R -> 10 0 R` and `8 0 R -> 6 0 R -> 10 0 R`,
			//   64 KiB each; uncached = 18.75 MiB > 16 MiB budget)
			// - 10 pages referencing commented aliases of the compressed category
			//   (`7 0 R -> 11 0 R` and `9 0 R -> 7 0 R -> 11 0 R`,
			//   900 KiB each; uncached category cache + page dict = 18 MiB > 16 MiB budget)
			std::string pdf_sc = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
			std::vector<uint64_t> offsets(340u, 0u);
			auto append_obj = [&](uint32_t num, std::string_view body) {
				offsets[num] = static_cast<uint64_t>(pdf_sc.size());
				pdf_sc += std::to_string(num) + " 0 obj\n";
				pdf_sc.append(body);
				pdf_sc += "\nendobj\n";
			};
			append_obj(1, "<< /Type /Catalog /Pages 2 0 R >>");
			std::string kids_sc = "[";
			for (uint32_t p = 0u; p < 300u; ++p) {
				const uint32_t pid = 20u + p;
				kids_sc += std::to_string(pid) + " 0 R ";
				const uint32_t res_ref = (p % 2u == 0u) ? 6u : 8u;
				append_obj(
					pid,
					"<< /Type /Page /Parent 2 0 R /Resources " +
						std::to_string(res_ref) + " 0 R >>");
			}
			for (uint32_t c = 0u; c < 10u; ++c) {
				const uint32_t pid = 320u + c;
				kids_sc += std::to_string(pid) + " 0 R ";
				const uint32_t cat_ref = (c % 2u == 0u) ? 7u : 9u;
				append_obj(
					pid,
					"<< /Type /Page /Parent 2 0 R /Resources << /Font " +
						std::to_string(cat_ref) + " 0 R >> >>");
			}
			kids_sc += "]";
			append_obj(
				2,
				"<< /Type /Pages /Count 310 /MediaBox [0 0 612 792] /Kids " +
					kids_sc + " >>");
			append_obj(5, objstm_obj);
			append_obj(6, "% comment before alias\n10 0 R % trailing");
			append_obj(7, "% comment before cat alias\n11 0 R");
			append_obj(8, "% second commented alias\n6 0 R");
			append_obj(9, "% second commented cat alias\n7 0 R");
			const uint64_t off_xref = static_cast<uint64_t>(pdf_sc.size());

			std::string xref_bin;
			auto push_xref = [&](uint8_t type, uint32_t f2, uint16_t f3) {
				xref_bin.push_back(static_cast<char>(type));
				xref_bin.push_back(static_cast<char>((f2 >> 24) & 0xFFu));
				xref_bin.push_back(static_cast<char>((f2 >> 16) & 0xFFu));
				xref_bin.push_back(static_cast<char>((f2 >> 8) & 0xFFu));
				xref_bin.push_back(static_cast<char>(f2 & 0xFFu));
				xref_bin.push_back(static_cast<char>((f3 >> 8) & 0xFFu));
				xref_bin.push_back(static_cast<char>(f3 & 0xFFu));
			};
			push_xref(0, 0, 65535);
			for (uint32_t id = 1u; id <= 329u; ++id) {
				if (id == 10u) {
					push_xref(2, 5, 0);
				} else if (id == 11u) {
					push_xref(2, 5, 1);
				} else if (offsets[id] != 0u) {
					push_xref(1, static_cast<uint32_t>(offsets[id]), 0);
				} else {
					push_xref(0, 0, 65535);
				}
			}
			push_xref(1, static_cast<uint32_t>(off_xref), 0); // obj 330

			pdf_sc += "330 0 obj\n<< /Type /XRef /Size 331 /Root 1 0 R /W [1 4 2] /Length " +
				std::to_string(xref_bin.size()) + " >>\nstream\n" +
				xref_bin + "\nendstream\nendobj\nstartxref\n" +
				std::to_string(off_xref) + "\n%%EOF\n";

			const std::vector<uint8_t> bytes_sc(pdf_sc.begin(), pdf_sc.end());
			auto sc_res = Document::open_memory(bytes_sc);
			ASSERT_TRUE(sc_res.has_value()) << sc_res.error().message.view();
			EXPECT_EQ((*sc_res)->page_count(), 310u);
			ASSERT_EQ((*sc_res)->fonts().size(), 1u);
			EXPECT_EQ((*sc_res)->fonts()[0].name, "Courier");
			auto r0 = (*sc_res)->page_resources(0);
			auto r1 = (*sc_res)->page_resources(1);
			auto r300 = (*sc_res)->page_resources(300);
			auto r301 = (*sc_res)->page_resources(301);
			ASSERT_TRUE(r0.has_value());
			ASSERT_TRUE(r1.has_value());
			ASSERT_TRUE(r300.has_value());
			ASSERT_TRUE(r301.has_value());
			EXPECT_EQ(*r0, *r1);
			EXPECT_EQ(*r300, *r301);
		}

		// 3f. Generation-number preservation in `/Resources` and category caches:
		//     stale generation references (`10 1 R` / `12 1 R`) must resolve to
		//     `null` and must neither poison a subsequent live `10 0 R` / `12 0 R`
		//     lookup nor alias a previously cached `10 0 R` / `12 0 R` entry on
		//     each individual page's `page_resources(i)`:
		for (bool stale_first : {true, false}) {
			const std::string p_stale =
				"<< /Type /Page /Parent 2 0 R /Resources 10 1 R >>";
			const std::string p_live =
				"<< /Type /Page /Parent 2 0 R /Resources 10 0 R >>";
			const std::string p_cat_stale =
				"<< /Type /Page /Parent 2 0 R /Resources << /Font 12 1 R >> >>";
			const std::string p_cat_live =
				"<< /Type /Page /Parent 2 0 R /Resources << /Font 12 0 R >> >>";
			std::vector<std::pair<uint32_t, std::string>> objs_gen = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 4 /MediaBox [0 0 612 792] /Kids [3 0 R 4 0 R 5 0 R 6 0 R] >>"},
				{3, stale_first ? p_stale : p_live},
				{4, stale_first ? p_live : p_stale},
				{5, stale_first ? p_cat_stale : p_cat_live},
				{6, stale_first ? p_cat_live : p_cat_stale},
				{10, "<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"},
				{12, "<< /F2 << /Type /Font /Subtype /Type1 /BaseFont /Times-Roman >> >>"},
			};
			const auto bytes_gen = build_synthetic_pdf_from_objects("1.7", objs_gen);
			auto gen_res = Document::open_memory(bytes_gen);
			ASSERT_TRUE(gen_res.has_value()) << gen_res.error().message.view();
			EXPECT_EQ((*gen_res)->page_count(), 4u);
			EXPECT_EQ((*gen_res)->fonts().size(), 2u);

			const uint32_t idx_res_stale = stale_first ? 0u : 1u;
			const uint32_t idx_res_live = stale_first ? 1u : 0u;
			const uint32_t idx_cat_stale = stale_first ? 2u : 3u;
			const uint32_t idx_cat_live = stale_first ? 3u : 2u;

			auto r_stale = (*gen_res)->page_resources(idx_res_stale);
			auto r_live = (*gen_res)->page_resources(idx_res_live);
			auto r_cat_stale = (*gen_res)->page_resources(idx_cat_stale);
			auto r_cat_live = (*gen_res)->page_resources(idx_cat_live);
			ASSERT_TRUE(r_stale.has_value());
			ASSERT_TRUE(r_live.has_value());
			ASSERT_TRUE(r_cat_stale.has_value());
			ASSERT_TRUE(r_cat_live.has_value());
			EXPECT_TRUE(r_stale->empty());
			EXPECT_NE(r_live->get_dict("Font"), nullptr);
			EXPECT_EQ(r_cat_stale->get_dict("Font"), nullptr);
			EXPECT_NE(r_cat_live->get_dict("Font"), nullptr);
		}

		// 3g. Cached indirect-reference depth enforcement is order-independent:
		//     a 32-alias chain followed by a terminal `/Resources` dictionary
		//     (33 total hops > `kMaxIndirectRefDepth == 32`) must fail with
		//     `ErrorCode::DamagedFormat` whether an earlier page warmed the
		//     terminal dictionary (`132 0 R`), warmed every intermediate hop
		//     one by one (`132 0 R`, `131 0 R`, ..., `100 0 R`), or hit the
		//     full chain cold, while a 31-alias + 1-terminal chain (32 total
		//     hops) succeeds in both orders:
		for (bool warm_first : {true, false}) {
			std::vector<std::pair<uint32_t, std::string>> objs_depth = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 2 /MediaBox [0 0 612 792] /Kids [3 0 R 4 0 R] >>"},
				{3,
				 std::string("<< /Type /Page /Parent 2 0 R /Resources ") +
					 (warm_first ? "132 0 R" : "100 0 R") + " >>"},
				{4,
				 std::string("<< /Type /Page /Parent 2 0 R /Resources ") +
					 (warm_first ? "100 0 R" : "132 0 R") + " >>"},
				{132, "<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"},
			};
			for (uint32_t id = 100u; id < 132u; ++id) {
				objs_depth.emplace_back(id, std::to_string(id + 1u) + " 0 R");
			}
			const auto bytes_depth = build_synthetic_pdf_from_objects("1.7", objs_depth);
			auto depth_res = Document::open_memory(bytes_depth);
			ASSERT_FALSE(depth_res.has_value());
			EXPECT_EQ(depth_res.error().code, ErrorCode::DamagedFormat);

			// 31 aliases + 1 terminal (101..132 = 32 hops) is within limit:
			std::vector<std::pair<uint32_t, std::string>> objs_ok = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 2 /MediaBox [0 0 612 792] /Kids [3 0 R 4 0 R] >>"},
				{3,
				 std::string("<< /Type /Page /Parent 2 0 R /Resources ") +
					 (warm_first ? "132 0 R" : "101 0 R") + " >>"},
				{4,
				 std::string("<< /Type /Page /Parent 2 0 R /Resources ") +
					 (warm_first ? "101 0 R" : "132 0 R") + " >>"},
				{132, "<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"},
			};
			for (uint32_t id = 101u; id < 132u; ++id) {
				objs_ok.emplace_back(id, std::to_string(id + 1u) + " 0 R");
			}
			const auto bytes_ok = build_synthetic_pdf_from_objects("1.7", objs_ok);
			auto ok_res = Document::open_memory(bytes_ok);
			ASSERT_TRUE(ok_res.has_value()) << ok_res.error().message.view();
		}
		{
			// Incremental hop-by-hop cache warming (`132 0 R`, `131 0 R`, ..., `100 0 R`):
			std::vector<std::pair<uint32_t, std::string>> objs_inc = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{132, "<< /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >>"},
			};
			std::string kids_inc = "[";
			for (uint32_t step = 0u; step <= 32u; ++step) {
				const uint32_t pid = 10u + step;
				const uint32_t res_id = 132u - step;
				kids_inc += std::to_string(pid) + " 0 R ";
				objs_inc.emplace_back(
					pid,
					"<< /Type /Page /Parent 2 0 R /Resources " +
						std::to_string(res_id) + " 0 R >>");
			}
			kids_inc += "]";
			objs_inc.emplace_back(
				2u,
				"<< /Type /Pages /Count 33 /MediaBox [0 0 612 792] /Kids " +
					kids_inc + " >>");
			for (uint32_t id = 100u; id < 132u; ++id) {
				objs_inc.emplace_back(id, std::to_string(id + 1u) + " 0 R");
			}
			const auto bytes_inc = build_synthetic_pdf_from_objects("1.7", objs_inc);
			auto inc_res = Document::open_memory(bytes_inc);
			ASSERT_FALSE(inc_res.has_value());
			EXPECT_EQ(inc_res.error().code, ErrorCode::DamagedFormat);
		}
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

		// 3b. `/Catalog /Version` never downgrades a later header version
		//     (ISO 32000-1 section 7.5.2 / 7.7.2: lower `/1.4` on `%PDF-1.7`
		//     retains `"1.7"`, equal `/1.7` retains `"1.7"`, higher `/2.0`
		//     upgrades to `"2.0"`):
		for (const auto& [hdr_ver, cat_ver, expected_ver] :
			 std::vector<std::tuple<std::string_view, std::string_view, std::string_view>>{
				 {"1.7", "/1.4", "1.7"},
				 {"1.7", "/1.7", "1.7"},
				 {"1.4", "/1.7", "1.7"},
				 {"1.7", "/2.0", "2.0"},
			 }) {
			const auto ver_bytes = build_synthetic_pdf_from_objects(
				hdr_ver,
				{
					{1,
					 std::string("<< /Type /Catalog /Version ") +
						 std::string(cat_ver) + " /Pages 2 0 R >>"},
					{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
					{3, "<< /Type /Page /Parent 2 0 R >>"},
				});
			auto ver_doc = Document::open_memory(ver_bytes);
			ASSERT_TRUE(ver_doc.has_value()) << ver_doc.error().message.view();
			EXPECT_EQ((*ver_doc)->pdf_version(), expected_ver);
		}

		// 3c. XMP extraction resolves XML namespace bindings (`xmlns:dublin`,
		//     `xmlns:adobe`, `xmlns:basic`, `xmlns:r`, default `xmlns`,
		//     shorthand attributes, non-first `xml:lang='x-default'`, and
		//     ignores rebound `xmlns:dc` pointing to a non-Dublin-Core URI):
		{
			const std::string custom_ns_xmp =
				"<r:RDF xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>\n"
				" <r:Description xmlns:dc='http://example.com/not-dublin-core'>\n"
				"  <dc:title>Wrong Title From Rebound Prefix</dc:title>\n"
				" </r:Description>\n"
				" <r:Description xmlns:dublin='http://purl.org/dc/elements/1.1/'\n"
				"                xmlns:basic='http://ns.adobe.com/xap/1.0/'\n"
				"                xmlns:adobe='http://ns.adobe.com/pdf/1.3/'\n"
				"                basic:CreatorTool='Custom Prefix Tool'\n"
				"                adobe:Producer='Custom Prefix Producer'>\n"
				"  <dublin:title>\n"
				"   <r:Alt>\n"
				"    <r:li xml:lang='de'>Deutscher Titel</r:li>\n"
				"    <r:li xml:lang='x-default'>Namespace Resolved Title</r:li>\n"
				"   </r:Alt>\n"
				"  </dublin:title>\n"
				"  <dublin:creator><r:Seq><r:li>Custom Prefix Author</r:li></r:Seq></dublin:creator>\n"
				"  <description xmlns='http://purl.org/dc/elements/1.1/'>Default Namespace Subject</description>\n"
				"  <adobe:Keywords>ns-kw1, ns-kw2</adobe:Keywords>\n"
				"  <basic:CreateDate>2026-10-20T01:00:00Z</basic:CreateDate>\n"
				"  <basic:ModifyDate>2026-10-20T02:00:00Z</basic:ModifyDate>\n"
				" </r:Description>\n"
				"</r:RDF>\n";
			const std::string custom_meta_stm =
				"<< /Type /Metadata /Subtype /XML /Length " +
				std::to_string(custom_ns_xmp.size()) + " >>\nstream\n" +
				custom_ns_xmp + "\nendstream";
			const auto ns_bytes = build_synthetic_pdf_from_objects(
				"1.7",
				{
					{1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>"},
					{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
					{3, "<< /Type /Page /Parent 2 0 R >>"},
					{4, custom_meta_stm},
				});
			auto ns_doc_res = Document::open_memory(ns_bytes);
			ASSERT_TRUE(ns_doc_res.has_value()) << ns_doc_res.error().message.view();
			const Document& ns_doc = **ns_doc_res;
			EXPECT_EQ(ns_doc.metadata().title, "Namespace Resolved Title");
			EXPECT_EQ(ns_doc.metadata().author, "Custom Prefix Author");
			EXPECT_EQ(ns_doc.metadata().subject, "Default Namespace Subject");
			EXPECT_EQ(ns_doc.metadata().keywords, "ns-kw1, ns-kw2");
			EXPECT_EQ(ns_doc.metadata().creator, "Custom Prefix Tool");
			EXPECT_EQ(ns_doc.metadata().producer, "Custom Prefix Producer");
			ASSERT_TRUE(ns_doc.metadata().parsed_creation_date.has_value());
			EXPECT_EQ(ns_doc.metadata().parsed_creation_date->iso8601, "2026-10-20T01:00:00Z");
			ASSERT_TRUE(ns_doc.metadata().parsed_mod_date.has_value());
			EXPECT_EQ(ns_doc.metadata().parsed_mod_date->iso8601, "2026-10-20T02:00:00Z");
		}

		// 3d. Foreign `lang` attributes (`evil:lang='x-default'` or non-`xml`
		//     prefixes attempting to bind `http://www.w3.org/XML/1998/namespace`)
		//     never override `xml:lang`, and 16,000 namespace declarations on a
		//     single wide element are handled in O(N) time:
		{
			std::string wide_ns_decls;
			wide_ns_decls.reserve(16000u * 24u);
			for (uint32_t i = 0u; i < 16000u; ++i) {
				wide_ns_decls += " xmlns:u" + std::to_string(i) + "='urn:x:" + std::to_string(i) + "'";
			}
			const std::string lang_and_wide_xmp =
				"<r:RDF xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'\n"
				"       xmlns:evil='urn:unrelated'\n"
				"       xmlns:fake='http://www.w3.org/XML/1998/namespace'" +
				wide_ns_decls + ">\n"
				" <r:Description xmlns:dc='http://purl.org/dc/elements/1.1/'>\n"
				"  <dc:title>\n"
				"   <r:Alt>\n"
				"    <r:li xml:lang='de' evil:lang='x-default' fake:lang='x-default'>WRONG</r:li>\n"
				"    <r:li xml:lang='x-default'>RIGHT</r:li>\n"
				"   </r:Alt>\n"
				"  </dc:title>\n"
				" </r:Description>\n"
				"</r:RDF>\n";
			const std::string wide_meta_stm =
				"<< /Type /Metadata /Subtype /XML /Length " +
				std::to_string(lang_and_wide_xmp.size()) + " >>\nstream\n" +
				lang_and_wide_xmp + "\nendstream";
			const auto wide_bytes = build_synthetic_pdf_from_objects(
				"1.7",
				{
					{1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>"},
					{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
					{3, "<< /Type /Page /Parent 2 0 R >>"},
					{4, wide_meta_stm},
				});
			auto wide_doc_res = Document::open_memory(wide_bytes);
			ASSERT_TRUE(wide_doc_res.has_value()) << wide_doc_res.error().message.view();
			EXPECT_EQ((*wide_doc_res)->metadata().title, "RIGHT");
		}

		// 3e. Unmatched closing tags at root scope (`</bad>` before/after root
		//     or standalone) do not read `scopes[1]` out of bounds, and 60,000
		//     `&` characters without semicolons in `decode_xml_entities` decode
		//     in O(N) time:
		{
			const std::string stray_close_only = "</bad></orphan>";
			const std::string stray_only_stm =
				"<< /Type /Metadata /Subtype /XML /Length " +
				std::to_string(stray_close_only.size()) + " >>\nstream\n" +
				stray_close_only + "\nendstream";
			const auto stray_only_bytes = build_synthetic_pdf_from_objects(
				"1.7",
				{
					{1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>"},
					{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
					{3, "<< /Type /Page /Parent 2 0 R >>"},
					{4, stray_only_stm},
				});
			auto stray_only_doc = Document::open_memory(stray_only_bytes);
			ASSERT_TRUE(stray_only_doc.has_value()) << stray_only_doc.error().message.view();
			EXPECT_TRUE((*stray_only_doc)->metadata().title.empty());

			const std::string many_amps(60000u, '&');
			const std::string stray_and_amps_xmp =
				"</bad><r:RDF xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'\n"
				"            xmlns:dc='http://purl.org/dc/elements/1.1/'>\n"
				" <r:Description dc:author='" + many_amps + "'>\n"
				"  <dc:title>" + many_amps + "</dc:title>\n"
				" </r:Description>\n"
				"</r:RDF></extra>";
			const std::string stray_amps_stm =
				"<< /Type /Metadata /Subtype /XML /Length " +
				std::to_string(stray_and_amps_xmp.size()) + " >>\nstream\n" +
				stray_and_amps_xmp + "\nendstream";
			const auto stray_amps_bytes = build_synthetic_pdf_from_objects(
				"1.7",
				{
					{1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>"},
					{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
					{3, "<< /Type /Page /Parent 2 0 R >>"},
					{4, stray_amps_stm},
				});
			auto stray_amps_doc = Document::open_memory(stray_amps_bytes);
			ASSERT_TRUE(stray_amps_doc.has_value()) << stray_amps_doc.error().message.view();
			EXPECT_EQ((*stray_amps_doc)->metadata().title, many_amps);
			EXPECT_GE((*stray_amps_doc)->xmp_entity_scan_steps(), many_amps.size());
			EXPECT_LE((*stray_amps_doc)->xmp_entity_scan_steps(), many_amps.size() * 20u);
		}
	}

	// 4. `parse_pdf_date` handles `std::bad_alloc` cleanly without terminating
	//    on both `D:YYYY...` and ISO-8601 XMP date strings:
	{
		const std::string padded_iso = std::string(64u, ' ') + "2026-10-19T14:30:00+02:00";
		const std::string padded_pdf = std::string(64u, ' ') + "D:20261019143000+02'00'";
		for (int fail_after = 0; fail_after < 2; ++fail_after) {
			{
				detail::ScopedCryptoAllocFaultInjector guard(fail_after);
				EXPECT_FALSE(parse_pdf_date(padded_iso).has_value());
			}
			{
				detail::ScopedCryptoAllocFaultInjector guard(fail_after);
				EXPECT_FALSE(parse_pdf_date(padded_pdf).has_value());
			}
		}
		const auto ok_iso = parse_pdf_date(padded_iso);
		ASSERT_TRUE(ok_iso.has_value());
		EXPECT_EQ(ok_iso->iso8601, "2026-10-19T14:30:00+02:00");
	}

	// 5. Oversized `/Info` field (> 64 KiB) and oversized `/Catalog /Metadata`
	//    XMP stream (> 4 MiB) fail with typed `PdfError`:
	{
		const std::string title_65k(65537u, 'T');
		const auto bytes_info = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{4,
				 "<< /Type /Metadata /Subtype /XML /Length " +
				 std::to_string(title_65k.size() + 25u) +
				 " >>\nstream\n<dc:title>" + title_65k + "</dc:title>\nendstream"},
			});
		auto res_info = Document::open_memory(bytes_info);
		ASSERT_FALSE(res_info.has_value());
		EXPECT_EQ(res_info.error().code, ErrorCode::DamagedFormat);

		const std::string xmp_over_4m(4u * 1024u * 1024u + 64u, 'X');
		const auto bytes_xmp = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{4,
				 "<< /Type /Metadata /Subtype /XML /Length " +
				 std::to_string(xmp_over_4m.size()) +
				 " >>\nstream\n" + xmp_over_4m + "\nendstream"},
			});
		auto res_xmp = Document::open_memory(bytes_xmp);
		ASSERT_FALSE(res_xmp.has_value());
		EXPECT_EQ(res_xmp.error().code, ErrorCode::DecompressionLimitExceeded);
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

	// 4. Bounded allocation on `/Names /Dests` when 2,048 destination names
	//    reference a shared 64 KiB destination dictionary (`<< /D [3 0 R /XYZ
	//    90 650 1.5] /Payload (...) >>`): resolved destination objects are
	//    cached and stored as lightweight `ParsedDest` records without cloning
	//    the 64 KiB dictionary per name (< 32 MiB working-set delta):
	{
		auto get_working_set_bytes = []() -> size_t {
			PROCESS_MEMORY_COUNTERS pmc{};
			pmc.cb = sizeof(pmc);
			if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc)) != FALSE) {
				return static_cast<size_t>(pmc.WorkingSetSize);
			}
			return 0u;
		};

		const std::string payload_64k(65536u, 'D');
		std::string names_arr = "[";
		for (uint32_t i = 0u; i < 2048u; ++i) {
			names_arr += "(dest_" + std::to_string(i) + ") 10 0 R ";
		}
		names_arr += "]";

		const auto bytes = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1,
				 "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R "
				 "/Names << /Dests << /Names " + names_arr + " >> >> >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{4, "<< /Type /Outlines /First 5 0 R /Last 5 0 R /Count 1 >>"},
				{5, "<< /Title (Jump 2047) /Parent 4 0 R /Dest (dest_2047) >>"},
				{10, "<< /D [3 0 R /XYZ 90 650 1.5] /Payload (" + payload_64k + ") >>"},
			});

		const size_t ws_before = get_working_set_bytes();
		auto doc_res = Document::open_memory(bytes);
		ASSERT_TRUE(doc_res.has_value()) << doc_res.error().message.view();
		const size_t ws_after = get_working_set_bytes();
		const size_t ws_delta = (ws_after > ws_before) ? (ws_after - ws_before) : 0u;
		EXPECT_LT(ws_delta, 32u * 1024u * 1024u)
			<< "Named destinations amplified memory on shared destination dictionary: "
			<< ws_delta << " bytes";

		ASSERT_EQ((*doc_res)->outline().size(), 1u);
		EXPECT_EQ((*doc_res)->outline()[0].page_index, 0);
		EXPECT_DOUBLE_EQ((*doc_res)->outline()[0].dest_x_pt, 90.0);
		EXPECT_DOUBLE_EQ((*doc_res)->outline()[0].dest_y_pt, 650.0);
		EXPECT_DOUBLE_EQ((*doc_res)->outline()[0].zoom, 1.5);
		EXPECT_EQ((*doc_res)->outline()[0].fit_mode, "XYZ");

		// Also verify that 2,048 destination names sharing a destination array
		// with a 64 KiB invalid fit-mode name (`[3 0 R /DDDD...]`) do not copy
		// or retain the 64 KiB fit-mode string (< 32 MiB working-set delta):
		const auto bytes_bad_fit = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1,
				 "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R "
				 "/Names << /Dests << /Names " + names_arr + " >> >> >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{4, "<< /Type /Outlines /First 5 0 R /Last 5 0 R /Count 1 >>"},
				{5, "<< /Title (Jump Invalid Fit) /Parent 4 0 R /Dest (dest_2047) >>"},
				{10, "[3 0 R /" + payload_64k + "]"},
			});

		const size_t ws_fit_before = get_working_set_bytes();
		auto doc_fit_res = Document::open_memory(bytes_bad_fit);
		ASSERT_TRUE(doc_fit_res.has_value()) << doc_fit_res.error().message.view();
		const size_t ws_fit_after = get_working_set_bytes();
		const size_t ws_fit_delta = (ws_fit_after > ws_fit_before)
			? (ws_fit_after - ws_fit_before)
			: 0u;
		EXPECT_LT(ws_fit_delta, 32u * 1024u * 1024u)
			<< "Named destinations amplified memory on 64 KiB fit-mode name: "
			<< ws_fit_delta << " bytes";
		ASSERT_EQ((*doc_fit_res)->outline().size(), 1u);
		EXPECT_FALSE((*doc_fit_res)->outline()[0].has_destination);
		EXPECT_EQ((*doc_fit_res)->outline()[0].page_index, -1);

		// Oversized `/Outlines` item `/Title` (> 4 KiB) fails with `ErrorCode::DamagedFormat`:
		const std::string title_8k(8192u, 'O');
		const auto bytes_bad_outline = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{4, "<< /Type /Outlines /First 5 0 R /Last 5 0 R /Count 1 >>"},
				{5, "<< /Title (" + title_8k + ") /Parent 4 0 R /Dest [3 0 R /Fit] >>"},
			});
		auto bad_outline_res = Document::open_memory(bytes_bad_outline);
		ASSERT_FALSE(bad_outline_res.has_value());
		EXPECT_EQ(bad_outline_res.error().code, ErrorCode::DamagedFormat);

		// Destination alias-cache budget enforcement: 2,500 named destinations
		// each with a 30-hop distinct indirect alias chain converging on `10 0 R`
		// exceed `kMaxCachedDestObjs` (65,536 entries) and fail deterministically
		// with `ErrorCode::DamagedFormat`:
		{
			std::vector<std::pair<uint32_t, std::string>> objs_dest_budget = {
				{1, "<< /Type /Catalog /Pages 2 0 R /Names << /Dests 5 0 R >> >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R >>"},
				{10, "[3 0 R /Fit]"},
			};
			std::string dest_names_arr = "[";
			for (uint32_t i = 0u; i < 2500u; ++i) {
				const uint32_t start_id = 20u + i * 30u;
				dest_names_arr += "(d" + std::to_string(i) + ") " + std::to_string(start_id) + " 0 R ";
				for (uint32_t hop = 0u; hop < 30u; ++hop) {
					const uint32_t next_id = (hop == 29u) ? 10u : (start_id + hop + 1u);
					objs_dest_budget.emplace_back(
						start_id + hop,
						std::to_string(next_id) + " 0 R");
				}
			}
			dest_names_arr += "]";
			objs_dest_budget.emplace_back(5u, "<< /Names " + dest_names_arr + " >>");
			const auto bytes_dest_budget =
				build_synthetic_pdf_from_objects("1.7", objs_dest_budget);
			auto dest_budget_res = Document::open_memory(bytes_dest_budget);
			ASSERT_FALSE(dest_budget_res.has_value());
			EXPECT_EQ(dest_budget_res.error().code, ErrorCode::DamagedFormat);
		}
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

		// 4c. 1,025 distinct `/Subtype /Link` annotations on a single page are
		//     extracted completely without silent truncation at 1,024:
		{
			std::string annots_arr = "[";
			std::vector<std::pair<uint32_t, std::string>> objs_1025 = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
			};
			for (uint32_t i = 0u; i < 1025u; ++i) {
				const uint32_t obj_id = 10u + i;
				annots_arr += std::to_string(obj_id) + " 0 R ";
				objs_1025.emplace_back(
					obj_id,
					"<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] "
					"/A << /S /URI /URI (https://example.com/" + std::to_string(i) + ") >> >>");
			}
			annots_arr += "]";
			objs_1025.emplace_back(3u, "<< /Type /Page /Parent 2 0 R /Annots " + annots_arr + " >>");

			const auto bytes_1025 = build_synthetic_pdf_from_objects("1.7", objs_1025);
			auto doc_1025 = Document::open_memory(bytes_1025);
			ASSERT_TRUE(doc_1025.has_value()) << doc_1025.error().message.view();
			auto links_1025 = (*doc_1025)->page_links(0);
			ASSERT_TRUE(links_1025.has_value()) << links_1025.error().message.view();
			ASSERT_EQ(links_1025->size(), 1025u);
			EXPECT_EQ(links_1025->back().uri, "https://example.com/1024");
		}

		// 4d. 2,048 distinct `/Subtype /Link` annotations referencing a shared
		//     64 KiB `/URI` action or carrying 6 KiB URIs exceeding the 2 MiB
		//     aggregate link budget fail with `ErrorCode::DamagedFormat` and
		//     bounded working-set growth (< 32 MiB):
		{
			const std::string uri_64k = "https://example.com/" + std::string(65536u, 'u');
			std::string annots_shared = "[";
			std::vector<std::pair<uint32_t, std::string>> objs_shared = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{10, "<< /S /URI /URI (" + uri_64k + ") >>"},
			};
			for (uint32_t i = 0u; i < 2048u; ++i) {
				const uint32_t obj_id = 20u + i;
				annots_shared += std::to_string(obj_id) + " 0 R ";
				objs_shared.emplace_back(
					obj_id,
					"<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A 10 0 R >>");
			}
			annots_shared += "]";
			objs_shared.emplace_back(3u, "<< /Type /Page /Parent 2 0 R /Annots " + annots_shared + " >>");

			const auto bytes_shared = build_synthetic_pdf_from_objects("1.7", objs_shared);
			const size_t ws_shared_before = get_working_set_bytes();
			auto doc_shared = Document::open_memory(bytes_shared);
			const size_t ws_shared_after = get_working_set_bytes();
			const size_t ws_shared_delta = (ws_shared_after > ws_shared_before)
				? (ws_shared_after - ws_shared_before)
				: 0u;
			ASSERT_FALSE(doc_shared.has_value());
			EXPECT_EQ(doc_shared.error().code, ErrorCode::DamagedFormat);
			EXPECT_LT(ws_shared_delta, 32u * 1024u * 1024u)
				<< "Shared 64 KiB /URI action caused excessive working-set growth: "
				<< ws_shared_delta << " bytes";

			// Also verify 2,048 distinct links with 6 KiB URIs (12 MiB total > 2 MiB cap)
			// fail cleanly with `ErrorCode::DamagedFormat` and < 32 MiB growth:
			const std::string uri_6k = "https://example.com/" + std::string(6144u, 'q');
			std::vector<std::pair<uint32_t, std::string>> objs_agg = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{10, "<< /S /URI /URI (" + uri_6k + ") >>"},
			};
			for (uint32_t i = 0u; i < 2048u; ++i) {
				const uint32_t obj_id = 20u + i;
				objs_agg.emplace_back(
					obj_id,
					"<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A 10 0 R >>");
			}
			objs_agg.emplace_back(3u, "<< /Type /Page /Parent 2 0 R /Annots " + annots_shared + " >>");

			const auto bytes_agg = build_synthetic_pdf_from_objects("1.7", objs_agg);
			const size_t ws_agg_before = get_working_set_bytes();
			auto doc_agg = Document::open_memory(bytes_agg);
			const size_t ws_agg_after = get_working_set_bytes();
			const size_t ws_agg_delta = (ws_agg_after > ws_agg_before)
				? (ws_agg_after - ws_agg_before)
				: 0u;
			ASSERT_FALSE(doc_agg.has_value());
			EXPECT_EQ(doc_agg.error().code, ErrorCode::DamagedFormat);
			EXPECT_LT(ws_agg_delta, 32u * 1024u * 1024u)
				<< "Aggregate link URI budget exceeded working-set limit: "
				<< ws_agg_delta << " bytes";
		}

		// 4e. 300 links, each with a 31-hop indirect alias chain converging
		//     on a single shared 6 KiB `/URI` action (`10 0 R`), share one
		//     `std::shared_ptr<const CachedActionSummary>` in the alias cache
		//     without duplicating the 6 KiB string per alias hop (< 32 MiB
		//     working-set delta):
		{
			const std::string uri_6k = "https://example.com/" + std::string(6144u, 'q');
			std::string annots_alias = "[";
			std::vector<std::pair<uint32_t, std::string>> objs_alias = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{10, "<< /S /URI /URI (" + uri_6k + ") >>"},
			};
			uint32_t next_free_id = 20u;
			for (uint32_t link_idx = 0u; link_idx < 300u; ++link_idx) {
				const uint32_t annot_id = next_free_id++;
				annots_alias += std::to_string(annot_id) + " 0 R ";
				const uint32_t first_alias_id = next_free_id;
				objs_alias.emplace_back(
					annot_id,
					"<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A " +
						std::to_string(first_alias_id) + " 0 R >>");
				for (uint32_t hop = 0u; hop < 30u; ++hop) {
					const uint32_t cur_alias = next_free_id++;
					const uint32_t target_id = (hop == 29u) ? 10u : next_free_id;
					objs_alias.emplace_back(cur_alias, std::to_string(target_id) + " 0 R");
				}
			}
			annots_alias += "]";
			objs_alias.emplace_back(
				3u,
				"<< /Type /Page /Parent 2 0 R /Annots " + annots_alias + " >>");

			const auto bytes_alias = build_synthetic_pdf_from_objects("1.7", objs_alias);
			const size_t ws_alias_before = get_working_set_bytes();
			auto doc_alias = Document::open_memory(bytes_alias);
			const size_t ws_alias_after = get_working_set_bytes();
			const size_t ws_alias_delta = (ws_alias_after > ws_alias_before)
				? (ws_alias_after - ws_alias_before)
				: 0u;
			ASSERT_TRUE(doc_alias.has_value()) << doc_alias.error().message.view();
			auto links_alias = (*doc_alias)->page_links(0);
			ASSERT_TRUE(links_alias.has_value()) << links_alias.error().message.view();
			EXPECT_EQ(links_alias->size(), 300u);
			EXPECT_LT(ws_alias_delta, 32u * 1024u * 1024u)
				<< "Action alias cache duplicated 6 KiB URI payload per alias hop: "
				<< ws_alias_delta << " bytes";
		}

		// 4f. 300 links with 31-hop `% comment` alias chains converging on a
		//     shared 6 KiB `/URI` action (`10 0 R`) still hit the fast scanner
		//     and action alias cache without duplicating memory (< 32 MiB
		//     working-set delta):
		{
			const std::string uri_6k = "https://example.com/" + std::string(6144u, 'z');
			std::string annots_c = "[";
			std::vector<std::pair<uint32_t, std::string>> objs_c = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{10, "<< /S /URI /URI (" + uri_6k + ") >>"},
			};
			uint32_t next_free_id = 20u;
			for (uint32_t link_idx = 0u; link_idx < 300u; ++link_idx) {
				const uint32_t annot_id = next_free_id++;
				annots_c += std::to_string(annot_id) + " 0 R ";
				const uint32_t first_alias_id = next_free_id;
				objs_c.emplace_back(
					annot_id,
					"<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A " +
						std::to_string(first_alias_id) + " 0 R >>");
				for (uint32_t hop = 0u; hop < 30u; ++hop) {
					const uint32_t cur_alias = next_free_id++;
					const uint32_t target_id = (hop == 29u) ? 10u : next_free_id;
					objs_c.emplace_back(
						cur_alias,
						"% comment in alias\n" + std::to_string(target_id) + " 0 R % tail");
				}
			}
			annots_c += "]";
			objs_c.emplace_back(
				3u,
				"<< /Type /Page /Parent 2 0 R /Annots " + annots_c + " >>");

			const auto bytes_c = build_synthetic_pdf_from_objects("1.7", objs_c);
			const size_t ws_c_before = get_working_set_bytes();
			auto doc_c = Document::open_memory(bytes_c);
			const size_t ws_c_after = get_working_set_bytes();
			const size_t ws_c_delta = (ws_c_after > ws_c_before)
				? (ws_c_after - ws_c_before)
				: 0u;
			ASSERT_TRUE(doc_c.has_value()) << doc_c.error().message.view();
			auto links_c = (*doc_c)->page_links(0);
			ASSERT_TRUE(links_c.has_value()) << links_c.error().message.view();
			EXPECT_EQ(links_c->size(), 300u);
			EXPECT_LT(ws_c_delta, 32u * 1024u * 1024u)
				<< "Commented action alias chain duplicated 6 KiB URI payload: "
				<< ws_c_delta << " bytes";
		}

		// 4g. Generation-number preservation in `/A` action and `/Dest` caches:
		//     stale generation references (`10 1 R` / `11 1 R`) must not poison
		//     or alias live generation references (`10 0 R` / `11 0 R`) in
		//     either ordering:
		for (bool stale_first : {true, false}) {
			const std::string a_stale =
				"<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A 10 1 R >>";
			const std::string a_live =
				"<< /Type /Annot /Subtype /Link /Rect [0 10 10 20] /A 10 0 R >>";
			const std::string d_stale =
				"<< /Type /Annot /Subtype /Link /Rect [0 20 10 30] /Dest 11 1 R >>";
			const std::string d_live =
				"<< /Type /Annot /Subtype /Link /Rect [0 30 10 40] /Dest 11 0 R >>";
			std::vector<std::pair<uint32_t, std::string>> objs_gen_act = {
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R /Annots [4 0 R 5 0 R 6 0 R 7 0 R] >>"},
				{4, stale_first ? a_stale : a_live},
				{5, stale_first ? a_live : a_stale},
				{6, stale_first ? d_stale : d_live},
				{7, stale_first ? d_live : d_stale},
				{10, "<< /S /URI /URI (https://example.com/live-gen0) >>"},
				{11, "[3 0 R /Fit]"},
			};
			const auto bytes_gen_act =
				build_synthetic_pdf_from_objects("1.7", objs_gen_act);
			auto doc_gen_act = Document::open_memory(bytes_gen_act);
			ASSERT_TRUE(doc_gen_act.has_value()) << doc_gen_act.error().message.view();
			auto links_gen = (*doc_gen_act)->page_links(0);
			ASSERT_TRUE(links_gen.has_value()) << links_gen.error().message.view();
			ASSERT_EQ(links_gen->size(), 4u);
			const size_t idx_a_stale = stale_first ? 0u : 1u;
			const size_t idx_a_live = stale_first ? 1u : 0u;
			const size_t idx_d_stale = stale_first ? 2u : 3u;
			const size_t idx_d_live = stale_first ? 3u : 2u;
			EXPECT_EQ((*links_gen)[idx_a_stale].kind, LinkKind::RefusedAction);
			EXPECT_TRUE((*links_gen)[idx_a_stale].uri.empty());
			EXPECT_EQ((*links_gen)[idx_a_live].kind, LinkKind::ExternalUri);
			EXPECT_EQ((*links_gen)[idx_a_live].uri, "https://example.com/live-gen0");
			EXPECT_EQ((*links_gen)[idx_d_stale].kind, LinkKind::RefusedAction);
			EXPECT_EQ((*links_gen)[idx_d_stale].page_index, -1);
			EXPECT_EQ((*links_gen)[idx_d_live].kind, LinkKind::Internal);
			EXPECT_EQ((*links_gen)[idx_d_live].page_index, 0);
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

		// Oversized `/Font /BaseFont` (> 1 KiB) fails with `ErrorCode::DamagedFormat`:
		const std::string bad_bf(2048u, 'F');
		const auto bytes_bad_font = build_synthetic_pdf_from_objects(
			"1.7",
			{
				{1, "<< /Type /Catalog /Pages 2 0 R >>"},
				{2, "<< /Type /Pages /Count 1 /MediaBox [0 0 612 792] /Kids [3 0 R] >>"},
				{3, "<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 4 0 R >> >> >>"},
				{4, "<< /Type /Font /Subtype /Type1 /BaseFont /" + bad_bf + " >>"},
			});
		auto bad_font_res = Document::open_memory(bytes_bad_font);
		ASSERT_FALSE(bad_font_res.has_value());
		EXPECT_EQ(bad_font_res.error().code, ErrorCode::DamagedFormat);

		// Equivalent font dictionaries across pages merge `has_to_unicode` and
		// `has_differences` regardless of page/font encounter order:
		for (bool diff_first : {true, false}) {
			const std::string f_plain =
				"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
				"/Encoding /WinAnsiEncoding /ToUnicode 12 0 R >>";
			const std::string f_diff =
				"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
				"/Encoding << /BaseEncoding /WinAnsiEncoding /Differences [65 /Euro] >> >>";
			const auto bytes_merge = build_synthetic_pdf_from_objects(
				"1.7",
				{
					{1, "<< /Type /Catalog /Pages 2 0 R >>"},
					{2, "<< /Type /Pages /Count 2 /MediaBox [0 0 612 792] /Kids [3 0 R 4 0 R] >>"},
					{3, "<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 10 0 R >> >> >>"},
					{4, "<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 11 0 R >> >> >>"},
					{10, diff_first ? f_diff : f_plain},
					{11, diff_first ? f_plain : f_diff},
					{12, "<< /Length 4 >>\nstream\nCMAP\nendstream"},
				});
			auto merge_res = Document::open_memory(bytes_merge);
			ASSERT_TRUE(merge_res.has_value()) << merge_res.error().message.view();
			ASSERT_EQ((*merge_res)->fonts().size(), 1u);
			EXPECT_TRUE((*merge_res)->fonts()[0].has_to_unicode);
			EXPECT_TRUE((*merge_res)->fonts()[0].has_differences);
		}
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
