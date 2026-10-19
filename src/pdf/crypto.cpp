// Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6`)
// via Windows Next Generation Cryptography (`bcrypt.dll`) for ISO 32000-1:2008
// section 7.6 and ISO 32000-2:2020 section 7.6.4 (D01 T01 section 9).

#include "crypto.h"

#include "filters.h"
#include "lexer.h"
#include "parser.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace recto::pdf {

namespace {

// ISO 32000-1 section 7.6.3.3 Algorithm 2: 32-byte standard password padding string.
inline constexpr std::array<uint8_t, 32> kPdfPasswordPadding = {
	0x28u, 0xBFu, 0x4Eu, 0x5Eu, 0x4Eu, 0x75u, 0x8Au, 0x41u,
	0x64u, 0x00u, 0x4Eu, 0x56u, 0xFFu, 0xFAu, 0x01u, 0x08u,
	0x2Eu, 0x2Eu, 0x00u, 0xB6u, 0xD0u, 0x68u, 0x3Eu, 0x80u,
	0x2Fu, 0x0Cu, 0xA9u, 0xFEu, 0x64u, 0x53u, 0x69u, 0x7Au,
};

struct BcryptAlgCloser {
	BCRYPT_ALG_HANDLE h{nullptr};
	~BcryptAlgCloser() noexcept
	{
		if (h != nullptr) {
			(void)BCryptCloseAlgorithmProvider(h, 0);
		}
	}
};

struct BcryptHashCloser {
	BCRYPT_HASH_HANDLE h{nullptr};
	~BcryptHashCloser() noexcept
	{
		if (h != nullptr) {
			(void)BCryptDestroyHash(h);
		}
	}
};

struct BcryptKeyCloser {
	BCRYPT_KEY_HANDLE h{nullptr};
	~BcryptKeyCloser() noexcept
	{
		if (h != nullptr) {
			(void)BCryptDestroyKey(h);
		}
	}
};

[[nodiscard]] PdfError make_crypto_error(
	ErrorCode code,
	DiagnosticId diag_id,
	std::string_view message,
	std::optional<uint32_t> obj_num = std::nullopt,
	uint16_t gen_num = 0) noexcept
{
	record_diagnostic(code, diag_id, std::nullopt, obj_num, gen_num);
	return PdfError(code, message, std::nullopt, obj_num, gen_num);
}

[[nodiscard]] constexpr uint64_t pack_obj_key(IndirectRef ref) noexcept
{
	return (static_cast<uint64_t>(ref.obj_num) << 16u) | static_cast<uint64_t>(ref.gen_num);
}

// Computes a cryptographic digest (`BCRYPT_MD5_ALGORITHM`, `BCRYPT_SHA256_ALGORITHM`,
// `BCRYPT_SHA384_ALGORITHM`, or `BCRYPT_SHA512_ALGORITHM`) into `out_digest`.
[[nodiscard]] bool cng_hash(
	LPCWSTR alg_id,
	std::span<const std::span<const uint8_t>> chunks,
	std::span<uint8_t> out_digest)
{
	BcryptAlgCloser alg{};
	if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, alg_id, nullptr, 0))) {
		return false;
	}
	DWORD obj_len = 0;
	DWORD cb_res = 0;
	if (!BCRYPT_SUCCESS(BCryptGetProperty(
			alg.h,
			BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&obj_len),
			sizeof(obj_len),
			&cb_res,
			0))) {
		return false;
	}
	std::array<uint8_t, 1024> stack_obj{};
	std::vector<uint8_t> heap_obj;
	PUCHAR obj_ptr = stack_obj.data();
	if (obj_len > stack_obj.size()) {
		heap_obj.resize(obj_len);
		obj_ptr = heap_obj.data();
	}
	BcryptHashCloser hash{};
	if (!BCRYPT_SUCCESS(BCryptCreateHash(
			alg.h,
			&hash.h,
			obj_ptr,
			obj_len,
			nullptr,
			0,
			0))) {
		return false;
	}
	for (const auto& chunk : chunks) {
		if (!chunk.empty()) {
			if (!BCRYPT_SUCCESS(BCryptHashData(
					hash.h,
					const_cast<PUCHAR>(chunk.data()),
					static_cast<ULONG>(chunk.size()),
					0))) {
				return false;
			}
		}
	}
	return BCRYPT_SUCCESS(BCryptFinishHash(
		hash.h,
		out_digest.data(),
		static_cast<ULONG>(out_digest.size()),
		0));
}

[[nodiscard]] bool cng_md5(
	std::initializer_list<std::span<const uint8_t>> chunks,
	std::array<uint8_t, 16>& out)
{
	return cng_hash(
		BCRYPT_MD5_ALGORITHM,
		std::span<const std::span<const uint8_t>>(chunks.begin(), chunks.size()),
		out);
}

[[nodiscard]] bool cng_sha256(
	std::initializer_list<std::span<const uint8_t>> chunks,
	std::array<uint8_t, 32>& out)
{
	return cng_hash(
		BCRYPT_SHA256_ALGORITHM,
		std::span<const std::span<const uint8_t>>(chunks.begin(), chunks.size()),
		out);
}

[[nodiscard]] bool cng_sha384(
	std::initializer_list<std::span<const uint8_t>> chunks,
	std::array<uint8_t, 48>& out)
{
	return cng_hash(
		BCRYPT_SHA384_ALGORITHM,
		std::span<const std::span<const uint8_t>>(chunks.begin(), chunks.size()),
		out);
}

[[nodiscard]] bool cng_sha512(
	std::initializer_list<std::span<const uint8_t>> chunks,
	std::array<uint8_t, 64>& out)
{
	return cng_hash(
		BCRYPT_SHA512_ALGORITHM,
		std::span<const std::span<const uint8_t>>(chunks.begin(), chunks.size()),
		out);
}

// Encrypts/decrypts `data` in place with RC4 (`BCRYPT_RC4_ALGORITHM`) using `key`.
[[nodiscard]] bool cng_rc4(
	std::span<const uint8_t> key,
	std::span<uint8_t> data)
{
	if (key.empty()) {
		return false;
	}
	if (data.empty()) {
		return true;
	}
	BcryptAlgCloser alg{};
	if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_RC4_ALGORITHM, nullptr, 0))) {
		return false;
	}
	DWORD obj_len = 0;
	DWORD cb_res = 0;
	if (!BCRYPT_SUCCESS(BCryptGetProperty(
			alg.h,
			BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&obj_len),
			sizeof(obj_len),
			&cb_res,
			0))) {
		return false;
	}
	std::array<uint8_t, 1024> stack_obj{};
	std::vector<uint8_t> heap_obj;
	PUCHAR obj_ptr = stack_obj.data();
	if (obj_len > stack_obj.size()) {
		heap_obj.resize(obj_len);
		obj_ptr = heap_obj.data();
	}
	BcryptKeyCloser hkey{};
	if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
			alg.h,
			&hkey.h,
			obj_ptr,
			obj_len,
			const_cast<PUCHAR>(key.data()),
			static_cast<ULONG>(key.size()),
			0))) {
		return false;
	}
	ULONG out_len = 0;
	return BCRYPT_SUCCESS(BCryptEncrypt(
		hkey.h,
		data.data(),
		static_cast<ULONG>(data.size()),
		nullptr,
		nullptr,
		0,
		data.data(),
		static_cast<ULONG>(data.size()),
		&out_len,
		0));
}

// Performs raw AES-CBC encryption (`encrypt == true`) or decryption (`encrypt == false`)
// without padding via `BCRYPT_AES_ALGORITHM` (`BCRYPT_CHAIN_MODE_CBC`).
// `input.size()` and `output.size()` must be a positive multiple of 16.
[[nodiscard]] bool cng_aes_cbc_raw(
	bool encrypt,
	std::span<const uint8_t> key,
	std::span<const uint8_t, 16> iv_in,
	std::span<const uint8_t> input,
	std::span<uint8_t> output)
{
	if ((key.size() != 16u && key.size() != 32u) ||
		input.size() != output.size() ||
		(input.size() % 16u) != 0u) {
		return false;
	}
	if (input.empty()) {
		return true;
	}
	BcryptAlgCloser alg{};
	if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
		return false;
	}
	if (!BCRYPT_SUCCESS(BCryptSetProperty(
			alg.h,
			BCRYPT_CHAINING_MODE,
			reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
			sizeof(BCRYPT_CHAIN_MODE_CBC),
			0))) {
		return false;
	}
	DWORD obj_len = 0;
	DWORD cb_res = 0;
	if (!BCRYPT_SUCCESS(BCryptGetProperty(
			alg.h,
			BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&obj_len),
			sizeof(obj_len),
			&cb_res,
			0))) {
		return false;
	}
	std::array<uint8_t, 1024> stack_obj{};
	std::vector<uint8_t> heap_obj;
	PUCHAR obj_ptr = stack_obj.data();
	if (obj_len > stack_obj.size()) {
		heap_obj.resize(obj_len);
		obj_ptr = heap_obj.data();
	}
	BcryptKeyCloser hkey{};
	if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
			alg.h,
			&hkey.h,
			obj_ptr,
			obj_len,
			const_cast<PUCHAR>(key.data()),
			static_cast<ULONG>(key.size()),
			0))) {
		return false;
	}
	std::array<uint8_t, 16> iv{};
	std::memcpy(iv.data(), iv_in.data(), 16u);
	ULONG out_len = 0;
	if (encrypt) {
		return BCRYPT_SUCCESS(BCryptEncrypt(
			hkey.h,
			const_cast<PUCHAR>(input.data()),
			static_cast<ULONG>(input.size()),
			nullptr,
			iv.data(),
			static_cast<ULONG>(iv.size()),
			output.data(),
			static_cast<ULONG>(output.size()),
			&out_len,
			0));
	}
	return BCRYPT_SUCCESS(BCryptDecrypt(
		hkey.h,
		const_cast<PUCHAR>(input.data()),
		static_cast<ULONG>(input.size()),
		nullptr,
		iv.data(),
		static_cast<ULONG>(iv.size()),
		output.data(),
		static_cast<ULONG>(output.size()),
		&out_len,
		0));
}

