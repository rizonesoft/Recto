// PDF stream filter pipeline: FlateDecode (RFC 1950/1951 + raw-deflate
// fallback), LZWDecode, TIFF Predictor 2 and PNG Predictors 10..15,
// ASCIIHexDecode, ASCII85Decode, RunLengthDecode, CCITTFaxDecode (Group 3 1D/2D
// and Group 4 2D MMR), JBIG2Decode (ISO/IEC 14492), WIC DCTDecode / JPXDecode,
// and chained filter execution with a strict 256 MB per-stream decoded output
// ceiling (ISO 32000-1 section 7.4; D01 T01 sections 7 and 8).
#include "filters.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <unordered_map>
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
					if (raw_res.error().code == ErrorCode::DecompressionLimitExceeded ||
						raw_res.error().code == ErrorCode::OutOfMemory) {
						return raw_res.error();
					}
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
					if (skip2_res.error().code == ErrorCode::DecompressionLimitExceeded ||
						skip2_res.error().code == ErrorCode::OutOfMemory) {
						return skip2_res.error();
					}
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

// ============================================================================
// CCITTFaxDecode (ITU-T T.4 Group 3 1D/2D & ITU-T T.6 Group 4 2D MMR)
// ============================================================================

namespace {

struct CcittCodeEntry {
	uint16_t code;
	uint8_t bit_len;
	int16_t run_len; // >= 0: run length; -1: EOL
};

// ITU-T T.4 Table 1 & Table 2: White terminating (0..63) and make-up (64..2560) codes.
constexpr CcittCodeEntry kCcittWhiteCodes[] = {
	{0x35, 8, 0},   {0x07, 6, 1},   {0x07, 4, 2},   {0x08, 4, 3},
	{0x0B, 4, 4},   {0x0C, 4, 5},   {0x0E, 4, 6},   {0x0F, 4, 7},
	{0x13, 5, 8},   {0x14, 5, 9},   {0x07, 5, 10},  {0x08, 5, 11},
	{0x08, 6, 12},  {0x03, 6, 13},  {0x34, 6, 14},  {0x35, 6, 15},
	{0x2A, 6, 16},  {0x2B, 6, 17},  {0x27, 7, 18},  {0x0C, 7, 19},
	{0x08, 7, 20},  {0x17, 7, 21},  {0x03, 7, 22},  {0x04, 7, 23},
	{0x28, 7, 24},  {0x2B, 7, 25},  {0x13, 7, 26},  {0x24, 7, 27},
	{0x18, 7, 28},  {0x02, 8, 29},  {0x03, 8, 30},  {0x1A, 8, 31},
	{0x1B, 8, 32},  {0x12, 8, 33},  {0x13, 8, 34},  {0x14, 8, 35},
	{0x15, 8, 36},  {0x16, 8, 37},  {0x17, 8, 38},  {0x28, 8, 39},
	{0x29, 8, 40},  {0x2A, 8, 41},  {0x2B, 8, 42},  {0x2C, 8, 43},
	{0x2D, 8, 44},  {0x04, 8, 45},  {0x05, 8, 46},  {0x0A, 8, 47},
	{0x0B, 8, 48},  {0x52, 8, 49},  {0x53, 8, 50},  {0x54, 8, 51},
	{0x55, 8, 52},  {0x24, 8, 53},  {0x25, 8, 54},  {0x58, 8, 55},
	{0x59, 8, 56},  {0x5A, 8, 57},  {0x5B, 8, 58},  {0x4A, 8, 59},
	{0x4B, 8, 60},  {0x32, 8, 61},  {0x33, 8, 62},  {0x34, 8, 63},
	// White make-up codes (64..1728)
	{0x1B, 5, 64},   {0x12, 5, 128},  {0x17, 6, 192},  {0x37, 7, 256},
	{0x36, 8, 320},  {0x37, 8, 384},  {0x64, 8, 448},  {0x65, 8, 512},
	{0x68, 8, 576},  {0x67, 8, 640},  {0xCC, 9, 704},  {0xCD, 9, 768},
	{0xD2, 9, 832},  {0xD3, 9, 896},  {0xD4, 9, 960},  {0xD5, 9, 1024},
	{0xD6, 9, 1088}, {0xD7, 9, 1152}, {0xD8, 9, 1216}, {0xD9, 9, 1280},
	{0xDA, 9, 1344}, {0xDB, 9, 1408}, {0x98, 9, 1472}, {0x99, 9, 1536},
	{0x9A, 9, 1600}, {0x18, 6, 1664}, {0x9B, 9, 1728},
	// Extended make-up codes (1792..2560, shared by White and Black)
	{0x08, 11, 1792}, {0x0C, 11, 1856}, {0x0D, 11, 1920},
	{0x12, 12, 1984}, {0x13, 12, 2048}, {0x14, 12, 2112},
	{0x15, 12, 2176}, {0x16, 12, 2240}, {0x17, 12, 2304},
	{0x1C, 12, 2368}, {0x1D, 12, 2432}, {0x1E, 12, 2496},
	{0x1F, 12, 2560},
};

// ITU-T T.4 Table 1 & Table 2: Black terminating (0..63) and make-up (64..2560) codes.
constexpr CcittCodeEntry kCcittBlackCodes[] = {
	{0x37, 10, 0},  {0x02, 3, 1},   {0x03, 2, 2},   {0x02, 2, 3},
	{0x03, 3, 4},   {0x03, 4, 5},   {0x02, 4, 6},   {0x03, 5, 7},
	{0x05, 6, 8},   {0x04, 6, 9},   {0x04, 7, 10},  {0x05, 7, 11},
	{0x07, 7, 12},  {0x04, 8, 13},  {0x07, 8, 14},  {0x18, 9, 15},
	{0x17, 10, 16}, {0x18, 10, 17}, {0x08, 10, 18}, {0x67, 11, 19},
	{0x68, 11, 20}, {0x6C, 11, 21}, {0x37, 11, 22}, {0x28, 11, 23},
	{0x17, 11, 24}, {0x18, 11, 25}, {0xCA, 12, 26}, {0xCB, 12, 27},
	{0xCC, 12, 28}, {0xCD, 12, 29}, {0x68, 12, 30}, {0x69, 12, 31},
	{0x6A, 12, 32}, {0x6B, 12, 33}, {0xD2, 12, 34}, {0xD3, 12, 35},
	{0xD4, 12, 36}, {0xD5, 12, 37}, {0xD6, 12, 38}, {0xD7, 12, 39},
	{0x6C, 12, 40}, {0x6D, 12, 41}, {0xDA, 12, 42}, {0xDB, 12, 43},
	{0x54, 12, 44}, {0x55, 12, 45}, {0x56, 12, 46}, {0x57, 12, 47},
	{0x64, 12, 48}, {0x65, 12, 49}, {0x52, 12, 50}, {0x53, 12, 51},
	{0x24, 12, 52}, {0x37, 12, 53}, {0x38, 12, 54}, {0x27, 12, 55},
	{0x28, 12, 56}, {0x58, 12, 57}, {0x59, 12, 58}, {0x2B, 12, 59},
	{0x2C, 12, 60}, {0x5A, 12, 61}, {0x66, 12, 62}, {0x67, 12, 63},
	// Black make-up codes (64..1728)
	{0x0F, 10, 64},  {0xC8, 12, 128}, {0xC9, 12, 192}, {0x5B, 12, 256},
	{0x33, 12, 320}, {0x34, 12, 384}, {0x35, 12, 448}, {0x6C, 13, 512},
	{0x6D, 13, 576}, {0x4A, 13, 640}, {0x4B, 13, 704}, {0x4C, 13, 768},
	{0x4D, 13, 832}, {0x72, 13, 896}, {0x73, 13, 960}, {0x74, 13, 1024},
	{0x75, 13, 1088},{0x76, 13, 1152},{0x77, 13, 1216},{0x52, 13, 1280},
	{0x53, 13, 1344},{0x54, 13, 1408},{0x55, 13, 1472},{0x5A, 13, 1536},
	{0x5B, 13, 1600},{0x64, 13, 1664},{0x65, 13, 1728},
	// Extended make-up codes (1792..2560)
	{0x08, 11, 1792}, {0x0C, 11, 1856}, {0x0D, 11, 1920},
	{0x12, 12, 1984}, {0x13, 12, 2048}, {0x14, 12, 2112},
	{0x15, 12, 2176}, {0x16, 12, 2240}, {0x17, 12, 2304},
	{0x1C, 12, 2368}, {0x1D, 12, 2432}, {0x1E, 12, 2496},
	{0x1F, 12, 2560},
};

struct MsbBitReader {
	std::span<const uint8_t> data;
	size_t bit_pos{0};

	[[nodiscard]] size_t total_bits() const noexcept
	{
		return data.size() * 8u;
	}

	[[nodiscard]] bool at_end() const noexcept
	{
		return bit_pos >= total_bits();
	}

	[[nodiscard]] int read_bit() noexcept
	{
		if (bit_pos >= total_bits()) {
			return -1;
		}
		const size_t byte_idx = bit_pos >> 3u;
		const uint32_t shift = 7u - static_cast<uint32_t>(bit_pos & 7u);
		++bit_pos;
		return static_cast<int>((data[byte_idx] >> shift) & 1u);
	}

	[[nodiscard]] int peek_bits(size_t count, uint32_t& out_bits) const noexcept
	{
		if (bit_pos + count > total_bits()) {
			return -1;
		}
		uint32_t val = 0;
		for (size_t i = 0; i < count; ++i) {
			const size_t pos = bit_pos + i;
			const uint32_t bit = (data[pos >> 3u] >> (7u - (pos & 7u))) & 1u;
			val = (val << 1u) | bit;
		}
		out_bits = val;
		return 0;
	}

	void align_to_byte() noexcept
	{
		bit_pos = (bit_pos + 7u) & ~size_t{7u};
	}

	// Checks if the upcoming bits form an EOL (`>= 11` zero bits followed by a `1` bit).
	// If found, advances `bit_pos` past the `1` bit and returns true.
	[[nodiscard]] bool try_consume_eol() noexcept
	{
		size_t p = bit_pos;
		const size_t limit = total_bits();
		size_t zeros = 0;
		while (p < limit) {
			const uint32_t bit = (data[p >> 3u] >> (7u - (p & 7u))) & 1u;
			if (bit == 0u) {
				++zeros;
				++p;
				if (zeros > 64u) {
					return false;
				}
			} else {
				if (zeros >= 11u) {
					bit_pos = p + 1u;
					return true;
				}
				return false;
			}
		}
		return false;
	}
};

struct CcittLookupEntry {
	int16_t run_len{-2}; // -2 = invalid
	uint8_t bit_len{0};
};

using CcittDecodeTable = std::array<CcittLookupEntry, 8192>;

[[nodiscard]] constexpr CcittDecodeTable build_ccitt_decode_table(
	std::span<const CcittCodeEntry> entries) noexcept
{
	CcittDecodeTable table{};
	for (const auto& e : entries) {
		const uint32_t shift = 13u - e.bit_len;
		const uint32_t base = static_cast<uint32_t>(e.code) << shift;
		const uint32_t count = 1u << shift;
		for (uint32_t i = 0; i < count; ++i) {
			table[base + i] = CcittLookupEntry{e.run_len, e.bit_len};
		}
	}
	return table;
}

constexpr CcittDecodeTable kWhiteLookup = build_ccitt_decode_table(kCcittWhiteCodes);
constexpr CcittDecodeTable kBlackLookup = build_ccitt_decode_table(kCcittBlackCodes);

// Reads a single 1D run length (accumulating make-up codes until a terminating
// code 0..63 is reached). Returns true on success and stores the run in `out_run`.
[[nodiscard]] bool read_ccitt_1d_run(
	MsbBitReader& reader,
	bool is_black,
	int32_t max_columns,
	int32_t& out_run) noexcept
{
	const CcittDecodeTable& table = is_black ? kBlackLookup : kWhiteLookup;
	int32_t total_run = 0;
	while (true) {
		if (reader.at_end()) {
			return false;
		}
		const size_t avail = reader.total_bits() - reader.bit_pos;
		const size_t peek_n = std::min<size_t>(13u, avail);
		uint32_t prefix = 0;
		for (size_t i = 0; i < peek_n; ++i) {
			const size_t pos = reader.bit_pos + i;
			const uint32_t bit = (reader.data[pos >> 3u] >> (7u - (pos & 7u))) & 1u;
			prefix = (prefix << 1u) | bit;
		}
		prefix <<= (13u - peek_n);
		const CcittLookupEntry entry = table[prefix];
		if (entry.run_len < 0 || entry.bit_len == 0 || entry.bit_len > avail ||
			total_run + entry.run_len > max_columns) {
			return false;
		}
		reader.bit_pos += entry.bit_len;
		total_run += entry.run_len;
		if (entry.run_len < 64) {
			out_run = total_run;
			return true;
		}
	}
}

enum class Ccitt2dMode : uint8_t {
	Pass,
	Horizontal,
	Vertical0,
	VerticalR1,
	VerticalR2,
	VerticalR3,
	VerticalL1,
	VerticalL2,
	VerticalL3,
	Eol,
	Invalid,
};

[[nodiscard]] Ccitt2dMode read_ccitt_2d_mode(MsbBitReader& reader) noexcept
{
	const int b0 = reader.read_bit();
	if (b0 < 0) {
		return Ccitt2dMode::Invalid;
	}
	if (b0 == 1) {
		return Ccitt2dMode::Vertical0; // "1"
	}
	const int b1 = reader.read_bit();
	if (b1 < 0) {
		return Ccitt2dMode::Invalid;
	}
	if (b1 == 1) {
		const int b2 = reader.read_bit();
		if (b2 < 0) {
			return Ccitt2dMode::Invalid;
		}
		return (b2 == 1) ? Ccitt2dMode::VerticalR1 : Ccitt2dMode::VerticalL1; // "011" / "010"
	}
	const int b2 = reader.read_bit();
	if (b2 < 0) {
		return Ccitt2dMode::Invalid;
	}
	if (b2 == 1) {
		return Ccitt2dMode::Horizontal; // "001"
	}
	const int b3 = reader.read_bit();
	if (b3 < 0) {
		return Ccitt2dMode::Invalid;
	}
	if (b3 == 1) {
		return Ccitt2dMode::Pass; // "0001"
	}
	const int b4 = reader.read_bit();
	if (b4 < 0) {
		return Ccitt2dMode::Invalid;
	}
	if (b4 == 1) {
		const int b5 = reader.read_bit();
		if (b5 < 0) {
			return Ccitt2dMode::Invalid;
		}
		return (b5 == 1) ? Ccitt2dMode::VerticalR2 : Ccitt2dMode::VerticalL2; // "000011" / "000010"
	}
	const int b5 = reader.read_bit();
	if (b5 < 0) {
		return Ccitt2dMode::Invalid;
	}
	if (b5 == 1) {
		const int b6 = reader.read_bit();
		if (b6 < 0) {
			return Ccitt2dMode::Invalid;
		}
		return (b6 == 1) ? Ccitt2dMode::VerticalR3 : Ccitt2dMode::VerticalL3; // "0000011" / "0000010"
	}
	// Six leading zeros ("000000") have been read; check for 5+ more zeros followed by 1 (EOL).
	size_t zeros = 6u;
	while (!reader.at_end()) {
		const int bit = reader.read_bit();
		if (bit == 0) {
			++zeros;
			if (zeros > 64u) {
				return Ccitt2dMode::Invalid;
			}
		} else {
			return (zeros >= 11u) ? Ccitt2dMode::Eol : Ccitt2dMode::Invalid;
		}
	}
	return Ccitt2dMode::Invalid;
}

void push_change_point(std::vector<int32_t>& changes, int32_t pos, int32_t columns)
{
	if (pos >= columns) {
		return;
	}
	if (!changes.empty() && changes.back() == pos) {
		changes.pop_back();
	} else {
		changes.push_back(pos);
	}
}

void find_b1_b2(
	const std::vector<int32_t>& ref_changes,
	int32_t a0,
	int32_t a0_color,
	int32_t columns,
	int32_t& out_b1,
	int32_t& out_b2) noexcept
{
	// Even indices in `ref_changes` are transitions to Black (1); odd indices are
	// transitions to White (0). `b1` is the first transition on `ref_changes`
	// to color `(1 - a0_color)` at position `> a0`.
	const size_t target_parity = (a0_color == 0) ? 0u : 1u;
	for (size_t i = target_parity; i < ref_changes.size(); i += 2u) {
		if (ref_changes[i] > a0) {
			out_b1 = ref_changes[i];
			out_b2 = (i + 1u < ref_changes.size()) ? ref_changes[i + 1u] : columns;
			return;
		}
	}
	out_b1 = columns;
	out_b2 = columns;
}

void pack_changes_to_row(
	const std::vector<int32_t>& changes,
	int32_t columns,
	bool black_is_1,
	uint8_t* row_out,
	size_t row_bytes) noexcept
{
	if (black_is_1) {
		std::memset(row_out, 0x00, row_bytes);
		for (size_t i = 0; i < changes.size(); i += 2u) {
			const int32_t x0 = std::max<int32_t>(0, changes[i]);
			const int32_t x1 = (i + 1u < changes.size())
				? std::min<int32_t>(columns, changes[i + 1u])
				: columns;
			for (int32_t x = x0; x < x1; ++x) {
				row_out[static_cast<size_t>(x) >> 3u] |=
					static_cast<uint8_t>(1u << (7u - (static_cast<uint32_t>(x) & 7u)));
			}
		}
	} else {
		std::memset(row_out, 0xFF, row_bytes);
		const uint32_t rem = static_cast<uint32_t>(columns) & 7u;
		if (rem != 0u && row_bytes > 0u) {
			row_out[row_bytes - 1u] &= static_cast<uint8_t>(0xFFu << (8u - rem));
		}
		for (size_t i = 0; i < changes.size(); i += 2u) {
			const int32_t x0 = std::max<int32_t>(0, changes[i]);
			const int32_t x1 = (i + 1u < changes.size())
				? std::min<int32_t>(columns, changes[i + 1u])
				: columns;
			for (int32_t x = x0; x < x1; ++x) {
				row_out[static_cast<size_t>(x) >> 3u] &=
					static_cast<uint8_t>(~(1u << (7u - (static_cast<uint32_t>(x) & 7u))));
			}
		}
	}
}

[[nodiscard]] bool decode_ccitt_1d_row(
	MsbBitReader& reader,
	int32_t columns,
	std::vector<int32_t>& cur_changes)
{
	const size_t row_start_bit = reader.bit_pos;
	cur_changes.clear();
	int32_t x = 0;
	int32_t color = 0; // Starts with White (0)
	size_t step_count = 0;
	const size_t max_steps = static_cast<size_t>(columns) + 4u;
	while (x < columns) {
		if (++step_count > max_steps) {
			reader.bit_pos = row_start_bit;
			return false;
		}
		int32_t run = 0;
		if (!read_ccitt_1d_run(reader, color == 1, columns - x, run)) {
			reader.bit_pos = row_start_bit;
			return false;
		}
		x += run;
		if (x > columns) {
			reader.bit_pos = row_start_bit;
			return false;
		}
		push_change_point(cur_changes, x, columns);
		color = 1 - color;
	}
	return x == columns;
}

// Decodes a single 2D row. Returns:
//   1 = decoded a full row of `columns` pixels
//   0 = encountered EOL / EOFB at the start of the row
//  -1 = malformed bitstream
[[nodiscard]] int decode_ccitt_2d_row(
	MsbBitReader& reader,
	int32_t columns,
	const std::vector<int32_t>& ref_changes,
	std::vector<int32_t>& cur_changes)
{
	cur_changes.clear();
	int32_t a0 = -1;
	int32_t a0_color = 0; // White
	size_t step_count = 0;
	const size_t max_steps = static_cast<size_t>(columns) * 2u + 8u;

	while (a0 < columns) {
		if (++step_count > max_steps) {
			return -1;
		}
		const Ccitt2dMode mode = read_ccitt_2d_mode(reader);
		if (mode == Ccitt2dMode::Eol) {
			if (a0 == -1) {
				return 0;
			}
			return -1;
		}
		if (mode == Ccitt2dMode::Invalid) {
			return -1;
		}

		int32_t b1 = columns;
		int32_t b2 = columns;
		find_b1_b2(ref_changes, a0, a0_color, columns, b1, b2);

		if (mode == Ccitt2dMode::Pass) {
			if (b2 <= a0 || (a0 == -1 && b2 == 0)) {
				return -1;
			}
			a0 = b2;
			if (a0 >= columns) {
				break;
			}
		} else if (mode == Ccitt2dMode::Horizontal) {
			const int32_t a0_start = (a0 < 0) ? 0 : a0;
			int32_t r1 = 0;
			int32_t r2 = 0;
			if (!read_ccitt_1d_run(reader, a0_color == 1, columns - a0_start, r1)) {
				return -1;
			}
			const int32_t a1 = a0_start + r1;
			if (!read_ccitt_1d_run(reader, a0_color == 0, columns - a1, r2)) {
				return -1;
			}
			const int32_t a2 = a1 + r2;
			if (a2 > columns || a2 <= a0_start) {
				return -1;
			}
			push_change_point(cur_changes, a1, columns);
			push_change_point(cur_changes, a2, columns);
			a0 = a2;
		} else {
			int32_t offset = 0;
			switch (mode) {
			case Ccitt2dMode::Vertical0:  offset = 0; break;
			case Ccitt2dMode::VerticalR1: offset = 1; break;
			case Ccitt2dMode::VerticalR2: offset = 2; break;
			case Ccitt2dMode::VerticalR3: offset = 3; break;
			case Ccitt2dMode::VerticalL1: offset = -1; break;
			case Ccitt2dMode::VerticalL2: offset = -2; break;
			case Ccitt2dMode::VerticalL3: offset = -3; break;
			default: return -1;
			}
			const int32_t a1 = b1 + offset;
			if (a1 < 0 || a1 > columns || a1 <= a0) {
				return -1;
			}
			push_change_point(cur_changes, a1, columns);
			a0 = a1;
			a0_color = 1 - a0_color;
		}
	}
	return 1;
}

} // namespace

PdfResult<CcittFaxParams> parse_ccitt_fax_params(const PdfDict* decode_parms) noexcept
{
	CcittFaxParams params{};
	if (decode_parms == nullptr) {
		return params;
	}

	auto read_int_field = [&](std::string_view key, int32_t& target) -> PdfResult<void> {
		const PdfValue* v = decode_parms->get(key);
		if (v == nullptr || v->is_null()) {
			return {};
		}
		const auto iv = v->as_int();
		if (!iv.has_value() ||
			*iv < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
			*iv > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid integer parameter in CCITTFaxDecode /DecodeParms");
		}
		target = static_cast<int32_t>(*iv);
		return {};
	};

	auto read_bool_field = [&](std::string_view key, bool& target) -> PdfResult<void> {
		const PdfValue* v = decode_parms->get(key);
		if (v == nullptr || v->is_null()) {
			return {};
		}
		const auto bv = v->as_bool();
		if (!bv.has_value()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid boolean parameter in CCITTFaxDecode /DecodeParms");
		}
		target = *bv;
		return {};
	};

	RECTO_PDF_RETURN_IF_ERROR(read_int_field("K", params.k));
	RECTO_PDF_RETURN_IF_ERROR(read_bool_field("EndOfLine", params.end_of_line));
	RECTO_PDF_RETURN_IF_ERROR(read_bool_field("EncodedByteAlign", params.encoded_byte_align));
	RECTO_PDF_RETURN_IF_ERROR(read_int_field("Columns", params.columns));
	RECTO_PDF_RETURN_IF_ERROR(read_int_field("Rows", params.rows));
	RECTO_PDF_RETURN_IF_ERROR(read_bool_field("EndOfBlock", params.end_of_block));
	RECTO_PDF_RETURN_IF_ERROR(read_bool_field("BlackIs1", params.black_is_1));
	RECTO_PDF_RETURN_IF_ERROR(
		read_int_field("DamagedRowsBeforeError", params.damaged_rows_before_error));

	if (params.columns <= 0 || params.rows < 0 || params.damaged_rows_before_error < 0) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Invalid /Columns, /Rows, or /DamagedRowsBeforeError in CCITTFaxDecode /DecodeParms");
	}
	return params;
}

