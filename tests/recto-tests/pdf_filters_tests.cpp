// Unit tests for PDF stream filters: FlateDecode, LZWDecode, TIFF Predictor 2
// and PNG Predictors 10..15, ASCIIHexDecode, ASCII85Decode, RunLengthDecode,
// CCITTFaxDecode (Group 3 1D/2D and Group 4 2D), JBIG2Decode, WIC DCTDecode and
// JPXDecode, chained filter execution, and the 256 MB decompression cap
// (D01 T01 sections 7 and 8).
#include "recto_pdf.h"
#include "test_support.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <latch>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

[[nodiscard]] std::vector<uint8_t> pack_bitstring(std::string_view bits)
{
	std::vector<uint8_t> out;
	uint8_t cur = 0u;
	uint32_t bit_count = 0u;
	for (char ch : bits) {
		if (ch != '0' && ch != '1') {
			continue;
		}
		cur = static_cast<uint8_t>((cur << 1u) | (ch == '1' ? 1u : 0u));
		if (++bit_count == 8u) {
			out.push_back(cur);
			cur = 0u;
			bit_count = 0u;
		}
	}
	if (bit_count > 0u) {
		out.push_back(static_cast<uint8_t>(cur << (8u - bit_count)));
	}
	return out;
}

TEST(PdfFilters, CcittGroup4)
{
	// 1. Decode the 16x16 Group 4 monochrome image (object 10 0 R) in render/images-and-vectors.pdf.
	const auto pdf_bytes = test::read_file_bytes(
		test::corpus_path("render/images-and-vectors.pdf"));
	auto xref_res = XrefTable::parse(pdf_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto stm_obj = xref_res->resolve_object(pdf_bytes, IndirectRef{10u, 0u});
	RECTO_ASSERT_OK(stm_obj);
	ASSERT_TRUE(stm_obj->is_stream());

	auto decoded = decode_stream(*stm_obj->as_stream());
	RECTO_ASSERT_OK(decoded);
	ASSERT_EQ(decoded->size(), 32u); // 16 rows * 2 bytes per row

	// Expected rows (BlackIs1 = false -> 1 = White, 0 = Black):
	// Row 0 (4W, 8B, 4W): 11110000 00001111 = 0xF0, 0x0F
	// Row 1 (4W, 8B, 4W): 0xF0, 0x0F
	// Row 2 (5W, 8B, 3W): 11111000 00000111 = 0xF8, 0x07
	// Row 3 (4W, 8B, 4W): 0xF0, 0x0F
	// Rows 4..15 (16W):   0xFF, 0xFF
	EXPECT_EQ((*decoded)[0], 0xF0u);
	EXPECT_EQ((*decoded)[1], 0x0Fu);
	EXPECT_EQ((*decoded)[2], 0xF0u);
	EXPECT_EQ((*decoded)[3], 0x0Fu);
	EXPECT_EQ((*decoded)[4], 0xF8u);
	EXPECT_EQ((*decoded)[5], 0x07u);
	EXPECT_EQ((*decoded)[6], 0xF0u);
	EXPECT_EQ((*decoded)[7], 0x0Fu);
	for (size_t row = 4; row < 16; ++row) {
		EXPECT_EQ((*decoded)[row * 2u + 0u], 0xFFu) << "row " << row;
		EXPECT_EQ((*decoded)[row * 2u + 1u], 0xFFu) << "row " << row;
	}

	// 2. Exercise VR(2), VR(3), VL(2), VL(3), Pass, and Horizontal make-up codes (>= 64) on a 96-column image.
	// Row 0 (72W, 16B, 8W):
	//   H(001) + [W64(11011) + W8(10011)] + [B16(0000010111)] + V0(1)
	// Row 1 (74W, 13B, 9W):
	//   VR2(000011) -> a1 = 72 + 2 = 74; VL1(010) -> a1 = 88 - 1 = 87; V0(1)
	// Row 2 (77W, 7B, 12W):
	//   VR3(0000011) -> a1 = 74 + 3 = 77; VL3(0000010) -> a1 = 87 - 3 = 84; V0(1)
	// Row 3 (75W, 9B, 12W):
	//   VL2(000010) -> a1 = 77 - 2 = 75; V0(1) -> a1 = 84; V0(1)
	// EOFB: 000000000001000000000001
	{
		const std::string g4_bits =
			std::string("001") + "11011" + "10011" + "0000010111" + "1" +
			"000011" + "010" + "1" +
			"0000011" + "0000010" + "1" +
			"000010" + "1" + "1" +
			"000000000001000000000001";
		const auto g4_bytes = pack_bitstring(g4_bits);
		PdfDict parms;
		parms.set("K", PdfValue(int64_t{-1}));
		parms.set("Columns", PdfValue(int64_t{96}));
		parms.set("Rows", PdfValue(int64_t{4}));
		parms.set("BlackIs1", PdfValue(true));

		auto res = decode_ccitt_fax(g4_bytes, &parms);
		RECTO_ASSERT_OK(res);
		ASSERT_EQ(res->size(), 48u); // 4 rows * 12 bytes per row

		auto count_black_bits = [&](size_t row, int32_t& first_black, int32_t& last_black_exclusive) {
			first_black = -1;
			last_black_exclusive = -1;
			for (int32_t x = 0; x < 96; ++x) {
				const uint8_t b = (*res)[row * 12u + (static_cast<size_t>(x) >> 3u)];
				const bool is_black = ((b >> (7u - (static_cast<uint32_t>(x) & 7u))) & 1u) != 0u;
				if (is_black) {
					if (first_black < 0) {
						first_black = x;
					}
					last_black_exclusive = x + 1;
				}
			}
		};

		int32_t x0 = 0;
		int32_t x1 = 0;
		count_black_bits(0, x0, x1);
		EXPECT_EQ(x0, 72);
		EXPECT_EQ(x1, 88);

		count_black_bits(1, x0, x1);
		EXPECT_EQ(x0, 74);
		EXPECT_EQ(x1, 87);

		count_black_bits(2, x0, x1);
		EXPECT_EQ(x0, 77);
		EXPECT_EQ(x1, 84);

		count_black_bits(3, x0, x1);
		EXPECT_EQ(x0, 75);
		EXPECT_EQ(x1, 84);
	}

	// 3. Reject incomplete single-EOL EOFB and zero-progress H(0, 0) in Group 4.
	{
		PdfDict parms;
		parms.set("K", PdfValue(int64_t{-1}));
		parms.set("Columns", PdfValue(int64_t{16}));
		// Row 0 = 16W (V0 = "1"), followed by a single EOL "000000000001" + non-EOL "11111111".
		const auto bad_eofb = pack_bitstring("100000000000111111111");
		RECTO_EXPECT_ERR(decode_ccitt_fax(bad_eofb, &parms), ErrorCode::DamagedFormat);

		// H(001) + W0(00110101) + B0(0000110111) at start of row -> zero progress -> DamagedFormat.
		const auto h00 = pack_bitstring("001001101010000110111");
		RECTO_EXPECT_ERR(decode_ccitt_fax(h00, &parms), ErrorCode::DamagedFormat);
	}
}

TEST(PdfFilters, CcittGroup3AndBlackIs1)
{
	// 1. Group 3 1D (K == 0) with /Columns 12 (non-multiple of 8) across /BlackIs1 false and true,
	// and /EncodedByteAlign false and true.
	// Row 0: 4W (1011) + 4B (011) + 4W (1011) -> 11 bits
	// Row 1: 8W (10011) + 4B (011) + W0 (00110101) -> 16 bits
	{
		const std::string row0_bits = "1011" "011" "1011";
		const std::string row1_bits = "10011" "011" "00110101";

		// Unaligned bitstream
		const auto unaligned_bytes = pack_bitstring(row0_bits + row1_bits);

		PdfDict parms_bi0;
		parms_bi0.set("K", PdfValue(int64_t{0}));
		parms_bi0.set("Columns", PdfValue(int64_t{12}));
		parms_bi0.set("Rows", PdfValue(int64_t{2}));
		parms_bi0.set("BlackIs1", PdfValue(false));
		parms_bi0.set("EncodedByteAlign", PdfValue(false));

		auto dec_bi0 = decode_ccitt_fax(unaligned_bytes, &parms_bi0);
		RECTO_ASSERT_OK(dec_bi0);
		ASSERT_EQ(dec_bi0->size(), 4u); // 2 rows * 2 bytes/row
		// Row 0 (4W, 4B, 4W), BlackIs1=false (1=W, 0=B, low 4 bits of byte 1 masked to 0):
		// byte 0: 11110000 = 0xF0, byte 1: 11110000 = 0xF0
		// Row 1 (8W, 4B), BlackIs1=false:
		// byte 2: 11111111 = 0xFF, byte 3: 00000000 = 0x00
		EXPECT_EQ((*dec_bi0)[0], 0xF0u);
		EXPECT_EQ((*dec_bi0)[1], 0xF0u);
		EXPECT_EQ((*dec_bi0)[2], 0xFFu);
		EXPECT_EQ((*dec_bi0)[3], 0x00u);

		// Same stream with /BlackIs1 true (1=B, 0=W):
		// Row 0: byte 0 = 00001111 = 0x0F, byte 1 = 00000000 = 0x00
		// Row 1: byte 2 = 00000000 = 0x00, byte 3 = 11110000 = 0xF0
		PdfDict parms_bi1 = parms_bi0;
		parms_bi1.set("BlackIs1", PdfValue(true));
		auto dec_bi1 = decode_ccitt_fax(unaligned_bytes, &parms_bi1);
		RECTO_ASSERT_OK(dec_bi1);
		ASSERT_EQ(dec_bi1->size(), 4u);
		EXPECT_EQ((*dec_bi1)[0], 0x0Fu);
		EXPECT_EQ((*dec_bi1)[1], 0x00u);
		EXPECT_EQ((*dec_bi1)[2], 0x00u);
		EXPECT_EQ((*dec_bi1)[3], 0xF0u);

		// Byte-aligned bitstream (/EncodedByteAlign true): pad row0_bits (11 bits) with 5 zero bits to 16 bits.
		const auto aligned_bytes = pack_bitstring(row0_bits + "00000" + row1_bits);
		PdfDict parms_aligned = parms_bi1;
		parms_aligned.set("EncodedByteAlign", PdfValue(true));
		auto dec_aligned = decode_ccitt_fax(aligned_bytes, &parms_aligned);
		RECTO_ASSERT_OK(dec_aligned);
		EXPECT_EQ(*dec_aligned, *dec_bi1);
	}

	// 2. Group 3 2D (K = 2) with /EndOfLine true and mixed 1D (tag=1) and 2D (tag=0) rows,
	// plus /DamagedRowsBeforeError recovery on Group 3 1D.
	{
		// 16 columns, 2 rows:
		// Row 0 (1D): EOL(000000000001) + tag(1) + 4W(1011) + 8B(000101) + 4W(1011)
		// Row 1 (2D): EOL(000000000001) + tag(0) + VR1(011) + VR1(011) + V0(1) -> 5W, 8B, 3W
		// RTC:        EOL(000000000001) + tag(1) + EOL(000000000001) + tag(1)
		const std::string g3_2d_bits =
			std::string("000000000001") + "1" + "1011" + "000101" + "1011" +
			"000000000001" + "0" + "011" + "011" + "1" +
			"000000000001" + "1" + "000000000001" + "1";
		const auto g3_2d_bytes = pack_bitstring(g3_2d_bits);

		PdfDict parms_g3_2d;
		parms_g3_2d.set("K", PdfValue(int64_t{2}));
		parms_g3_2d.set("Columns", PdfValue(int64_t{16}));
		parms_g3_2d.set("Rows", PdfValue(int64_t{2}));
		parms_g3_2d.set("EndOfLine", PdfValue(true));
		parms_g3_2d.set("BlackIs1", PdfValue(false));

		auto dec_2d = decode_ccitt_fax(g3_2d_bytes, &parms_g3_2d);
		RECTO_ASSERT_OK(dec_2d);
		ASSERT_EQ(dec_2d->size(), 4u);
		EXPECT_EQ((*dec_2d)[0], 0xF0u);
		EXPECT_EQ((*dec_2d)[1], 0x0Fu);
		EXPECT_EQ((*dec_2d)[2], 0xF8u);
		EXPECT_EQ((*dec_2d)[3], 0x07u);

		// Group 3 1D damaged row tolerance (/DamagedRowsBeforeError 1 vs 0):
		// Row 0: EOL + 4W(1011) + 8B(000101) + 4W(1011)
		// Row 1 (damaged: invalid prefix "00000001" before next EOL)
		// Row 2: EOL + 16W(101010)
		const std::string damaged_bits =
			std::string("000000000001") + "1011" + "000101" + "1011" +
			"000000000001" + "00000001" +
			"000000000001" + "101010";
		const auto damaged_bytes = pack_bitstring(damaged_bits);

		PdfDict parms_strict;
		parms_strict.set("K", PdfValue(int64_t{0}));
		parms_strict.set("Columns", PdfValue(int64_t{16}));
		parms_strict.set("Rows", PdfValue(int64_t{3}));
		parms_strict.set("EndOfLine", PdfValue(true));
		parms_strict.set("DamagedRowsBeforeError", PdfValue(int64_t{0}));
		RECTO_EXPECT_ERR(decode_ccitt_fax(damaged_bytes, &parms_strict), ErrorCode::DamagedFormat);

		PdfDict parms_tolerant = parms_strict;
		parms_tolerant.set("DamagedRowsBeforeError", PdfValue(int64_t{1}));
		auto dec_tol = decode_ccitt_fax(damaged_bytes, &parms_tolerant);
		RECTO_ASSERT_OK(dec_tol);
		ASSERT_EQ(dec_tol->size(), 6u);
		// Row 0 and substituted Row 1 are 0xF0, 0x0F; Row 2 is 0xFF, 0xFF.
		EXPECT_EQ((*dec_tol)[0], 0xF0u);
		EXPECT_EQ((*dec_tol)[1], 0x0Fu);
		EXPECT_EQ((*dec_tol)[2], 0xF0u);
		EXPECT_EQ((*dec_tol)[3], 0x0Fu);
		EXPECT_EQ((*dec_tol)[4], 0xFFu);
		EXPECT_EQ((*dec_tol)[5], 0xFFu);

		// 3. ISO 32000-1 Table 11: When /EndOfBlock is true (default), EOFB overrides /Rows
		// even when the stream encodes more rows than /Rows before the EOFB marker.
		const std::string g4_3rows_bits =
			std::string("001") + "1011" + "000101" + "1" + // Row 0: 4W, 8B, 4W
			"111" +                                        // Row 1: V0, V0, V0
			"0001" + "1" +                                 // Row 2: Pass + V0 -> 16W
			"000000000001000000000001";                    // EOFB
		const auto g4_3rows_bytes = pack_bitstring(g4_3rows_bits);

		PdfDict parms_eob_true;
		parms_eob_true.set("K", PdfValue(int64_t{-1}));
		parms_eob_true.set("Columns", PdfValue(int64_t{16}));
		parms_eob_true.set("Rows", PdfValue(int64_t{1})); // /Rows = 1, but 3 rows precede EOFB
		parms_eob_true.set("EndOfBlock", PdfValue(true));
		auto dec_eob_true = decode_ccitt_fax(g4_3rows_bytes, &parms_eob_true);
		RECTO_ASSERT_OK(dec_eob_true);
		ASSERT_EQ(dec_eob_true->size(), 6u); // All 3 rows decoded up to EOFB

		PdfDict parms_eob_false = parms_eob_true;
		parms_eob_false.set("EndOfBlock", PdfValue(false));
		auto dec_eob_false = decode_ccitt_fax(g4_3rows_bytes, &parms_eob_false);
		RECTO_ASSERT_OK(dec_eob_false);
		ASSERT_EQ(dec_eob_false->size(), 2u); // Stops strictly at /Rows = 1 when /EndOfBlock is false
	}
}

namespace {

void append_u16_be(std::vector<uint8_t>& buf, uint16_t v)
{
	buf.push_back(static_cast<uint8_t>((v >> 8u) & 0xFFu));
	buf.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void append_u32_be(std::vector<uint8_t>& buf, uint32_t v)
{
	buf.push_back(static_cast<uint8_t>((v >> 24u) & 0xFFu));
	buf.push_back(static_cast<uint8_t>((v >> 16u) & 0xFFu));
	buf.push_back(static_cast<uint8_t>((v >> 8u) & 0xFFu));
	buf.push_back(static_cast<uint8_t>(v & 0xFFu));
}

// Deterministic QM/MQ arithmetic encoder matching ISO/IEC 14492 Annex E / ITU-T T.800 Annex C.
struct TestMqQeEntry {
	uint16_t qe;
	uint8_t nmps;
	uint8_t nlps;
	uint8_t sw;
};

constexpr TestMqQeEntry kTestMqQeTable[47] = {
	{0x5601,  1,  1, 1}, {0x3401,  2,  6, 0}, {0x1801,  3,  9, 0}, {0x0AC1,  4, 12, 0},
	{0x0521,  5, 29, 0}, {0x0221, 38, 33, 0}, {0x5601,  7,  6, 1}, {0x5401,  8, 14, 0},
	{0x4801,  9, 14, 0}, {0x3801, 10, 14, 0}, {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0},
	{0x1C01, 13, 20, 0}, {0x1601, 29, 21, 0}, {0x5601, 15, 14, 1}, {0x5401, 16, 14, 0},
	{0x5101, 17, 15, 0}, {0x4801, 18, 16, 0}, {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0},
	{0x3001, 21, 19, 0}, {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0},
	{0x1C01, 25, 22, 0}, {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0}, {0x1401, 28, 25, 0},
	{0x1201, 29, 26, 0}, {0x1101, 30, 27, 0}, {0x0AC1, 31, 28, 0}, {0x09C1, 32, 29, 0},
	{0x08A1, 33, 30, 0}, {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0}, {0x02A1, 36, 33, 0},
	{0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0}, {0x0085, 40, 37, 0},
	{0x0049, 41, 38, 0}, {0x0025, 42, 39, 0}, {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0},
	{0x0005, 45, 42, 0}, {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0},
};

struct TestMqContext {
	uint8_t index = 0u;
	uint8_t mps = 0u;
};

class TestMqEncoder {
public:
	void encode(TestMqContext& cx, uint8_t bit)
	{
		const TestMqQeEntry& entry = kTestMqQeTable[cx.index];
		a_ -= entry.qe;
		const bool lower_is_mps = (a_ >= entry.qe);
		const uint8_t lower_bit = lower_is_mps ? cx.mps : static_cast<uint8_t>(1u - cx.mps);

		if (bit == lower_bit) {
			if ((a_ & 0x8000u) == 0u) {
				if (a_ < entry.qe) {
					if (entry.sw != 0u) {
						cx.mps = static_cast<uint8_t>(1u - cx.mps);
					}
					cx.index = entry.nlps;
				} else {
					cx.index = entry.nmps;
				}
				renorme();
			}
		} else {
			add_to_bitstream(a_, total_shifts_);
			if (a_ < entry.qe) {
				cx.index = entry.nmps;
			} else {
				if (entry.sw != 0u) {
					cx.mps = static_cast<uint8_t>(1u - cx.mps);
				}
				cx.index = entry.nlps;
			}
			a_ = entry.qe;
			renorme();
		}
	}

	// ISO/IEC 14492 Annex A: Arithmetic Integer Encoding (small integers -3..3 or OOB).
	void encode_arith_int(std::array<TestMqContext, 512>& ctxs, int32_t val, bool is_oob = false)
	{
		uint32_t prev = 1u;
		auto put_bit = [&](uint8_t b) {
			encode(ctxs[prev & 0x1FFu], b);
			prev = (prev << 1u) | b;
			if (prev >= 0x200u) {
				prev = (prev & 0x1FFu) | 0x100u;
			}
		};
		if (is_oob) {
			put_bit(1u); // S = 1
			put_bit(0u); // class 0..3
			put_bit(0u); // v = 0
			put_bit(0u);
			return;
		}
		const uint8_t s = (val < 0) ? 1u : 0u;
		const uint32_t v = static_cast<uint32_t>(
			val < 0 ? -static_cast<int64_t>(val) : static_cast<int64_t>(val));
		put_bit(s);
		if (v < 4u) {
			put_bit(0u);
			put_bit(static_cast<uint8_t>((v >> 1u) & 1u));
			put_bit(static_cast<uint8_t>(v & 1u));
		} else if (v < 20u) {
			put_bit(1u);
			put_bit(0u);
			const uint32_t rem = v - 4u;
			for (int i = 3; i >= 0; --i) {
				put_bit(static_cast<uint8_t>((rem >> i) & 1u));
			}
		} else if (v < 84u) {
			put_bit(1u);
			put_bit(1u);
			put_bit(0u);
			const uint32_t rem = v - 20u;
			for (int i = 5; i >= 0; --i) {
				put_bit(static_cast<uint8_t>((rem >> i) & 1u));
			}
		} else if (v < 340u) {
			put_bit(1u);
			put_bit(1u);
			put_bit(1u);
			put_bit(0u);
			const uint32_t rem = v - 84u;
			for (int i = 7; i >= 0; --i) {
				put_bit(static_cast<uint8_t>((rem >> i) & 1u));
			}
		} else if (v < 4436u) {
			put_bit(1u);
			put_bit(1u);
			put_bit(1u);
			put_bit(1u);
			put_bit(0u);
			const uint32_t rem = v - 340u;
			for (int i = 11; i >= 0; --i) {
				put_bit(static_cast<uint8_t>((rem >> i) & 1u));
			}
		} else {
			for (int k = 0; k < 5; ++k) {
				put_bit(1u);
			}
			const uint32_t rem = v - 4436u;
			for (int i = 31; i >= 0; --i) {
				put_bit(static_cast<uint8_t>((rem >> i) & 1u));
			}
		}
	}

	void encode_iaid(std::vector<TestMqContext>& ctxs, uint32_t code_len, uint32_t sym_id)
	{
		uint32_t prev = 1u;
		for (uint32_t i = 0; i < code_len; ++i) {
			const uint8_t b = static_cast<uint8_t>((sym_id >> (code_len - 1u - i)) & 1u);
			encode(ctxs[prev & ((1u << code_len) - 1u)], b);
			prev = (prev << 1u) | b;
		}
	}

	[[nodiscard]] std::vector<uint8_t> finish(bool invert_bytes = true)
	{
		add_to_bitstream(a_ >> 1u, total_shifts_);
		const size_t min_bits = total_shifts_ + 32u;
		if (bits_.size() < min_bits) {
			bits_.resize(min_bits, 0u);
		}
		std::vector<uint8_t> out;
		size_t bit_idx = 0u;
		bool prev_ff = false;
		while (bit_idx < min_bits) {
			const uint32_t nbits = prev_ff ? 7u : 8u;
			uint8_t u_byte = 0u;
			for (uint32_t i = 0; i < nbits; ++i) {
				const uint8_t b = (bit_idx < bits_.size()) ? bits_[bit_idx++] : 0u;
				u_byte = static_cast<uint8_t>((u_byte << 1u) | (b & 1u));
			}
			uint8_t raw_byte = u_byte;
			if (invert_bytes) {
				raw_byte = prev_ff
					? static_cast<uint8_t>((u_byte ^ 0x7Fu) & 0x7Fu)
					: static_cast<uint8_t>(u_byte ^ 0xFFu);
			}
			out.push_back(raw_byte);
			prev_ff = (raw_byte == 0xFFu);
		}
		out.push_back(0xFFu);
		out.push_back(0xFFu);
		return out;
	}

private:
	void renorme() noexcept
	{
		do {
			a_ <<= 1u;
			++total_shifts_;
		} while ((a_ & 0x8000u) == 0u);
	}

	void add_to_bitstream(uint32_t val, size_t shift)
	{
		const size_t lsb_pos = shift + 14u;
		if (bits_.size() <= lsb_pos) {
			bits_.resize(lsb_pos + 1u, 0u);
		}
		uint32_t carry = 0u;
		const int max_k = (shift == 0u) ? 14 : 15;
		for (int k = 0; k <= max_k; ++k) {
			const size_t pos = lsb_pos - static_cast<size_t>(k);
			const uint32_t add_bit = (val >> k) & 1u;
			const uint32_t sum = static_cast<uint32_t>(bits_[pos]) + add_bit + carry;
			bits_[pos] = static_cast<uint8_t>(sum & 1u);
			carry = sum >> 1u;
		}
		size_t pos = lsb_pos - static_cast<size_t>(max_k);
		while (carry != 0u && pos > 0u) {
			--pos;
			const uint32_t sum = static_cast<uint32_t>(bits_[pos]) + carry;
			bits_[pos] = static_cast<uint8_t>(sum & 1u);
			carry = sum >> 1u;
		}
	}

	uint32_t a_ = 0x8000u;
	size_t total_shifts_ = 0u;
	std::vector<uint8_t> bits_;
};

// Encodes a template-2 arithmetic Generic Region into `enc`.
void encode_test_generic_region_t2(
	TestMqEncoder& enc,
	std::vector<TestMqContext>& gb_cx,
	uint32_t w,
	uint32_t h,
	int8_t atx0,
	int8_t aty0,
	const std::vector<uint8_t>& pixels_row_major)
{
	auto get_px = [&](int32_t x, int32_t y) -> uint32_t {
		if (x < 0 || x >= static_cast<int32_t>(w) || y < 0 || y >= static_cast<int32_t>(h)) {
			return 0u;
		}
		return pixels_row_major[static_cast<size_t>(y) * w + static_cast<size_t>(x)] ? 1u : 0u;
	};
	for (uint32_t y = 0; y < h; ++y) {
		const int32_t iy = static_cast<int32_t>(y);
		for (uint32_t x = 0; x < w; ++x) {
			const int32_t ix = static_cast<int32_t>(x);
			uint32_t ctx = 0u;
			ctx |= get_px(ix - 1, iy) << 0u;
			ctx |= get_px(ix - 2, iy) << 1u;
			ctx |= get_px(ix + atx0, iy + aty0) << 2u;
			ctx |= get_px(ix + 1, iy - 1) << 3u;
			ctx |= get_px(ix, iy - 1) << 4u;
			ctx |= get_px(ix - 1, iy - 1) << 5u;
			ctx |= get_px(ix - 2, iy - 1) << 6u;
			ctx |= get_px(ix + 1, iy - 2) << 7u;
			ctx |= get_px(ix, iy - 2) << 8u;
			ctx |= get_px(ix - 1, iy - 2) << 9u;
			enc.encode(gb_cx[ctx], static_cast<uint8_t>(get_px(ix, iy)));
		}
	}
}

void append_jbig2_segment(
	std::vector<uint8_t>& buf,
	uint32_t seg_num,
	uint8_t seg_type,
	std::span<const uint8_t> referred_to,
	uint8_t page_assoc,
	std::span<const uint8_t> payload)
{
	append_u32_be(buf, seg_num);
	buf.push_back(static_cast<uint8_t>(seg_type & 0x3Fu));
	buf.push_back(static_cast<uint8_t>((referred_to.size() & 0x07u) << 5u));
	for (uint8_t r : referred_to) {
		buf.push_back(r);
	}
	buf.push_back(page_assoc);
	append_u32_be(buf, static_cast<uint32_t>(payload.size()));
	buf.insert(buf.end(), payload.begin(), payload.end());
}

[[nodiscard]] std::vector<uint8_t> make_jbig2_page_info_payload(
	uint32_t width,
	uint32_t height,
	uint8_t flags = 0u)
{
	std::vector<uint8_t> p;
	append_u32_be(p, width);
	append_u32_be(p, height);
	append_u32_be(p, 72u);
	append_u32_be(p, 72u);
	p.push_back(flags);
	append_u16_be(p, 0u);
	return p;
}

} // namespace

TEST(PdfFilters, Jbig2Decode)
{
	// 1. Standalone JBIG2 file header + Page Information (48, 16x16) + Immediate Generic Region (38, MMR)
	// + End of Page (49) + End of File (51), matching the 16x16 Group 4 bitmap.
	{
		const std::string g4_16x16_bits =
			std::string("001") + "1011" + "000101" + "1" +
			"111" +
			"011" + "011" + "1" +
			"010" + "010" + "1" +
			"0001" + "1" +
			"11111111111" +
			"000000000001000000000001";
		const auto mmr_payload = pack_bitstring(g4_16x16_bits);

		std::vector<uint8_t> stream = {
			0x97u, 0x4Au, 0x42u, 0x32u, 0x0Du, 0x0Au, 0x1Au, 0x0Au,
			0x01u, // sequential organization, known page count
			0x00u, 0x00u, 0x00u, 0x01u, // 1 page
		};

		const auto page_info = make_jbig2_page_info_payload(16u, 16u, 0u);
		append_jbig2_segment(stream, 0u, 48u, {}, 1u, page_info);

		std::vector<uint8_t> gr_payload;
		append_u32_be(gr_payload, 16u); // width
		append_u32_be(gr_payload, 16u); // height
		append_u32_be(gr_payload, 0u);  // x
		append_u32_be(gr_payload, 0u);  // y
		gr_payload.push_back(0u);       // OR comb op
		gr_payload.push_back(0x01u);    // MMR = 1
		gr_payload.insert(gr_payload.end(), mmr_payload.begin(), mmr_payload.end());
		append_jbig2_segment(stream, 1u, 38u, {}, 1u, gr_payload);

		append_jbig2_segment(stream, 2u, 49u, {}, 1u, {}); // End of Page
		append_jbig2_segment(stream, 3u, 51u, {}, 0u, {}); // End of File

		auto dec = decode_jbig2(stream);
		RECTO_ASSERT_OK(dec);
		ASSERT_EQ(dec->size(), 32u);
		EXPECT_EQ((*dec)[0], 0xF0u);
		EXPECT_EQ((*dec)[1], 0x0Fu);
		EXPECT_EQ((*dec)[4], 0xF8u);
		EXPECT_EQ((*dec)[5], 0x07u);
		EXPECT_EQ((*dec)[8], 0xFFu);
		EXPECT_EQ((*dec)[9], 0xFFu);
	}

	// 2. Immediate Generic Region (38/39) using QM Arithmetic templates 0..3 and TPGDON.
	for (uint8_t tmpl = 0u; tmpl <= 3u; ++tmpl) {
		for (bool tpgdon : {false, true}) {
			std::vector<uint8_t> stream;
			const auto page_info = make_jbig2_page_info_payload(8u, 4u, 0u);
			append_jbig2_segment(stream, 0u, 48u, {}, 1u, page_info);

			std::vector<uint8_t> gr;
			append_u32_be(gr, 8u);
			append_u32_be(gr, 4u);
			append_u32_be(gr, 0u);
			append_u32_be(gr, 0u);
			gr.push_back(0u);
			const uint8_t gr_flags = static_cast<uint8_t>(
				((tmpl & 0x03u) << 1u) | (tpgdon ? 0x08u : 0x00u));
			gr.push_back(gr_flags);
			if (tmpl == 0u) {
				const int8_t at0[8] = {3, -1, -3, -1, 2, -2, -2, -2};
				gr.insert(
					gr.end(),
					reinterpret_cast<const uint8_t*>(at0),
					reinterpret_cast<const uint8_t*>(at0) + 8);
			} else {
				const int8_t at1[2] = {3, -1};
				gr.insert(
					gr.end(),
					reinterpret_cast<const uint8_t*>(at1),
					reinterpret_cast<const uint8_t*>(at1) + 2);
			}
			gr.insert(gr.end(), 16u, 0xFFu);
			append_jbig2_segment(stream, 1u, 39u, {}, 1u, gr);
			append_jbig2_segment(stream, 2u, 49u, {}, 1u, {});

			auto dec = decode_jbig2(stream);
			RECTO_ASSERT_OK(dec);
			ASSERT_EQ(dec->size(), 4u);
			if (tpgdon) {
				EXPECT_EQ((*dec)[0], 0xFFu);
				EXPECT_EQ((*dec)[2], (*dec)[1]);
			} else {
				EXPECT_EQ((*dec)[0], 0x00u);
			}
		}
	}

	// 3. Non-trivial Symbol Dictionary (segment 0) in /JBIG2Globals resolved via IndirectRef +
	// Text Region (segment 6) with 2 glyph instances and non-zero SBDSOFFSET +
	// multi-bitplane (GRAYMAX = 3, 2 bitplanes) Pattern Dictionary (segment 16) +
	// Halftone Region (segment 22) in both MMR and arithmetic modes.
	{
		// Build a globals stream containing:
		// - Segment 0: Symbol Dictionary (template 2, SDNUMEXSYMS = 2, SDNUMNEWSYMS = 2)
		//   exporting two 4x2 symbols:
		//   * Symbol 0 (4x2): row 0 = [1, 1, 0, 0], row 1 = [1, 1, 0, 0]
		//   * Symbol 1 (4x2): row 0 = [0, 0, 1, 1], row 1 = [0, 0, 1, 1]
		// - Segment 1: Pattern Dictionary (seg_type = 16, MMR = 1, 4x2 patterns, GRAYMAX = 3 -> 16x2 collective):
		//   * Pattern 0 (cols 0..3):   all 0 (white)
		//   * Pattern 1 (cols 4..7):   all 0 (white)
		//   * Pattern 2 (cols 8..11):  all 0 (white)
		//   * Pattern 3 (cols 12..15): all 1 (black)
		std::vector<uint8_t> globals_bytes;
		{
			TestMqEncoder sd_enc;
			std::vector<TestMqContext> gb_cx(1024u);
			std::array<TestMqContext, 512> iadh{};
			std::array<TestMqContext, 512> iadw{};
			std::array<TestMqContext, 512> iaex{};

			// Height class 1: delta_h = +2 -> hc_height = 2
			sd_enc.encode_arith_int(iadh, 2);
			// Symbol 0: delta_w = +4 -> sym_w = 4
			sd_enc.encode_arith_int(iadw, 4);
			const std::vector<uint8_t> sym0_px = {
				1u, 1u, 0u, 0u,
				1u, 1u, 0u, 0u,
			};
			encode_test_generic_region_t2(sd_enc, gb_cx, 4u, 2u, 2, -1, sym0_px);

			// Symbol 1: delta_w = 0 -> sym_w = 4
			sd_enc.encode_arith_int(iadw, 0);
			const std::vector<uint8_t> sym1_px = {
				0u, 0u, 1u, 1u,
				0u, 0u, 1u, 1u,
			};
			encode_test_generic_region_t2(sd_enc, gb_cx, 4u, 2u, 2, -1, sym1_px);

			// End of height class: IADW = OOB
			sd_enc.encode_arith_int(iadw, 0, /*is_oob=*/true);

			// IAEX export flags: skip 0 (ex_flag = 0), export 2 (ex_flag = 1)
			sd_enc.encode_arith_int(iaex, 0);
			sd_enc.encode_arith_int(iaex, 2);

			const auto sd_arith = sd_enc.finish();
			std::vector<uint8_t> sd_payload;
			append_u16_be(sd_payload, 0x0800u); // SDHUFF=0, SDREFAGG=0, SDTEMPLATE=2 (bits 10..11 = 2)
			sd_payload.push_back(2u);           // SDATX1 = 2
			sd_payload.push_back(static_cast<uint8_t>(-1)); // SDATY1 = -1
			append_u32_be(sd_payload, 2u); // SDNUMEXSYMS = 2
			append_u32_be(sd_payload, 2u); // SDNUMNEWSYMS = 2
			sd_payload.insert(sd_payload.end(), sd_arith.begin(), sd_arith.end());
			append_jbig2_segment(globals_bytes, 0u, 0u, {}, 0u, sd_payload);

			// Pattern dictionary: 4 patterns of 4x2 (collective 16x2, GRAYMAX = 3 -> 2 bitplanes!):
			// Patterns 0..2 (cols 0..11) are white (12W = 001000), Pattern 3 (cols 12..15) is black (4B = 011).
			// Row 0: H(001) + W12(001000) + B4(011); Row 1: V0(1) + V0(1); EOFB.
			const auto pat_mmr = pack_bitstring(
				std::string("001") + "001000" + "011" +
				"11" +
				"000000000001000000000001");
			std::vector<uint8_t> pd_payload;
			pd_payload.push_back(0x01u); // HDMMR = 1
			pd_payload.push_back(4u);    // HDPW = 4
			pd_payload.push_back(2u);    // HDPH = 2
			append_u32_be(pd_payload, 3u); // GRAYMAX = 3 (4 patterns -> gs_bpp = 2 bitplanes)
			pd_payload.insert(pd_payload.end(), pat_mmr.begin(), pat_mmr.end());
			append_jbig2_segment(globals_bytes, 1u, 16u, {}, 0u, pd_payload);
		}

		// Page stream (16x2 bitmap = 4 bytes):
		// - Segment 2: Page Information (16x2)
		// - Segment 3: Immediate Text Region (seg_type = 6, 8x2 at x=0, y=0, SBNUMINSTANCES = 2,
		//              REFCORNER = 1 (TOPLEFT), SBDSOFFSET = +2):
		//              * Instance 0: Symbol 0 (4x2, [1,1,0,0]) at x = 0 (curs becomes 0 + 4 - 1 = 3)
		//              * Instance 1: IDS = -1 -> curs = 3 + (-1) + SBDSOFFSET(2) = 4!
		//                Places Symbol 1 (4x2, [0,0,1,1]) at x = 4!
		//              Resulting 8x2 text region on cols 0..7: [1, 1, 0, 0, 0, 0, 1, 1] = 0xC3 in JBIG2 -> 0x3C in PDF!
		// - Segment 4: Immediate Halftone Region (seg_type = 22, 4x2 at x=8, y=0, MMR = 1, 2 bitplanes):
		//              Selects pattern 3 (Gray-code: bitplane 1 = 1, bitplane 0 = 0, since binary 3 = 11_2 -> Gray = 10_2!)
		//              Wait: Gray-to-binary is b_1 = g_1, b_0 = b_1 ^ g_0! So to get pattern 3 (b_1 = 1, b_0 = 1),
		//              Gray code is g_1 = 1, g_0 = 0!
		// - Segment 5: Immediate Halftone Region (seg_type = 22, 4x2 at x=12, y=0, MMR = 0 arithmetic, 2 bitplanes):
		//              Also selects pattern 3 (g_1 = 1, g_0 = 0) at x=12..15!
		std::vector<uint8_t> page_bytes;
		append_jbig2_segment(page_bytes, 2u, 48u, {}, 1u, make_jbig2_page_info_payload(16u, 2u, 0u));

		// Segment 3: Immediate Text Region
		{
			TestMqEncoder tr_enc;
			std::array<TestMqContext, 512> iadt{};
			std::array<TestMqContext, 512> iafs{};
			std::array<TestMqContext, 512> iads{};
			std::vector<TestMqContext> iaid(2u);

			tr_enc.encode_arith_int(iadt, 0); // initial stript = 0
			tr_enc.encode_arith_int(iadt, 0); // strip dt = 0 -> stript = 0
			tr_enc.encode_arith_int(iafs, 0); // dfs = 0 -> firsts = 0, curs = 0
			tr_enc.encode_iaid(iaid, 1u, 0u); // Instance 0: Symbol 0 at x = 0 (curs becomes 3)
			tr_enc.encode_arith_int(iads, -1); // IDS = -1 -> curs = 3 + (-1) + SBDSOFFSET(2) = 4
			tr_enc.encode_iaid(iaid, 1u, 1u); // Instance 1: Symbol 1 at x = 4
			const auto tr_arith = tr_enc.finish();

			std::vector<uint8_t> tr_payload;
			append_u32_be(tr_payload, 8u); // width = 8
			append_u32_be(tr_payload, 2u); // height = 2
			append_u32_be(tr_payload, 0u); // x = 0
			append_u32_be(tr_payload, 0u); // y = 0
			tr_payload.push_back(0u);      // OR comb op
			// sbr_flags: REFCORNER = 1 (bits 4..5 = 1 -> 0x0010), SBDSOFFSET = +2 (bits 10..14 = 2 -> 0x0800)
			append_u16_be(tr_payload, 0x0810u);
			append_u32_be(tr_payload, 2u); // SBNUMINSTANCES = 2
			tr_payload.insert(tr_payload.end(), tr_arith.begin(), tr_arith.end());
			const uint8_t ref0[1] = {0u};
			append_jbig2_segment(page_bytes, 3u, 6u, ref0, 1u, tr_payload);
		}

		// Segment 4: MMR Halftone Region (2 bitplanes: plane 1 = 1 [black], plane 0 = 0 [white] -> Gray 10_2 = binary 3)
		{
			// Plane 1 (1x1 = 1): H(001) + W0(00110101) + B1(010) + EOFB
			// Plane 0 (1x1 = 0): V0(1) + EOFB
			const auto grid_mmr_2planes = pack_bitstring(
				std::string("001") + "00110101" + "010" + "000000000001000000000001" +
				"1" + "000000000001000000000001");
			std::vector<uint8_t> ht_payload;
			append_u32_be(ht_payload, 4u); // width = 4
			append_u32_be(ht_payload, 2u); // height = 2
			append_u32_be(ht_payload, 8u); // x = 8
			append_u32_be(ht_payload, 0u); // y = 0
			ht_payload.push_back(0u);      // OR comb op
			ht_payload.push_back(0x01u);   // HTMMR = 1
			append_u32_be(ht_payload, 1u); // HGW = 1
			append_u32_be(ht_payload, 1u); // HGH = 1
			append_u32_be(ht_payload, 0u); // HGX = 0
			append_u32_be(ht_payload, 0u); // HGY = 0
			append_u16_be(ht_payload, 0x0400u); // HRX
			append_u16_be(ht_payload, 0u);      // HRY
			ht_payload.insert(ht_payload.end(), grid_mmr_2planes.begin(), grid_mmr_2planes.end());
			const uint8_t ref1[1] = {1u};
			append_jbig2_segment(page_bytes, 4u, 22u, ref1, 1u, ht_payload);
		}

		// Segment 5: Arithmetic Halftone Region (HTMMR = 0, HTTEMPLATE = 2 -> fixed AT {2, -1} with no AT header bytes)
		{
			TestMqEncoder ht_enc;
			std::vector<TestMqContext> ht_cx(1024u);
			// Plane 1 (1x1 = 1), Plane 0 (1x1 = 0) -> Gray 10_2 = binary 3 (Pattern 3 = all black)
			encode_test_generic_region_t2(ht_enc, ht_cx, 1u, 1u, 2, -1, {1u});
			encode_test_generic_region_t2(ht_enc, ht_cx, 1u, 1u, 2, -1, {0u});
			const auto ht_arith = ht_enc.finish();

			std::vector<uint8_t> ht_payload;
			append_u32_be(ht_payload, 4u);  // width = 4
			append_u32_be(ht_payload, 2u);  // height = 2
			append_u32_be(ht_payload, 12u); // x = 12
			append_u32_be(ht_payload, 0u);  // y = 0
			ht_payload.push_back(0u);       // OR comb op
			ht_payload.push_back(0x04u);    // HTMMR = 0, HTTEMPLATE = 2 (bits 1..2 = 2)
			append_u32_be(ht_payload, 1u);  // HGW = 1
			append_u32_be(ht_payload, 1u);  // HGH = 1
			append_u32_be(ht_payload, 0u);  // HGX = 0
			append_u32_be(ht_payload, 0u);  // HGY = 0
			append_u16_be(ht_payload, 0x0400u); // HRX
			append_u16_be(ht_payload, 0u);      // HRY
			ht_payload.insert(ht_payload.end(), ht_arith.begin(), ht_arith.end());
			const uint8_t ref1[1] = {1u};
			append_jbig2_segment(page_bytes, 5u, 22u, ref1, 1u, ht_payload);
		}

		append_jbig2_segment(page_bytes, 6u, 49u, {}, 1u, {});

		// Store /JBIG2Globals as an IndirectRef (42 0 R) and resolve it via IndirectObjectResolver!
		PdfDict parms;
		parms.set("JBIG2Globals", PdfValue(IndirectRef{42u, 0u}));

		PdfDict stm_dict;
		stm_dict.set("Filter", PdfValue(PdfName("JBIG2Decode")));
		stm_dict.set("DecodeParms", PdfValue(std::move(parms)));

		// Without a resolver, an unresolved IndirectRef in /JBIG2Globals must return DamagedFormat.
		RECTO_EXPECT_ERR(decode_stream(page_bytes, stm_dict), ErrorCode::DamagedFormat);

		const IndirectObjectResolver resolver = [&](IndirectRef ref) -> PdfResult<PdfValue> {
			if (ref.obj_num == 42u && ref.gen_num == 0u) {
				PdfStream globals_stm;
				globals_stm.raw_bytes = globals_bytes;
				return PdfValue(std::move(globals_stm));
			}
			return make_pdf_error(ErrorCode::DamagedFormat, "Unexpected object reference");
		};

		auto dec = decode_stream(page_bytes, stm_dict, kMaxDecodedStreamBytes, &resolver);
		RECTO_ASSERT_OK(dec);
		ASSERT_EQ(dec->size(), 4u); // 16x2 = 2 bytes per row * 2 rows = 4 bytes
		// Cols 0..7 (Text Region): [1, 1, 0, 0, 0, 0, 1, 1] in JBIG2 -> ~0xC3 = 0x3C in PDF DeviceGray
		// Cols 8..11 (MMR Halftone Pattern 3) + Cols 12..15 (Arith Halftone Pattern 3): all 1s -> 0x00 in PDF DeviceGray
		EXPECT_EQ((*dec)[0], 0x3Cu);
		EXPECT_EQ((*dec)[1], 0x00u);
		EXPECT_EQ((*dec)[2], 0x3Cu);
		EXPECT_EQ((*dec)[3], 0x00u);

		// A /JBIG2Globals stream with recursive /Filter /JBIG2Decode must be rejected with DamagedFormat.
		const IndirectObjectResolver recursive_resolver = [&](IndirectRef ref) -> PdfResult<PdfValue> {
			if (ref.obj_num == 42u && ref.gen_num == 0u) {
				PdfStream globals_stm;
				globals_stm.dict.set("Filter", PdfValue(PdfName("JBIG2Decode")));
				globals_stm.raw_bytes = globals_bytes;
				return PdfValue(std::move(globals_stm));
			}
			return make_pdf_error(ErrorCode::DamagedFormat, "Unexpected object reference");
		};
		RECTO_EXPECT_ERR(
			decode_stream(page_bytes, stm_dict, kMaxDecodedStreamBytes, &recursive_resolver),
			ErrorCode::DamagedFormat);
	}

	// 4. Reject malformed segment headers, truncated segment lengths, and indeterminate length on Page Info.
	{
		const std::array<uint8_t, 5> too_short = {0x00, 0x00, 0x00, 0x01, 0x30};
		RECTO_EXPECT_ERR(decode_jbig2(too_short), ErrorCode::DamagedFormat);

		// Segment claiming 1024 bytes when only 4 bytes follow.
		std::vector<uint8_t> trunc_seg;
		append_u32_be(trunc_seg, 0u);
		trunc_seg.push_back(48u);
		trunc_seg.push_back(0u);
		trunc_seg.push_back(1u);
		append_u32_be(trunc_seg, 1024u);
		trunc_seg.insert(trunc_seg.end(), 4u, 0u);
		RECTO_EXPECT_ERR(decode_jbig2(trunc_seg), ErrorCode::DamagedFormat);

		// Segment type 48 with indeterminate length 0xFFFFFFFF -> rejected.
		std::vector<uint8_t> indet_page_info;
		append_u32_be(indet_page_info, 0u);
		indet_page_info.push_back(48u);
		indet_page_info.push_back(0u);
		indet_page_info.push_back(1u);
		append_u32_be(indet_page_info, 0xFFFFFFFFu);
		RECTO_EXPECT_ERR(decode_jbig2(indet_page_info), ErrorCode::DamagedFormat);

		// Indeterminate-length Generic Region (seg_type = 38, len = 0xFFFFFFFF) shorter than 18 + AT + 6 trailer bytes -> rejected cleanly without underflow.
		std::vector<uint8_t> short_indet_gr;
		append_jbig2_segment(short_indet_gr, 0u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));
		append_u32_be(short_indet_gr, 1u);
		short_indet_gr.push_back(38u);
		short_indet_gr.push_back(0u);
		short_indet_gr.push_back(1u);
		append_u32_be(short_indet_gr, 0xFFFFFFFFu);
		short_indet_gr.insert(short_indet_gr.end(), 18u, 0u); // flags=0 (MMR=0, template=0 -> needs 18+8+6=32 bytes, ends with 00 00)
		short_indet_gr.push_back(0x00u);
		short_indet_gr.push_back(0x00u);
		append_u32_be(short_indet_gr, 2u);
		RECTO_EXPECT_ERR(decode_jbig2(short_indet_gr), ErrorCode::DamagedFormat);
	}

	// 5. Immediate Generic Refinement Region (seg_type = 42, template 1 + TPGRON) and
	// Huffman-coded Symbol Dictionary (sd_huff = 1) + Huffman-coded Text Region (sb_huff = 1).
	{
		// (a) Immediate Generic Refinement Region (seg_type = 42, template 1, TPGRON = 0)
		// refining an 8x2 page bitmap (initially row 0 = 0x0F in PDF, row 1 = 0xFF in PDF):
		// With an all-0xFF arithmetic stream (MPS = 0), every refined pixel is 0 (white -> 0xFF in PDF),
		// proving that segment 42 decodes via decode_jbig2_refinement_region and replaces the page region.
		std::vector<uint8_t> grr_stream;
		append_jbig2_segment(grr_stream, 0u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));

		// Segment 1: Immediate Generic Region (MMR) setting row 0 = 4 black, 4 white in JBIG2 (0x0F in PDF), row 1 = 8 white (0xFF in PDF).
		const auto mmr_8x2 = pack_bitstring(
			std::string("001") + "00110101" + "011" + "1011" + // Row 0: 0W, 4B, 4W
			"0001" + "1" +                                     // Row 1: Pass + V0 -> 8W
			"000000000001000000000001");
		std::vector<uint8_t> gr_payload;
		append_u32_be(gr_payload, 8u);
		append_u32_be(gr_payload, 2u);
		append_u32_be(gr_payload, 0u);
		append_u32_be(gr_payload, 0u);
		gr_payload.push_back(4u); // REPLACE
		gr_payload.push_back(0x01u); // MMR = 1
		gr_payload.insert(gr_payload.end(), mmr_8x2.begin(), mmr_8x2.end());
		append_jbig2_segment(grr_stream, 1u, 38u, {}, 1u, gr_payload);

		// Segment 2: Immediate Generic Refinement Region (seg_type = 42, referring to page bitmap, template 1, TPGRON = 0)
		// Refine the 8x2 page bitmap (initially {0x0F, 0xFF} in PDF) with {0xFF, 0xFF} MQ termination marker:
		// Under ISO/IEC 14492 Table E.1, state 0 has Qe = 0x5601 so the 2nd decode in the same context triggers
		// a conditional MPS/LPS exchange when C = 0, deterministically refining {0x0F, 0xFF} -> {0x16, 0x57}.
		std::vector<uint8_t> grr_payload;
		append_u32_be(grr_payload, 8u);
		append_u32_be(grr_payload, 2u);
		append_u32_be(grr_payload, 0u);
		append_u32_be(grr_payload, 0u);
		grr_payload.push_back(4u); // REPLACE
		grr_payload.push_back(0x01u); // GRTEMPLATE = 1 (bit 0 = 1), TPGRON = 0 (bit 1 = 0)
		grr_payload.push_back(0xFFu);
		grr_payload.push_back(0xFFu);
		append_jbig2_segment(grr_stream, 2u, 42u, {}, 1u, grr_payload);
		append_jbig2_segment(grr_stream, 3u, 49u, {}, 1u, {});

		auto dec_grr = decode_jbig2(grr_stream);
		RECTO_ASSERT_OK(dec_grr);
		ASSERT_EQ(dec_grr->size(), 2u);
		EXPECT_EQ((*dec_grr)[0], 0x16u);
		EXPECT_EQ((*dec_grr)[1], 0x57u);

		// (b) Huffman Symbol Dictionary (SDHUFF = 1, SDHUFFDH = 3 [custom seg 53], SDHUFFDW = 1 [B.3], SDHUFFBMSIZE = 0 [B.1])
		// + Huffman Text Region (SBHUFF = 1, SBHUFFFS = 1 [B.7], SBHUFFDS = 1 [B.9], SBHUFFDT = 1 [B.12])
		// with mandatory 35-runcode SBSYMCODES symbol ID Huffman table (ITU-T T.88 section 7.4.3.1.7).
		std::vector<uint8_t> huff_stream;
		append_jbig2_segment(huff_stream, 0u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));

		// Segment 1: Custom Huffman Table (seg_type = 53, ITU-T T.88 section 7.4.2):
		// HTOOB = 0, HTPS = 2, HTRS = 2 (ct_flags = 0x12), HTLOW = 1, HTHIGH = 3:
		// - Line 0 [1..2]: pref_len = 1 ("01"), range_len = 1 ("01") -> prefix "0" + 1-bit offset
		// - Lower line (<= 0): pref_len = 2 ("10")
		// - Upper line (>= 3): pref_len = 2 ("10")
		std::vector<uint8_t> ct_payload = {0x12u};
		append_u32_be(ct_payload, 1u); // HTLOW = 1
		append_u32_be(ct_payload, 3u); // HTHIGH = 3
		ct_payload.push_back(0x5Au);   // "01" + "01" + "10" + "10"
		append_jbig2_segment(huff_stream, 1u, 53u, {}, 1u, ct_payload);

		// Segment 2: Huffman Symbol Dictionary referencing custom table Segment 1 for SDHUFFDH (= 3)
		// and using Table B.3 for SDHUFFDW (= 1), exporting 2 symbols of width 4 and height 2:
		// - DH = +2 (custom table line 0: prefix "0" + 1-bit offset "1" = 2 bits) -> hc_height = 2
		// - Sym 0 DW = +4 (Table B.3: {3, 4, 3} prefix "1110" + 3-bit offset "001" = 7 bits) -> sym_w = 4
		// - Sym 1 DW = +0 (Table B.3: {0, 1, 0} prefix "0" = 1 bit) -> sym_w = 4
		// - DW = OOB (Table B.3: {0, 6, 0, oob} prefix "111110" = 6 bits) -> end of height class
		// - Collective BMSIZE = 0 (uncompressed 8x2 = 2 bytes: Table B.1 prefix "0" + 4-bit "0000" = 5 bits)
		// - Total header bits = 2 + 7 + 1 + 6 + 5 = 21 bits; pad with "000" -> 24 bits (3 bytes)
		// - Uncompressed 8x2 collective bitmap: row 0 = 0xAA, row 1 = 0x55 (16 bits)
		// - Export flags (Table B.1): skip 0 ("00000"), export 2 ("00010") + pad "000000"
		const auto sd_huff_bits = pack_bitstring(
			std::string("01") + "1110001" + "0" + "111110" + "00000" + "000" +
			"10101010" + "01010101" +
			"00000" + "00010" + "000000");
		std::vector<uint8_t> sd_huff_payload;
		// SDHUFF = 1 (bit 0), SDHUFFDH = 3 custom (bits 2..3 = 3), SDHUFFDW = 1 B.3 (bits 4..5 = 1) -> 0x001Du
		append_u16_be(sd_huff_payload, 0x001Du);
		append_u32_be(sd_huff_payload, 2u); // SDNUMEXSYMS = 2
		append_u32_be(sd_huff_payload, 2u); // SDNUMNEWSYMS = 2
		sd_huff_payload.insert(sd_huff_payload.end(), sd_huff_bits.begin(), sd_huff_bits.end());
		const uint8_t ref_ct[1] = {1u};
		append_jbig2_segment(huff_stream, 2u, 0u, ref_ct, 1u, sd_huff_payload);

		// Segment 3: Huffman Text Region (2 instances: Symbol 0 at x=0, Symbol 1 at x=4):
		// - Mandatory 35-runcode table (35 * 4 = 140 bits): runcode 1 has pref_len=1 ("0001"), all other 34 have pref_len=0 ("0000")
		// - 2 symbol code lengths using runcode 1 (prefix "0"): "0" (sym 0 len = 1) + "0" (sym 1 len = 1) = 2 bits
		// - Align to byte boundary: 140 + 2 = 142 bits -> pad with "00" (144 bits = 18 bytes)
		// - Initial STRIPT = 1 (Table B.12: prefix "0") -> stript = -1
		// - Strip DT = 1 (Table B.12: prefix "0") -> stript = -1 + 1 = 0
		// - First S (DFS = 0, Table B.7: {0, 4, 5} prefix "1011" + 5-bit "00000") -> firsts = 0, curs = 0
		// - Instance 0: Symbol ID 0 (prefix "0") -> draws at x=0..3, curs becomes 0 + (4 - 1) = 3
		// - Instance 1: IDS = 1 (Table B.9: {1, 3, 1} prefix "011" + 1-bit "0") -> curs becomes 3 + 1 = 4; Symbol ID 1 (prefix "1") -> draws at x=4..7
		std::string runcode_hdr = "0000" "0001";
		for (int i = 2; i < 35; ++i) {
			runcode_hdr += "0000";
		}
		runcode_hdr += "00" "00"; // two '0' bits for sym 0/1 lengths + two '0' bits byte-alignment padding
		const auto tr_huff_bits = pack_bitstring(
			runcode_hdr +
			"0" + "0" + "101100000" + "0" + "0110" + "1" + "000000");
		std::vector<uint8_t> tr_huff_payload;
		append_u32_be(tr_huff_payload, 8u);
		append_u32_be(tr_huff_payload, 2u);
		append_u32_be(tr_huff_payload, 0u);
		append_u32_be(tr_huff_payload, 0u);
		tr_huff_payload.push_back(0u); // OR
		append_u16_be(tr_huff_payload, 0x0011u); // SBHUFF = 1 (bit 0), REFCORNER = 1 TOPLEFT (bits 4..5 = 1)
		// SBHUFFFS = 1 [B.7] (bits 0..1 = 1), SBHUFFDS = 1 [B.9] (bits 2..3 = 1), SBHUFFDT = 1 [B.12] (bits 4..5 = 1) -> 0x0015u
		append_u16_be(tr_huff_payload, 0x0015u);
		append_u32_be(tr_huff_payload, 2u);      // SBNUMINSTANCES = 2
		tr_huff_payload.insert(tr_huff_payload.end(), tr_huff_bits.begin(), tr_huff_bits.end());
		const uint8_t ref_sd[1] = {2u};
		append_jbig2_segment(huff_stream, 3u, 6u, ref_sd, 1u, tr_huff_payload);
		append_jbig2_segment(huff_stream, 4u, 49u, {}, 1u, {});

		auto dec_huff = decode_jbig2(huff_stream);
		RECTO_ASSERT_OK(dec_huff);
		ASSERT_EQ(dec_huff->size(), 2u);
		// JBIG2 row 0 = 0xAA, row 1 = 0x55 -> PDF DeviceGray (0=black, 1=white): ~0xAA = 0x55, ~0x55 = 0xAA
		EXPECT_EQ((*dec_huff)[0], 0x55u);
		EXPECT_EQ((*dec_huff)[1], 0xAAu);

		// 5c. Multi-symbol refinement/aggregate coding (`SDREFAGG = 1`, `REFAGGNINST = 2`, ITU-T T.88 §6.5.8.2):
		// Verify that a Symbol Dictionary with `SDHUFF = 1` and `SDREFAGG = 1` aggregates 2 symbols from an imported
		// dictionary (`Segment 2` above: Symbol 0 = 4x2, Symbol 1 = 4x2) into a single 8x2 symbol via `decode_text_region`.
		std::vector<uint8_t> agg_stream;
		append_jbig2_segment(agg_stream, 0u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));
		append_jbig2_segment(agg_stream, 1u, 53u, {}, 1u, ct_payload);
		append_jbig2_segment(agg_stream, 2u, 0u, ref_ct, 1u, sd_huff_payload);

		// Segment 3: Symbol Dictionary referencing Segment 2 (2 imported symbols), producing 1 new symbol (8x2) with `REFAGGNINST = 2`:
		// - `SDHUFF = 1` (bit 0), `SDREFAGG = 1` (bit 1), `SDHUFFDH = 0` [B.4] (bits 2..3 = 0), `SDHUFFDW = 1` [B.3] (bits 4..5 = 1), `SDHUFFAGGINST = 0` [B.1] (bits 8..9 = 0) -> `0x0013u`
		// - DH = +2 (Table B.4: {2, 2, 0} prefix "10" = 2 bits) -> hc_height = 2
		// - Sym 0 DW = +8 (Table B.3: {3, 4, 3} prefix "1110" + 3-bit offset "101" = 7 bits) -> sym_w = 8
		// - REFAGGNINST = 2 (Table B.1: {0, 1, 4} prefix "0" + 4-bit offset "0010" = 5 bits)
		// - Because `REFAGGNINST > 1` (`2 > 1`) and `SDHUFF = 1` (ITU-T T.88 §6.5.8.2.1, Table 17 `REFCORNER = TOPLEFT`, & §6.5.8.2.3):
		//   - Fixed tables B.6 (FS), B.8 (DS), B.11 (DT), `REFCORNER = 1` (`TOPLEFT`), and fixed-length `sym_code_len = 2`-bit symbol IDs:
		//     - Initial STRIPT = 1 (Table B.11: `{1, 1, 0}` prefix `"0"`) -> stript = -1
		//     - Strip DT = 1 (Table B.11: `{1, 1, 0}` prefix `"0"`) -> stript = -1 + 1 = 0 (`TOPLEFT` top row of 8x2 symbol)
		//     - First S (DFS = 0, Table B.6: `{0, 2, 7}` prefix `"00"` + 7-bit `"0000000"`) -> firsts = 0, curs = 0
		//     - Instance 0: Symbol ID 0 (`"00"`, 2 bits), R_I = 0 (`"0"`) -> draws at x=0..3, y=0..1, curs becomes 0 + (4 - 1) = 3
		//     - Instance 1: IDS = 1 (Table B.8: `{0, 2, 1}` prefix `"00"` + 1-bit `"1"`) -> curs becomes 4; Symbol ID 1 (`"01"`, 2 bits), R_I = 0 (`"0"`) -> draws at x=4..7, y=0..1
		//     - Strip-ending IADS = OOB (Table B.8: `{0, 2, 0, 0, false, true}` prefix `"01"` = 2 bits)
		// - End of height class DW = OOB (Table B.3: prefix "111110" = 6 bits)
		// - Export flags (Table B.1): skip 2 imported ("00010", 5 bits), export 1 new ("00001", 5 bits) + pad `"0000"`
		const auto sd_agg_bits = pack_bitstring(
			std::string("10") + "1110101" + "00010" +
			"0" + "0" + "000000000" + "000" + "001" + "010" + "01" +
			"111110" + "00010" + "00001" + "0000");
		std::vector<uint8_t> sd_agg_payload;
		append_u16_be(sd_agg_payload, 0x0013u); // SDHUFF = 1, SDREFAGG = 1, SDHUFFDH = 0 (B.4), SDHUFFDW = 1 (B.3)
		sd_agg_payload.insert(sd_agg_payload.end(), {0u, 0u, 0u, 0u}); // 4 SDRAT bytes (SBRTEMPLATE = 0)
		append_u32_be(sd_agg_payload, 1u); // SDNUMEXSYMS = 1
		append_u32_be(sd_agg_payload, 1u); // SDNUMNEWSYMS = 1
		sd_agg_payload.insert(sd_agg_payload.end(), sd_agg_bits.begin(), sd_agg_bits.end());
		append_jbig2_segment(agg_stream, 3u, 0u, ref_sd, 1u, sd_agg_payload);

		// Segment 4: Text Region placing the single aggregated 8x2 symbol (ID 0, 1-bit symbol ID `"0"`) at (0, 0):
		std::string tr1_runcode = "0000" "0001";
		for (int i = 2; i < 35; ++i) {
			tr1_runcode += "0000";
		}
		tr1_runcode += "0" "000"; // 1 symbol length ('0' -> len 1) + 3 padding bits to align 141 -> 144 bits
		const auto tr1_bits = pack_bitstring(
			tr1_runcode +
			"0" + "0" + "101100000" + "0" + "0000");
		std::vector<uint8_t> tr1_payload;
		append_u32_be(tr1_payload, 8u);
		append_u32_be(tr1_payload, 2u);
		append_u32_be(tr1_payload, 0u);
		append_u32_be(tr1_payload, 0u);
		tr1_payload.push_back(0u);
		append_u16_be(tr1_payload, 0x0011u);
		append_u16_be(tr1_payload, 0x0015u);
		append_u32_be(tr1_payload, 1u); // SBNUMINSTANCES = 1
		tr1_payload.insert(tr1_payload.end(), tr1_bits.begin(), tr1_bits.end());
		const uint8_t ref_sd3[1] = {3u};
		append_jbig2_segment(agg_stream, 4u, 6u, ref_sd3, 1u, tr1_payload);
		append_jbig2_segment(agg_stream, 5u, 49u, {}, 1u, {});

		auto dec_agg = decode_jbig2(agg_stream);
		RECTO_ASSERT_OK(dec_agg);
		ASSERT_EQ(dec_agg->size(), 2u);
		EXPECT_EQ((*dec_agg)[0], 0x55u);
		EXPECT_EQ((*dec_agg)[1], 0xAAu);

		// 5d. Arithmetic multi-symbol aggregation (`SDHUFF = 0`, `SDREFAGG = 1`, `REFAGGNINST = 2`, ITU-T T.88 Table 17):
		// Verify that `REFCORNER = TOPLEFT` (`stript = 0` draws at `y = 0`) and strip-ending `IADS` OOB is consumed before `IADW = OOB` and `IAEX`.
		{
			TestMqEncoder arith_agg_enc;
			std::array<TestMqContext, 512> iadh{};
			std::array<TestMqContext, 512> iadw{};
			std::array<TestMqContext, 512> iaex{};
			std::array<TestMqContext, 512> iaai{};
			std::array<TestMqContext, 512> iadt{};
			std::array<TestMqContext, 512> iafs{};
			std::array<TestMqContext, 512> iads{};
			std::array<TestMqContext, 512> iari{};
			std::vector<TestMqContext> iaid(4u); // sym_code_len = 2 for 2 imported + 1 new = 3 total symbols

			arith_agg_enc.encode_arith_int(iadh, 2); // DH = +2 -> hc_height = 2
			arith_agg_enc.encode_arith_int(iadw, 8); // DW = +8 -> sym_w = 8
			arith_agg_enc.encode_arith_int(iaai, 2); // REFAGGNINST = 2
			arith_agg_enc.encode_arith_int(iadt, 0); // initial stript = 0
			arith_agg_enc.encode_arith_int(iadt, 0); // strip dt = 0 -> stript = 0 (TOPLEFT)
			arith_agg_enc.encode_arith_int(iafs, 0); // dfs = 0 -> firsts = 0, curs = 0
			arith_agg_enc.encode_iaid(iaid, 2u, 0u); // Instance 0: Symbol 0 (4x2)
			arith_agg_enc.encode_arith_int(iari, 0); // R_I = 0
			arith_agg_enc.encode_arith_int(iads, 1); // IDS = 1 -> curs = 3 + 1 = 4
			arith_agg_enc.encode_iaid(iaid, 2u, 1u); // Instance 1: Symbol 1 (4x2)
			arith_agg_enc.encode_arith_int(iari, 0); // R_I = 0
			arith_agg_enc.encode_arith_int(iads, 0, /*is_oob=*/true); // Strip-ending IADS OOB
			arith_agg_enc.encode_arith_int(iadw, 0, /*is_oob=*/true); // End of height class IADW OOB
			arith_agg_enc.encode_arith_int(iaex, 2); // skip 2 imported
			arith_agg_enc.encode_arith_int(iaex, 1); // export 1 new
			const auto arith_agg_bytes = arith_agg_enc.finish();

			std::vector<uint8_t> sd_arith_agg_payload;
			append_u16_be(sd_arith_agg_payload, 0x1802u); // SDHUFF = 0, SDREFAGG = 1, SDTEMPLATE = 2 (bits 10..11), SDRTEMPLATE = 1 (bit 12 -> 0 SDRAT bytes)
			sd_arith_agg_payload.push_back(2u);
			sd_arith_agg_payload.push_back(static_cast<uint8_t>(-1));
			append_u32_be(sd_arith_agg_payload, 1u); // SDNUMEXSYMS = 1
			append_u32_be(sd_arith_agg_payload, 1u); // SDNUMNEWSYMS = 1
			sd_arith_agg_payload.insert(sd_arith_agg_payload.end(), arith_agg_bytes.begin(), arith_agg_bytes.end());

			std::vector<uint8_t> arith_agg_stream;
			append_jbig2_segment(arith_agg_stream, 0u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));
			append_jbig2_segment(arith_agg_stream, 1u, 53u, {}, 1u, ct_payload);
			append_jbig2_segment(arith_agg_stream, 2u, 0u, ref_ct, 1u, sd_huff_payload);
			append_jbig2_segment(arith_agg_stream, 3u, 0u, ref_sd, 1u, sd_arith_agg_payload);
			append_jbig2_segment(arith_agg_stream, 4u, 6u, ref_sd3, 1u, tr1_payload);
			append_jbig2_segment(arith_agg_stream, 5u, 49u, {}, 1u, {});

			auto dec_arith_agg = decode_jbig2(arith_agg_stream);
			RECTO_ASSERT_OK(dec_arith_agg);
			ASSERT_EQ(dec_arith_agg->size(), 2u);
			EXPECT_EQ((*dec_arith_agg)[0], 0x55u);
			EXPECT_EQ((*dec_arith_agg)[1], 0xAAu);
		}

		// 5e. Huffman Text Region refinement (`SBHUFF = 1`, `SBREFINE = 1`, ITU-T T.88 §7.4.3.2) preserving `huff_gr_cx` across refined instances
		// + signed integer overflow regression check in JBIG2 refinement (`rdw = INT32_MAX` rejected cleanly with DamagedFormat):
		{
			// Build a Huffman Text Region with 2 refined instances of Symbol 0 (4x2) at x=0 and x=4:
			// Each instance has:
			// - R_I = 1 ("1")
			// - rdw = 0, rdh = 0, rdx = 0, rdy = 0 (Table B.14: {0, 1, 0} -> "0" each = "0000")
			// - rsize = 2 (Table B.1: "00010", 5 bits) + byte alignment + 2 bytes {0xFF, 0xFF} (16 bits of 1s)
			// Because `huff_gr_cx` is shared across both refined instances in the segment, the 2nd instance starts with the evolved
			// MqContext states from the 1st instance, so its 4-bit row output (low nibble of byte) differs from the 1st instance's (high nibble)!
			const auto tr_huff_ref_bits = pack_bitstring(
				runcode_hdr +
				"0" + "0" + "101100000" + // dt_init=1, dt=1 -> stript=0, dfs=0 -> firsts=0
				"0" + "1" + "0000" + "00010" + "00000" + // sym 0 ("0"), R_I=1 ("1"), rdw/rdh/rdx/rdy=0 ("0000"), rsize=2 ("00010") + 5 pad bits to byte-align (22 bits -> 24 bits)
				"1111111111111111" +                     // 2-byte refinement stream {0xFF, 0xFF}
				"0110" + "0" + "1" + "0000" + "00010" +  // IDS=1 (Table B.9: "0110"), sym 0 ("0"), R_I=1 ("1"), rdw/rdh/rdx/rdy=0 ("0000"), rsize=2 ("00010") -> 15 bits + 1 pad bit -> 16 bits
				"0" +
				"1111111111111111");                     // 2-byte refinement stream {0xFF, 0xFF}
			std::vector<uint8_t> tr_huff_ref_payload;
			append_u32_be(tr_huff_ref_payload, 8u);
			append_u32_be(tr_huff_ref_payload, 2u);
			append_u32_be(tr_huff_ref_payload, 0u);
			append_u32_be(tr_huff_ref_payload, 0u);
			tr_huff_ref_payload.push_back(0u);
			append_u16_be(tr_huff_ref_payload, 0x8013u); // SBHUFF = 1, SBREFINE = 1, REFCORNER = 1 (TOPLEFT), SBRTEMPLATE = 1 (0 AT bytes)
			append_u16_be(tr_huff_ref_payload, 0x0015u); // FS=B.7 (1), DS=B.9 (1), DT=B.12 (1), RDW/RDH/RDX/RDY=B.14 (0), RSIZE=B.1 (0)
			append_u32_be(tr_huff_ref_payload, 2u); // SBNUMINSTANCES = 2
			tr_huff_ref_payload.insert(tr_huff_ref_payload.end(), tr_huff_ref_bits.begin(), tr_huff_ref_bits.end());

			std::vector<uint8_t> huff_ref_stream;
			append_jbig2_segment(huff_ref_stream, 0u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));
			append_jbig2_segment(huff_ref_stream, 1u, 53u, {}, 1u, ct_payload);
			append_jbig2_segment(huff_ref_stream, 2u, 0u, ref_ct, 1u, sd_huff_payload);
			append_jbig2_segment(huff_ref_stream, 3u, 6u, ref_sd, 1u, tr_huff_ref_payload);
			append_jbig2_segment(huff_ref_stream, 4u, 49u, {}, 1u, {});

			auto dec_huff_ref = decode_jbig2(huff_ref_stream);
			RECTO_ASSERT_OK(dec_huff_ref);
			ASSERT_EQ(dec_huff_ref->size(), 2u);
			// Verify the 2nd instance (low nibble) evolved differently from the 1st instance (high nibble) due to shared huff_gr_cx!
			EXPECT_NE(((*dec_huff_ref)[0] >> 4u) & 0x0Fu, (*dec_huff_ref)[0] & 0x0Fu);

			TestMqEncoder ovf_enc;
			std::array<TestMqContext, 512> iadt{};
			std::array<TestMqContext, 512> iafs{};
			std::array<TestMqContext, 512> iari{};
			std::array<TestMqContext, 512> iardw{};
			std::array<TestMqContext, 512> iardh{};
			std::array<TestMqContext, 512> iardx{};
			std::array<TestMqContext, 512> iardy{};
			std::vector<TestMqContext> iaid(2u);

			ovf_enc.encode_arith_int(iadt, 0);
			ovf_enc.encode_arith_int(iadt, 0);
			ovf_enc.encode_arith_int(iafs, 0);
			ovf_enc.encode_iaid(iaid, 1u, 0u);
			ovf_enc.encode_arith_int(iari, 1); // R_I = 1
			ovf_enc.encode_arith_int(iardw, std::numeric_limits<int32_t>::max()); // rdw = INT32_MAX
			ovf_enc.encode_arith_int(iardh, 0);
			ovf_enc.encode_arith_int(iardx, 0);
			ovf_enc.encode_arith_int(iardy, 0);
			const auto ovf_bytes = ovf_enc.finish();

			std::vector<uint8_t> tr_ovf_payload;
			append_u32_be(tr_ovf_payload, 8u);
			append_u32_be(tr_ovf_payload, 2u);
			append_u32_be(tr_ovf_payload, 0u);
			append_u32_be(tr_ovf_payload, 0u);
			tr_ovf_payload.push_back(0u);
			append_u16_be(tr_ovf_payload, 0x8012u); // SBHUFF = 0, SBREFINE = 1 (bit 1), REFCORNER = 1, SBRTEMPLATE = 1 (bit 15)
			append_u32_be(tr_ovf_payload, 1u);
			tr_ovf_payload.insert(tr_ovf_payload.end(), ovf_bytes.begin(), ovf_bytes.end());

			std::vector<uint8_t> ovf_stream;
			append_jbig2_segment(ovf_stream, 0u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));
			append_jbig2_segment(ovf_stream, 1u, 53u, {}, 1u, ct_payload);
			append_jbig2_segment(ovf_stream, 2u, 0u, ref_ct, 1u, sd_huff_payload);
			append_jbig2_segment(ovf_stream, 3u, 6u, ref_sd, 1u, tr_ovf_payload);
			RECTO_EXPECT_ERR(decode_jbig2(ovf_stream), ErrorCode::DamagedFormat);
		}
	}
}