// Performs raw AES-256-ECB decryption without padding via `BCRYPT_AES_ALGORITHM`
// (`BCRYPT_CHAIN_MODE_ECB`), used to verify `/Perms` in `R=5..6`.
[[nodiscard]] bool cng_aes256_ecb_decrypt(
	std::span<const uint8_t, 32> key,
	std::span<const uint8_t, 16> input,
	std::span<uint8_t, 16> output)
{
	BcryptAlgCloser alg{};
	if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
		return false;
	}
	if (!BCRYPT_SUCCESS(BCryptSetProperty(
			alg.h,
			BCRYPT_CHAINING_MODE,
			reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
			sizeof(BCRYPT_CHAIN_MODE_ECB),
			0))) {
		return false;
	}
	DWORD obj_len = 0;
	DWORD cb_res = 0;
	if (!BCRYPT_SUCCESS(BCryptGetProperty(
			alg.h,
			BCRYPT_OBJECT_LENGTH,
			reinterpret_cast<PUCHAR>(&obj_len),
			sizeof(obj_len),
			&cb_res,
			0))) {
		return false;
	}
	std::array<uint8_t, 1024> stack_obj{};
	std::vector<uint8_t> heap_obj;
	PUCHAR obj_ptr = stack_obj.data();
	if (obj_len > stack_obj.size()) {
		heap_obj.resize(obj_len);
		obj_ptr = heap_obj.data();
	}
	BcryptKeyCloser hkey{};
	if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
			alg.h,
			&hkey.h,
			obj_ptr,
			obj_len,
			const_cast<PUCHAR>(key.data()),
			static_cast<ULONG>(key.size()),
			0))) {
		return false;
	}
	ULONG out_len = 0;
	return BCRYPT_SUCCESS(BCryptDecrypt(
		hkey.h,
		const_cast<PUCHAR>(input.data()),
		16u,
		nullptr,
		nullptr,
		0,
		output.data(),
		16u,
		&out_len,
		0));
}

// Pads or truncates `password` to 32 bytes using `kPdfPasswordPadding`
// (ISO 32000-1 section 7.6.3.3 step a).
[[nodiscard]] std::array<uint8_t, 32> pad_password_r2_r4(std::string_view password) noexcept
{
	std::array<uint8_t, 32> out{};
	const size_t n = std::min<size_t>(password.size(), 32u);
	if (n > 0u) {
		std::memcpy(out.data(), password.data(), n);
	}
	if (n < 32u) {
		std::memcpy(out.data() + n, kPdfPasswordPadding.data(), 32u - n);
	}
	return out;
}

// RFC 3454 Table B.1: Commonly mapped to nothing.
[[nodiscard]] constexpr bool is_saslprep_mapped_to_nothing(uint32_t cp) noexcept
{
	return cp == 0x00ADu || cp == 0x034Fu || cp == 0x1806u ||
		(cp >= 0x180Bu && cp <= 0x180Du) ||
		(cp >= 0x200Bu && cp <= 0x200Du) ||
		cp == 0x2060u ||
		(cp >= 0xFE00u && cp <= 0xFE0Fu) ||
		cp == 0xFEFFu;
}

// RFC 3454 Table C.1.2: Non-ASCII space characters.
[[nodiscard]] constexpr bool is_saslprep_non_ascii_space(uint32_t cp) noexcept
{
	return cp == 0x00A0u || cp == 0x1680u ||
		(cp >= 0x2000u && cp <= 0x200Bu) ||
		cp == 0x202Fu || cp == 0x205Fu || cp == 0x3000u;
}

// RFC 4013 Section 2.3 Prohibited Output (RFC 3454 Tables C.1.2, C.2.1, C.2.2,
// C.3, C.4, C.5, C.6, C.7, C.8, and C.9).
[[nodiscard]] constexpr bool is_saslprep_prohibited(uint32_t cp) noexcept
{
	// Table C.1.2: Non-ASCII space characters
	if (is_saslprep_non_ascii_space(cp)) {
		return true;
	}
	// Table C.2.1: ASCII control characters
	if (cp <= 0x001Fu || cp == 0x007Fu) {
		return true;
	}
	// Table C.2.2: Non-ASCII control characters
	if ((cp >= 0x0080u && cp <= 0x009Fu) ||
		cp == 0x06DDu || cp == 0x070Fu || cp == 0x180Eu ||
		(cp >= 0x200Cu && cp <= 0x200Du) ||
		(cp >= 0x2028u && cp <= 0x2029u) ||
		(cp >= 0x2060u && cp <= 0x2063u) ||
		(cp >= 0x206Au && cp <= 0x206Fu) ||
		cp == 0xFEFFu ||
		(cp >= 0xFFF9u && cp <= 0xFFFCu) ||
		(cp >= 0x1D173u && cp <= 0x1D17Au)) {
		return true;
	}
	// Table C.3: Private Use characters
	if ((cp >= 0xE000u && cp <= 0xF8FFu) ||
		(cp >= 0xF0000u && cp <= 0xFFFFDu) ||
		(cp >= 0x100000u && cp <= 0x10FFFDu)) {
		return true;
	}
	// Table C.4: Non-character code points
	if ((cp >= 0xFDD0u && cp <= 0xFDEFu) ||
		((cp & 0xFFFFu) >= 0xFFFEu && cp <= 0x10FFFFu)) {
		return true;
	}
	// Table C.5: Surrogate code points
	if (cp >= 0xD800u && cp <= 0xDFFFu) {
		return true;
	}
	// Table C.6: Inappropriate for plain text
	if (cp >= 0xFFF9u && cp <= 0xFFFDu) {
		return true;
	}
	// Table C.7: Inappropriate for canonical representation
	if (cp >= 0x2FF0u && cp <= 0x2FFBu) {
		return true;
	}
	// Table C.8: Change display properties or are deprecated
	if (cp == 0x0340u || cp == 0x0341u ||
		cp == 0x200Eu || cp == 0x200Fu ||
		(cp >= 0x202Au && cp <= 0x202Eu) ||
		(cp >= 0x206Au && cp <= 0x206Fu)) {
		return true;
	}
	// Table C.9: Tagging characters
	if (cp == 0xE0001u || (cp >= 0xE0020u && cp <= 0xE007Fu)) {
		return true;
	}
	return cp > 0x10FFFFu;
}

// RFC 3454 Table D.1: Characters with bidirectional property "R" or "AL" (RandALCat).
[[nodiscard]] bool is_saslprep_randalcat(uint32_t cp) noexcept
{
	if (cp == 0x05BEu || cp == 0x05C0u || cp == 0x05C3u ||
		(cp >= 0x05D0u && cp <= 0x05EAu) ||
		(cp >= 0x05F0u && cp <= 0x05F4u) ||
		cp == 0x061Bu || cp == 0x061Fu ||
		(cp >= 0x0621u && cp <= 0x063Au) ||
		(cp >= 0x0640u && cp <= 0x064Au) ||
		(cp >= 0x066Du && cp <= 0x066Fu) ||
		(cp >= 0x0671u && cp <= 0x06D5u) ||
		cp == 0x06DDu ||
		(cp >= 0x06E5u && cp <= 0x06E6u) ||
		(cp >= 0x06FAu && cp <= 0x06FEu) ||
		(cp >= 0x0700u && cp <= 0x070Du) ||
		(cp >= 0x070Fu && cp <= 0x0710u) ||
		(cp >= 0x0712u && cp <= 0x072Cu) ||
		(cp >= 0x0780u && cp <= 0x07A5u) ||
		cp == 0x07B1u || cp == 0x200Fu || cp == 0xFB1Du ||
		(cp >= 0xFB1Fu && cp <= 0xFB28u) ||
		(cp >= 0xFB2Au && cp <= 0xFB36u) ||
		(cp >= 0xFB38u && cp <= 0xFB3Cu) ||
		cp == 0xFB3Eu ||
		(cp >= 0xFB40u && cp <= 0xFB41u) ||
		(cp >= 0xFB43u && cp <= 0xFB44u) ||
		(cp >= 0xFB46u && cp <= 0xFBB1u) ||
		(cp >= 0xFBD3u && cp <= 0xFD3Du) ||
		(cp >= 0xFD50u && cp <= 0xFD8Fu) ||
		(cp >= 0xFD92u && cp <= 0xFDC7u) ||
		(cp >= 0xFDF0u && cp <= 0xFDFCu) ||
		(cp >= 0xFE70u && cp <= 0xFE74u) ||
		(cp >= 0xFE76u && cp <= 0xFEFCu) ||
		(cp >= 0x10800u && cp <= 0x10FFFu) ||
		(cp >= 0x1E800u && cp <= 0x1EEFFu)) {
		return true;
	}
	if (cp <= 0xFFFFu) {
		const wchar_t wc = static_cast<wchar_t>(cp);
		WORD ctype2 = 0;
		if (GetStringTypeW(CT_CTYPE2, &wc, 1, &ctype2) && ctype2 == C2_RIGHTTOLEFT) {
			return true;
		}
	}
	return false;
}

// RFC 3454 Table D.2: Characters with bidirectional property "L" (LCat).
[[nodiscard]] bool is_saslprep_lcat(uint32_t cp) noexcept
{
	if (is_saslprep_randalcat(cp)) {
		return false;
	}
	if ((cp >= 0x0041u && cp <= 0x005Au) ||
		(cp >= 0x0061u && cp <= 0x007Au) ||
		cp == 0x200Eu) {
		return true;
	}
	if (cp <= 0xFFFFu) {
		const wchar_t wc = static_cast<wchar_t>(cp);
		WORD ctype2 = 0;
		if (GetStringTypeW(CT_CTYPE2, &wc, 1, &ctype2) && ctype2 == C2_LEFTTORIGHT) {
			return true;
		}
		return false;
	}
	wchar_t pair[2] = {
		static_cast<wchar_t>(0xD800u + ((cp - 0x10000u) >> 10u)),
		static_cast<wchar_t>(0xDC00u + ((cp - 0x10000u) & 0x3FFu)),
	};
	WORD ctype2[2] = {0, 0};
	if (GetStringTypeW(CT_CTYPE2, pair, 2, ctype2) && ctype2[0] == C2_LEFTTORIGHT) {
		return true;
	}
	return (cp >= 0x10000u && cp <= 0x107FFu) ||
		(cp >= 0x11000u && cp <= 0x1D7FFu) ||
		(cp >= 0x20000u && cp <= 0x2FA1Du);
}

