// Unit tests for `recto::pdf::SecurityHandler` (`src/pdf/crypto.h` and
// `src/pdf/crypto.cpp`, D01 T01 section 9):
// - `/Encrypt` dictionary parsing across `R=2..6` and rejection of unknown
//   `/Filter` or invalid `/V`/`/R` combinations (`ErrorCode::UnsupportedEncryption`).
// - `R=2, 3, 4` MD5 key derivation (`BCRYPT_MD5_ALGORITHM`) and `RC4` / `AES-128-CBC`
//   authentication (`render/encrypted-rc4-128.pdf`, `render/encrypted-aes-128.pdf`).
// - `R=5, 6` (`AES-256`) Algorithm 2.B iterative SHA-256/SHA-384/SHA-512 +
//   AES-128-CBC key derivation, `/UE` and `/OE` 32-byte file key unwrap, and
//   `/Perms` AES-256-ECB verification (`render/encrypted-aes-256.pdf`).
// - Empty user password (`""`) auto-unlock on open for owner-restricted PDFs.
// - Per-object `PdfString` and `PdfStream` decryption and `FlateDecode`
//   decompression across all three encrypted corpus fixtures.
// - Exemptions per ISO 32000-1 section 7.6.2 (`/Encrypt` dictionary, trailer
//   `/ID`, `/Sig /Contents` signature hex strings, `/Type /XRef`, unencrypted
//   `/Type /Metadata`, and inner objects of an already-decrypted `/Type /ObjStm`).
// - Structured `PdfPermissions` extraction from `/P` bitmask (ISO 32000-1 Table 22).

#include "crypto.h"
#include "document.h"

#include "filters.h"
#include "parser.h"
#include "test_support.h"
#include "xref.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {
namespace {

[[nodiscard]] std::vector<uint8_t> read_corpus_bytes(std::string_view rel_path)
{
	return ::recto::test::read_file_bytes(::recto::test::corpus_path(rel_path));
}

[[nodiscard]] PdfResult<PdfDict> parse_dict_literal(std::string_view text)
{
	const std::span<const uint8_t> bytes(
		reinterpret_cast<const uint8_t*>(text.data()),
		text.size());
	Parser parser(bytes);
	return parser.parse_dictionary();
}

TEST(PdfCrypto, ParseEncryptDict)
{
	// 1. Valid R=2 (V=1, 40-bit RC4)
	{
		auto d = parse_dict_literal(
			"<< /Filter /Standard /V 1 /R 2 /P -4 "
			"/O <000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F> "
			"/U <202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F> >>");
		RECTO_ASSERT_OK(d);
		auto p = SecurityHandler::parse_encrypt_dict(*d);
		RECTO_ASSERT_OK(p);
		EXPECT_EQ(p->v, 1);
		EXPECT_EQ(p->r, 2);
		EXPECT_EQ(p->key_length_bits, 40);
		EXPECT_EQ(p->stream_method, CryptFilterMethod::V2);
		EXPECT_EQ(p->string_method, CryptFilterMethod::V2);
	}

	// 2. Valid R=3 (V=2, 128-bit RC4)
	{
		auto d = parse_dict_literal(
			"<< /Filter /Standard /V 2 /R 3 /Length 128 /P -1028 "
			"/O <9AD1299EE371CAE0F199684F14618F0170B8264568238304623BCED84A73E30B> "
			"/U <5C7015ABEBDB4BDAB3EBAA83386CACE600000000000000000000000000000000> >>");
		RECTO_ASSERT_OK(d);
		auto p = SecurityHandler::parse_encrypt_dict(*d);
		RECTO_ASSERT_OK(p);
		EXPECT_EQ(p->v, 2);
		EXPECT_EQ(p->r, 3);
		EXPECT_EQ(p->key_length_bits, 128);
		EXPECT_EQ(p->stream_method, CryptFilterMethod::V2);
		EXPECT_EQ(p->string_method, CryptFilterMethod::V2);
	}

	// 3. Valid R=4 (V=4, AESV2, EncryptMetadata false)
	{
		auto d = parse_dict_literal(
			"<< /Filter /Standard /V 4 /R 4 /Length 128 /P -1028 /EncryptMetadata false "
			"/CF << /StdCF << /Type /CryptFilter /CFM /AESV2 /Length 16 >> >> "
			"/StmF /StdCF /StrF /StdCF "
			"/O <9AD1299EE371CAE0F199684F14618F0170B8264568238304623BCED84A73E30B> "
			"/U <C3B2EFDE1910AD979F7F329CF6BC79D500000000000000000000000000000000> >>");
		RECTO_ASSERT_OK(d);
		auto p = SecurityHandler::parse_encrypt_dict(*d);
		RECTO_ASSERT_OK(p);
		EXPECT_EQ(p->v, 4);
		EXPECT_EQ(p->r, 4);
		EXPECT_EQ(p->key_length_bits, 128);
		EXPECT_FALSE(p->encrypt_metadata);
		EXPECT_EQ(p->stream_method, CryptFilterMethod::AESV2);
		EXPECT_EQ(p->string_method, CryptFilterMethod::AESV2);
	}

	// 4. Valid R=5 and R=6 (V=5, AESV3, 256-bit)
	for (int rev : {5, 6}) {
		auto d = parse_dict_literal(
			std::string("<< /Filter /Standard /V 5 /R ") + std::to_string(rev) +
			" /Length 256 /P -1028 /EncryptMetadata true "
			"/CF << /StdCF << /Type /CryptFilter /CFM /AESV3 /Length 32 >> >> "
			"/StmF /StdCF /StrF /StdCF "
			"/O <9F27ADAD3EE6DEED50E53031C6339E3F407D75EBA094458D77E3FA7CEA18ACC56FB88920061F44E9FB9BE48EA0363B62> "
			"/U <CAF15DB36A35AD0DCF1B93EC0B7F412A45F931C3B1ED619FF145CF6329925BD8EE78E25AE26EE4665BCC0C2C296ED204> "
			"/OE <A924F66F552A94E1FCA4027A3B728B482AB2921A0CD2946633176C7FA8C69BCB> "
			"/UE <19846DD81E1BDA92F88CE22C0A00A93581D1BC8A27D55360DB0F965A510B879B> "
			"/Perms <F4D33D5AC9A00A9789FFD4835630A1CE> >>");
		RECTO_ASSERT_OK(d);
		auto p = SecurityHandler::parse_encrypt_dict(*d);
		RECTO_ASSERT_OK(p);
		EXPECT_EQ(p->v, 5);
		EXPECT_EQ(p->r, rev);
		EXPECT_EQ(p->key_length_bits, 256);
		EXPECT_EQ(p->stream_method, CryptFilterMethod::AESV3);
		EXPECT_EQ(p->string_method, CryptFilterMethod::AESV3);
	}

	// 5. Reject unknown /Filter or unsupported /V / /R combinations with UnsupportedEncryption
	{
		auto bad_filter = parse_dict_literal("<< /Filter /Adobe.PubSec /V 4 /R 4 >>");
		RECTO_ASSERT_OK(bad_filter);
		RECTO_EXPECT_ERR(
			SecurityHandler::parse_encrypt_dict(*bad_filter),
			ErrorCode::UnsupportedEncryption);

		auto bad_v = parse_dict_literal("<< /Filter /Standard /V 3 /R 3 /P -4 >>");
		RECTO_ASSERT_OK(bad_v);
		RECTO_EXPECT_ERR(
			SecurityHandler::parse_encrypt_dict(*bad_v),
			ErrorCode::UnsupportedEncryption);

		auto bad_r = parse_dict_literal("<< /Filter /Standard /V 5 /R 7 /P -4 >>");
		RECTO_ASSERT_OK(bad_r);
		RECTO_EXPECT_ERR(
			SecurityHandler::parse_encrypt_dict(*bad_r),
			ErrorCode::UnsupportedEncryption);

		auto mismatch_vr = parse_dict_literal("<< /Filter /Standard /V 2 /R 4 /P -4 >>");
		RECTO_ASSERT_OK(mismatch_vr);
		RECTO_EXPECT_ERR(
			SecurityHandler::parse_encrypt_dict(*mismatch_vr),
			ErrorCode::UnsupportedEncryption);
	}
}

TEST(PdfCrypto, CorpusRc4AndAes128AndAes256UnlockAndDecrypt)
{
	struct EncryptedFixture {
		const char* path;
		const char* mode_upper;
		int32_t expected_v;
		int32_t expected_r;
		int32_t expected_bits;
	};
	constexpr std::array<EncryptedFixture, 3> kFixtures = {{
		{"render/encrypted-rc4-128.pdf", "RC4-128", 2, 3, 128},
		{"render/encrypted-aes-128.pdf", "AES-128", 4, 4, 128},
		{"render/encrypted-aes-256.pdf", "AES-256", 5, 6, 256},
	}};

	for (const auto& fx : kFixtures) {
		SCOPED_TRACE(fx.path);
		const std::vector<uint8_t> file_bytes = read_corpus_bytes(fx.path);
		ASSERT_FALSE(file_bytes.empty());

		const std::string expected_title = std::string("Recto Encrypted ") + fx.mode_upper;
		const std::string expected_author = "Recto Corpus Generator";
		const std::string expected_date = "D:20261013000000Z";
		const std::string expected_stream_plaintext =
			std::string("q 0.94 0.96 0.99 rg 36 700 523 80 re f Q\n"
						"BT /F1 18 Tf 54 748 Td (Recto Encrypted PDF (") +
			fx.mode_upper +
			")) Tj /F1 12 Tf 0 -24 Td (Unlocked with user password recto-test or owner password recto-owner.) Tj ET\n";

		auto xref_res = XrefTable::parse(file_bytes);
		RECTO_ASSERT_OK(xref_res);

		auto sh_opt_res = SecurityHandler::from_xref(*xref_res, file_bytes);
		RECTO_ASSERT_OK(sh_opt_res);
		ASSERT_TRUE(sh_opt_res->has_value());
		SecurityHandler sh = std::move(**sh_opt_res);

		EXPECT_EQ(sh.params().v, fx.expected_v);
		EXPECT_EQ(sh.params().r, fx.expected_r);
		EXPECT_EQ(sh.params().key_length_bits, fx.expected_bits);

		// Empty password must NOT unlock these user-password-protected fixtures:
		EXPECT_FALSE(sh.is_authenticated());
		RECTO_EXPECT_ERR(
			sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u}),
			ErrorCode::NeedsPassword);