TEST(PdfFilters, ConcurrentWicDecode)
{
	// Extract the 8x8 RGB JPEG stream (object 7 0 R) from render/images-and-vectors.pdf.
	const auto pdf_bytes = test::read_file_bytes(
		test::corpus_path("render/images-and-vectors.pdf"));
	auto xref_res = XrefTable::parse(pdf_bytes);
	RECTO_ASSERT_OK(xref_res);
	auto stm_obj = xref_res->resolve_object(pdf_bytes, IndirectRef{7u, 0u});
	RECTO_ASSERT_OK(stm_obj);
	ASSERT_TRUE(stm_obj->is_stream());
	const PdfStream& jpeg_stm = *stm_obj->as_stream();

	auto baseline = decode_stream(jpeg_stm);
	RECTO_ASSERT_OK(baseline);
	ASSERT_EQ(baseline->size(), 8u * 8u * 3u);

	constexpr size_t kNumThreads = 8;
	constexpr size_t kItersPerThread = 16;
	std::latch start_latch(kNumThreads);
	std::latch factory_latch(kNumThreads);
	std::array<uintptr_t, kNumThreads> factory_ids{};
	std::array<bool, kNumThreads> thread_ok{};
	std::vector<std::thread> workers;
	workers.reserve(kNumThreads);

	for (size_t t = 0; t < kNumThreads; ++t) {
		workers.emplace_back([&, t]() {
			start_latch.arrive_and_wait();
			factory_ids[t] = current_thread_wic_factory_id();
			// Keep all threads alive until every thread has sampled its thread-local
			// IWICImagingFactory pointer so COM addresses cannot be recycled after thread exit.
			factory_latch.arrive_and_wait();

			bool ok = (factory_ids[t] != 0u);
			for (size_t iter = 0; iter < kItersPerThread && ok; ++iter) {
				auto res = decode_stream(jpeg_stm);
				if (!res.has_value() || *res != *baseline) {
					ok = false;
				}
			}
			thread_ok[t] = ok;
		});
	}

	for (auto& w : workers) {
		w.join();
	}

	for (size_t t = 0; t < kNumThreads; ++t) {
		EXPECT_TRUE(thread_ok[t]) << "worker thread " << t << " failed concurrent WIC decode";
		EXPECT_NE(factory_ids[t], 0u);
		for (size_t u = t + 1; u < kNumThreads; ++u) {
			EXPECT_NE(factory_ids[t], factory_ids[u])
				<< "threads " << t << " and " << u << " unexpectedly shared an IWICImagingFactory";
		}
	}
}

