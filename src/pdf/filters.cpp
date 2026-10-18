// Lossless PDF stream filter pipeline: FlateDecode (RFC 1950/1951 + raw-deflate
// fallback), LZWDecode, TIFF Predictor 2 and PNG Predictors 10..15,
// ASCIIHexDecode, ASCII85Decode, RunLengthDecode, and chained filter execution
// with a strict 256 MB per-stream decoded output ceiling (ISO 32000-1 section
// 7.4; D01 T01 section 7).
#include "filters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <utility>

namespace recto::pdf {

namespace {

constexpr size_t kMaxInitialReserveBytes = 1024ULL * 1024ULL; // 1 MiB

[[nodiscard]] PdfError make_filter_error(
	ErrorCode code,
	DiagnosticId diag_id,
	std::string_view message,
	std::optional<uint64_t> byte_offset = std::nullopt) noexcept
{
	static_cast<void>(diag_id);
	return PdfError(code, message, byte_offset);
}

// Ensures `out` has capacity for `additional` more bytes without geometric
// allocation growth ever exceeding `max_decoded_bytes`.
void ensure_bounded_capacity(
	std::vector<uint8_t>& out,
	size_t additional,
	size_t max_decoded_bytes)
{
	const size_t required = out.size() + additional;
	if (required <= out.capacity()) {
		return;
	}
	const size_t current_cap = out.capacity();
	const size_t doubled = (current_cap <= max_decoded_bytes / 2u)
		? std::max<size_t>(current_cap * 2u, 256u)
		: max_decoded_bytes;
	const size_t next_cap = std::min<size_t>(
		max_decoded_bytes,
		std::max<size_t>(required, doubled));
	out.reserve(next_cap);
}

[[nodiscard]] constexpr bool is_pdf_whitespace(uint8_t b) noexcept
{
	return b == 0x00 || b == 0x09 || b == 0x0A || b == 0x0C || b == 0x0D || b == 0x20;
}

[[nodiscard]] constexpr int hex_nibble(uint8_t b) noexcept
{
	if (b >= '0' && b <= '9') {
		return static_cast<int>(b - '0');
	}
	if (b >= 'A' && b <= 'F') {
		return static_cast<int>(b - 'A' + 10);
	}
	if (b >= 'a' && b <= 'f') {
		return static_cast<int>(b - 'a' + 10);
	}
	return -1;
}

[[nodiscard]] uint32_t compute_adler32(std::span<const uint8_t> data) noexcept
{
	constexpr uint32_t kModAdler = 65521u;
	uint32_t s1 = 1u;
	uint32_t s2 = 0u;
	size_t idx = 0;
	while (idx < data.size()) {
		const size_t chunk = std::min<size_t>(data.size() - idx, 5552u);
		for (size_t i = 0; i < chunk; ++i) {
			s1 += data[idx + i];
			s2 += s1;
		}
		s1 %= kModAdler;
		s2 %= kModAdler;
		idx += chunk;
	}
	return (s2 << 16u) | s1;
}

// ---------------------------------------------------------------------------
// RFC 1951 Deflate bit reader and canonical Huffman decoder
// ---------------------------------------------------------------------------

struct DeflateBitReader {
	std::span<const uint8_t> bytes{};
	size_t byte_pos{0};
	uint64_t bit_buf{0};
	uint32_t bit_count{0};

	[[nodiscard]] bool ensure_bits(uint32_t needed) noexcept
	{
		while (bit_count < needed && byte_pos < bytes.size()) {
			bit_buf |= static_cast<uint64_t>(bytes[byte_pos++]) << bit_count;
			bit_count += 8u;
		}
		return bit_count >= needed;
	}

	[[nodiscard]] uint32_t peek_bits(uint32_t count) const noexcept
	{
		return static_cast<uint32_t>(bit_buf & ((1ULL << count) - 1ULL));
	}

	void drop_bits(uint32_t count) noexcept
	{
		bit_buf >>= count;
		bit_count -= count;
	}

	[[nodiscard]] bool read_bits(uint32_t count, uint32_t& out) noexcept
	{
		if (count == 0) {
			out = 0;
			return true;
		}
		if (!ensure_bits(count)) {
			return false;
		}
		out = peek_bits(count);
		drop_bits(count);
		return true;
	}

	void align_to_byte() noexcept
	{
		const uint32_t discard = bit_count & 7u;
		drop_bits(discard);
	}

	[[nodiscard]] size_t aligned_byte_position() const noexcept
	{
		return byte_pos - (bit_count / 8u);
	}
};

struct HuffmanTree {
	static constexpr uint32_t kMaxBits = 15;
	static constexpr uint32_t kMaxSymbols = 288;

	std::array<uint16_t, kMaxBits + 1> count{};
	std::array<uint16_t, kMaxSymbols> symbols{};
	uint32_t max_len{0};
	bool empty_tree{false};

	[[nodiscard]] bool build(
		std::span<const uint8_t> lengths,
		bool allow_incomplete_single) noexcept
	{
		count.fill(0);
		symbols.fill(0);
		max_len = 0;
		empty_tree = false;

		for (const uint8_t len : lengths) {
			if (len > kMaxBits) {
				return false;
			}
			if (len > 0) {
				++count[len];
				if (len > max_len) {
					max_len = len;
				}
			}
		}

		if (max_len == 0) {
			empty_tree = true;
			return allow_incomplete_single;
		}

		// Check over-subscription and completeness of the prefix code tree.
		uint32_t total_nonzero = 0;
		int32_t left = 1;
		for (uint32_t bits = 1; bits <= kMaxBits; ++bits) {
			total_nonzero += count[bits];
			left <<= 1;
			left -= static_cast<int32_t>(count[bits]);
			if (left < 0) {
				return false;
			}
		}
		if (left > 0 && !(allow_incomplete_single && total_nonzero == 1u)) {
			return false;
		}

		std::array<uint16_t, kMaxBits + 1> offsets{};
		uint16_t accum = 0;
		for (uint32_t bits = 1; bits <= kMaxBits; ++bits) {
			offsets[bits] = accum;
			accum = static_cast<uint16_t>(accum + count[bits]);
		}

		for (size_t sym = 0; sym < lengths.size(); ++sym) {
			const uint8_t len = lengths[sym];
			if (len != 0) {
				symbols[offsets[len]++] = static_cast<uint16_t>(sym);
			}
		}
		return true;
	}