		// Wrong password must return WrongPassword:
		RECTO_EXPECT_ERR(sh.authenticate("wrong-pw"), ErrorCode::WrongPassword);
		EXPECT_FALSE(sh.is_authenticated());

		// Authenticate with user password "recto-test":
		RECTO_ASSERT_OK(sh.authenticate("recto-test"));
		EXPECT_TRUE(sh.is_authenticated());
		EXPECT_FALSE(sh.unlocked_as_owner());
		EXPECT_TRUE(sh.perms_verified());

		// Resolve and decrypt /Info dictionary (object 6 0 R) and assert exact plaintext strings:
		auto info_val = sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(info_val);
		const PdfDict* info_dict = info_val->as_dict();
		ASSERT_NE(info_dict, nullptr);
		const PdfString* title_str = info_dict->get_string("Title");
		const PdfString* author_str = info_dict->get_string("Author");
		const PdfString* date_str = info_dict->get_string("CreationDate");
		ASSERT_NE(title_str, nullptr);
		ASSERT_NE(author_str, nullptr);
		ASSERT_NE(date_str, nullptr);
		auto title_u8 = title_str->to_utf8();
		auto author_u8 = author_str->to_utf8();
		auto date_u8 = date_str->to_utf8();
		RECTO_ASSERT_OK(title_u8);
		RECTO_ASSERT_OK(author_u8);
		RECTO_ASSERT_OK(date_u8);
		EXPECT_EQ(*title_u8, expected_title);
		EXPECT_EQ(*author_u8, expected_author);
		EXPECT_EQ(*date_u8, expected_date);

		// Resolve and decrypt page content stream (object 4 0 R), decompress via FlateDecode,
		// and assert exact byte-for-byte plaintext equality:
		auto stm_val = sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(stm_val);
		const PdfStream* page_stm = stm_val->as_stream();
		ASSERT_NE(page_stm, nullptr);
		auto decoded_stream = decode_stream(*page_stm);
		RECTO_ASSERT_OK(decoded_stream);
		const std::string content_text(
			reinterpret_cast<const char*>(decoded_stream->data()),
			decoded_stream->size());
		EXPECT_EQ(content_text, expected_stream_plaintext);

		// Verify object caching inside SecurityHandler (Freeze check):
		EXPECT_EQ(sh.cached_object_count(), 2u);
		auto cached_stm = sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(cached_stm);
		EXPECT_EQ(sh.cached_object_count(), 2u);

		// Authenticate with owner password "recto-owner" on a fresh SecurityHandler
		// and assert exact plaintext equality against the independent expected strings:
		auto sh_owner_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
		RECTO_ASSERT_OK(sh_owner_opt);
		SecurityHandler sh_owner = std::move(**sh_owner_opt);
		RECTO_ASSERT_OK(sh_owner.authenticate("recto-owner"));
		EXPECT_TRUE(sh_owner.is_authenticated());
		EXPECT_TRUE(sh_owner.unlocked_as_owner());
		EXPECT_TRUE(sh_owner.perms_verified());
		EXPECT_TRUE(std::ranges::equal(sh_owner.file_encryption_key(), sh.file_encryption_key()));

		auto owner_info_val = sh_owner.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(owner_info_val);
		const PdfDict* owner_info_dict = owner_info_val->as_dict();
		ASSERT_NE(owner_info_dict, nullptr);
		EXPECT_EQ(*owner_info_dict->get_string("Title")->to_utf8(), expected_title);
		EXPECT_EQ(*owner_info_dict->get_string("Author")->to_utf8(), expected_author);
		EXPECT_EQ(*owner_info_dict->get_string("CreationDate")->to_utf8(), expected_date);

		auto owner_stm_val = sh_owner.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(owner_stm_val);
		auto owner_decoded = decode_stream(*owner_stm_val->as_stream());
		RECTO_ASSERT_OK(owner_decoded);
		const std::string owner_content_text(
			reinterpret_cast<const char*>(owner_decoded->data()),
			owner_decoded->size());
		EXPECT_EQ(owner_content_text, expected_stream_plaintext);
	}
}

TEST(PdfCrypto, EmptyUserPasswordAutoUnlock)
{
	// 1. Construct an owner-restricted R=3 (128-bit RC4) /Encrypt dictionary whose
	// user password is "" (empty) with /P -3904 (0xFFFFF0C0: all user permissions false).
	// SecurityHandler::create must auto-unlock on creation without returning NeedsPassword,
	// while reporting is_authenticated() == true and the active permission flags.
	auto d_r3 = parse_dict_literal(
		"<< /Filter /Standard /V 2 /R 3 /Length 128 /P -3904 "
		"/O <0000000000000000000000000000000000000000000000000000000000000000> "
		"/U <A0685F035CD33F87FA04C93FA5380B3300000000000000000000000000000000> >>");
	RECTO_ASSERT_OK(d_r3);
	const std::vector<uint8_t> zero_id(16u, 0u);
	auto sh_res = SecurityHandler::create(*d_r3, zero_id, IndirectRef{7u, 0u});
	RECTO_ASSERT_OK(sh_res);
	EXPECT_TRUE(sh_res->is_authenticated());
	EXPECT_FALSE(sh_res->unlocked_as_owner());
	EXPECT_EQ(sh_res->permissions().raw_p, -3904);
	EXPECT_FALSE(sh_res->permissions().can_print);
	EXPECT_FALSE(sh_res->permissions().can_modify);
	EXPECT_FALSE(sh_res->permissions().can_copy_text);
	EXPECT_FALSE(sh_res->permissions().can_annotate);
	EXPECT_FALSE(sh_res->permissions().can_fill_forms);
	EXPECT_FALSE(sh_res->permissions().can_extract_for_accessibility);
	EXPECT_FALSE(sh_res->permissions().can_assemble);
	EXPECT_FALSE(sh_res->permissions().can_print_high_quality);

	// 2. Construct an R=6 (256-bit AES) /Encrypt dictionary whose user password is "" (empty).
	// Verify auto-unlock on creation, verify that a password consisting solely of U+00AD
	// ("\xC2\xAD") normalizes to empty "" and authenticates, and verify that corrupting
	// /Perms causes SecurityHandler::create to propagate ErrorCode::DamagedFormat instead
	// of swallowing it.
	auto d_r6_empty = parse_dict_literal(
		"<< /Filter /Standard /V 5 /R 6 /Length 256 /P -1028 /EncryptMetadata true "
		"/CF << /StdCF << /Type /CryptFilter /CFM /AESV3 /Length 32 >> >> "
		"/StmF /StdCF /StrF /StdCF "
		"/O <000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000> "
		"/U <439FEBA099A63D0D035A1E5FB67FF307329189584956425AFF2D3BD3D15EDC6000000000000000000000000000000000> "
		"/OE <0000000000000000000000000000000000000000000000000000000000000000> "
		"/UE <BD530CC64DDCBCA19739E303D4135C3DBA43882D0FB08A15D3157DC344419531> "
		"/Perms <ECB382C363ACAB9013C3269B4146DEFE> >>");
	RECTO_ASSERT_OK(d_r6_empty);
	auto sh_r6 = SecurityHandler::create(*d_r6_empty, zero_id, IndirectRef{7u, 0u});
	RECTO_ASSERT_OK(sh_r6);
	EXPECT_TRUE(sh_r6->is_authenticated());
	EXPECT_TRUE(sh_r6->perms_verified());

	// Password consisting solely of U+00AD SOFT HYPHEN ("\xC2\xAD") normalizes to empty "":
	RECTO_ASSERT_OK(sh_r6->authenticate("\xC2\xAD"));
	EXPECT_TRUE(sh_r6->is_authenticated());

	// Corrupt /Perms on an empty-password R=6 /Encrypt dictionary: SecurityHandler::create
	// must propagate ErrorCode::DamagedFormat rather than swallowing it.
	auto d_r6_bad_perms = parse_dict_literal(
		"<< /Filter /Standard /V 5 /R 6 /Length 256 /P -1028 /EncryptMetadata true "
		"/CF << /StdCF << /Type /CryptFilter /CFM /AESV3 /Length 32 >> >> "
		"/StmF /StdCF /StrF /StdCF "
		"/O <000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000> "
		"/U <439FEBA099A63D0D035A1E5FB67FF307329189584956425AFF2D3BD3D15EDC6000000000000000000000000000000000> "
		"/OE <0000000000000000000000000000000000000000000000000000000000000000> "
		"/UE <BD530CC64DDCBCA19739E303D4135C3DBA43882D0FB08A15D3157DC344419531> "
		"/Perms <00000000000000000000000000000000> >>");
	RECTO_ASSERT_OK(d_r6_bad_perms);
	RECTO_EXPECT_ERR(
		SecurityHandler::create(*d_r6_bad_perms, zero_id, IndirectRef{7u, 0u}),
		ErrorCode::DamagedFormat);
}