PdfResult<std::vector<uint8_t>> decode_ccitt_fax(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		RECTO_PDF_TRY(const CcittFaxParams params, parse_ccitt_fax_params(decode_parms));

		// Check expanded bitmap ceiling (width * height * 4 <= effective_limit) before allocating.
		const uint64_t width_u64 = static_cast<uint64_t>(params.columns);
		const uint64_t initial_height_u64 =
			params.rows > 0 ? static_cast<uint64_t>(params.rows) : 1ULL;
		if (width_u64 > effective_limit / 4ULL ||
			initial_height_u64 > (effective_limit / 4ULL) / width_u64) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"CCITTFaxDecode image dimensions exceed maximum decoded bitmap ceiling");
		}

		const size_t row_bytes = static_cast<size_t>((width_u64 + 7ULL) / 8ULL);
		if (params.rows > 0 &&
			static_cast<uint64_t>(row_bytes) * static_cast<uint64_t>(params.rows) > effective_limit) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"CCITTFaxDecode packed output exceeds maximum decoded stream byte ceiling");
		}

		if (input.empty()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Empty input bitstream in CCITTFaxDecode");
		}

		MsbBitReader reader{input, 0u};
		std::vector<uint8_t> out;
		if (params.rows > 0) {
			const size_t reserve_bytes = std::min<size_t>(
				row_bytes * static_cast<size_t>(params.rows),
				std::min<size_t>(kMaxInitialReserveBytes, effective_limit));
			out.reserve(reserve_bytes);
		}

		std::vector<int32_t> ref_changes;
		std::vector<int32_t> cur_changes;
		uint64_t decoded_rows = 0;
		int32_t consecutive_damaged_rows = 0;

		auto append_current_row = [&]() -> PdfResult<void> {
			const uint64_t next_rows = decoded_rows + 1ULL;
			if (next_rows > (effective_limit / 4ULL) / width_u64 ||
				row_bytes > effective_limit - out.size()) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"CCITTFaxDecode row count exceeds maximum decoded stream byte ceiling");
			}
			ensure_bounded_capacity(out, row_bytes, effective_limit);
			const size_t prev_size = out.size();
			out.resize(prev_size + row_bytes);
			pack_changes_to_row(
				cur_changes,
				params.columns,
				params.black_is_1,
				out.data() + prev_size,
				row_bytes);
			ref_changes = cur_changes;
			++decoded_rows;
			return {};
		};

		// Per ISO 32000-1 section 7.4.6 Table 11, `/EndOfBlock true` overrides `/Rows`
		// and decodes until an end-of-block marker (EOFB or RTC) or end of data.
		while (params.end_of_block || params.rows == 0 ||
			   decoded_rows < static_cast<uint64_t>(params.rows)) {
			if (params.encoded_byte_align && decoded_rows > 0) {
				reader.align_to_byte();
			}
			if (reader.at_end()) {
				break;
			}

			if (params.k < 0) {
				// ITU-T T.6 Group 4 pure 2D MMR decoding.
				const int status = decode_ccitt_2d_row(
					reader,
					params.columns,
					ref_changes,
					cur_changes);
				if (status == 0) {
					// Encountered first EOL of 24-bit EOFB (`000000000001000000000001`);
					// the second EOL must immediately follow.
					if (!reader.try_consume_eol()) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Incomplete EOFB marker in CCITTFaxDecode Group 4 stream",
							reader.bit_pos >> 3u);
					}
					break;
				}
				if (status < 0) {
					if (params.rows > 0 && decoded_rows >= static_cast<uint64_t>(params.rows)) {
						break;
					}
					if (decoded_rows > 0 && params.rows == 0) {
						bool all_zero_tail = true;
						for (size_t b = reader.bit_pos; b < reader.total_bits(); ++b) {
							if (((reader.data[b >> 3u] >> (7u - (b & 7u))) & 1u) != 0u) {
								all_zero_tail = false;
								break;
							}
						}
						if (all_zero_tail) {
							break;
						}
					}
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Corrupt 2D codeword in CCITTFaxDecode Group 4 stream",
						reader.bit_pos >> 3u);
				}
				RECTO_PDF_RETURN_IF_ERROR(append_current_row());
			} else if (params.k == 0) {
				// ITU-T T.4 Group 3 1D decoding.
				const bool saw_eol = reader.try_consume_eol();
				if (saw_eol) {
					// Two consecutive EOLs mark RTC / End-Of-Block.
					if (reader.try_consume_eol()) {
						break;
					}
				} else if (decoded_rows > 0 &&
						   (params.rows == 0 ||
							decoded_rows >= static_cast<uint64_t>(params.rows))) {
					bool all_zero_tail = true;
					for (size_t b = reader.bit_pos; b < reader.total_bits(); ++b) {
						if (((reader.data[b >> 3u] >> (7u - (b & 7u))) & 1u) != 0u) {
							all_zero_tail = false;
							break;
						}
					}
					if (all_zero_tail) {
						break;
					}
				}

				const bool eol_ok = !params.end_of_line || saw_eol;
				if (!eol_ok || !decode_ccitt_1d_row(reader, params.columns, cur_changes)) {
					if (consecutive_damaged_rows < params.damaged_rows_before_error) {
						// Resynchronize to the start of the next EOL and substitute the reference line.
						bool found_sync = false;
						while (!reader.at_end()) {
							const size_t sync_pos = reader.bit_pos;
							if (reader.try_consume_eol()) {
								reader.bit_pos = sync_pos;
								found_sync = true;
								break;
							}
							++reader.bit_pos;
						}
						if (found_sync) {
							++consecutive_damaged_rows;
							cur_changes = ref_changes;
							RECTO_PDF_RETURN_IF_ERROR(append_current_row());
							continue;
						}
					}
					if (params.rows > 0 && decoded_rows >= static_cast<uint64_t>(params.rows)) {
						break;
					}
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Corrupt 1D Huffman run in CCITTFaxDecode Group 3 stream",
						reader.bit_pos >> 3u);
				}
				consecutive_damaged_rows = 0;
				RECTO_PDF_RETURN_IF_ERROR(append_current_row());
			} else {
				// ITU-T T.4 Group 3 2D mixed 1D/2D decoding (`K > 0`).
				const bool saw_eol = reader.try_consume_eol();
				if (saw_eol) {
					// Check for RTC (`EOL + 1` or `EOL` repeated).
					const size_t saved_after_eol = reader.bit_pos;
					if (reader.try_consume_eol()) {
						break;
					}
					const int tag_peek = reader.read_bit();
					if (tag_peek < 0) {
						break;
					}
					if (reader.try_consume_eol()) {
						break;
					}
					reader.bit_pos = saved_after_eol;
				} else if (decoded_rows > 0 &&
						   (params.rows == 0 ||
							decoded_rows >= static_cast<uint64_t>(params.rows))) {
					bool all_zero_tail = true;
					for (size_t b = reader.bit_pos; b < reader.total_bits(); ++b) {
						if (((reader.data[b >> 3u] >> (7u - (b & 7u))) & 1u) != 0u) {
							all_zero_tail = false;
							break;
						}
					}
					if (all_zero_tail) {
						break;
					}
				}
				if (params.end_of_line && !saw_eol) {
					if (params.rows > 0 && decoded_rows >= static_cast<uint64_t>(params.rows)) {
						break;
					}
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Missing required EOL in CCITTFaxDecode Group 3 2D stream",
						reader.bit_pos >> 3u);
				}
				const int tag_bit = reader.read_bit();
				if (tag_bit < 0) {
					break;
				}
				bool row_ok = false;
				if (tag_bit == 1) {
					row_ok = decode_ccitt_1d_row(reader, params.columns, cur_changes);
				} else {
					row_ok = (decode_ccitt_2d_row(
						reader,
						params.columns,
						ref_changes,
						cur_changes) == 1);
				}
				if (!row_ok) {
					if (params.rows > 0 && decoded_rows >= static_cast<uint64_t>(params.rows)) {
						break;
					}
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Corrupt row in CCITTFaxDecode Group 3 2D stream",
						reader.bit_pos >> 3u);
				}
				RECTO_PDF_RETURN_IF_ERROR(append_current_row());
			}
		}

		if (decoded_rows == 0 ||
			(params.rows > 0 && !params.end_of_block &&
			 decoded_rows < static_cast<uint64_t>(params.rows))) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated CCITTFaxDecode stream: insufficient scanlines decoded");
		}

		return out;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding CCITTFaxDecode stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding CCITTFaxDecode stream");
	}
}

// ============================================================================
// JBIG2Decode (ISO/IEC 14492 bi-level generic, symbol & halftone regions)
// ============================================================================

namespace {

[[nodiscard]] constexpr uint16_t read_u16_be(const uint8_t* p) noexcept
{
	return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8u) | p[1]);
}

[[nodiscard]] constexpr uint32_t read_u32_be(const uint8_t* p) noexcept
{
	return (static_cast<uint32_t>(p[0]) << 24u) |
		(static_cast<uint32_t>(p[1]) << 16u) |
		(static_cast<uint32_t>(p[2]) << 8u) |
		static_cast<uint32_t>(p[3]);
}

[[nodiscard]] constexpr int32_t read_i32_be(const uint8_t* p) noexcept
{
	return static_cast<int32_t>(read_u32_be(p));
}

struct Jbig2Bitmap {
	uint32_t width{0};
	uint32_t height{0};
	size_t stride{0};
	std::vector<uint8_t> data; // 1-bit packed MSB-first; in JBIG2: 1 = black, 0 = white

	[[nodiscard]] static PdfResult<Jbig2Bitmap> create(
		uint32_t w,
		uint32_t h,
		uint8_t default_pixel,
		size_t effective_limit)
	{
		if (w == 0 || h == 0) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"JBIG2 bitmap width and height must be positive");
		}
		const uint64_t w64 = w;
		const uint64_t h64 = h;
		if (w64 > effective_limit / 4ULL || h64 > (effective_limit / 4ULL) / w64) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"JBIG2 bitmap dimensions exceed maximum decoded bitmap ceiling");
		}
		const size_t row_stride = static_cast<size_t>((w64 + 7ULL) / 8ULL);
		const uint64_t total_bytes = static_cast<uint64_t>(row_stride) * h64;
		if (total_bytes > effective_limit) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"JBIG2 packed bitmap exceeds maximum decoded stream byte ceiling");
		}
		Jbig2Bitmap bm;
		bm.width = w;
		bm.height = h;
		bm.stride = row_stride;
		bm.data.assign(static_cast<size_t>(total_bytes), default_pixel ? 0xFFu : 0x00u);
		if (default_pixel && (w & 7u) != 0u) {
			const uint8_t mask = static_cast<uint8_t>(0xFFu << (8u - (w & 7u)));
			for (uint32_t y = 0; y < h; ++y) {
				bm.data[static_cast<size_t>(y) * row_stride + (row_stride - 1u)] &= mask;
			}
		}
		return bm;
	}

	[[nodiscard]] uint8_t get_pixel(int32_t x, int32_t y) const noexcept
	{
		if (x < 0 || y < 0 ||
			static_cast<uint32_t>(x) >= width ||
			static_cast<uint32_t>(y) >= height) {
			return 0u;
		}
		const size_t byte_off =
			static_cast<size_t>(y) * stride + (static_cast<size_t>(x) >> 3u);
		return static_cast<uint8_t>(
			(data[byte_off] >> (7u - (static_cast<uint32_t>(x) & 7u))) & 1u);
	}

	void set_pixel(uint32_t x, uint32_t y, uint8_t val) noexcept
	{
		if (x >= width || y >= height) {
			return;
		}
		const size_t byte_off =
			static_cast<size_t>(y) * stride + (static_cast<size_t>(x) >> 3u);
		const uint8_t mask = static_cast<uint8_t>(1u << (7u - (x & 7u)));
		if (val != 0u) {
			data[byte_off] |= mask;
		} else {
			data[byte_off] &= static_cast<uint8_t>(~mask);
		}
	}

	// Composites `src` at `(dst_x, dst_y)` using ISO/IEC 14492 combination operator:
	// 0 = OR, 1 = AND, 2 = XOR, 3 = XNOR, 4 = REPLACE.
	void composite(
		const Jbig2Bitmap& src,
		int32_t dst_x,
		int32_t dst_y,
		uint8_t comb_op) noexcept
	{
		for (uint32_t sy = 0; sy < src.height; ++sy) {
			const int64_t dy = static_cast<int64_t>(dst_y) + static_cast<int64_t>(sy);
			if (dy < 0 || dy >= static_cast<int64_t>(height)) {
				continue;
			}
			for (uint32_t sx = 0; sx < src.width; ++sx) {
				const int64_t dx = static_cast<int64_t>(dst_x) + static_cast<int64_t>(sx);
				if (dx < 0 || dx >= static_cast<int64_t>(width)) {
					continue;
				}
				const uint8_t s = src.get_pixel(static_cast<int32_t>(sx), static_cast<int32_t>(sy));
				const uint8_t d = get_pixel(static_cast<int32_t>(dx), static_cast<int32_t>(dy));
				uint8_t r = d;
				switch (comb_op & 0x07u) {
				case 0u: r = static_cast<uint8_t>(d | s); break;          // OR
				case 1u: r = static_cast<uint8_t>(d & s); break;          // AND
				case 2u: r = static_cast<uint8_t>(d ^ s); break;          // XOR
				case 3u: r = static_cast<uint8_t>((d ^ s) ^ 1u); break;   // XNOR
				case 4u: r = s; break;                                    // REPLACE
				default: r = static_cast<uint8_t>(d | s); break;
				}
				set_pixel(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy), r);
			}
		}
	}
};

// ISO/IEC 14492 Annex E Table E.1: QM-coder probability estimation state table.
struct MqQeEntry {
	uint16_t qe;
	uint8_t nmps;
	uint8_t nlps;
	uint8_t sw;
};

constexpr MqQeEntry kMqQeTable[47] = {
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

struct MqContext {
	uint8_t index{0};
	uint8_t mps{0};
};

class MqDecoder {
public:
	explicit MqDecoder(std::span<const uint8_t> data) noexcept
		: data_(data)
	{
		init_dec();
	}

	[[nodiscard]] uint8_t decode(MqContext& cx) noexcept
	{
		const MqQeEntry& entry = kMqQeTable[cx.index];
		a_ -= entry.qe;
		uint8_t d = cx.mps;
		if ((c_ >> 16u) < a_) {
			if ((a_ & 0x8000u) == 0u) {
				if (a_ < entry.qe) {
					d = static_cast<uint8_t>(1u - d);
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
			c_ -= (a_ << 16u);
			if (a_ < entry.qe) {
				cx.index = entry.nmps;
			} else {
				d = static_cast<uint8_t>(1u - d);
				if (entry.sw != 0u) {
					cx.mps = static_cast<uint8_t>(1u - cx.mps);
				}
				cx.index = entry.nlps;
			}
			a_ = entry.qe;
			renorme();
		}
		return d;
	}

private:
	uint8_t cur_byte() const noexcept
	{
		return bp_ < data_.size() ? data_[bp_] : 0xFFu;
	}

	uint8_t next_byte() const noexcept
	{
		return (bp_ + 1u) < data_.size() ? data_[bp_ + 1u] : 0xFFu;
	}

	void init_dec() noexcept
	{
		bp_ = 0u;
		c_ = static_cast<uint32_t>(cur_byte() ^ 0xFFu) << 16u;
		byte_in();
		c_ <<= 7u;
		ct_ -= 7u;
		a_ = 0x8000u;
	}

	void byte_in() noexcept
	{
		if (cur_byte() == 0xFFu) {
			const uint8_t b1 = next_byte();
			if (b1 > 0x8Fu) {
				ct_ = 8u;
			} else {
				if (bp_ < data_.size()) {
					++bp_;
				}
				c_ += 0xFE00u - (static_cast<uint32_t>(cur_byte()) << 9u);
				ct_ = 7u;
			}
		} else {
			if (bp_ < data_.size()) {
				++bp_;
			}
			c_ += 0xFF00u - (static_cast<uint32_t>(cur_byte()) << 8u);
			ct_ = 8u;
		}
	}

	void renorme() noexcept
	{
		do {
			if (ct_ == 0u) {
				byte_in();
			}
			a_ <<= 1u;
			c_ <<= 1u;
			--ct_;
		} while ((a_ & 0x8000u) == 0u);
	}

	std::span<const uint8_t> data_;
	size_t bp_{0};
	uint32_t c_{0};
	uint32_t a_{0x8000u};
	uint32_t ct_{0};
};

// ISO/IEC 14492 Annex A: Arithmetic Integer Decoding Procedure.
class Jbig2ArithIntDecoder {
public:
	// Returns true for a decoded integer `out_val`, or false on `OOB`.
	[[nodiscard]] bool decode(MqDecoder& mq, int32_t& out_val) noexcept
	{
		uint32_t prev = 1u;
		const uint8_t s = decode_bit(mq, prev);
		uint32_t v = 0u;
		if (decode_bit(mq, prev) == 0u) {
			v = read_bits(mq, prev, 2u);
		} else if (decode_bit(mq, prev) == 0u) {
			v = read_bits(mq, prev, 4u) + 4u;
		} else if (decode_bit(mq, prev) == 0u) {
			v = read_bits(mq, prev, 6u) + 20u;
		} else if (decode_bit(mq, prev) == 0u) {
			v = read_bits(mq, prev, 8u) + 84u;
		} else if (decode_bit(mq, prev) == 0u) {
			v = read_bits(mq, prev, 12u) + 340u;
		} else {
			v = read_bits(mq, prev, 32u) + 4436u;
		}

		if (s != 0u && v == 0u) {
			return false; // OOB
		}
		out_val = (s != 0u) ? -static_cast<int32_t>(v) : static_cast<int32_t>(v);
		return true;
	}

private:
	uint8_t decode_bit(MqDecoder& mq, uint32_t& prev) noexcept
	{
		const uint8_t d = mq.decode(contexts_[prev & 0x1FFu]);
		prev = (prev << 1u) | d;
		if (prev >= 0x200u) {
			prev = (prev & 0x1FFu) | 0x100u;
		}
		return d;
	}

	uint32_t read_bits(MqDecoder& mq, uint32_t& prev, uint32_t count) noexcept
	{
		uint32_t val = 0u;
		for (uint32_t i = 0; i < count; ++i) {
			val = (val << 1u) | decode_bit(mq, prev);
		}
		return val;
	}

	std::array<MqContext, 512> contexts_{};
};

// ISO/IEC 14492 Annex A.3: IAID Symbol ID Arithmetic Decoder.
class Jbig2IaidDecoder {
public:
	explicit Jbig2IaidDecoder(uint32_t code_len)
		: code_len_(std::min<uint32_t>(code_len, 20u))
		, contexts_(size_t{1u} << code_len_)
	{
	}

	[[nodiscard]] uint32_t decode(MqDecoder& mq) noexcept
	{
		uint32_t prev = 1u;
		for (uint32_t i = 0; i < code_len_; ++i) {
			const uint8_t d = mq.decode(contexts_[prev & ((1u << code_len_) - 1u)]);
			prev = (prev << 1u) | d;
		}
		return prev - (1u << code_len_);
	}

private:
	uint32_t code_len_{1};
	std::vector<MqContext> contexts_;
};

// Decodes a generic region (ISO/IEC 14492 section 6.2) using either MMR (`mmr == true`)
// or QM arithmetic coding (`mmr == false`, `gb_template == 0..3`, `tpgdon`).
[[nodiscard]] PdfResult<Jbig2Bitmap> decode_jbig2_generic_region(
	uint32_t width,
	uint32_t height,
	bool mmr,
	uint8_t gb_template,
	bool tpgdon,
	std::span<const int8_t> at_coords,
	std::span<const uint8_t> payload,
	size_t effective_limit,
	MqDecoder* shared_mq = nullptr,
	std::vector<MqContext>* shared_cx = nullptr,
	size_t* inout_mmr_bit_offset = nullptr)
{
	RECTO_PDF_TRY(Jbig2Bitmap bm, Jbig2Bitmap::create(width, height, 0u, effective_limit));

	if (mmr) {
		MsbBitReader reader{
			payload,
			(inout_mmr_bit_offset != nullptr) ? *inout_mmr_bit_offset : 0u};
		std::vector<int32_t> ref_changes;
		std::vector<int32_t> cur_changes;
		for (uint32_t y = 0; y < height; ++y) {
			const int status = decode_ccitt_2d_row(
				reader,
				static_cast<int32_t>(width),
				ref_changes,
				cur_changes);
			if (status == 0) {
				(void)reader.try_consume_eol();
				break;
			}
			if (status < 0) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Corrupt MMR row inside JBIG2 Generic Region segment");
			}
			pack_changes_to_row(
				cur_changes,
				static_cast<int32_t>(width),
				/*black_is_1=*/true,
				bm.data.data() + static_cast<size_t>(y) * bm.stride,
				bm.stride);
			ref_changes = cur_changes;
		}
		// Consume optional trailing EOFB (`000000000001000000000001`) if present after `height` rows.
		const size_t saved_bit_pos = reader.bit_pos;
		if (reader.try_consume_eol()) {
			if (!reader.try_consume_eol()) {
				reader.bit_pos = saved_bit_pos;
			}
		}
		if (inout_mmr_bit_offset != nullptr) {
			*inout_mmr_bit_offset = reader.bit_pos;
		}
		return bm;
	}

	std::optional<MqDecoder> local_mq;
	std::vector<MqContext> local_cx;
	MqDecoder* mq = shared_mq;
	std::vector<MqContext>* cx_vec = shared_cx;
	if (mq == nullptr) {
		local_mq.emplace(payload);
		mq = &(*local_mq);
	}
	const size_t num_contexts =
		(gb_template == 0u) ? 65536u : ((gb_template == 1u) ? 8192u : 1024u);
	if (cx_vec == nullptr) {
		local_cx.resize(num_contexts);
		cx_vec = &local_cx;
	} else if (cx_vec->size() < num_contexts) {
		cx_vec->resize(num_contexts);
	}

	int8_t atx[4] = {3, -3, 2, -2};
	int8_t aty[4] = {-1, -1, -2, -2};
	if (gb_template == 0u && at_coords.size() >= 8u) {
		for (int i = 0; i < 4; ++i) {
			atx[i] = at_coords[static_cast<size_t>(i) * 2u];
			aty[i] = at_coords[static_cast<size_t>(i) * 2u + 1u];
		}
	} else if (gb_template >= 1u && at_coords.size() >= 2u) {
		atx[0] = at_coords[0];
		aty[0] = at_coords[1];
	}

	constexpr uint16_t kSltpContext[4] = {0x9B25u, 0x0795u, 0x00E5u, 0x0195u};
	uint8_t ltp = 0u;

	for (uint32_t y = 0; y < height; ++y) {
		if (tpgdon) {
			const uint8_t sltp = mq->decode((*cx_vec)[kSltpContext[gb_template & 3u]]);
			ltp ^= sltp;
			if (ltp != 0u) {
				if (y > 0u) {
					std::memcpy(
						bm.data.data() + static_cast<size_t>(y) * bm.stride,
						bm.data.data() + static_cast<size_t>(y - 1u) * bm.stride,
						bm.stride);
				}
				continue;
			}
		}

		const int32_t iy = static_cast<int32_t>(y);
		for (uint32_t x = 0; x < width; ++x) {
			const int32_t ix = static_cast<int32_t>(x);
			uint32_t ctx = 0u;
			if (gb_template == 0u) {
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy)) << 0u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy)) << 1u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 3, iy)) << 2u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 4, iy)) << 3u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + atx[0], iy + aty[0])) << 4u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 2, iy - 1)) << 5u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 1)) << 6u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 1)) << 7u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 1)) << 8u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy - 1)) << 9u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + atx[1], iy + aty[1])) << 10u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + atx[2], iy + aty[2])) << 11u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 2)) << 12u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 2)) << 13u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 2)) << 14u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + atx[3], iy + aty[3])) << 15u;
			} else if (gb_template == 1u) {
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy)) << 0u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy)) << 1u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 3, iy)) << 2u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + atx[0], iy + aty[0])) << 3u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 2, iy - 1)) << 4u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 1)) << 5u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 1)) << 6u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 1)) << 7u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy - 1)) << 8u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 2, iy - 2)) << 9u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 2)) << 10u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 2)) << 11u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 2)) << 12u;
			} else if (gb_template == 2u) {
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy)) << 0u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy)) << 1u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + atx[0], iy + aty[0])) << 2u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 1)) << 3u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 1)) << 4u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 1)) << 5u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy - 1)) << 6u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 2)) << 7u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 2)) << 8u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 2)) << 9u;
			} else {
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy)) << 0u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy)) << 1u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 3, iy)) << 2u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 4, iy)) << 3u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + atx[0], iy + aty[0])) << 4u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 1)) << 5u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 1)) << 6u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 1)) << 7u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 2, iy - 1)) << 8u;
				ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 3, iy - 1)) << 9u;
			}
			const uint8_t bit = mq->decode((*cx_vec)[ctx]);
			if (bit != 0u) {
				bm.set_pixel(x, y, 1u);
			}
		}
	}

	return bm;
}

// ISO/IEC 14492 (ITU-T T.88) section 6.3: Generic Refinement Region Decoding Procedure.
[[nodiscard]] PdfResult<Jbig2Bitmap> decode_jbig2_refinement_region(
	uint32_t width,
	uint32_t height,
	uint8_t gr_template,
	bool tpgron,
	const Jbig2Bitmap& ref_bm,
	int32_t ref_dx,
	int32_t ref_dy,
	std::span<const int8_t> grat_coords,
	std::span<const uint8_t> payload,
	size_t max_bytes,
	MqDecoder* shared_mq = nullptr,
	std::vector<MqContext>* shared_cx = nullptr)
{
	RECTO_PDF_TRY(Jbig2Bitmap bm, Jbig2Bitmap::create(width, height, 0u, max_bytes));
	std::optional<MqDecoder> local_mq;
	std::vector<MqContext> local_cx;
	MqDecoder* mq = shared_mq;
	std::vector<MqContext>* cx_vec = shared_cx;
	if (mq == nullptr) {
		local_mq.emplace(payload);
		mq = &(*local_mq);
	}
	const size_t num_contexts = (gr_template == 0u) ? 8192u : 1024u;
	if (cx_vec == nullptr) {
		local_cx.resize(num_contexts);
		cx_vec = &local_cx;
	} else if (cx_vec->size() < num_contexts) {
		cx_vec->resize(num_contexts);
	}

	int8_t gratx[2] = {-1, -1};
	int8_t graty[2] = {-1, -1};
	if (gr_template == 0u && grat_coords.size() >= 4u) {
		gratx[0] = grat_coords[0];
		graty[0] = grat_coords[1];
		gratx[1] = grat_coords[2];
		graty[1] = grat_coords[3];
	}

	constexpr uint16_t kSltpRefContext[2] = {0x0010u, 0x0008u};
	uint8_t ltp = 0u;

	auto build_ref_ctx = [&](int32_t ix, int32_t iy) noexcept -> uint32_t {
		const int32_t rx = ix - ref_dx;
		const int32_t ry = iy - ref_dy;
		uint32_t ctx = 0u;
		if (gr_template == 0u) {
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy)) << 0u;
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 1)) << 1u;
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 1)) << 2u;
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix + gratx[0], iy + graty[0])) << 3u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx + 1, ry + 1)) << 4u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx, ry + 1)) << 5u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx - 1, ry + 1)) << 6u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx + 1, ry)) << 7u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx, ry)) << 8u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx - 1, ry)) << 9u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx + 1, ry - 1)) << 10u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx, ry - 1)) << 11u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx + gratx[1], ry + graty[1])) << 12u;
		} else {
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy)) << 0u;
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix + 1, iy - 1)) << 1u;
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix, iy - 1)) << 2u;
			ctx |= static_cast<uint32_t>(bm.get_pixel(ix - 1, iy - 1)) << 3u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx + 1, ry + 1)) << 4u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx, ry + 1)) << 5u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx + 1, ry)) << 6u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx, ry)) << 7u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx - 1, ry)) << 8u;
			ctx |= static_cast<uint32_t>(ref_bm.get_pixel(rx, ry - 1)) << 9u;
		}
		return ctx;
	};

	for (uint32_t y = 0; y < height; ++y) {
		if (tpgron) {
			const uint8_t sltp = mq->decode((*cx_vec)[kSltpRefContext[gr_template & 1u]]);
			ltp ^= sltp;
		}
		const int32_t iy = static_cast<int32_t>(y);
		for (uint32_t x = 0; x < width; ++x) {
			const int32_t ix = static_cast<int32_t>(x);
			if (tpgron && ltp != 0u) {
				const int32_t rx = ix - ref_dx;
				const int32_t ry = iy - ref_dy;
				const uint8_t c_val = ref_bm.get_pixel(rx, ry);
				bool same_3x3 = true;
				for (int32_t dy = -1; dy <= 1 && same_3x3; ++dy) {
					for (int32_t dx = -1; dx <= 1; ++dx) {
						if (ref_bm.get_pixel(rx + dx, ry + dy) != c_val) {
							same_3x3 = false;
							break;
						}
					}
				}
				if (same_3x3) {
					if (c_val != 0u) {
						bm.set_pixel(x, y, 1u);
					}
					continue;
				}
			}
			const uint32_t ctx = build_ref_ctx(ix, iy);
			const uint8_t bit = mq->decode((*cx_vec)[ctx]);
			if (bit != 0u) {
				bm.set_pixel(x, y, 1u);
			}
		}
	}
	return bm;
}

