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
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace recto::pdf {

inline constexpr size_t kMaxDecodedStreamBytes = 256ULL * 1024ULL * 1024ULL;
inline constexpr size_t kMaxFilterChainDepth = 8;

// Optional callback used by stream filters (such as `JBIG2Decode` when
// `/JBIG2Globals` is an indirect reference `N G R`) to resolve an indirect
// object from the owning `XrefTable` / `Document`.
using IndirectObjectResolver = std::function<PdfResult<PdfValue>(IndirectRef)>;

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

struct CcittFaxParams {
	int32_t k{0};
	bool end_of_line{false};
	bool encoded_byte_align{false};
	int32_t columns{1728};
	int32_t rows{0};
	bool end_of_block{true};
	bool black_is_1{false};
	int32_t damaged_rows_before_error{0};
};

// Extracts and validates `/K`, `/EndOfLine`, `/EncodedByteAlign`, `/Columns`,
// `/Rows`, `/EndOfBlock`, `/BlackIs1`, and `/DamagedRowsBeforeError` from an
// optional `/DecodeParms` dictionary (ISO 32000-1 section 7.4.6, Table 11).
[[nodiscard]] PdfResult<CcittFaxParams> parse_ccitt_fax_params(
	const PdfDict* decode_parms) noexcept;

// Decodes a `CCITTFaxDecode` monochrome bi-level stream (ITU-T T.4 Group 3 1D
// and 2D, ITU-T T.6 Group 4 2D MMR) into packed 1-bit-per-pixel rows.
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_ccitt_fax(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Decodes a `JBIG2Decode` monochrome bi-level stream (ISO/IEC 14492, ISO
// 32000-1 section 7.4.7) with optional `/JBIG2Globals` stream in `decode_parms`
// (either a direct `PdfStream` or an `IndirectRef` resolved via `resolver`) or
// explicit `globals_bytes`, returning packed 1-bit-per-pixel rows (`0` =
// black, `1` = white per PDF DeviceGray convention).
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_jbig2(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes,
	const IndirectObjectResolver* resolver = nullptr) noexcept;

[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_jbig2(
	std::span<const uint8_t> input,
	std::span<const uint8_t> globals_bytes,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

struct WicDecodedImage {
	uint32_t width{0};
	uint32_t height{0};
	uint32_t components{0};
	bool is_cmyk{false};
	std::vector<uint8_t> pixels;
	std::vector<uint8_t> rgba_pixels;
	std::vector<uint8_t> bgra_pixels;
};

using DecodedImage = WicDecodedImage;

// Returns the address of the calling thread's thread-local `IWICImagingFactory`
// COM instance (initializing MTA COM + WIC on first call per thread).
[[nodiscard]] uintptr_t current_thread_wic_factory_id() noexcept;

// Decodes a `DCTDecode` (JPEG) stream via the calling thread's thread-local
// `IWICImagingFactory`, handling Grayscale, RGB/YCbCr, and CMYK/YCCK (with
// `/Decode` array inversion `[1 0 1 0 1 0 1 0]` for Adobe inverted CMYK JPEGs).
[[nodiscard]] PdfResult<WicDecodedImage> decode_dct_image(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_dct(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Decodes a `JPXDecode` (JPEG 2000) stream via the calling thread's
// thread-local `IWICImagingFactory` or the native ISO/IEC 15444-1 (ITU-T T.800)
// Tier-1/Tier-2 EBCOT + 5/3 & 9/7 DWT JPEG 2000 codestream decoder.
[[nodiscard]] PdfResult<WicDecodedImage> decode_jpx_image(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_jpx(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes) noexcept;

// Dispatches a single PDF stream filter by name (including standard PDF
// abbreviations `Fl`, `LZW`, `AHx`, `A85`, `RL`, `CCF`, `DCT`).
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_single_filter(
	std::string_view filter_name,
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes,
	const IndirectObjectResolver* resolver = nullptr) noexcept;

// Executes a single or chained `/Filter` pipeline (up to `kMaxFilterChainDepth`
// = 8 stages) using matching `/DecodeParms` (or `/DP`) entries from
// `stream_dict`. Streams specifying an external file (`/F`) are rejected on
// inline `decode_stream` with `ErrorCode::UnsupportedFeature`; callers that
// have resolved external bytes explicitly invoke `decode_external_stream`.
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_stream(
	std::span<const uint8_t> raw_bytes,
	const PdfDict& stream_dict,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes,
	const IndirectObjectResolver* resolver = nullptr) noexcept;

[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_stream(
	const PdfStream& stream,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes,
	const IndirectObjectResolver* resolver = nullptr) noexcept;

// Executes a single or chained `/FFilter` pipeline (up to `kMaxFilterChainDepth`
// = 8 stages) using matching `/FDecodeParms` entries from `stream_dict` on
// caller-supplied external stream bytes (`stream_dict` must specify `/F`).
[[nodiscard]] PdfResult<std::vector<uint8_t>> decode_external_stream(
	std::span<const uint8_t> external_bytes,
	const PdfDict& stream_dict,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes,
	const IndirectObjectResolver* resolver = nullptr) noexcept;

} // namespace recto::pdf