// Strict UTF-8 decoder into Unicode scalar values (`U+0000..U+10FFFF` excluding surrogates).
[[nodiscard]] bool decode_utf8_codepoints(
	std::string_view utf8,
	std::vector<uint32_t>& out_cps)
{
	out_cps.clear();
	out_cps.reserve(utf8.size());
	size_t i = 0;
	while (i < utf8.size()) {
		const uint8_t b0 = static_cast<uint8_t>(utf8[i]);
		if (b0 < 0x80u) {
			out_cps.push_back(b0);
			++i;
			continue;
		}
		uint32_t cp = 0;
		size_t extra = 0;
		uint32_t min_cp = 0;
		if ((b0 & 0xE0u) == 0xC0u) {
			cp = b0 & 0x1Fu;
			extra = 1u;
			min_cp = 0x80u;
		} else if ((b0 & 0xF0u) == 0xE0u) {
			cp = b0 & 0x0Fu;
			extra = 2u;
			min_cp = 0x800u;
		} else if ((b0 & 0xF8u) == 0xF0u) {
			cp = b0 & 0x07u;
			extra = 3u;
			min_cp = 0x10000u;
		} else {
			return false;
		}
		if (i + extra >= utf8.size()) {
			return false;
		}
		for (size_t k = 1u; k <= extra; ++k) {
			const uint8_t bx = static_cast<uint8_t>(utf8[i + k]);
			if ((bx & 0xC0u) != 0x80u) {
				return false;
			}
			cp = (cp << 6u) | (bx & 0x3Fu);
		}
		if (cp < min_cp || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
			return false;
		}
		out_cps.push_back(cp);
		i += 1u + extra;
	}
	return true;
}

[[nodiscard]] bool decode_utf16_codepoints(
	std::wstring_view utf16,
	std::vector<uint32_t>& out_cps)
{
	out_cps.clear();
	out_cps.reserve(utf16.size());
	size_t i = 0;
	while (i < utf16.size()) {
		const uint32_t w0 = static_cast<uint16_t>(utf16[i]);
		if (w0 >= 0xD800u && w0 <= 0xDBFFu) {
			if (i + 1u >= utf16.size()) {
				return false;
			}
			const uint32_t w1 = static_cast<uint16_t>(utf16[i + 1u]);
			if (w1 < 0xDC00u || w1 > 0xDFFFu) {
				return false;
			}
			const uint32_t cp = 0x10000u + (((w0 - 0xD800u) << 10u) | (w1 - 0xDC00u));
			out_cps.push_back(cp);
			i += 2u;
		} else if (w0 >= 0xDC00u && w0 <= 0xDFFFu) {
			return false;
		} else {
			out_cps.push_back(w0);
			++i;
		}
	}
	return true;
}

// Prepares a UTF-8 password for `R=5..6` according to RFC 4013 (SASLprep) and
// ISO 32000-2 section 7.6.4.3.4:
// 1. Maps RFC 3454 Table B.1 characters to nothing and Table C.1.2 non-ASCII
//    spaces to `U+0020` SPACE (preserving empty results when all characters map
//    to nothing, e.g. `U+00AD`).
// 2. Normalizes via Unicode Normalization Form KC (`NormalizationKC`).
// 3. Rejects RFC 4013 Section 2.3 prohibited output characters (Tables C.1.2,
//    C.2.1, C.2.2, C.3, C.4, C.5, C.6, C.7, C.8, C.9).
// 4. Enforces RFC 4013 Section 2.4 / RFC 3454 Section 6 bidirectional rules
//    (Tables D.1 RandALCat and D.2 LCat).
// 5. Encodes to UTF-8 and truncates to at most 127 bytes.
[[nodiscard]] std::optional<std::vector<uint8_t>> prepare_password_r5_r6(std::string_view password)
{
	if (password.empty()) {
		return std::vector<uint8_t>{};
	}

	std::vector<uint32_t> input_cps;
	if (!decode_utf8_codepoints(password, input_cps)) {
		return std::nullopt;
	}

	std::wstring mapped_utf16;
	mapped_utf16.reserve(input_cps.size());
	for (uint32_t cp : input_cps) {
		if (is_saslprep_mapped_to_nothing(cp)) {
			continue;
		}
		if (is_saslprep_non_ascii_space(cp)) {
			cp = 0x0020u;
		}
		if (cp <= 0xFFFFu) {
			mapped_utf16.push_back(static_cast<wchar_t>(cp));
		} else {
			const uint32_t v = cp - 0x10000u;
			mapped_utf16.push_back(static_cast<wchar_t>(0xD800u + (v >> 10u)));
			mapped_utf16.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
		}
	}

	// Preserve valid empty normalization results (e.g. password consisting only of U+00AD).
	if (mapped_utf16.empty()) {
		return std::vector<uint8_t>{};
	}

	const int est = NormalizeString(
		NormalizationKC,
		mapped_utf16.data(),
		static_cast<int>(mapped_utf16.size()),
		nullptr,
		0);
	if (est <= 0) {
		return std::nullopt;
	}
	std::wstring nfkc_utf16(static_cast<size_t>(est), L'\0');
	const int actual = NormalizeString(
		NormalizationKC,
		mapped_utf16.data(),
		static_cast<int>(mapped_utf16.size()),
		nfkc_utf16.data(),
		est);
	if (actual < 0) {
		return std::nullopt;
	}
	nfkc_utf16.resize(static_cast<size_t>(actual));
	if (nfkc_utf16.empty()) {
		return std::vector<uint8_t>{};
	}

	std::vector<uint32_t> norm_cps;
	if (!decode_utf16_codepoints(nfkc_utf16, norm_cps)) {
		return std::nullopt;
	}

	bool has_randal = false;
	bool has_l = false;
	for (uint32_t cp : norm_cps) {
		if (is_saslprep_prohibited(cp)) {
			return std::nullopt;
		}
		if (is_saslprep_randalcat(cp)) {
			has_randal = true;
		} else if (is_saslprep_lcat(cp)) {
			has_l = true;
		}
	}

	// RFC 3454 Section 6 Bidirectional rules:
	// Rule 2: If a string contains any RandALCat character, it MUST NOT contain any LCat character.
	// Rule 3: If a string contains any RandALCat character, a RandALCat character MUST be the
	//         first character of the string, and a RandALCat character MUST be the last character.
	if (has_randal) {
		if (has_l ||
			!is_saslprep_randalcat(norm_cps.front()) ||
			!is_saslprep_randalcat(norm_cps.back())) {
			return std::nullopt;
		}
	}

	std::vector<uint8_t> utf8_out;
	utf8_out.reserve(norm_cps.size() * 4u);
	for (uint32_t cp : norm_cps) {
		if (cp <= 0x7Fu) {
			utf8_out.push_back(static_cast<uint8_t>(cp));
		} else if (cp <= 0x7FFu) {
			utf8_out.push_back(static_cast<uint8_t>(0xC0u | (cp >> 6u)));
			utf8_out.push_back(static_cast<uint8_t>(0x80u | (cp & 0x3Fu)));
		} else if (cp <= 0xFFFFu) {
			utf8_out.push_back(static_cast<uint8_t>(0xE0u | (cp >> 12u)));
			utf8_out.push_back(static_cast<uint8_t>(0x80u | ((cp >> 6u) & 0x3Fu)));
			utf8_out.push_back(static_cast<uint8_t>(0x80u | (cp & 0x3Fu)));
		} else {
			utf8_out.push_back(static_cast<uint8_t>(0xF0u | (cp >> 18u)));
			utf8_out.push_back(static_cast<uint8_t>(0x80u | ((cp >> 12u) & 0x3Fu)));
			utf8_out.push_back(static_cast<uint8_t>(0x80u | ((cp >> 6u) & 0x3Fu)));
			utf8_out.push_back(static_cast<uint8_t>(0x80u | (cp & 0x3Fu)));
		}
	}
	if (utf8_out.size() > 127u) {
		utf8_out.resize(127u);
	}
	return utf8_out;
}

// ISO 32000-1 section 7.6.3.3 Algorithm 2: Computing an encryption key (`R=2..4`).
[[nodiscard]] bool derive_file_key_r2_r4(
	std::span<const uint8_t, 32> padded_pwd,
	const EncryptDictParams& params,
	std::span<const uint8_t> file_id_0,
	std::vector<uint8_t>& out_key)
{
	if (params.o_bytes.size() < 32u) {
		return false;
	}
	const uint32_t u_p = static_cast<uint32_t>(params.p);
	const std::array<uint8_t, 4> p_le = {
		static_cast<uint8_t>(u_p & 0xFFu),
		static_cast<uint8_t>((u_p >> 8u) & 0xFFu),
		static_cast<uint8_t>((u_p >> 16u) & 0xFFu),
		static_cast<uint8_t>((u_p >> 24u) & 0xFFu),
	};
	const std::array<uint8_t, 4> no_meta_ff = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
	const std::span<const uint8_t> o_span(params.o_bytes.data(), 32u);
	const std::span<const uint8_t> meta_span =
		(params.r >= 4 && !params.encrypt_metadata)
			? std::span<const uint8_t>(no_meta_ff)
			: std::span<const uint8_t>{};

	std::array<uint8_t, 16> digest{};
	if (!cng_md5({padded_pwd, o_span, p_le, file_id_0, meta_span}, digest)) {
		return false;
	}

	const size_t key_bytes = (params.r == 2)
		? 5u
		: static_cast<size_t>(params.key_length_bits / 8);
	if (key_bytes == 0u || key_bytes > 16u) {
		return false;
	}

	if (params.r >= 3) {
		for (int i = 0; i < 50; ++i) {
			std::array<uint8_t, 16> next{};
			if (!cng_md5({std::span<const uint8_t>(digest.data(), key_bytes)}, next)) {
				return false;
			}
			digest = next;
		}
	}

	out_key.assign(digest.begin(), digest.begin() + key_bytes);
	return true;
}