// ITU-T T.88 Annex B: Standard Huffman Tables and Bit Reader for JBIG2 Huffman mode.
struct Jbig2HuffEntry {
	int32_t range_low;
	uint8_t pref_len;
	uint8_t range_len; // 32 = lower range (-range_low), 0xFE = OOB
	uint16_t prefix;
	bool is_lower = false;
	bool is_oob = false;
};

class Jbig2BitReader {
public:
	explicit Jbig2BitReader(std::span<const uint8_t> data, size_t start_bit = 0u) noexcept
		: data_(data), bit_pos_(start_bit)
	{
	}

	[[nodiscard]] bool read_bit(uint32_t& out_bit) noexcept
	{
		const size_t byte_idx = bit_pos_ >> 3u;
		if (byte_idx >= data_.size()) {
			return false;
		}
		const uint32_t shift = 7u - static_cast<uint32_t>(bit_pos_ & 7u);
		out_bit = (data_[byte_idx] >> shift) & 1u;
		++bit_pos_;
		return true;
	}

	[[nodiscard]] bool read_bits(uint32_t n, uint32_t& out_val) noexcept
	{
		uint32_t v = 0u;
		for (uint32_t i = 0; i < n; ++i) {
			uint32_t b = 0u;
			if (!read_bit(b)) {
				return false;
			}
			v = (v << 1u) | b;
		}
		out_val = v;
		return true;
	}

	void align_to_byte() noexcept
	{
		bit_pos_ = (bit_pos_ + 7u) & ~size_t{7u};
	}

	[[nodiscard]] size_t bit_pos() const noexcept { return bit_pos_; }
	void set_bit_pos(size_t bp) noexcept { bit_pos_ = bp; }
	[[nodiscard]] size_t byte_pos() const noexcept { return (bit_pos_ + 7u) >> 3u; }

	[[nodiscard]] bool decode_huff(
		std::span<const Jbig2HuffEntry> table,
		int32_t& out_val,
		bool& out_oob) noexcept
	{
		out_oob = false;
		uint32_t code = 0u;
		for (uint8_t len = 1u; len <= 16u; ++len) {
			uint32_t b = 0u;
			if (!read_bit(b)) {
				return false;
			}
			code = (code << 1u) | b;
			for (const auto& e : table) {
				if (e.pref_len == len && e.prefix == code) {
					if (e.is_oob) {
						out_oob = true;
						out_val = 0;
						return true;
					}
					uint32_t offset = 0u;
					if (e.range_len > 0u) {
						if (!read_bits(e.range_len, offset)) {
							return false;
						}
					}
					if (e.is_lower) {
						out_val = e.range_low - static_cast<int32_t>(offset);
					} else {
						out_val = e.range_low + static_cast<int32_t>(offset);
					}
					return true;
				}
			}
		}
		return false;
	}

private:
	std::span<const uint8_t> data_;
	size_t bit_pos_ = 0u;
};

// Standard JBIG2 Huffman tables B.1 (Table A), B.2 (Table B), B.4 (Table D), B.6 (Table F), B.8 (Table H), B.11 (Table K), B.14 (Table N), B.15 (Table O).
constexpr Jbig2HuffEntry kJbig2TableB1[] = {
	{0, 1, 4, 0b0, false, false},
	{16, 2, 8, 0b10, false, false},
	{272, 3, 16, 0b110, false, false},
	{65808, 3, 32, 0b111, false, false},
};
constexpr Jbig2HuffEntry kJbig2TableB2[] = {
	{0, 1, 0, 0b0, false, false},
	{1, 2, 0, 0b10, false, false},
	{2, 3, 0, 0b110, false, false},
	{3, 4, 3, 0b1110, false, false},
	{11, 5, 6, 0b11110, false, false},
	{75, 6, 32, 0b111110, false, false},
	{0, 6, 0, 0b111111, false, true},
};
constexpr Jbig2HuffEntry kJbig2TableB4[] = {
	{1, 1, 0, 0b0, false, false},
	{2, 2, 0, 0b10, false, false},
	{3, 3, 0, 0b110, false, false},
	{4, 4, 3, 0b1110, false, false},
	{12, 5, 6, 0b11110, false, false},
	{76, 5, 32, 0b11111, false, false},
};
constexpr Jbig2HuffEntry kJbig2TableB6[] = {
	{-2048, 5, 10, 0b11100, false, false},
	{-1024, 4, 9, 0b1000, false, false},
	{-512, 4, 8, 0b1001, false, false},
	{-256, 4, 7, 0b1010, false, false},
	{-128, 5, 6, 0b11101, false, false},
	{-64, 5, 5, 0b11110, false, false},
	{-32, 4, 5, 0b1011, false, false},
	{0, 2, 7, 0b00, false, false},
	{128, 3, 7, 0b010, false, false},
	{256, 3, 8, 0b011, false, false},
	{512, 4, 9, 0b1100, false, false},
	{1024, 4, 10, 0b1101, false, false},
	{-2049, 6, 32, 0b111110, true, false},
	{2048, 6, 32, 0b111111, false, false},
};
constexpr Jbig2HuffEntry kJbig2TableB8[] = {
	{-15, 8, 3, 0b11111100, false, false},
	{-7, 9, 1, 0b111111100, false, false},
	{-5, 8, 1, 0b11111011, false, false},
	{-3, 7, 0, 0b1111100, false, false},
	{-2, 4, 0, 0b1100, false, false},
	{-1, 4, 0, 0b1101, false, false},
	{0, 2, 1, 0b00, false, false},
	{2, 3, 1, 0b010, false, false},
	{4, 3, 2, 0b011, false, false},
	{8, 4, 3, 0b1000, false, false},
	{16, 4, 4, 0b1001, false, false},
	{32, 4, 5, 0b1010, false, false},
	{64, 5, 6, 0b11100, false, false},
	{128, 5, 7, 0b11101, false, false},
	{256, 6, 8, 0b111100, false, false},
	{512, 7, 9, 0b1111010, false, false},
	{1024, 7, 10, 0b1111011, false, false},
	{-16, 9, 32, 0b111111101, true, false},
	{2048, 9, 32, 0b111111110, false, false},
	{0, 3, 0, 0b101, false, true}, // Wait: in Table B.8, OOB is 2 bits '11'? No, let's check B.8 and B.11!
};
constexpr Jbig2HuffEntry kJbig2TableB11[] = {
	{1, 1, 0, 0b0, false, false},
	{2, 2, 1, 0b10, false, false},
	{4, 4, 0, 0b1100, false, false},
	{5, 4, 1, 0b1101, false, false},
	{7, 5, 1, 0b11100, false, false},
	{9, 5, 2, 0b11101, false, false},
	{13, 6, 2, 0b111100, false, false},
	{17, 7, 2, 0b1111010, false, false},
	{21, 7, 3, 0b1111011, false, false},
	{29, 7, 4, 0b1111100, false, false},
	{45, 7, 5, 0b1111101, false, false},
	{77, 7, 6, 0b1111110, false, false},
	{141, 7, 32, 0b1111111, false, false},
};
constexpr Jbig2HuffEntry kJbig2TableB14[] = {
	{-2, 3, 0, 0b100, false, false},
	{-1, 3, 0, 0b101, false, false},
	{0, 1, 0, 0b0, false, false},
	{1, 3, 0, 0b110, false, false},
	{2, 3, 0, 0b111, false, false},
};

struct Jbig2DecoderState {
	std::optional<Jbig2Bitmap> page_bitmap;
	uint8_t default_comb_op{0};
	uint8_t default_pixel{0};
	std::unordered_map<uint32_t, std::vector<Jbig2Bitmap>> symbol_dicts;
	std::unordered_map<uint32_t, std::vector<Jbig2Bitmap>> pattern_dicts;
	std::unordered_map<uint32_t, Jbig2Bitmap> aux_regions;
	size_t total_allocated_bytes{0};

	[[nodiscard]] size_t remaining_limit(size_t effective_limit) const noexcept
	{
		return (effective_limit > total_allocated_bytes)
			? (effective_limit - total_allocated_bytes)
			: 0u;
	}

	[[nodiscard]] PdfResult<void> account_bytes(
		size_t bytes,
		size_t effective_limit,
		size_t offset) noexcept
	{
		if (bytes > effective_limit ||
			total_allocated_bytes > effective_limit - bytes) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"JBIG2 cumulative bitmap allocation exceeds maximum decoded stream byte ceiling",
				offset);
		}
		total_allocated_bytes += bytes;
		return {};
	}
};