// The audit of D01 T01 §9 (D06 T05 §23): a password that is both owner and user unlocks as the owner (ISO 32000-2
// Algorithm 2.A order) in R=3 and R=6; a non-ASCII R=3 password given in UTF-8 matches its PDFDocEncoding bytes; a
// /V 4 crypt filter's /Length sets the key length when the dictionary has none; and an indirect stream /Type gets the
// treatment its direct form gets. The /O, /U, /UE, /OE and /Perms values were computed independently with
// scripts/build-corpus.py's own RC4, MD5 and Algorithm 2.B code (ID 00..0F, /P -3904).
TEST(PdfCrypto, AuditOwnerFirstEncodingLengthAndIndirectType)
{
	std::array<uint8_t, 16> id0{};
	for (uint8_t i = 0; i < 16u; ++i) {
		id0[i] = i;
	}
	const auto handler = [&](const std::string& dict) {
		auto parsed = parse_dict_literal(dict);
		EXPECT_TRUE(parsed.has_value());
		return SecurityHandler::create(*parsed, id0, IndirectRef{7u, 0u});
	};

	auto r3_same = handler(
		"<< /Filter /Standard /V 2 /R 3 /Length 128 /P -3904 "
		"/O <72D80447DAB347EF4A5AE93CB806D4A069F2FE2A30C58BDF9E241180FEB9CD08> "
		"/U <0C189AF3F5A7DF8C9A5E1A08F95F047E00000000000000000000000000000000> >>");
	RECTO_ASSERT_OK(r3_same);
	RECTO_ASSERT_OK(r3_same->authenticate("same"));
	EXPECT_TRUE(r3_same->unlocked_as_owner());

	auto r6_same = handler(
		"<< /Filter /Standard /V 5 /R 6 /Length 256 /P -3904 /EncryptMetadata true "
		"/CF << /StdCF << /Type /CryptFilter /CFM /AESV3 /Length 32 >> >> /StmF /StdCF /StrF /StdCF "
		"/O <5E44B2FB30CC4458958D5391B5E4E7D0F1B37F860B141CD5F36815664D0628604F5653414C5430314F4B53414C543031> "
		"/U <07EEEFA132E578815744594BD3F3356C7800D536AF464742D2DAF7868D805EFA555653414C543031554B53414C543031> "
		"/OE <1B8BD7D5CB7F4DF2A92C8DD845284EEA3E494E60F984EBEFFC82E883F3561A56> "
		"/UE <95331891284D14DEEEFFC892E7FED399603ED493AED86243F6B4029756BE0D20> "
		"/Perms <42451D7FEE06C420C0A8A87584FA69D3> >>");
	RECTO_ASSERT_OK(r6_same);
	RECTO_ASSERT_OK(r6_same->authenticate("same"));
	EXPECT_TRUE(r6_same->unlocked_as_owner());

	auto r3_cafe = handler(
		"<< /Filter /Standard /V 2 /R 3 /Length 128 /P -3904 "
		"/O <8543795BA2B3B504CD72692001AEEFAA62DD176DDA46B9DA5A86A86BF9DC5377> "
		"/U <9BDDF19C2DD96291E2D35AA0F38B39EF00000000000000000000000000000000> >>");
	RECTO_ASSERT_OK(r3_cafe);
	RECTO_ASSERT_OK(r3_cafe->authenticate("caf\xC3\xA9")); // UTF-8 "café", stored as PDFDocEncoding 63 61 66 E9
	EXPECT_FALSE(r3_cafe->unlocked_as_owner());
	RECTO_ASSERT_OK(r3_cafe->authenticate("caf\xE9")); // the PDFDocEncoding bytes themselves still match
	RECTO_EXPECT_ERR(r3_cafe->authenticate("cafe"), ErrorCode::WrongPassword);

	// Round 2: a legacy password whose PDFDocEncoding bytes look like a UTF-16 BOM (þÿ, FE FF) still authenticates
	// from UTF-8: the conversion is plain PDFDocEncoding, not the text-string encoder that guards BOMs.
	auto r3_bom = handler(
		"<< /Filter /Standard /V 2 /R 3 /Length 128 /P -3904 "
		"/O <18DD370DC452B52F094687612BF85E069C2F384BF4DE6904B46EB9E77F2E9EDA> "
		"/U <AF6CF068305310E7C1CC603E7F9F034600000000000000000000000000000000> >>");
	RECTO_ASSERT_OK(r3_bom);
	RECTO_ASSERT_OK(r3_bom->authenticate("\xC3\xBE\xC3\xBF"));

	auto v4_dict = parse_dict_literal(
		"<< /Filter /Standard /V 4 /R 4 /P -3904 /CF << /StdCF << /CFM /V2 /Length 5 >> >> "
		"/StmF /StdCF /StrF /StdCF /O <0000000000000000000000000000000000000000000000000000000000000000> "
		"/U <0000000000000000000000000000000000000000000000000000000000000000> >>");
	RECTO_ASSERT_OK(v4_dict);
	auto v4 = SecurityHandler::parse_encrypt_dict(*v4_dict);
	RECTO_ASSERT_OK(v4);
	EXPECT_EQ(v4->key_length_bits, 40);

	// A stream whose /Type is a reference to /XRef is left as it is, as a direct /Type /XRef is.
	const std::vector<uint8_t> file_bytes = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto xref_res = XrefTable::parse(file_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto sh_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
	RECTO_ASSERT_OK(sh_opt);
	ASSERT_TRUE(sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	RECTO_ASSERT_OK(sh.authenticate("recto-test"));
	auto indirect_type = parse_dict_literal("<< /Type 20 0 R /Size 10 /W [1 2 1] >>");
	RECTO_ASSERT_OK(indirect_type);
	const std::vector<uint8_t> raw = {0x01u, 0x02u, 0x03u, 0x04u};
	PdfValue stream(PdfStream(std::move(*indirect_type), raw));
	const IndirectObjectResolver to_xref = [](IndirectRef) -> PdfResult<PdfValue> {
		return PdfValue(PdfName::from_decoded("XRef"));
	};
	RECTO_ASSERT_OK(sh.decrypt_object_values(stream, IndirectRef{11u, 0u}, false, &to_xref));
	EXPECT_EQ(stream.as_stream()->raw_bytes, raw);
}

TEST(PdfCrypto, SaslprepAndTruncatedAesCiphertext)
{
	// 1. Direct RFC 4013 SASLprep preparation tests (`SecurityHandler::prepare_saslprep_password`):
	// Positive cases:
	{
		auto empty_res = SecurityHandler::prepare_saslprep_password("");
		RECTO_ASSERT_OK(empty_res);
		EXPECT_TRUE(empty_res->empty());

		// U+00AD SOFT HYPHEN (Table B.1) maps to nothing -> empty password:
		auto shy_res = SecurityHandler::prepare_saslprep_password("\xC2\xAD");
		RECTO_ASSERT_OK(shy_res);
		EXPECT_TRUE(shy_res->empty());

		// Embedded Table B.1 characters (U+00AD, U+200B) map to nothing:
		auto mapped_res = SecurityHandler::prepare_saslprep_password("recto\xC2\xAD\xE2\x80\x8B-test");
		RECTO_ASSERT_OK(mapped_res);
		const std::string mapped_str(mapped_res->begin(), mapped_res->end());
		EXPECT_EQ(mapped_str, "recto-test");

		// Table C.1.2 non-ASCII space (U+00A0 NO-BREAK SPACE) maps to ASCII space (U+0020):
		auto nbsp_res = SecurityHandler::prepare_saslprep_password("a\xC2\xA0" "b");
		RECTO_ASSERT_OK(nbsp_res);
		const std::string nbsp_str(nbsp_res->begin(), nbsp_res->end());
		EXPECT_EQ(nbsp_str, "a b");

		// Supplementary-plane nonspacing mark U+1D167 (MUSICAL SYMBOL COMBINING TREMOLO-1,
		// not in RFC 3454 Table D.1 or D.2) between two RandALCat characters (U+05D0 HEBREW
		// LETTER ALEF) is valid under RFC 3454 Section 6 / RFC 4013 Section 2.4:
		const std::string valid_supp_bidi = "\xD7\x90\xF0\x9D\x85\xA7\xD7\x90";
		auto supp_res = SecurityHandler::prepare_saslprep_password(valid_supp_bidi);
		RECTO_ASSERT_OK(supp_res);
		const std::string supp_str(supp_res->begin(), supp_res->end());
		EXPECT_EQ(supp_str, valid_supp_bidi);
	}

	// Negative cases (direct preparation must reject with ErrorCode::WrongPassword):
	// - ASCII control character (U+0007, Table C.2.1):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password(std::string("recto\x07-test", 11)),
		ErrorCode::WrongPassword);
	// - Private Use character (U+E000 -> \xEE\x80\x80, Table C.3):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password("recto\xEE\x80\x80-test"),
		ErrorCode::WrongPassword);
	// - Replacement character (U+FFFD -> \xEF\xBF\xBD, Table C.6):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password("recto\xEF\xBF\xBD-test"),
		ErrorCode::WrongPassword);
	// - Bidi control character (U+202E RIGHT-TO-LEFT OVERRIDE -> \xE2\x80\xAE, Table C.8):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password("recto\xE2\x80\xAE-test"),
		ErrorCode::WrongPassword);
	// - Invalid UTF-8 byte sequence (\xFF\xFE):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password("\xFF\xFE"),
		ErrorCode::WrongPassword);
	// - Bidirectional Rule 2 violation (mixing RandALCat U+05D0 \xD7\x90 with LCat 'a'):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password("\xD7\x90" "a" "\xD7\x90"),
		ErrorCode::WrongPassword);
	// - Bidirectional Rule 2 violation with supplementary LCat (U+1D100 MUSICAL SYMBOL SINGLE BARLINE):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password("\xD7\x90\xF0\x9D\x84\x80\xD7\x90"),
		ErrorCode::WrongPassword);
	// - Bidirectional Rule 3 violation (RandALCat present, but first char is digit '1'):
	RECTO_EXPECT_ERR(
		SecurityHandler::prepare_saslprep_password("1\xD7\x90"),
		ErrorCode::WrongPassword);

	// 2. End-to-end R=5 authentication against an /Encrypt dictionary whose /U, /UE, and /Perms
	// were derived from the raw unvalidated string "1\xD7\x90" (bidi Rule 3 violation).
	// If SASLprep validation were bypassed, authenticate("1\xD7\x90") would match /U and return Ok.
	{
		auto d_bidi_r3 = parse_dict_literal(
			"<< /Filter /Standard /V 5 /R 5 /Length 256 /P -1028 /EncryptMetadata true "
			"/CF << /StdCF << /Type /CryptFilter /CFM /AESV3 /Length 32 >> >> "
			"/StmF /StdCF /StrF /StdCF "
			"/O <000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000> "
			"/U <BCB65C24B48B71EB66CEBB0522FBD3266461367CA4CB172A219EF6835C82D1C701010101010101010202020202020202> "
			"/OE <0000000000000000000000000000000000000000000000000000000000000000> "
			"/UE <7F15841308F74C2397B0A30791B3D59CF6D36A362F319EE7E8DE913FFC67B379> "
			"/Perms <F5F99B4811A7CFAB72B2B9C7381ED13F> >>");
		RECTO_ASSERT_OK(d_bidi_r3);
		auto sh_bidi = SecurityHandler::create(*d_bidi_r3, {}, IndirectRef{7u, 0u});
		RECTO_ASSERT_OK(sh_bidi);
		EXPECT_FALSE(sh_bidi->is_authenticated());
		RECTO_EXPECT_ERR(sh_bidi->authenticate("1\xD7\x90"), ErrorCode::WrongPassword);
	}

	// Conversely, an R=5 /Encrypt dictionary whose /U, /UE, and /Perms were derived from the
	// valid supplementary-plane bidi string "\xD7\x90\xF0\x9D\x85\xA7\xD7\x90" (U+05D0 U+1D167 U+05D0)
	// must authenticate and unwrap the 32-byte key {0x00..0x1F}:
	{
		auto d_supp_mn = parse_dict_literal(
			"<< /Filter /Standard /V 5 /R 5 /Length 256 /P -1028 /EncryptMetadata true "
			"/CF << /StdCF << /Type /CryptFilter /CFM /AESV3 /Length 32 >> >> "
			"/StmF /StdCF /StrF /StdCF "
			"/O <000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000> "
			"/U <B5DC17B1912F2EB6FC0EBC629FF2118C8FE3EDB878FF806BC8F3C33391A4982201010101010101010202020202020202> "
			"/OE <0000000000000000000000000000000000000000000000000000000000000000> "
			"/UE <167B9B1DE03DD2F3185189D23209963F367FB9CBA5BDAE8B278FD12E927053C5> "
			"/Perms <F5F99B4811A7CFAB72B2B9C7381ED13F> >>");
		RECTO_ASSERT_OK(d_supp_mn);
		auto sh_supp = SecurityHandler::create(*d_supp_mn, {}, IndirectRef{7u, 0u});
		RECTO_ASSERT_OK(sh_supp);
		RECTO_ASSERT_OK(sh_supp->authenticate("\xD7\x90\xF0\x9D\x85\xA7\xD7\x90"));
		EXPECT_TRUE(sh_supp->is_authenticated());
		EXPECT_TRUE(sh_supp->perms_verified());
		ASSERT_EQ(sh_supp->file_encryption_key().size(), 32u);
		for (size_t i = 0; i < 32u; ++i) {
			EXPECT_EQ(sh_supp->file_encryption_key()[i], static_cast<uint8_t>(i));
		}
	}

	// 3. Truncated 16-byte IV-only and non-multiple-of-16 AES-256 ciphertexts must be rejected:
	const std::vector<uint8_t> file_bytes = read_corpus_bytes("render/encrypted-aes-256.pdf");
	auto xref_res = XrefTable::parse(file_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto sh_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
	RECTO_ASSERT_OK(sh_opt);
	ASSERT_TRUE(sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	RECTO_ASSERT_OK(sh.authenticate("recto\xC2\xAD\xE2\x80\x8B-test"));
	EXPECT_TRUE(sh.is_authenticated());

	const std::vector<uint8_t> iv_only_16(16u, 0x42u);
	RECTO_EXPECT_ERR(
		sh.decrypt_bytes(iv_only_16, IndirectRef{4u, 0u}, CryptFilterMethod::AESV3),
		ErrorCode::DamagedFormat);

	const std::vector<uint8_t> odd_len_33(33u, 0x42u);
	RECTO_EXPECT_ERR(
		sh.decrypt_bytes(odd_len_33, IndirectRef{4u, 0u}, CryptFilterMethod::AESV3),
		ErrorCode::DamagedFormat);
}

TEST(PdfCrypto, OutOfMemoryFaultInjection)
{
	// Verify SecurityHandler default constructor is NOT marked noexcept (since its
	// std::unordered_map members allocate sentinel nodes during default construction):
	static_assert(!std::is_nothrow_default_constructible_v<SecurityHandler>);
	static_assert(!noexcept(SecurityHandler()));
	EXPECT_FALSE(std::is_nothrow_default_constructible_v<SecurityHandler>);
	EXPECT_FALSE(noexcept(SecurityHandler()));

	// Direct SecurityHandler default construction must propagate std::bad_alloc
	// rather than calling std::terminate:
	{
		detail::ScopedCryptoAllocFaultInjector fault(0);
		EXPECT_THROW((void)SecurityHandler{}, std::bad_alloc);
	}

	const std::vector<uint8_t> file_bytes = read_corpus_bytes("render/encrypted-aes-256.pdf");
	auto xref_res = XrefTable::parse(file_bytes);
	RECTO_ASSERT_OK(xref_res);

	auto valid_sh_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
	RECTO_ASSERT_OK(valid_sh_opt);
	ASSERT_TRUE(valid_sh_opt->has_value());
	SecurityHandler valid_sh = std::move(**valid_sh_opt);
	RECTO_ASSERT_OK(valid_sh.authenticate("recto-test"));

	auto enc_obj = xref_res->read_indirect_object(file_bytes, IndirectRef{7u, 0u});
	RECTO_ASSERT_OK(enc_obj);
	const PdfDict* enc_dict = enc_obj->value.as_dict();
	ASSERT_NE(enc_dict, nullptr);

	{
		detail::ScopedCryptoAllocFaultInjector fault(0);
		RECTO_EXPECT_ERR(
			SecurityHandler::prepare_saslprep_password("recto-test"),
			ErrorCode::OutOfMemory);
	}
	{
		detail::ScopedCryptoAllocFaultInjector fault(0);
		RECTO_EXPECT_ERR(
			SecurityHandler::parse_encrypt_dict(*enc_dict),
			ErrorCode::OutOfMemory);
	}
	// Sweep all allocation checkpoints inside SecurityHandler::create()
	// (0: create entry, 1: parse_encrypt_dict, 2: SecurityHandler() default constructor,
	//  3: auto-unlock authenticate(""), 4: prepare_saslprep_password("")):
	for (int step = 0; step <= 4; ++step) {
		detail::ScopedCryptoAllocFaultInjector fault(step);
		RECTO_EXPECT_ERR(
			SecurityHandler::create(*enc_dict, {}, IndirectRef{7u, 0u}),
			ErrorCode::OutOfMemory);
	}
	for (int step = 0; step <= 5; ++step) {
		detail::ScopedCryptoAllocFaultInjector fault(step);
		RECTO_EXPECT_ERR(
			SecurityHandler::from_xref(*xref_res, file_bytes),
			ErrorCode::OutOfMemory);
	}
	for (int step = 0; step <= 1; ++step) {
		detail::ScopedCryptoAllocFaultInjector fault(step);
		RECTO_EXPECT_ERR(
			valid_sh.authenticate("recto-test"),
			ErrorCode::OutOfMemory);
	}
	{
		const std::vector<uint8_t> dummy_ct(32u, 0u);
		detail::ScopedCryptoAllocFaultInjector fault(0);
		RECTO_EXPECT_ERR(
			valid_sh.decrypt_bytes(dummy_ct, IndirectRef{4u, 0u}, CryptFilterMethod::AESV3),
			ErrorCode::OutOfMemory);
	}
	{
		PdfValue str_val(PdfString("ciphertext"));
		detail::ScopedCryptoAllocFaultInjector fault(0);
		RECTO_EXPECT_ERR(
			valid_sh.decrypt_object_values(str_val, IndirectRef{4u, 0u}, false),
			ErrorCode::OutOfMemory);
	}
	{
		detail::ScopedCryptoAllocFaultInjector fault(0);
		RECTO_EXPECT_ERR(
			valid_sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u}),
			ErrorCode::OutOfMemory);
	}
}

