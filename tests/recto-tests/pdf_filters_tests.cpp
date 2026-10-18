// Unit tests for lossless PDF stream filters: FlateDecode, LZWDecode,
// TIFF Predictor 2 and PNG Predictors 10..15, ASCIIHexDecode, ASCII85Decode,
// RunLengthDecode, chained filter execution, and the 256 MB decompression cap
// (D01 T01 section 7).
#include "recto_pdf.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace recto::pdf {
namespace {

constexpr std::string_view kKnownFilterPlaintext =
	"Recto fuzz_filters deterministic plaintext vector 0123456789ABCD";

[[nodiscard]] std::span<const uint8_t> as_bytes(std::string_view sv) noexcept
{
	return {
		reinterpret_cast<const uint8_t*>(sv.data()),
		sv.size()};
}

[[nodiscard]] std::string_view as_string_view(std::span<const uint8_t> bytes) noexcept
{
	return {
		reinterpret_cast<const char*>(bytes.data()),
		bytes.size()};
}

// Helper to pack variable-width MSB-first LZW codes into a byte vector.
[[nodiscard]] std::vector<uint8_t> pack_lzw_codes(
	std::span<const std::pair<uint32_t, uint32_t>> code_and_bits)
{
	std::vector<uint8_t> out;
	uint64_t bit_buf = 0;
	uint32_t bit_cnt = 0;
	for (const auto& [code, bits] : code_and_bits) {
		bit_buf = (bit_buf << bits) | (code & ((1ULL << bits) - 1ULL));
		bit_cnt += bits;
		while (bit_cnt >= 8u) {
			bit_cnt -= 8u;
			out.push_back(static_cast<uint8_t>((bit_buf >> bit_cnt) & 0xFFu));
		}
	}
	if (bit_cnt > 0u) {
		out.push_back(static_cast<uint8_t>((bit_buf << (8u - bit_cnt)) & 0xFFu));
	}
	return out;
}

TEST(PdfFilters, FlateDecode)
{
	// 1. Seed fixture seeds/seed-filter-00-flate.bin (selector 0x00 + zlib payload).
	{
		const auto seed_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-00-flate.bin"));
		ASSERT_GT(seed_bin.size(), 2u);
		ASSERT_EQ(seed_bin[0], 0x00u);
		const std::span<const uint8_t> zlib_payload(seed_bin.data() + 1, seed_bin.size() - 1);

		auto dec_zlib = decode_flate(zlib_payload);
		RECTO_ASSERT_OK(dec_zlib);
		EXPECT_EQ(as_string_view(*dec_zlib), kKnownFilterPlaintext);

		// 2. Raw-deflate fallback: strip the 2-byte RFC 1950 zlib header and 4-byte Adler-32 trailer.
		ASSERT_GT(zlib_payload.size(), 6u);
		const std::span<const uint8_t> raw_deflate =
			zlib_payload.subspan(2, zlib_payload.size() - 6);
		auto dec_raw = decode_flate(raw_deflate);
		RECTO_ASSERT_OK(dec_raw);
		EXPECT_EQ(as_string_view(*dec_raw), kKnownFilterPlaintext);

		// 3. Corrupted 2-byte zlib header that fails modulo-31 checksum triggers automatic fallback.
		std::vector<uint8_t> bad_hdr(zlib_payload.begin(), zlib_payload.end());
		bad_hdr[0] = 0x78u;
		bad_hdr[1] = 0x00u; // (0x78 * 256 + 0) % 31 != 0
		auto dec_bad_hdr = decode_flate(bad_hdr);
		RECTO_ASSERT_OK(dec_bad_hdr);
		EXPECT_EQ(as_string_view(*dec_bad_hdr), kKnownFilterPlaintext);

		// 4. Valid 2-byte zlib header with corrupted Adler-32 trailer fails with DamagedFormat.
		std::vector<uint8_t> bad_adler(zlib_payload.begin(), zlib_payload.end());
		bad_adler.back() ^= 0xFFu;
		RECTO_EXPECT_ERR(decode_flate(bad_adler), ErrorCode::DamagedFormat);
	}

	// 5. Stored uncompressed deflate block (BFINAL=1, BTYPE=00): header byte 0x01, LEN=5, NLEN=0xFFFA, "Hello".
	{
		const std::array<uint8_t, 10> stored_raw = {
			0x01u, 0x05u, 0x00u, 0xFAu, 0xFFu, 'H', 'e', 'l', 'l', 'o'};
		auto dec_stored = decode_flate(stored_raw);
		RECTO_ASSERT_OK(dec_stored);
		EXPECT_EQ(as_string_view(*dec_stored), "Hello");

		// Mismatched LEN/NLEN complement fails with DamagedFormat.
		std::array<uint8_t, 10> bad_stored = stored_raw;
		bad_stored[3] = 0x00u;
		RECTO_EXPECT_ERR(decode_flate(bad_stored), ErrorCode::DamagedFormat);
	}

	// 6. Decode render/simple-1p.pdf page content stream 4 0 R.
	{
		const auto pdf_bytes = test::read_file_bytes(
			test::corpus_path("render/simple-1p.pdf"));
		auto xref_res = XrefTable::parse(pdf_bytes);
		RECTO_ASSERT_OK(xref_res);
		auto stm_obj = xref_res->resolve_object(pdf_bytes, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(stm_obj);
		ASSERT_TRUE(stm_obj->is_stream());

		auto decoded = decode_stream(*stm_obj->as_stream());
		RECTO_ASSERT_OK(decoded);
		EXPECT_NE(
			as_string_view(*decoded).find("Recto Native PDF Engine - Simple 1P"),
			std::string_view::npos);
	}

	// 7. Empty dynamic DEFLATE block (single 1-bit EOB symbol 256 in literal/length tree).
	{
		const std::array<uint8_t, 42> empty_dyn_block = {
			0x05u, 0xC0u, 0x01u, 0x04u, 0x00u, 0x00u, 0x00u, 0x00u,
			0x10u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
			0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
			0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
			0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
			0x80u, 0x00u};
		auto dec_empty_dyn = decode_flate(empty_dyn_block);
		RECTO_ASSERT_OK(dec_empty_dyn);
		EXPECT_TRUE(dec_empty_dyn->empty());
	}

	// 8. Small configurable max_decoded_bytes ceiling and corpus decompression bomb
	// tests/corpus/broken/deflate-bomb-10kb.pdf rejected with ErrorCode::DecompressionLimitExceeded
	// while keeping filter functions pure and free of global diagnostic ring side effects.
	{
		const auto seed_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-00-flate.bin"));
		const std::span<const uint8_t> zlib_payload(seed_bin.data() + 1, seed_bin.size() - 1);

		// Exact ceiling == kKnownFilterPlaintext.size() (63 bytes) succeeds; 62 bytes fails.
		auto exact_ok = decode_flate(zlib_payload, nullptr, kKnownFilterPlaintext.size());
		RECTO_ASSERT_OK(exact_ok);
		EXPECT_LE(exact_ok->capacity(), kKnownFilterPlaintext.size());
		RECTO_EXPECT_ERR(
			decode_flate(zlib_payload, nullptr, kKnownFilterPlaintext.size() - 1u),
			ErrorCode::DecompressionLimitExceeded);

		// Corrupt 2-byte header (0x78, 0x00) followed by stored 5-byte block "Hello"
		// with a 4-byte ceiling must preserve ErrorCode::DecompressionLimitExceeded.
		const std::array<uint8_t, 12> bad_hdr_stored = {
			0x78u, 0x00u, 0x01u, 0x05u, 0x00u, 0xFAu, 0xFFu, 'H', 'e', 'l', 'l', 'o'};
		RECTO_EXPECT_ERR(
			decode_flate(bad_hdr_stored, nullptr, 4u),
			ErrorCode::DecompressionLimitExceeded);

		clear_diagnostics();
		const auto bomb_pdf = test::read_file_bytes(
			test::corpus_path("broken/deflate-bomb-10kb.pdf"));
		ASSERT_FALSE(bomb_pdf.empty());

		auto xref_res = XrefTable::parse(bomb_pdf);
		RECTO_ASSERT_OK(xref_res);
		auto stm_obj = xref_res->resolve_object(bomb_pdf, IndirectRef{4u, 0u});
		RECTO_ASSERT_OK(stm_obj);
		ASSERT_TRUE(stm_obj->is_stream());

		clear_diagnostics();
		auto bomb_res = decode_stream(*stm_obj->as_stream());
		RECTO_EXPECT_ERR(bomb_res, ErrorCode::DecompressionLimitExceeded);
		// Freeze contract: stream filters are pure stateless functions that do not
		// mutate shared process-wide state.
		EXPECT_TRUE(diagnostics_tail().empty());
	}
}

TEST(PdfFilters, LzwDecode)
{
	// 1. Seed fixture seeds/seed-filter-01-lzw.bin.
	{
		const auto seed_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-01-lzw.bin"));
		ASSERT_GT(seed_bin.size(), 2u);
		ASSERT_EQ(seed_bin[0], 0x01u);
		const std::span<const uint8_t> lzw_payload(seed_bin.data() + 1, seed_bin.size() - 1);

		auto dec = decode_lzw(lzw_payload);
		RECTO_ASSERT_OK(dec);
		EXPECT_EQ(as_string_view(*dec), kKnownFilterPlaintext);
	}

	// 2. Multi-character LZW dictionary construction, KwKwK special case (code == next_code),
	// and Clear code 256 table reset.
	{
		// Sequence:
		// 256 (Clear)
		// 'A' (65) -> emits "A", prev='A'
		// 'B' (66) -> emits "B", table[258] = "AB", prev='B'
		// 258      -> emits "AB", table[259] = "BA", prev=258 ("AB")
		// 260 (== next_code! KwKwK case) -> emits prev ("AB") + prev[0] ('A') = "ABA"
		//             and sets table[260] = "ABA"
		// 256 (Clear) -> resets table
		// 'Z' (90) -> emits "Z"
		// 257 (EOD) -> total output: "A" + "B" + "AB" + "ABA" + "Z" = "ABABABAZ"
		const std::pair<uint32_t, uint32_t> codes[] = {
			{256u, 9u},
			{'A', 9u},
			{'B', 9u},
			{258u, 9u},
			{260u, 9u},
			{256u, 9u},
			{'Z', 9u},
			{257u, 9u},
		};
		const auto encoded = pack_lzw_codes(codes);
		auto dec = decode_lzw(encoded);
		RECTO_ASSERT_OK(dec);
		EXPECT_EQ(as_string_view(*dec), "ABABABAZ");
	}

	// 3. Bit-width transition boundary: /EarlyChange 1 (switches 9->10 bits after adding table[510],
	// when next_code reaches 511) vs /EarlyChange 0 (switches 9->10 bits after adding table[511],
	// when next_code reaches 512).
	auto build_transition_stream = [](bool early_change_one) -> std::vector<uint8_t> {
		std::vector<std::pair<uint32_t, uint32_t>> codes;
		codes.emplace_back(256u, 9u);
		// First literal 'A': no table entry added yet (next_code = 258).
		codes.emplace_back('A', 9u);
		// Next 253 literals add entries 258..510 (after 253rd, next_code == 511).
		for (int i = 0; i < 253; ++i) {
			codes.emplace_back('B', 9u);
		}
		// Under EarlyChange=1, next_code==511 triggers 10-bit codes now;
		// under EarlyChange=0, this code is still 9-bit and triggers 10-bit codes after it.
		if (early_change_one) {
			codes.emplace_back(258u, 10u); // "AB"
			codes.emplace_back(257u, 10u); // EOD
		} else {
			codes.emplace_back('C', 9u);   // adds entry 511 -> next_code == 512 -> 10 bits
			codes.emplace_back(258u, 10u); // "AB"
			codes.emplace_back(257u, 10u); // EOD
		}
		return pack_lzw_codes(codes);
	};

	{
		PdfDict parms_ec1;
		parms_ec1.set("EarlyChange", PdfValue(int64_t{1}));
		const auto stm_ec1 = build_transition_stream(true);
		auto dec_ec1 = decode_lzw(stm_ec1, &parms_ec1);
		RECTO_ASSERT_OK(dec_ec1);
		EXPECT_EQ(dec_ec1->size(), 1u + 253u + 2u);
		EXPECT_EQ((*dec_ec1)[0], 'A');
		EXPECT_EQ((*dec_ec1)[dec_ec1->size() - 2], 'A');
		EXPECT_EQ((*dec_ec1)[dec_ec1->size() - 1], 'B');

		PdfDict parms_ec0;
		parms_ec0.set("EarlyChange", PdfValue(int64_t{0}));
		const auto stm_ec0 = build_transition_stream(false);
		auto dec_ec0 = decode_lzw(stm_ec0, &parms_ec0);
		RECTO_ASSERT_OK(dec_ec0);
		EXPECT_EQ(dec_ec0->size(), 1u + 253u + 1u + 2u);
		EXPECT_EQ((*dec_ec0)[dec_ec0->size() - 3], 'C');
		EXPECT_EQ((*dec_ec0)[dec_ec0->size() - 2], 'A');
		EXPECT_EQ((*dec_ec0)[dec_ec0->size() - 1], 'B');
	}

	// 4. Reject corrupt out-of-range LZW code (300 when next_code is 258) and missing EOD,
	// and verify dictionary pool growth across >200chained KwKwK entries (>20 KB dict_pool reallocation).
	{
		const std::pair<uint32_t, uint32_t> bad_codes[] = {
			{256u, 9u},
			{300u, 9u},
			{257u, 9u},
		};
		RECTO_EXPECT_ERR(decode_lzw(pack_lzw_codes(bad_codes)), ErrorCode::DamagedFormat);

		const std::pair<uint32_t, uint32_t> no_eod[] = {
			{256u, 9u},
			{'A', 9u},
		};
		RECTO_EXPECT_ERR(decode_lzw(pack_lzw_codes(no_eod)), ErrorCode::DamagedFormat);

		// Chain 200 KwKwK codes (258..457) where entry 258+k has length k+2:
		// total dictionary pool size = sum_{k=0..199} (k+2) = 20,300 bytes (> 16,384 initial reserve).
		std::vector<std::pair<uint32_t, uint32_t>> deep_codes;
		deep_codes.emplace_back(256u, 9u);
		deep_codes.emplace_back('X', 9u);
		for (uint32_t c = 258u; c < 458u; ++c) {
			deep_codes.emplace_back(c, 9u);
		}
		deep_codes.emplace_back(257u, 9u);
		auto deep_res = decode_lzw(pack_lzw_codes(deep_codes));
		RECTO_ASSERT_OK(deep_res);
		// Total output length: 1 + sum_{k=0..199} (k+2) = 20,301 'X' bytes.
		ASSERT_EQ(deep_res->size(), 20301u);
		EXPECT_EQ(*deep_res, std::vector<uint8_t>(20301u, 'X'));
	}
}

TEST(PdfFilters, Predictors)
{
	// 1. Seed fixture seeds/seed-filter-02-predictor.bin (Flate + PNG Up Predictor 12, Columns 8).
	{
		const auto seed_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-02-predictor.bin"));
		ASSERT_GT(seed_bin.size(), 2u);
		ASSERT_EQ(seed_bin[0], 0x02u);
		const std::span<const uint8_t> payload(seed_bin.data() + 1, seed_bin.size() - 1);

		PdfDict parms;
		parms.set("Predictor", PdfValue(int64_t{12}));
		parms.set("Columns", PdfValue(int64_t{8}));
		auto dec = decode_flate(payload, &parms);
		RECTO_ASSERT_OK(dec);
		EXPECT_EQ(as_string_view(*dec), kKnownFilterPlaintext);
	}

	// 2. All 5 PNG row filters (0=None, 1=Sub, 2=Up, 3=Average, 4=Paeth) in a 5-row image
	// (/Predictor 15 Optimum, /Colors 3, /BitsPerComponent 8, /Columns 2 -> 6 bytes per row).
	{
		const std::array<std::array<uint8_t, 6>, 5> orig = {{
			{10, 20, 30, 40, 50, 60},
			{15, 25, 35, 50, 65, 80},
			{20, 30, 40, 60, 70, 90},
			{25, 40, 55, 70, 85, 100},
			{33, 44, 55, 66, 77, 88},
		}};
		constexpr size_t kBpp = 3;
		constexpr size_t kRowBytes = 6;

		auto paeth = [](uint8_t a, uint8_t b, uint8_t c) -> uint8_t {
			const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
			const int pa = std::abs(p - static_cast<int>(a));
			const int pb = std::abs(p - static_cast<int>(b));
			const int pc = std::abs(p - static_cast<int>(c));
			if (pa <= pb && pa <= pc) return a;
			if (pb <= pc) return b;
			return c;
		};

		std::vector<uint8_t> encoded;
		for (size_t r = 0; r < 5; ++r) {
			const uint8_t tag = static_cast<uint8_t>(r); // row r uses PNG filter r (0..4)
			encoded.push_back(tag);
			for (size_t x = 0; x < kRowBytes; ++x) {
				const uint8_t cur = orig[r][x];
				const uint8_t left = (x >= kBpp) ? orig[r][x - kBpp] : 0u;
				const uint8_t up = (r > 0) ? orig[r - 1][x] : 0u;
				const uint8_t up_left = (r > 0 && x >= kBpp) ? orig[r - 1][x - kBpp] : 0u;
				uint8_t filt = 0;
				switch (tag) {
				case 0: filt = cur; break;
				case 1: filt = static_cast<uint8_t>(cur - left); break;
				case 2: filt = static_cast<uint8_t>(cur - up); break;
				case 3: filt = static_cast<uint8_t>(cur - ((uint32_t(left) + uint32_t(up)) >> 1u)); break;
				case 4: filt = static_cast<uint8_t>(cur - paeth(left, up, up_left)); break;
				}
				encoded.push_back(filt);
			}
		}

		PredictorParams p{15, 3, 8, 2};
		auto dec = apply_predictor(encoded, p);
		RECTO_ASSERT_OK(dec);
		ASSERT_EQ(dec->size(), 30u);
		for (size_t r = 0; r < 5; ++r) {
			for (size_t x = 0; x < kRowBytes; ++x) {
				EXPECT_EQ((*dec)[r * kRowBytes + x], orig[r][x])
					<< "mismatch at row " << r << " col " << x;
			}
		}
	}

	// 3. TIFF Predictor 2 across /BitsPerComponent 8, 16, and 4 (/Colors 2, /Columns 4).
	{
		// 8-bit, Colors=2, Columns=3 -> samples: (10, 20), (15, 25), (30, 5)
		// Differenced row: (10, 20), (5, 5), (15, 236)
		const std::array<uint8_t, 6> tiff8_in = {10, 20, 5, 5, 15, 236};
		const std::array<uint8_t, 6> tiff8_exp = {10, 20, 15, 25, 30, 5};
		auto dec8 = apply_predictor(tiff8_in, PredictorParams{2, 2, 8, 3});
		RECTO_ASSERT_OK(dec8);
		EXPECT_EQ(*dec8, std::vector<uint8_t>(tiff8_exp.begin(), tiff8_exp.end()));

		// 16-bit big-endian, Colors=1, Columns=3 -> samples: 0x0100, 0x0250, 0x0350
		// Differenced: 0x0100, 0x0150, 0x0100
		const std::array<uint8_t, 6> tiff16_in = {0x01, 0x00, 0x01, 0x50, 0x01, 0x00};
		const std::array<uint8_t, 6> tiff16_exp = {0x01, 0x00, 0x02, 0x50, 0x03, 0x50};
		auto dec16 = apply_predictor(tiff16_in, PredictorParams{2, 1, 16, 3});
		RECTO_ASSERT_OK(dec16);
		EXPECT_EQ(*dec16, std::vector<uint8_t>(tiff16_exp.begin(), tiff16_exp.end()));

		// 4-bit MSB-first, Colors=1, Columns=4 -> nibbles: 2, 5, 7, 15
		// Differenced nibbles: 2, 3, 2, 8 -> bytes: 0x23, 0x28 -> decoded: 0x25, 0x7F
		const std::array<uint8_t, 2> tiff4_in = {0x23u, 0x28u};
		const std::array<uint8_t, 2> tiff4_exp = {0x25u, 0x7Fu};
		auto dec4 = apply_predictor(tiff4_in, PredictorParams{2, 1, 4, 4});
		RECTO_ASSERT_OK(dec4);
		EXPECT_EQ(*dec4, std::vector<uint8_t>(tiff4_exp.begin(), tiff4_exp.end()));
	}
}

TEST(PdfFilters, AsciiFilters)
{
	// 1. Seed fixtures seeds/seed-filter-03-ascii85.bin and seeds/seed-filter-04-asciihex.bin.
	{
		const auto a85_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-03-ascii85.bin"));
		ASSERT_GT(a85_bin.size(), 2u);
		ASSERT_EQ(a85_bin[0], 0x03u);
		auto dec_a85 = decode_ascii85(std::span<const uint8_t>(a85_bin.data() + 1, a85_bin.size() - 1));
		RECTO_ASSERT_OK(dec_a85);
		EXPECT_EQ(as_string_view(*dec_a85), kKnownFilterPlaintext);

		const auto hex_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-04-asciihex.bin"));
		ASSERT_GT(hex_bin.size(), 2u);
		ASSERT_EQ(hex_bin[0], 0x04u);
		auto dec_hex = decode_ascii_hex(std::span<const uint8_t>(hex_bin.data() + 1, hex_bin.size() - 1));
		RECTO_ASSERT_OK(dec_hex);
		EXPECT_EQ(as_string_view(*dec_hex), kKnownFilterPlaintext);
	}

	// 2. ASCIIHexDecode whitespace, odd final nibble padding, and invalid character / missing '>' rejection.
	{
		auto odd_hex = decode_ascii_hex(as_bytes("48 65 6c 6C 6F 5 >"));
		RECTO_ASSERT_OK(odd_hex);
		const std::vector<uint8_t> expected = {'H', 'e', 'l', 'l', 'o', 0x50u};
		EXPECT_EQ(*odd_hex, expected);

		RECTO_EXPECT_ERR(decode_ascii_hex(as_bytes("4865")), ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(decode_ascii_hex(as_bytes("48G5>")), ErrorCode::DamagedFormat);
	}

	// 3. ASCII85Decode RFC/ISO vectors: "9jqo^~>" -> "Man ", 'z' -> 4 zero bytes,
	// partial 2, 3, and 4-char final tuples, and rejection of 'v'..'y', single-char tuple, and overflow.
	{
		auto man = decode_ascii85(as_bytes("9jqo^~>"));
		RECTO_ASSERT_OK(man);
		EXPECT_EQ(as_string_view(*man), "Man ");

		auto zeros = decode_ascii85(as_bytes("  z \n z ~>"));
		RECTO_ASSERT_OK(zeros);
		EXPECT_EQ(*zeros, std::vector<uint8_t>(8, 0u));

		// Partial final tuples: "A" (1 byte) -> "5l~>", "AB" (2 bytes) -> "5sb~>", "ABC" (3 bytes) -> "5sdp~>"
		auto p1 = decode_ascii85(as_bytes("5l~>"));
		RECTO_ASSERT_OK(p1);
		EXPECT_EQ(as_string_view(*p1), "A");

		auto p2 = decode_ascii85(as_bytes("5sb~>"));
		RECTO_ASSERT_OK(p2);
		EXPECT_EQ(as_string_view(*p2), "AB");

		auto p3 = decode_ascii85(as_bytes("5sdp~>"));
		RECTO_ASSERT_OK(p3);
		EXPECT_EQ(as_string_view(*p3), "ABC");

		// Invalid character 'v', 'z' inside tuple, 1-char final tuple, and 32-bit tuple overflow ("s8W-\"~>").
		RECTO_EXPECT_ERR(decode_ascii85(as_bytes("9jqov~>")), ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(decode_ascii85(as_bytes("9zqo^~>")), ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(decode_ascii85(as_bytes("5~>")), ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(decode_ascii85(as_bytes("s8W-\"~>")), ErrorCode::DamagedFormat);
		RECTO_EXPECT_ERR(decode_ascii85(as_bytes("9jqo^")), ErrorCode::DamagedFormat);
	}
}

TEST(PdfFilters, RunLengthDecode)
{
	// 1. Seed fixture seeds/seed-filter-05-runlength.bin.
	{
		const auto rle_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-05-runlength.bin"));
		ASSERT_GT(rle_bin.size(), 2u);
		ASSERT_EQ(rle_bin[0], 0x05u);
		auto dec_rle = decode_run_length(std::span<const uint8_t>(rle_bin.data() + 1, rle_bin.size() - 1));
		RECTO_ASSERT_OK(dec_rle);
		EXPECT_EQ(as_string_view(*dec_rle), kKnownFilterPlaintext);
	}

	// 2. Literal copy (0..127 -> 1..128 bytes), repeat run (129..255 -> 257 - n copies), and 128 EOD.
	{
		// 2 literal bytes "Hi" (len_byte = 1), 4 repeats of '!' (len_byte = 257 - 4 = 253), 128 EOD.
		const std::array<uint8_t, 6> rle_in = {1u, 'H', 'i', 253u, '!', 128u};
		auto dec = decode_run_length(rle_in);
		RECTO_ASSERT_OK(dec);
		EXPECT_EQ(as_string_view(*dec), "Hi!!!!");
	}

	// 3. Truncated literal run, truncated repeat run, and missing EOD fail safely without out-of-bounds read.
	{
		const std::array<uint8_t, 3> trunc_lit = {4u, 'A', 'B'};
		RECTO_EXPECT_ERR(decode_run_length(trunc_lit), ErrorCode::DamagedFormat);

		const std::array<uint8_t, 1> trunc_rep = {250u};
		RECTO_EXPECT_ERR(decode_run_length(trunc_rep), ErrorCode::DamagedFormat);

		const std::array<uint8_t, 2> no_eod = {0u, 'A'};
		RECTO_EXPECT_ERR(decode_run_length(no_eod), ErrorCode::DamagedFormat);
	}
}

TEST(PdfFilters, ChainedPipeline)
{
	// 1. Seed fixture seeds/seed-filter-0a-chain-a85-flate.bin and seeds/seed-filters-ascii-rle.pdf.
	{
		const auto chain_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-0a-chain-a85-flate.bin"));
		ASSERT_GT(chain_bin.size(), 2u);
		ASSERT_EQ(chain_bin[0], 0x0Au);
		const std::span<const uint8_t> payload(chain_bin.data() + 1, chain_bin.size() - 1);

		PdfArray filters;
		filters.push_back(PdfValue(PdfName("ASCII85Decode")));
		filters.push_back(PdfValue(PdfName("FlateDecode")));
		PdfDict dict;
		dict.set("Filter", PdfValue(std::move(filters)));

		auto dec = decode_stream(payload, dict);
		RECTO_ASSERT_OK(dec);
		EXPECT_EQ(as_string_view(*dec), kKnownFilterPlaintext);
	}

	// 2. Decode all 4 streams (4=ASCIIHex, 5=ASCII85, 6=RunLength, 7=[ASCII85 Flate]) in seeds/seed-filters-ascii-rle.pdf.
	{
		const auto pdf_bytes = test::read_file_bytes(
			test::corpus_path("seeds/seed-filters-ascii-rle.pdf"));
		auto xref_res = XrefTable::parse(pdf_bytes);
		RECTO_ASSERT_OK(xref_res);

		for (uint32_t obj_id = 4; obj_id <= 7; ++obj_id) {
			auto stm_obj = xref_res->resolve_object(pdf_bytes, IndirectRef{obj_id, 0u});
			RECTO_ASSERT_OK(stm_obj);
			ASSERT_TRUE(stm_obj->is_stream());
			auto dec = decode_stream(*stm_obj->as_stream());
			RECTO_ASSERT_OK(dec);
			EXPECT_NE(as_string_view(*dec).find("Seed 3"), std::string_view::npos);
		}
	}

	// 3. Multi-stage pipeline with matching /DecodeParms array and up to 8 filters succeeds;
	// 9-stage filter pipeline is rejected with ErrorCode::DamagedFormat.
	{
		const auto pred_bin = test::read_file_bytes(
			test::corpus_path("seeds/seed-filter-02-predictor.bin"));
		ASSERT_GT(pred_bin.size(), 2u);
		const std::span<const uint8_t> flate_pred_payload(pred_bin.data() + 1, pred_bin.size() - 1);

		// Encode flate_pred_payload as ASCIIHex so stage 0 is AHx and stage 1 is Fl with Predictor 12.
		std::string hex_encoded;
		static constexpr char kHexChars[] = "0123456789ABCDEF";
		for (uint8_t b : flate_pred_payload) {
			hex_encoded.push_back(kHexChars[(b >> 4u) & 0x0Fu]);
			hex_encoded.push_back(kHexChars[b & 0x0Fu]);
		}
		hex_encoded.push_back('>');

		PdfArray filters_2;
		filters_2.push_back(PdfValue(PdfName("AHx")));
		filters_2.push_back(PdfValue(PdfName("Fl")));

		PdfDict flate_parms;
		flate_parms.set("Predictor", PdfValue(int64_t{12}));
		flate_parms.set("Columns", PdfValue(int64_t{8}));

		PdfArray parms_2;
		parms_2.push_back(PdfValue(PdfNull{}));
		parms_2.push_back(PdfValue(std::move(flate_parms)));

		PdfDict stm_dict;
		stm_dict.set("Filter", PdfValue(std::move(filters_2)));
		stm_dict.set("DecodeParms", PdfValue(std::move(parms_2)));

		auto dec_2 = decode_stream(as_bytes(hex_encoded), stm_dict);
		RECTO_ASSERT_OK(dec_2);
		EXPECT_EQ(as_string_view(*dec_2), kKnownFilterPlaintext);

		// 8-stage Crypt Identity pipeline succeeds.
		PdfArray filters_8;
		for (size_t i = 0; i < 8; ++i) {
			filters_8.push_back(PdfValue(PdfName("Crypt")));
		}
		PdfDict dict_8;
		dict_8.set("Filter", PdfValue(std::move(filters_8)));
		auto dec_8 = decode_stream(as_bytes("8-stage-ok"), dict_8);
		RECTO_ASSERT_OK(dec_8);
		EXPECT_EQ(as_string_view(*dec_8), "8-stage-ok");

		// 9-stage pipeline fails with ErrorCode::DamagedFormat.
		PdfArray filters_9;
		for (size_t i = 0; i < 9; ++i) {
			filters_9.push_back(PdfValue(PdfName("Crypt")));
		}
		PdfDict dict_9;
		dict_9.set("Filter", PdfValue(std::move(filters_9)));
		RECTO_EXPECT_ERR(decode_stream(as_bytes("9-stage-fail"), dict_9), ErrorCode::DamagedFormat);

		// /F + /FFilter + /FDecodeParms handling:
		// - Inline decode_stream on a dictionary or PdfStream with /F always returns
		//   ErrorCode::UnsupportedFeature (even if inline raw_bytes are present).
		// - decode_external_stream uses /FFilter and /FDecodeParms on caller-supplied bytes.
		// - Orphan /FFilter or /FDecodeParms without /F fails with ErrorCode::DamagedFormat,
		//   even when an ordinary inline /Filter is also present.
		PdfDict ext_dict;
		ext_dict.set("F", PdfValue(PdfString("external.bin")));
		ext_dict.set("FFilter", PdfValue(PdfName("FlateDecode")));
		PdfDict ext_parms;
		ext_parms.set("Predictor", PdfValue(int64_t{12}));
		ext_parms.set("Columns", PdfValue(int64_t{8}));
		ext_dict.set("FDecodeParms", PdfValue(std::move(ext_parms)));

		RECTO_EXPECT_ERR(decode_stream(std::span<const uint8_t>{}, ext_dict), ErrorCode::UnsupportedFeature);
		RECTO_EXPECT_ERR(decode_stream(as_bytes("4865>"), ext_dict), ErrorCode::UnsupportedFeature);

		PdfStream parsed_ext_stm;
		parsed_ext_stm.dict.set("F", PdfValue(PdfString("external.bin")));
		parsed_ext_stm.dict.set("FFilter", PdfValue(PdfName("ASCIIHexDecode")));
		parsed_ext_stm.raw_bytes = {'4', '8', '6', '5', '>'};
		RECTO_EXPECT_ERR(decode_stream(parsed_ext_stm), ErrorCode::UnsupportedFeature);

		auto dec_ext = decode_external_stream(flate_pred_payload, ext_dict);
		RECTO_ASSERT_OK(dec_ext);
		EXPECT_EQ(as_string_view(*dec_ext), kKnownFilterPlaintext);

		PdfDict orphan_ffilter;
		orphan_ffilter.set("FFilter", PdfValue(PdfName("ASCIIHexDecode")));
		RECTO_EXPECT_ERR(decode_stream(as_bytes("4865>"), orphan_ffilter), ErrorCode::DamagedFormat);

		PdfDict orphan_ffilter_with_filter;
		orphan_ffilter_with_filter.set("Filter", PdfValue(PdfName("ASCIIHexDecode")));
		orphan_ffilter_with_filter.set("FFilter", PdfValue(PdfName("FlateDecode")));
		RECTO_EXPECT_ERR(decode_stream(as_bytes("4865>"), orphan_ffilter_with_filter), ErrorCode::DamagedFormat);

		PdfDict orphan_fparms_with_filter;
		orphan_fparms_with_filter.set("Filter", PdfValue(PdfName("ASCIIHexDecode")));
		orphan_fparms_with_filter.set("FDecodeParms", PdfValue(PdfDict{}));
		RECTO_EXPECT_ERR(decode_stream(as_bytes("4865>"), orphan_fparms_with_filter), ErrorCode::DamagedFormat);
	}
}

} // namespace
} // namespace recto::pdf
