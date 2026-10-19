#pragma once

// Standard Security Handler decryption (`RC4`, `AES-128`, `AES-256` `R=2..6`)
// via Windows Next Generation Cryptography (`bcrypt.dll`) for ISO 32000-1:2008
// section 7.6 and ISO 32000-2:2020 section 7.6.4 (D01 T01 section 9).
//
// Freeze contract (foundation plan section 4.2):
// - `bcrypt.dll` algorithm providers are opened safely without mutable globals.
// - `SecurityHandler` state is owned per `pdf::Document` on its actor thread.
// - Decrypted indirect objects are cached per `SecurityHandler` / `pdf::Document`
//   so decryption runs at most once per indirect object.

#include "error.h"
#include "object.h"
#include "xref.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace recto::pdf {

// Crypt filter method (`/CFM`) for streams (`/StmF`) and strings (`/StrF`).
enum class CryptFilterMethod : uint8_t {
	None = 0, // `/None` or `/Identity`: unencrypted pass-through
	V2 = 1,   // `/V2`: RC4 (40..128-bit)
	AESV2 = 2, // `/AESV2`: AES-128-CBC with 16-byte random IV prefix and PKCS#7 padding
	AESV3 = 3, // `/AESV3`: AES-256-CBC with 16-byte random IV prefix and PKCS#7 padding
};

// User access permissions decoded from the signed 32-bit `/P` bitmask
// (ISO 32000-1:2008 Table 22 / ISO 32000-2:2020 Table 22, 1-indexed bit positions).
struct PdfPermissions {
	int32_t raw_p{-1};
	bool can_print{true};                     // Bit 3 (0x004)
	bool can_modify{true};                    // Bit 4 (0x008)
	bool can_copy_text{true};                 // Bit 5 (0x010)
	bool can_annotate{true};                  // Bit 6 (0x020)
	bool can_fill_forms{true};                // Bit 9 (0x100) for R>=3, else can_annotate
	bool can_extract_for_accessibility{true}; // Bit 10 (0x200) for R>=3 (always true in PDF 2.0 / R>=5 if desired, or bit 10 per Table 22)
	bool can_assemble{true};                  // Bit 11 (0x400) for R>=3, else can_modify
	bool can_print_high_quality{true};        // Bit 12 (0x800) for R>=3, else can_print

	[[nodiscard]] static constexpr PdfPermissions from_bitmask(
		int32_t p,
		int32_t revision = 3) noexcept
	{
		const uint32_t u = static_cast<uint32_t>(p);
		PdfPermissions perms{};
		perms.raw_p = p;
		perms.can_print = (u & (1u << 2)) != 0u;      // Bit 3
		perms.can_modify = (u & (1u << 3)) != 0u;     // Bit 4
		perms.can_copy_text = (u & (1u << 4)) != 0u;  // Bit 5
		perms.can_annotate = (u & (1u << 5)) != 0u;   // Bit 6
		if (revision >= 3) {
			perms.can_fill_forms = (u & (1u << 8)) != 0u;                // Bit 9
			perms.can_extract_for_accessibility = (u & (1u << 9)) != 0u; // Bit 10
			perms.can_assemble = (u & (1u << 10)) != 0u;                 // Bit 11
			perms.can_print_high_quality = (u & (1u << 11)) != 0u;       // Bit 12
		} else {
			perms.can_fill_forms = perms.can_annotate;
			perms.can_extract_for_accessibility = perms.can_copy_text;
			perms.can_assemble = perms.can_modify;
			perms.can_print_high_quality = perms.can_print;
		}
		return perms;
	}

	constexpr bool operator==(const PdfPermissions&) const noexcept = default;
};