	[[nodiscard]] bool decode(DeflateBitReader& br, uint16_t& out_sym) const noexcept
	{
		if (empty_tree || max_len == 0) {
			return false;
		}
		uint32_t code = 0;
		uint32_t first = 0;
		uint32_t index = 0;
		for (uint32_t len = 1; len <= max_len; ++len) {
			uint32_t bit = 0;
			if (!br.read_bits(1u, bit)) {
				return false;
			}
			code |= bit;
			const uint32_t c = count[len];
			if (code >= first && code - first < c) {
				out_sym = symbols[index + (code - first)];
				return true;
			}
			index += c;
			first = (first + c) << 1u;
			code <<= 1u;
		}
		return false;
	}
};

[[nodiscard]] PdfResult<void> inflate_huffman_block(
	DeflateBitReader& br,
	const HuffmanTree& lit_len_tree,
	const HuffmanTree& dist_tree,
	std::vector<uint8_t>& out,
	size_t max_decoded_bytes)
{
	static constexpr std::array<uint16_t, 29> kLenBase = {
		3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
		31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	static constexpr std::array<uint8_t, 29> kLenExtra = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
		2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
	static constexpr std::array<uint16_t, 30> kDistBase = {
		1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
		193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
		6145, 8193, 12289, 16385, 24577};
	static constexpr std::array<uint8_t, 30> kDistExtra = {
		0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
		6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

	while (true) {
		uint16_t sym = 0;
		if (!lit_len_tree.decode(br, sym)) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid or truncated Huffman code in FlateDecode stream",
				br.aligned_byte_position());
		}

		if (sym < 256u) {
			if (out.size() >= max_decoded_bytes) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"FlateDecode output exceeds maximum decoded stream byte ceiling",
					br.aligned_byte_position());
			}
			ensure_bounded_capacity(out, 1u, max_decoded_bytes);
			out.push_back(static_cast<uint8_t>(sym));
			continue;
		}

		if (sym == 256u) {
			return {};
		}

		const uint32_t len_idx = sym - 257u;
		if (len_idx >= kLenBase.size()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid length symbol in FlateDecode stream",
				br.aligned_byte_position());
		}

		uint32_t extra_len = 0;
		if (!br.read_bits(kLenExtra[len_idx], extra_len)) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated length extra bits in FlateDecode stream",
				br.aligned_byte_position());
		}
		const size_t match_len = static_cast<size_t>(kLenBase[len_idx]) + extra_len;

		uint16_t dist_sym = 0;
		if (!dist_tree.decode(br, dist_sym) || dist_sym >= kDistBase.size()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid or truncated distance symbol in FlateDecode stream",
				br.aligned_byte_position());
		}

		uint32_t extra_dist = 0;
		if (!br.read_bits(kDistExtra[dist_sym], extra_dist)) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated distance extra bits in FlateDecode stream",
				br.aligned_byte_position());
		}
		const size_t distance = static_cast<size_t>(kDistBase[dist_sym]) + extra_dist;

		if (distance == 0 || distance > out.size()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"FlateDecode back-reference distance precedes start of output buffer",
				br.aligned_byte_position());
		}
		if (match_len > max_decoded_bytes - out.size()) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"FlateDecode match expansion exceeds maximum decoded stream byte ceiling",
				br.aligned_byte_position());
		}

		const size_t start_pos = out.size() - distance;
		ensure_bounded_capacity(out, match_len, max_decoded_bytes);
		out.resize(out.size() + match_len);
		uint8_t* dst = out.data() + start_pos + distance;
		const uint8_t* src = out.data() + start_pos;
		if (distance == 1) {
			std::memset(dst, *src, match_len);
		} else if (distance >= match_len) {
			std::memcpy(dst, src, match_len);
		} else {
			for (size_t i = 0; i < match_len; ++i) {
				dst[i] = src[i];
			}
		}
	}
}