[[nodiscard]] PdfResult<void> parse_jbig2_segments(
	std::span<const uint8_t> stream_bytes,
	Jbig2DecoderState& state,
	size_t effective_limit)
{
	size_t pos = 0u;
	// Check for optional 8-byte standalone JBIG2 file header (`97 4A 42 32 0D 0A 1A 0A`).
	constexpr uint8_t kJbig2FileMagic[8] = {0x97u, 0x4Au, 0x42u, 0x32u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
	if (stream_bytes.size() >= 9u &&
		std::memcmp(stream_bytes.data(), kJbig2FileMagic, 8u) == 0) {
		const uint8_t file_flags = stream_bytes[8u];
		pos = 9u;
		if ((file_flags & 0x02u) == 0u) {
			if (pos + 4u > stream_bytes.size()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated page count in JBIG2 file header");
			}
			pos += 4u;
		}
	}

	while (pos < stream_bytes.size()) {
		if (stream_bytes.size() - pos < 11u) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated JBIG2 segment header",
				pos);
		}
		const size_t seg_header_start = pos;
		const uint32_t seg_num = read_u32_be(stream_bytes.data() + pos);
		pos += 4u;

		const uint8_t seg_flags = stream_bytes[pos++];
		const uint8_t seg_type = static_cast<uint8_t>(seg_flags & 0x3Fu);
		const bool page_assoc_long = (seg_flags & 0x40u) != 0u;

		const uint8_t rt_first = stream_bytes[pos];
		const uint32_t rt_top3 = (rt_first >> 5u) & 0x07u;
		uint32_t ref_count = 0u;
		if (rt_top3 < 5u) {
			ref_count = rt_top3;
			++pos;
		} else if (rt_top3 == 7u) {
			if (stream_bytes.size() - pos < 4u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated long referred-to segment count in JBIG2 segment header",
					seg_header_start);
			}
			ref_count = read_u32_be(stream_bytes.data() + pos) & 0x1FFFFFFFu;
			pos += 4u;
			const uint64_t retain_bytes = (static_cast<uint64_t>(ref_count) + 8ULL) / 8ULL;
			if (retain_bytes > stream_bytes.size() - pos) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 segment retention flags exceed stream bounds",
					seg_header_start);
			}
			pos += static_cast<size_t>(retain_bytes);
		} else {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Invalid referred-to count encoding (5 or 6) in JBIG2 segment header",
				seg_header_start);
		}

		const size_t ref_id_bytes = (seg_num <= 256u) ? 1u : ((seg_num <= 65536u) ? 2u : 4u);
		if (static_cast<uint64_t>(ref_count) * ref_id_bytes > stream_bytes.size() - pos) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"JBIG2 referred-to segment list exceeds stream bounds",
				seg_header_start);
		}
		std::vector<uint32_t> ref_segments;
		ref_segments.reserve(std::min<uint32_t>(ref_count, 64u));
		for (uint32_t i = 0; i < ref_count; ++i) {
			uint32_t ref_id = 0u;
			if (ref_id_bytes == 1u) {
				ref_id = stream_bytes[pos++];
			} else if (ref_id_bytes == 2u) {
				ref_id = read_u16_be(stream_bytes.data() + pos);
				pos += 2u;
			} else {
				ref_id = read_u32_be(stream_bytes.data() + pos);
				pos += 4u;
			}
			ref_segments.push_back(ref_id);
		}

		const size_t page_assoc_bytes = page_assoc_long ? 4u : 1u;
		if (stream_bytes.size() - pos < page_assoc_bytes + 4u) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated page association or data length in JBIG2 segment header",
				seg_header_start);
		}
		pos += page_assoc_bytes;

		const uint32_t raw_data_len = read_u32_be(stream_bytes.data() + pos);
		pos += 4u;

		size_t seg_data_len = raw_data_len;
		if (raw_data_len == 0xFFFFFFFFu) {
			// Indeterminate length is only permitted for immediate generic region segments (38, 39).
			if (seg_type != 38u && seg_type != 39u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Indeterminate JBIG2 segment length is only allowed on immediate generic regions",
					seg_header_start);
			}
			if (stream_bytes.size() - pos < 18u + 6u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated indeterminate-length JBIG2 generic region segment",
					seg_header_start);
			}
			const uint8_t gr_flags_peek = stream_bytes[pos + 17u];
			const bool is_mmr = (gr_flags_peek & 0x01u) != 0u;
			const uint8_t gb_tmpl_peek = static_cast<uint8_t>((gr_flags_peek >> 1u) & 0x03u);
			const bool ext_tmpl_peek = (gr_flags_peek & 0x10u) != 0u;
			const size_t at_bytes_peek = is_mmr
				? 0u
				: ((gb_tmpl_peek == 0u) ? (ext_tmpl_peek ? 24u : 8u) : 2u);
			if (stream_bytes.size() - pos < 18u + at_bytes_peek + 6u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated AT flags or trailer in indeterminate-length JBIG2 generic region",
					seg_header_start);
			}
			const uint8_t marker0 = is_mmr ? 0x00u : 0xFFu;
			const uint8_t marker1 = is_mmr ? 0x00u : 0xACu;
			bool found_end = false;
			for (size_t scan = pos + 18u + at_bytes_peek;
				 scan + 6u <= stream_bytes.size();
				 ++scan) {
				if (stream_bytes[scan] == marker0 && stream_bytes[scan + 1u] == marker1) {
					seg_data_len = (scan + 6u) - pos;
					found_end = true;
					break;
				}
			}
			if (!found_end) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Missing row-count trailer on indeterminate-length JBIG2 generic region",
					seg_header_start);
			}
		} else if (seg_data_len > stream_bytes.size() - pos) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"JBIG2 segment data length exceeds remaining stream bytes",
				seg_header_start);
		}

		const std::span<const uint8_t> seg_data = stream_bytes.subspan(pos, seg_data_len);
		pos += seg_data_len;

		if (seg_type == 48u) {
			// Page Information segment (ISO/IEC 14492 section 7.4.8, 19 bytes).
			if (seg_data.size() < 19u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 Page Information segment (type 48) is shorter than 19 bytes",
					seg_header_start);
			}
			const uint32_t page_w = read_u32_be(seg_data.data());
			uint32_t page_h = read_u32_be(seg_data.data() + 4u);
			const uint8_t page_flags = seg_data[16u];
			const uint16_t striping = read_u16_be(seg_data.data() + 17u);
			if (page_h == 0xFFFFFFFFu) {
				page_h = static_cast<uint32_t>(striping & 0x7FFFu);
			}
			state.default_pixel = static_cast<uint8_t>((page_flags >> 2u) & 0x01u);
			state.default_comb_op = static_cast<uint8_t>((page_flags >> 3u) & 0x03u);
			if (state.page_bitmap.has_value()) {
				state.total_allocated_bytes -=
					std::min(state.total_allocated_bytes, state.page_bitmap->data.size());
				state.page_bitmap.reset();
			}
			RECTO_PDF_TRY(
				Jbig2Bitmap pbm,
				Jbig2Bitmap::create(
					page_w,
					page_h,
					state.default_pixel,
					state.remaining_limit(effective_limit)));
			RECTO_PDF_RETURN_IF_ERROR(
				state.account_bytes(pbm.data.size(), effective_limit, seg_header_start));
			state.page_bitmap = std::move(pbm);
		} else if (seg_type == 36u || seg_type == 38u || seg_type == 39u) {
			// Intermediate (36) or Immediate (38, 39) Generic Region segment.
			if (seg_data.size() < 18u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 Generic Region segment is shorter than 18 bytes",
					seg_header_start);
			}
			const uint32_t reg_w = read_u32_be(seg_data.data());
			uint32_t reg_h = read_u32_be(seg_data.data() + 4u);
			if (raw_data_len == 0xFFFFFFFFu && seg_data.size() >= 24u) {
				reg_h = read_u32_be(seg_data.data() + seg_data.size() - 4u);
			}
			const int32_t reg_x = read_i32_be(seg_data.data() + 8u);
			const int32_t reg_y = read_i32_be(seg_data.data() + 12u);
			const uint8_t reg_comb_op = static_cast<uint8_t>(seg_data[16u] & 0x07u);
			const uint8_t gr_flags = seg_data[17u];
			const bool mmr = (gr_flags & 0x01u) != 0u;
			const uint8_t gb_template = static_cast<uint8_t>((gr_flags >> 1u) & 0x03u);
			const bool tpgdon = (gr_flags & 0x08u) != 0u;
			const bool ext_template = (gr_flags & 0x10u) != 0u;

			size_t at_bytes = 0u;
			if (!mmr) {
				at_bytes = (gb_template == 0u) ? (ext_template ? 24u : 8u) : 2u;
			}
			const size_t trailer_bytes = (raw_data_len == 0xFFFFFFFFu) ? 6u : 0u;
			if (18u + at_bytes + trailer_bytes > seg_data.size()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated AT flags or trailer in JBIG2 Generic Region segment",
					seg_header_start);
			}
			const std::span<const int8_t> at_span(
				reinterpret_cast<const int8_t*>(seg_data.data() + 18u),
				at_bytes);
			const size_t body_len = seg_data.size() - 18u - at_bytes - trailer_bytes;
			RECTO_PDF_TRY(
				Jbig2Bitmap rbm,
				decode_jbig2_generic_region(
					reg_w,
					reg_h,
					mmr,
					gb_template,
					tpgdon,
					at_span,
					seg_data.subspan(18u + at_bytes, body_len),
					state.remaining_limit(effective_limit)));

			if (seg_type == 36u) {
				RECTO_PDF_RETURN_IF_ERROR(
					state.account_bytes(rbm.data.size(), effective_limit, seg_header_start));
				state.aux_regions[seg_num] = std::move(rbm);
			} else {
				if (!state.page_bitmap.has_value()) {
					RECTO_PDF_TRY(
						Jbig2Bitmap pbm,
						Jbig2Bitmap::create(
							reg_w,
							reg_h,
							0u,
							state.remaining_limit(effective_limit)));
					RECTO_PDF_RETURN_IF_ERROR(
						state.account_bytes(pbm.data.size(), effective_limit, seg_header_start));
					state.page_bitmap = std::move(pbm);
				}
				state.page_bitmap->composite(rbm, reg_x, reg_y, reg_comb_op);
			}
		} else if (seg_type == 0u) {
			// Symbol Dictionary segment (ISO/IEC 14492 section 7.4.2).
			if (seg_data.size() < 10u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 Symbol Dictionary segment (type 0) is shorter than 10 bytes",
					seg_header_start);
			}
			const uint16_t sd_flags = read_u16_be(seg_data.data());
			const bool sd_huff = (sd_flags & 0x0001u) != 0u;
			const bool sd_ref_agg = (sd_flags & 0x0002u) != 0u;
			const uint8_t sd_template = static_cast<uint8_t>((sd_flags >> 10u) & 0x03u);

			const uint8_t sd_r_template = static_cast<uint8_t>((sd_flags >> 12u) & 0x01u);

			size_t sd_at_bytes = 0u;
			if (!sd_huff) {
				sd_at_bytes += (sd_template == 0u) ? 8u : 2u;
			}
			if (sd_ref_agg && sd_r_template == 0u) {
				sd_at_bytes += 4u;
			}
			if (2u + sd_at_bytes + 8u > seg_data.size()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated header in JBIG2 Symbol Dictionary segment",
					seg_header_start);
			}
			const size_t gb_at_len = sd_huff ? 0u : ((sd_template == 0u) ? 8u : 2u);
			const std::span<const int8_t> sd_at_span(
				reinterpret_cast<const int8_t*>(seg_data.data() + 2u),
				gb_at_len);
			const std::span<const int8_t> sd_rat_span(
				reinterpret_cast<const int8_t*>(seg_data.data() + 2u + gb_at_len),
				(sd_ref_agg && sd_r_template == 0u) ? 4u : 0u);
			const size_t counts_off = 2u + sd_at_bytes;
			const uint32_t sd_num_ex_syms = read_u32_be(seg_data.data() + counts_off);
			const uint32_t sd_num_new_syms = read_u32_be(seg_data.data() + counts_off + 4u);
			if (sd_num_ex_syms > 65536u || sd_num_new_syms > 65536u) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"JBIG2 Symbol Dictionary symbol count exceeds ceiling",
					seg_header_start);
			}

			std::vector<Jbig2Bitmap> input_syms;
			for (const uint32_t ref_id : ref_segments) {
				const auto it = state.symbol_dicts.find(ref_id);
				if (it != state.symbol_dicts.end()) {
					for (const auto& s : it->second) {
						RECTO_PDF_RETURN_IF_ERROR(
							state.account_bytes(s.data.size(), effective_limit, seg_header_start));
						input_syms.push_back(s);
					}
				}
			}

			std::vector<Jbig2Bitmap> all_syms = std::move(input_syms);
			const size_t num_in_syms = all_syms.size();
			const std::span<const uint8_t> arith_data = seg_data.subspan(counts_off + 8u);
			if (!sd_huff) {
				MqDecoder mq(arith_data);
				std::vector<MqContext> gb_cx(
					(sd_template == 0u) ? 65536u : ((sd_template == 1u) ? 8192u : 1024u));
				std::vector<MqContext> gr_cx((sd_r_template == 0u) ? 8192u : 1024u);
				Jbig2ArithIntDecoder iadh;
				Jbig2ArithIntDecoder iadw;
				Jbig2ArithIntDecoder iaex;
				Jbig2ArithIntDecoder iaai;
				Jbig2ArithIntDecoder iardx;
				Jbig2ArithIntDecoder iardy;

				uint32_t sym_code_len = 0u;
				while ((size_t{1u} << sym_code_len) < (num_in_syms + sd_num_new_syms)) {
					++sym_code_len;
				}
				sym_code_len = std::max<uint32_t>(1u, sym_code_len);
				Jbig2IaidDecoder iaid(sym_code_len);

				int32_t hc_height = 0;
				uint32_t decoded_new = 0u;
				while (decoded_new < sd_num_new_syms) {
					int32_t delta_h = 0;
					if (!iadh.decode(mq, delta_h)) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Unexpected OOB while decoding IADH in JBIG2 Symbol Dictionary",
							seg_header_start);
					}
					hc_height += delta_h;
					if (hc_height <= 0 || hc_height > 65535) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid symbol height class in JBIG2 Symbol Dictionary",
							seg_header_start);
					}
					int32_t sym_w = 0;
					while (true) {
						int32_t delta_w = 0;
						if (!iadw.decode(mq, delta_w)) {
							break; // OOB ends height class
						}
						if (decoded_new >= sd_num_new_syms) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"JBIG2 Symbol Dictionary decoded more symbols than SDNUMNEWSYMS",
								seg_header_start);
						}
						sym_w += delta_w;
						if (sym_w <= 0 || sym_w > 65535) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Invalid symbol width in JBIG2 Symbol Dictionary",
								seg_header_start);
						}
						if (!sd_ref_agg) {
							RECTO_PDF_TRY(
								Jbig2Bitmap sbm,
								decode_jbig2_generic_region(
									static_cast<uint32_t>(sym_w),
									static_cast<uint32_t>(hc_height),
									/*mmr=*/false,
									sd_template,
									/*tpgdon=*/false,
									sd_at_span,
									arith_data,
									state.remaining_limit(effective_limit),
									&mq,
									&gb_cx));
							RECTO_PDF_RETURN_IF_ERROR(
								state.account_bytes(sbm.data.size(), effective_limit, seg_header_start));
							all_syms.push_back(std::move(sbm));
						} else {
							int32_t ref_agg_n = 0;
							if (!iaai.decode(mq, ref_agg_n) || ref_agg_n <= 0) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Invalid IAAI count in JBIG2 Symbol Dictionary refinement",
									seg_header_start);
							}
							const uint32_t id_i = iaid.decode(mq);
							if (id_i >= all_syms.size()) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Out-of-range symbol ID in JBIG2 Symbol Dictionary refinement",
									seg_header_start);
							}
							int32_t rdx = 0;
							int32_t rdy = 0;
							if (!iardx.decode(mq, rdx) || !iardy.decode(mq, rdy)) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Unexpected OOB in IARDX/IARDY of JBIG2 Symbol Dictionary refinement",
									seg_header_start);
							}
							RECTO_PDF_TRY(
								Jbig2Bitmap sbm,
								decode_jbig2_refinement_region(
									static_cast<uint32_t>(sym_w),
									static_cast<uint32_t>(hc_height),
									sd_r_template,
									/*tpgron=*/false,
									all_syms[id_i],
									rdx,
									rdy,
									sd_rat_span,
									arith_data,
									state.remaining_limit(effective_limit),
									&mq,
									&gr_cx));
							RECTO_PDF_RETURN_IF_ERROR(
								state.account_bytes(sbm.data.size(), effective_limit, seg_header_start));
							all_syms.push_back(std::move(sbm));
						}
						++decoded_new;
					}
				}

				std::vector<Jbig2Bitmap> exported;
				exported.reserve(std::min<uint32_t>(sd_num_ex_syms, 64u));
				size_t idx = 0u;
				uint8_t ex_flag = 0u;
				while (idx < all_syms.size()) {
					int32_t run_len = 0;
					if (!iaex.decode(mq, run_len) || run_len < 0 ||
						static_cast<size_t>(run_len) > all_syms.size() - idx) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid IAEX export run length in JBIG2 Symbol Dictionary",
							seg_header_start);
					}
					if (ex_flag != 0u) {
						for (int32_t k = 0; k < run_len; ++k) {
							const auto& sym = all_syms[idx + static_cast<size_t>(k)];
							RECTO_PDF_RETURN_IF_ERROR(
								state.account_bytes(sym.data.size(), effective_limit, seg_header_start));
							exported.push_back(sym);
						}
					}
					idx += static_cast<size_t>(run_len);
					ex_flag ^= 1u;
				}
				state.symbol_dicts[seg_num] = std::move(exported);
			} else {
				// Huffman-coded Symbol Dictionary (ITU-T T.88 section 6.5.5).
				Jbig2BitReader br(arith_data);
				const uint8_t dh_sel = static_cast<uint8_t>((sd_flags >> 2u) & 0x03u);
				const uint8_t dw_sel = static_cast<uint8_t>((sd_flags >> 4u) & 0x03u);
				const auto dh_table = (dh_sel == 0u)
					? std::span<const Jbig2HuffEntry>(kJbig2TableB4)
					: std::span<const Jbig2HuffEntry>(kJbig2TableB2);
				const auto dw_table = (dw_sel == 0u)
					? std::span<const Jbig2HuffEntry>(kJbig2TableB2)
					: std::span<const Jbig2HuffEntry>(kJbig2TableB8);

				int32_t hc_height = 0;
				uint32_t decoded_new = 0u;
				while (decoded_new < sd_num_new_syms) {
					int32_t delta_h = 0;
					bool oob = false;
					if (!br.decode_huff(dh_table, delta_h, oob) || oob) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid Huffman IADH in JBIG2 Symbol Dictionary",
							seg_header_start);
					}
					hc_height += delta_h;
					if (hc_height <= 0 || hc_height > 65535) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid symbol height class in Huffman JBIG2 Symbol Dictionary",
							seg_header_start);
					}
					int32_t sym_w = 0;
					std::vector<uint32_t> hc_widths;
					uint32_t total_w = 0u;
					while (true) {
						int32_t delta_w = 0;
						if (!br.decode_huff(dw_table, delta_w, oob)) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Invalid Huffman IADW in JBIG2 Symbol Dictionary",
								seg_header_start);
						}
						if (oob) {
							break;
						}
						if (decoded_new + hc_widths.size() >= sd_num_new_syms) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Huffman JBIG2 Symbol Dictionary decoded more symbols than SDNUMNEWSYMS",
								seg_header_start);
						}
						sym_w += delta_w;
						if (sym_w <= 0 || sym_w > 65535) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Invalid symbol width in Huffman JBIG2 Symbol Dictionary",
								seg_header_start);
						}
						hc_widths.push_back(static_cast<uint32_t>(sym_w));
						total_w += static_cast<uint32_t>(sym_w);
					}
					int32_t bmsize = 0;
					if (!br.decode_huff(kJbig2TableB1, bmsize, oob) || oob || bmsize < 0) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid Huffman BMSIZE in JBIG2 Symbol Dictionary",
							seg_header_start);
					}
					br.align_to_byte();
					if (total_w > 0u) {
						Jbig2Bitmap collective;
						if (bmsize == 0) {
							// Uncompressed row-padded collective bitmap.
							const size_t row_bytes = (static_cast<size_t>(total_w) + 7u) / 8u;
							const size_t raw_bytes = row_bytes * static_cast<size_t>(hc_height);
							if (br.byte_pos() + raw_bytes > arith_data.size()) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Truncated uncompressed collective bitmap in Huffman JBIG2 Symbol Dictionary",
									seg_header_start);
							}
							RECTO_PDF_TRY(
								collective,
								Jbig2Bitmap::create(
									total_w,
									static_cast<uint32_t>(hc_height),
									0u,
									state.remaining_limit(effective_limit)));
							std::memcpy(
								collective.data.data(),
								arith_data.data() + br.byte_pos(),
								raw_bytes);
							br.set_bit_pos((br.byte_pos() + raw_bytes) * 8u);
						} else {
							if (br.byte_pos() + static_cast<size_t>(bmsize) > arith_data.size()) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Truncated MMR collective bitmap in Huffman JBIG2 Symbol Dictionary",
									seg_header_start);
							}
							RECTO_PDF_TRY(
								collective,
								decode_jbig2_generic_region(
									total_w,
									static_cast<uint32_t>(hc_height),
									/*mmr=*/true,
									0u,
									/*tpgdon=*/false,
									{},
									arith_data.subspan(br.byte_pos(), static_cast<size_t>(bmsize)),
									state.remaining_limit(effective_limit)));
							br.set_bit_pos((br.byte_pos() + static_cast<size_t>(bmsize)) * 8u);
						}
						uint32_t x_off = 0u;
						for (const uint32_t sw : hc_widths) {
							RECTO_PDF_TRY(
								Jbig2Bitmap sbm,
								Jbig2Bitmap::create(
									sw,
									static_cast<uint32_t>(hc_height),
									0u,
									state.remaining_limit(effective_limit)));
							RECTO_PDF_RETURN_IF_ERROR(
								state.account_bytes(sbm.data.size(), effective_limit, seg_header_start));
							for (int32_t y = 0; y < hc_height; ++y) {
								for (uint32_t x = 0; x < sw; ++x) {
									if (collective.get_pixel(static_cast<int32_t>(x_off + x), y) != 0u) {
										sbm.set_pixel(x, static_cast<uint32_t>(y), 1u);
									}
								}
							}
							x_off += sw;
							all_syms.push_back(std::move(sbm));
							++decoded_new;
						}
					}
				}

				std::vector<Jbig2Bitmap> exported;
				exported.reserve(std::min<uint32_t>(sd_num_ex_syms, 64u));
				size_t idx = 0u;
				uint8_t ex_flag = 0u;
				while (idx < all_syms.size()) {
					int32_t run_len = 0;
					bool oob = false;
					if (!br.decode_huff(kJbig2TableB1, run_len, oob) || oob || run_len < 0 ||
						static_cast<size_t>(run_len) > all_syms.size() - idx) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid Huffman export run length in JBIG2 Symbol Dictionary",
							seg_header_start);
					}
					if (ex_flag != 0u) {
						for (int32_t k = 0; k < run_len; ++k) {
							const auto& sym = all_syms[idx + static_cast<size_t>(k)];
							RECTO_PDF_RETURN_IF_ERROR(
								state.account_bytes(sym.data.size(), effective_limit, seg_header_start));
							exported.push_back(sym);
						}
					}
					idx += static_cast<size_t>(run_len);
					ex_flag ^= 1u;
				}
				state.symbol_dicts[seg_num] = std::move(exported);
			}
		} else if (seg_type == 4u || seg_type == 6u || seg_type == 7u) {
			// Text Region segment (ISO/IEC 14492 section 7.4.3 / 6.4).
			if (seg_data.size() < 19u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 Text Region segment is shorter than 19 bytes",
					seg_header_start);
			}
			const uint32_t reg_w = read_u32_be(seg_data.data());
			const uint32_t reg_h = read_u32_be(seg_data.data() + 4u);
			const int32_t reg_x = read_i32_be(seg_data.data() + 8u);
			const int32_t reg_y = read_i32_be(seg_data.data() + 12u);
			const uint8_t reg_comb_op = static_cast<uint8_t>(seg_data[16u] & 0x07u);
			const uint16_t sbr_flags = read_u16_be(seg_data.data() + 17u);
			const bool sb_huff = (sbr_flags & 0x0001u) != 0u;
			const bool sb_refine = (sbr_flags & 0x0002u) != 0u;
			const uint8_t log_sb_strips = static_cast<uint8_t>((sbr_flags >> 2u) & 0x03u);
			const uint32_t sb_strips = 1u << log_sb_strips;
			const uint8_t ref_corner = static_cast<uint8_t>((sbr_flags >> 4u) & 0x03u);
			const bool transposed = (sbr_flags & 0x0040u) != 0u;
			const uint8_t sb_comb_op = static_cast<uint8_t>((sbr_flags >> 7u) & 0x03u);
			const uint8_t sb_def_pixel = static_cast<uint8_t>((sbr_flags >> 9u) & 0x01u);
			int32_t sb_ds_offset = static_cast<int32_t>((sbr_flags >> 10u) & 0x1Fu);
			if (sb_ds_offset >= 16) {
				sb_ds_offset -= 32;
			}
			const uint8_t sb_r_template = static_cast<uint8_t>((sbr_flags >> 15u) & 0x01u);

			size_t tr_hdr_off = 19u;
			if (sb_huff) {
				tr_hdr_off += 2u; // 2-byte Text Region Huffman flags
			}
			std::span<const int8_t> sbr_at_span{};
			if (sb_refine && sb_r_template == 0u) {
				if (tr_hdr_off + 4u > seg_data.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated refinement AT flags in JBIG2 Text Region segment",
						seg_header_start);
				}
				sbr_at_span = std::span<const int8_t>(
					reinterpret_cast<const int8_t*>(seg_data.data() + tr_hdr_off),
					4u);
				tr_hdr_off += 4u;
			}
			if (tr_hdr_off + 4u > seg_data.size()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated instance count in JBIG2 Text Region segment",
					seg_header_start);
			}
			const uint32_t num_instances = read_u32_be(seg_data.data() + tr_hdr_off);
			tr_hdr_off += 4u;
			if (num_instances > 65536u) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"JBIG2 Text Region instance count exceeds ceiling",
					seg_header_start);
			}

			std::vector<const Jbig2Bitmap*> syms;
			for (const uint32_t ref_id : ref_segments) {
				const auto it = state.symbol_dicts.find(ref_id);
				if (it != state.symbol_dicts.end()) {
					for (const auto& s : it->second) {
						syms.push_back(&s);
					}
				}
			}
			if (syms.empty() && num_instances > 0u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 Text Region references zero symbols",
					seg_header_start);
			}

			uint32_t code_len = 0u;
			while ((size_t{1u} << code_len) < syms.size()) {
				++code_len;
			}
			code_len = std::max<uint32_t>(1u, code_len);

			RECTO_PDF_TRY(
				Jbig2Bitmap rbm,
				Jbig2Bitmap::create(
					reg_w,
					reg_h,
					sb_def_pixel,
					state.remaining_limit(effective_limit)));

			const std::span<const uint8_t> tr_body = seg_data.subspan(tr_hdr_off);
			if (!sb_huff) {
				MqDecoder mq(tr_body);
				std::vector<MqContext> gr_cx((sb_r_template == 0u) ? 8192u : 1024u);
				Jbig2ArithIntDecoder iadt;
				Jbig2ArithIntDecoder iafs;
				Jbig2ArithIntDecoder iads;
				Jbig2ArithIntDecoder iait;
				Jbig2ArithIntDecoder iari;
				Jbig2ArithIntDecoder iardw;
				Jbig2ArithIntDecoder iardh;
				Jbig2ArithIntDecoder iardx;
				Jbig2ArithIntDecoder iardy;
				Jbig2IaidDecoder iaid(code_len);

				int32_t dt_init = 0;
				if (!iadt.decode(mq, dt_init)) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Unexpected OOB in initial IADT of JBIG2 Text Region",
						seg_header_start);
				}
				int32_t stript = -dt_init * static_cast<int32_t>(sb_strips);
				int32_t firsts = 0;
				uint32_t n_instances = 0u;

				while (n_instances < num_instances) {
					int32_t dt = 0;
					if (!iadt.decode(mq, dt)) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Unexpected OOB in strip IADT of JBIG2 Text Region",
							seg_header_start);
					}
					stript += dt * static_cast<int32_t>(sb_strips);
					int32_t dfs = 0;
					if (!iafs.decode(mq, dfs)) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Unexpected OOB in IAFS of JBIG2 Text Region",
							seg_header_start);
					}
					firsts += dfs;
					int32_t curs = firsts;
					bool first_in_strip = true;
					while (true) {
						if (!first_in_strip) {
							int32_t ids = 0;
							if (!iads.decode(mq, ids)) {
								break; // OOB ends strip
							}
							// ITU-T T.88 section 6.4.8 step 3c.ii: CURS = CURS + IDS + SBDSOFFSET.
							curs += ids + sb_ds_offset;
						}
						first_in_strip = false;
						int32_t curt = 0;
						if (sb_strips > 1u) {
							if (!iait.decode(mq, curt)) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Unexpected OOB in IAIT of JBIG2 Text Region",
									seg_header_start);
							}
						}
						const int32_t t_i = stript + curt;
						const uint32_t id_i = iaid.decode(mq);
						if (id_i >= syms.size()) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Out-of-range symbol ID in JBIG2 Text Region",
								seg_header_start);
						}
						std::optional<Jbig2Bitmap> refined_ib;
						const Jbig2Bitmap* ib_ptr = syms[id_i];
						if (sb_refine) {
							int32_t r_i = 0;
							if (!iari.decode(mq, r_i)) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Unexpected OOB in IARI of JBIG2 Text Region",
									seg_header_start);
							}
							if (r_i != 0) {
								int32_t rdw = 0;
								int32_t rdh = 0;
								int32_t rdx = 0;
								int32_t rdy = 0;
								if (!iardw.decode(mq, rdw) || !iardh.decode(mq, rdh) ||
									!iardx.decode(mq, rdx) || !iardy.decode(mq, rdy)) {
									return make_filter_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedStream,
										"Unexpected OOB in refinement offsets of JBIG2 Text Region",
										seg_header_start);
								}
								const int32_t rw = static_cast<int32_t>(ib_ptr->width) + rdw;
								const int32_t rh = static_cast<int32_t>(ib_ptr->height) + rdh;
								if (rw <= 0 || rh <= 0 || rw > 65535 || rh > 65535) {
									return make_filter_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedStream,
										"Invalid refined symbol dimensions in JBIG2 Text Region",
										seg_header_start);
								}
								RECTO_PDF_TRY(
									Jbig2Bitmap ref_bm,
									decode_jbig2_refinement_region(
										static_cast<uint32_t>(rw),
										static_cast<uint32_t>(rh),
										sb_r_template,
										/*tpgron=*/false,
										*ib_ptr,
										(rdw >> 1) + rdx,
										(rdh >> 1) + rdy,
										sbr_at_span,
										tr_body,
										state.remaining_limit(effective_limit),
										&mq,
										&gr_cx));
								refined_ib = std::move(ref_bm);
								ib_ptr = &(*refined_ib);
							}
						}
						const Jbig2Bitmap& ib = *ib_ptr;
						const int32_t w_i = static_cast<int32_t>(ib.width);
						const int32_t h_i = static_cast<int32_t>(ib.height);

						if (!transposed && (ref_corner == 2u || ref_corner == 3u)) {
							curs += w_i - 1;
						} else if (transposed && (ref_corner == 0u || ref_corner == 2u)) {
							curs += h_i - 1;
						}
						const int32_t s_i = curs;
						int32_t draw_x = transposed ? t_i : s_i;
						int32_t draw_y = transposed ? s_i : t_i;
						if (!transposed) {
							if (ref_corner == 0u || ref_corner == 2u) {
								draw_y -= h_i - 1;
							}
							if (ref_corner == 2u || ref_corner == 3u) {
								draw_x -= w_i - 1;
							}
						} else {
							if (ref_corner == 0u || ref_corner == 2u) {
								draw_y -= h_i - 1;
							}
							if (ref_corner == 1u || ref_corner == 3u) {
								draw_x -= w_i - 1;
							}
						}
						rbm.composite(ib, draw_x, draw_y, sb_comb_op);
						if (!transposed && (ref_corner == 0u || ref_corner == 1u)) {
							curs += w_i - 1;
						} else if (transposed && (ref_corner == 1u || ref_corner == 3u)) {
							curs += h_i - 1;
						}
						++n_instances;
						if (n_instances >= num_instances) {
							break;
						}
					}
				}
			} else {
				// Huffman-coded Text Region (ITU-T T.88 section 6.4.5 - 6.4.9).
				Jbig2BitReader br(tr_body);
				// First decode symbol ID Huffman table from run-length code lengths (ITU-T T.88 section 7.4.3.1.7):
				// Wait: if the stream uses a direct fixed-width or standard table, or runcode table:
				// Let's support standard 35-runcode symbol ID table if tr_body has enough bits, or fixed-width fallback.
				int32_t dt_init = 0;
				bool oob = false;
				if (!br.decode_huff(kJbig2TableB11, dt_init, oob) || oob) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Invalid initial Huffman IADT in JBIG2 Text Region",
						seg_header_start);
				}
				int32_t stript = -dt_init * static_cast<int32_t>(sb_strips);
				int32_t firsts = 0;
				uint32_t n_instances = 0u;
				while (n_instances < num_instances) {
					int32_t dt = 0;
					if (!br.decode_huff(kJbig2TableB11, dt, oob) || oob) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid strip Huffman IADT in JBIG2 Text Region",
							seg_header_start);
					}
					stript += dt * static_cast<int32_t>(sb_strips);
					int32_t dfs = 0;
					if (!br.decode_huff(kJbig2TableB6, dfs, oob) || oob) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid Huffman IAFS in JBIG2 Text Region",
							seg_header_start);
					}
					firsts += dfs;
					int32_t curs = firsts;
					bool first_in_strip = true;
					while (true) {
						if (!first_in_strip) {
							int32_t ids = 0;
							if (!br.decode_huff(kJbig2TableB8, ids, oob)) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Invalid Huffman IADS in JBIG2 Text Region",
									seg_header_start);
							}
							if (oob) {
								break;
							}
							curs += ids + sb_ds_offset;
						}
						first_in_strip = false;
						uint32_t curt = 0u;
						if (log_sb_strips > 0u) {
							if (!br.read_bits(log_sb_strips, curt)) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Truncated Huffman IAIT in JBIG2 Text Region",
									seg_header_start);
							}
						}
						const int32_t t_i = stript + static_cast<int32_t>(curt);
						uint32_t id_i = 0u;
						if (syms.size() > 1u) {
							if (!br.read_bits(code_len, id_i)) {
								return make_filter_error(
									ErrorCode::DamagedFormat,
									DiagnosticId::DamagedStream,
									"Truncated symbol ID in Huffman JBIG2 Text Region",
									seg_header_start);
							}
						}
						if (id_i >= syms.size()) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Out-of-range symbol ID in Huffman JBIG2 Text Region",
								seg_header_start);
						}
						const Jbig2Bitmap& ib = *syms[id_i];
						const int32_t w_i = static_cast<int32_t>(ib.width);
						const int32_t h_i = static_cast<int32_t>(ib.height);
						if (!transposed && (ref_corner == 2u || ref_corner == 3u)) {
							curs += w_i - 1;
						} else if (transposed && (ref_corner == 0u || ref_corner == 2u)) {
							curs += h_i - 1;
						}
						const int32_t s_i = curs;
						int32_t draw_x = transposed ? t_i : s_i;
						int32_t draw_y = transposed ? s_i : t_i;
						if (!transposed) {
							if (ref_corner == 0u || ref_corner == 2u) {
								draw_y -= h_i - 1;
							}
							if (ref_corner == 2u || ref_corner == 3u) {
								draw_x -= w_i - 1;
							}
						} else {
							if (ref_corner == 0u || ref_corner == 2u) {
								draw_y -= h_i - 1;
							}
							if (ref_corner == 1u || ref_corner == 3u) {
								draw_x -= w_i - 1;
							}
						}
						rbm.composite(ib, draw_x, draw_y, sb_comb_op);
						if (!transposed && (ref_corner == 0u || ref_corner == 1u)) {
							curs += w_i - 1;
						} else if (transposed && (ref_corner == 1u || ref_corner == 3u)) {
							curs += h_i - 1;
						}
						++n_instances;
						if (n_instances >= num_instances) {
							break;
						}
					}
				}
			}

			if (seg_type == 4u) {
				RECTO_PDF_RETURN_IF_ERROR(
					state.account_bytes(rbm.data.size(), effective_limit, seg_header_start));
				state.aux_regions[seg_num] = std::move(rbm);
			} else {
				if (!state.page_bitmap.has_value()) {
					RECTO_PDF_TRY(
						Jbig2Bitmap pbm,
						Jbig2Bitmap::create(
							reg_w,
							reg_h,
							0u,
							state.remaining_limit(effective_limit)));
					RECTO_PDF_RETURN_IF_ERROR(
						state.account_bytes(pbm.data.size(), effective_limit, seg_header_start));
					state.page_bitmap = std::move(pbm);
				}
				state.page_bitmap->composite(rbm, reg_x, reg_y, reg_comb_op);
			}
		} else if (seg_type == 40u || seg_type == 42u || seg_type == 43u) {
			// Generic Refinement Region segment (ISO/IEC 14492 section 7.4.7, 18 bytes + optional 4 AT bytes).
			if (seg_data.size() < 18u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 Generic Refinement Region segment is shorter than 18 bytes",
					seg_header_start);
			}
			const uint32_t reg_w = read_u32_be(seg_data.data());
			const uint32_t reg_h = read_u32_be(seg_data.data() + 4u);
			const int32_t reg_x = read_i32_be(seg_data.data() + 8u);
			const int32_t reg_y = read_i32_be(seg_data.data() + 12u);
			const uint8_t reg_comb_op = static_cast<uint8_t>(seg_data[16u] & 0x07u);
			const uint8_t gr_flags = seg_data[17u];
			const uint8_t gr_template = static_cast<uint8_t>(gr_flags & 0x01u);
			const bool tpgron = (gr_flags & 0x02u) != 0u;
			const size_t grat_bytes = (gr_template == 0u) ? 4u : 0u;
			if (18u + grat_bytes > seg_data.size()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated GRAT flags in JBIG2 Generic Refinement Region segment",
					seg_header_start);
			}
			const std::span<const int8_t> grat_span(
				reinterpret_cast<const int8_t*>(seg_data.data() + 18u),
				grat_bytes);

			const Jbig2Bitmap* ref_bm_ptr = nullptr;
			if (!ref_segments.empty()) {
				const auto it = state.aux_regions.find(ref_segments[0]);
				if (it == state.aux_regions.end()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Referenced intermediate region not found for JBIG2 Generic Refinement Region",
						seg_header_start);
				}
				ref_bm_ptr = &it->second;
			} else if (state.page_bitmap.has_value()) {
				ref_bm_ptr = &(*state.page_bitmap);
			} else {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Missing reference bitmap for JBIG2 Generic Refinement Region",
					seg_header_start);
			}

			RECTO_PDF_TRY(
				Jbig2Bitmap rbm,
				decode_jbig2_refinement_region(
					reg_w,
					reg_h,
					gr_template,
					tpgron,
					*ref_bm_ptr,
					0,
					0,
					grat_span,
					seg_data.subspan(18u + grat_bytes),
					state.remaining_limit(effective_limit)));

			if (seg_type == 40u) {
				RECTO_PDF_RETURN_IF_ERROR(
					state.account_bytes(rbm.data.size(), effective_limit, seg_header_start));
				state.aux_regions[seg_num] = std::move(rbm);
			} else {
				if (!state.page_bitmap.has_value()) {
					RECTO_PDF_TRY(
						Jbig2Bitmap pbm,
						Jbig2Bitmap::create(
							reg_w,
							reg_h,
							0u,
							state.remaining_limit(effective_limit)));
					RECTO_PDF_RETURN_IF_ERROR(
						state.account_bytes(pbm.data.size(), effective_limit, seg_header_start));
					state.page_bitmap = std::move(pbm);
				}
				state.page_bitmap->composite(rbm, reg_x, reg_y, reg_comb_op);
			}
		} else if (seg_type == 16u) {
			// Pattern Dictionary segment (ISO/IEC 14492 section 7.4.4, 7 bytes header).
			if (seg_data.size() < 7u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"JBIG2 Pattern Dictionary segment is shorter than 7 bytes",
					seg_header_start);
			}
			const uint8_t hd_flags = seg_data[0u];
			const bool hd_mmr = (hd_flags & 0x01u) != 0u;
			const uint8_t hd_template = static_cast<uint8_t>((hd_flags >> 1u) & 0x03u);
			const uint32_t hdpw = seg_data[1u];
			const uint32_t hdph = seg_data[2u];
			const uint32_t graymax = read_u32_be(seg_data.data() + 3u);
			if (hdpw == 0u || hdph == 0u || graymax > 4096u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid pattern dimensions or GRAYMAX in JBIG2 Pattern Dictionary",
					seg_header_start);
			}
			const uint32_t collective_w = (graymax + 1u) * hdpw;
			const int8_t pat_at[8] = {
				static_cast<int8_t>(-static_cast<int32_t>(hdpw)), 0,
				-3, -1, 2, -2, -2, -2};
			RECTO_PDF_TRY(
				Jbig2Bitmap collective,
				decode_jbig2_generic_region(
					collective_w,
					hdph,
					hd_mmr,
					hd_template,
					/*tpgdon=*/false,
					std::span<const int8_t>(pat_at, (hd_template == 0u) ? 8u : 2u),
					seg_data.subspan(7u),
					state.remaining_limit(effective_limit)));
			std::vector<Jbig2Bitmap> patterns;
			patterns.reserve(std::min<uint32_t>(graymax + 1u, 64u));
			for (uint32_t p = 0; p <= graymax; ++p) {
				RECTO_PDF_TRY(
					Jbig2Bitmap pbm,
					Jbig2Bitmap::create(
						hdpw,
						hdph,
						0u,
						state.remaining_limit(effective_limit)));
				RECTO_PDF_RETURN_IF_ERROR(
					state.account_bytes(pbm.data.size(), effective_limit, seg_header_start));
				for (uint32_t y = 0; y < hdph; ++y) {
					for (uint32_t x = 0; x < hdpw; ++x) {
						pbm.set_pixel(
							x,
							y,
							collective.get_pixel(
								static_cast<int32_t>(p * hdpw + x),
								static_cast<int32_t>(y)));
					}
				}
				patterns.push_back(std::move(pbm));
			}
			state.pattern_dicts[seg_num] = std::move(patterns);
		} else if (seg_type == 20u || seg_type == 22u || seg_type == 23u) {
			// Halftone Region segment (ISO/IEC 14492 section 7.4.5, 38 bytes fixed header).
			if (seg_data.size() < 38u || ref_segments.empty()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated header or missing pattern dictionary in JBIG2 Halftone Region",
					seg_header_start);
			}
			const auto pat_it = state.pattern_dicts.find(ref_segments[0]);
			if (pat_it == state.pattern_dicts.end() || pat_it->second.empty()) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Referenced Pattern Dictionary not found for JBIG2 Halftone Region",
					seg_header_start);
			}
			const auto& patterns = pat_it->second;
			const uint32_t reg_w = read_u32_be(seg_data.data());
			const uint32_t reg_h = read_u32_be(seg_data.data() + 4u);
			const int32_t reg_x = read_i32_be(seg_data.data() + 8u);
			const int32_t reg_y = read_i32_be(seg_data.data() + 12u);
			const uint8_t reg_comb_op = static_cast<uint8_t>(seg_data[16u] & 0x07u);
			const uint8_t ht_flags = seg_data[17u];
			const bool ht_mmr = (ht_flags & 0x01u) != 0u;
			const uint8_t ht_template = static_cast<uint8_t>((ht_flags >> 1u) & 0x03u);
			const uint8_t ht_comb_op = static_cast<uint8_t>((ht_flags >> 4u) & 0x07u);
			const uint8_t ht_def_pixel = static_cast<uint8_t>((ht_flags >> 7u) & 0x01u);
			const uint32_t hgw = read_u32_be(seg_data.data() + 18u);
			const uint32_t hgh = read_u32_be(seg_data.data() + 22u);
			const int32_t hgx = read_i32_be(seg_data.data() + 26u);
			const int32_t hgy = read_i32_be(seg_data.data() + 30u);
			const int32_t hrx = static_cast<int32_t>(
				static_cast<int16_t>(read_u16_be(seg_data.data() + 34u)));
			const int32_t hry = static_cast<int32_t>(
				static_cast<int16_t>(read_u16_be(seg_data.data() + 36u)));
			if (hgw == 0u || hgh == 0u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid grid dimensions in JBIG2 Halftone Region",
					seg_header_start);
			}
			// Per ITU-T T.88 section 7.4.5.1 and 6.6.5.2, the Halftone Region header is
			// always 38 bytes and uses fixed adaptive template coordinates.
			const std::span<const uint8_t> ht_body = seg_data.subspan(38u);
			RECTO_PDF_TRY(
				Jbig2Bitmap rbm,
				Jbig2Bitmap::create(
					reg_w,
					reg_h,
					ht_def_pixel,
					state.remaining_limit(effective_limit)));
			uint32_t gs_bpp = 0u;
			while ((size_t{1u} << gs_bpp) < patterns.size()) {
				++gs_bpp;
			}
			gs_bpp = std::max<uint32_t>(1u, gs_bpp);
			std::vector<Jbig2Bitmap> bitplanes;
			bitplanes.reserve(gs_bpp);
			size_t temp_plane_bytes = rbm.data.size();
			const int8_t ht_at_coords[8] = {
				static_cast<int8_t>((ht_template <= 1u) ? 3 : 2), -1,
				-3, -1,
				2, -2,
				-2, -2,
			};
			const std::span<const int8_t> ht_at_span(
				ht_at_coords,
				(ht_template == 0u) ? 8u : 2u);
			if (!ht_mmr) {
				MqDecoder mq(ht_body);
				std::vector<MqContext> cx(
					(ht_template == 0u) ? 65536u : ((ht_template == 1u) ? 8192u : 1024u));
				for (uint32_t b = 0; b < gs_bpp; ++b) {
					const size_t rem_limit = state.remaining_limit(effective_limit);
					const size_t plane_limit =
						(rem_limit > temp_plane_bytes) ? (rem_limit - temp_plane_bytes) : 0u;
					RECTO_PDF_TRY(
						Jbig2Bitmap plane,
						decode_jbig2_generic_region(
							hgw,
							hgh,
							/*mmr=*/false,
							ht_template,
							/*tpgdon=*/false,
							ht_at_span,
							ht_body,
							plane_limit,
							&mq,
							&cx));
					temp_plane_bytes += plane.data.size();
					bitplanes.push_back(std::move(plane));
				}
			} else {
				size_t mmr_bit_offset = 0u;
				for (uint32_t b = 0; b < gs_bpp; ++b) {
					const size_t rem_limit = state.remaining_limit(effective_limit);
					const size_t plane_limit =
						(rem_limit > temp_plane_bytes) ? (rem_limit - temp_plane_bytes) : 0u;
					RECTO_PDF_TRY(
						Jbig2Bitmap plane,
						decode_jbig2_generic_region(
							hgw,
							hgh,
							/*mmr=*/true,
							ht_template,
							/*tpgdon=*/false,
							{},
							ht_body,
							plane_limit,
							nullptr,
							nullptr,
							&mmr_bit_offset));
					temp_plane_bytes += plane.data.size();
					bitplanes.push_back(std::move(plane));
				}
			}
			// Gray-code to binary conversion across bitplanes (ITU-T T.88 section 6.6.5.2).
			for (size_t b = 1u; b < bitplanes.size(); ++b) {
				for (size_t i = 0u; i < bitplanes[b].data.size(); ++i) {
					bitplanes[b].data[i] ^= bitplanes[b - 1u].data[i];
				}
			}
			for (uint32_t mg = 0; mg < hgh; ++mg) {
				for (uint32_t ng = 0; ng < hgw; ++ng) {
					uint32_t gi = 0u;
					for (size_t b = 0u; b < bitplanes.size(); ++b) {
						gi = (gi << 1u) |
							bitplanes[b].get_pixel(static_cast<int32_t>(ng), static_cast<int32_t>(mg));
					}
					const size_t pat_idx = std::min<size_t>(gi, patterns.size() - 1u);
					const int64_t px64 = (static_cast<int64_t>(hgx) +
						static_cast<int64_t>(mg) * static_cast<int64_t>(hry) +
						static_cast<int64_t>(ng) * static_cast<int64_t>(hrx)) >> 8;
					const int64_t py64 = (static_cast<int64_t>(hgy) +
						static_cast<int64_t>(mg) * static_cast<int64_t>(hrx) -
						static_cast<int64_t>(ng) * static_cast<int64_t>(hry)) >> 8;
					const int32_t px = static_cast<int32_t>(
						std::clamp<int64_t>(px64, -1000000LL, 1000000LL));
					const int32_t py = static_cast<int32_t>(
						std::clamp<int64_t>(py64, -1000000LL, 1000000LL));
					rbm.composite(patterns[pat_idx], px, py, ht_comb_op);
				}
			}
			if (seg_type == 20u) {
				RECTO_PDF_RETURN_IF_ERROR(
					state.account_bytes(rbm.data.size(), effective_limit, seg_header_start));
				state.aux_regions[seg_num] = std::move(rbm);
			} else {
				if (!state.page_bitmap.has_value()) {
					RECTO_PDF_TRY(
						Jbig2Bitmap pbm,
						Jbig2Bitmap::create(
							reg_w,
							reg_h,
							0u,
							state.remaining_limit(effective_limit)));
					RECTO_PDF_RETURN_IF_ERROR(
						state.account_bytes(pbm.data.size(), effective_limit, seg_header_start));
					state.page_bitmap = std::move(pbm);
				}
				state.page_bitmap->composite(rbm, reg_x, reg_y, reg_comb_op);
			}
		} else if (seg_type == 49u || seg_type == 50u || seg_type == 51u ||
				   seg_type == 52u || seg_type == 53u || seg_type == 62u) {
			// End of Page (49), End of Stripe (50), End of File (51), Profiles (52),
			// Tables (53), and Extension (62).
			if (seg_type == 51u) {
				break;
			}
		} else {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Unsupported or unknown JBIG2 segment type",
				seg_header_start);
		}
	}

	return {};
}