namespace {

// Builds a minimal deterministic 8x8 4-component CMYK baseline JPEG decodable by WIC.
[[nodiscard]] std::vector<uint8_t> make_minimal_jpeg_8x8_cmyk()
{
	std::vector<uint8_t> out = {0xFFu, 0xD8u};
	auto append_seg = [&](uint8_t marker, std::span<const uint8_t> payload) {
		out.push_back(0xFFu);
		out.push_back(marker);
		append_u16_be(out, static_cast<uint16_t>(payload.size() + 2u));
		out.insert(out.end(), payload.begin(), payload.end());
	};

	// DQT: table 0 = all 16s
	std::vector<uint8_t> dqt(65u, 16u);
	dqt[0] = 0u;
	append_seg(0xDBu, dqt);

	// SOF0: 8-bit, 8x8, 4 components (C=1, M=2, Y=3, K=4), 1x1 sampling, quant table 0
	const std::array<uint8_t, 18> sof0 = {
		8u, 0u, 8u, 0u, 8u, 4u,
		1u, 0x11u, 0u,
		2u, 0x11u, 0u,
		3u, 0x11u, 0u,
		4u, 0x11u, 0u,
	};
	append_seg(0xC0u, sof0);

	// DHT: DC table 0 and AC table 0, each mapping 1-bit code '0' -> symbol 0x00
	std::vector<uint8_t> dht;
	for (uint8_t tc_id : {uint8_t{0x00u}, uint8_t{0x10u}}) {
		dht.push_back(tc_id);
		dht.push_back(1u);
		dht.insert(dht.end(), 15u, 0u);
		dht.push_back(0x00u);
	}
	append_seg(0xC4u, dht);

	// SOS: 4 components (all using DC0/AC0), Ss=0, Se=63, Ah/Al=0
	const std::array<uint8_t, 12> sos = {
		4u,
		1u, 0x00u,
		2u, 0x00u,
		3u, 0x00u,
		4u, 0x00u,
		0u, 63u, 0u,
	};
	append_seg(0xDAu, sos);

	// 1 MCU of 4 blocks * 2 zero bits (DC=0, AC=EOB) = 8 zero bits -> 0x00, 0x7F.
	out.push_back(0x00u);
	out.push_back(0x7Fu);
	out.push_back(0xFFu);
	out.push_back(0xD9u);
	return out;
}

// Encodes a 1x1 EBCOT code-block with a single signed coefficient `val` (at bitplane 0,
// `mb_total = 9`, `zero_bitplanes = 8 - top_bp`) using ITU-T T.800 Tier-1 MQ arithmetic coding.
struct EncodedJ2kSubband {
	uint32_t zero_bitplanes = 0u;
	uint32_t num_passes = 1u;
	std::vector<uint8_t> bytes;
};

[[nodiscard]] EncodedJ2kSubband encode_j2k_1x1_subband(int32_t val, uint32_t mb_total = 9u)
{
	EncodedJ2kSubband res{};
	std::array<TestMqContext, 19> ctxs{};
	ctxs[0].index = 4u;
	ctxs[17].index = 3u;
	ctxs[18].index = 46u;
	TestMqEncoder enc;

	if (val == 0) {
		res.zero_bitplanes = mb_total - 1u;
		res.num_passes = 1u;
		enc.encode(ctxs[0], 0u); // CUP: insignificant
		res.bytes = enc.finish();
		return res;
	}

	const uint8_t sgn = (val < 0) ? 1u : 0u;
	const uint32_t mag = static_cast<uint32_t>(val < 0 ? -val : val);
	const uint32_t top_bp = static_cast<uint32_t>(std::bit_width(mag) - 1u);
	res.zero_bitplanes = (mb_total - 1u) - top_bp;
	res.num_passes = 1u + 3u * top_bp;

	// First pass: Cleanup pass on `top_bp` -> becomes significant (ZC ctx 0 = 1), then sign (SC ctx 9, xor 0 = sgn).
	enc.encode(ctxs[0], 1u);
	enc.encode(ctxs[9], sgn);

	// Remaining bitplanes `bp = top_bp - 1` down to `0`: SPP (empty), MRP (ctx 14 first time, then ctx 16), CUP (empty).
	bool first_mr = true;
	for (int32_t bp = static_cast<int32_t>(top_bp) - 1; bp >= 0; --bp) {
		const uint8_t bit = static_cast<uint8_t>((mag >> bp) & 1u);
		enc.encode(ctxs[first_mr ? 14u : 16u], bit);
		first_mr = false;
	}
	res.bytes = enc.finish();
	return res;
}

class TestBitWriter {
public:
	void write_bit(uint32_t b)
	{
		cur_ = static_cast<uint8_t>((cur_ << 1u) | (b & 1u));
		++count_;
		const uint32_t limit = prev_ff_ ? 7u : 8u;
		if (count_ == limit) {
			bytes_.push_back(cur_);
			prev_ff_ = (cur_ == 0xFFu);
			cur_ = 0u;
			count_ = 0u;
		}
	}