[[nodiscard]] PdfResult<std::vector<uint8_t>> inflate_raw_deflate(
	std::span<const uint8_t> deflate_bytes,
	size_t max_decoded_bytes,
	size_t* out_consumed_bytes = nullptr)
{
	std::vector<uint8_t> out;
	const size_t initial_res = std::min<size_t>(
		{max_decoded_bytes, kMaxInitialReserveBytes, std::max<size_t>(deflate_bytes.size() * 2u, 256u)});
	out.reserve(initial_res);

	DeflateBitReader br{deflate_bytes, 0, 0, 0};
	bool is_final = false;

	while (!is_final) {
		uint32_t bfinal = 0;
		uint32_t btype = 0;
		if (!br.read_bits(1u, bfinal) || !br.read_bits(2u, btype)) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated block header in FlateDecode stream",
				br.aligned_byte_position());
		}
		is_final = (bfinal != 0u);

		if (btype == 0u) {
			// Stored (uncompressed) block
			br.align_to_byte();
			uint32_t len = 0;
			uint32_t nlen = 0;
			if (!br.read_bits(16u, len) || !br.read_bits(16u, nlen)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated stored block length in FlateDecode stream",
					br.aligned_byte_position());
			}
			if ((len ^ 0xFFFFu) != nlen) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Stored block LEN/NLEN complement mismatch in FlateDecode stream",
					br.aligned_byte_position());
			}
			if (len > max_decoded_bytes - out.size()) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"Stored block in FlateDecode exceeds maximum decoded stream byte ceiling",
					br.aligned_byte_position());
			}
			const size_t raw_pos = br.aligned_byte_position();
			if (raw_pos + len > deflate_bytes.size()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated stored block payload in FlateDecode stream",
					raw_pos);
			}
			ensure_bounded_capacity(out, len, max_decoded_bytes);
			out.insert(
				out.end(),
				deflate_bytes.begin() + static_cast<std::ptrdiff_t>(raw_pos),
				deflate_bytes.begin() + static_cast<std::ptrdiff_t>(raw_pos + len));
			br.byte_pos = raw_pos + len;
			br.bit_buf = 0;
			br.bit_count = 0;
		} else if (btype == 1u) {
			// Fixed Huffman block
			std::array<uint8_t, 288> lit_lengths{};
			for (size_t i = 0; i <= 143; ++i) {
				lit_lengths[i] = 8;
			}
			for (size_t i = 144; i <= 255; ++i) {
				lit_lengths[i] = 9;
			}
			for (size_t i = 256; i <= 279; ++i) {
				lit_lengths[i] = 7;
			}
			for (size_t i = 280; i <= 287; ++i) {
				lit_lengths[i] = 8;
			}
			std::array<uint8_t, 32> dist_lengths{};
			dist_lengths.fill(5);

			HuffmanTree lit_tree;
			HuffmanTree dist_tree;
			if (!lit_tree.build(lit_lengths, false) ||
				!dist_tree.build(dist_lengths, true)) {
				return make_filter_error(
					ErrorCode::InternalError,
					DiagnosticId::DamagedStream,
					"Failed to construct fixed Huffman tables");
			}
			auto block_res = inflate_huffman_block(br, lit_tree, dist_tree, out, max_decoded_bytes);
			if (!block_res.has_value()) {
				return block_res.error();
			}
		} else if (btype == 2u) {
			// Dynamic Huffman block
			uint32_t hlit_raw = 0;
			uint32_t hdist_raw = 0;
			uint32_t hclen_raw = 0;
			if (!br.read_bits(5u, hlit_raw) ||
				!br.read_bits(5u, hdist_raw) ||
				!br.read_bits(4u, hclen_raw)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated dynamic Huffman header in FlateDecode stream",
					br.aligned_byte_position());
			}
			const uint32_t hlit = hlit_raw + 257u;
			const uint32_t hdist = hdist_raw + 1u;
			const uint32_t hclen = hclen_raw + 4u;
			if (hlit > 286u || hdist > 30u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Dynamic Huffman HLIT/HDIST count out of valid RFC 1951 range",
					br.aligned_byte_position());
			}

			static constexpr std::array<uint8_t, 19> kCodeLenOrder = {
				16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
			std::array<uint8_t, 19> code_len_lengths{};
			for (uint32_t i = 0; i < hclen; ++i) {
				uint32_t cl = 0;
				if (!br.read_bits(3u, cl)) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated code-length alphabet in dynamic Huffman block",
						br.aligned_byte_position());
				}
				code_len_lengths[kCodeLenOrder[i]] = static_cast<uint8_t>(cl);
			}

			HuffmanTree code_len_tree;
			if (!code_len_tree.build(code_len_lengths, true)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid code-length Huffman tree in FlateDecode stream",
					br.aligned_byte_position());
			}

			const uint32_t total_codes = hlit + hdist;
			std::array<uint8_t, 288 + 32> unpacked_lengths{};
			uint32_t idx = 0;
			while (idx < total_codes) {
				uint16_t cl_sym = 0;
				if (!code_len_tree.decode(br, cl_sym)) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Corrupt code-length symbol in dynamic Huffman block",
						br.aligned_byte_position());
				}
				if (cl_sym <= 15u) {
					unpacked_lengths[idx++] = static_cast<uint8_t>(cl_sym);
				} else if (cl_sym == 16u) {
					if (idx == 0) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Repeat code 16 at start of dynamic Huffman code lengths",
							br.aligned_byte_position());
					}
					uint32_t rep = 0;
					if (!br.read_bits(2u, rep)) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Truncated repeat count for code 16 in dynamic Huffman block",
							br.aligned_byte_position());
					}
					rep += 3u;
					if (idx + rep > total_codes) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Code-length repeat 16 overflows HLIT + HDIST count",
							br.aligned_byte_position());
					}
					const uint8_t prev = unpacked_lengths[idx - 1];
					for (uint32_t r = 0; r < rep; ++r) {
						unpacked_lengths[idx++] = prev;
					}
				} else if (cl_sym == 17u || cl_sym == 18u) {
					const uint32_t extra_bits = (cl_sym == 17u) ? 3u : 7u;
					const uint32_t base_rep = (cl_sym == 17u) ? 3u : 11u;
					uint32_t rep = 0;
					if (!br.read_bits(extra_bits, rep)) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Truncated zero-repeat count in dynamic Huffman block",
							br.aligned_byte_position());
					}
					rep += base_rep;
					if (idx + rep > total_codes) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Zero-repeat overflows HLIT + HDIST count in dynamic Huffman block",
							br.aligned_byte_position());
					}
					for (uint32_t r = 0; r < rep; ++r) {
						unpacked_lengths[idx++] = 0;
					}
				} else {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Out-of-range code-length symbol in dynamic Huffman block",
						br.aligned_byte_position());
				}
			}

			// End-of-block symbol 256 must have non-zero code length.
			if (unpacked_lengths[256] == 0) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Dynamic Huffman literal/length tree is missing EOB symbol 256",
					br.aligned_byte_position());
			}

			HuffmanTree lit_tree;
			HuffmanTree dist_tree;
			const std::span<const uint8_t> lit_span(unpacked_lengths.data(), hlit);
			const std::span<const uint8_t> dist_span(unpacked_lengths.data() + hlit, hdist);
			if (!lit_tree.build(lit_span, true) ||
				!dist_tree.build(dist_span, true)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid dynamic literal/length or distance Huffman tree",
					br.aligned_byte_position());
			}
			auto block_res = inflate_huffman_block(br, lit_tree, dist_tree, out, max_decoded_bytes);
			if (!block_res.has_value()) {
				return block_res.error();
			}
		} else {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Reserved block type 3 in FlateDecode stream",
				br.aligned_byte_position());
		}
	}

	br.align_to_byte();
	if (out_consumed_bytes != nullptr) {
		*out_consumed_bytes = br.aligned_byte_position();
	}
	return out;
}

[[nodiscard]] constexpr bool has_valid_zlib_header(std::span<const uint8_t> input) noexcept
{
	if (input.size() < 2) {
		return false;
	}
	const uint32_t cmf = input[0];
	const uint32_t flg = input[1];
	const uint32_t cm = cmf & 0x0Fu;
	const uint32_t cinfo = (cmf >> 4u) & 0x0Fu;
	const bool fdict = (flg & 0x20u) != 0u;
	return cm == 8u && cinfo <= 7u && !fdict && ((cmf * 256u + flg) % 31u == 0u);
}

[[nodiscard]] constexpr uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) noexcept
{
	const int ia = static_cast<int>(a);
	const int ib = static_cast<int>(b);
	const int ic = static_cast<int>(c);
	const int p = ia + ib - ic;
	const int pa = (p >= ia) ? (p - ia) : (ia - p);
	const int pb = (p >= ib) ? (p - ib) : (ib - p);
	const int pc = (p >= ic) ? (p - ic) : (ic - p);
	if (pa <= pb && pa <= pc) {
		return a;
	}
	if (pb <= pc) {
		return b;
	}
	return c;
}

} // namespace