// `/A` and `/#2FA` are distinct keys (the second decodes to "/A"); decryption must reach each entry once.
TEST(PdfCrypto, DecryptsKeysThatDifferOnlyByALeadingSlash)
{
	const std::vector<uint8_t> file_bytes = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto xref_res = XrefTable::parse(file_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto sh_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
	RECTO_ASSERT_OK(sh_opt);
	ASSERT_TRUE(sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	RECTO_ASSERT_OK(sh.authenticate("recto-test"));

	const IndirectRef ref{6u, 0u};
	const std::vector<uint8_t> cipher = {0xC0u, 0x4Bu, 0x99u, 0x3Au};
	auto plain = sh.decrypt_bytes(cipher, ref, CryptFilterMethod::V2);
	RECTO_ASSERT_OK(plain);
	ASSERT_NE(*plain, cipher);

	const auto expect_both_decrypted = [&](const PdfDict& dict) {
		size_t seen = 0;
		for (const auto& [name, value] : dict.entries()) {
			if (name.view() == "A" || name.view() == "/A") {
				const PdfString* s = value.as_string();
				ASSERT_NE(s, nullptr) << name.view();
				EXPECT_EQ(s->bytes, *plain) << name.view();
				++seen;
			}
		}
		EXPECT_EQ(seen, 2u);
	};

	auto dict_res = parse_dict_literal("<< /A <C04B993A> /#2FA <C04B993A> >>");
	RECTO_ASSERT_OK(dict_res);
	PdfValue dict_val(std::move(*dict_res));
	RECTO_ASSERT_OK(sh.decrypt_object_values(dict_val, ref, false));
	ASSERT_NE(dict_val.as_dict(), nullptr);
	expect_both_decrypted(*dict_val.as_dict());

	auto stm_dict_res = parse_dict_literal("<< /A <C04B993A> /#2FA <C04B993A> /Length 0 >>");
	RECTO_ASSERT_OK(stm_dict_res);
	PdfValue stm_val(PdfStream(std::move(*stm_dict_res), std::vector<uint8_t>{}));
	RECTO_ASSERT_OK(sh.decrypt_object_values(stm_val, ref, false));
	ASSERT_NE(stm_val.as_stream(), nullptr);
	expect_both_decrypted(stm_val.as_stream()->dict);
}

TEST(PdfCrypto, Exemptions)
{
	// Load encrypted-rc4-128.pdf and authenticate with "recto-test":
	const std::vector<uint8_t> file_bytes = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto xref_res = XrefTable::parse(file_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto sh_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
	RECTO_ASSERT_OK(sh_opt);
	ASSERT_TRUE(sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	RECTO_ASSERT_OK(sh.authenticate("recto-test"));

	// 1. /Encrypt dictionary itself (7 0 R) is never decrypted:
	auto enc_obj = xref_res->read_indirect_object(file_bytes, IndirectRef{7u, 0u});
	RECTO_ASSERT_OK(enc_obj);
	const PdfValue original_enc = *enc_obj->value.deep_clone();
	RECTO_ASSERT_OK(sh.decrypt_object_values(enc_obj->value, IndirectRef{7u, 0u}, false));
	EXPECT_EQ(enc_obj->value, original_enc);

	// 2. /Type /Sig dictionary preserves `/Contents` hex bytes untouched while decrypting `/Reason`:
	auto sig_dict_res = parse_dict_literal(
		"<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached "
		"/ByteRange [0 100 200 300] /Contents <DEADBEEF01020304> /Reason <C04B993A> >>");
	RECTO_ASSERT_OK(sig_dict_res);
	PdfValue sig_val(std::move(*sig_dict_res));
	RECTO_ASSERT_OK(sh.decrypt_object_values(sig_val, IndirectRef{6u, 0u}, false));
	const PdfDict* dec_sig = sig_val.as_dict();
	ASSERT_NE(dec_sig, nullptr);
	const PdfString* sig_contents = dec_sig->get_string("Contents");
	const PdfString* sig_reason = dec_sig->get_string("Reason");
	ASSERT_NE(sig_contents, nullptr);
	ASSERT_NE(sig_reason, nullptr);
	const std::vector<uint8_t> expected_contents = {0xDEu, 0xADu, 0xBEu, 0xEFu, 0x01u, 0x02u, 0x03u, 0x04u};
	EXPECT_EQ(sig_contents->bytes, expected_contents);
	const std::vector<uint8_t> orig_reason = {0xC0u, 0x4Bu, 0x99u, 0x3Au};
	EXPECT_NE(sig_reason->bytes, orig_reason);

	// 3. Objects inside an already-decrypted `/Type /ObjStm` (`is_inside_obj_stm = true`) are NOT double-decrypted:
	auto inner_dict_res = parse_dict_literal("<< /Title (Plaintext inside ObjStm) >>");
	RECTO_ASSERT_OK(inner_dict_res);
	PdfValue inner_val(std::move(*inner_dict_res));
	const PdfValue expected_inner = *inner_val.deep_clone();
	RECTO_ASSERT_OK(sh.decrypt_object_values(inner_val, IndirectRef{10u, 0u}, /*is_inside_obj_stm=*/true));
	EXPECT_EQ(inner_val, expected_inner);

	// 4. `/Type /XRef` stream is never decrypted:
	auto xref_stm_dict = parse_dict_literal("<< /Type /XRef /Size 10 /W [1 2 1] >>");
	RECTO_ASSERT_OK(xref_stm_dict);
	const std::vector<uint8_t> raw_xref_bytes = {0x01u, 0x02u, 0x03u, 0x04u};
	PdfValue xref_stm_val(PdfStream(std::move(*xref_stm_dict), raw_xref_bytes));
	RECTO_ASSERT_OK(sh.decrypt_object_values(xref_stm_val, IndirectRef{11u, 0u}, false));
	EXPECT_EQ(xref_stm_val.as_stream()->raw_bytes, raw_xref_bytes);

	// 5. `/Type /ObjStm` container and its compressed child share `decrypted_cache_` in both
	// resolution orders so the container is decrypted once per object:
	const std::string_view obj_stm_plain = "2 0 << /Title (Compressed Child) >>";
	const std::span<const uint8_t> obj_stm_plain_span(
		reinterpret_cast<const uint8_t*>(obj_stm_plain.data()),
		obj_stm_plain.size());
	// Since RC4 (`CryptFilterMethod::V2`) is symmetric, `decrypt_bytes` encrypts `obj_stm_plain` for `1 0 R`:
	auto obj_stm_cipher = sh.decrypt_bytes(obj_stm_plain_span, IndirectRef{1u, 0u}, CryptFilterMethod::V2);
	RECTO_ASSERT_OK(obj_stm_cipher);

	std::vector<uint8_t> synth_pdf;
	auto append_ascii = [&](std::string_view sv) {
		synth_pdf.insert(
			synth_pdf.end(),
			reinterpret_cast<const uint8_t*>(sv.data()),
			reinterpret_cast<const uint8_t*>(sv.data()) + sv.size());
	};
	append_ascii("%PDF-1.5\n");
	const uint16_t off_obj1 = static_cast<uint16_t>(synth_pdf.size());
	append_ascii("1 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Length 35 >>\nstream\n");
	synth_pdf.insert(synth_pdf.end(), obj_stm_cipher->begin(), obj_stm_cipher->end());
	append_ascii("\nendstream\nendobj\n");
	const uint16_t off_obj3 = static_cast<uint16_t>(synth_pdf.size());
	append_ascii("3 0 obj\n<< /Type /XRef /Size 4 /Root 2 0 R /W [1 2 1] /Index [0 4] /Length 16 >>\nstream\n");
	const std::array<uint8_t, 16> xref_entries = {
		0x00u, 0x00u, 0x00u, 0xFFu, // obj 0: free
		0x01u, static_cast<uint8_t>(off_obj1 >> 8u), static_cast<uint8_t>(off_obj1 & 0xFFu), 0x00u, // obj 1: uncompressed ObjStm
		0x02u, 0x00u, 0x01u, 0x00u, // obj 2: compressed in ObjStm 1, index 0
		0x01u, static_cast<uint8_t>(off_obj3 >> 8u), static_cast<uint8_t>(off_obj3 & 0xFFu), 0x00u, // obj 3: XRef stream
	};
	synth_pdf.insert(synth_pdf.end(), xref_entries.begin(), xref_entries.end());
	append_ascii("\nendstream\nendobj\nstartxref\n");
	append_ascii(std::to_string(off_obj3));
	append_ascii("\n%%EOF\n");

	auto synth_xref = XrefTable::parse(synth_pdf);
	RECTO_ASSERT_OK(synth_xref);

	// Order A: resolve compressed child `2 0 R` first, then `/ObjStm` container `1 0 R` with empty `file_bytes`:
	sh.clear_decrypted_cache();
	EXPECT_EQ(sh.cached_object_count(), 0u);
	auto child_a = sh.resolve_and_decrypt_object(*synth_xref, synth_pdf, IndirectRef{2u, 0u});
	RECTO_ASSERT_OK(child_a);
	ASSERT_NE(child_a->as_dict(), nullptr);
	ASSERT_NE(child_a->as_dict()->get_string("Title"), nullptr);
	EXPECT_EQ(*child_a->as_dict()->get_string("Title")->to_utf8(), "Compressed Child");
	// Only the container is kept as a decrypted object; the member is re-read from the bounded decoded container,
	// since a member can expand far past its share of the file (D06 T05 §21).
	EXPECT_EQ(sh.cached_object_count(), 1u);

	auto stm_a = sh.resolve_and_decrypt_object(*synth_xref, {}, IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(stm_a);
	ASSERT_NE(stm_a->as_stream(), nullptr);
	EXPECT_EQ(stm_a->as_stream()->raw_bytes, std::vector<uint8_t>(obj_stm_plain_span.begin(), obj_stm_plain_span.end()));

	// Order B: resolve `/ObjStm` container `1 0 R` first, then compressed child `2 0 R` with empty `file_bytes`:
	sh.clear_decrypted_cache();
	EXPECT_EQ(sh.cached_object_count(), 0u);
	auto stm_b = sh.resolve_and_decrypt_object(*synth_xref, synth_pdf, IndirectRef{1u, 0u});
	RECTO_ASSERT_OK(stm_b);
	EXPECT_EQ(sh.cached_object_count(), 1u);

	auto child_b = sh.resolve_and_decrypt_object(*synth_xref, {}, IndirectRef{2u, 0u});
	RECTO_ASSERT_OK(child_b);
	ASSERT_NE(child_b->as_dict(), nullptr);
	ASSERT_NE(child_b->as_dict()->get_string("Title"), nullptr);
	EXPECT_EQ(*child_b->as_dict()->get_string("Title")->to_utf8(), "Compressed Child");
	EXPECT_EQ(sh.cached_object_count(), 1u); // the member is not kept (see order A)
	// Read again with no file bytes at all: it comes from the decoded container still cached.
	auto child_again = sh.resolve_and_decrypt_object(*synth_xref, {}, IndirectRef{2u, 0u});
	RECTO_ASSERT_OK(child_again);
	EXPECT_EQ(*child_again->as_dict()->get_string("Title")->to_utf8(), "Compressed Child");
	EXPECT_EQ(sh.cached_object_count(), 1u);
}

// An encrypted object stream's /DecodeParms may be an indirect reference: SecurityHandler resolves (and decrypts) it
// through the same path, with the container already visited.
TEST(PdfCrypto, EncryptedObjStmIndirectDecodeParmsResolve)
{
	const std::vector<uint8_t> file_bytes = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto xref_res = XrefTable::parse(file_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto sh_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
	RECTO_ASSERT_OK(sh_opt);
	ASSERT_TRUE(sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	RECTO_ASSERT_OK(sh.authenticate("recto-test"));

	const std::string_view plain = "322030203432>"; // ASCIIHex of "2 0 42": object 2 is the integer 42
	const std::span<const uint8_t> plain_span(reinterpret_cast<const uint8_t*>(plain.data()), plain.size());
	auto cipher = sh.decrypt_bytes(plain_span, IndirectRef{1u, 0u}, CryptFilterMethod::V2); // RC4 is symmetric
	RECTO_ASSERT_OK(cipher);

	std::vector<uint8_t> pdf;
	const auto append = [&](std::string_view sv) { pdf.insert(pdf.end(), sv.begin(), sv.end()); };
	append("%PDF-1.5\n");
	const uint16_t off1 = static_cast<uint16_t>(pdf.size());
	append("1 0 obj\n<< /Type /ObjStm /N 1 /First 4 /Filter /ASCIIHexDecode /DecodeParms 4 0 R /Length " +
		std::to_string(cipher->size()) + " >>\nstream\n");
	pdf.insert(pdf.end(), cipher->begin(), cipher->end());
	append("\nendstream\nendobj\n");
	const uint16_t off4 = static_cast<uint16_t>(pdf.size());
	append("4 0 obj\n<< >>\nendobj\n");
	const uint16_t off3 = static_cast<uint16_t>(pdf.size());
	append("3 0 obj\n<< /Type /XRef /Size 5 /Root 2 0 R /W [1 2 1] /Index [0 5] /Length 20 >>\nstream\n");
	const std::array<uint8_t, 20> rows = {
		0x00u, 0x00u, 0x00u, 0xFFu,
		0x01u, static_cast<uint8_t>(off1 >> 8u), static_cast<uint8_t>(off1 & 0xFFu), 0x00u,
		0x02u, 0x00u, 0x01u, 0x00u,
		0x01u, static_cast<uint8_t>(off3 >> 8u), static_cast<uint8_t>(off3 & 0xFFu), 0x00u,
		0x01u, static_cast<uint8_t>(off4 >> 8u), static_cast<uint8_t>(off4 & 0xFFu), 0x00u,
	};
	pdf.insert(pdf.end(), rows.begin(), rows.end());
	append("\nendstream\nendobj\nstartxref\n" + std::to_string(off3) + "\n%%EOF\n");

	auto synth_xref = XrefTable::parse(pdf);
	RECTO_ASSERT_OK(synth_xref);
	sh.clear_decrypted_cache();
	auto child = sh.resolve_and_decrypt_object(*synth_xref, pdf, IndirectRef{2u, 0u});
	RECTO_ASSERT_OK(child);
	EXPECT_EQ(child->as_int(), std::optional<int64_t>(42));
}

// An indirect /Filter that starts with /Crypt chooses the stream's crypt filter exactly as a direct one: with
// /Name /Identity the bytes stay as stored, and /Crypt is stripped from the chain.
TEST(PdfCrypto, IndirectCryptFilterChoosesTheStreamCipher)
{
	const std::vector<uint8_t> file_bytes = read_corpus_bytes("render/encrypted-rc4-128.pdf");
	auto xref_res = XrefTable::parse(file_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto sh_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
	RECTO_ASSERT_OK(sh_opt);
	ASSERT_TRUE(sh_opt->has_value());
	SecurityHandler sh = std::move(**sh_opt);
	RECTO_ASSERT_OK(sh.authenticate("recto-test"));

	const std::vector<uint8_t> stored = {0x78u, 0x9Cu, 0x73u, 0x04u, 0x00u, 0x00u, 0x42u, 0x00u, 0x42u}; // Flate "A"
	const IndirectObjectResolver resolver = [](IndirectRef ref) -> PdfResult<PdfValue> {
		if (ref.obj_num == 10u) {
			return PdfValue(PdfArray{
				PdfValue(PdfName::from_decoded("Crypt")), PdfValue(PdfName::from_decoded("FlateDecode"))});
		}
		return PdfError{ErrorCode::DamagedFormat, "missing object"};
	};
	auto dict = parse_dict_literal("<< /Filter 10 0 R /DecodeParms [<< /Name /Identity >> null] >>");
	RECTO_ASSERT_OK(dict);
	PdfValue stream_val(PdfStream(std::move(*dict), stored));
	RECTO_ASSERT_OK(sh.decrypt_object_values(stream_val, IndirectRef{12u, 0u}, false, &resolver));
	const PdfStream* stm = stream_val.as_stream();
	ASSERT_NE(stm, nullptr);
	EXPECT_EQ(stm->raw_bytes, stored); // Identity: not run through the document's RC4 cipher
	const PdfArray* filters = stm->dict.get_array("Filter");
	ASSERT_NE(filters, nullptr);
	ASSERT_EQ(filters->size(), 1u);
	EXPECT_EQ((*filters)[0].as_name(), std::optional<std::string_view>("FlateDecode"));
	auto decoded = decode_stream(*stm);
	RECTO_ASSERT_OK(decoded);
	EXPECT_EQ(*decoded, std::vector<uint8_t>{'A'});
}

TEST(PdfCrypto, PermissionsBitmask)
{
	// ISO 32000-1 Table 22 (1-indexed bit positions: 3, 4, 5, 6, 9, 10, 11, 12):
	// All permissions denied (`0`):
	{
		const auto p0 = PdfPermissions::from_bitmask(0, 3);
		EXPECT_FALSE(p0.can_print);
		EXPECT_FALSE(p0.can_modify);
		EXPECT_FALSE(p0.can_copy_text);
		EXPECT_FALSE(p0.can_annotate);
		EXPECT_FALSE(p0.can_fill_forms);
		EXPECT_FALSE(p0.can_extract_for_accessibility);
		EXPECT_FALSE(p0.can_assemble);
		EXPECT_FALSE(p0.can_print_high_quality);
	}

	// Test each individual bit position (bits 3, 4, 5, 6, 9, 10, 11, 12):
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 2, 3).can_print);                      // bit 3
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 3, 3).can_modify);                     // bit 4
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 4, 3).can_copy_text);                  // bit 5
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 5, 3).can_annotate);                   // bit 6
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 8, 3).can_fill_forms);                 // bit 9
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 9, 3).can_extract_for_accessibility);  // bit 10
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 10, 3).can_assemble);                  // bit 11
	EXPECT_TRUE(PdfPermissions::from_bitmask(1 << 11, 3).can_print_high_quality);        // bit 12

	// Corpus `/P -1028` (`0xFFFFFBFC` = bits 3, 4, 5, 6, 9, 10 set; bit 11 (0x400) clear; bit 12 (0x800) set):
	{
		const auto pm = PdfPermissions::from_bitmask(-1028, 3);
		EXPECT_TRUE(pm.can_print);
		EXPECT_TRUE(pm.can_modify);
		EXPECT_TRUE(pm.can_copy_text);
		EXPECT_TRUE(pm.can_annotate);
		EXPECT_TRUE(pm.can_fill_forms);
		EXPECT_TRUE(pm.can_extract_for_accessibility);
		EXPECT_FALSE(pm.can_assemble);
		EXPECT_TRUE(pm.can_print_high_quality);
	}
}