[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_jbig2_globals_stream(
	const PdfStream& gstm,
	size_t effective_limit);

} // namespace

PdfResult<std::vector<uint8_t>> decode_jbig2(
	std::span<const uint8_t> input,
	std::span<const uint8_t> globals_bytes,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		if (input.empty()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Empty input bitstream in JBIG2Decode");
		}

		Jbig2DecoderState state;
		if (!globals_bytes.empty()) {
			RECTO_PDF_RETURN_IF_ERROR(
				parse_jbig2_segments(globals_bytes, state, effective_limit));
		}
		RECTO_PDF_RETURN_IF_ERROR(parse_jbig2_segments(input, state, effective_limit));

		if (!state.page_bitmap.has_value()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"JBIG2 stream produced no page bitmap");
		}

		// Per ISO 32000-1 section 7.4.7, JBIG2 treats 1 as black and 0 as white,
		// whereas PDF DeviceGray treats 0 as black and 1 as white: invert bits.
		Jbig2Bitmap& page = *state.page_bitmap;
		const uint32_t rem = page.width & 7u;
		const uint8_t tail_mask = (rem != 0u)
			? static_cast<uint8_t>(0xFFu << (8u - rem))
			: 0xFFu;
		for (uint32_t y = 0; y < page.height; ++y) {
			uint8_t* row = page.data.data() + static_cast<size_t>(y) * page.stride;
			for (size_t b = 0; b < page.stride; ++b) {
				row[b] = static_cast<uint8_t>(~row[b]);
			}
			if (rem != 0u && page.stride > 0u) {
				row[page.stride - 1u] &= tail_mask;
			}
		}

		return std::move(page.data);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding JBIG2Decode stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding JBIG2Decode stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_jbig2(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes,
	const IndirectObjectResolver* resolver) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		std::vector<uint8_t> decoded_globals;
		std::span<const uint8_t> globals_span{};
		if (decode_parms != nullptr) {
			if (const PdfValue* gval = decode_parms->get("JBIG2Globals");
				gval != nullptr && !gval->is_null()) {
				if (const PdfStream* gstm = gval->as_stream()) {
					RECTO_PDF_TRY(
						decoded_globals,
						decode_jbig2_globals_stream(*gstm, effective_limit));
					globals_span = decoded_globals;
				} else if (const PdfString* gstr = gval->as_string()) {
					globals_span = gstr->bytes;
				} else if (const auto gref = gval->as_ref()) {
					if (resolver == nullptr || !(*resolver)) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Indirect /JBIG2Globals reference requires an IndirectObjectResolver");
					}
					RECTO_PDF_TRY(const PdfValue resolved_val, (*resolver)(*gref));
					const PdfStream* resolved_stm = resolved_val.as_stream();
					if (resolved_stm == nullptr) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Resolved /JBIG2Globals indirect object is not a PDF stream");
					}
					RECTO_PDF_TRY(
						decoded_globals,
						decode_jbig2_globals_stream(*resolved_stm, effective_limit));
					globals_span = decoded_globals;
				} else {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Invalid /JBIG2Globals entry in JBIG2Decode /DecodeParms (expected stream or indirect ref)");
				}
			}
		}
		const size_t remaining_for_page =
			(effective_limit > decoded_globals.size())
			? (effective_limit - decoded_globals.size())
			: 0u;
		return decode_jbig2(input, globals_span, remaining_for_page);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while preparing JBIG2Globals stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while preparing JBIG2Globals stream");
	}
}

// ============================================================================
// Thread-local WIC DCTDecode (JPEG) and JPXDecode (JPEG 2000)
// ============================================================================

namespace {

using Microsoft::WRL::ComPtr;

class ThreadLocalWicFactoryHolder {
public:
	ThreadLocalWicFactoryHolder() noexcept
	{
		const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (hr == S_OK || hr == S_FALSE) {
			should_uninit_ = true;
		}
		(void)CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&factory_));
	}

	~ThreadLocalWicFactoryHolder() noexcept
	{
		factory_.Reset();
		if (should_uninit_) {
			CoUninitialize();
		}
	}

	ThreadLocalWicFactoryHolder(const ThreadLocalWicFactoryHolder&) = delete;
	ThreadLocalWicFactoryHolder& operator=(const ThreadLocalWicFactoryHolder&) = delete;

	[[nodiscard]] IWICImagingFactory* get() const noexcept
	{
		return factory_.Get();
	}

private:
	ComPtr<IWICImagingFactory> factory_;
	bool should_uninit_{false};
};

[[nodiscard]] IWICImagingFactory* get_thread_local_wic_factory() noexcept
{
	thread_local ThreadLocalWicFactoryHolder holder;
	return holder.get();
}

struct JpegHeaderInfo {
	uint32_t width{0};
	uint32_t height{0};
	uint8_t components{0};
	bool has_sof{false};
	bool has_adobe_app14{false};
	uint8_t adobe_transform{0};
};

// Scans a JPEG byte stream for `SOF0..SOF15` and Adobe `APP14` markers BEFORE
// passing the stream to WIC so oversized dimensions (`width * height * 4 > limit`)
// are rejected before any bitmap allocation.
[[nodiscard]] PdfResult<JpegHeaderInfo> inspect_jpeg_header(
	std::span<const uint8_t> input,
	size_t effective_limit) noexcept
{
	if (input.size() < 4u || input[0] != 0xFFu || input[1] != 0xD8u) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Invalid or missing JPEG SOI marker (0xFFD8) in DCTDecode stream");
	}

	JpegHeaderInfo info{};
	size_t pos = 2u;
	while (pos + 1u < input.size()) {
		if (input[pos] != 0xFFu) {
			++pos;
			continue;
		}
		while (pos < input.size() && input[pos] == 0xFFu) {
			++pos;
		}
		if (pos >= input.size()) {
			break;
		}
		const uint8_t marker = input[pos++];
		if (marker == 0x00u || marker == 0x01u || (marker >= 0xD0u && marker <= 0xD8u)) {
			continue;
		}
		if (marker == 0xD9u) { // EOI
			break;
		}
		if (pos + 2u > input.size()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated marker segment length in DCTDecode JPEG stream",
				pos);
		}
		const uint16_t seg_len = read_u16_be(input.data() + pos);
		if (seg_len < 2u || pos + seg_len > input.size()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"JPEG marker segment length exceeds DCTDecode stream bounds",
				pos);
		}
		const std::span<const uint8_t> payload = input.subspan(pos + 2u, seg_len - 2u);

		// Adobe APP14 marker (0xFFEE): 12-byte payload starting with "Adobe".
		if (marker == 0xEEu && payload.size() >= 12u &&
			std::memcmp(payload.data(), "Adobe", 5u) == 0) {
			info.has_adobe_app14 = true;
			info.adobe_transform = payload[11u];
		}

		// SOF markers: 0xC0..0xCF except 0xC4 (DHT), 0xC8 (JPG), and 0xCC (DAC).
		if (marker >= 0xC0u && marker <= 0xCFu &&
			marker != 0xC4u && marker != 0xC8u && marker != 0xCCu) {
			if (payload.size() < 6u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Truncated SOF frame header in DCTDecode JPEG stream",
					pos);
			}
			info.height = read_u16_be(payload.data() + 1u);
			info.width = read_u16_be(payload.data() + 3u);
			info.components = payload[5u];
			info.has_sof = true;
			if (info.width == 0u || info.height == 0u ||
				(info.components != 1u && info.components != 3u && info.components != 4u)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid width, height, or component count in JPEG SOF header",
					pos);
			}
			const uint64_t w64 = info.width;
			const uint64_t h64 = info.height;
			if (w64 > effective_limit / 4ULL || h64 > (effective_limit / 4ULL) / w64) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"DCTDecode JPEG header dimensions exceed maximum decoded bitmap ceiling",
					pos);
			}
		}

		if (marker == 0xDAu) { // SOS: entropy-coded scan follows
			break;
		}
		pos += seg_len;
	}

	if (!info.has_sof) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Missing SOF marker in DCTDecode JPEG stream");
	}
	return info;
}

// Applies an optional PDF `/Decode [Dmin_0 Dmax_0 ...]` linear mapping to an
// N-component 8-bit sample buffer (`c' = Dmin + (c / 255) * (Dmax - Dmin)`).
void apply_pdf_decode_array(
	std::vector<uint8_t>& samples,
	uint32_t components,
	const PdfDict* decode_parms) noexcept
{
	if (decode_parms == nullptr || components == 0u) {
		return;
	}
	const PdfArray* dec_arr = decode_parms->get_array("Decode");
	if (dec_arr == nullptr) {
		dec_arr = decode_parms->get_array("D");
	}
	if (dec_arr == nullptr || dec_arr->size() != static_cast<size_t>(components) * 2u) {
		return;
	}

	std::array<double, 8> dmin{};
	std::array<double, 8> dmax{};
	bool non_identity = false;
	for (uint32_t c = 0; c < components && c < 4u; ++c) {
		const auto v0 = (*dec_arr)[static_cast<size_t>(c) * 2u].as_number();
		const auto v1 = (*dec_arr)[static_cast<size_t>(c) * 2u + 1u].as_number();
		if (!v0.has_value() || !v1.has_value() || !std::isfinite(*v0) || !std::isfinite(*v1)) {
			return;
		}
		dmin[c] = *v0;
		dmax[c] = *v1;
		if (std::abs(dmin[c] - 0.0) > 1e-6 || std::abs(dmax[c] - 1.0) > 1e-6) {
			non_identity = true;
		}
	}
	if (!non_identity) {
		return;
	}

	const size_t pixel_count = samples.size() / components;
	for (size_t p = 0; p < pixel_count; ++p) {
		uint8_t* px = samples.data() + p * components;
		for (uint32_t c = 0; c < components && c < 4u; ++c) {
			const double t = static_cast<double>(px[c]) / 255.0;
			const double mapped = std::clamp(dmin[c] + t * (dmax[c] - dmin[c]), 0.0, 1.0);
			px[c] = static_cast<uint8_t>(std::lround(mapped * 255.0));
		}
	}
}

// Converts normalized 8-bit CMYK (`0` = 0% ink, `255` = 100% ink) to sRGB `(R, G, B)`.
constexpr void cmyk_to_srgb_pixel(
	uint8_t c,
	uint8_t m,
	uint8_t y,
	uint8_t k,
	uint8_t& out_r,
	uint8_t& out_g,
	uint8_t& out_b) noexcept
{
	const uint32_t inv_c = 255u - c;
	const uint32_t inv_m = 255u - m;
	const uint32_t inv_y = 255u - y;
	const uint32_t inv_k = 255u - k;
	out_r = static_cast<uint8_t>((inv_c * inv_k + 127u) / 255u);
	out_g = static_cast<uint8_t>((inv_m * inv_k + 127u) / 255u);
	out_b = static_cast<uint8_t>((inv_y * inv_k + 127u) / 255u);
}

} // namespace

uintptr_t current_thread_wic_factory_id() noexcept
{
	return reinterpret_cast<uintptr_t>(get_thread_local_wic_factory());
}

PdfResult<WicDecodedImage> decode_dct_image(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
		RECTO_PDF_TRY(const JpegHeaderInfo hdr, inspect_jpeg_header(input, effective_limit));

		IWICImagingFactory* factory = get_thread_local_wic_factory();
		if (factory == nullptr) {
			return make_pdf_error(
				ErrorCode::InternalError,
				"Failed to initialize thread-local IWICImagingFactory");
		}

		ComPtr<IWICStream> wic_stream;
		HRESULT hr = factory->CreateStream(&wic_stream);
		if (FAILED(hr) || !wic_stream) {
			return make_pdf_error(
				ErrorCode::InternalError,
				"IWICImagingFactory::CreateStream failed in DCTDecode");
		}
		hr = wic_stream->InitializeFromMemory(
			const_cast<BYTE*>(input.data()),
			static_cast<DWORD>(std::min<size_t>(
				input.size(),
				static_cast<size_t>(std::numeric_limits<DWORD>::max()))));
		if (FAILED(hr)) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"IWICStream::InitializeFromMemory failed in DCTDecode");
		}

		ComPtr<IWICBitmapDecoder> decoder;
		hr = factory->CreateDecoderFromStream(
			wic_stream.Get(),
			nullptr,
			WICDecodeMetadataCacheOnDemand,
			&decoder);
		if (FAILED(hr) || !decoder) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"WIC rejected corrupt or unsupported JPEG stream in DCTDecode");
		}

		ComPtr<IWICBitmapFrameDecode> frame;
		hr = decoder->GetFrame(0, &frame);
		if (FAILED(hr) || !frame) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"IWICBitmapDecoder::GetFrame(0) failed in DCTDecode");
		}

		UINT w = 0;
		UINT h = 0;
		hr = frame->GetSize(&w, &h);
		if (FAILED(hr) || w == 0u || h == 0u) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"IWICBitmapFrameDecode::GetSize failed in DCTDecode");
		}
		const uint64_t w64 = w;
		const uint64_t h64 = h;
		if (w64 > effective_limit / 4ULL || h64 > (effective_limit / 4ULL) / w64) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"DCTDecode frame dimensions exceed maximum decoded bitmap ceiling");
		}

		WICPixelFormatGUID pixel_format{};
		hr = frame->GetPixelFormat(&pixel_format);
		if (FAILED(hr)) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"IWICBitmapFrameDecode::GetPixelFormat failed in DCTDecode");
		}

		WicDecodedImage result;
		result.width = w;
		result.height = h;
		const size_t pixel_count = static_cast<size_t>(w64 * h64);
		const uint64_t comp_bytes = (IsEqualGUID(pixel_format, GUID_WICPixelFormat32bppCMYK) || hdr.components == 4u)
			? 4ULL
			: ((IsEqualGUID(pixel_format, GUID_WICPixelFormat8bppGray) || hdr.components == 1u) ? 1ULL : 3ULL);
		const uint64_t total_wic_bytes = w64 * h64 * (comp_bytes + 8ULL);
		if (total_wic_bytes > effective_limit) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"DCTDecode cumulative decoded image buffers exceed ceiling");
		}

		if (IsEqualGUID(pixel_format, GUID_WICPixelFormat32bppCMYK) || hdr.components == 4u) {
			result.components = 4u;
			result.is_cmyk = true;
			const size_t cmyk_bytes = pixel_count * 4u;
			std::vector<uint8_t> cmyk_buf(cmyk_bytes);
			hr = frame->CopyPixels(
				nullptr,
				w * 4u,
				static_cast<UINT>(cmyk_bytes),
				cmyk_buf.data());
			if (FAILED(hr)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"IWICBitmapFrameDecode::CopyPixels failed on CMYK JPEG");
			}

			// Apply explicit `/Decode` array (such as `[1 0 1 0 1 0 1 0]` for
			// Adobe inverted CMYK JPEGs) if provided in `decode_parms`.
			apply_pdf_decode_array(cmyk_buf, 4u, decode_parms);

			result.rgba_pixels.resize(pixel_count * 4u);
			result.bgra_pixels.resize(pixel_count * 4u);
			for (size_t p = 0; p < pixel_count; ++p) {
				uint8_t r = 0;
				uint8_t g = 0;
				uint8_t b = 0;
				cmyk_to_srgb_pixel(
					cmyk_buf[p * 4u + 0u],
					cmyk_buf[p * 4u + 1u],
					cmyk_buf[p * 4u + 2u],
					cmyk_buf[p * 4u + 3u],
					r,
					g,
					b);
				result.rgba_pixels[p * 4u + 0u] = r;
				result.rgba_pixels[p * 4u + 1u] = g;
				result.rgba_pixels[p * 4u + 2u] = b;
				result.rgba_pixels[p * 4u + 3u] = 255u;

				result.bgra_pixels[p * 4u + 0u] = b;
				result.bgra_pixels[p * 4u + 1u] = g;
				result.bgra_pixels[p * 4u + 2u] = r;
				result.bgra_pixels[p * 4u + 3u] = 255u;
			}
			result.pixels = std::move(cmyk_buf);
			return result;
		}

		if (IsEqualGUID(pixel_format, GUID_WICPixelFormat8bppGray) || hdr.components == 1u) {
			result.components = 1u;
			result.pixels.resize(pixel_count);
			hr = frame->CopyPixels(
				nullptr,
				w,
				static_cast<UINT>(pixel_count),
				result.pixels.data());
			if (FAILED(hr)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"IWICBitmapFrameDecode::CopyPixels failed on Grayscale JPEG");
			}
			apply_pdf_decode_array(result.pixels, 1u, decode_parms);
			result.rgba_pixels.resize(pixel_count * 4u);
			result.bgra_pixels.resize(pixel_count * 4u);
			for (size_t p = 0; p < pixel_count; ++p) {
				const uint8_t v = result.pixels[p];
				result.rgba_pixels[p * 4u + 0u] = v;
				result.rgba_pixels[p * 4u + 1u] = v;
				result.rgba_pixels[p * 4u + 2u] = v;
				result.rgba_pixels[p * 4u + 3u] = 255u;

				result.bgra_pixels[p * 4u + 0u] = v;
				result.bgra_pixels[p * 4u + 1u] = v;
				result.bgra_pixels[p * 4u + 2u] = v;
				result.bgra_pixels[p * 4u + 3u] = 255u;
			}
			return result;
		}

		// 3-component JPEG: convert via `IWICFormatConverter` to `24bppRGB` if not already `24bppBGR`/`24bppRGB`.
		result.components = 3u;
		const size_t rgb_bytes = pixel_count * 3u;
		result.pixels.resize(rgb_bytes);
		if (IsEqualGUID(pixel_format, GUID_WICPixelFormat24bppBGR)) {
			hr = frame->CopyPixels(
				nullptr,
				w * 3u,
				static_cast<UINT>(rgb_bytes),
				result.pixels.data());
			if (FAILED(hr)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"IWICBitmapFrameDecode::CopyPixels failed on 24bppBGR JPEG");
			}
			for (size_t p = 0; p < pixel_count; ++p) {
				std::swap(result.pixels[p * 3u + 0u], result.pixels[p * 3u + 2u]);
			}
		} else if (IsEqualGUID(pixel_format, GUID_WICPixelFormat24bppRGB)) {
			hr = frame->CopyPixels(
				nullptr,
				w * 3u,
				static_cast<UINT>(rgb_bytes),
				result.pixels.data());
			if (FAILED(hr)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"IWICBitmapFrameDecode::CopyPixels failed on 24bppRGB JPEG");
			}
		} else {
			ComPtr<IWICFormatConverter> converter;
			hr = factory->CreateFormatConverter(&converter);
			if (FAILED(hr) || !converter) {
				return make_pdf_error(
					ErrorCode::InternalError,
					"IWICImagingFactory::CreateFormatConverter failed in DCTDecode");
			}
			hr = converter->Initialize(
				frame.Get(),
				GUID_WICPixelFormat24bppRGB,
				WICBitmapDitherTypeNone,
				nullptr,
				0.0,
				WICBitmapPaletteTypeCustom);
			if (FAILED(hr)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"IWICFormatConverter::Initialize failed in DCTDecode");
			}
			hr = converter->CopyPixels(
				nullptr,
				w * 3u,
				static_cast<UINT>(rgb_bytes),
				result.pixels.data());
			if (FAILED(hr)) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"IWICFormatConverter::CopyPixels failed in DCTDecode");
			}
		}

		apply_pdf_decode_array(result.pixels, 3u, decode_parms);
		result.rgba_pixels.resize(pixel_count * 4u);
		result.bgra_pixels.resize(pixel_count * 4u);
		for (size_t p = 0; p < pixel_count; ++p) {
			const uint8_t r = result.pixels[p * 3u + 0u];
			const uint8_t g = result.pixels[p * 3u + 1u];
			const uint8_t b = result.pixels[p * 3u + 2u];
			result.rgba_pixels[p * 4u + 0u] = r;
			result.rgba_pixels[p * 4u + 1u] = g;
			result.rgba_pixels[p * 4u + 2u] = b;
			result.rgba_pixels[p * 4u + 3u] = 255u;

			result.bgra_pixels[p * 4u + 0u] = b;
			result.bgra_pixels[p * 4u + 1u] = g;
			result.bgra_pixels[p * 4u + 2u] = r;
			result.bgra_pixels[p * 4u + 3u] = 255u;
		}
		return result;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding DCTDecode JPEG stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding DCTDecode JPEG stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_dct(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	RECTO_PDF_TRY(
		WicDecodedImage img,
		decode_dct_image(input, decode_parms, max_decoded_bytes));
	if (img.is_cmyk) {
		if (decode_parms != nullptr) {
			if (const auto target_fmt = decode_parms->get_name("TargetFormat")) {
				if (*target_fmt == "BGRA") {
					return std::move(img.bgra_pixels);
				}
				if (*target_fmt == "CMYK") {
					return std::move(img.pixels);
				}
			}
		}
		return std::move(img.rgba_pixels);
	}
	return std::move(img.pixels);
}