	void write_bits(uint32_t val, uint32_t n)
	{
		for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
			write_bit((val >> i) & 1u);
		}
	}

	void encode_num_passes(uint32_t n)
	{
		if (n == 1u) {
			write_bit(0u);
		} else if (n == 2u) {
			write_bits(0b10u, 2u);
		} else if (n <= 5u) {
			write_bits(0b11u, 2u);
			write_bits(n - 3u, 2u);
		} else if (n <= 36u) {
			write_bits(0b1111u, 4u);
			write_bits(n - 6u, 5u);
		} else {
			write_bits(0b111111111u, 9u);
			write_bits(n - 37u, 7u);
		}
	}

	void flush_aligned()
	{
		if (count_ > 0u) {
			const uint32_t limit = prev_ff_ ? 7u : 8u;
			cur_ = static_cast<uint8_t>(cur_ << (limit - count_));
			bytes_.push_back(cur_);
			prev_ff_ = (cur_ == 0xFFu);
			cur_ = 0u;
			count_ = 0u;
		}
		if (prev_ff_) {
			bytes_.push_back(0x00u);
			prev_ff_ = false;
		}
	}

	[[nodiscard]] const std::vector<uint8_t>& bytes() const noexcept
	{
		return bytes_;
	}

private:
	std::vector<uint8_t> bytes_;
	uint8_t cur_ = 0u;
	uint32_t count_ = 0u;
	bool prev_ff_ = false;
};

