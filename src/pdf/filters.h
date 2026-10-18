#pragma once

// Lossless PDF stream filter pipeline: FlateDecode (RFC 1950/1951 + raw-deflate
// fallback), LZWDecode, TIFF Predictor 2 and PNG Predictors 10..15,
// ASCIIHexDecode, ASCII85Decode, RunLengthDecode, and chained filter execution
// with a strict 256 MB per-stream decoded output ceiling (ISO 32000-1 section
// 7.4; D01 T01 section 7).

#include "error.h"
#include "object.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace recto::pdf {

inline constexpr size_t kMaxDecodedStreamBytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr size_t kMaxFilterChainDepth = 8;

struct PredictorParams {
	int32_t predictor{1};
	int32_t colors{1};
	int32_t bits_per_component{8};
	int32_t columns{1};
};

// Extracts and validates `/Predictor`, `/Colors`, `/BitsPerComponent`, and
// `/Columns` from an optional `/DecodeParms` dictionary.
[[nodiscard]] PdfResult<PredictorParams> parse_predictor_params(
	const PdfDict* decode_parms) noexcept;

// Applies TIFF Predictor 2 or PNG Predictors 10..15 (`Sub`, `Up`, `Average`,
// `Paeth`, `Optimum`) to a decompressed stream buffer.
[[nodiscard]] PdfResult<std::vector<uint8_t>> apply_predictor(
	std::span<const uint8_t> input,
	const PredictorParams& params,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Decompresses a `FlateDecode` stream (RFC 1950 zlib wrapper or raw RFC 1951
// deflate fallback) and applies any configured predictor from `decode_parms`.
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_flate(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Decompresses an `LZWDecode` stream (9-12 bit variable-width codes, clear
// table `256`, EOD `257`, `/EarlyChange` `0` or `1`) and applies any configured
// predictor from `decode_parms`.
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_lzw(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Decodes an `ASCIIHexDecode` stream (`0-9A-Fa-f`, whitespace ignored, odd
// final nibble zero-padded, `>` EOD marker).
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_ascii_hex(
	std::span<const uint8_t> input,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Decodes an `ASCII85Decode` stream (`!`..`u` base-85 tuples, `z` -> four zero
// bytes, 2..4 byte final tuple padding, `~>` EOD marker).
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_ascii85(
	std::span<const uint8_t> input,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Decodes a `RunLengthDecode` stream (`0..127` literal copy of `n + 1` bytes,
// `129..255` repeat next byte `257 - n` times, `128` EOD marker).
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_run_length(
	std::span<const uint8_t> input,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Dispatches a single PDF stream filter by name (including standard PDF
// abbreviations `Fl`, `LZW`, `AHx`, `A85`, `RL`).
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_single_filter(
	std::string_view filter_name,
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Executes a single or chained `/Filter` pipeline (up to `kMaxFilterChainDepth`
// = 8 stages) using matching `/DecodeParms` entries from `stream_dict`.
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_stream(
	std::span<const uint8_t> raw_bytes,
	const PdfDict& stream_dict,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_stream(
	const PdfStream& stream,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

} // namespace recto::pdf