// ISO 32000-1 section 7.6.3.4 Algorithm 4 (`R=2`) and Algorithm 5 (`R=3, 4`):
// Computes the expected `/U` value from `file_key` and `file_id_0`.
[[nodiscard]] bool compute_u_value_r2_r4(
	int32_t revision,
	std::span<const uint8_t> file_key,
	std::span<const uint8_t> file_id_0,
	std::array<uint8_t, 32>& out_u)
{
	if (file_key.empty() || file_key.size() > 16u) {
		return false;
	}
	if (revision == 2) {
		std::memcpy(out_u.data(), kPdfPasswordPadding.data(), 32u);
		return cng_rc4(file_key, out_u);
	}
	std::array<uint8_t, 16> digest{};
	if (!cng_md5({kPdfPasswordPadding, file_id_0}, digest)) {
		return false;
	}
	if (!cng_rc4(file_key, digest)) {
		return false;
	}
	std::array<uint8_t, 16> iter_key_buf{};
	const std::span<uint8_t> iter_key(iter_key_buf.data(), file_key.size());
	for (uint8_t i = 1u; i <= 19u; ++i) {
		for (size_t k = 0; k < file_key.size(); ++k) {
			iter_key[k] = static_cast<uint8_t>(file_key[k] ^ i);
		}
		if (!cng_rc4(iter_key, digest)) {
			return false;
		}
	}
	out_u.fill(uint8_t{0});
	std::memcpy(out_u.data(), digest.data(), 16u);
	return true;
}

// ISO 32000-1 section 7.6.3.4 Algorithm 7: Unwraps the padded user password
// from `/O` using the owner password (`R=2..4`).
[[nodiscard]] bool unwrap_owner_password_r2_r4(
	std::string_view owner_password,
	const EncryptDictParams& params,
	std::array<uint8_t, 32>& out_user_padded)
{
	if (params.o_bytes.size() < 32u) {
		return false;
	}
	const auto padded_owner = pad_password_r2_r4(owner_password);
	std::array<uint8_t, 16> digest{};
	if (!cng_md5({padded_owner}, digest)) {
		return false;
	}
	if (params.r >= 3) {
		for (int i = 0; i < 50; ++i) {
			std::array<uint8_t, 16> next{};
			if (!cng_md5({digest}, next)) {
				return false;
			}
			digest = next;
		}
	}
	const size_t key_bytes = (params.r == 2)
		? 5u
		: static_cast<size_t>(params.key_length_bits / 8);
	if (key_bytes == 0u || key_bytes > 16u) {
		return false;
	}
	const std::span<const uint8_t> rc4_key(digest.data(), key_bytes);
	std::memcpy(out_user_padded.data(), params.o_bytes.data(), 32u);
	if (params.r == 2) {
		return cng_rc4(rc4_key, out_user_padded);
	}
	std::array<uint8_t, 16> iter_key_buf{};
	const std::span<uint8_t> iter_key(iter_key_buf.data(), key_bytes);
	for (int i = 19; i >= 0; --i) {
		for (size_t k = 0; k < key_bytes; ++k) {
			iter_key[k] = static_cast<uint8_t>(rc4_key[k] ^ static_cast<uint8_t>(i));
		}
		if (!cng_rc4(iter_key, out_user_padded)) {
			return false;
		}
	}
	return true;
}

// Compute `sum(E) mod 3` for a big-endian unsigned integer `E` in `R=6`
// Algorithm 2.B (`(a * 10 + b) mod 3 == (a + b) mod 3` because `256 == 1 mod 3`).
[[nodiscard]] constexpr uint32_t mod3_big_endian(std::span<const uint8_t> bytes) noexcept
{
	uint32_t acc = 0u;
	for (uint8_t b : bytes) {
		acc += b;
	}
	return acc % 3u;
}

// ISO 32000-2:2020 section 7.6.4.3.4 Algorithm 2.B (`R=6` iterative hash) and
// `R=5` SHA-256 hash.
[[nodiscard]] bool compute_hash_r5_r6(
	int32_t revision,
	std::span<const uint8_t> password_utf8,
	std::span<const uint8_t, 8> salt,
	std::span<const uint8_t> u_bytes_48,
	std::array<uint8_t, 32>& out_hash)
{
	if (revision == 5) {
		return cng_sha256({password_utf8, salt, u_bytes_48}, out_hash);
	}

	// Revision 6 Algorithm 2.B:
	// Initial K = SHA-256(password || salt || u_bytes_48).
	std::array<uint8_t, 64> k_buf{};
	size_t k_len = 32u;
	{
		std::array<uint8_t, 32> k0{};
		if (!cng_sha256({password_utf8, salt, u_bytes_48}, k0)) {
			return false;
		}
		std::memcpy(k_buf.data(), k0.data(), 32u);
	}

	std::vector<uint8_t> k1;
	std::vector<uint8_t> e_buf;
	int round = 0;
	while (true) {
		const size_t single_len = password_utf8.size() + k_len + u_bytes_48.size();
		const size_t total_len = single_len * 64u;
		k1.resize(total_len);
		for (size_t r = 0; r < 64u; ++r) {
			uint8_t* dst = k1.data() + r * single_len;
			if (!password_utf8.empty()) {
				std::memcpy(dst, password_utf8.data(), password_utf8.size());
				dst += password_utf8.size();
			}
			std::memcpy(dst, k_buf.data(), k_len);
			dst += k_len;
			if (!u_bytes_48.empty()) {
				std::memcpy(dst, u_bytes_48.data(), u_bytes_48.size());
			}
		}

		e_buf.resize(total_len);
		const std::span<const uint8_t, 16> aes_key(k_buf.data(), 16u);
		const std::span<const uint8_t, 16> aes_iv(k_buf.data() + 16u, 16u);
		if (!cng_aes_cbc_raw(/*encrypt=*/true, aes_key, aes_iv, k1, e_buf)) {
			return false;
		}

		const uint32_t m = mod3_big_endian(std::span<const uint8_t>(e_buf.data(), 16u));
		if (m == 0u) {
			std::array<uint8_t, 32> h256{};
			if (!cng_sha256({e_buf}, h256)) {
				return false;
			}
			std::memcpy(k_buf.data(), h256.data(), 32u);
			k_len = 32u;
		} else if (m == 1u) {
			std::array<uint8_t, 48> h384{};
			if (!cng_sha384({e_buf}, h384)) {
				return false;
			}
			std::memcpy(k_buf.data(), h384.data(), 48u);
			k_len = 48u;
		} else {
			std::array<uint8_t, 64> h512{};
			if (!cng_sha512({e_buf}, h512)) {
				return false;
			}
			std::memcpy(k_buf.data(), h512.data(), 64u);
			k_len = 64u;
		}

		++round;
		const uint8_t last_e = e_buf.back();
		if (round >= 64 && static_cast<int>(last_e) <= (round - 32)) {
			break;
		}
	}

	std::memcpy(out_hash.data(), k_buf.data(), 32u);
	return true;
}

[[nodiscard]] PdfResult<CryptFilterMethod> parse_cfm_name(std::string_view cfm)
{
	if (cfm == "None") {
		return CryptFilterMethod::None;
	}
	if (cfm == "V2") {
		return CryptFilterMethod::V2;
	}
	if (cfm == "AESV2") {
		return CryptFilterMethod::AESV2;
	}
	if (cfm == "AESV3") {
		return CryptFilterMethod::AESV3;
	}
	return make_crypto_error(
		ErrorCode::UnsupportedEncryption,
		DiagnosticId::UnsupportedEncryption,
		"Unsupported /CFM crypt filter method in /Encrypt dictionary");
}

} // namespace