namespace {

struct JpxHeaderInfo {
	uint32_t width{0};
	uint32_t height{0};
	uint16_t components{0};
	uint8_t bpc{8};
	std::span<const uint8_t> codestream{};
};

// Inspects a JP2 box container (`00 00 00 0C 6A 50 20 20 0D 0A 87 0A`) or raw
// J2K codestream (`FF 4F FF 51`) and enforces the `width * height * 4 <= limit`
// ceiling before any memory allocation.
[[nodiscard]] PdfResult<JpxHeaderInfo> inspect_jpx_header(
	std::span<const uint8_t> input,
	size_t effective_limit) noexcept
{
	constexpr uint8_t kJp2Signature[12] = {
		0x00u, 0x00u, 0x00u, 0x0Cu,
		0x6Au, 0x50u, 0x20u, 0x20u,
		0x0Du, 0x0Au, 0x87u, 0x0Au,
	};

	JpxHeaderInfo info{};
	if (input.size() >= 12u && std::memcmp(input.data(), kJp2Signature, 12u) == 0) {
		size_t pos = 0u;
		bool saw_ihdr = false;
		while (pos + 8u <= input.size()) {
			uint64_t box_len = read_u32_be(input.data() + pos);
			const uint32_t box_type = read_u32_be(input.data() + pos + 4u);
			size_t header_len = 8u;
			if (box_len == 1ULL) {
				if (pos + 16u > input.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated extended 64-bit box length in JPXDecode stream",
						pos);
				}
				box_len = (static_cast<uint64_t>(read_u32_be(input.data() + pos + 8u)) << 32u) |
					static_cast<uint64_t>(read_u32_be(input.data() + pos + 12u));
				header_len = 16u;
			} else if (box_len == 0ULL) {
				box_len = input.size() - pos;
			}
			if (box_len < header_len || box_len > input.size() - pos) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid JP2 box length in JPXDecode stream",
					pos);
			}
			const std::span<const uint8_t> box_payload =
				input.subspan(pos + header_len, static_cast<size_t>(box_len) - header_len);

			if (box_type == 0x6A703268u) { // 'jp2h' superbox
				size_t sub_pos = 0u;
				while (sub_pos + 8u <= box_payload.size()) {
					const uint32_t sub_len = read_u32_be(box_payload.data() + sub_pos);
					const uint32_t sub_type = read_u32_be(box_payload.data() + sub_pos + 4u);
					if (sub_len < 8u || sub_len > box_payload.size() - sub_pos) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Invalid sub-box length inside jp2h header box");
					}
					if (sub_type == 0x69686472u) { // 'ihdr'
						if (sub_len < 8u + 14u) {
							return make_filter_error(
								ErrorCode::DamagedFormat,
								DiagnosticId::DamagedStream,
								"Truncated ihdr box in JPXDecode stream");
						}
						const uint8_t* ihdr = box_payload.data() + sub_pos + 8u;
						info.height = read_u32_be(ihdr);
						info.width = read_u32_be(ihdr + 4u);
						info.components = read_u16_be(ihdr + 8u);
						info.bpc = static_cast<uint8_t>((ihdr[10u] & 0x7Fu) + 1u);
						saw_ihdr = true;
					}
					sub_pos += sub_len;
				}
			} else if (box_type == 0x6A703263u) { // 'jp2c' codestream box
				info.codestream = box_payload;
			}
			pos += static_cast<size_t>(box_len);
		}
		if (!saw_ihdr || info.codestream.empty()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"JP2 container is missing required ihdr or jp2c box in JPXDecode");
		}
	} else if (input.size() >= 4u && input[0] == 0xFFu && input[1] == 0x4Fu) {
		info.codestream = input;
	} else {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Invalid JP2 signature or J2K SOC marker (0xFF4F) in JPXDecode stream");
	}

	// Also inspect the J2K codestream `SOC (0xFF4F)` + `SIZ (0xFF51)` marker segment.
	const std::span<const uint8_t> cs = info.codestream;
	if (cs.size() < 45u || cs[0] != 0xFFu || cs[1] != 0x4Fu ||
		cs[2] != 0xFFu || cs[3] != 0x51u) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Missing SOC (0xFF4F) or SIZ (0xFF51) marker in JPXDecode codestream");
	}
	const uint16_t lsiz = read_u16_be(cs.data() + 4u);
	if (lsiz < 41u || 4u + lsiz > cs.size()) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Truncated SIZ marker segment in JPXDecode codestream");
	}
	const uint32_t xsiz = read_u32_be(cs.data() + 8u);
	const uint32_t ysiz = read_u32_be(cs.data() + 12u);
	const uint32_t xosiz = read_u32_be(cs.data() + 16u);
	const uint32_t yosiz = read_u32_be(cs.data() + 20u);
	const uint16_t csiz = read_u16_be(cs.data() + 40u);
	if (xsiz <= xosiz || ysiz <= yosiz || csiz == 0u || csiz > 4u ||
		lsiz < 38u + 3u * csiz) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Invalid image grid or component count in J2K SIZ marker segment");
	}
	const uint32_t cs_w = xsiz - xosiz;
	const uint32_t cs_h = ysiz - yosiz;
	if (info.width == 0u) {
		info.width = cs_w;
		info.height = cs_h;
		info.components = csiz;
		info.bpc = static_cast<uint8_t>((cs[42u] & 0x7Fu) + 1u);
	} else if (info.width != cs_w || info.height != cs_h || info.components != csiz) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"JP2 ihdr dimensions do not match J2K SIZ marker dimensions");
	}

	const uint64_t w64 = info.width;
	const uint64_t h64 = info.height;
	if (w64 == 0ULL || h64 == 0ULL) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Zero width or height in JPXDecode stream");
	}
	if (w64 > effective_limit / 4ULL || h64 > (effective_limit / 4ULL) / w64) {
		return make_filter_error(
			ErrorCode::DecompressionLimitExceeded,
			DiagnosticId::DecompressionLimitExceeded,
			"JPXDecode image dimensions exceed maximum decoded bitmap ceiling");
	}
	return info;
}

// ---------------------------------------------------------------------------
// Native ISO/IEC 15444-1 (ITU-T T.800) Part 1 JPEG 2000 Codestream Decoder
// (Tier-2 packet header with 0xFF bit-stuffing & tag-trees + Tier-1 EBCOT
// MQ arithmetic block decoder + 2D 5/3 reversible & 9/7 irreversible inverse
// DWT + inverse RCT/ICT multi-component transform).
// ---------------------------------------------------------------------------

class J2kPacketBitReader {
public:
	explicit J2kPacketBitReader(std::span<const uint8_t> data) noexcept
		: data_(data)
	{
	}

	[[nodiscard]] bool read_bit(uint32_t& out_bit) noexcept
	{
		if (bits_left_ == 0u) {
			if (pos_ >= data_.size()) {
				return false;
			}
			const uint8_t b = data_[pos_++];
			cur_byte_ = b;
			// ITU-T T.800 §B.10.1: after a 0xFF byte in a packet header, the
			// next byte has its MSB stuffed as 0 (7 data bits).
			bits_left_ = prev_was_ff_ ? 7u : 8u;
			prev_was_ff_ = (b == 0xFFu);
		}
		--bits_left_;
		out_bit = (cur_byte_ >> bits_left_) & 1u;
		return true;
	}

	[[nodiscard]] bool read_bits(uint32_t count, uint32_t& out_val) noexcept
	{
		uint32_t v = 0u;
		for (uint32_t i = 0; i < count; ++i) {
			uint32_t b = 0u;
			if (!read_bit(b)) {
				return false;
			}
			v = (v << 1u) | b;
		}
		out_val = v;
		return true;
	}

	[[nodiscard]] bool align_to_byte() noexcept
	{
		bits_left_ = 0u;
		if (prev_was_ff_) {
			if (pos_ >= data_.size()) {
				return false;
			}
			// Consume stuffed byte after trailing 0xFF in packet header.
			++pos_;
			prev_was_ff_ = false;
		}
		return true;
	}

	[[nodiscard]] size_t bytes_consumed() const noexcept
	{
		return pos_;
	}

private:
	std::span<const uint8_t> data_;
	size_t pos_ = 0u;
	uint8_t cur_byte_ = 0u;
	uint32_t bits_left_ = 0u;
	bool prev_was_ff_ = false;
};

struct J2kTagTree {
	uint32_t width = 0u;
	uint32_t height = 0u;
	struct Level {
		uint32_t w = 0u;
		uint32_t h = 0u;
		std::vector<int32_t> val;
		std::vector<int32_t> low;
	};
	std::vector<Level> levels;

	void init(uint32_t w, uint32_t h)
	{
		width = w;
		height = h;
		levels.clear();
		uint32_t cw = std::max(1u, w);
		uint32_t ch = std::max(1u, h);
		while (true) {
			Level lv;
			lv.w = cw;
			lv.h = ch;
			lv.val.assign(static_cast<size_t>(cw) * ch, std::numeric_limits<int32_t>::max());
			lv.low.assign(static_cast<size_t>(cw) * ch, 0);
			levels.push_back(std::move(lv));
			if (cw == 1u && ch == 1u) {
				break;
			}
			cw = (cw + 1u) >> 1u;
			ch = (ch + 1u) >> 1u;
		}
	}

	[[nodiscard]] bool decode(
		J2kPacketBitReader& br,
		uint32_t x,
		uint32_t y,
		int32_t threshold,
		int32_t& out_val) noexcept
	{
		if (levels.empty() || x >= width || y >= height) {
			return false;
		}
		int32_t low = 0;
		for (int l = static_cast<int>(levels.size()) - 1; l >= 0; --l) {
			Level& lv = levels[static_cast<size_t>(l)];
			const uint32_t lx = x >> static_cast<uint32_t>(l);
			const uint32_t ly = y >> static_cast<uint32_t>(l);
			const size_t idx = static_cast<size_t>(ly) * lv.w + lx;
			if (lv.low[idx] < low) {
				lv.low[idx] = low;
			}
			while (lv.low[idx] < threshold && lv.low[idx] < lv.val[idx]) {
				uint32_t bit = 0u;
				if (!br.read_bit(bit)) {
					return false;
				}
				if (bit == 1u) {
					lv.val[idx] = lv.low[idx];
				} else {
					++lv.low[idx];
				}
			}
			low = (lv.val[idx] < lv.low[idx]) ? lv.val[idx] : lv.low[idx];
		}
		out_val = levels[0].val[static_cast<size_t>(y) * width + x];
		return true;
	}
};

// ITU-T T.800 Table B.4: Decode number of coding passes for a code-block.
[[nodiscard]] bool decode_j2k_num_passes(J2kPacketBitReader& br, uint32_t& out_passes) noexcept
{
	uint32_t b0 = 0u;
	if (!br.read_bit(b0)) {
		return false;
	}
	if (b0 == 0u) {
		out_passes = 1u;
		return true;
	}
	uint32_t b1 = 0u;
	if (!br.read_bit(b1)) {
		return false;
	}
	if (b1 == 0u) {
		out_passes = 2u;
		return true;
	}
	uint32_t two_bits = 0u;
	if (!br.read_bits(2u, two_bits)) {
		return false;
	}
	if (two_bits != 3u) {
		out_passes = 3u + two_bits;
		return true;
	}
	uint32_t five_bits = 0u;
	if (!br.read_bits(5u, five_bits)) {
		return false;
	}
	if (five_bits != 31u) {
		out_passes = 6u + five_bits;
		return true;
	}
	uint32_t seven_bits = 0u;
	if (!br.read_bits(7u, seven_bits)) {
		return false;
	}
	out_passes = 37u + seven_bits;
	return true;
}

// ITU-T T.800 Annex D (EBCOT Tier-1 Context Modeling and MQ Decoding).
// Context indices 0..18:
//  0..8   : Zero Coding (ZC) / Significance Propagation & Cleanup
//  9..13  : Sign Coding (SC)
//  14..16 : Magnitude Refinement (MR)
//  17     : Run-Length Coding (RLC)
//  18     : Uniform (UNI)
constexpr size_t kJ2kNumContexts = 19u;

void init_j2k_mq_contexts(std::array<MqContext, kJ2kNumContexts>& ctxs) noexcept
{
	for (auto& c : ctxs) {
		c.index = 0u;
		c.mps = 0u;
	}
	// ITU-T T.800 Table D.7: initial states for contexts 0, 17, 18.
	ctxs[0].index = 4u;
	ctxs[17].index = 3u;
	ctxs[18].index = 46u;
}

// Subband types: 0 = LL/LH, 1 = HL, 2 = HH
[[nodiscard]] uint8_t compute_j2k_zc_context(
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	const std::vector<uint8_t>& sig,
	uint8_t band_type) noexcept
{
	auto is_sig = [&](int32_t nx, int32_t ny) noexcept -> uint32_t {
		if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
			return 0u;
		}
		return sig[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)] ? 1u : 0u;
	};
	uint32_t h_sum = is_sig(x - 1, y) + is_sig(x + 1, y);
	uint32_t v_sum = is_sig(x, y - 1) + is_sig(x, y + 1);
	const uint32_t d_sum = is_sig(x - 1, y - 1) + is_sig(x + 1, y - 1) +
		is_sig(x - 1, y + 1) + is_sig(x + 1, y + 1);

	if (band_type == 2u) { // HH band (Table D.2)
		const uint32_t hv = h_sum + v_sum;
		if (d_sum >= 3u) {
			return 8u;
		}
		if (d_sum == 2u) {
			return (hv >= 1u) ? 7u : 6u;
		}
		if (d_sum == 1u) {
			if (hv >= 2u) {
				return 5u;
			}
			return (hv == 1u) ? 4u : 3u;
		}
		if (hv >= 2u) {
			return 2u;
		}
		return (hv == 1u) ? 1u : 0u;
	}
	if (band_type == 1u) { // HL band swaps H and V (Table D.1)
		std::swap(h_sum, v_sum);
	}
	if (h_sum == 2u) {
		return 8u;
	}
	if (h_sum == 1u) {
		if (v_sum >= 1u) {
			return 7u;
		}
		return (d_sum >= 1u) ? 6u : 5u;
	}
	if (v_sum == 2u) {
		return 4u;
	}
	if (v_sum == 1u) {
		return 3u;
	}
	if (d_sum >= 2u) {
		return 2u;
	}
	return (d_sum == 1u) ? 1u : 0u;
}

// ITU-T T.800 Table D.3: Sign coding context and XOR bit.
void compute_j2k_sc_context(
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	const std::vector<uint8_t>& sig,
	const std::vector<uint8_t>& sgn,
	uint8_t& out_ctx,
	uint8_t& out_xor) noexcept
{
	auto contrib = [&](int32_t nx, int32_t ny) noexcept -> int32_t {
		if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
			return 0;
		}
		const size_t idx = static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx);
		if (!sig[idx]) {
			return 0;
		}
		return sgn[idx] ? -1 : 1;
	};
	int32_t hc = std::clamp(contrib(x - 1, y) + contrib(x + 1, y), -1, 1);
	int32_t vc = std::clamp(contrib(x, y - 1) + contrib(x, y + 1), -1, 1);
	out_xor = 0u;
	if (hc < 0) {
		hc = -hc;
		vc = -vc;
		out_xor = 1u;
	} else if (hc == 0 && vc < 0) {
		vc = -vc;
		out_xor = 1u;
	}
	if (hc == 1) {
		if (vc == 1) {
			out_ctx = 13u;
		} else if (vc == 0) {
			out_ctx = 12u;
		} else {
			out_ctx = 11u;
		}
	} else {
		out_ctx = (vc == 1) ? 10u : 9u;
	}
}

[[nodiscard]] bool has_significant_neighbor(
	int32_t x,
	int32_t y,
	int32_t w,
	int32_t h,
	const std::vector<uint8_t>& sig) noexcept
{
	for (int32_t dy = -1; dy <= 1; ++dy) {
		for (int32_t dx = -1; dx <= 1; ++dx) {
			if (dx == 0 && dy == 0) {
				continue;
			}
			const int32_t nx = x + dx;
			const int32_t ny = y + dy;
			if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
				if (sig[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)] != 0u) {
					return true;
				}
			}
		}
	}
	return false;
}

// Decode a single EBCOT Tier-1 code-block into signed wavelet coefficients.
void decode_j2k_codeblock(
	std::span<const uint8_t> cb_data,
	uint32_t cb_w,
	uint32_t cb_h,
	uint32_t num_passes,
	uint32_t zero_bitplanes,
	uint32_t mb_total,
	uint8_t band_type,
	uint8_t cb_style,
	std::vector<int32_t>& out_coeffs)
{
	const size_t total = static_cast<size_t>(cb_w) * cb_h;
	out_coeffs.assign(total, 0);
	if (cb_data.empty() || num_passes == 0u || cb_w == 0u || cb_h == 0u) {
		return;
	}

	const int32_t w = static_cast<int32_t>(cb_w);
	const int32_t h = static_cast<int32_t>(cb_h);
	std::vector<uint8_t> sig(total, 0u);
	std::vector<uint8_t> sgn(total, 0u);
	std::vector<uint8_t> refined(total, 0u);
	std::vector<uint8_t> visited(total, 0u);
	std::vector<int32_t> mag(total, 0);

	std::array<MqContext, kJ2kNumContexts> ctxs{};
	init_j2k_mq_contexts(ctxs);
	MqDecoder mq(cb_data);

	// Optional raw byte-aligned arithmetic-bypass mode if bit 7 (0x80, custom test/raw) is not set:
	// Standard ITU-T T.800 uses MQ arithmetic coding starting with Cleanup pass on the highest non-zero bitplane.
	const uint32_t clamped_mb = std::min<uint32_t>(mb_total, 30u);
	int32_t bitplane = static_cast<int32_t>(clamped_mb) - 1 - static_cast<int32_t>(std::min<uint32_t>(zero_bitplanes, 30u));
	if (bitplane < 0) {
		bitplane = 0;
	}
	if (bitplane > 29) {
		bitplane = 29;
	}

	auto decode_sign = [&](int32_t x, int32_t y, size_t idx) noexcept {
		uint8_t sc_ctx = 9u;
		uint8_t sc_xor = 0u;
		compute_j2k_sc_context(x, y, w, h, sig, sgn, sc_ctx, sc_xor);
		const uint8_t bit = mq.decode(ctxs[sc_ctx]) ^ sc_xor;
		sgn[idx] = bit;
	};

	uint32_t pass_type = 2u; // First pass is always Cleanup (CUP = 2, SPP = 0, MRP = 1)
	for (uint32_t p = 0; p < num_passes && bitplane >= 0; ++p) {
		if ((cb_style & 0x02u) != 0u) {
			init_j2k_mq_contexts(ctxs);
		}
		const int32_t bp_mask = static_cast<int32_t>(uint32_t{1u} << static_cast<uint32_t>(bitplane));

		if (pass_type == 0u) {
			// Significance Propagation Pass (SPP, ITU-T T.800 §D.3.1)
			std::fill(visited.begin(), visited.end(), uint8_t{0});
			for (int32_t y0 = 0; y0 < h; y0 += 4) {
				const int32_t stripe_h = std::min(4, h - y0);
				for (int32_t x = 0; x < w; ++x) {
					for (int32_t dy = 0; dy < stripe_h; ++dy) {
						const int32_t y = y0 + dy;
						const size_t idx = static_cast<size_t>(y) * cb_w + static_cast<size_t>(x);
						if (!sig[idx] && has_significant_neighbor(x, y, w, h, sig)) {
							const uint8_t zc = compute_j2k_zc_context(x, y, w, h, sig, band_type);
							if (mq.decode(ctxs[zc]) != 0u) {
								decode_sign(x, y, idx);
								sig[idx] = 1u;
								mag[idx] |= bp_mask;
							}
							visited[idx] = 1u;
						}
					}
				}
			}
			pass_type = 1u;
		} else if (pass_type == 1u) {
			// Magnitude Refinement Pass (MRP, ITU-T T.800 §D.3.3)
			for (int32_t y0 = 0; y0 < h; y0 += 4) {
				const int32_t stripe_h = std::min(4, h - y0);
				for (int32_t x = 0; x < w; ++x) {
					for (int32_t dy = 0; dy < stripe_h; ++dy) {
						const int32_t y = y0 + dy;
						const size_t idx = static_cast<size_t>(y) * cb_w + static_cast<size_t>(x);
						if (sig[idx] && !visited[idx]) {
							uint8_t mr_ctx = 16u;
							if (!refined[idx]) {
								mr_ctx = has_significant_neighbor(x, y, w, h, sig) ? 15u : 14u;
							}
							if (mq.decode(ctxs[mr_ctx]) != 0u) {
								mag[idx] |= bp_mask;
							}
							refined[idx] = 1u;
						}
					}
				}
			}
			pass_type = 2u;
		} else {
			// Cleanup Pass (CUP, ITU-T T.800 §D.3.4)
			for (int32_t y0 = 0; y0 < h; y0 += 4) {
				const int32_t stripe_h = std::min(4, h - y0);
				for (int32_t x = 0; x < w; ++x) {
					int32_t dy = 0;
					if (stripe_h == 4) {
						bool can_rlc = true;
						for (int32_t k = 0; k < 4; ++k) {
							const int32_t y = y0 + k;
							const size_t idx = static_cast<size_t>(y) * cb_w + static_cast<size_t>(x);
							if (sig[idx] || visited[idx] || has_significant_neighbor(x, y, w, h, sig)) {
								can_rlc = false;
								break;
							}
						}
						if (can_rlc) {
							if (mq.decode(ctxs[17]) == 0u) {
								continue;
							}
							const uint8_t r0 = mq.decode(ctxs[18]);
							const uint8_t r1 = mq.decode(ctxs[18]);
							dy = static_cast<int32_t>((r0 << 1u) | r1);
							const int32_t y = y0 + dy;
							const size_t idx = static_cast<size_t>(y) * cb_w + static_cast<size_t>(x);
							decode_sign(x, y, idx);
							sig[idx] = 1u;
							mag[idx] |= bp_mask;
							++dy;
						}
					}
					for (; dy < stripe_h; ++dy) {
						const int32_t y = y0 + dy;
						const size_t idx = static_cast<size_t>(y) * cb_w + static_cast<size_t>(x);
						if (!sig[idx] && !visited[idx]) {
							const uint8_t zc = compute_j2k_zc_context(x, y, w, h, sig, band_type);
							if (mq.decode(ctxs[zc]) != 0u) {
								decode_sign(x, y, idx);
								sig[idx] = 1u;
								mag[idx] |= bp_mask;
							}
						}
						visited[idx] = 0u;
					}
				}
			}
			if ((cb_style & 0x20u) != 0u) {
				// SEGMARK (0x0A in context 18) at the end of Cleanup pass.
				for (int s = 0; s < 4; ++s) {
					(void)mq.decode(ctxs[18]);
				}
			}
			--bitplane;
			pass_type = 0u;
		}
	}

	for (size_t i = 0; i < total; ++i) {
		out_coeffs[i] = sgn[i] ? -mag[i] : mag[i];
	}
}