PdfResult<PredictorParams> parse_predictor_params(const PdfDict* decode_parms) noexcept
{
	PredictorParams params{};
	if (decode_parms == nullptr) {
		return params;
	}

	if (const PdfValue* pred_val = decode_parms->get("Predictor")) {
		const auto v = pred_val->as_int();
		if (!v.has_value() ||
			(*v != 1 && *v != 2 && (*v < 10 || *v > 15))) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Unsupported or non-integer /Predictor in /DecodeParms");
		}
		params.predictor = static_cast<int32_t>(*v);
	}

	if (params.predictor == 1) {
		return params;
	}

	if (const PdfValue* col_val = decode_parms->get("Colors")) {
		const auto v = col_val->as_int();
		if (!v.has_value() || *v < 1 || *v > 32) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid /Colors in /DecodeParms (expected 1..32)");
		}
		params.colors = static_cast<int32_t>(*v);
	}

	if (const PdfValue* bpc_val = decode_parms->get("BitsPerComponent")) {
		const auto v = bpc_val->as_int();
		if (!v.has_value() ||
			(*v != 1 && *v != 2 && *v != 4 && *v != 8 && *v != 16)) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid /BitsPerComponent in /DecodeParms (expected 1, 2, 4, 8, or 16)");
		}
		params.bits_per_component = static_cast<int32_t>(*v);
	}

	if (const PdfValue* cols_val = decode_parms->get("Columns")) {
		const auto v = cols_val->as_int();
		if (!v.has_value() || *v < 1 || *v > 16'777'216) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid /Columns in /DecodeParms");
		}
		params.columns = static_cast<int32_t>(*v);
	}

	return params;
}

PdfResult<std::vector<uint8_t>> apply_predictor(
	std::span<const uint8_t> input,
	const PredictorParams& params,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		if (params.predictor == 1) {
			if (input.size() > effective_limit) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"Predictor output exceeds maximum decoded stream byte ceiling");
			}
			return std::vector<uint8_t>(input.begin(), input.end());
		}

		if (params.colors < 1 || params.colors > 32 ||
			(params.bits_per_component != 1 &&
			 params.bits_per_component != 2 &&
			 params.bits_per_component != 4 &&
			 params.bits_per_component != 8 &&
			 params.bits_per_component != 16) ||
			params.columns < 1 || params.columns > 16'777'216) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid predictor parameters (/Colors, /BitsPerComponent, or /Columns)");
		}

		const uint64_t bits_per_pixel =
			static_cast<uint64_t>(params.colors) * static_cast<uint64_t>(params.bits_per_component);
		const size_t bytes_per_pixel = static_cast<size_t>((bits_per_pixel + 7ULL) / 8ULL);
		const uint64_t row_bits =
			static_cast<uint64_t>(params.columns) * bits_per_pixel;
		const size_t row_bytes = static_cast<size_t>((row_bits + 7ULL) / 8ULL);
		if (row_bytes == 0) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Computed predictor row byte width is zero");
		}

		if (params.predictor == 2) {
			// TIFF Predictor 2: horizontal differencing without a per-row filter tag byte.
			if (input.size() % row_bytes != 0) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"TIFF Predictor 2 stream length is not a multiple of row byte width");
			}
			if (input.size() > effective_limit) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"TIFF Predictor 2 output exceeds maximum decoded stream byte ceiling");
			}

			std::vector<uint8_t> out(input.begin(), input.end());
			const size_t num_rows = out.size() / row_bytes;
			const size_t colors = static_cast<size_t>(params.colors);
			const size_t columns = static_cast<size_t>(params.columns);
			const size_t total_samples = columns * colors;

			for (size_t r = 0; r < num_rows; ++r) {
				uint8_t* row = out.data() + r * row_bytes;
				if (params.bits_per_component == 8) {
					for (size_t s = colors; s < total_samples; ++s) {
						row[s] = static_cast<uint8_t>(row[s] + row[s - colors]);
					}
				} else if (params.bits_per_component == 16) {
					for (size_t s = colors; s < total_samples; ++s) {
						const size_t cur_off = s * 2u;
						const size_t prev_off = (s - colors) * 2u;
						const uint16_t cur_val = static_cast<uint16_t>(
							(static_cast<uint16_t>(row[cur_off]) << 8u) | row[cur_off + 1]);
						const uint16_t prev_val = static_cast<uint16_t>(
							(static_cast<uint16_t>(row[prev_off]) << 8u) | row[prev_off + 1]);
						const uint16_t sum = static_cast<uint16_t>(cur_val + prev_val);
						row[cur_off] = static_cast<uint8_t>((sum >> 8u) & 0xFFu);
						row[cur_off + 1] = static_cast<uint8_t>(sum & 0xFFu);
					}
				} else {
					// Sub-byte components (1, 2, or 4 bits per component), MSB-first within each byte.
					const uint32_t bpc = static_cast<uint32_t>(params.bits_per_component);
					const uint32_t mask = (1u << bpc) - 1u;
					const uint32_t samples_per_byte = 8u / bpc;
					auto get_sample = [&](size_t s) noexcept -> uint32_t {
						const size_t byte_idx = s / samples_per_byte;
						const uint32_t shift = (samples_per_byte - 1u - (s % samples_per_byte)) * bpc;
						return (static_cast<uint32_t>(row[byte_idx]) >> shift) & mask;
					};
					auto set_sample = [&](size_t s, uint32_t val) noexcept {
						const size_t byte_idx = s / samples_per_byte;
						const uint32_t shift = (samples_per_byte - 1u - (s % samples_per_byte)) * bpc;
						row[byte_idx] = static_cast<uint8_t>(
							(row[byte_idx] & ~(mask << shift)) | ((val & mask) << shift));
					};
					for (size_t s = colors; s < total_samples; ++s) {
						const uint32_t sum = (get_sample(s) + get_sample(s - colors)) & mask;
						set_sample(s, sum);
					}
				}
			}
			return out;
		}

		if (params.predictor >= 10 && params.predictor <= 15) {
			// PNG Predictors 10..15: each encoded row starts with a 1-byte filter tag (0..4).
			const size_t encoded_row_stride = row_bytes + 1u;
			if (input.empty()) {
				return std::vector<uint8_t>{};
			}
			if (input.size() % encoded_row_stride != 0) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"PNG Predictor stream length is not a multiple of (row_bytes + 1)");
			}
			const size_t num_rows = input.size() / encoded_row_stride;
			if (num_rows > effective_limit / row_bytes) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"PNG Predictor output exceeds maximum decoded stream byte ceiling");
			}

			std::vector<uint8_t> out(num_rows * row_bytes, 0);
			for (size_t r = 0; r < num_rows; ++r) {
				const uint8_t* src_row = input.data() + r * encoded_row_stride;
				const uint8_t filter_tag = src_row[0];
				const uint8_t* raw = src_row + 1;
				uint8_t* cur = out.data() + r * row_bytes;
				const uint8_t* prev = (r > 0) ? (out.data() + (r - 1) * row_bytes) : nullptr;

				switch (filter_tag) {
				case 0: // None
					std::memcpy(cur, raw, row_bytes);
					break;
				case 1: // Sub
					for (size_t x = 0; x < row_bytes; ++x) {
						const uint8_t left = (x >= bytes_per_pixel) ? cur[x - bytes_per_pixel] : 0u;
						cur[x] = static_cast<uint8_t>(raw[x] + left);
					}
					break;
				case 2: // Up
					if (prev != nullptr) {
						for (size_t x = 0; x < row_bytes; ++x) {
							cur[x] = static_cast<uint8_t>(raw[x] + prev[x]);
						}
					} else {
						std::memcpy(cur, raw, row_bytes);
					}
					break;
				case 3: // Average
					for (size_t x = 0; x < row_bytes; ++x) {
						const uint32_t left = (x >= bytes_per_pixel) ? cur[x - bytes_per_pixel] : 0u;
						const uint32_t up = (prev != nullptr) ? prev[x] : 0u;
						cur[x] = static_cast<uint8_t>(raw[x] + ((left + up) >> 1u));
					}
					break;
				case 4: // Paeth
					for (size_t x = 0; x < row_bytes; ++x) {
						const uint8_t left = (x >= bytes_per_pixel) ? cur[x - bytes_per_pixel] : 0u;
						const uint8_t up = (prev != nullptr) ? prev[x] : 0u;
						const uint8_t up_left =
							(prev != nullptr && x >= bytes_per_pixel) ? prev[x - bytes_per_pixel] : 0u;
						cur[x] = static_cast<uint8_t>(raw[x] + paeth_predictor(left, up, up_left));
					}
					break;
				default:
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Invalid PNG row filter tag byte (expected 0..4)",
						r * encoded_row_stride);
				}
			}
			return out;
		}

		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Unsupported /Predictor value");
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while applying stream predictor");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while applying stream predictor");
	}
}