// Encrypting new objects (D01 T02 section 36). The expected ciphertexts in `tests/corpus/crypto/encrypt-vectors.json`
// come from scripts/build-corpus.py's own RC4, MD5 and AES with each encrypted fixture's file key, object 12
// generation 3 and the IV 00..0F, so a key-derivation or padding mistake shared by encryption and decryption cannot
// pass on a round trip alone.
namespace {

std::vector<std::string> json_strings(const std::string& json, std::string_view key)
{
	std::vector<std::string> out;
	const std::string marker = "\"" + std::string(key) + "\": \"";
	for (size_t at = json.find(marker); at != std::string::npos; at = json.find(marker, at + 1u)) {
		const size_t start = at + marker.size();
		out.push_back(json.substr(start, json.find('"', start) - start));
	}
	return out;
}

std::vector<uint8_t> from_hex(std::string_view hex)
{
	std::vector<uint8_t> out;
	for (size_t i = 0; i + 1u < hex.size(); i += 2u) {
		out.push_back(static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2u)), nullptr, 16)));
	}
	return out;
}

std::string to_hex(std::span<const uint8_t> bytes)
{
	static constexpr char kDigits[] = "0123456789abcdef";
	std::string out;
	for (const uint8_t b : bytes) {
		out += kDigits[b >> 4u];
		out += kDigits[b & 0xFu];
	}
	return out;
}