// Parsed `/Encrypt` dictionary parameters for `/Filter /Standard`.
struct EncryptDictParams {
	int32_t v{0};                  // `/V` algorithm code (`1, 2, 4, 5`)
	int32_t r{0};                  // `/R` Standard handler revision (`2..6`)
	int32_t key_length_bits{40};   // `/Length` in bits (`40..128` multiple of 8, or `256` for `V=5`)
	int32_t p{0};                  // `/P` signed 32-bit permissions bitmask
	bool encrypt_metadata{true};   // `/EncryptMetadata` (default `true`)
	std::vector<uint8_t> o_bytes;  // `/O` (`32` bytes for `R=2..4`, `>=48` bytes for `R=5..6`)
	std::vector<uint8_t> u_bytes;  // `/U` (`32` bytes for `R=2..4`, `>=48` bytes for `R=5..6`)
	std::vector<uint8_t> oe_bytes; // `/OE` (`32` bytes for `R=5..6`)
	std::vector<uint8_t> ue_bytes; // `/UE` (`32` bytes for `R=5..6`)
	std::vector<uint8_t> perms;    // `/Perms` (`16` bytes for `R=5..6`)
	std::string stm_f{"Identity"}; // `/StmF` stream crypt filter name (`V=4, 5`)
	std::string str_f{"Identity"}; // `/StrF` string crypt filter name (`V=4, 5`)
	std::string eff_f{};           // `/EFF` embedded file crypt filter name (optional)
	CryptFilterMethod stream_method{CryptFilterMethod::None};
	CryptFilterMethod string_method{CryptFilterMethod::None};
	CryptFilterMethod embedded_file_method{CryptFilterMethod::None};
	std::unordered_map<std::string, CryptFilterMethod> crypt_filters{};
};

namespace detail {

void set_crypto_alloc_fail_countdown(int countdown) noexcept;
void check_crypto_alloc();

struct CryptoConstructAllocGuard {
	CryptoConstructAllocGuard()
	{
		check_crypto_alloc();
	}
};

class ScopedCryptoAllocFaultInjector {
public:
	explicit ScopedCryptoAllocFaultInjector(int fail_after_n_allocations) noexcept
	{
		set_crypto_alloc_fail_countdown(fail_after_n_allocations);
	}

	ScopedCryptoAllocFaultInjector(const ScopedCryptoAllocFaultInjector&) = delete;
	ScopedCryptoAllocFaultInjector& operator=(const ScopedCryptoAllocFaultInjector&) = delete;

	~ScopedCryptoAllocFaultInjector() noexcept
	{
		set_crypto_alloc_fail_countdown(-1);
	}
};

} // namespace detail

class SecurityHandler {
public:
	SecurityHandler() = default;

	// Prepares and validates a UTF-8 password for `R=5..6` according to RFC 4013
	// (SASLprep) and RFC 3454 (Stringprep) Tables B.1, C.1.2..C.9, D.1, D.2, and
	// ISO 32000-2 section 7.6.4.3.4 (NFKC normalization + 127-byte truncation).
	// Returns `ErrorCode::WrongPassword` when the password contains invalid UTF-8,
	// prohibited output characters, or bidirectional violations.
	[[nodiscard]] static PdfResult<std::vector<uint8_t>> prepare_saslprep_password(
		std::string_view password) noexcept;

	// Parses a `/Filter /Standard` `/Encrypt` dictionary (`encrypt_dict`) paired
	// with the first element of the trailer `/ID` array (`file_id_0`, required
	// for `R=2..4`) and optional indirect object number of the `/Encrypt`
	// dictionary (`encrypt_ref`). Automatically attempts empty user password
	// (`""`) authentication so owner-restricted PDFs unlock immediately without
	// prompting.
	[[nodiscard]] static PdfResult<SecurityHandler> create(
		const PdfDict& encrypt_dict,
		std::span<const uint8_t> file_id_0 = {},
		std::optional<IndirectRef> encrypt_ref = std::nullopt) noexcept;

	// Convenience helper that inspects `xref.trailer()` for `/Encrypt` and `/ID`:
	// - If `/Encrypt` is absent or null, returns `std::nullopt`.
	// - If `/Encrypt` is present, resolves it (when indirect) and constructs a
	//   `SecurityHandler` (attempting empty user password auto-unlock).
	[[nodiscard]] static PdfResult<std::optional<SecurityHandler>> from_xref(
		const XrefTable& xref,
		std::span<const uint8_t> file_bytes) noexcept;

	// Parses and validates `/Encrypt` dictionary fields without authenticating.
	[[nodiscard]] static PdfResult<EncryptDictParams> parse_encrypt_dict(
		const PdfDict& encrypt_dict) noexcept;