PdfResult<EncryptDictParams> SecurityHandler::parse_encrypt_dict(
	const PdfDict& encrypt_dict) noexcept
{
	try {
		const auto filter_opt = encrypt_dict.get_name("Filter");
		if (!filter_opt.has_value() || *filter_opt != "Standard") {
			return make_crypto_error(
				ErrorCode::UnsupportedEncryption,
				DiagnosticId::UnsupportedEncryption,
				"Unsupported security handler /Filter (expected /Standard)");
		}

		if (const auto subfilter_opt = encrypt_dict.get_name("SubFilter")) {
			// Standard Security Handler does not define /SubFilter; reject non-standard handlers.
			(void)subfilter_opt;
			return make_crypto_error(
				ErrorCode::UnsupportedEncryption,
				DiagnosticId::UnsupportedEncryption,
				"Unsupported /SubFilter in /Standard /Encrypt dictionary");
		}

		const auto v_opt = encrypt_dict.get_int("V");
		const auto r_opt = encrypt_dict.get_int("R");
		if (!v_opt.has_value() || !r_opt.has_value()) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Missing required /V or /R integer in /Encrypt dictionary");
		}

		const int64_t v64 = *v_opt;
		const int64_t r64 = *r_opt;
		if ((v64 != 1 && v64 != 2 && v64 != 4 && v64 != 5) ||
			(r64 < 2 || r64 > 6)) {
			return make_crypto_error(
				ErrorCode::UnsupportedEncryption,
				DiagnosticId::UnsupportedEncryption,
				"Unsupported /V or /R value in /Standard /Encrypt dictionary");
		}

		// Validate /V and /R pairing per ISO 32000-1 Table 20 and ISO 32000-2 Table 20:
		// - V=1 requires R=2 (or R=3 with 40-bit key)
		// - V=2 requires R=3
		// - V=4 requires R=4
		// - V=5 requires R=5 or R=6
		if ((v64 == 1 && (r64 != 2 && r64 != 3)) ||
			(v64 == 2 && r64 != 3) ||
			(v64 == 4 && r64 != 4) ||
			(v64 == 5 && (r64 != 5 && r64 != 6))) {
			return make_crypto_error(
				ErrorCode::UnsupportedEncryption,
				DiagnosticId::UnsupportedEncryption,
				"Incompatible /V and /R combination in /Standard /Encrypt dictionary");
		}

		EncryptDictParams params{};
		params.v = static_cast<int32_t>(v64);
		params.r = static_cast<int32_t>(r64);

		if (params.v == 1) {
			params.key_length_bits = 40;
			if (const auto len_opt = encrypt_dict.get_int("Length")) {
				if (*len_opt != 40) {
					return make_crypto_error(
						ErrorCode::UnsupportedEncryption,
						DiagnosticId::UnsupportedEncryption,
						"Invalid /Length for /V 1 in /Encrypt dictionary (expected 40)");
				}
			}
		} else if (params.v == 2 || params.v == 4) {
			const int64_t default_bits = (params.v == 4) ? 128 : 40;
			const int64_t len_val = encrypt_dict.get_int("Length").value_or(default_bits);
			// Some producers write /Length in bytes (5..16) instead of bits (40..128).
			int64_t bits = len_val;
			if (bits >= 5 && bits <= 16) {
				bits *= 8;
			}
			if (bits < 40 || bits > 128 || (bits % 8) != 0) {
				return make_crypto_error(
					ErrorCode::UnsupportedEncryption,
					DiagnosticId::UnsupportedEncryption,
					"Invalid /Length in /Encrypt dictionary (expected 40..128 multiple of 8)");
			}
			params.key_length_bits = static_cast<int32_t>(bits);
		} else { // V == 5
			const int64_t len_val = encrypt_dict.get_int("Length").value_or(256);
			if (len_val != 256 && len_val != 32) {
				return make_crypto_error(
					ErrorCode::UnsupportedEncryption,
					DiagnosticId::UnsupportedEncryption,
					"Invalid /Length for /V 5 in /Encrypt dictionary (expected 256)");
			}
			params.key_length_bits = 256;
		}

		const auto p_opt = encrypt_dict.get_int("P");
		if (!p_opt.has_value()) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Missing required /P permissions integer in /Encrypt dictionary");
		}
		params.p = static_cast<int32_t>(static_cast<uint32_t>(*p_opt));

		if (params.r >= 4) {
			params.encrypt_metadata = encrypt_dict.get_bool("EncryptMetadata").value_or(true);
		} else {
			params.encrypt_metadata = true;
		}

		const PdfString* o_str = encrypt_dict.get_string("O");
		const PdfString* u_str = encrypt_dict.get_string("U");
		if (o_str == nullptr || u_str == nullptr) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"Missing required /O or /U byte string in /Encrypt dictionary");
		}
		params.o_bytes = o_str->bytes;
		params.u_bytes = u_str->bytes;

		if (params.r <= 4) {
			if (params.o_bytes.size() < 32u || params.u_bytes.size() < 32u) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/O and /U strings must be at least 32 bytes for revisions R=2..4");
			}
		} else {
			if (params.o_bytes.size() < 48u || params.u_bytes.size() < 48u) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"/O and /U strings must be at least 48 bytes for revisions R=5..6");
			}
			const PdfString* oe_str = encrypt_dict.get_string("OE");
			const PdfString* ue_str = encrypt_dict.get_string("UE");
			const PdfString* perms_str = encrypt_dict.get_string("Perms");
			if (oe_str == nullptr || ue_str == nullptr || perms_str == nullptr ||
				oe_str->bytes.size() != 32u || ue_str->bytes.size() != 32u ||
				perms_str->bytes.size() < 16u) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedDictionary,
					"Missing or invalid /OE, /UE (32 bytes), or /Perms (16 bytes) in R=5..6 /Encrypt dictionary");
			}
			params.oe_bytes = oe_str->bytes;
			params.ue_bytes = ue_str->bytes;
			params.perms.assign(perms_str->bytes.begin(), perms_str->bytes.begin() + 16u);
		}

		params.crypt_filters.emplace("Identity", CryptFilterMethod::None);
		if (params.v == 1 || params.v == 2) {
			params.stream_method = CryptFilterMethod::V2;
			params.string_method = CryptFilterMethod::V2;
			params.embedded_file_method = CryptFilterMethod::V2;
		} else {
			if (const PdfDict* cf_dict = encrypt_dict.get_dict("CF")) {
				for (const auto& [cf_name, cf_val] : *cf_dict) {
					if (cf_name.view() == "Identity") {
						continue;
					}
					const PdfDict* sub_cf = cf_val.as_dict();
					if (sub_cf == nullptr) {
						return make_crypto_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedDictionary,
							"Entry in /Encrypt /CF dictionary is not a dictionary");
					}
					const auto cfm_name = sub_cf->get_name("CFM").value_or("None");
					RECTO_PDF_TRY(const CryptFilterMethod method, parse_cfm_name(cfm_name));
					if (params.v == 4 && method == CryptFilterMethod::AESV3) {
						return make_crypto_error(
							ErrorCode::UnsupportedEncryption,
							DiagnosticId::UnsupportedEncryption,
							"/CFM /AESV3 is not valid in a /V 4 /Encrypt dictionary");
					}
					if (params.v == 5 && (method == CryptFilterMethod::V2 || method == CryptFilterMethod::AESV2)) {
						return make_crypto_error(
							ErrorCode::UnsupportedEncryption,
							DiagnosticId::UnsupportedEncryption,
							"/CFM /V2 or /AESV2 is not valid in a /V 5 /Encrypt dictionary");
					}
					params.crypt_filters[std::string(cf_name.view())] = method;
				}
			}

			params.stm_f = std::string(encrypt_dict.get_name("StmF").value_or("Identity"));
			params.str_f = std::string(encrypt_dict.get_name("StrF").value_or("Identity"));
			if (const auto eff_opt = encrypt_dict.get_name("EFF")) {
				params.eff_f = std::string(*eff_opt);
			}

			auto resolve_cf = [&](const std::string& name, CryptFilterMethod& out_m) -> PdfResult<void> {
				const auto it = params.crypt_filters.find(name);
				if (it == params.crypt_filters.end()) {
					return make_crypto_error(
						ErrorCode::UnsupportedEncryption,
						DiagnosticId::UnsupportedEncryption,
						"Referenced crypt filter in /StmF, /StrF, or /EFF is not defined in /CF");
				}
				out_m = it->second;
				return {};
			};

			RECTO_PDF_RETURN_IF_ERROR(resolve_cf(params.stm_f, params.stream_method));
			RECTO_PDF_RETURN_IF_ERROR(resolve_cf(params.str_f, params.string_method));
			if (!params.eff_f.empty()) {
				RECTO_PDF_RETURN_IF_ERROR(resolve_cf(params.eff_f, params.embedded_file_method));
			} else {
				params.embedded_file_method = params.stream_method;
			}
		}

		return params;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while parsing /Encrypt dictionary");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while parsing /Encrypt dictionary");
	}
}

PdfResult<SecurityHandler> SecurityHandler::create(
	const PdfDict& encrypt_dict,
	std::span<const uint8_t> file_id_0,
	std::optional<IndirectRef> encrypt_ref) noexcept
{
	try {
		RECTO_PDF_TRY(EncryptDictParams parsed, parse_encrypt_dict(encrypt_dict));
		SecurityHandler handler;
		handler.params_ = std::move(parsed);
		handler.permissions_ = PdfPermissions::from_bitmask(handler.params_.p, handler.params_.r);
		handler.file_id_0_.assign(file_id_0.begin(), file_id_0.end());
		handler.encrypt_ref_ = encrypt_ref;

		// Automatically try empty user password ("") on creation so owner-only
		// restricted documents open without requiring a password prompt.
		// Suppress only WrongPassword; propagate DamagedFormat (/Perms mismatch),
		// OutOfMemory, or InternalError.
		const auto auth_res = handler.authenticate("");
		if (!auth_res.has_value() && auth_res.error().code != ErrorCode::WrongPassword) {
			return auth_res.error();
		}
		return handler;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while creating SecurityHandler");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while creating SecurityHandler");
	}
}

PdfResult<std::optional<SecurityHandler>> SecurityHandler::from_xref(
	const XrefTable& xref,
	std::span<const uint8_t> file_bytes) noexcept
{
	try {
		const PdfValue* enc_val = xref.trailer().get("Encrypt");
		if (enc_val == nullptr || enc_val->is_null()) {
			return std::optional<SecurityHandler>{std::nullopt};
		}

		std::optional<IndirectRef> enc_ref = enc_val->as_ref();
		PdfValue resolved_enc;
		const PdfDict* enc_dict = enc_val->as_dict();
		if (enc_dict == nullptr) {
			if (!enc_ref.has_value()) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedTrailer,
					"Trailer /Encrypt entry is neither a dictionary nor an indirect reference");
			}
			RECTO_PDF_TRY(resolved_enc, xref.resolve_object(file_bytes, *enc_ref));
			enc_dict = resolved_enc.as_dict();
			if (enc_dict == nullptr) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Resolved /Encrypt indirect object is not a dictionary",
					enc_ref->obj_num,
					enc_ref->gen_num);
			}
		}

		std::vector<uint8_t> id0;
		if (const PdfValue* id_val = xref.trailer().get("ID")) {
			PdfValue resolved_id;
			const PdfArray* id_arr = id_val->as_array();
			if (id_arr == nullptr && id_val->is_ref()) {
				RECTO_PDF_TRY(resolved_id, xref.resolve_value(file_bytes, *id_val));
				id_arr = resolved_id.as_array();
			}
			if (id_arr != nullptr && !id_arr->empty()) {
				if (const PdfString* first_id = (*id_arr)[0].as_string()) {
					id0 = first_id->bytes;
				}
			}
		}

		RECTO_PDF_TRY(
			SecurityHandler handler,
			create(*enc_dict, id0, enc_ref));
		return std::optional<SecurityHandler>{std::move(handler)};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while initializing SecurityHandler from XRefTable");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while initializing SecurityHandler from XRefTable");
	}
}

