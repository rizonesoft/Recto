// Unit tests for PDF stream filters: FlateDecode, LZWDecode, TIFF Predictor 2
// and PNG Predictors 10..15, ASCIIHexDecode, ASCII85Decode, RunLengthDecode,
// CCITTFaxDecode (Group 3 1D/2D and Group 4 2D), JBIG2Decode, WIC DCTDecode and
// JPXDecode, chained filter execution, and the 256 MB decompression cap
// (D01 T01 sections 7 and 8).
#include "recto_pdf.h"
#include "test_support.h"

#include <array>
#include <atomic>
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
	// With an all-0xFF arithmetic payload (C = 0 in MqDecoder):
	// - When TPGDON is false, row 0 triggers state-0 conditional exchange (all 8 pixels = 1 in JBIG2 -> 0x00 in PDF).
	// - When TPGDON is true, SLTP on row 0 decodes 1 (LTP = 1 -> row 0 stays default white = 0xFF in PDF),
	//   SLTP on row 1 decodes 1 (LTP = 0 -> row 1 decoded), and SLTP on row 2 decodes 1 (LTP = 1 -> row 2 copies row 1).
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

	// 3. Symbol Dictionary (segment 0) in /JBIG2Globals + Text Region (segment 6) +
	// Pattern Dictionary (segment 16) + Halftone Region (segment 22).
	{
		// Build a globals stream containing:
		// - Segment 0: Symbol Dictionary with 0 new symbols and 0 exported symbols (valid empty dict)
		// - Segment 1: Pattern Dictionary (seg_type = 16, MMR = 1, 8x2 patterns, GRAYMAX = 1 -> 16x2 collective)
		std::vector<uint8_t> globals_bytes;
		{
			std::vector<uint8_t> sd_payload;
			append_u16_be(sd_payload, 0x0000u); // arithmetic, template 0
			const int8_t sd_at[8] = {3, -1, -3, -1, 2, -2, -2, -2};
			sd_payload.insert(
				sd_payload.end(),
				reinterpret_cast<const uint8_t*>(sd_at),
				reinterpret_cast<const uint8_t*>(sd_at) + 8);
			append_u32_be(sd_payload, 0u); // SDNUMEXSYMS = 0
			append_u32_be(sd_payload, 0u); // SDNUMNEWSYMS = 0
			sd_payload.insert(sd_payload.end(), 8u, 0xFFu);
			append_jbig2_segment(globals_bytes, 0u, 0u, {}, 0u, sd_payload);

			// Pattern dictionary: 2 patterns of 8x2 (collective 16x2):
			// Pattern 0 (cols 0..7): all white; Pattern 1 (cols 8..15): all black!
			// In MMR, 8W + 8B on row 0 is H(001) + W8(10011) + B8(000101), row 1 is V0(1) + V0(1), EOFB.
			const auto pat_mmr = pack_bitstring(
				std::string("001") + "10011" + "000101" +
				"11" +
				"000000000001000000000001");
			std::vector<uint8_t> pd_payload;
			pd_payload.push_back(0x01u); // HDMMR = 1
			pd_payload.push_back(8u);    // HDPW = 8
			pd_payload.push_back(2u);    // HDPH = 2
			append_u32_be(pd_payload, 1u); // GRAYMAX = 1 (2 patterns: 0 and 1)
			pd_payload.insert(pd_payload.end(), pat_mmr.begin(), pat_mmr.end());
			append_jbig2_segment(globals_bytes, 1u, 16u, {}, 0u, pd_payload);
		}

		// Page stream:
		// - Segment 2: Page Information (8x2)
		// - Segment 3: Immediate Text Region (seg_type = 6, 8x2, 0 instances, refers to segment 0)
		// - Segment 4: Immediate Halftone Region (seg_type = 22, 8x2, refers to Pattern Dict segment 1,
		//              1x1 grid selecting pattern 1 = all black!)
		// - Segment 5: End of Page (49)
		std::vector<uint8_t> page_bytes;
		append_jbig2_segment(page_bytes, 2u, 48u, {}, 1u, make_jbig2_page_info_payload(8u, 2u, 0u));

		{
			std::vector<uint8_t> tr_payload;
			append_u32_be(tr_payload, 8u);
			append_u32_be(tr_payload, 2u);
			append_u32_be(tr_payload, 0u);
			append_u32_be(tr_payload, 0u);
			tr_payload.push_back(0u);
			append_u16_be(tr_payload, 0u);
			append_u32_be(tr_payload, 0u); // SBNUMINSTANCES = 0
			tr_payload.insert(tr_payload.end(), 8u, 0xFFu);
			const uint8_t ref0[1] = {0u};
			append_jbig2_segment(page_bytes, 3u, 6u, ref0, 1u, tr_payload);
		}

		{
			// 1x1 grid bitplane selecting pattern 1 (1 black pixel on a 1x1 MMR bitmap:
			// H(001) + W0(00110101) + B1(010) + EOFB).
			const auto grid_mmr = pack_bitstring(
				std::string("001") + "00110101" + "010" +
				"000000000001000000000001");
			std::vector<uint8_t> ht_payload;
			append_u32_be(ht_payload, 8u); // width
			append_u32_be(ht_payload, 2u); // height
			append_u32_be(ht_payload, 0u); // x
			append_u32_be(ht_payload, 0u); // y
			ht_payload.push_back(0u);      // OR comb op
			ht_payload.push_back(0x01u);   // HTMMR = 1
			append_u32_be(ht_payload, 1u); // HGW = 1
			append_u32_be(ht_payload, 1u); // HGH = 1
			append_u32_be(ht_payload, 0u); // HGX = 0
			append_u32_be(ht_payload, 0u); // HGY = 0
			append_u16_be(ht_payload, 0x0800u); // HRX
			append_u16_be(ht_payload, 0u);      // HRY
			ht_payload.insert(ht_payload.end(), grid_mmr.begin(), grid_mmr.end());
			const uint8_t ref1[1] = {1u};
			append_jbig2_segment(page_bytes, 4u, 22u, ref1, 1u, ht_payload);
		}
		append_jbig2_segment(page_bytes, 5u, 49u, {}, 1u, {});

		PdfStream globals_stm;
		globals_stm.raw_bytes = globals_bytes;
		PdfDict parms;
		parms.set("JBIG2Globals", PdfValue(std::move(globals_stm)));

		PdfDict stm_dict;
		stm_dict.set("Filter", PdfValue(PdfName("JBIG2Decode")));
		stm_dict.set("DecodeParms", PdfValue(std::move(parms)));

		auto dec = decode_stream(page_bytes, stm_dict);
		RECTO_ASSERT_OK(dec);
		ASSERT_EQ(dec->size(), 2u);
		// Pattern 1 is all-black (JBIG2 1 -> PDF DeviceGray 0x00).
		EXPECT_EQ((*dec)[0], 0x00u);
		EXPECT_EQ((*dec)[1], 0x00u);
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

// Builds a minimal deterministic 2x2 3-component JP2 container + J2K codestream.
[[nodiscard]] std::vector<uint8_t> make_minimal_jp2_2x2_rgb(
	std::span<const uint8_t, 12> rgb_samples,
	bool enable_mct = false)
{
	// J2K codestream: SOC + SIZ + COD + QCD + SOT + SOD + tile_data + EOC
	std::vector<uint8_t> cs;
	cs.push_back(0xFFu);
	cs.push_back(0x4Fu); // SOC

	// SIZ (0xFF51): Lsiz = 38 + 3*3 = 47
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

	// COD (0xFF52): Lcod = 12
	cs.push_back(0xFFu);
	cs.push_back(0x52u);
	append_u16_be(cs, 12u);
	cs.push_back(0u); // Scod
	cs.push_back(0u); // SGcod progression = LRCP
	append_u16_be(cs, 1u); // 1 layer
	cs.push_back(enable_mct ? 1u : 0u); // MCT flag
	cs.push_back(0u); // 0 decomposition levels
	cs.push_back(4u); // code-block width exp
	cs.push_back(4u); // code-block height exp
	cs.push_back(0u); // code-block style
	cs.push_back(1u); // 5-3 reversible wavelet

	// QCD (0xFF5C): Lqcd = 4
	cs.push_back(0xFFu);
	cs.push_back(0x5Cu);
	append_u16_be(cs, 4u);
	cs.push_back(0u);
	cs.push_back(0x40u);

	// SOT (0xFF90): Lsot = 10, Isot = 0, Psot = 14 + 12 = 26, TPsot = 0, TNsot = 1
	cs.push_back(0xFFu);
	cs.push_back(0x90u);
	append_u16_be(cs, 10u);
	append_u16_be(cs, 0u);
	append_u32_be(cs, 14u + static_cast<uint32_t>(rgb_samples.size()));
	cs.push_back(0u);
	cs.push_back(1u);

	// SOD (0xFF93) + 12 sample bytes + EOC (0xFFD9)
	cs.push_back(0xFFu);
	cs.push_back(0x93u);
	cs.insert(cs.end(), rgb_samples.begin(), rgb_samples.end());
	cs.push_back(0xFFu);
	cs.push_back(0xD9u);

	// Wrap in JP2 container: signature box + ihdr inside jp2h + jp2c
	std::vector<uint8_t> jp2 = {
		0x00u, 0x00u, 0x00u, 0x0Cu,
		0x6Au, 0x50u, 0x20u, 0x20u,
		0x0Du, 0x0Au, 0x87u, 0x0Au,
	};

	// 'jp2h' superbox (8 + 22 = 30 bytes) containing 'ihdr' (22 bytes)
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

	// 'jp2c' codestream box
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

	// 3. Decode JPXDecode (JP2 box container and raw J2K codestream).
	{
		const std::array<uint8_t, 12> expected_rgb = {
			10, 20, 30,
			40, 50, 60,
			70, 80, 90,
			100, 150, 200,
		};
		const auto jp2_bytes = make_minimal_jp2_2x2_rgb(expected_rgb, /*enable_mct=*/false);
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

	// 4. Oversized image header rejection (width * height * 4 > 256 MB):
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

		// (c) DCTDecode: SOF0 header claiming 65535 x 65535 pixels
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
		}

		// (d) JPXDecode: J2K SIZ header claiming 100000 x 100000 pixels
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
		}

		EXPECT_TRUE(diagnostics_tail().empty());
	}
}

} // namespace
} // namespace recto::pdf
