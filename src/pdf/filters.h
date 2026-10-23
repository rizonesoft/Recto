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

// Which pixel buffers a DCT decode builds (D06 T05 §38). Each is image-sized, so a caller asks only for what it uses
// and the decoded-bytes ceiling is charged with that alone.
enum class DctOutput : uint8_t {
	All,     // the samples, RGBA and BGRA
	Samples, // the decoded samples: gray, RGB, or CMYK for a CMYK image
	Rgba,    // 8-bit RGBA only
	Bgra,    // 8-bit BGRA only
	Stream,  // what DCTDecode returns as a stream filter: the samples, or for a CMYK image RGBA (or BGRA or the CMYK
	         // samples when a direct caller's /TargetFormat asks)
};

// Decodes a `DCTDecode` (JPEG) stream via the calling thread's thread-local
// `IWICImagingFactory`, handling Grayscale, RGB/YCbCr, and CMYK/YCCK (with
// `/Decode` array inversion `[1 0 1 0 1 0 1 0]` for Adobe inverted CMYK JPEGs).
[[nodiscard]] PdfResult<WicDecodedImage> decode_dct_image(
	std::span<const uint8_t> input,
	const PdfDict* decode_parms = nullptr,
	size_t max_decoded_bytes = kMaxDecodedStreamBytes,
	DctOutput output = DctOutput::All) noexcept;

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

// Whether every stage of `stream_dict`'s /Filter (a direct name or array of names; none at all counts) is one of the
// general-purpose lossless filters whose output detail::ScopedDecodeWorkMeter measures: FlateDecode, LZWDecode,
// ASCIIHexDecode, ASCII85Decode, RunLengthDecode or their abbreviations. Image filters (DCT, JPX, JBIG2, CCITT) are
// not: their raster buffers are allocated outside the meter, and they never produce PDF syntax.
[[nodiscard]] bool filters_are_metered_lossless(const PdfDict& stream_dict) noexcept;

namespace detail {

// Measures decoding work on this thread: the output capacity every decoder that can expand its input (Flate, LZW,
// RunLength, ASCIIHex, ASCII85) grows while the meter is in scope, counted whether the decode then succeeds or fails.
// Damaged-file recovery charges its cumulative decoded budget by it, so a container that inflates and then fails
// its checksum still pays for the inflation. Meters nest; an inner meter's count is added to the outer one.
class ScopedDecodeWorkMeter {
public:
	ScopedDecodeWorkMeter() noexcept;
	~ScopedDecodeWorkMeter() noexcept;
	ScopedDecodeWorkMeter(const ScopedDecodeWorkMeter&) = delete;
	ScopedDecodeWorkMeter& operator=(const ScopedDecodeWorkMeter&) = delete;

	[[nodiscard]] uint64_t bytes() const noexcept
	{
		return bytes_;
	}

private:
	uint64_t* previous_{nullptr};
	uint64_t bytes_{0};
};

// Which JPXDecode implementation a decode on this thread may use. WIC is tried first when a JPEG 2000 codec is
// registered, and the native decoder otherwise, so which one ran depends on the machine. Tests pin it while the
// scope lives (and restore the previous choice on exit) so a test of either path cannot silently run the other.
enum class JpxPath : uint8_t {
	Default, // WIC when a codec is registered, else native
	NativeOnly,
	WicOnly, // refused with UnsupportedFeature when no codec is registered
};

class ScopedJpxPath {
public:
	explicit ScopedJpxPath(JpxPath path) noexcept;
	~ScopedJpxPath() noexcept;
	ScopedJpxPath(const ScopedJpxPath&) = delete;
	ScopedJpxPath& operator=(const ScopedJpxPath&) = delete;

private:
	JpxPath previous_{JpxPath::Default};
};

// The WIC steps a decode goes through, for tests that make one of them fail (D06 T05 §37).
enum class WicStage : uint8_t {
	Factory,       // the thread's IWICImagingFactory (COM initialization and creation)
	CreateDecoder, // IWICImagingFactory::CreateDecoderFromStream
	GetFrame,      // IWICBitmapDecoder::GetFrame
	ConvertFormat, // IWICFormatConverter::Initialize
	CopyPixels,    // IWICBitmapSource::CopyPixels (a frame or a converter)
};

// Tests only: while the scope lives, the WIC step `stage` on this thread fails with `hresult` instead of running, so
// each failure class can be exercised at each step on a machine whose codecs never fail. Scopes nest; the previous
// injection returns on exit.
class ScopedWicFailure {
public:
	ScopedWicFailure(WicStage stage, int32_t hresult) noexcept;
	~ScopedWicFailure() noexcept;
	ScopedWicFailure(const ScopedWicFailure&) = delete;
	ScopedWicFailure& operator=(const ScopedWicFailure&) = delete;

private:
	WicStage previous_stage_{WicStage::Factory};
	int32_t previous_hresult_{0};
};

// Test seam for the JPX hand-over's message: sets `error`'s message to `first` followed by the earlier attempt's
// `cause`, falling back to a compact form from `cause`'s numbers when memory has run out.
void compose_message_after(PdfError& error, std::string_view first, const PdfError& cause) noexcept;

// Tests only: while the scope lives, a three-component DCT frame on this thread goes through IWICFormatConverter even
// when WIC already gives 24-bit RGB or BGR, so the conversion step can be exercised (and failed) on any machine.
class ScopedDctForceConverter {
public:
	ScopedDctForceConverter() noexcept;
	~ScopedDctForceConverter() noexcept;
	ScopedDctForceConverter(const ScopedDctForceConverter&) = delete;
	ScopedDctForceConverter& operator=(const ScopedDctForceConverter&) = delete;

private:
	bool previous_{false};
};

// Tests only: how many native JPXDecode decodes this thread has started, so a test can tell whether WIC handed a
// stream over.
[[nodiscard]] uint64_t native_jpx_attempts() noexcept;

} // namespace detail

} // namespace recto::pdf