PdfResult<void> SecurityHandler::authenticate(std::string_view password) noexcept
{
	try {
		if (params_.r <= 4) {
			// 1. Try as user password (ISO 32000-1 section 7.6.3.4 Algorithm 6):
			const auto padded_user = pad_password_r2_r4(password);
			std::vector<uint8_t> cand_key;
			if (!derive_file_key_r2_r4(padded_user, params_, file_id_0_, cand_key)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG MD5 key derivation failed during R=2..4 user authentication");
			}
			std::array<uint8_t, 32> computed_u{};
			if (!compute_u_value_r2_r4(params_.r, cand_key, file_id_0_, computed_u)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG RC4/MD5 computation failed during R=2..4 user authentication");
			}
			const size_t cmp_len = (params_.r == 2) ? 32u : 16u;
			if (std::memcmp(computed_u.data(), params_.u_bytes.data(), cmp_len) == 0) {
				file_key_ = std::move(cand_key);
				authenticated_ = true;
				unlocked_as_owner_ = false;
				perms_verified_ = true;
				permissions_ = PdfPermissions::from_bitmask(params_.p, params_.r);
				decrypted_cache_.clear();
				decrypted_obj_stm_cache_.clear();
				return {};
			}

			// 2. Try as owner password (ISO 32000-1 section 7.6.3.4 Algorithm 7):
			std::array<uint8_t, 32> unwrapped_user{};
			if (!unwrap_owner_password_r2_r4(password, params_, unwrapped_user)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG RC4/MD5 unwrap failed during R=2..4 owner authentication");
			}
			if (!derive_file_key_r2_r4(unwrapped_user, params_, file_id_0_, cand_key)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG MD5 key derivation failed during R=2..4 owner verification");
			}
			if (!compute_u_value_r2_r4(params_.r, cand_key, file_id_0_, computed_u)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG RC4/MD5 computation failed during R=2..4 owner verification");
			}
			if (std::memcmp(computed_u.data(), params_.u_bytes.data(), cmp_len) == 0) {
				file_key_ = std::move(cand_key);
				authenticated_ = true;
				unlocked_as_owner_ = true;
				perms_verified_ = true;
				permissions_ = PdfPermissions::from_bitmask(params_.p, params_.r);
				decrypted_cache_.clear();
				decrypted_obj_stm_cache_.clear();
				return {};
			}

			return make_crypto_error(
				ErrorCode::WrongPassword,
				DiagnosticId::WrongPassword,
				"Supplied password did not authenticate against R=2..4 /U or /O");
		}

		// Revisions 5 and 6 (`AES-256`, ISO 32000-2 section 7.6.4.4.7 Algorithm 11 & 12):
		const auto prepared_pwd_opt = prepare_password_r5_r6(password);
		if (!prepared_pwd_opt.has_value()) {
			return make_crypto_error(
				ErrorCode::WrongPassword,
				DiagnosticId::WrongPassword,
				"Supplied password failed RFC 4013 SASLprep validation");
		}
		const std::vector<uint8_t>& pwd_bytes = *prepared_pwd_opt;
		const std::array<uint8_t, 16> zero_iv{};
		std::array<uint8_t, 32> unwrapped_key{};
		bool matched_user = false;
		bool matched_owner = false;

		// 1. Test against `/U` (validation salt `U[32..39]`, key salt `U[40..47]`):
		{
			const std::span<const uint8_t, 8> u_val_salt(params_.u_bytes.data() + 32u, 8u);
			const std::span<const uint8_t, 8> u_key_salt(params_.u_bytes.data() + 40u, 8u);
			std::array<uint8_t, 32> u_hash{};
			if (!compute_hash_r5_r6(params_.r, pwd_bytes, u_val_salt, {}, u_hash)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG hash failed during R=5..6 user password validation");
			}
			if (std::memcmp(u_hash.data(), params_.u_bytes.data(), 32u) == 0) {
				std::array<uint8_t, 32> ue_key{};
				if (!compute_hash_r5_r6(params_.r, pwd_bytes, u_key_salt, {}, ue_key)) {
					return make_crypto_error(
						ErrorCode::InternalError,
						DiagnosticId::InternalError,
						"CNG hash failed during R=5..6 /UE key derivation");
				}
				if (!cng_aes_cbc_raw(
						/*encrypt=*/false,
						ue_key,
						zero_iv,
						std::span<const uint8_t>(params_.ue_bytes.data(), 32u),
						unwrapped_key)) {
					return make_crypto_error(
						ErrorCode::InternalError,
						DiagnosticId::InternalError,
						"CNG AES-256-CBC unwrap failed for /UE");
				}
				matched_user = true;
			}
		}

		// 2. If user password did not match, test against `/O` (`O[32..39]`, `O[40..47]`, `U[0..47]`):
		if (!matched_user) {
			const std::span<const uint8_t, 8> o_val_salt(params_.o_bytes.data() + 32u, 8u);
			const std::span<const uint8_t, 8> o_key_salt(params_.o_bytes.data() + 40u, 8u);
			const std::span<const uint8_t> u_first_48(params_.u_bytes.data(), 48u);
			std::array<uint8_t, 32> o_hash{};
			if (!compute_hash_r5_r6(params_.r, pwd_bytes, o_val_salt, u_first_48, o_hash)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG hash failed during R=5..6 owner password validation");
			}
			if (std::memcmp(o_hash.data(), params_.o_bytes.data(), 32u) == 0) {
				std::array<uint8_t, 32> oe_key{};
				if (!compute_hash_r5_r6(params_.r, pwd_bytes, o_key_salt, u_first_48, oe_key)) {
					return make_crypto_error(
						ErrorCode::InternalError,
						DiagnosticId::InternalError,
						"CNG hash failed during R=5..6 /OE key derivation");
				}
				if (!cng_aes_cbc_raw(
						/*encrypt=*/false,
						oe_key,
						zero_iv,
						std::span<const uint8_t>(params_.oe_bytes.data(), 32u),
						unwrapped_key)) {
					return make_crypto_error(
						ErrorCode::InternalError,
						DiagnosticId::InternalError,
						"CNG AES-256-CBC unwrap failed for /OE");
				}
				matched_owner = true;
			}
		}

		if (!matched_user && !matched_owner) {
			return make_crypto_error(
				ErrorCode::WrongPassword,
				DiagnosticId::WrongPassword,
				"Supplied password did not authenticate against R=5..6 /U or /O");
		}

		// 3. Verify `/Perms` via AES-256-ECB (ISO 32000-2 section 7.6.4.4.9 Algorithm 13):
		std::array<uint8_t, 16> dec_perms{};
		if (!cng_aes256_ecb_decrypt(
				unwrapped_key,
				std::span<const uint8_t, 16>(params_.perms.data(), 16u),
				dec_perms)) {
			return make_crypto_error(
				ErrorCode::InternalError,
				DiagnosticId::InternalError,
				"CNG AES-256-ECB decryption failed while verifying /Perms");
		}

		if (dec_perms[9] != 'a' || dec_perms[10] != 'd' || dec_perms[11] != 'b') {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Encrypt /Perms verification failed: missing 'adb' magic bytes");
		}

		const uint32_t perms_p_u32 =
			static_cast<uint32_t>(dec_perms[0]) |
			(static_cast<uint32_t>(dec_perms[1]) << 8u) |
			(static_cast<uint32_t>(dec_perms[2]) << 16u) |
			(static_cast<uint32_t>(dec_perms[3]) << 24u);
		const int32_t perms_p = static_cast<int32_t>(perms_p_u32);
		if (perms_p != params_.p) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Encrypt /Perms permission bitmask does not match /P");
		}

		const bool perms_meta = (dec_perms[8] == 'T');
		if ((dec_perms[8] != 'T' && dec_perms[8] != 'F') ||
			perms_meta != params_.encrypt_metadata) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedDictionary,
				"/Encrypt /Perms EncryptMetadata flag does not match /EncryptMetadata");
		}

		file_key_.assign(unwrapped_key.begin(), unwrapped_key.end());
		authenticated_ = true;
		unlocked_as_owner_ = matched_owner;
		perms_verified_ = true;
		permissions_ = PdfPermissions::from_bitmask(params_.p, params_.r);
		decrypted_cache_.clear();
		decrypted_obj_stm_cache_.clear();
		return {};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while authenticating PDF password");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while authenticating PDF password");
	}
}