CryptFilterMethod method_named(std::string_view name)
{
	return name == "V2" ? CryptFilterMethod::V2 : name == "AESV2" ? CryptFilterMethod::AESV2 : CryptFilterMethod::AESV3;
}

struct OpenedFixture {
	std::vector<uint8_t> bytes;
	std::unique_ptr<Document> doc;
};

OpenedFixture open_encrypted(std::string_view rel_path)
{
	OpenedFixture f{read_corpus_bytes(rel_path), nullptr};
	auto doc = Document::open_memory(f.bytes, "recto-test");
	EXPECT_TRUE(doc.has_value()) << rel_path;
	if (doc.has_value()) {
		f.doc = std::move(*doc);
	}
	return f;
}

constexpr IndirectRef kVectorRef{12u, 3u};

} // namespace

TEST(PdfCrypto, EncryptBytes)
{
	const std::vector<uint8_t> raw = read_corpus_bytes("crypto/encrypt-vectors.json");
	const std::string json(raw.begin(), raw.end());
	const auto paths = json_strings(json, "path");
	const auto methods = json_strings(json, "method");
	const auto keys = json_strings(json, "file_key");
	const auto plains = json_strings(json, "plaintext");
	const auto ciphers = json_strings(json, "ciphertext");
	ASSERT_EQ(paths.size(), 3u);
	ASSERT_EQ(plains.size(), 15u);
	ASSERT_EQ(ciphers.size(), 15u);
	std::array<uint8_t, 16> iv{};
	for (uint8_t i = 0; i < 16u; ++i) {
		iv[i] = i;
	}
	for (size_t f = 0; f < paths.size(); ++f) {
		SCOPED_TRACE(paths[f]);
		const OpenedFixture fixture = open_encrypted(paths[f]);
		ASSERT_NE(fixture.doc, nullptr);
		const auto& handler = fixture.doc->security_handler();
		ASSERT_TRUE(handler.has_value());
		// The key the vectors were computed with is the key Recto derived from the password.
		ASSERT_EQ(to_hex(handler->file_encryption_key()), keys[f]);
		const CryptFilterMethod method = method_named(methods[f]);
		const bool aes = method != CryptFilterMethod::V2;
		for (size_t c = 0; c < 5u; ++c) {
			const std::vector<uint8_t> plain = from_hex(plains[f * 5u + c]);
			SCOPED_TRACE(plain.size());
			{
				detail::ScopedEncryptionIv fixed(iv);
				auto cipher = handler->encrypt_bytes(plain, kVectorRef, method);
				RECTO_ASSERT_OK(cipher);
				EXPECT_EQ(to_hex(*cipher), ciphers[f * 5u + c]);
				EXPECT_EQ(cipher->size(), aes ? 16u + (plain.size() / 16u + 1u) * 16u : plain.size());
			}
			// Without a fixed IV: decryption restores the bytes, AES differs on every call and RC4 does not.
			auto first = handler->encrypt_bytes(plain, kVectorRef, method);
			auto second = handler->encrypt_bytes(plain, kVectorRef, method);
			RECTO_ASSERT_OK(first);
			RECTO_ASSERT_OK(second);
			EXPECT_EQ(*first != *second, aes);
			auto back = handler->decrypt_bytes(*first, kVectorRef, method);
			RECTO_ASSERT_OK(back);
			EXPECT_EQ(*back, plain);
		}
		auto none = handler->encrypt_bytes(from_hex("616263"), kVectorRef, CryptFilterMethod::None);
		RECTO_ASSERT_OK(none);
		EXPECT_EQ(*none, from_hex("616263"));
	}
	// A handler that is not authenticated encrypts nothing.
	const std::vector<uint8_t> locked_bytes = read_corpus_bytes("render/encrypted-aes-256.pdf");
	auto locked = Document::open_memory_unauthenticated(locked_bytes);
	RECTO_ASSERT_OK(locked);
	ASSERT_TRUE((*locked)->security_handler().has_value());
	RECTO_EXPECT_ERR((*locked)->security_handler()->encrypt_bytes(from_hex("00"), kVectorRef, CryptFilterMethod::AESV3),
		ErrorCode::NeedsPassword);
}