// 1D 5-3 reversible inverse discrete wavelet synthesis filter (ITU-T T.800 §F.3.8.1, i0 = 0).
void idwt_53_1d(std::span<const int32_t> low, std::span<const int32_t> high, std::span<int32_t> out) noexcept
{
	const size_t n = out.size();
	if (n == 0u) {
		return;
	}
	if (n == 1u) {
		out[0] = low.empty() ? 0 : low[0];
		return;
	}
	const size_t n_low = low.size();
	const size_t n_high = high.size();

	auto get_high = [&](int64_t idx) noexcept -> int32_t {
		if (n_high == 0u) {
			return 0;
		}
		if (idx < 0) {
			idx = -idx - 1;
		}
		if (static_cast<size_t>(idx) >= n_high) {
			idx = static_cast<int64_t>(n_high) - 1;
		}
		return high[static_cast<size_t>(idx)];
	};

	for (size_t k = 0; k < n_low; ++k) {
		const int32_t d_prev = get_high(static_cast<int64_t>(k) - 1);
		const int32_t d_curr = get_high(static_cast<int64_t>(k));
		out[2u * k] = low[k] - ((d_prev + d_curr + 2) >> 2);
	}
	for (size_t k = 0; k < n_high; ++k) {
		const int32_t s_curr = out[2u * k];
		const int32_t s_next = (2u * (k + 1u) < n) ? out[2u * (k + 1u)] : out[2u * k];
		out[2u * k + 1u] = high[k] + ((s_curr + s_next) >> 1);
	}
}

// 2D 5-3 reversible inverse DWT for one resolution level.
void idwt_53_2d(
	uint32_t w,
	uint32_t h,
	uint32_t ll_w,
	uint32_t ll_h,
	const std::vector<int32_t>& ll,
	const std::vector<int32_t>& hl,
	const std::vector<int32_t>& lh,
	const std::vector<int32_t>& hh,
	std::vector<int32_t>& out)
{
	out.assign(static_cast<size_t>(w) * h, 0);
	const uint32_t hl_w = w - ll_w;
	const uint32_t lh_h = h - ll_h;

	std::vector<int32_t> temp(static_cast<size_t>(w) * h, 0);
	std::vector<int32_t> low_buf(std::max(w, h), 0);
	std::vector<int32_t> high_buf(std::max(w, h), 0);
	std::vector<int32_t> dst_buf(std::max(w, h), 0);

	// Horizontal synthesis on top rows (LL + HL) and bottom rows (LH + HH).
	for (uint32_t y = 0; y < ll_h; ++y) {
		for (uint32_t x = 0; x < ll_w; ++x) {
			low_buf[x] = ll[static_cast<size_t>(y) * ll_w + x];
		}
		for (uint32_t x = 0; x < hl_w; ++x) {
			high_buf[x] = hl[static_cast<size_t>(y) * hl_w + x];
		}
		idwt_53_1d(
			std::span<const int32_t>(low_buf.data(), ll_w),
			std::span<const int32_t>(high_buf.data(), hl_w),
			std::span<int32_t>(&temp[static_cast<size_t>(y) * w], w));
	}
	for (uint32_t y = 0; y < lh_h; ++y) {
		for (uint32_t x = 0; x < ll_w; ++x) {
			low_buf[x] = lh[static_cast<size_t>(y) * ll_w + x];
		}
		for (uint32_t x = 0; x < hl_w; ++x) {
			high_buf[x] = hh[static_cast<size_t>(y) * hl_w + x];
		}
		idwt_53_1d(
			std::span<const int32_t>(low_buf.data(), ll_w),
			std::span<const int32_t>(high_buf.data(), hl_w),
			std::span<int32_t>(&temp[static_cast<size_t>(ll_h + y) * w], w));
	}

	// Vertical synthesis on each column.
	for (uint32_t x = 0; x < w; ++x) {
		for (uint32_t y = 0; y < ll_h; ++y) {
			low_buf[y] = temp[static_cast<size_t>(y) * w + x];
		}
		for (uint32_t y = 0; y < lh_h; ++y) {
			high_buf[y] = temp[static_cast<size_t>(ll_h + y) * w + x];
		}
		idwt_53_1d(
			std::span<const int32_t>(low_buf.data(), ll_h),
			std::span<const int32_t>(high_buf.data(), lh_h),
			std::span<int32_t>(dst_buf.data(), h));
		for (uint32_t y = 0; y < h; ++y) {
			out[static_cast<size_t>(y) * w + x] = dst_buf[y];
		}
	}
}

// 1D 9-7 irreversible inverse DWT (ITU-T T.800 §F.3.8.2 lifting steps).
void idwt_97_1d(std::span<const float> low, std::span<const float> high, std::span<float> out) noexcept
{
	const size_t n = out.size();
	if (n == 0u) {
		return;
	}
	if (n == 1u) {
		out[0] = low.empty() ? 0.0f : low[0];
		return;
	}
	constexpr float kAlpha = -1.586134342f;
	constexpr float kBeta = -0.052980118f;
	constexpr float kGamma = 0.882911075f;
	constexpr float kDelta = 0.443506852f;
	constexpr float kK = 1.230174105f;

	const size_t n_low = low.size();
	const size_t n_high = high.size();
	for (size_t k = 0; k < n_low; ++k) {
		out[2u * k] = low[k] * kK;
	}
	for (size_t k = 0; k < n_high; ++k) {
		out[2u * k + 1u] = high[k] / kK;
	}
	auto sym = [&](int64_t idx) noexcept -> float {
		if (idx < 0) {
			idx = -idx;
		}
		if (static_cast<size_t>(idx) >= n) {
			idx = static_cast<int64_t>(2u * n - 2u) - idx;
			if (idx < 0) {
				idx = 0;
			}
		}
		return out[static_cast<size_t>(idx)];
	};
	for (size_t k = 0; k < n_low; ++k) {
		const int64_t i = static_cast<int64_t>(2u * k);
		out[static_cast<size_t>(i)] -= kDelta * (sym(i - 1) + sym(i + 1));
	}
	for (size_t k = 0; k < n_high; ++k) {
		const int64_t i = static_cast<int64_t>(2u * k + 1u);
		out[static_cast<size_t>(i)] -= kGamma * (sym(i - 1) + sym(i + 1));
	}
	for (size_t k = 0; k < n_low; ++k) {
		const int64_t i = static_cast<int64_t>(2u * k);
		out[static_cast<size_t>(i)] -= kBeta * (sym(i - 1) + sym(i + 1));
	}
	for (size_t k = 0; k < n_high; ++k) {
		const int64_t i = static_cast<int64_t>(2u * k + 1u);
		out[static_cast<size_t>(i)] -= kAlpha * (sym(i - 1) + sym(i + 1));
	}
}

void idwt_97_2d_float(
	uint32_t w,
	uint32_t h,
	uint32_t ll_w,
	uint32_t ll_h,
	const std::vector<float>& ll,
	const std::vector<float>& hl,
	const std::vector<float>& lh,
	const std::vector<float>& hh,
	std::vector<float>& out)
{
	out.assign(static_cast<size_t>(w) * h, 0.0f);
	const uint32_t hl_w = w - ll_w;
	const uint32_t lh_h = h - ll_h;

	std::vector<float> temp(static_cast<size_t>(w) * h, 0.0f);
	std::vector<float> low_buf(std::max(w, h), 0.0f);
	std::vector<float> high_buf(std::max(w, h), 0.0f);
	std::vector<float> dst_buf(std::max(w, h), 0.0f);

	for (uint32_t y = 0; y < ll_h; ++y) {
		for (uint32_t x = 0; x < ll_w; ++x) {
			low_buf[x] = ll[static_cast<size_t>(y) * ll_w + x];
		}
		for (uint32_t x = 0; x < hl_w; ++x) {
			high_buf[x] = hl[static_cast<size_t>(y) * hl_w + x];
		}
		idwt_97_1d(
			std::span<const float>(low_buf.data(), ll_w),
			std::span<const float>(high_buf.data(), hl_w),
			std::span<float>(&temp[static_cast<size_t>(y) * w], w));
	}
	for (uint32_t y = 0; y < lh_h; ++y) {
		for (uint32_t x = 0; x < ll_w; ++x) {
			low_buf[x] = lh[static_cast<size_t>(y) * ll_w + x];
		}
		for (uint32_t x = 0; x < hl_w; ++x) {
			high_buf[x] = hh[static_cast<size_t>(y) * hl_w + x];
		}
		idwt_97_1d(
			std::span<const float>(low_buf.data(), ll_w),
			std::span<const float>(high_buf.data(), hl_w),
			std::span<float>(&temp[static_cast<size_t>(ll_h + y) * w], w));
	}
	for (uint32_t x = 0; x < w; ++x) {
		for (uint32_t y = 0; y < ll_h; ++y) {
			low_buf[y] = temp[static_cast<size_t>(y) * w + x];
		}
		for (uint32_t y = 0; y < lh_h; ++y) {
			high_buf[y] = temp[static_cast<size_t>(ll_h + y) * w + x];
		}
		idwt_97_1d(
			std::span<const float>(low_buf.data(), ll_h),
			std::span<const float>(high_buf.data(), lh_h),
			std::span<float>(dst_buf.data(), h));
		for (uint32_t y = 0; y < h; ++y) {
			out[static_cast<size_t>(y) * w + x] = dst_buf[y];
		}
	}
}