PdfResult<std::vector<uint8_t>> decode_flate(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		RECTO_PDF_TRY(const PredictorParams pred_params, parse_predictor_params(decode_parms));

		if (input.empty()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Empty FlateDecode input stream",
				0);
		}

		std::vector<uint8_t> inflated;
		if (has_valid_zlib_header(input)) {
			size_t consumed = 0;
			auto zlib_res = inflate_raw_deflate(input.subspan(2), effective_limit, &consumed);
			if (zlib_res.has_value()) {
				// Verify the 4-byte RFC 1950 big-endian Adler-32 trailer when present.
				const std::span<const uint8_t> after_deflate = input.subspan(2 + consumed);
				if (after_deflate.size() >= 4) {
					const uint32_t expected_adler =
						(static_cast<uint32_t>(after_deflate[0]) << 24u) |
						(static_cast<uint32_t>(after_deflate[1]) << 16u) |
						(static_cast<uint32_t>(after_deflate[2]) << 8u) |
						static_cast<uint32_t>(after_deflate[3]);
					const uint32_t actual_adler = compute_adler32(*zlib_res);
					if (expected_adler != actual_adler) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"FlateDecode RFC 1950 Adler-32 checksum mismatch",
							2 + consumed);
					}
				}
				inflated = std::move(*zlib_res);
			} else if (
				zlib_res.error().code == ErrorCode::DecompressionLimitExceeded ||
				zlib_res.error().code == ErrorCode::OutOfMemory) {
				return zlib_res.error();
			} else {
				// Fallback to raw RFC 1951 deflate from byte 0 in case a raw-deflate
				// stream coincidentally passed the 2-byte modulo-31 check.
				auto raw_res = inflate_raw_deflate(input, effective_limit);
				if (!raw_res.has_value()) {
					return zlib_res.error();
				}
				inflated = std::move(*raw_res);
			}
		} else {
			// Automatic raw-deflate fallback when the 2-byte RFC 1950 header is
			// omitted or fails checksum; if raw deflate from byte 0 fails and
			// byte 0 looks like a deflate CMF byte (CM == 8, CINFO <= 7) with a
			// corrupt FLG checksum byte, also try skipping the 2-byte header.
			auto raw_res = inflate_raw_deflate(input, effective_limit);
			if (raw_res.has_value()) {
				inflated = std::move(*raw_res);
			} else if (
				raw_res.error().code == ErrorCode::DecompressionLimitExceeded ||
				raw_res.error().code == ErrorCode::OutOfMemory) {
				return raw_res.error();
			} else if (
				input.size() > 2 &&
				(input[0] & 0x0Fu) == 8u &&
				((input[0] >> 4u) & 0x0Fu) <= 7u) {
				auto skip2_res = inflate_raw_deflate(input.subspan(2), effective_limit);
				if (!skip2_res.has_value()) {
					return raw_res.error();
				}
				inflated = std::move(*skip2_res);
			} else {
				return raw_res.error();
			}
		}

		if (pred_params.predictor == 1) {
			return inflated;
		}
		return apply_predictor(inflated, pred_params, effective_limit);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding FlateDecode stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding FlateDecode stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_lzw(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		RECTO_PDF_TRY(const PredictorParams pred_params, parse_predictor_params(decode_parms));

		int32_t early_change = 1;
		if (decode_parms != nullptr) {
			if (const PdfValue* ec_val = decode_parms->get("EarlyChange")) {
				const auto v = ec_val->as_int();
				if (!v.has_value() || (*v != 0 && *v != 1)) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Invalid /EarlyChange in LZWDecode /DecodeParms (expected 0 or 1)");
				}
				early_change = static_cast<int32_t>(*v);
			}
		}

		if (input.empty()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Empty LZWDecode input stream",
				0);
		}

		struct LzwEntry {
			uint32_t offset{0};
			uint32_t length{0};
		};

		std::array<LzwEntry, 4096> table{};
		std::vector<uint8_t> dict_pool;
		dict_pool.reserve(16384);

		uint32_t code_bits = 9u;
		uint32_t next_code = 258u;
		const uint32_t threshold_delta = (early_change != 0) ? 1u : 0u;
		std::optional<uint32_t> prev_code = std::nullopt;

		auto reset_table = [&]() noexcept {
			dict_pool.clear();
			code_bits = 9u;
			next_code = 258u;
			prev_code = std::nullopt;
		};

		uint8_t single_byte_buf = 0;
		auto get_code_span = [&](uint32_t code) noexcept -> std::span<const uint8_t> {
			if (code < 256u) {
				single_byte_buf = static_cast<uint8_t>(code);
				return {&single_byte_buf, 1u};
			}
			const LzwEntry& e = table[code];
			return {dict_pool.data() + e.offset, e.length};
		};

		size_t byte_pos = 0;
		uint64_t bit_buf = 0;
		uint32_t bit_count = 0;
		auto read_code_msb = [&](uint32_t bits, uint32_t& out_code) noexcept -> bool {
			while (bit_count < bits && byte_pos < input.size()) {
				bit_buf = (bit_buf << 8u) | static_cast<uint64_t>(input[byte_pos++]);
				bit_count += 8u;
			}
			if (bit_count < bits) {
				return false;
			}
			bit_count -= bits;
			out_code = static_cast<uint32_t>((bit_buf >> bit_count) & ((1ULL << bits) - 1ULL));
			return true;
		};

		std::vector<uint8_t> out;
		out.reserve(std::min<size_t>(
			{effective_limit, kMaxInitialReserveBytes, std::max<size_t>(input.size() * 2u, 256u)}));

		std::vector<uint8_t> cur_seq;
		cur_seq.reserve(256);

		bool saw_eod = false;
		uint32_t code = 0;
		while (read_code_msb(code_bits, code)) {
			if (code == 256u) {
				reset_table();
				continue;
			}
			if (code == 257u) {
				saw_eod = true;
				break;
			}

			if (code < next_code) {
				const auto span = get_code_span(code);
				cur_seq.assign(span.begin(), span.end());
			} else if (code == next_code && prev_code.has_value()) {
				const auto prev_span = get_code_span(*prev_code);
				cur_seq.assign(prev_span.begin(), prev_span.end());
				cur_seq.push_back(prev_span[0]);
			} else {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Out-of-range LZW code in LZWDecode stream",
					byte_pos);
			}

			if (cur_seq.size() > effective_limit - out.size()) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"LZWDecode output exceeds maximum decoded stream byte ceiling",
					byte_pos);
			}
			ensure_bounded_capacity(out, cur_seq.size(), effective_limit);
			out.insert(out.end(), cur_seq.begin(), cur_seq.end());

			if (prev_code.has_value() && next_code < 4096u) {
				const uint32_t pcode = *prev_code;
				const uint32_t new_off = static_cast<uint32_t>(dict_pool.size());
				if (pcode < 256u) {
					dict_pool.push_back(static_cast<uint8_t>(pcode));
					dict_pool.push_back(cur_seq[0]);
					table[next_code] = LzwEntry{new_off, 2u};
				} else {
					const uint32_t src_off = table[pcode].offset;
					const uint32_t src_len = table[pcode].length;
					const uint32_t new_len = src_len + 1u;
					dict_pool.resize(static_cast<size_t>(new_off) + new_len);
					std::memcpy(
						dict_pool.data() + new_off,
						dict_pool.data() + src_off,
						src_len);
					dict_pool[new_off + src_len] = cur_seq[0];
					table[next_code] = LzwEntry{new_off, new_len};
				}
				++next_code;

				if (next_code == (1u << code_bits) - threshold_delta && code_bits < 12u) {
					++code_bits;
				}
			}

			prev_code = code;
		}

		if (!saw_eod) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated LZWDecode stream: missing EOD code 257",
				byte_pos);
		}

		if (pred_params.predictor == 1) {
			return out;
		}
		return apply_predictor(out, pred_params, effective_limit);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding LZWDecode stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding LZWDecode stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_ascii_hex(
	std::span<const uint8_t> input,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		std::vector<uint8_t> out;
		out.reserve(std::min<size_t>({effective_limit, kMaxInitialReserveBytes, (input.size() + 1u) / 2u}));

		int high_nibble = -1;
		bool saw_eod = false;

		for (size_t i = 0; i < input.size(); ++i) {
			const uint8_t b = input[i];
			if (is_pdf_whitespace(b)) {
				continue;
			}
			if (b == '>') {
				saw_eod = true;
				break;
			}
			const int nib = hex_nibble(b);
			if (nib < 0) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid non-hexadecimal character in ASCIIHexDecode stream",
					i);
			}
			if (high_nibble < 0) {
				high_nibble = nib;
			} else {
				if (out.size() >= effective_limit) {
					return make_filter_error(
						ErrorCode::DecompressionLimitExceeded,
						DiagnosticId::DecompressionLimitExceeded,
						"ASCIIHexDecode output exceeds maximum decoded stream byte ceiling",
						i);
				}
				ensure_bounded_capacity(out, 1u, effective_limit);
				out.push_back(static_cast<uint8_t>((high_nibble << 4) | nib));
				high_nibble = -1;
			}
		}

		if (!saw_eod) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Missing '>' EOD marker in ASCIIHexDecode stream",
				input.size());
		}

		if (high_nibble >= 0) {
			if (out.size() >= effective_limit) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"ASCIIHexDecode output exceeds maximum decoded stream byte ceiling",
					input.size());
			}
			ensure_bounded_capacity(out, 1u, effective_limit);
			out.push_back(static_cast<uint8_t>(high_nibble << 4));
		}

		return out;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding ASCIIHexDecode stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding ASCIIHexDecode stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_ascii85(
	std::span<const uint8_t> input,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		std::vector<uint8_t> out;
		out.reserve(std::min<size_t>({effective_limit, kMaxInitialReserveBytes, input.size()}));

		std::array<uint32_t, 5> tuple{};
		size_t tuple_len = 0;
		bool saw_eod = false;

		for (size_t i = 0; i < input.size(); ++i) {
			const uint8_t b = input[i];
			if (is_pdf_whitespace(b)) {
				continue;
			}
			if (b == '~') {
				if (i + 1 < input.size() && input[i + 1] == '>') {
					saw_eod = true;
					break;
				}
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Malformed '~' without '>' in ASCII85Decode EOD marker",
					i);
			}
			if (b == 'z') {
				if (tuple_len != 0) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Invalid 'z' inside partial tuple in ASCII85Decode stream",
						i);
				}
				if (4u > effective_limit - out.size()) {
					return make_filter_error(
						ErrorCode::DecompressionLimitExceeded,
						DiagnosticId::DecompressionLimitExceeded,
						"ASCII85Decode output exceeds maximum decoded stream byte ceiling",
						i);
				}
				ensure_bounded_capacity(out, 4u, effective_limit);
				out.insert(out.end(), {0u, 0u, 0u, 0u});
				continue;
			}
			if (b < '!' || b > 'u') {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid character outside '!'..'u' in ASCII85Decode stream",
					i);
			}

			tuple[tuple_len++] = static_cast<uint32_t>(b - '!');
			if (tuple_len == 5) {
				uint64_t val = 0;
				for (size_t k = 0; k < 5; ++k) {
					val = val * 85ULL + tuple[k];
				}
				if (val > 0xFFFFFFFFULL) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"ASCII85Decode 5-tuple overflows 32-bit unsigned integer",
						i);
				}
				if (4u > effective_limit - out.size()) {
					return make_filter_error(
						ErrorCode::DecompressionLimitExceeded,
						DiagnosticId::DecompressionLimitExceeded,
						"ASCII85Decode output exceeds maximum decoded stream byte ceiling",
						i);
				}
				ensure_bounded_capacity(out, 4u, effective_limit);
				out.push_back(static_cast<uint8_t>((val >> 24u) & 0xFFu));
				out.push_back(static_cast<uint8_t>((val >> 16u) & 0xFFu));
				out.push_back(static_cast<uint8_t>((val >> 8u) & 0xFFu));
				out.push_back(static_cast<uint8_t>(val & 0xFFu));
				tuple_len = 0;
			}
		}

		if (!saw_eod) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Missing '~>' EOD marker in ASCII85Decode stream",
				input.size());
		}

		if (tuple_len == 1) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid single-character final tuple in ASCII85Decode stream",
				input.size());
		}

		if (tuple_len >= 2) {
			for (size_t k = tuple_len; k < 5; ++k) {
				tuple[k] = 84u; // Pad with 'u' ('u' - '!' == 84)
			}
			uint64_t val = 0;
			for (size_t k = 0; k < 5; ++k) {
				val = val * 85ULL + tuple[k];
			}
			if (val > 0xFFFFFFFFULL) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"ASCII85Decode final partial tuple overflows 32-bit unsigned integer",
					input.size());
			}
			const size_t emit_bytes = tuple_len - 1u;
			if (emit_bytes > effective_limit - out.size()) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"ASCII85Decode output exceeds maximum decoded stream byte ceiling",
					input.size());
			}
			ensure_bounded_capacity(out, emit_bytes, effective_limit);
			for (size_t b_idx = 0; b_idx < emit_bytes; ++b_idx) {
				const uint32_t shift = static_cast<uint32_t>((3u - b_idx) * 8u);
				out.push_back(static_cast<uint8_t>((val >> shift) & 0xFFu));
			}
		}

		return out;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding ASCII85Decode stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding ASCII85Decode stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_run_length(
	std::span<const uint8_t> input,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		std::vector<uint8_t> out;
		out.reserve(std::min<size_t>({effective_limit, kMaxInitialReserveBytes, input.size()}));

		size_t pos = 0;
		bool saw_eod = false;
		while (pos < input.size()) {
			const uint8_t length_byte = input[pos++];
			if (length_byte == 128u) {
				saw_eod = true;
				break;
			}
			if (length_byte < 128u) {
				const size_t count = static_cast<size_t>(length_byte) + 1u;
				if (pos + count > input.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated literal run in RunLengthDecode stream",
						pos - 1u);
				}
				if (count > effective_limit - out.size()) {
					return make_filter_error(
						ErrorCode::DecompressionLimitExceeded,
						DiagnosticId::DecompressionLimitExceeded,
						"RunLengthDecode output exceeds maximum decoded stream byte ceiling",
						pos - 1u);
				}
				ensure_bounded_capacity(out, count, effective_limit);
				out.insert(
					out.end(),
					input.begin() + static_cast<std::ptrdiff_t>(pos),
					input.begin() + static_cast<std::ptrdiff_t>(pos + count));
				pos += count;
			} else {
				const size_t count = 257u - static_cast<size_t>(length_byte);
				if (pos >= input.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated repeat run in RunLengthDecode stream",
						pos - 1u);
				}
				if (count > effective_limit - out.size()) {
					return make_filter_error(
						ErrorCode::DecompressionLimitExceeded,
						DiagnosticId::DecompressionLimitExceeded,
						"RunLengthDecode output exceeds maximum decoded stream byte ceiling",
						pos - 1u);
				}
				const uint8_t repeat_byte = input[pos++];
				ensure_bounded_capacity(out, count, effective_limit);
				out.insert(out.end(), count, repeat_byte);
			}
		}

		if (!saw_eod) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Missing EOD byte 128 in RunLengthDecode stream",
				input.size());
		}

		return out;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding RunLengthDecode stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding RunLengthDecode stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_single_filter(
	std::string_view filter_name,
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	if (filter_name == "FlateDecode" || filter_name == "Fl") {
		return decode_flate(input, decode_parms, max_decoded_bytes);
	}
	if (filter_name == "LZWDecode" || filter_name == "LZW") {
		return decode_lzw(input, decode_parms, max_decoded_bytes);
	}
	if (filter_name == "ASCIIHexDecode" || filter_name == "AHx") {
		return decode_ascii_hex(input, max_decoded_bytes);
	}
	if (filter_name == "ASCII85Decode" || filter_name == "A85") {
		return decode_ascii85(input, max_decoded_bytes);
	}
	if (filter_name == "RunLengthDecode" || filter_name == "RL") {
		return decode_run_length(input, max_decoded_bytes);
	}
	if (filter_name == "Crypt") {
		// Identity Crypt filter passes bytes through unchanged; non-Identity
		// Crypt filters are handled by the decryption layer (§9).
		if (decode_parms != nullptr) {
			if (const auto crypt_name = decode_parms->get_name("Name")) {
				if (*crypt_name != "Identity") {
					return make_pdf_error(
						ErrorCode::UnsupportedFeature,
						"Non-Identity /Crypt filter requires security handler");
				}
			}
		}
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		if (input.size() > effective_limit) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"Crypt Identity stream exceeds maximum decoded stream byte ceiling");
		}
		try {
			return std::vector<uint8_t>(input.begin(), input.end());
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory in Crypt Identity filter");
		}
	}
	if (filter_name == "CCITTFaxDecode" || filter_name == "CCF" ||
		filter_name == "JBIG2Decode" ||
		filter_name == "DCTDecode" || filter_name == "DCT" ||
		filter_name == "JPXDecode") {
		return make_pdf_error(
			ErrorCode::UnsupportedFeature,
			"Bi-level and image stream filters are implemented in section 8");
	}
	return make_filter_error(
		ErrorCode::DamagedFormat,
		DiagnosticId::DamagedStream,
		"Unknown or unsupported PDF stream /Filter name");
}