TEST(PdfCrypto, EncryptObjects)
{
	// The AES-128 fixture's encryption dictionary with strings left clear (/StrF /Identity), streams in AES-128
	// (/StmF /StdCF) and embedded files in RC4 (/EFF /RC4CF): each payload must take its own filter.
	const OpenedFixture fixture = open_encrypted("render/encrypted-aes-128.pdf");
	ASSERT_NE(fixture.doc, nullptr);
	const PdfDict& trailer = fixture.doc->xref().trailer();
	const auto encrypt_ref = fixture.doc->xref().encrypt_ref();
	ASSERT_TRUE(encrypt_ref.has_value());
	auto encrypt_value = fixture.doc->resolve_value(PdfValue{*encrypt_ref});
	RECTO_ASSERT_OK(encrypt_value);
	PdfDict encrypt = *encrypt_value->as_dict();
	auto filters = parse_dict_literal(
		"<< /StdCF << /Type /CryptFilter /CFM /AESV2 /AuthEvent /DocOpen /Length 16 >> "
		"/RC4CF << /Type /CryptFilter /CFM /V2 /AuthEvent /DocOpen /Length 16 >> >>");
	RECTO_ASSERT_OK(filters);
	encrypt.set("CF", PdfValue{*filters});
	encrypt.set("StrF", PdfValue{PdfName("Identity")});
	encrypt.set("StmF", PdfValue{PdfName("StdCF")});
	encrypt.set("EFF", PdfValue{PdfName("RC4CF")});
	const PdfArray* ids = trailer.get_array("ID");
	ASSERT_NE(ids, nullptr);
	const PdfString* id0 = (*ids)[0].as_string();
	ASSERT_NE(id0, nullptr);
	auto made = SecurityHandler::create(encrypt, id0->bytes, *encrypt_ref);
	RECTO_ASSERT_OK(made);
	SecurityHandler& handler = *made;
	RECTO_ASSERT_OK(handler.authenticate("recto-test"));

	const auto stream_of = [](std::string_view dict_text, std::string_view payload) {
		auto dict = parse_dict_literal(dict_text);
		EXPECT_TRUE(dict.has_value());
		PdfStream s;
		s.dict = dict.has_value() ? *dict : PdfDict{};
		s.raw_bytes.assign(payload.begin(), payload.end());
		return PdfValue{std::move(s)};
	};
	// An indirect /DecodeParms naming the RC4 filter for a stream whose chain starts with /Crypt.
	const IndirectObjectResolver resolver = [&](IndirectRef r) -> PdfResult<PdfValue> {
		if (r == IndirectRef{40u, 0u}) {
			auto parms = parse_dict_literal("<< /Type /CryptFilterDecodeParms /Name /RC4CF >>");
			return PdfValue{PdfArray{PdfValue{*parms}, PdfValue{}}};
		}
		if (r == IndirectRef{41u, 0u}) {
			return PdfValue{PdfName("EmbeddedFile")};
		}
		return make_pdf_error(ErrorCode::DamagedFormat, "no such object");
	};
	PdfArray parts;
	parts.push_back(PdfValue{PdfString(std::string_view("clear string"))});
	parts.push_back(stream_of("<< /Length 9 >>", "content 1"));
	parts.push_back(stream_of("<< /Type 41 0 R >>", "an embedded file"));
	parts.push_back(stream_of("<< /Filter [/Crypt /FlateDecode] /DecodeParms 40 0 R >>", "crypt override"));
	parts.push_back(stream_of("<< /Filter /Crypt /DecodeParms << /Name /Identity >> >>", "identity payload"));
	parts.push_back(stream_of("<< /Type /XRef >>", "xref bytes"));
	parts.push_back(PdfValue{PdfDict{}});
	auto sig = parse_dict_literal("<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 10 20 30] /Contents <0102> /M (D:2026) >>");
	RECTO_ASSERT_OK(sig);
	parts.push_back(PdfValue{*sig});
	const PdfValue original{parts};
	PdfValue value = original;
	RECTO_ASSERT_OK(handler.encrypt_object_values(value, IndirectRef{30u, 0u}, &resolver));

	const PdfArray& out = *value.as_array();
	const auto raw_of = [&](size_t i) { return out[i].as_stream()->raw_bytes; };
	const auto bytes_of_text = [](std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); };
	EXPECT_EQ(out[0], original.as_array()->at(0)); // /StrF /Identity: strings stay clear
	EXPECT_EQ(raw_of(1).size(), 32u);               // /StmF AES-128: IV and one block
	auto rc4_file = handler.encrypt_bytes(bytes_of_text("an embedded file"), IndirectRef{30u, 0u}, CryptFilterMethod::V2);
	RECTO_ASSERT_OK(rc4_file);
	EXPECT_EQ(raw_of(2), *rc4_file); // /EFF RC4, through the indirect /Type
	auto rc4_override = handler.encrypt_bytes(bytes_of_text("crypt override"), IndirectRef{30u, 0u}, CryptFilterMethod::V2);
	RECTO_ASSERT_OK(rc4_override);
	EXPECT_EQ(raw_of(3), *rc4_override); // the stream's own /Crypt filter, its parameters indirect
	EXPECT_EQ(out[3].as_stream()->dict.get("DecodeParms")->as_ref(), (IndirectRef{40u, 0u})); // not written in
	EXPECT_EQ(raw_of(4), bytes_of_text("identity payload")); // the Identity filter
	EXPECT_EQ(raw_of(5), bytes_of_text("xref bytes"));       // cross-reference streams are never encrypted
	EXPECT_EQ(*out[7].as_dict()->get("Contents"), *sig->get("Contents")); // a signature's /Contents stays
	// With strings encrypted (the fixture's own handler, /StrF AES-128), a signature's other strings are encrypted
	// and its /Contents still is not.
	PdfValue signed_value{*sig};
	RECTO_ASSERT_OK(fixture.doc->security_handler()->encrypt_object_values(signed_value, IndirectRef{30u, 0u}));
	EXPECT_EQ(*signed_value.as_dict()->get("Contents"), *sig->get("Contents"));
	EXPECT_NE(*signed_value.as_dict()->get("M"), *sig->get("M"));

	// Decryption restores every payload; it removes the /Crypt filter it applied, as it always does.
	PdfValue back = value;
	RECTO_ASSERT_OK(handler.decrypt_object_values(back, IndirectRef{30u, 0u}, false, &resolver));
	for (const size_t i : {size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{5}}) {
		EXPECT_EQ(back.as_array()->at(i).as_stream()->raw_bytes, original.as_array()->at(i).as_stream()->raw_bytes) << i;
	}
	EXPECT_EQ(back.as_array()->at(7), original.as_array()->at(7));

	// The /Encrypt dictionary itself is left alone, and an unknown crypt filter is refused with the value unchanged.
	PdfValue encrypt_copy{encrypt};
	RECTO_ASSERT_OK(handler.encrypt_object_values(encrypt_copy, *encrypt_ref));
	EXPECT_EQ(encrypt_copy, PdfValue{encrypt});
	PdfValue unknown = stream_of("<< /Filter /Crypt /DecodeParms << /Name /Nowhere >> >>", "payload");
	const PdfValue unknown_before = unknown;
	RECTO_EXPECT_ERR(handler.encrypt_object_values(unknown, IndirectRef{30u, 0u}), ErrorCode::UnsupportedEncryption);
	EXPECT_EQ(unknown, unknown_before);
}

TEST(PdfCrypto, EncryptFailures)
{
	const OpenedFixture fixture = open_encrypted("render/encrypted-aes-256.pdf");
	ASSERT_NE(fixture.doc, nullptr);
	const SecurityHandler& handler = *fixture.doc->security_handler();
	PdfArray strings;
	for (int i = 0; i < 4; ++i) {
		strings.push_back(PdfValue{PdfString(std::string_view("secret"))});
	}
	const PdfValue original{strings};
	// A failure at the third string, after two are done: a typed error, nothing changed, and a retry succeeds.
	for (const auto kind : {detail::ScopedEncryptionFailure::Kind::Allocation, detail::ScopedEncryptionFailure::Kind::Randomness}) {
		PdfValue value = original;
		{
			detail::ScopedEncryptionFailure fail(kind, 3);
			const auto failed = handler.encrypt_object_values(value, IndirectRef{30u, 0u});
			ASSERT_FALSE(failed.has_value());
			EXPECT_EQ(failed.error().code,
				kind == detail::ScopedEncryptionFailure::Kind::Allocation ? ErrorCode::OutOfMemory : ErrorCode::InternalError);
		}
		EXPECT_EQ(value, original);
		RECTO_ASSERT_OK(handler.encrypt_object_values(value, IndirectRef{30u, 0u}));
		EXPECT_NE(value, original);
	}
	// Nesting past the limit is refused before anything is encrypted.
	PdfValue deep{PdfString(std::string_view("inner"))};
	for (int i = 0; i < 70; ++i) {
		deep = PdfValue{PdfArray{std::move(deep)}};
	}
	const PdfValue deep_before = deep;
	RECTO_EXPECT_ERR(handler.encrypt_object_values(deep, IndirectRef{30u, 0u}), ErrorCode::RecursionLimitExceeded);
	EXPECT_EQ(deep, deep_before);
}