[[nodiscard]] std::vector<uint8_t> build_j2k_packet_for_subbands(
	std::span<const EncodedJ2kSubband> sbs)
{
	TestBitWriter bw;
	bw.write_bit(1u); // Non-empty packet
	for (const auto& sb : sbs) {
		bw.write_bit(1u); // Inclusion tag-tree (1x1): included in layer 0
		for (uint32_t z = 0; z < sb.zero_bitplanes; ++z) {
			bw.write_bit(0u);
		}
		bw.write_bit(1u); // Terminate zero-bitplane tag-tree
		bw.encode_num_passes(sb.num_passes);
		// Increase Lblock from 3 to 8 (five '1' bits + '0') so length fits comfortably.
		for (int i = 0; i < 5; ++i) {
			bw.write_bit(1u);
		}
		bw.write_bit(0u);
		const uint32_t pass_log2 =
			(sb.num_passes <= 1u) ? 0u : static_cast<uint32_t>(std::bit_width(sb.num_passes) - 1u);
		const uint32_t len_bits = 8u + pass_log2;
		bw.write_bits(static_cast<uint32_t>(sb.bytes.size()), len_bits);
	}
	bw.flush_aligned();
	std::vector<uint8_t> pkt = bw.bytes();
	for (const auto& sb : sbs) {
		pkt.insert(pkt.end(), sb.bytes.begin(), sb.bytes.end());
	}
	return pkt;
}