PdfResult<std::vector<uint8_t>> decode_stream(
	std::span<const uint8_t> raw_bytes,
	const PdfDict& stream_dict,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);

		const PdfValue* f_spec = stream_dict.get("F");
		const bool has_external_file = (f_spec != nullptr && !f_spec->is_null());

		const PdfValue* filter_val = stream_dict.get("Filter");
		bool using_external_filters = false;
		if (has_external_file) {
			if (raw_bytes.empty()) {
				return make_pdf_error(
					ErrorCode::UnsupportedFeature,
					"External stream file specification (/F) requires caller-supplied stream bytes");
			}
			// When caller supplies external stream bytes for a stream dictionary
			// with /F, ISO 32000-1 Table 5 specifies /FFilter and /FDecodeParms
			// (ignoring inline /Filter and /DecodeParms).
			using_external_filters = true;
			filter_val = stream_dict.get("FFilter");
		} else if (filter_val == nullptr || filter_val->is_null()) {
			if (const PdfValue* ffilter_only = stream_dict.get("FFilter");
				ffilter_only != nullptr && !ffilter_only->is_null()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Stream dictionary specifies /FFilter without /F external file specification");
			}
			if (const PdfValue* fparms_only = stream_dict.get("FDecodeParms");
				fparms_only != nullptr && !fparms_only->is_null()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Stream dictionary specifies /FDecodeParms without /F external file specification");
			}
		}

		if (filter_val == nullptr || filter_val->is_null()) {
			if (raw_bytes.size() > effective_limit) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"Unfiltered stream exceeds maximum decoded stream byte ceiling");
			}
			return std::vector<uint8_t>(raw_bytes.begin(), raw_bytes.end());
		}

		const PdfValue* parms_val = nullptr;
		if (using_external_filters) {
			parms_val = stream_dict.get("FDecodeParms");
		} else {
			parms_val = stream_dict.get("DecodeParms");
			if (parms_val == nullptr) {
				parms_val = stream_dict.get("DP");
			}
		}

		std::array<std::string_view, kMaxFilterChainDepth> filter_names{};
		std::array<const PdfDict*, kMaxFilterChainDepth> filter_parms{};
		size_t num_filters = 0;

		if (const auto single_name = filter_val->as_name()) {
			filter_names[0] = *single_name;
			num_filters = 1;
			if (parms_val != nullptr && !parms_val->is_null()) {
				if (const PdfDict* d = parms_val->as_dict()) {
					filter_parms[0] = d;
				} else if (const PdfArray* a = parms_val->as_array()) {
					if (a->size() == 1) {
						if ((*a)[0].is_dict()) {
							filter_parms[0] = (*a)[0].as_dict();
						} else if (!(*a)[0].is_null()) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Invalid /DecodeParms array element type for single /Filter");
						}
					} else if (!a->empty()) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"/DecodeParms array length does not match single /Filter");
					}
				} else {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Invalid /DecodeParms type for single /Filter");
				}
			}
		} else if (const PdfArray* filter_arr = filter_val->as_array()) {
			if (filter_arr->size() > kMaxFilterChainDepth) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Chained /Filter array exceeds maximum pipeline depth of 8");
			}
			num_filters = filter_arr->size();
			for (size_t i = 0; i < num_filters; ++i) {
				const auto fname = (*filter_arr)[i].as_name();
				if (!fname.has_value()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Non-name element in /Filter array");
				}
				filter_names[i] = *fname;
			}

			if (parms_val != nullptr && !parms_val->is_null()) {
				if (const PdfArray* parms_arr = parms_val->as_array()) {
					if (!parms_arr->empty() && parms_arr->size() != num_filters) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"/DecodeParms array length does not match /Filter array length");
					}
					for (size_t i = 0; i < parms_arr->size(); ++i) {
						if ((*parms_arr)[i].is_dict()) {
							filter_parms[i] = (*parms_arr)[i].as_dict();
						} else if (!(*parms_arr)[i].is_null()) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Invalid /DecodeParms array element (expected dictionary or null)");
						}
					}
				} else if (const PdfDict* single_dict = parms_val->as_dict()) {
					if (num_filters == 1) {
						filter_parms[0] = single_dict;
					} else {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Single dictionary /DecodeParms provided for multi-stage /Filter array");
					}
				} else {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Invalid /DecodeParms type for /Filter array");
				}
			}
		} else {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid /Filter value in stream dictionary (expected name or array)");
		}

		if (num_filters == 0) {
			if (raw_bytes.size() > effective_limit) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"Unfiltered stream exceeds maximum decoded stream byte ceiling");
			}
			return std::vector<uint8_t>(raw_bytes.begin(), raw_bytes.end());
		}

		std::vector<uint8_t> stage_buf;
		for (size_t i = 0; i < num_filters; ++i) {
			const std::span<const uint8_t> stage_in =
				(i == 0) ? raw_bytes : std::span<const uint8_t>(stage_buf);
			RECTO_PDF_TRY(
				std::vector<uint8_t> next_buf,
				decode_single_filter(filter_names[i], stage_in, filter_parms[i], effective_limit));
			stage_buf = std::move(next_buf);
		}
		return stage_buf;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while executing stream filter pipeline");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while executing stream filter pipeline");
	}
}

PdfResult<std::vector<uint8_t>> decode_stream(
	const PdfStream& stream,
	size_t max_decoded_bytes) noexcept
{
	return decode_stream(stream.raw_bytes, stream.dict, max_decoded_bytes);
}

} // namespace recto::pdf