	// Authenticates `password` against `/U` (user password) or `/O` (owner
	// password) for `R=2..6`, unwrapping the file encryption key and verifying
	// `/Perms` (`R=5..6`). Returns `ErrorCode::WrongPassword` when neither user
	// nor owner password matches.
	[[nodiscard]] PdfResult<void> authenticate(std::string_view password) noexcept;

	[[nodiscard]] bool is_authenticated() const noexcept
	{
		return authenticated_;
	}

	[[nodiscard]] bool unlocked_as_owner() const noexcept
	{
		return unlocked_as_owner_;
	}

	[[nodiscard]] bool perms_verified() const noexcept
	{
		return perms_verified_;
	}

	[[nodiscard]] const EncryptDictParams& params() const noexcept
	{
		return params_;
	}

	[[nodiscard]] const PdfPermissions& permissions() const noexcept
	{
		return permissions_;
	}

	[[nodiscard]] std::span<const uint8_t> file_encryption_key() const noexcept
	{
		return file_key_;
	}

	[[nodiscard]] std::optional<IndirectRef> encrypt_ref() const noexcept
	{
		return encrypt_ref_;
	}

	[[nodiscard]] size_t cached_object_count() const noexcept
	{
		return decrypted_cache_.size();
	}

	void clear_decrypted_cache() noexcept
	{
		decrypted_cache_.clear();
	}

	// Decrypts a raw byte buffer (e.g. a `PdfString` or `PdfStream` payload)
	// belonging to indirect object `ref` using `method`.
	[[nodiscard]] PdfResult<std::vector<uint8_t>> decrypt_bytes(
		std::span<const uint8_t> ciphertext,
		IndirectRef ref,
		CryptFilterMethod method) const noexcept;

	// Recursively decrypts all `PdfString` and `PdfStream` values inside `value`
	// in place for indirect object `ref`.
	// Exemptions (ISO 32000-1 section 7.6.2):
	// - When `ref == encrypt_ref()` or `is_inside_obj_stm == true`, `value` is
	//   left untouched (objects inside an `/ObjStm` are already decrypted when
	//   the `/ObjStm` stream itself is decrypted).
	// - Strings under `/Sig` or `/DocTimeStamp` `/Contents` (digital signature
	//   hex byte ranges) are never decrypted.
	// - `/Type /XRef` cross-reference streams are never encrypted per ISO 32000-1
	//   section 7.5.8.1.
	// - `/Type /Metadata` streams are skipped when `/EncryptMetadata` is `false`,
	//   or when their `/Filter` array begins with `/Crypt` (`/Name /Identity`).
	[[nodiscard]] PdfResult<void> decrypt_object_values(
		PdfValue& value,
		IndirectRef ref,
		bool is_inside_obj_stm = false) const noexcept;

	// Resolves indirect object `ref` from `xref` and `file_bytes`, decrypts its
	// strings and stream payloads (and decompresses `/Type /ObjStm` containers
	// after decrypting the container stream), and caches the decrypted
	// `PdfValue` inside this `SecurityHandler` so decryption runs once per object.
	[[nodiscard]] PdfResult<PdfValue> resolve_and_decrypt_object(
		const XrefTable& xref,
		std::span<const uint8_t> file_bytes,
		IndirectRef ref) noexcept;

private:
	[[nodiscard]] PdfResult<void> decrypt_value_recursive(
		PdfValue& value,
		IndirectRef ref,
		size_t depth) const;

	[[nodiscard]] PdfResult<PdfValue> resolve_and_decrypt_with_visited(
		const XrefTable& xref,
		std::span<const uint8_t> file_bytes,
		IndirectRef ref,
		std::vector<IndirectRef>& visited_refs);

	detail::CryptoConstructAllocGuard alloc_guard_{};
	EncryptDictParams params_{};
	PdfPermissions permissions_{};
	std::vector<uint8_t> file_id_0_{};
	std::optional<IndirectRef> encrypt_ref_{std::nullopt};
	std::vector<uint8_t> file_key_{};
	bool authenticated_{false};
	bool unlocked_as_owner_{false};
	bool perms_verified_{false};
	std::unordered_map<uint64_t, PdfValue> decrypted_cache_{};
	std::unordered_map<uint32_t, CachedObjStream> decrypted_obj_stm_cache_{};
};

} // namespace recto::pdf
