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

#include "filters.h"
#include "parser.h"
#include "test_support.h"
#include "xref.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
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

TEST(PdfCrypto,CorpusRc4AndAes128AndAes256UnlockAndDecrypt)
{
	struct EncryptedFixture {
		const char* path;
		int32_t expected_v;
		int32_t expected_r;
		int32_t expected_bits;
	};
	constexpr std::array<EncryptedFixture, 3> kFixtures = {{
		{"render/encrypted-rc4-128.pdf", 2, 3, 128},
		{"render/encrypted-aes-128.pdf", 4, 4, 128},
		{"render/encrypted-aes-256.pdf", 5, 6, 256},
	}};

	for (const auto& fx : kFixtures) {
		SCOPED_TRACE(fx.path);
		const std::vector<uint8_t> file_bytes = read_corpus_bytes(fx.path);
		ASSERT_FALSE(file_bytes.empty());

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

		// Resolve and decrypt /Info dictionary (object 6 0 R):
		auto info_val = sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{6u, 0u});
		RECTO_ASSERT_OK(info_val);
		const PdfDict* info_dict = info_val->as_dict();
		ASSERT_NE(info_dict, nullptr);
		const PdfString* title_str = info_dict->get_string("Title");
		const PdfString* author_str = info_dict->get_string("Author");
		ASSERT_NE(title_str, nullptr);
		ASSERT_NE(author_str, nullptr);
		auto title_u8 = title_str->to_utf8();
		auto author_u8 = author_str->to_utf8();
		RECTO_ASSERT_OK(title_u8);
		RECTO_ASSERT_OK(author_u8);
		EXPECT_FALSE(title_u8->empty());
		EXPECT_FALSE(author_u8->empty());

		// Resolve and decrypt page content stream (object 4 0 R) and decompress via FlateDecode:
		auto stm_val = sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(stm_val);
		const PdfStream* page_stm = stm_val->as_stream();
		ASSERT_NE(page_stm, nullptr);
		auto decoded_stream = decode_stream(*page_stm);
		RECTO_ASSERT_OK(decoded_stream);
		const std::string content_text(
			reinterpret_cast<const char*>(decoded_stream->data()),
			decoded_stream->size());
		EXPECT_NE(content_text.find("BT"), std::string::npos);
		EXPECT_NE(content_text.find("ET"), std::string::npos);

		// Verify object caching inside SecurityHandler (Freeze check):
		EXPECT_EQ(sh.cached_object_count(), 2u);
		auto cached_stm = sh.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(cached_stm);
		EXPECT_EQ(sh.cached_object_count(), 2u);

		// Authenticate with owner password "recto-owner" on a fresh SecurityHandler:
		auto sh_owner_opt = SecurityHandler::from_xref(*xref_res, file_bytes);
		RECTO_ASSERT_OK(sh_owner_opt);
		SecurityHandler sh_owner = std::move(**sh_owner_opt);
		RECTO_ASSERT_OK(sh_owner.authenticate("recto-owner"));
		EXPECT_TRUE(sh_owner.is_authenticated());
		EXPECT_TRUE(sh_owner.unlocked_as_owner());
		EXPECT_TRUE(sh_owner.perms_verified());
		EXPECT_TRUE(std::ranges::equal(sh_owner.file_encryption_key(), sh.file_encryption_key()));

		auto owner_stm_val = sh_owner.resolve_and_decrypt_object(*xref_res, file_bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(owner_stm_val);
		auto owner_decoded = decode_stream(*owner_stm_val->as_stream());
		RECTO_ASSERT_OK(owner_decoded);
		EXPECT_EQ(*owner_decoded, *decoded_stream);
	}
}

TEST(PdfCrypto, EmptyUserPasswordAutoUnlock)
{
	// Construct an owner-restricted R=3 (128-bit RC4) /Encrypt dictionary whose
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
	// Wait: -1028 in 32-bit two's complement is `0xFFFFFBFC`:
	// `0xFBFC` = `1111 1011 1111 1100`:
	// - bit 3 (`0x004`): 1 (`can_print = true`)
	// - bit 4 (`0x008`): 1 (`can_modify = true`)
	// - bit 5 (`0x010`): 1 (`can_copy_text = true`)
	// - bit 6 (`0x020`): 1 (`can_annotate = true`)
	// - bit 9 (`0x100`): 1 (`can_fill_forms = true`)
	// - bit 10 (`0x200`): 1 (`can_extract_for_accessibility = true`)
	// - bit 11 (`0x400`): 0 (`can_assemble = false`)
	// - bit 12 (`0x800`): 1 (`can_print_high_quality = true`)
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

} // namespace
} // namespace recto::pdf