// Forward 1D 5-3 reversible DWT for a 2-sample signal:
// d[0] = x[1] - x[0], s[0] = x[0] + floor(d[0] / 2).
void fdwt_53_2x2(const std::array<int32_t, 4>& img, int32_t& ll, int32_t& hl, int32_t& lh, int32_t& hh)
{
	// Horizontal analysis on row 0 and row 1
	const int32_t d0 = img[1] - img[0];
	const int32_t s0 = img[0] + (d0 >> 1);
	const int32_t d1 = img[3] - img[2];
	const int32_t s1 = img[2] + (d1 >> 1);
	// Vertical analysis on col 0 (s0, s1) and col 1 (d0, d1)
	lh = s1 - s0;
	ll = s0 + (lh >> 1);
	hh = d1 - d0;
	hl = d0 + (hh >> 1);
}

// Builds a genuine ITU-T T.800 2x2 3-component JP2 container + J2K codestream
// using 1-level 5-3 reversible DWT (`num_decomp_levels = 1`), RCT (`enable_mct`),
// Tier-2 packet headers with tag-trees, and Tier-1 EBCOT MQ-coded code-blocks.
[[nodiscard]] std::vector<uint8_t> make_genuine_jp2_2x2_rgb(
	std::span<const uint8_t, 12> rgb_samples,
	bool enable_mct = true)
{
	std::array<std::array<int32_t, 4>, 3> comp{};
	for (size_t p = 0; p < 4; ++p) {
		const int32_t r = static_cast<int32_t>(rgb_samples[p * 3 + 0]) - 128;
		const int32_t g = static_cast<int32_t>(rgb_samples[p * 3 + 1]) - 128;
		const int32_t b = static_cast<int32_t>(rgb_samples[p * 3 + 2]) - 128;
		if (enable_mct) {
			// Forward RCT (ITU-T T.800 Annex G.2)
			comp[0][p] = (r + 2 * g + b) >> 2;
			comp[1][p] = b - g;
			comp[2][p] = r - g;
		} else {
			comp[0][p] = r;
			comp[1][p] = g;
			comp[2][p] = b;
		}
	}

	std::vector<uint8_t> tile_packets;
	// LRCP progression: 1 layer, resolutions r = 0..1, components c = 0..2
	std::array<int32_t, 3> ll{}, hl{}, lh{}, hh{};
	for (int c = 0; c < 3; ++c) {
		fdwt_53_2x2(comp[c], ll[c], hl[c], lh[c], hh[c]);
	}
	// Resolution 0 packets (LL1 for each component)
	for (int c = 0; c < 3; ++c) {
		const EncodedJ2kSubband sb_ll = encode_j2k_1x1_subband(ll[c], 9u);
		const auto pkt = build_j2k_packet_for_subbands(std::span<const EncodedJ2kSubband>(&sb_ll, 1u));
		tile_packets.insert(tile_packets.end(), pkt.begin(), pkt.end());
	}
	// Resolution 1 packets (HL1, LH1, HH1 for each component)
	for (int c = 0; c < 3; ++c) {
		const std::array<EncodedJ2kSubband, 3> sbs = {
			encode_j2k_1x1_subband(hl[c], 9u),
			encode_j2k_1x1_subband(lh[c], 9u),
			encode_j2k_1x1_subband(hh[c], 9u),
		};
		const auto pkt = build_j2k_packet_for_subbands(sbs);
		tile_packets.insert(tile_packets.end(), pkt.begin(), pkt.end());
	}

	std::vector<uint8_t> cs;
	cs.push_back(0xFFu);
	cs.push_back(0x4Fu); // SOC

	// SIZ (0xFF51): Lsiz = 47
	cs.push_back(0xFFu);
	cs.push_back(0x51u);
	append_u16_be(cs, 47u);
	append_u16_be(cs, 0u);  // Rsiz
	append_u32_be(cs, 2u);  // Xsiz = 2
	append_u32_be(cs, 2u);  // Ysiz = 2
	append_u32_be(cs, 0u);  // XOsiz = 0
	append_u32_be(cs, 0u);  // YOsiz = 0
	append_u32_be(cs, 2u);  // XTsiz = 2
	append_u32_be(cs, 2u);  // YTsiz = 2
	append_u32_be(cs, 0u);  // XTOsiz = 0
	append_u32_be(cs, 0u);  // YTOsiz = 0
	append_u16_be(cs, 3u);  // Csiz = 3
	for (int c = 0; c < 3; ++c) {
		cs.push_back(7u); // Ssiz = 8-bit unsigned (7)
		cs.push_back(1u); // XRsiz = 1
		cs.push_back(1u); // YRsiz = 1
	}

	// COD (0xFF52): Lcod = 12, 1 decomposition level, 5-3 reversible DWT
	cs.push_back(0xFFu);
	cs.push_back(0x52u);
	append_u16_be(cs, 12u);
	cs.push_back(0u); // Scod
	cs.push_back(0u); // SGcod progression = LRCP
	append_u16_be(cs, 1u); // 1 layer
	cs.push_back(enable_mct ? 1u : 0u); // MCT flag
	cs.push_back(1u); // 1 DWT decomposition level
	cs.push_back(4u); // code-block width exp (64)
	cs.push_back(4u); // code-block height exp (64)
	cs.push_back(0u); // code-block style
	cs.push_back(1u); // 5-3 reversible wavelet

	// QCD (0xFF5C): Lqcd = 3 + 4 = 7 (guard_bits = 2, eps = 8 -> mb_total = 8 + 2 - 1 = 9 for all 4 subbands)
	cs.push_back(0xFFu);
	cs.push_back(0x5Cu);
	append_u16_be(cs, 7u);
	cs.push_back(0x40u); // Sqcd: guard_bits = 2, qstyle = 0
	for (int sb = 0; sb < 4; ++sb) {
		cs.push_back(static_cast<uint8_t>(8u << 3u)); // eps = 8
	}

	// SOT (0xFF90) + SOD (0xFF93) + tile_packets + EOC (0xFFD9)
	cs.push_back(0xFFu);
	cs.push_back(0x90u);
	append_u16_be(cs, 10u);
	append_u16_be(cs, 0u);
	append_u32_be(cs, 14u + static_cast<uint32_t>(tile_packets.size()));
	cs.push_back(0u);
	cs.push_back(1u);

	cs.push_back(0xFFu);
	cs.push_back(0x93u);
	cs.insert(cs.end(), tile_packets.begin(), tile_packets.end());
	cs.push_back(0xFFu);
	cs.push_back(0xD9u);

	// Wrap in JP2 container: signature box + ihdr inside jp2h + jp2c
	std::vector<uint8_t> jp2 = {
		0x00u, 0x00u, 0x00u, 0x0Cu,
		0x6Au, 0x50u, 0x20u, 0x20u,
		0x0Du, 0x0Au, 0x87u, 0x0Au,
	};
	append_u32_be(jp2, 30u);
	append_u32_be(jp2, 0x6A703268u); // 'jp2h'
	append_u32_be(jp2, 22u);
	append_u32_be(jp2, 0x69686472u); // 'ihdr'
	append_u32_be(jp2, 2u);          // height = 2
	append_u32_be(jp2, 2u);          // width = 2
	append_u16_be(jp2, 3u);          // 3 components
	jp2.push_back(7u);               // 8 bpc
	jp2.push_back(7u);               // compression type = 7
	jp2.push_back(0u);               // UnkC
	jp2.push_back(0u);               // IPR

	append_u32_be(jp2, static_cast<uint32_t>(8u + cs.size()));
	append_u32_be(jp2, 0x6A703263u); // 'jp2c'
	jp2.insert(jp2.end(), cs.begin(), cs.end());
	return jp2;
}

} // namespace