PdfResult<std::vector<uint8_t>> SecurityHandler::decrypt_bytes(
	std::span<const uint8_t> ciphertext,
	IndirectRef ref,
	CryptFilterMethod method) const noexcept
{
	try {
		if (!authenticated_) {
			return make_crypto_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot decrypt PDF object before SecurityHandler is authenticated",
				ref.obj_num,
				ref.gen_num);
		}
		if (method == CryptFilterMethod::None || ciphertext.empty()) {
			return std::vector<uint8_t>(ciphertext.begin(), ciphertext.end());
		}

		if (method == CryptFilterMethod::V2 || method == CryptFilterMethod::AESV2) {
			// ISO 32000-1 section 7.6.2 Algorithm 1: Derive per-object key from
			// `file_key_ || obj_num(3 LE) || gen_num(2 LE) [|| "sAlT" for AESV2]`.
			const std::array<uint8_t, 5> obj_gen_le = {
				static_cast<uint8_t>(ref.obj_num & 0xFFu),
				static_cast<uint8_t>((ref.obj_num >> 8u) & 0xFFu),
				static_cast<uint8_t>((ref.obj_num >> 16u) & 0xFFu),
				static_cast<uint8_t>(ref.gen_num & 0xFFu),
				static_cast<uint8_t>((ref.gen_num >> 8u) & 0xFFu),
			};
			const std::array<uint8_t, 4> aes_salt = {'s', 'A', 'l', 'T'};
			const std::span<const uint8_t> salt_span =
				(method == CryptFilterMethod::AESV2)
					? std::span<const uint8_t>(aes_salt)
					: std::span<const uint8_t>{};

			std::array<uint8_t, 16> obj_digest{};
			if (!cng_md5({file_key_, obj_gen_le, salt_span}, obj_digest)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG MD5 per-object key derivation failed",
					ref.obj_num,
					ref.gen_num);
			}
			const size_t obj_key_len = std::min<size_t>(file_key_.size() + 5u, 16u);
			const std::span<const uint8_t> obj_key(obj_digest.data(), obj_key_len);

			if (method == CryptFilterMethod::V2) {
				std::vector<uint8_t> out(ciphertext.begin(), ciphertext.end());
				if (!cng_rc4(obj_key, out)) {
					return make_crypto_error(
						ErrorCode::InternalError,
						DiagnosticId::InternalError,
						"CNG RC4 per-object decryption failed",
						ref.obj_num,
						ref.gen_num);
				}
				return out;
			}

			// AES-128-CBC (`/AESV2`): requires 16-byte IV prefix + >=1 block of PKCS#7 padded ciphertext.
			if (ciphertext.size() < 32u || (ciphertext.size() % 16u) != 0u) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"AES-128 ciphertext length must be >= 32 and a multiple of 16 bytes",
					ref.obj_num,
					ref.gen_num);
			}
			const std::span<const uint8_t, 16> iv(ciphertext.data(), 16u);
			const std::span<const uint8_t> body = ciphertext.subspan(16u);
			std::vector<uint8_t> out(body.size());
			if (!cng_aes_cbc_raw(/*encrypt=*/false, obj_key, iv, body, out)) {
				return make_crypto_error(
					ErrorCode::InternalError,
					DiagnosticId::InternalError,
					"CNG AES-128-CBC per-object decryption failed",
					ref.obj_num,
					ref.gen_num);
			}
			const uint8_t pad = out.back();
			if (pad == 0u || pad > 16u || pad > out.size()) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Invalid PKCS#7 padding byte in AES-128 decrypted PDF object",
					ref.obj_num,
					ref.gen_num);
			}
			for (size_t i = out.size() - pad; i < out.size(); ++i) {
				if (out[i] != pad) {
					return make_crypto_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Corrupt PKCS#7 padding bytes in AES-128 decrypted PDF object",
						ref.obj_num,
						ref.gen_num);
				}
			}
			out.resize(out.size() - pad);
			return out;
		}

		// AES-256-CBC (`/AESV3`, `R=5..6`): uses the 32-byte `file_key_` directly.
		// Requires 16-byte IV prefix + >=1 block of PKCS#7 padded ciphertext (>= 32 bytes).
		if (file_key_.size() != 32u) {
			return make_crypto_error(
				ErrorCode::InternalError,
				DiagnosticId::InternalError,
				"AES-256 file encryption key is not 32 bytes",
				ref.obj_num,
				ref.gen_num);
		}
		if (ciphertext.size() < 32u || (ciphertext.size() % 16u) != 0u) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"AES-256 ciphertext length must be >= 32 and a multiple of 16 bytes",
				ref.obj_num,
				ref.gen_num);
		}
		const std::span<const uint8_t, 16> iv(ciphertext.data(), 16u);
		const std::span<const uint8_t> body = ciphertext.subspan(16u);
		std::vector<uint8_t> out(body.size());
		if (!cng_aes_cbc_raw(/*encrypt=*/false, file_key_, iv, body, out)) {
			return make_crypto_error(
				ErrorCode::InternalError,
				DiagnosticId::InternalError,
				"CNG AES-256-CBC per-object decryption failed",
				ref.obj_num,
				ref.gen_num);
		}
		const uint8_t pad = out.back();
		if (pad == 0u || pad > 16u || pad > out.size()) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Invalid PKCS#7 padding byte in AES-256 decrypted PDF object",
				ref.obj_num,
				ref.gen_num);
		}
		for (size_t i = out.size() - pad; i < out.size(); ++i) {
			if (out[i] != pad) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Corrupt PKCS#7 padding bytes in AES-256 decrypted PDF object",
					ref.obj_num,
					ref.gen_num);
			}
		}
		out.resize(out.size() - pad);
		return out;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decrypting PDF object bytes",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decrypting PDF object bytes",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}
}

PdfResult<void> SecurityHandler::decrypt_value_recursive(
	PdfValue& value,
	IndirectRef ref,
	size_t depth) const
{
	if (depth > kMaxObjectCloneDepth) {
		return make_crypto_error(
			ErrorCode::RecursionLimitExceeded,
			DiagnosticId::RecursionLimitExceeded,
			"Object nesting depth exceeded while decrypting PDF object",
			ref.obj_num,
			ref.gen_num);
	}

	if (PdfString* str = value.as_string_mut()) {
		if (params_.string_method != CryptFilterMethod::None && !str->bytes.empty()) {
			RECTO_PDF_TRY(
				std::vector<uint8_t> plain,
				decrypt_bytes(str->bytes, ref, params_.string_method));
			str->bytes = std::move(plain);
		}
		return {};
	}

	if (PdfArray* arr = value.as_array_mut()) {
		for (PdfValue& elem : *arr) {
			RECTO_PDF_RETURN_IF_ERROR(decrypt_value_recursive(elem, ref, depth + 1u));
		}
		return {};
	}

	if (PdfDict* dict = value.as_dict_mut()) {
		// Check whether this dictionary is a Digital Signature dictionary (`/Type /Sig`,
		// `/Type /DocTimeStamp`, or contains `/FT /Sig` or `/ByteRange` + `/Filter` + `/Contents`).
		// Per ISO 32000-1 section 7.6.2, the `/Contents` hex string of a signature
		// dictionary is never encrypted.
		const auto type_name = dict->get_name("Type");
		const bool is_sig_dict =
			(type_name.has_value() && (*type_name == "Sig" || *type_name == "DocTimeStamp")) ||
			(dict->contains("ByteRange") && dict->contains("Contents") &&
			 (dict->contains("Filter") || dict->contains("SubFilter")));

		for (size_t i = 0; i < dict->size(); ++i) {
			const std::string_view key = dict->entries()[i].first.view();
			if (is_sig_dict && key == "Contents") {
				continue;
			}
			PdfValue* entry_val = dict->get_mut(key);
			if (entry_val != nullptr) {
				RECTO_PDF_RETURN_IF_ERROR(decrypt_value_recursive(*entry_val, ref, depth + 1u));
			}
		}
		return {};
	}

	if (PdfStream* stm = value.as_stream_mut()) {
		const auto stm_type = stm->dict.get_name("Type");
		// ISO 32000-1 section 7.5.8.1: Cross-reference streams (`/Type /XRef`) are never encrypted.
		if (stm_type.has_value() && *stm_type == "XRef") {
			return {};
		}

		// Decrypt strings inside the stream dictionary first.
		for (size_t i = 0; i < stm->dict.size(); ++i) {
			const std::string_view key = stm->dict.entries()[i].first.view();
			PdfValue* entry_val = stm->dict.get_mut(key);
			if (entry_val != nullptr) {
				RECTO_PDF_RETURN_IF_ERROR(decrypt_value_recursive(*entry_val, ref, depth + 1u));
			}
		}

		// ISO 32000-1 section 7.6.3.2 / 7.6.5: `/Type /Metadata` streams are not encrypted
		// when `/EncryptMetadata` is false.
		if (!params_.encrypt_metadata && stm_type.has_value() && *stm_type == "Metadata") {
			return {};
		}

		// Determine effective stream crypt filter method (honoring per-stream `/Crypt` filter
		// in `/Filter` and `/DecodeParms`, or `/EmbeddedFile` `/EFF`).
		CryptFilterMethod eff_method = params_.stream_method;
		if (stm_type.has_value() && *stm_type == "EmbeddedFile" && !params_.eff_f.empty()) {
			eff_method = params_.embedded_file_method;
		}

		// Check if the stream's first `/Filter` is `/Crypt` (ISO 32000-1 section 7.4.10).
		bool strip_leading_crypt_filter = false;
		if (const PdfValue* filter_val = stm->dict.get("Filter")) {
			if (const auto single_f = filter_val->as_name()) {
				if (*single_f == "Crypt") {
					std::string cf_name = "Identity";
					if (const PdfDict* dp = stm->dict.get_dict("DecodeParms")) {
						cf_name = std::string(dp->get_name("Name").value_or("Identity"));
					}
					const auto it = params_.crypt_filters.find(cf_name);
					if (it == params_.crypt_filters.end()) {
						return make_crypto_error(
							ErrorCode::UnsupportedEncryption,
							DiagnosticId::UnsupportedEncryption,
							"Unknown /Crypt filter /Name in stream /DecodeParms",
							ref.obj_num,
							ref.gen_num);
					}
					eff_method = it->second;
					strip_leading_crypt_filter = true;
				}
			} else if (const PdfArray* f_arr = filter_val->as_array()) {
				if (!f_arr->empty()) {
					if (const auto first_f = (*f_arr)[0].as_name(); first_f.has_value() && *first_f == "Crypt") {
						std::string cf_name = "Identity";
						if (const PdfArray* dp_arr = stm->dict.get_array("DecodeParms")) {
							if (!dp_arr->empty()) {
								if (const PdfDict* dp0 = (*dp_arr)[0].as_dict()) {
									cf_name = std::string(dp0->get_name("Name").value_or("Identity"));
								}
							}
						} else if (const PdfDict* dp = stm->dict.get_dict("DecodeParms")) {
							cf_name = std::string(dp->get_name("Name").value_or("Identity"));
						}
						const auto it = params_.crypt_filters.find(cf_name);
						if (it == params_.crypt_filters.end()) {
							return make_crypto_error(
								ErrorCode::UnsupportedEncryption,
								DiagnosticId::UnsupportedEncryption,
								"Unknown /Crypt filter /Name in stream /DecodeParms array",
								ref.obj_num,
								ref.gen_num);
						}
						eff_method = it->second;
						strip_leading_crypt_filter = true;
					}
				}
			}
		}

		if (eff_method != CryptFilterMethod::None && !stm->raw_bytes.empty()) {
			RECTO_PDF_TRY(
				std::vector<uint8_t> plain,
				decrypt_bytes(stm->raw_bytes, ref, eff_method));
			stm->raw_bytes = std::move(plain);
			stm->raw_length = stm->raw_bytes.size();
		}

		if (strip_leading_crypt_filter) {
			if (stm->dict.get_name("Filter").has_value()) {
				stm->dict.erase("Filter");
				stm->dict.erase("DecodeParms");
			} else if (PdfArray* f_arr = stm->dict.get_array_mut("Filter")) {
				f_arr->erase(f_arr->begin());
				if (PdfArray* dp_arr = stm->dict.get_array_mut("DecodeParms")) {
					if (!dp_arr->empty()) {
						dp_arr->erase(dp_arr->begin());
					}
				}
			}
		}

		return {};
	}

	return {};
}