TEST(PdfCrypto, EncryptFailClosed)
{
	const OpenedFixture fixture = open_encrypted("render/encrypted-aes-128.pdf");
	ASSERT_NE(fixture.doc, nullptr);
	const SecurityHandler& handler = *fixture.doc->security_handler(); // /StrF and /StmF AES-128
	const auto stream_of = [](std::string_view dict_text, std::string_view payload) {
		auto dict = parse_dict_literal(dict_text);
		EXPECT_TRUE(dict.has_value());
		PdfStream s;
		s.dict = dict.has_value() ? *dict : PdfDict{};
		s.raw_bytes.assign(payload.begin(), payload.end());
		return PdfValue{std::move(s)};
	};
	const IndirectObjectResolver resolver = [&](IndirectRef r) -> PdfResult<PdfValue> {
		if (r == IndirectRef{42u, 0u}) {
			return PdfValue{PdfArray{PdfValue{PdfName("Crypt")}, PdfValue{PdfName("FlateDecode")}}};
		}
		if (r == IndirectRef{45u, 0u}) {
			return PdfValue{PdfString(std::string_view("not parameters"))};
		}
		if (r == IndirectRef{46u, 0u}) {
			return PdfValue{PdfString(std::string_view("EmbeddedFile"))};
		}
		return make_pdf_error(ErrorCode::DamagedFormat, "a damaged object");
	};

	// Empty AES string and stream: the IV and one padding block each, never passed through as decryption passes
	// empty ciphertext; a /DocTimeStamp's /Contents stays; an indirect /Filter whose chain starts with /Crypt takes
	// the filter its direct /DecodeParms names (Identity here, so the payload stays as written).
	PdfArray parts;
	parts.push_back(PdfValue{PdfString(std::string_view(""))});
	parts.push_back(stream_of("<< >>", ""));
	auto stamp = parse_dict_literal("<< /Type /DocTimeStamp /Contents <0A0B> /Name (stamp) >>");
	RECTO_ASSERT_OK(stamp);
	parts.push_back(PdfValue{*stamp});
	parts.push_back(stream_of("<< /Filter 42 0 R /DecodeParms << /Name /Identity >> >>", "indirect filter"));
	PdfValue value{parts};
	RECTO_ASSERT_OK(handler.encrypt_object_values(value, IndirectRef{30u, 0u}, &resolver));
	const PdfArray& out = *value.as_array();
	EXPECT_EQ(out[0].as_string()->bytes.size(), 32u);
	EXPECT_EQ(out[1].as_stream()->raw_bytes.size(), 32u);
	EXPECT_EQ(*out[2].as_dict()->get("Contents"), *stamp->get("Contents"));
	EXPECT_NE(*out[2].as_dict()->get("Name"), *stamp->get("Name"));
	const std::string_view indirect_payload = "indirect filter";
	EXPECT_EQ(out[3].as_stream()->raw_bytes, std::vector<uint8_t>(indirect_payload.begin(), indirect_payload.end()));
	PdfValue back = value;
	RECTO_ASSERT_OK(handler.decrypt_object_values(back, IndirectRef{30u, 0u}, false, &resolver));
	EXPECT_TRUE(back.as_array()->at(0).as_string()->bytes.empty());
	EXPECT_TRUE(back.as_array()->at(1).as_stream()->raw_bytes.empty());

	// Fail closed: a /DecodeParms, /Filter or /Type that does not resolve is refused, never read as absent (which
	// would pick Identity or the default filter), and the value is unchanged.
	for (const std::string_view dict : {std::string_view("<< /Filter /Crypt /DecodeParms 43 0 R >>"),
			 std::string_view("<< /Filter 43 0 R >>"), std::string_view("<< /Type 43 0 R >>")}) {
		SCOPED_TRACE(std::string(dict));
		PdfValue damaged = stream_of(dict, "payload");
		const PdfValue before = damaged;
		RECTO_EXPECT_ERR(handler.encrypt_object_values(damaged, IndirectRef{30u, 0u}, &resolver), ErrorCode::DamagedFormat);
		EXPECT_EQ(damaged, before);
	}

	// Every parameter must be readable when encrypting: a reference with no resolver to read it, a resolved value of
	// the wrong kind and a /Name that is not a name are refused, with the value unchanged.
	const auto refuses = [&](std::string_view dict, const IndirectObjectResolver* with) {
		SCOPED_TRACE(std::string(dict));
		PdfValue damaged = stream_of(dict, "payload");
		const PdfValue before = damaged;
		RECTO_EXPECT_ERR(handler.encrypt_object_values(damaged, IndirectRef{30u, 0u}, with), ErrorCode::DamagedFormat);
		EXPECT_EQ(damaged, before);
	};
	refuses("<< /Filter /Crypt /DecodeParms 43 0 R >>", nullptr);
	refuses("<< /Type 44 0 R >>", nullptr);
	refuses("<< /Filter /Crypt /DecodeParms 45 0 R >>", &resolver);
	refuses("<< /Filter /Crypt /DecodeParms << /Name (StdCF) >> >>", nullptr);
	refuses("<< /Filter (Crypt) >>", nullptr);
	refuses("<< /Filter /Crypt /DecodeParms [<< /Name /StdCF >>] >>", nullptr); // a single filter's parameters are a dictionary
	refuses("<< /Filter [/FlateDecode /Crypt] >>", nullptr);                    // /Crypt comes first in a chain (7.4.10)
	refuses("<< /Type 46 0 R >>", &resolver);                                   // a /Type resolving to a string
	refuses("<< /Type (EmbeddedFile) >>", nullptr);                             // a direct /Type that is not a name

	// The signature exemption is for signatures: an annotation holding /ByteRange, /Contents and /Filter, whose /Type
	// says what it is, has its /Contents encrypted like any text.
	auto annot = parse_dict_literal("<< /Type /Annot /Subtype /Text /Contents (secret) /ByteRange [] /Filter /Adobe.PPKLite >>");
	RECTO_ASSERT_OK(annot);
	PdfValue annot_value{*annot};
	RECTO_ASSERT_OK(handler.encrypt_object_values(annot_value, IndirectRef{30u, 0u}));
	EXPECT_NE(*annot_value.as_dict()->get("Contents"), *annot->get("Contents"));
	PdfValue annot_back = annot_value;
	RECTO_ASSERT_OK(handler.decrypt_object_values(annot_back, IndirectRef{30u, 0u}));
	EXPECT_EQ(*annot_back.as_dict()->get("Contents"), *annot->get("Contents")); // decryption agrees
	// Without a /Type (optional for annotations, ISO 32000-1 Table 164) an annotation is still not a signature, while
	// an untyped dictionary in a signature's own shape still is.
	auto untyped_annot = parse_dict_literal(
		"<< /Subtype /Text /Rect [0 0 10 10] /Contents (secret) /ByteRange [0 10 20 30] /Filter /Adobe.PPKLite >>");
	auto untyped_sig = parse_dict_literal("<< /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 10 20 30] /Contents <0102> >>");
	RECTO_ASSERT_OK(untyped_annot);
	RECTO_ASSERT_OK(untyped_sig);
	PdfValue untyped_annot_value{*untyped_annot};
	PdfValue untyped_sig_value{*untyped_sig};
	RECTO_ASSERT_OK(handler.encrypt_object_values(untyped_annot_value, IndirectRef{30u, 0u}));
	RECTO_ASSERT_OK(handler.encrypt_object_values(untyped_sig_value, IndirectRef{30u, 0u}));
	EXPECT_NE(*untyped_annot_value.as_dict()->get("Contents"), *untyped_annot->get("Contents"));
	EXPECT_EQ(*untyped_sig_value.as_dict()->get("Contents"), *untyped_sig->get("Contents"));

	// /EncryptMetadata false (an R=6 dictionary from scripts/build-corpus.py with the /Perms that says so): a
	// /Metadata stream stays readable, any other stream is encrypted.
	const std::vector<uint8_t> raw = read_corpus_bytes("crypto/encrypt-vectors.json");
	const auto dicts = json_strings(std::string(raw.begin(), raw.end()), "metadata_false_dict");
	ASSERT_EQ(dicts.size(), 1u);
	auto parsed = parse_dict_literal(dicts[0]);
	RECTO_ASSERT_OK(parsed);
	auto made = SecurityHandler::create(*parsed, {}, IndirectRef{9u, 0u});
	RECTO_ASSERT_OK(made);
	RECTO_ASSERT_OK(made->authenticate("recto-test"));
	ASSERT_FALSE(made->params().encrypt_metadata);
	PdfArray pair{stream_of("<< /Type /Metadata /Subtype /XML >>", "<x:xmpmeta/>"), stream_of("<< >>", "page content")};
	PdfValue streams{pair};
	RECTO_ASSERT_OK(made->encrypt_object_values(streams, IndirectRef{31u, 0u}));
	const std::string_view xmp = "<x:xmpmeta/>";
	EXPECT_EQ(streams.as_array()->at(0).as_stream()->raw_bytes, std::vector<uint8_t>(xmp.begin(), xmp.end()));
	EXPECT_EQ(streams.as_array()->at(1).as_stream()->raw_bytes.size(), 32u);
}

TEST(PdfCrypto, EncryptMemory)
{
#if defined(__SANITIZE_ADDRESS__)
	GTEST_SKIP() << "the heap meter does not replace the allocator under ASan";
#else
	// A 64 MiB AES payload costs its ciphertext and no padded copy: the peak live heap of the call stays within the
	// output plus a little, where a padded copy beside it would double it.
	const OpenedFixture fixture = open_encrypted("render/encrypted-aes-256.pdf");
	ASSERT_NE(fixture.doc, nullptr);
	const SecurityHandler& handler = *fixture.doc->security_handler();
	const std::vector<uint8_t> payload(size_t{64} << 20u, uint8_t{0x5A});
	size_t peak = 0;
	PdfResult<std::vector<uint8_t>> cipher = std::vector<uint8_t>{};
	{
		recto::test::ScopedHeapMeter meter;
		cipher = handler.encrypt_bytes(payload, IndirectRef{30u, 0u}, CryptFilterMethod::AESV3);
		peak = meter.peak_bytes();
	}
	RECTO_ASSERT_OK(cipher);
	EXPECT_EQ(cipher->size(), payload.size() + 32u);
	EXPECT_LE(peak, cipher->size() + (size_t{1} << 20u)) << "peak " << peak << " for " << cipher->size() << " bytes out";
#endif
}

} // namespace
} // namespace recto::pdf