TEST(PdfFilters, DctRgbAndCmyk)
{
	// 1. Decode the 8x8 RGB JPEG in render/images-and-vectors.pdf (object 7 0 R).
	{
		const auto pdf_bytes = test::read_file_bytes(
			test::corpus_path("render/images-and-vectors.pdf"));
		auto xref_res = XrefTable::parse(pdf_bytes);
		RECTO_ASSERT_OK(xref_res);
		auto stm_obj = xref_res->resolve_object(pdf_bytes, IndirectRef{7u, 0u});
		RECTO_ASSERT_OK(stm_obj);
		ASSERT_TRUE(stm_obj->is_stream());

		auto img = decode_dct_image(stm_obj->as_stream()->raw_bytes);
		RECTO_ASSERT_OK(img);
		EXPECT_EQ(img->width, 8u);
		EXPECT_EQ(img->height, 8u);
		EXPECT_EQ(img->components, 3u);
		EXPECT_FALSE(img->is_cmyk);
		ASSERT_EQ(img->pixels.size(), 8u * 8u * 3u);
		ASSERT_EQ(img->rgba_pixels.size(), 8u * 8u * 4u);
		ASSERT_EQ(img->bgra_pixels.size(), 8u * 8u * 4u);
		// Every pixel in the all-zero DC/AC JPEG is neutral grey (128, 128, 128).
		EXPECT_NEAR(img->pixels[0], 128, 2);
		EXPECT_NEAR(img->pixels[1], 128, 2);
		EXPECT_NEAR(img->pixels[2], 128, 2);
		EXPECT_EQ(img->rgba_pixels[3], 255u);
		EXPECT_EQ(img->bgra_pixels[3], 255u);
	}

	// 2. Decode 4-component CMYK JPEG with and without /Decode array inversion
	// ([1 0 1 0 1 0 1 0] for Adobe inverted CMYK vs custom /Decode [0.8 0.2 0.0 1.0 1.0 0.0 0.0 0.0]).
	{
		const auto cmyk_jpeg = make_minimal_jpeg_8x8_cmyk();
		auto base_cmyk = decode_dct_image(cmyk_jpeg);
		RECTO_ASSERT_OK(base_cmyk);
		EXPECT_EQ(base_cmyk->width, 8u);
		EXPECT_EQ(base_cmyk->height, 8u);
		EXPECT_EQ(base_cmyk->components, 4u);
		EXPECT_TRUE(base_cmyk->is_cmyk);
		ASSERT_EQ(base_cmyk->pixels.size(), 256u);
		ASSERT_EQ(base_cmyk->rgba_pixels.size(), 256u);
		ASSERT_EQ(base_cmyk->bgra_pixels.size(), 256u);

		// Raw CMYK samples from all-zero DC/AC blocks are ~128 per channel.
		// Apply /Decode [1 0 1 0 1 0 1 0] (Adobe inverted CMYK) and verify inversion,
		// and apply /Decode [0 0 1 1 0 0 0 0] (forcing C=0%, M=100%, Y=0%, K=0% -> pure Magenta RGB(255, 0, 255)).
		PdfArray adobe_decode;
		for (int c = 0; c < 4; ++c) {
			adobe_decode.push_back(PdfValue(int64_t{1}));
			adobe_decode.push_back(PdfValue(int64_t{0}));
		}
		PdfDict adobe_parms;
		adobe_parms.set("Decode", PdfValue(std::move(adobe_decode)));
		auto inv_cmyk = decode_dct_image(cmyk_jpeg, &adobe_parms);
		RECTO_ASSERT_OK(inv_cmyk);
		EXPECT_EQ(inv_cmyk->pixels[0], static_cast<uint8_t>(255u - base_cmyk->pixels[0]));

		PdfArray magenta_decode;
		// C: [0, 0], M: [1, 1], Y: [0, 0], K: [0, 0] -> C=0, M=255, Y=0, K=0 -> sRGB = (255, 0, 255)
		magenta_decode.push_back(PdfValue(int64_t{0}));
		magenta_decode.push_back(PdfValue(int64_t{0}));
		magenta_decode.push_back(PdfValue(int64_t{1}));
		magenta_decode.push_back(PdfValue(int64_t{1}));
		magenta_decode.push_back(PdfValue(int64_t{0}));
		magenta_decode.push_back(PdfValue(int64_t{0}));
		magenta_decode.push_back(PdfValue(int64_t{0}));
		magenta_decode.push_back(PdfValue(int64_t{0}));

		PdfDict image_stm_dict;
		image_stm_dict.set("Filter", PdfValue(PdfName("DCTDecode")));
		image_stm_dict.set("Decode", PdfValue(std::move(magenta_decode)));
		PdfDict dp_dict;
		dp_dict.set("ColorTransform", PdfValue(int64_t{0}));
		image_stm_dict.set("DecodeParms", PdfValue(std::move(dp_dict)));

		auto magenta_rgba = decode_stream(cmyk_jpeg, image_stm_dict);
		RECTO_ASSERT_OK(magenta_rgba);
		ASSERT_EQ(magenta_rgba->size(), 256u);
		EXPECT_EQ((*magenta_rgba)[0], 255u); // R
		EXPECT_EQ((*magenta_rgba)[1], 0u);   // G
		EXPECT_EQ((*magenta_rgba)[2], 255u); // B
		EXPECT_EQ((*magenta_rgba)[3], 255u); // A
	}

	// 3. Decode genuine ITU-T T.800 JPXDecode (JP2 box container and raw J2K codestream
	// with 1-level 5-3 reversible DWT, RCT, Tier-2 tag-tree packet headers, and Tier-1 EBCOT MQ coding).
	{
		const std::array<uint8_t, 12> expected_rgb = {
			10, 20, 30,
			40, 50, 60,
			70, 80, 90,
			100, 150, 200,
		};
		for (bool mct : {false, true}) {
			const auto jp2_bytes = make_genuine_jp2_2x2_rgb(expected_rgb, mct);
			auto dec_img = decode_jpx_image(jp2_bytes);
			RECTO_ASSERT_OK(dec_img);
			EXPECT_EQ(dec_img->width, 2u);
			EXPECT_EQ(dec_img->height, 2u);
			EXPECT_EQ(dec_img->components, 3u);
			EXPECT_EQ(dec_img->pixels, std::vector<uint8_t>(expected_rgb.begin(), expected_rgb.end()));

			auto dec_jp2 = decode_jpx(jp2_bytes);
			RECTO_ASSERT_OK(dec_jp2);
			EXPECT_EQ(*dec_jp2, std::vector<uint8_t>(expected_rgb.begin(), expected_rgb.end()));

			// Also test through decode_stream(/Filter /JPXDecode).
			PdfDict jpx_dict;
			jpx_dict.set("Filter", PdfValue(PdfName("JPXDecode")));
			auto dec_stm = decode_stream(jp2_bytes, jpx_dict);
			RECTO_ASSERT_OK(dec_stm);
			EXPECT_EQ(*dec_stm, std::vector<uint8_t>(expected_rgb.begin(), expected_rgb.end()));
		}

		// 3b. Decode synthetic JP2 streams covering:
		// - Multi-sample code-block exercising ITU-T T.800 Table D.1 ZC contexts (`v_sum == 1` -> ctx 3, `v_sum == 0 && d_sum >= 2` -> ctx 2)
		// - 2x2 multi-tile (4x4 image split into four 2x2 tiles) JP2 codestream
		// - 9-7 irreversible lossy DWT with non-zero 11-bit mantissa step sizes (`mu_b != 0`) and ICT
		{
			// (i) 2x2 single-level (0 DWT levels, 2x2 LL0 code-block) where samples (0,0), (1,0), (0,1), (1,1)
			// become significant in column-stripe order:
			// - (0,0) = 64: h=0, v=0, d=0 -> ctx 0
			// - (0,1) = 64: top neighbor (0,0) is significant -> h=0, v=1, d=0 -> ctx 3 (`v_sum == 1`)
			// - (1,0) = 64: left (0,0) and bottom-left (0,1) are significant -> h=1, v=0, d=1 -> ctx 6
			// - (1,1) = 64: left (0,1), top (1,0), top-left (0,0) are significant -> h=1, v=1, d=1 -> ctx 7
			auto encode_2x2_all64_ll0 = []() -> EncodedJ2kSubband {
				EncodedJ2kSubband res{};
				res.zero_bitplanes = 2u; // mb_total = 9, top_bp = 6 (val = 64 = 1 << 6) -> zbp = 8 - 6 = 2
				res.num_passes = 1u + 3u * 6u; // 19 passes (CUP on bp=6, then 6*(SPP, MRP, CUP) on bp=5..0)
				std::array<TestMqContext, 19> ctxs{};
				ctxs[0].index = 4u;
				ctxs[17].index = 3u;
				ctxs[18].index = 46u;
				TestMqEncoder enc;
				// Pass 0 (CUP on bp=6, stripe_h=2 so no RLC):
				// x=0, y=0: ZC ctx 0 -> 1, SC (h=0, v=0 -> ctx 9, xor 0) -> 0
				enc.encode(ctxs[0], 1u);
				enc.encode(ctxs[9], 0u);
				// x=0, y=1: top (0,0) sig -> v_sum=1 -> ZC ctx 3 -> 1, SC (h=0, v=1 -> ctx 10, xor 0) -> 0
				enc.encode(ctxs[3], 1u);
				enc.encode(ctxs[10], 0u);
				// x=1, y=0: left (0,0) & bottom-left (0,1) sig -> h=1, v=0, d=1 -> ZC ctx 6 -> 1, SC (h=1, v=0 -> ctx 12, xor 0) -> 0
				enc.encode(ctxs[6], 1u);
				enc.encode(ctxs[12], 0u);
				// x=1, y=1: left (0,1), top (1,0), top-left (0,0) sig -> h=1, v=1, d=1 -> ZC ctx 7 -> 1, SC (h=1, v=1 -> ctx 13, xor 0) -> 0
				enc.encode(ctxs[7], 1u);
				enc.encode(ctxs[13], 0u);
				// Remaining 6 bitplanes (bp=5..0): all 4 samples are already significant, so SPP and CUP do nothing,
				// and MRP decodes 4 zero bits (ctx 15 first time since each has significant neighbors, then ctx 16).
				for (int bp = 5; bp >= 0; --bp) {
					const uint8_t mr_ctx = (bp == 5) ? 15u : 16u;
					for (int k = 0; k < 4; ++k) {
						enc.encode(ctxs[mr_ctx], 0u);
					}
				}
				res.bytes = enc.finish();
				return res;
			};

			const EncodedJ2kSubband sb_2x2 = encode_2x2_all64_ll0();
			std::vector<uint8_t> pkt_0lvl;
			for (int c = 0; c < 3; ++c) {
				const auto p = build_j2k_packet_for_subbands(std::span<const EncodedJ2kSubband>(&sb_2x2, 1u));
				pkt_0lvl.insert(pkt_0lvl.end(), p.begin(), p.end());
			}
			std::vector<uint8_t> cs_0lvl = {0xFFu, 0x4Fu, 0xFFu, 0x51u};
			append_u16_be(cs_0lvl, 47u);
			append_u16_be(cs_0lvl, 0u);
			append_u32_be(cs_0lvl, 2u);
			append_u32_be(cs_0lvl, 2u);
			append_u32_be(cs_0lvl, 0u);
			append_u32_be(cs_0lvl, 0u);
			append_u32_be(cs_0lvl, 2u);
			append_u32_be(cs_0lvl, 2u);
			append_u32_be(cs_0lvl, 0u);
			append_u32_be(cs_0lvl, 0u);
			append_u16_be(cs_0lvl, 3u);
			for (int c = 0; c < 3; ++c) {
				cs_0lvl.push_back(7u);
				cs_0lvl.push_back(1u);
				cs_0lvl.push_back(1u);
			}
			cs_0lvl.push_back(0xFFu);
			cs_0lvl.push_back(0x52u);
			append_u16_be(cs_0lvl, 12u);
			cs_0lvl.push_back(0u);
			cs_0lvl.push_back(0u);
			append_u16_be(cs_0lvl, 1u);
			cs_0lvl.push_back(0u); // no MCT
			cs_0lvl.push_back(0u); // 0 DWT levels -> 2x2 LL0 code-block!
			cs_0lvl.push_back(4u);
			cs_0lvl.push_back(4u);
			cs_0lvl.push_back(0u);
			cs_0lvl.push_back(1u); // 5-3 reversible
			cs_0lvl.push_back(0xFFu);
			cs_0lvl.push_back(0x5Cu);
			append_u16_be(cs_0lvl, 4u);
			cs_0lvl.push_back(0x40u); // guard=2, qstyle=0
			cs_0lvl.push_back(static_cast<uint8_t>(8u << 3u)); // eps=8 -> mb_total=9
			cs_0lvl.push_back(0xFFu);
			cs_0lvl.push_back(0x90u);
			append_u16_be(cs_0lvl, 10u);
			append_u16_be(cs_0lvl, 0u);
			append_u32_be(cs_0lvl, 14u + static_cast<uint32_t>(pkt_0lvl.size()));
			cs_0lvl.push_back(0u);
			cs_0lvl.push_back(1u);
			cs_0lvl.push_back(0xFFu);
			cs_0lvl.push_back(0x93u);
			cs_0lvl.insert(cs_0lvl.end(), pkt_0lvl.begin(), pkt_0lvl.end());
			cs_0lvl.push_back(0xFFu);
			cs_0lvl.push_back(0xD9u);

			auto dec_2x2_cb = decode_jpx_image(cs_0lvl);
			RECTO_ASSERT_OK(dec_2x2_cb);
			ASSERT_EQ(dec_2x2_cb->pixels.size(), 12u);
			for (uint8_t px : dec_2x2_cb->pixels) {
				EXPECT_EQ(px, 192u); // 64 + 128 DC shift = 192
			}

			// (ii) 2x2 multi-tile codestream (4x4 image split into four 2x2 tiles, each tile constant colour):
			std::vector<uint8_t> cs_tiled = {0xFFu, 0x4Fu, 0xFFu, 0x51u};
			append_u16_be(cs_tiled, 47u);
			append_u16_be(cs_tiled, 0u);
			append_u32_be(cs_tiled, 4u); // Xsiz = 4
			append_u32_be(cs_tiled, 4u); // Ysiz = 4
			append_u32_be(cs_tiled, 0u);
			append_u32_be(cs_tiled, 0u);
			append_u32_be(cs_tiled, 2u); // XTsiz = 2
			append_u32_be(cs_tiled, 2u); // YTsiz = 2
			append_u32_be(cs_tiled, 0u);
			append_u32_be(cs_tiled, 0u);
			append_u16_be(cs_tiled, 3u);
			for (int c = 0; c < 3; ++c) {
				cs_tiled.push_back(7u);
				cs_tiled.push_back(1u);
				cs_tiled.push_back(1u);
			}
			cs_tiled.push_back(0xFFu);
			cs_tiled.push_back(0x52u);
			append_u16_be(cs_tiled, 12u);
			cs_tiled.push_back(0u);
			cs_tiled.push_back(0u);
			append_u16_be(cs_tiled, 1u);
			cs_tiled.push_back(0u); // no MCT
			cs_tiled.push_back(1u); // 1 DWT level per 2x2 tile
			cs_tiled.push_back(4u);
			cs_tiled.push_back(4u);
			cs_tiled.push_back(0u);
			cs_tiled.push_back(1u);
			cs_tiled.push_back(0xFFu);
			cs_tiled.push_back(0x5Cu);
			append_u16_be(cs_tiled, 7u);
			cs_tiled.push_back(0x40u);
			for (int sb = 0; sb < 4; ++sb) {
				cs_tiled.push_back(static_cast<uint8_t>(8u << 3u));
			}
			const std::array<int32_t, 4> tile_vals = {-100, -50, 25, 100};
			for (uint16_t t = 0; t < 4u; ++t) {
				std::vector<uint8_t> t_pkts;
				const EncodedJ2kSubband sb_dc = encode_j2k_1x1_subband(tile_vals[t], 9u);
				const EncodedJ2kSubband sb_zero = encode_j2k_1x1_subband(0, 9u);
				for (int c = 0; c < 3; ++c) {
					const auto p0 = build_j2k_packet_for_subbands(std::span<const EncodedJ2kSubband>(&sb_dc, 1u));
					t_pkts.insert(t_pkts.end(), p0.begin(), p0.end());
				}
				const std::array<EncodedJ2kSubband, 3> zero_sbs = {sb_zero, sb_zero, sb_zero};
				for (int c = 0; c < 3; ++c) {
					const auto p1 = build_j2k_packet_for_subbands(zero_sbs);
					t_pkts.insert(t_pkts.end(), p1.begin(), p1.end());
				}
				cs_tiled.push_back(0xFFu);
				cs_tiled.push_back(0x90u);
				append_u16_be(cs_tiled, 10u);
				append_u16_be(cs_tiled, t); // Isot = t
				append_u32_be(cs_tiled, 14u + static_cast<uint32_t>(t_pkts.size()));
				cs_tiled.push_back(0u);
				cs_tiled.push_back(1u);
				cs_tiled.push_back(0xFFu);
				cs_tiled.push_back(0x93u);
				cs_tiled.insert(cs_tiled.end(), t_pkts.begin(), t_pkts.end());
			}
			cs_tiled.push_back(0xFFu);
			cs_tiled.push_back(0xD9u);

			auto dec_tiled = decode_jpx_image(cs_tiled);
			RECTO_ASSERT_OK(dec_tiled);
			ASSERT_EQ(dec_tiled->width, 4u);
			ASSERT_EQ(dec_tiled->height, 4u);
			for (uint32_t y = 0; y < 4u; ++y) {
				for (uint32_t x = 0; x < 4u; ++x) {
					const uint32_t tid = (y / 2u) * 2u + (x / 2u);
					const uint8_t exp_v = static_cast<uint8_t>(tile_vals[tid] + 128);
					const size_t idx = (static_cast<size_t>(y) * 4u + x) * 3u;
					EXPECT_EQ(dec_tiled->pixels[idx + 0u], exp_v);
					EXPECT_EQ(dec_tiled->pixels[idx + 1u], exp_v);
					EXPECT_EQ(dec_tiled->pixels[idx + 2u], exp_v);
				}
			}

			// (iii) 9-7 irreversible lossy DWT (`wavelet_type = 0`) with scalar expounded QCD (`qstyle = 2`,
			// `eps = 8`, `mu = 1024` -> step size `Delta_b = 1.5 * 2^G_b`) and ICT (`enable_mct = 1`):
			// Encode Y = 40 (so Y_dequant = 40 * 1.5 = 60 -> +128 = 188), Cb = 0, Cr = 20 (Cr_dequant = 30 -> R = 188 + 1.402*30 = 230).
			std::vector<uint8_t> cs_97 = {0xFFu, 0x4Fu, 0xFFu, 0x51u};
			append_u16_be(cs_97, 47u);
			append_u16_be(cs_97, 0u);
			append_u32_be(cs_97, 2u);
			append_u32_be(cs_97, 2u);
			append_u32_be(cs_97, 0u);
			append_u32_be(cs_97, 0u);
			append_u32_be(cs_97, 2u);
			append_u32_be(cs_97, 2u);
			append_u32_be(cs_97, 0u);
			append_u32_be(cs_97, 0u);
			append_u16_be(cs_97, 3u);
			for (int c = 0; c < 3; ++c) {
				cs_97.push_back(7u);
				cs_97.push_back(1u);
				cs_97.push_back(1u);
			}
			cs_97.push_back(0xFFu);
			cs_97.push_back(0x52u);
			append_u16_be(cs_97, 12u);
			cs_97.push_back(0u);
			cs_97.push_back(0u);
			append_u16_be(cs_97, 1u);
			cs_97.push_back(1u); // MCT = 1 (ICT)
			cs_97.push_back(1u); // 1 DWT level
			cs_97.push_back(4u);
			cs_97.push_back(4u);
			cs_97.push_back(0u);
			cs_97.push_back(0u); // 9-7 irreversible wavelet!
			cs_97.push_back(0xFFu);
			cs_97.push_back(0x5Cu);
			append_u16_be(cs_97, 11u); // 3 + 4 * 2 bytes
			cs_97.push_back(0x42u);     // guard_bits = 2, qstyle = 2 (scalar expounded)
			for (int sb = 0; sb < 4; ++sb) {
				append_u16_be(cs_97, static_cast<uint16_t>((8u << 11u) | 1024u)); // eps = 8, mu = 1024 (1 + 1024/2048 = 1.5)
			}
			std::vector<uint8_t> pkts_97;
			const std::array<int32_t, 3> ycbcr_ll = {40, 0, 20};
			const EncodedJ2kSubband sb_z = encode_j2k_1x1_subband(0, 9u);
			for (int c = 0; c < 3; ++c) {
				const EncodedJ2kSubband sb_c = encode_j2k_1x1_subband(ycbcr_ll[c], 9u);
				const auto p0 = build_j2k_packet_for_subbands(std::span<const EncodedJ2kSubband>(&sb_c, 1u));
				pkts_97.insert(pkts_97.end(), p0.begin(), p0.end());
			}
			const std::array<EncodedJ2kSubband, 3> z3 = {sb_z, sb_z, sb_z};
			for (int c = 0; c < 3; ++c) {
				const auto p1 = build_j2k_packet_for_subbands(z3);
				pkts_97.insert(pkts_97.end(), p1.begin(), p1.end());
			}
			cs_97.push_back(0xFFu);
			cs_97.push_back(0x90u);
			append_u16_be(cs_97, 10u);
			append_u16_be(cs_97, 0u);
			append_u32_be(cs_97, 14u + static_cast<uint32_t>(pkts_97.size()));
			cs_97.push_back(0u);
			cs_97.push_back(1u);
			cs_97.push_back(0xFFu);
			cs_97.push_back(0x93u);
			cs_97.insert(cs_97.end(), pkts_97.begin(), pkts_97.end());
			cs_97.push_back(0xFFu);
			cs_97.push_back(0xD9u);

			auto dec_97 = decode_jpx_image(cs_97);
			RECTO_ASSERT_OK(dec_97);
			ASSERT_EQ(dec_97->pixels.size(), 12u);
			// With eps=8, mu=1024 (Delta_0 = 1.5), Y_dequant = 40 * 1.5 = 60, Cb_dequant = 0, Cr_dequant = 20 * 1.5 = 30.
			// After 2D 9-7 inverse DWT and ICT: R = round(60 + 1.402 * 30) + 128 = 230, G = round(60 - 0.71414 * 30) + 128 = 167, B = 60 + 128 = 188.
			EXPECT_EQ(dec_97->pixels[0], 230u);
			EXPECT_EQ(dec_97->pixels[1], 167u);
			EXPECT_EQ(dec_97->pixels[2], 188u);

			// (iv) Odd-origin JPX tiles (`(x0 & 1) != 0` and `(y0 & 1) != 0`) with 1-level 5-3 and 9-7 DWT:
			// Test both `n == 1` (`[1, 2) x [1, 2)`, 1x1 high-pass `HH1` subband) and `n == 3` (`[1, 4) x [1, 4)`,
			// `LL1` = 1x1, `HL1` = 2x1, `LH1` = 1x2, `HH1` = 2x2) under ASan.
			for (uint8_t wv_type : {uint8_t{1u}, uint8_t{0u}}) {
				// 3x3 odd-origin tile `[1, 4) x [1, 4)` (`XOsiz = 1, YOsiz = 1, Xsiz = 4, Ysiz = 4`):
				// Resolution 0 (`LL1` is 1x1) = 30; Resolution 1 (`HL1`, `LH1`, `HH1`) uses an empty packet (`0x00`).
				const EncodedJ2kSubband sb_ll30 = encode_j2k_1x1_subband(30, 9u);
				std::vector<uint8_t> odd_pkts =
					build_j2k_packet_for_subbands(std::span<const EncodedJ2kSubband>(&sb_ll30, 1u));
				odd_pkts.push_back(0x00u); // Empty packet for Resolution 1 (all detail subbands zero)

				std::vector<uint8_t> cs_odd3 = {0xFFu, 0x4Fu, 0xFFu, 0x51u};
				append_u16_be(cs_odd3, 41u); // Lsiz = 38 + 3*1 = 41 (1 component)
				append_u16_be(cs_odd3, 0u);
				append_u32_be(cs_odd3, 4u);  // Xsiz = 4
				append_u32_be(cs_odd3, 4u);  // Ysiz = 4
				append_u32_be(cs_odd3, 1u);  // XOsiz = 1 (odd origin!)
				append_u32_be(cs_odd3, 1u);  // YOsiz = 1 (odd origin!)
				append_u32_be(cs_odd3, 4u);  // XTsiz = 4
				append_u32_be(cs_odd3, 4u);  // YTsiz = 4
				append_u32_be(cs_odd3, 0u);  // XTOsiz = 0
				append_u32_be(cs_odd3, 0u);  // YTOsiz = 0
				append_u16_be(cs_odd3, 1u);  // Csiz = 1
				cs_odd3.push_back(7u);       // 8-bit unsigned
				cs_odd3.push_back(1u);
				cs_odd3.push_back(1u);
				cs_odd3.push_back(0xFFu);
				cs_odd3.push_back(0x52u);
				append_u16_be(cs_odd3, 12u);
				cs_odd3.push_back(0u);
				cs_odd3.push_back(0u);
				append_u16_be(cs_odd3, 1u);
				cs_odd3.push_back(0u);
				cs_odd3.push_back(1u); // 1 DWT decomposition level
				cs_odd3.push_back(4u);
				cs_odd3.push_back(4u);
				cs_odd3.push_back(0u);
				cs_odd3.push_back(wv_type);
				cs_odd3.push_back(0xFFu);
				cs_odd3.push_back(0x5Cu);
				append_u16_be(cs_odd3, 7u);
				cs_odd3.push_back(0x40u);
				for (int sb = 0; sb < 4; ++sb) {
					cs_odd3.push_back(static_cast<uint8_t>(8u << 3u));
				}
				cs_odd3.push_back(0xFFu);
				cs_odd3.push_back(0x90u);
				append_u16_be(cs_odd3, 10u);
				append_u16_be(cs_odd3, 0u);
				append_u32_be(cs_odd3, 14u + static_cast<uint32_t>(odd_pkts.size()));
				cs_odd3.push_back(0u);
				cs_odd3.push_back(1u);
				cs_odd3.push_back(0xFFu);
				cs_odd3.push_back(0x93u);
				cs_odd3.insert(cs_odd3.end(), odd_pkts.begin(), odd_pkts.end());
				cs_odd3.push_back(0xFFu);
				cs_odd3.push_back(0xD9u);

				auto dec_odd3 = decode_jpx_image(cs_odd3);
				RECTO_ASSERT_OK(dec_odd3);
				EXPECT_EQ(dec_odd3->width, 3u);
				EXPECT_EQ(dec_odd3->height, 3u);
				ASSERT_EQ(dec_odd3->pixels.size(), 9u);
				for (uint8_t px : dec_odd3->pixels) {
					EXPECT_EQ(px, 158u); // 30 + 128 = 158 across all 9 samples
				}
			}

			// 1x1 singleton tiles `[1, 2) x [1, 2)` (`XOsiz = 1, YOsiz = 1, Xsiz = 2, Ysiz = 2`) and `[0, 1) x [0, 1)`:
			// Per ITU-T T.800 Annex F.3.6 (`1D_SR` when `n == 1`), both 5-3 reversible and 9-7 irreversible synthesis
			// halve odd-origin `HH1` (`40 / 4 + 128 = 138` in 2D) and leave even-origin `LL1` unscaled (`40 + 128 = 168`).
			for (uint8_t wv_type : {uint8_t{1u}, uint8_t{0u}}) {
				for (bool odd_origin : {true, false}) {
					const EncodedJ2kSubband sb_40 = encode_j2k_1x1_subband(40, 9u);
					std::vector<uint8_t> s1_pkts;
					if (odd_origin) {
						s1_pkts.push_back(0x00u); // Empty packet for Resolution 0 (0x0 LL1)
						const auto p1 = build_j2k_packet_for_subbands(std::span<const EncodedJ2kSubband>(&sb_40, 1u));
						s1_pkts.insert(s1_pkts.end(), p1.begin(), p1.end());
					} else {
						const auto p0 = build_j2k_packet_for_subbands(std::span<const EncodedJ2kSubband>(&sb_40, 1u));
						s1_pkts.insert(s1_pkts.end(), p0.begin(), p0.end());
						s1_pkts.push_back(0x00u); // Empty packet for Resolution 1 (0x0 HL1/LH1/HH1)
					}

					std::vector<uint8_t> cs_s1 = {0xFFu, 0x4Fu, 0xFFu, 0x51u};
					append_u16_be(cs_s1, 41u);
					append_u16_be(cs_s1, 0u);
					append_u32_be(cs_s1, odd_origin ? 2u : 1u); // Xsiz
					append_u32_be(cs_s1, odd_origin ? 2u : 1u); // Ysiz
					append_u32_be(cs_s1, odd_origin ? 1u : 0u); // XOsiz
					append_u32_be(cs_s1, odd_origin ? 1u : 0u); // YOsiz
					append_u32_be(cs_s1, odd_origin ? 2u : 1u); // XTsiz
					append_u32_be(cs_s1, odd_origin ? 2u : 1u); // YTsiz
					append_u32_be(cs_s1, 0u);
					append_u32_be(cs_s1, 0u);
					append_u16_be(cs_s1, 1u); // Csiz = 1
					cs_s1.push_back(7u);
					cs_s1.push_back(1u);
					cs_s1.push_back(1u);
					cs_s1.push_back(0xFFu);
					cs_s1.push_back(0x52u);
					append_u16_be(cs_s1, 12u);
					cs_s1.push_back(0u);
					cs_s1.push_back(0u);
					append_u16_be(cs_s1, 1u);
					cs_s1.push_back(0u);
					cs_s1.push_back(1u);
					cs_s1.push_back(4u);
					cs_s1.push_back(4u);
					cs_s1.push_back(0u);
					cs_s1.push_back(wv_type);
					cs_s1.push_back(0xFFu);
					cs_s1.push_back(0x5Cu);
					append_u16_be(cs_s1, 7u);
					cs_s1.push_back(0x40u);
					for (int sb = 0; sb < 4; ++sb) {
						// For 9-7 irreversible (`wv_type == 0`), subband `gain` is `{0, 1, 1, 2}` so setting `eps = 8 + gain`
						// gives `rb - eps = 0` (`delta_b = 1.0f`) and keeps `mb_total = 9` when `guard_bits` adjusts or we use `eps = 8`
						// and verify `40 * 4 / 4 + 128 = 168` vs `40 / 4 + 128 = 138`.
						cs_s1.push_back(static_cast<uint8_t>(8u << 3u));
					}
					cs_s1.push_back(0xFFu);
					cs_s1.push_back(0x90u);
					append_u16_be(cs_s1, 10u);
					append_u16_be(cs_s1, 0u);
					append_u32_be(cs_s1, 14u + static_cast<uint32_t>(s1_pkts.size()));
					cs_s1.push_back(0u);
					cs_s1.push_back(1u);
					cs_s1.push_back(0xFFu);
					cs_s1.push_back(0x93u);
					cs_s1.insert(cs_s1.end(), s1_pkts.begin(), s1_pkts.end());
					cs_s1.push_back(0xFFu);
					cs_s1.push_back(0xD9u);

					auto dec_s1 = decode_jpx_image(cs_s1);
					RECTO_ASSERT_OK(dec_s1);
					ASSERT_EQ(dec_s1->pixels.size(), 1u);
					// For 5-3 (`wv_type == 1`): odd-origin `HH1` = `40 / 4 + 128 = 138`; even-origin `LL1` = `40 + 128 = 168`.
					// For 9-7 (`wv_type == 0`, `eps = 8` for all bands): `HH1` has `gain = 2` (`delta_b = 4.0f`), so
					// `q = 40 -> 160.0f`, halved twice by odd-origin 2D `1D_SR` (`* 0.5f * 0.5f`) to `40.0f + 128 = 168`
					// (without the ITU-T T.800 §F.3.6 `n == 1` fix, `160.0f / (kK^2) = 105.6f + 128 = 234`!).
					const uint8_t expected_px = (wv_type == 1u && odd_origin) ? 138u : 168u;
					EXPECT_EQ(dec_s1->pixels[0], expected_px);
				}
			}
		}
	}

	// 4. Oversized image header rejection (width * height * 4 > 256 MB) and cumulative buffer budget:
	// Verify that 65535 x 65535 and 100000 x 100000 headers in CCITTFaxDecode, JBIG2Decode,
	// DCTDecode, and JPXDecode are rejected with ErrorCode::DecompressionLimitExceeded before
	// allocating pixel buffers, and leave diagnostics_tail() empty (pure stateless filter contract).
	{
		clear_diagnostics();

		// (a) CCITTFaxDecode: 65535 x 65535 and 100000 x 100000
		for (int64_t dim : {int64_t{65535}, int64_t{100000}}) {
			PdfDict ccitt_huge;
			ccitt_huge.set("K", PdfValue(int64_t{-1}));
			ccitt_huge.set("Columns", PdfValue(dim));
			ccitt_huge.set("Rows", PdfValue(dim));
			const std::array<uint8_t, 4> dummy = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
			RECTO_EXPECT_ERR(
				decode_ccitt_fax(dummy, &ccitt_huge),
				ErrorCode::DecompressionLimitExceeded);
		}

		// (b) JBIG2Decode: Page Information segment claiming 65535 x 65535 and 100000 x 100000
		for (uint32_t dim : {65535u, 100000u}) {
			std::vector<uint8_t> jbig2_huge;
			append_jbig2_segment(
				jbig2_huge,
				0u,
				48u,
				{},
				1u,
				make_jbig2_page_info_payload(dim, dim, 0u));
			RECTO_EXPECT_ERR(
				decode_jbig2(jbig2_huge),
				ErrorCode::DecompressionLimitExceeded);
		}

		// (c) DCTDecode: SOF0 header claiming 65535 x 65535 pixels, plus cumulative buffer budget check
		// on an 8x8 RGB JPEG (pixels=192 + rgba=256 + bgra=256 = 704 bytes > max_decoded_bytes=256).
		{
			std::vector<uint8_t> jpeg_huge = {
				0xFFu, 0xD8u,
				0xFFu, 0xC0u, 0x00u, 0x11u, // SOF0, length 17
				0x08u,                      // 8-bit
				0xFFu, 0xFFu,               // height = 65535
				0xFFu, 0xFFu,               // width = 65535
				0x03u,                      // 3 components
				1u, 0x11u, 0u,
				2u, 0x11u, 0u,
				3u, 0x11u, 0u,
				0xFFu, 0xD9u,
			};
			RECTO_EXPECT_ERR(
				decode_dct(jpeg_huge),
				ErrorCode::DecompressionLimitExceeded);

			const auto pdf_bytes = test::read_file_bytes(
				test::corpus_path("render/images-and-vectors.pdf"));
			auto xref_res = XrefTable::parse(pdf_bytes);
			RECTO_ASSERT_OK(xref_res);
			auto stm_obj = xref_res->resolve_object(pdf_bytes, IndirectRef{7u, 0u});
			RECTO_ASSERT_OK(stm_obj);
			RECTO_EXPECT_ERR(
				decode_dct_image(stm_obj->as_stream()->raw_bytes, nullptr, 256u),
				ErrorCode::DecompressionLimitExceeded);
		}

		// (d) JPXDecode: J2K SIZ header claiming 100000 x 100000 pixels, plus cumulative buffer budget check
		// on a 2x2 RGB JP2 (pixels=12 + rgba=16 + bgra=16 = 44 bytes > max_decoded_bytes=32, and
		// max_decoded_bytes=64 where total_wic_bytes=44 fits but tile working-set buffers exceed 64).
		{
			std::vector<uint8_t> j2k_huge = {
				0xFFu, 0x4Fu, // SOC
				0xFFu, 0x51u, // SIZ
			};
			append_u16_be(j2k_huge, 47u);
			append_u16_be(j2k_huge, 0u);
			append_u32_be(j2k_huge, 100000u); // Xsiz
			append_u32_be(j2k_huge, 100000u); // Ysiz
			append_u32_be(j2k_huge, 0u);
			append_u32_be(j2k_huge, 0u);
			append_u32_be(j2k_huge, 100000u);
			append_u32_be(j2k_huge, 100000u);
			append_u32_be(j2k_huge, 0u);
			append_u32_be(j2k_huge, 0u);
			append_u16_be(j2k_huge, 3u);
			for (int c = 0; c < 3; ++c) {
				j2k_huge.push_back(7u);
				j2k_huge.push_back(1u);
				j2k_huge.push_back(1u);
			}
			RECTO_EXPECT_ERR(
				decode_jpx(j2k_huge),
				ErrorCode::DecompressionLimitExceeded);

			const std::array<uint8_t, 12> rgb_2x2 = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200};
			const auto jp2_2x2 = make_genuine_jp2_2x2_rgb(rgb_2x2, true);
			RECTO_EXPECT_ERR(
				decode_jpx_image(jp2_2x2, nullptr, 32u),
				ErrorCode::DecompressionLimitExceeded);
			RECTO_EXPECT_ERR(
				decode_jpx_image(jp2_2x2, nullptr, 64u),
				ErrorCode::DecompressionLimitExceeded);

			// 1x1 image with 4096 tiles (XOsiz = 4095, Xsiz = 4096, XTsiz = 1) under max_decoded_bytes = 2048:
			// Must be rejected with DecompressionLimitExceeded before allocating tile_part_spans(4096).
			std::vector<uint8_t> j2k_4096_tiles = {0xFFu, 0x4Fu, 0xFFu, 0x51u};
			append_u16_be(j2k_4096_tiles, 41u);
			append_u16_be(j2k_4096_tiles, 0u);
			append_u32_be(j2k_4096_tiles, 4096u); // Xsiz = 4096
			append_u32_be(j2k_4096_tiles, 1u);    // Ysiz = 1
			append_u32_be(j2k_4096_tiles, 4095u); // XOsiz = 4095 -> 1x1 image
			append_u32_be(j2k_4096_tiles, 0u);    // YOsiz = 0
			append_u32_be(j2k_4096_tiles, 1u);    // XTsiz = 1 -> 4096 tiles
			append_u32_be(j2k_4096_tiles, 1u);    // YTsiz = 1
			append_u32_be(j2k_4096_tiles, 0u);
			append_u32_be(j2k_4096_tiles, 0u);
			append_u16_be(j2k_4096_tiles, 1u);    // Csiz = 1
			j2k_4096_tiles.push_back(7u);
			j2k_4096_tiles.push_back(1u);
			j2k_4096_tiles.push_back(1u);
			RECTO_EXPECT_ERR(
				decode_jpx_image(j2k_4096_tiles, nullptr, 2048u),
				ErrorCode::DecompressionLimitExceeded);
		}

		EXPECT_TRUE(diagnostics_tail().empty());
	}
}

} // namespace
} // namespace recto::pdf