PdfResult<void> SecurityHandler::decrypt_object_values(
	PdfValue& value,
	IndirectRef ref,
	bool is_inside_obj_stm) const noexcept
{
	try {
		if (!authenticated_) {
			return make_crypto_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot decrypt PDF object before SecurityHandler is authenticated",
				ref.obj_num,
				ref.gen_num);
		}
		// Exemptions (ISO 32000-1 section 7.6.2):
		// 1. The `/Encrypt` dictionary itself is never decrypted.
		// 2. Objects stored inside an `/ObjStm` are never individually decrypted
		//    (the enclosing `/ObjStm` stream was already decrypted as a whole).
		if (is_inside_obj_stm ||
			(encrypt_ref_.has_value() && *encrypt_ref_ == ref)) {
			return {};
		}
		return decrypt_value_recursive(value, ref, 0u);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decrypting PDF object values",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decrypting PDF object values",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}
}

PdfResult<PdfValue> SecurityHandler::resolve_and_decrypt_object(
	const XrefTable& xref,
	std::span<const uint8_t> file_bytes,
	IndirectRef ref) noexcept
{
	try {
		if (!authenticated_) {
			return make_crypto_error(
				ErrorCode::NeedsPassword,
				DiagnosticId::NeedsPassword,
				"Cannot resolve and decrypt PDF object before SecurityHandler is authenticated",
				ref.obj_num,
				ref.gen_num);
		}
		std::vector<IndirectRef> visited_refs;
		visited_refs.reserve(kMaxIndirectRefDepth);
		return resolve_and_decrypt_with_visited(xref, file_bytes, ref, visited_refs);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while resolving and decrypting indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while resolving and decrypting indirect object",
			std::nullopt,
			ref.obj_num,
			ref.gen_num);
	}
}

PdfResult<PdfValue> SecurityHandler::resolve_and_decrypt_with_visited(
	const XrefTable& xref,
	std::span<const uint8_t> file_bytes,
	IndirectRef ref,
	std::vector<IndirectRef>& visited_refs)
{
	struct VisitedScopeGuard {
		std::vector<IndirectRef>& vec;
		size_t saved_size;
		~VisitedScopeGuard() noexcept
		{
			vec.resize(saved_size);
		}
	} guard{visited_refs, visited_refs.size()};

	IndirectRef current = ref;
	while (true) {
		if (std::find(visited_refs.begin(), visited_refs.end(), current) != visited_refs.end()) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Cyclic indirect object reference chain detected",
				current.obj_num,
				current.gen_num);
		}
		if (visited_refs.size() >= kMaxIndirectRefDepth) {
			return make_crypto_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedObject,
				"Indirect object reference chain exceeds maximum depth of 32",
				current.obj_num,
				current.gen_num);
		}
		visited_refs.push_back(current);

		const uint64_t cache_key = pack_obj_key(current);
		if (const auto c_it = decrypted_cache_.find(cache_key); c_it != decrypted_cache_.end()) {
			return c_it->second.deep_clone();
		}

		const XrefEntry* entry = xref.find(current.obj_num);
		if (entry == nullptr || entry->is_free()) {
			return PdfValue(PdfNull{});
		}

		if (entry->is_compressed()) {
			if (current.gen_num != 0u) {
				return PdfValue(PdfNull{});
			}
			const uint32_t stm_obj_num = entry->obj_stm_num;
			const uint32_t stm_index = entry->obj_stm_index;
			const IndirectRef stm_ref{stm_obj_num, 0u};
			if (std::find(visited_refs.begin(), visited_refs.end(), stm_ref) != visited_refs.end()) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Cyclic /Type /ObjStm reference detected",
					current.obj_num,
					current.gen_num);
			}

			auto stm_cache_it = decrypted_obj_stm_cache_.find(stm_obj_num);
			if (stm_cache_it == decrypted_obj_stm_cache_.end()) {
				const XrefEntry* stm_entry = xref.find(stm_obj_num);
				if (stm_entry == nullptr || !stm_entry->is_uncompressed() || stm_entry->generation != 0u) {
					return make_crypto_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Target /Type /ObjStm object is missing, free, or nested",
						stm_obj_num,
						0u);
				}
				RECTO_PDF_TRY(
					IndirectObject raw_stm_obj,
					xref.read_indirect_object(file_bytes, stm_ref));
				RECTO_PDF_RETURN_IF_ERROR(
					decrypt_object_values(raw_stm_obj.value, stm_ref, /*is_inside_obj_stm=*/false));
				const PdfStream* obj_stm = raw_stm_obj.value.as_stream();
				if (obj_stm == nullptr ||
					obj_stm->dict.get_name("Type").value_or("") != "ObjStm") {
					return make_crypto_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Compressed object target is not a /Type /ObjStm stream",
						stm_obj_num,
						0u);
				}
				const auto n_opt = obj_stm->dict.get_int("N");
				const auto first_opt = obj_stm->dict.get_int("First");
				if (!n_opt.has_value() || *n_opt <= 0 || *n_opt > kMaxObjStmEntries ||
					!first_opt.has_value() || *first_opt < 0) {
					return make_crypto_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"Invalid /N or /First in decrypted /Type /ObjStm stream",
						stm_obj_num,
						0u);
				}
				RECTO_PDF_TRY(
					std::vector<uint8_t> decoded_stm,
					decode_stream(*obj_stm));
				const uint64_t first_off = static_cast<uint64_t>(*first_opt);
				if (first_off > decoded_stm.size()) {
					return make_crypto_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedObject,
						"/Type /ObjStm /First offset exceeds decoded stream length",
						stm_obj_num,
						0u);
				}

				const uint32_t n_count = static_cast<uint32_t>(*n_opt);
				CachedObjStream cached;
				cached.first_offset = first_off;
				cached.entries.reserve(n_count);
				const std::span<const uint8_t> header_bytes(
					decoded_stm.data(),
					static_cast<size_t>(first_off));
				Lexer hdr_lexer(header_bytes);
				for (uint32_t i = 0; i < n_count; ++i) {
					RECTO_PDF_TRY(const Token num_tok, hdr_lexer.next_token());
					RECTO_PDF_TRY(const Token off_tok, hdr_lexer.next_token());
					if (num_tok.kind != TokenKind::Integer || num_tok.int_value <= 0 ||
						off_tok.kind != TokenKind::Integer || off_tok.int_value < 0) {
						return make_crypto_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedObject,
							"Malformed header pair in decrypted /Type /ObjStm stream",
							stm_obj_num,
							0u);
					}
					const uint64_t rel_off = static_cast<uint64_t>(off_tok.int_value);
					if (rel_off > decoded_stm.size() - first_off ||
						(!cached.entries.empty() && rel_off < cached.entries.back().rel_offset)) {
						return make_crypto_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedObject,
							"Out-of-bounds relative offset in decrypted /Type /ObjStm stream",
							stm_obj_num,
							0u);
					}
					cached.entries.push_back(CachedObjStream::HeaderPair{
						static_cast<uint32_t>(num_tok.int_value),
						rel_off});
				}
				cached.decoded_bytes = std::move(decoded_stm);
				stm_cache_it = decrypted_obj_stm_cache_.emplace(stm_obj_num, std::move(cached)).first;
			}

			const CachedObjStream& cached_stm = stm_cache_it->second;
			if (stm_index >= cached_stm.entries.size() ||
				cached_stm.entries[stm_index].obj_num != current.obj_num) {
				return make_crypto_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedObject,
					"Compressed object index or number mismatch in decrypted /Type /ObjStm",
					current.obj_num,
					current.gen_num);
			}
			const size_t obj_start = static_cast<size_t>(
				cached_stm.first_offset + cached_stm.entries[stm_index].rel_offset);
			const size_t obj_end = (stm_index + 1u < cached_stm.entries.size())
				? static_cast<size_t>(
					  cached_stm.first_offset + cached_stm.entries[stm_index + 1u].rel_offset)
				: cached_stm.decoded_bytes.size();
			Parser inner_parser(std::span<const uint8_t>(
				cached_stm.decoded_bytes.data() + obj_start,
				obj_end - obj_start));
			RECTO_PDF_TRY(PdfValue inner_val, inner_parser.parse_direct_object());
			// Inner objects of an `/ObjStm` are NOT decrypted again (`is_inside_obj_stm = true`).
			RECTO_PDF_RETURN_IF_ERROR(
				decrypt_object_values(inner_val, current, /*is_inside_obj_stm=*/true));
			if (const auto next_ref = inner_val.as_ref()) {
				current = *next_ref;
				continue;
			}
			RECTO_PDF_TRY(PdfValue cloned, inner_val.deep_clone());
			decrypted_cache_.emplace(cache_key, std::move(cloned));
			return std::move(inner_val);
		}

		// Uncompressed indirect object:
		RECTO_PDF_TRY(
			IndirectObject raw_obj,
			xref.read_indirect_object(file_bytes, current));
		if (const auto next_ref = raw_obj.value.as_ref()) {
			current = *next_ref;
			continue;
		}
		RECTO_PDF_RETURN_IF_ERROR(
			decrypt_object_values(raw_obj.value, current, /*is_inside_obj_stm=*/false));
		RECTO_PDF_TRY(PdfValue cloned, raw_obj.value.deep_clone());
		decrypted_cache_.emplace(cache_key, std::move(cloned));
		return std::move(raw_obj.value);
	}
}

} // namespace recto::pdf