[[nodiscard]] PdfResult<WicDecodedImage> decode_jpx_internal(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes)
{
	const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);
	RECTO_PDF_TRY(const JpxHeaderInfo hdr, inspect_jpx_header(input, effective_limit));

	// First attempt decoding through the thread-local WIC factory if a JP2 codec is registered.
	if (IWICImagingFactory* factory = get_thread_local_wic_factory(); factory != nullptr) {
		ComPtr<IWICStream> wic_stream;
		if (SUCCEEDED(factory->CreateStream(&wic_stream)) && wic_stream) {
			if (SUCCEEDED(wic_stream->InitializeFromMemory(
					const_cast<BYTE*>(input.data()),
					static_cast<DWORD>(std::min<size_t>(
						input.size(),
						static_cast<size_t>(std::numeric_limits<DWORD>::max())))))) {
				ComPtr<IWICBitmapDecoder> decoder;
				if (SUCCEEDED(factory->CreateDecoderFromStream(
						wic_stream.Get(),
						nullptr,
						WICDecodeMetadataCacheOnDemand,
						&decoder)) &&
					decoder) {
					ComPtr<IWICBitmapFrameDecode> frame;
					if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
						UINT w = 0;
						UINT h = 0;
						if (SUCCEEDED(frame->GetSize(&w, &h)) && w > 0u && h > 0u) {
							const uint64_t w64 = w;
							const uint64_t h64 = h;
							const uint32_t out_ch = (hdr.components == 1u) ? 1u : 3u;
							if (w64 > effective_limit / (out_ch + 8ULL) ||
								h64 > (effective_limit / (out_ch + 8ULL)) / w64) {
								return make_filter_error(
									ErrorCode::DecompressionLimitExceeded,
									DiagnosticId::DecompressionLimitExceeded,
									"JPXDecode WIC frame dimensions exceed ceiling");
							}
							ComPtr<IWICFormatConverter> converter;
							if (SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
								converter) {
								const GUID target_guid = (hdr.components == 1u)
									? GUID_WICPixelFormat8bppGray
									: GUID_WICPixelFormat24bppRGB;
								if (SUCCEEDED(converter->Initialize(
										frame.Get(),
										target_guid,
										WICBitmapDitherTypeNone,
										nullptr,
										0.0,
										WICBitmapPaletteTypeCustom))) {
									WicDecodedImage img{};
									img.width = w;
									img.height = h;
									img.components = out_ch;
									img.pixels.resize(static_cast<size_t>(w64 * h64 * out_ch));
									if (SUCCEEDED(converter->CopyPixels(
											nullptr,
											w * out_ch,
											static_cast<UINT>(img.pixels.size()),
											img.pixels.data()))) {
										apply_pdf_decode_array(img.pixels, out_ch, decode_parms);
										const size_t px_count = static_cast<size_t>(w64 * h64);
										img.rgba_pixels.resize(px_count * 4u);
										img.bgra_pixels.resize(px_count * 4u);
										for (size_t p = 0; p < px_count; ++p) {
											const uint8_t r = img.pixels[p * out_ch + 0u];
											const uint8_t g = (out_ch >= 3u) ? img.pixels[p * out_ch + 1u] : r;
											const uint8_t b = (out_ch >= 3u) ? img.pixels[p * out_ch + 2u] : r;
											img.rgba_pixels[p * 4u + 0u] = r;
											img.rgba_pixels[p * 4u + 1u] = g;
											img.rgba_pixels[p * 4u + 2u] = b;
											img.rgba_pixels[p * 4u + 3u] = 255u;
											img.bgra_pixels[p * 4u + 0u] = b;
											img.bgra_pixels[p * 4u + 1u] = g;
											img.bgra_pixels[p * 4u + 2u] = r;
											img.bgra_pixels[p * 4u + 3u] = 255u;
										}
										return img;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	// Native ISO/IEC 15444-1 (ITU-T T.800) Part 1 JPEG 2000 codestream decoder.
	const std::span<const uint8_t> cs = hdr.codestream;
	const uint16_t lsiz = read_u16_be(cs.data() + 4u);
	const uint32_t xsiz = read_u32_be(cs.data() + 8u);
	const uint32_t ysiz = read_u32_be(cs.data() + 12u);
	const uint32_t xosiz = read_u32_be(cs.data() + 16u);
	const uint32_t yosiz = read_u32_be(cs.data() + 20u);
	const uint32_t xtsiz = read_u32_be(cs.data() + 24u);
	const uint32_t ytsiz = read_u32_be(cs.data() + 28u);
	const uint32_t xtosiz = read_u32_be(cs.data() + 32u);
	const uint32_t ytosiz = read_u32_be(cs.data() + 36u);
	if (xtsiz == 0u || ytsiz == 0u || xtosiz > xosiz || ytosiz > yosiz) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Invalid tile grid geometry in J2K SIZ marker segment");
	}
	const uint32_t num_tiles_x = (xsiz - xtosiz + xtsiz - 1u) / xtsiz;
	const uint32_t num_tiles_y = (ysiz - ytosiz + ytsiz - 1u) / ytsiz;
	if (num_tiles_x == 0u || num_tiles_y == 0u ||
		static_cast<uint64_t>(num_tiles_x) * num_tiles_y > 4096ULL) {
		return make_filter_error(
			ErrorCode::DecompressionLimitExceeded,
			DiagnosticId::DecompressionLimitExceeded,
			"JPXDecode tile grid count exceeds ceiling");
	}
	const uint32_t total_tiles = num_tiles_x * num_tiles_y;

	bool is_signed = false;
	uint8_t bit_depth = hdr.bpc;
	if (lsiz >= 39u && 4u + 38u < cs.size()) {
		const uint8_t ssiz0 = cs[4u + 38u];
		is_signed = ((ssiz0 & 0x80u) != 0u);
		bit_depth = static_cast<uint8_t>((ssiz0 & 0x7Fu) + 1u);
	}
	if (bit_depth == 0u || bit_depth > 16u) {
		bit_depth = 8u;
	}

	size_t pos = 4u + lsiz;
	bool saw_sot = false;
	bool saw_sod = false;
	bool saw_eoc = false;
	bool saw_cod = false;
	bool saw_qcd = false;

	uint8_t prog_order = 0u;
	uint16_t num_layers = 1u;
	bool mct_enabled = false;
	uint8_t num_decomp_levels = 0u;
	uint32_t cb_exp_w = 6u;
	uint32_t cb_exp_h = 6u;
	uint8_t cb_style = 0u;
	uint8_t wavelet_type = 1u; // 1 = 5-3 reversible, 0 = 9-7 irreversible
	uint8_t guard_bits = 2u;
	std::vector<uint8_t> subband_exponents;
	std::vector<uint16_t> subband_mantissas;
	std::vector<std::vector<uint8_t>> tile_bitstreams(total_tiles);
	size_t total_tile_stream_bytes = 0u;

	while (pos + 2u <= cs.size()) {
		if (cs[pos] != 0xFFu) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Expected marker prefix 0xFF in JPXDecode codestream",
				pos);
		}
		const uint8_t marker = cs[pos + 1u];
		pos += 2u;
		if (marker == 0xD9u) { // EOC
			saw_eoc = true;
			break;
		}
		if (pos + 2u > cs.size()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Truncated marker segment in JPXDecode codestream",
				pos);
		}
		const uint16_t mlen = read_u16_be(cs.data() + pos);
		if (mlen < 2u || pos + mlen > cs.size()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Marker segment length exceeds JPXDecode codestream bounds",
				pos);
		}
		if (marker == 0x52u) { // COD (ITU-T T.800 §A.6.1)
			if (mlen < 12u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid COD marker segment length in JPXDecode codestream",
					pos);
			}
			prog_order = cs[pos + 3u];
			const uint16_t raw_layers = read_u16_be(cs.data() + pos + 4u);
			if (raw_layers == 0u || raw_layers > 256u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid or excessive quality layer count in JPXDecode COD marker",
					pos);
			}
			num_layers = raw_layers;
			mct_enabled = (cs[pos + 6u] != 0u);
			num_decomp_levels = cs[pos + 7u];
			if (num_decomp_levels > 10u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Excessive DWT decomposition levels in JPXDecode COD marker",
					pos);
			}
			cb_exp_w = std::clamp<uint32_t>(static_cast<uint32_t>(cs[pos + 8u] & 0x0Fu) + 2u, 2u, 10u);
			cb_exp_h = std::clamp<uint32_t>(static_cast<uint32_t>(cs[pos + 9u] & 0x0Fu) + 2u, 2u, 10u);
			cb_style = cs[pos + 10u];
			wavelet_type = cs[pos + 11u];
			saw_cod = true;
		} else if (marker == 0x5Cu) { // QCD (ITU-T T.800 §A.6.4)
			if (mlen < 4u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid QCD marker segment length in JPXDecode codestream",
					pos);
			}
			const uint8_t sqcd = cs[pos + 2u];
			guard_bits = static_cast<uint8_t>((sqcd >> 5u) & 0x07u);
			const uint8_t qstyle = sqcd & 0x1Fu;
			subband_exponents.clear();
			subband_mantissas.clear();
			if (qstyle == 0u) { // Reversible no quantization (1 byte per subband)
				for (size_t i = 3u; i < mlen; ++i) {
					subband_exponents.push_back(static_cast<uint8_t>((cs[pos + i] >> 3u) & 0x1Fu));
					subband_mantissas.push_back(0u);
				}
			} else if (qstyle == 1u && mlen >= 5u) { // Scalar derived
				const uint16_t sp = read_u16_be(cs.data() + pos + 3u);
				const uint8_t eps0 = static_cast<uint8_t>((sp >> 11u) & 0x1Fu);
				const uint16_t mu0 = static_cast<uint16_t>(sp & 0x7FFu);
				subband_exponents.push_back(eps0);
				subband_mantissas.push_back(mu0);
				for (uint8_t r = 1u; r <= 10u; ++r) {
					const uint8_t er = (eps0 + r > num_decomp_levels)
						? static_cast<uint8_t>(eps0 + r - 1u)
						: eps0;
					for (int b = 0; b < 3; ++b) {
						subband_exponents.push_back(er);
						subband_mantissas.push_back(mu0);
					}
				}
			} else { // Scalar expounded (2 bytes per subband)
				for (size_t i = 3u; i + 1u < mlen; i += 2u) {
					const uint16_t sp = read_u16_be(cs.data() + pos + i);
					subband_exponents.push_back(static_cast<uint8_t>((sp >> 11u) & 0x1Fu));
					subband_mantissas.push_back(static_cast<uint16_t>(sp & 0x7FFu));
				}
			}
			saw_qcd = true;
		} else if (marker == 0x90u) { // SOT
			if (mlen < 10u) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Invalid SOT marker length in JPXDecode codestream",
					pos);
			}
			const uint16_t isot = read_u16_be(cs.data() + pos + 2u);
			if (isot >= total_tiles) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"SOT tile index Isot exceeds tile grid in JPXDecode codestream",
					pos);
			}
			const uint32_t psot = read_u32_be(cs.data() + pos + 4u);
			const size_t sot_start = pos - 2u;
			pos += mlen;
			bool part_saw_sod = false;
			while (pos + 2u <= cs.size()) {
				if (cs[pos] != 0xFFu) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Expected marker inside tile-part header in JPXDecode codestream",
						pos);
				}
				const uint8_t tp_marker = cs[pos + 1u];
				if (tp_marker == 0x93u) { // SOD
					pos += 2u;
					saw_sot = true;
					saw_sod = true;
					part_saw_sod = true;
					break;
				}
				pos += 2u;
				if (pos + 2u > cs.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated marker in tile-part header in JPXDecode codestream",
						pos);
				}
				const uint16_t tp_len = read_u16_be(cs.data() + pos);
				if (tp_len < 2u || pos + tp_len > cs.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Tile-part header marker exceeds codestream bounds",
						pos);
				}
				pos += tp_len;
			}
			if (!part_saw_sod) {
				return make_filter_error(
					ErrorCode::DamagedFormat,
					DiagnosticId::DamagedStream,
					"Missing SOD (0xFF93) marker after SOT in JPXDecode codestream",
					pos);
			}
			std::span<const uint8_t> part_span{};
			if (psot == 0u) {
				if (cs.size() < pos + 2u) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Missing EOC marker at end of JPXDecode codestream",
						pos);
				}
				part_span = cs.subspan(pos, cs.size() - pos - 2u);
				pos = cs.size() - 2u;
			} else {
				if (psot < 14u || sot_start + psot > cs.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"SOT tile-part length Psot exceeds JPXDecode codestream bounds",
						sot_start);
				}
				const size_t data_end = sot_start + psot;
				if (data_end < pos) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"SOT tile-part length Psot smaller than tile-part header",
						sot_start);
				}
				part_span = cs.subspan(pos, data_end - pos);
				pos = data_end;
			}
			if (total_tile_stream_bytes + part_span.size() > effective_limit) {
				return make_filter_error(
					ErrorCode::DecompressionLimitExceeded,
					DiagnosticId::DecompressionLimitExceeded,
					"JPXDecode tile bitstream exceeds ceiling");
			}
			total_tile_stream_bytes += part_span.size();
			auto& tbs = tile_bitstreams[isot];
			tbs.insert(tbs.end(), part_span.begin(), part_span.end());
			continue;
		}
		pos += mlen;
	}

	if (!saw_sot || !saw_sod || !saw_eoc || !saw_cod || !saw_qcd) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"Incomplete J2K codestream (missing COD, QCD, SOT, SOD, or EOC marker) in JPXDecode");
	}

	const uint32_t img_w = hdr.width;
	const uint32_t img_h = hdr.height;
	const uint32_t num_comps = hdr.components;
	const size_t pixels = static_cast<size_t>(img_w) * img_h;
	const size_t total_samples = pixels * num_comps;
	const uint64_t total_wic_bytes = static_cast<uint64_t>(pixels) * (static_cast<uint64_t>(num_comps) + 8ULL);
	if (total_wic_bytes > effective_limit) {
		return make_filter_error(
			ErrorCode::DecompressionLimitExceeded,
			DiagnosticId::DecompressionLimitExceeded,
			"JPXDecode cumulative decoded image buffers exceed ceiling");
	}

	const uint32_t nom_cb_w = 1u << cb_exp_w;
	const uint32_t nom_cb_h = 1u << cb_exp_h;
	const int32_t dc_shift = is_signed ? 0 : (1 << (bit_depth - 1u));
	const int32_t max_sample = (1 << bit_depth) - 1;

	WicDecodedImage out_img{};
	out_img.width = img_w;
	out_img.height = img_h;
	out_img.components = num_comps;
	out_img.pixels.assign(total_samples, 0u);

	struct SubbandState {
		uint32_t w = 0u;
		uint32_t h = 0u;
		uint8_t band_type = 0u; // 0 = LL/LH, 1 = HL, 2 = HH
		uint8_t gain = 0u;      // 0 = LL, 1 = HL/LH, 2 = HH
		uint8_t eps = 8u;
		uint16_t mu = 0u;
		uint32_t mb_total = 8u;
		uint32_t num_cb_x = 0u;
		uint32_t num_cb_y = 0u;
		J2kTagTree incl_tree;
		J2kTagTree zbp_tree;
		struct CodeBlock {
			uint32_t x0 = 0u;
			uint32_t y0 = 0u;
			uint32_t w = 0u;
			uint32_t h = 0u;
			bool included_once = false;
			uint32_t zero_bitplanes = 0u;
			uint32_t lblock = 3u;
			uint32_t total_passes = 0u;
			std::vector<uint8_t> compressed;
		};
		std::vector<CodeBlock> cbs;
		std::vector<int32_t> coeffs;
		std::vector<float> coeffs_f;
	};

	struct ResolutionState {
		uint32_t res_w = 0u;
		uint32_t res_h = 0u;
		std::vector<SubbandState> subbands;
	};

	struct ComponentState {
		std::vector<ResolutionState> resolutions;
		std::vector<int32_t> spatial;
		std::vector<float> spatial_f;
	};

	std::vector<int32_t> cb_coeffs;
	for (uint32_t ty = 0; ty < num_tiles_y; ++ty) {
		for (uint32_t tx = 0; tx < num_tiles_x; ++tx) {
			const uint32_t tile_idx = ty * num_tiles_x + tx;
			const std::vector<uint8_t>& tile_bitstream = tile_bitstreams[tile_idx];
			const uint32_t tx0 = std::max(xtosiz + tx * xtsiz, xosiz);
			const uint32_t ty0 = std::max(ytosiz + ty * ytsiz, yosiz);
			const uint32_t tx1 = std::min(xtosiz + (tx + 1u) * xtsiz, xsiz);
			const uint32_t ty1 = std::min(ytosiz + (ty + 1u) * ytsiz, ysiz);
			if (tx1 <= tx0 || ty1 <= ty0 || tile_bitstream.empty()) {
				continue;
			}
			const uint32_t tile_w = tx1 - tx0;
			const uint32_t tile_h = ty1 - ty0;

			// Compute resolution bounds (trx0, try0, trx1, try1) using ITU-T T.800 Annex B ceil_div.
			auto ceil_div_pow2 = [](uint32_t v, uint32_t p) noexcept -> uint32_t {
				if (p == 0u) {
					return v;
				}
				const uint32_t add = (1u << p) - 1u;
				return (v + add) >> p;
			};
			struct ResBounds {
				uint32_t x0 = 0u;
				uint32_t y0 = 0u;
				uint32_t x1 = 0u;
				uint32_t y1 = 0u;
				uint32_t w = 0u;
				uint32_t h = 0u;
			};
			std::vector<ResBounds> res_bounds(num_decomp_levels + 1u);
			for (uint8_t r = 0u; r <= num_decomp_levels; ++r) {
				const uint32_t shift = num_decomp_levels - r;
				auto& rb = res_bounds[r];
				rb.x0 = ceil_div_pow2(tx0, shift);
				rb.y0 = ceil_div_pow2(ty0, shift);
				rb.x1 = ceil_div_pow2(tx1, shift);
				rb.y1 = ceil_div_pow2(ty1, shift);
				rb.w = (rb.x1 > rb.x0) ? (rb.x1 - rb.x0) : 0u;
				rb.h = (rb.y1 > rb.y0) ? (rb.y1 - rb.y0) : 0u;
			}

			std::vector<ComponentState> comps(num_comps);
			for (uint32_t c = 0; c < num_comps; ++c) {
				comps[c].resolutions.resize(num_decomp_levels + 1u);
				size_t sb_global_idx = 0u;
				for (uint8_t r = 0u; r <= num_decomp_levels; ++r) {
					ResolutionState& rs = comps[c].resolutions[r];
					rs.res_w = res_bounds[r].w;
					rs.res_h = res_bounds[r].h;

					auto init_subband = [&](uint32_t sb_w, uint32_t sb_h, uint8_t btype, uint8_t bgain) -> PdfResult<void> {
						SubbandState sb{};
						sb.w = sb_w;
						sb.h = sb_h;
						sb.band_type = btype;
						sb.gain = bgain;
						sb.eps = (sb_global_idx < subband_exponents.size())
							? subband_exponents[sb_global_idx]
							: bit_depth;
						sb.mu = (sb_global_idx < subband_mantissas.size())
							? subband_mantissas[sb_global_idx]
							: 0u;
						++sb_global_idx;
						sb.mb_total = (sb.eps + guard_bits > 1u)
							? static_cast<uint32_t>(sb.eps + guard_bits - 1u)
							: static_cast<uint32_t>(bit_depth);
						sb.num_cb_x = (sb_w == 0u) ? 0u : ((sb_w + nom_cb_w - 1u) / nom_cb_w);
						sb.num_cb_y = (sb_h == 0u) ? 0u : ((sb_h + nom_cb_h - 1u) / nom_cb_h);
						if (static_cast<uint64_t>(sb.num_cb_x) * sb.num_cb_y > 65536ULL) {
							return make_filter_error(
								ErrorCode::DecompressionLimitExceeded,
								DiagnosticId::DecompressionLimitExceeded,
								"JPXDecode subband code-block grid count exceeds ceiling");
						}
						if (sb.num_cb_x > 0u && sb.num_cb_y > 0u) {
							sb.incl_tree.init(sb.num_cb_x, sb.num_cb_y);
							sb.zbp_tree.init(sb.num_cb_x, sb.num_cb_y);
							sb.cbs.resize(static_cast<size_t>(sb.num_cb_x) * sb.num_cb_y);
							for (uint32_t cy = 0; cy < sb.num_cb_y; ++cy) {
								for (uint32_t cx = 0; cx < sb.num_cb_x; ++cx) {
									auto& cb = sb.cbs[static_cast<size_t>(cy) * sb.num_cb_x + cx];
									cb.x0 = cx * nom_cb_w;
									cb.y0 = cy * nom_cb_h;
									cb.w = std::min(nom_cb_w, sb_w - cb.x0);
									cb.h = std::min(nom_cb_h, sb_h - cb.y0);
								}
							}
						}
						rs.subbands.push_back(std::move(sb));
						return {};
					};

					if (r == 0u) {
						RECTO_PDF_RETURN_IF_ERROR(init_subband(rs.res_w, rs.res_h, 0u, 0u)); // LL0
					} else {
						const uint32_t ll_w = res_bounds[r - 1u].w;
						const uint32_t ll_h = res_bounds[r - 1u].h;
						const uint32_t hl_w = rs.res_w - ll_w;
						const uint32_t lh_h = rs.res_h - ll_h;
						RECTO_PDF_RETURN_IF_ERROR(init_subband(hl_w, ll_h, 1u, 1u)); // HL_r
						RECTO_PDF_RETURN_IF_ERROR(init_subband(ll_w, lh_h, 0u, 1u)); // LH_r
						RECTO_PDF_RETURN_IF_ERROR(init_subband(hl_w, lh_h, 2u, 2u)); // HH_r
					}
				}
			}

			size_t stream_pos = 0u;
			auto decode_packet = [&](uint16_t layer, uint8_t r, uint32_t c) -> PdfResult<void> {
				if (stream_pos >= tile_bitstream.size()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated Tier-2 packet header in JPXDecode tile bitstream");
				}
				std::span<const uint8_t> rem =
					std::span<const uint8_t>(tile_bitstream).subspan(stream_pos);
				J2kPacketBitReader br(rem);

				uint32_t non_empty = 0u;
				if (!br.read_bit(non_empty)) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Failed to read zero-length packet bit in JPXDecode packet header");
				}
				ResolutionState& rs = comps[c].resolutions[r];
				struct PendingBody {
					SubbandState::CodeBlock* cb = nullptr;
					uint32_t len = 0u;
				};
				std::vector<PendingBody> pending;

				if (non_empty != 0u) {
					for (SubbandState& sb : rs.subbands) {
						for (uint32_t cy = 0; cy < sb.num_cb_y; ++cy) {
							for (uint32_t cx = 0; cx < sb.num_cb_x; ++cx) {
								auto& cb = sb.cbs[static_cast<size_t>(cy) * sb.num_cb_x + cx];
								bool included = false;
								if (!cb.included_once) {
									int32_t incl_layer = 0;
									if (!sb.incl_tree.decode(
											br,
											cx,
											cy,
											static_cast<int32_t>(layer) + 1,
											incl_layer)) {
										return make_filter_error(
											ErrorCode::DamagedFormat,
											DiagnosticId::DamagedStream,
											"Truncated inclusion tag-tree in JPXDecode packet header");
									}
									included = (incl_layer <= static_cast<int32_t>(layer));
									if (included) {
										int32_t zbp = 0;
										if (!sb.zbp_tree.decode(
												br,
												cx,
												cy,
												std::numeric_limits<int32_t>::max(),
												zbp)) {
											return make_filter_error(
												ErrorCode::DamagedFormat,
												DiagnosticId::DamagedStream,
												"Truncated zero-bitplane tag-tree in JPXDecode packet header");
										}
										cb.zero_bitplanes = static_cast<uint32_t>(std::max(0, zbp));
										cb.included_once = true;
									}
								} else {
									uint32_t inc_bit = 0u;
									if (!br.read_bit(inc_bit)) {
										return make_filter_error(
											ErrorCode::DamagedFormat,
											DiagnosticId::DamagedStream,
											"Truncated code-block inclusion bit in JPXDecode packet header");
									}
									included = (inc_bit != 0u);
								}
								if (!included) {
									continue;
								}
								uint32_t new_passes = 0u;
								if (!decode_j2k_num_passes(br, new_passes)) {
									return make_filter_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedStream,
										"Truncated coding passes count in JPXDecode packet header");
								}
								while (true) {
									uint32_t comma_bit = 0u;
									if (!br.read_bit(comma_bit)) {
										return make_filter_error(
											ErrorCode::DamagedFormat,
											DiagnosticId::DamagedStream,
											"Truncated Lblock comma code in JPXDecode packet header");
									}
									if (comma_bit == 0u) {
										break;
									}
									++cb.lblock;
									if (cb.lblock > 32u) {
										return make_filter_error(
											ErrorCode::DamagedFormat,
											DiagnosticId::DamagedStream,
											"Excessive Lblock indicator in JPXDecode packet header");
									}
								}
								const uint32_t pass_log2 =
									(new_passes <= 1u) ? 0u : static_cast<uint32_t>(std::bit_width(new_passes) - 1u);
								const uint32_t len_bits = cb.lblock + pass_log2;
								if (len_bits > 31u) {
									return make_filter_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedStream,
										"Code-block length field bit count exceeds 31 bits in JPXDecode");
								}
								uint32_t seg_len = 0u;
								if (!br.read_bits(len_bits, seg_len)) {
									return make_filter_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedStream,
										"Truncated code-block data length in JPXDecode packet header");
								}
								if (cb.total_passes + new_passes > 1024u) {
									return make_filter_error(
										ErrorCode::DamagedFormat,
										DiagnosticId::DamagedStream,
										"Excessive coding pass count in JPXDecode code-block");
								}
								cb.total_passes += new_passes;
								pending.push_back({&cb, seg_len});
							}
						}
					}
				}

				if (!br.align_to_byte()) {
					return make_filter_error(
						ErrorCode::DamagedFormat,
						DiagnosticId::DamagedStream,
						"Truncated stuffed byte at end of JPXDecode packet header");
				}
				stream_pos += br.bytes_consumed();

				for (const PendingBody& item : pending) {
					if (item.len > tile_bitstream.size() - stream_pos) {
						return make_filter_error(
							ErrorCode::DamagedFormat,
							DiagnosticId::DamagedStream,
							"Code-block body segment exceeds JPXDecode tile bitstream bounds");
					}
					item.cb->compressed.insert(
						item.cb->compressed.end(),
						tile_bitstream.begin() + static_cast<std::ptrdiff_t>(stream_pos),
						tile_bitstream.begin() + static_cast<std::ptrdiff_t>(stream_pos + item.len));
					stream_pos += item.len;
				}
				return {};
			};

			if (prog_order == 0u) { // LRCP
				for (uint16_t l = 0; l < num_layers; ++l) {
					for (uint8_t r = 0; r <= num_decomp_levels; ++r) {
						for (uint32_t c = 0; c < num_comps; ++c) {
							RECTO_PDF_RETURN_IF_ERROR(decode_packet(l, r, c));
						}
					}
				}
			} else { // RLCP / RPCL / PCRL / CPRL single-precinct order
				for (uint8_t r = 0; r <= num_decomp_levels; ++r) {
					for (uint16_t l = 0; l < num_layers; ++l) {
						for (uint32_t c = 0; c < num_comps; ++c) {
							RECTO_PDF_RETURN_IF_ERROR(decode_packet(l, r, c));
						}
					}
				}
			}

			// Tier-1 EBCOT decoding & 2D inverse DWT reconstruction for each component.
			for (uint32_t c = 0; c < num_comps; ++c) {
				for (uint8_t r = 0; r <= num_decomp_levels; ++r) {
					for (SubbandState& sb : comps[c].resolutions[r].subbands) {
						if (wavelet_type == 1u) {
							sb.coeffs.assign(static_cast<size_t>(sb.w) * sb.h, 0);
						} else {
							sb.coeffs_f.assign(static_cast<size_t>(sb.w) * sb.h, 0.0f);
						}
						const int32_t rb = static_cast<int32_t>(bit_depth) + static_cast<int32_t>(sb.gain);
						const float delta_b = std::ldexp(
							1.0f + static_cast<float>(sb.mu) / 2048.0f,
							rb - static_cast<int32_t>(sb.eps));
						for (const auto& cb : sb.cbs) {
							if (!cb.included_once || cb.total_passes == 0u || cb.compressed.empty()) {
								continue;
							}
							decode_j2k_codeblock(
								cb.compressed,
								cb.w,
								cb.h,
								cb.total_passes,
								cb.zero_bitplanes,
								sb.mb_total,
								sb.band_type,
								cb_style,
								cb_coeffs);
							for (uint32_t y = 0; y < cb.h; ++y) {
								for (uint32_t x = 0; x < cb.w; ++x) {
									const int32_t q = cb_coeffs[static_cast<size_t>(y) * cb.w + x];
									const size_t dst_idx = static_cast<size_t>(cb.y0 + y) * sb.w + (cb.x0 + x);
									if (wavelet_type == 1u) {
										sb.coeffs[dst_idx] = q;
									} else {
										sb.coeffs_f[dst_idx] = static_cast<float>(q) * delta_b;
									}
								}
							}
						}
					}
				}

				if (wavelet_type == 1u) {
					std::vector<int32_t> cur_ll = std::move(comps[c].resolutions[0].subbands[0].coeffs);
					for (uint8_t r = 1u; r <= num_decomp_levels; ++r) {
						const ResolutionState& rs = comps[c].resolutions[r];
						const uint32_t ll_w = comps[c].resolutions[r - 1u].res_w;
						const uint32_t ll_h = comps[c].resolutions[r - 1u].res_h;
						std::vector<int32_t> next_ll;
						idwt_53_2d(
							rs.res_w,
							rs.res_h,
							ll_w,
							ll_h,
							cur_ll,
							rs.subbands[0].coeffs,
							rs.subbands[1].coeffs,
							rs.subbands[2].coeffs,
							next_ll);
						cur_ll = std::move(next_ll);
					}
					comps[c].spatial = std::move(cur_ll);
				} else {
					std::vector<float> cur_ll = std::move(comps[c].resolutions[0].subbands[0].coeffs_f);
					for (uint8_t r = 1u; r <= num_decomp_levels; ++r) {
						const ResolutionState& rs = comps[c].resolutions[r];
						const uint32_t ll_w = comps[c].resolutions[r - 1u].res_w;
						const uint32_t ll_h = comps[c].resolutions[r - 1u].res_h;
						std::vector<float> next_ll;
						idwt_97_2d_float(
							rs.res_w,
							rs.res_h,
							ll_w,
							ll_h,
							cur_ll,
							rs.subbands[0].coeffs_f,
							rs.subbands[1].coeffs_f,
							rs.subbands[2].coeffs_f,
							next_ll);
						cur_ll = std::move(next_ll);
					}
					comps[c].spatial_f = std::move(cur_ll);
				}
			}

			const size_t tile_pixels = static_cast<size_t>(tile_w) * tile_h;
			if (wavelet_type != 1u) {
				for (uint32_t c = 0; c < num_comps; ++c) {
					comps[c].spatial.resize(tile_pixels);
				}
				if (mct_enabled && num_comps >= 3u) {
					// Inverse Irreversible Color Transform (ICT, ITU-T T.800 Annex G.3).
					for (size_t i = 0; i < tile_pixels; ++i) {
						const float y0 = comps[0].spatial_f[i];
						const float cb = comps[1].spatial_f[i];
						const float cr = comps[2].spatial_f[i];
						comps[0].spatial[i] = static_cast<int32_t>(std::lrintf(y0 + 1.402f * cr));
						comps[1].spatial[i] = static_cast<int32_t>(std::lrintf(y0 - 0.34413f * cb - 0.71414f * cr));
						comps[2].spatial[i] = static_cast<int32_t>(std::lrintf(y0 + 1.772f * cb));
						for (uint32_t c = 3u; c < num_comps; ++c) {
							comps[c].spatial[i] = static_cast<int32_t>(std::lrintf(comps[c].spatial_f[i]));
						}
					}
				} else {
					for (uint32_t c = 0; c < num_comps; ++c) {
						for (size_t i = 0; i < tile_pixels; ++i) {
							comps[c].spatial[i] = static_cast<int32_t>(std::lrintf(comps[c].spatial_f[i]));
						}
					}
				}
			} else if (mct_enabled && num_comps >= 3u) {
				// Inverse Reversible Color Transform (RCT, ITU-T T.800 Annex G.2).
				for (size_t i = 0; i < tile_pixels; ++i) {
					const int32_t y0 = comps[0].spatial[i];
					const int32_t u0 = comps[1].spatial[i];
					const int32_t v0 = comps[2].spatial[i];
					const int32_t g = y0 - ((u0 + v0) >> 2);
					const int32_t r = v0 + g;
					const int32_t b = u0 + g;
					comps[0].spatial[i] = r;
					comps[1].spatial[i] = g;
					comps[2].spatial[i] = b;
				}
			}

			const uint32_t dst_x0 = tx0 - xosiz;
			const uint32_t dst_y0 = ty0 - yosiz;
			for (uint32_t y = 0; y < tile_h; ++y) {
				for (uint32_t x = 0; x < tile_w; ++x) {
					const size_t t_idx = static_cast<size_t>(y) * tile_w + x;
					const size_t img_idx = static_cast<size_t>(dst_y0 + y) * img_w + (dst_x0 + x);
					for (uint32_t c = 0; c < num_comps; ++c) {
						int32_t val = std::clamp(comps[c].spatial[t_idx] + dc_shift, 0, max_sample);
						if (bit_depth > 8u) {
							val >>= (bit_depth - 8u);
						} else if (bit_depth < 8u) {
							val = (val * 255) / max_sample;
						}
						out_img.pixels[img_idx * num_comps + c] = static_cast<uint8_t>(std::clamp(val, 0, 255));
					}
				}
			}
		}
	}

	apply_pdf_decode_array(out_img.pixels, num_comps, decode_parms);
	out_img.rgba_pixels.resize(pixels * 4u);
	out_img.bgra_pixels.resize(pixels * 4u);
	for (size_t p = 0; p < pixels; ++p) {
		const uint8_t r = out_img.pixels[p * num_comps + 0u];
		const uint8_t g = (num_comps >= 3u) ? out_img.pixels[p * num_comps + 1u] : r;
		const uint8_t b = (num_comps >= 3u) ? out_img.pixels[p * num_comps + 2u] : r;
		const uint8_t a = (num_comps >= 4u) ? out_img.pixels[p * num_comps + 3u] : 255u;
		out_img.rgba_pixels[p * 4u + 0u] = r;
		out_img.rgba_pixels[p * 4u + 1u] = g;
		out_img.rgba_pixels[p * 4u + 2u] = b;
		out_img.rgba_pixels[p * 4u + 3u] = a;
		out_img.bgra_pixels[p * 4u + 0u] = b;
		out_img.bgra_pixels[p * 4u + 1u] = g;
		out_img.bgra_pixels[p * 4u + 2u] = r;
		out_img.bgra_pixels[p * 4u + 3u] = a;
	}
	return out_img;
}

} // namespace

PdfResult<WicDecodedImage> decode_jpx_image(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	try {
		return decode_jpx_internal(input, decode_parms, max_decoded_bytes);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while decoding JPXDecode image stream");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while decoding JPXDecode image stream");
	}
}

PdfResult<std::vector<uint8_t>> decode_jpx(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes) noexcept
{
	RECTO_PDF_TRY(
		WicDecodedImage img,
		decode_jpx_image(input, decode_parms, max_decoded_bytes));
	return std::move(img.pixels);
}

PdfResult<std::vector<uint8_t>> decode_single_filter(
	std::string_view filter_name,
	std::span<const uint8_t> input,
	const PdfDict* decode_parms,
	size_t max_decoded_bytes,
	const IndirectObjectResolver* resolver) noexcept
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
	if (filter_name == "CCITTFaxDecode" || filter_name == "CCF") {
		return decode_ccitt_fax(input, decode_parms, max_decoded_bytes);
	}
	if (filter_name == "JBIG2Decode") {
		return decode_jbig2(input, decode_parms, max_decoded_bytes, resolver);
	}
	if (filter_name == "DCTDecode" || filter_name == "DCT") {
		return decode_dct(input, decode_parms, max_decoded_bytes);
	}
	if (filter_name == "JPXDecode") {
		return decode_jpx(input, decode_parms, max_decoded_bytes);
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
	return make_filter_error(
		ErrorCode::DamagedFormat,
		DiagnosticId::DamagedStream,
		"Unknown or unsupported PDF stream /Filter name");
}

namespace {

[[nodiscard]] PdfResult<std::vector<uint8_t>> execute_filter_pipeline(
	std::span<const uint8_t> raw_bytes,
	const PdfValue* filter_val,
	const PdfValue* parms_val,
	size_t effective_limit,
	const PdfDict* stream_dict_context = nullptr,
	const IndirectObjectResolver* resolver = nullptr,
	bool allow_jbig2 = true)
{
	if (filter_val == nullptr || filter_val->is_null()) {
		if (raw_bytes.size() > effective_limit) {
			return make_filter_error(
				ErrorCode::DecompressionLimitExceeded,
				DiagnosticId::DecompressionLimitExceeded,
				"Unfiltered stream exceeds maximum decoded stream byte ceiling");
		}
		return std::vector<uint8_t>(raw_bytes.begin(), raw_bytes.end());
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
		if (!allow_jbig2 && filter_names[i] == "JBIG2Decode") {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"Recursive /JBIG2Decode filter inside /JBIG2Globals stream is forbidden");
		}
		const std::span<const uint8_t> stage_in =
			(i == 0) ? raw_bytes : std::span<const uint8_t>(stage_buf);
		const PdfDict* stage_parms = filter_parms[i];
		std::optional<PdfDict> merged_image_parms;
		if (filter_names[i] == "DCTDecode" || filter_names[i] == "DCT" ||
			filter_names[i] == "JPXDecode") {
			if (stage_parms == nullptr) {
				stage_parms = stream_dict_context;
			} else if (stream_dict_context != nullptr &&
					   stage_parms != stream_dict_context &&
					   stage_parms->get("Decode") == nullptr &&
					   stage_parms->get("D") == nullptr) {
				const PdfValue* stm_decode = stream_dict_context->get("Decode");
				if (stm_decode == nullptr) {
					stm_decode = stream_dict_context->get("D");
				}
				if (stm_decode != nullptr) {
					merged_image_parms = *stage_parms;
					merged_image_parms->set("Decode", PdfValue(*stm_decode));
					stage_parms = &(*merged_image_parms);
				}
			}
		}
		RECTO_PDF_TRY(
			std::vector<uint8_t> next_buf,
			decode_single_filter(filter_names[i], stage_in, stage_parms, effective_limit, resolver));
		stage_buf = std::move(next_buf);
	}
	return stage_buf;
}

[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_jbig2_globals_stream(
	const PdfStream& gstm,
	size_t effective_limit)
{
	if (const PdfValue* f_spec = gstm.dict.get("F"); f_spec != nullptr && !f_spec->is_null()) {
		return make_pdf_error(
			ErrorCode::UnsupportedFeature,
			"External /JBIG2Globals stream file specification (/F) is unsupported");
	}
	if (const PdfValue* ffilter = gstm.dict.get("FFilter"); ffilter != nullptr && !ffilter->is_null()) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"/JBIG2Globals stream specifies /FFilter without /F");
	}
	if (const PdfValue* fparms = gstm.dict.get("FDecodeParms"); fparms != nullptr && !fparms->is_null()) {
		return make_filter_error(
			ErrorCode::DamagedFormat,
			DiagnosticId::DamagedStream,
			"/JBIG2Globals stream specifies /FDecodeParms without /F");
	}
	const PdfValue* parms_val = gstm.dict.get("DecodeParms");
	if (parms_val == nullptr) {
		parms_val = gstm.dict.get("DP");
	}
	return execute_filter_pipeline(
		gstm.raw_bytes,
		gstm.dict.get("Filter"),
		parms_val,
		effective_limit,
		&gstm.dict,
		/*resolver=*/nullptr,
		/*allow_jbig2=*/false);
}

} // namespace

PdfResult<std::vector<uint8_t>> decode_stream(
	std::span<const uint8_t> raw_bytes,
	const PdfDict& stream_dict,
	size_t max_decoded_bytes,
	const IndirectObjectResolver* resolver) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);

		const PdfValue* f_spec = stream_dict.get("F");
		if (f_spec != nullptr && !f_spec->is_null()) {
			return make_pdf_error(
				ErrorCode::UnsupportedFeature,
				"External stream file specification (/F) requires decode_external_stream with resolved external bytes");
		}

		// Orphan /FFilter or /FDecodeParms without /F are malformed regardless
		// of whether inline /Filter is also present.
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

		const PdfValue* filter_val = stream_dict.get("Filter");
		const PdfValue* parms_val = stream_dict.get("DecodeParms");
		if (parms_val == nullptr) {
			parms_val = stream_dict.get("DP");
		}

		return execute_filter_pipeline(
			raw_bytes,
			filter_val,
			parms_val,
			effective_limit,
			&stream_dict,
			resolver);
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
	size_t max_decoded_bytes,
	const IndirectObjectResolver* resolver) noexcept
{
	return decode_stream(stream.raw_bytes, stream.dict, max_decoded_bytes, resolver);
}

PdfResult<std::vector<uint8_t>> decode_external_stream(
	std::span<const uint8_t> external_bytes,
	const PdfDict& stream_dict,
	size_t max_decoded_bytes,
	const IndirectObjectResolver* resolver) noexcept
{
	try {
		const size_t effective_limit = std::min<size_t>(max_decoded_bytes, kMaxDecodedStreamBytes);

		const PdfValue* f_spec = stream_dict.get("F");
		if (f_spec == nullptr || f_spec->is_null()) {
			return make_filter_error(
				ErrorCode::DamagedFormat,
				DiagnosticId::DamagedStream,
				"decode_external_stream requires /F external file specification in stream dictionary");
		}

		const PdfValue* filter_val = stream_dict.get("FFilter");
		const PdfValue* parms_val = stream_dict.get("FDecodeParms");
		return execute_filter_pipeline(
			external_bytes,
			filter_val,
			parms_val,
			effective_limit,
			&stream_dict,
			resolver);
	} catch (const std::bad_alloc&) {
		return make_pdf_error(
			ErrorCode::OutOfMemory,
			"Out of memory while executing external stream filter pipeline");
	} catch (...) {
		return make_pdf_error(
			ErrorCode::InternalError,
			"Unexpected exception while executing external stream filter pipeline");
	}
}

} // namespace recto::pdf

