// Colour spaces (ISO 32000-1 section 8.6; D01 T02 section 32): the device spaces, the CIE-based `CalGray`, `CalRGB`
// and `Lab` converted to sRGB by their formulas, and `ICCBased` spaces and calibrated `DeviceCMYK` through the Windows
// Color System. A `ColorSpace` is immutable; the transforms Windows builds live in the document's `ColorProfileCache`,
// built and used only on the `DocumentActor` thread. `Indexed`, `Separation` and `DeviceN` (section 33) draw through
// their base or alternate space, Separation and DeviceN through a tint transform `Function`.
#pragma once

#include "display_list.h"
#include "error.h"
#include "function.h"
#include "object.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace recto::pdf {

class Document;

// Bounds on what a hostile document may ask of colour management.
inline constexpr size_t kMaxIccProfileBytes = size_t{16} << 20u;
inline constexpr size_t kMaxColorSpaceDepth = 8u;           // nested spaces: `/Alternate` chains, resources naming resources
inline constexpr size_t kMaxCachedColorTransforms = 64u;    // per document
inline constexpr size_t kMaxCachedProfileBytes = size_t{64} << 20u; // per document
inline constexpr size_t kMaxOutputIntentProfiles = 16u; // distinct output intent profiles tried for DeviceCMYK
inline constexpr size_t kMaxDecodedProfileBytes = size_t{256} << 20u; // all profile decoding per document
// ISO 32000-1 Annex C.2: a DeviceN space has at most 32 colorants, the most components any colour has.
inline constexpr size_t kMaxColorComponents = 32u;
inline constexpr size_t kMaxIndexedLookupBytes = size_t{1} << 20u; // an Indexed lookup stream, decoded
inline constexpr size_t kMaxDeviceNColorants = 64u; // entries read from a DeviceN /Colorants dictionary
// Tint transform tables one interpretation holds: each function object is parsed once per interpretation and shared by
// every space naming it, and distinct ones add up to this; past it a space is unusable, with a diagnostic.
inline constexpr size_t kMaxColorFunctionBytes = size_t{64} << 20u;

using ColorComponents = std::array<double, kMaxColorComponents>;

// ISO 32000-1 8.6.5.8: an intent this reader does not know is RelativeColorimetric.
enum class RenderingIntent : uint8_t {
	AbsoluteColorimetric,
	RelativeColorimetric,
	Saturation,
	Perceptual,
};

[[nodiscard]] RenderingIntent rendering_intent_named(std::string_view name) noexcept;

enum class ColorSpaceKind : uint8_t {
	DeviceGray,
	DeviceRGB,
	DeviceCMYK,
	CalGray,
	CalRGB,
	Lab,
	ICCBased,
	Indexed,
	Separation,
	DeviceN,
};

// An ICC profile as a document holds it: its bytes (header checked) and its number of components. `key` names it in
// the document's cache: its stream's object number, or a hash of its bytes for a profile no object holds.
struct IccProfile {
	std::vector<uint8_t> bytes;
	uint32_t components = 0;
	uint64_t key = 0;
	bool lab = false; // three components whose data are Lab, not RGB

	// The bytes as a profile with `components` components: at most `kMaxIccProfileBytes`, its declared size, `acsp`
	// and its colour space matching (`GRAY`, `RGB ` or `Lab ` for three, `CMYK`); anything else is `DamagedFormat`
	// (`ResourceLimit` past the size bound).
	[[nodiscard]] static PdfResult<std::shared_ptr<const IccProfile>> make(
		std::vector<uint8_t> bytes, uint32_t components, uint64_t key) noexcept;
};

// The Windows transforms of one document, from each profile and intent to sRGB, built once and kept until the document
// closes; at most `kMaxCachedColorTransforms` transforms and `kMaxCachedProfileBytes` of profiles. Only the
// `DocumentActor` thread that owns the document uses it.
class ColorProfileCache {
public:
	ColorProfileCache() noexcept;
	~ColorProfileCache();
	ColorProfileCache(const ColorProfileCache&) = delete;
	ColorProfileCache& operator=(const ColorProfileCache&) = delete;

	// `count` colours of the profile's components (each 0 to 1) to sRGB (0 to 1, three per colour) in one Windows call.
	// False when the profile cannot be used (the profile, the sRGB profile, the transform or the translation failed, or
	// the budget is spent), its diagnostic recorded, and `out` untouched; `OutOfMemory` when memory runs out.
	[[nodiscard]] PdfResult<bool> to_srgb(const IccProfile& profile, RenderingIntent intent, std::span<const double> in,
		size_t count, std::span<double> out) noexcept;

	// The profile `DeviceCMYK` is drawn through: the document's first `/OutputIntents` `/DestOutputProfile` with
	// `/N 4`, else Windows' `RSWOP.icm`, else none (the `§3` formula), read once.
	[[nodiscard]] PdfResult<std::shared_ptr<const IccProfile>> default_cmyk(const Document& document) noexcept;

	[[nodiscard]] size_t transform_count() const noexcept;

	// A profile already read for this document under `key` (a stream's object number), so a space selected again is
	// not decoded again, or null; `remember` keeps one, within the profile byte budget.
	[[nodiscard]] std::shared_ptr<const IccProfile> profile(uint64_t key) const noexcept;
	// A profile stream to decode: false when it was rejected before (never decoded again) or the document's decoding
	// budget is spent; `note_decoded` charges a decode and `reject` marks a stream whose profile cannot be used.
	[[nodiscard]] bool may_decode(uint64_t key) const noexcept;
	void note_decoded(size_t bytes) noexcept;
	void reject(uint64_t key) noexcept;

	// False when the profile does not fit the document's profile budget: it is then not used; `OutOfMemory` when memory
	// runs out keeping it.
	[[nodiscard]] PdfResult<bool> remember(const std::shared_ptr<const IccProfile>& profile) noexcept;

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

// The parameters of a CIE-based space (Tables 63 to 65), in XYZ.
struct CieParams {
	std::array<double, 3> white{0.0, 1.0, 0.0};
	std::array<double, 3> black{0.0, 0.0, 0.0};
	std::array<double, 3> gamma{1.0, 1.0, 1.0};
	std::array<double, 9> matrix{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
	std::array<double, 4> range{-100.0, 100.0, -100.0, 100.0}; // Lab's a* and b*
};

// A colour space, immutable.
class ColorSpace {
public:
	[[nodiscard]] static std::shared_ptr<const ColorSpace> device(ColorSpaceKind kind) noexcept;
	[[nodiscard]] static std::shared_ptr<const ColorSpace> cie(ColorSpaceKind kind, const CieParams& params);
	// `ICCBased`: the profile, its `/Range` (two numbers per component) and the space used when the profile cannot be.
	[[nodiscard]] static std::shared_ptr<const ColorSpace> icc(std::shared_ptr<const IccProfile> profile,
		std::vector<double> range, std::shared_ptr<const ColorSpace> alternate);
	// An image's own profile (a JPX `colr` box) for an image dictionary that names no colour space; a profile that
	// cannot be read gives the device space of its component count, allocation failure `OutOfMemory`.
	[[nodiscard]] static PdfResult<std::shared_ptr<const ColorSpace>> from_icc_profile(
		std::span<const uint8_t> bytes, uint32_t components) noexcept;
	// `Indexed` (8.6.6.3): `hival + 1` palette entries of `base->components()` bytes each in `lookup`, already padded
	// to that length.
	[[nodiscard]] static std::shared_ptr<const ColorSpace> indexed(
		std::shared_ptr<const ColorSpace> base, uint32_t hival, std::vector<uint8_t> lookup);
	// `Separation` (8.6.6.4): one colorant drawn through `tint` into `alternate`.
	[[nodiscard]] static std::shared_ptr<const ColorSpace> separation(
		std::string name, std::shared_ptr<const ColorSpace> alternate, std::shared_ptr<const Function> tint);
	// What a DeviceN `/Attributes` dictionary says (8.6.6.5): an NChannel space, its spot colorants as Separation
	// spaces, and its process space with the names of the process components.
	struct DeviceNAttributes {
		bool nchannel = false;
		std::vector<std::pair<std::string, std::shared_ptr<const ColorSpace>>> colorants;
		std::shared_ptr<const ColorSpace> process;
		std::vector<std::string> process_components;
	};
	// `DeviceN` (8.6.6.5): `names.size()` colorants drawn through `tint` into `alternate`.
	[[nodiscard]] static std::shared_ptr<const ColorSpace> device_n(std::vector<std::string> names,
		std::shared_ptr<const ColorSpace> alternate, std::shared_ptr<const Function> tint, DeviceNAttributes attributes);

	[[nodiscard]] ColorSpaceKind kind() const noexcept
	{
		return kind_;
	}
	[[nodiscard]] size_t components() const noexcept;
	// The initial colour (ISO 32000-1 Table 74).
	[[nodiscard]] ColorComponents initial() const noexcept;
	// The range each component's decoded value spans, for an image's default `/Decode`.
	[[nodiscard]] std::pair<double, double> component_range(size_t component) const noexcept;

	// A colour in sRGB. `document` gives the profile cache (`ICCBased`, calibrated `DeviceCMYK`).
	[[nodiscard]] PdfResult<ColorRgba> rgba(std::span<const double> colour, RenderingIntent intent,
		const Document& document) const noexcept;

	// One image row: `width` pixels of `components()` samples of `bits` (1, 2, 4, 8 or 16) bits, big-endian and packed,
	// mapped through `decode` (two numbers per component, else the space's defaults) and converted to sRGB RGBA8 (alpha
	// 255), one Windows call per row for an ICC space. A row shorter than its samples is `DamagedFormat`.
	[[nodiscard]] PdfResult<std::vector<uint8_t>> convert_samples(std::span<const uint8_t> row, size_t width,
		uint32_t bits, std::span<const double> decode, RenderingIntent intent, const Document& document) const noexcept;

	[[nodiscard]] const ColorSpace* alternate() const noexcept
	{
		return alternate_.get();
	}
	// An Indexed space's base, else null.
	[[nodiscard]] const ColorSpace* base() const noexcept
	{
		return base_.get();
	}
	[[nodiscard]] uint32_t hival() const noexcept
	{
		return hival_;
	}
	// Separation's one colorant name, DeviceN's names; empty for every other space.
	[[nodiscard]] std::span<const std::string> colorants() const noexcept
	{
		return names_;
	}
	[[nodiscard]] bool nchannel() const noexcept
	{
		return attributes_.nchannel;
	}
	[[nodiscard]] const ColorSpace* process() const noexcept
	{
		return attributes_.process.get();
	}
	[[nodiscard]] std::span<const std::string> process_components() const noexcept
	{
		return attributes_.process_components;
	}
	[[nodiscard]] std::span<const std::pair<std::string, std::shared_ptr<const ColorSpace>>> colorant_spaces() const noexcept
	{
		return attributes_.colorants;
	}
	// False for a space that marks nothing: a Separation named `/None`, a DeviceN whose every colorant is `/None`, and
	// an Indexed space over either. Fills, strokes, text, stencil masks and images in such a space are not drawn.
	[[nodiscard]] bool paints() const noexcept;

private:
	ColorSpace() = default;
	explicit ColorSpace(ColorSpaceKind kind) noexcept
		: kind_(kind)
	{
	}
	// The colours of a run of decoded components in sRGB, three per colour.
	[[nodiscard]] PdfResult<void> to_srgb(std::span<const double> in, size_t count, RenderingIntent intent,
		const Document& document, std::span<double> out) const noexcept;

	ColorSpaceKind kind_ = ColorSpaceKind::DeviceGray;
	CieParams cie_{};
	std::shared_ptr<const IccProfile> profile_{};
	std::vector<double> range_{};
	std::shared_ptr<const ColorSpace> alternate_{};
	// Indexed.
	std::shared_ptr<const ColorSpace> base_{};
	uint32_t hival_ = 0;
	std::vector<uint8_t> lookup_{};
	// Separation and DeviceN.
	std::vector<std::string> names_{};
	std::shared_ptr<const Function> tint_{};
	DeviceNAttributes attributes_{};
};

// Colour space values resolved for one interpretation (ISO 32000-1 8.6.3 to 8.6.5.6): device names (their full names;
// an inline image's abbreviations are expanded by its reader, section 4, before they reach here) (through the
// `/Default*` spaces of `resources`, except inside their own definitions), `/ColorSpace` resource names, and arrays;
// at most `kMaxColorSpaceDepth` nested spaces, a cycle read as the device space of its component count (DeviceGray
// when none is known). `Pattern` (section 16) and a space that cannot be used (an unknown family, a missing lookup, an
// unusable tint transform) are null.
class ColorSpaces {
public:
	ColorSpaces(const Document& document, const PdfDict* color_space_resources) noexcept;

	// A device space, or the `/Default*` space that replaces it.
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> device(ColorSpaceKind kind) noexcept;
	// The space `cs` names.
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> named(std::string_view name) noexcept;
	// A space given as a value (a name or an array, an image's `/ColorSpace`).
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> resolve(const PdfValue& value) noexcept;

private:
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> resolve_at(
		const PdfValue& value, size_t depth, bool defaults) noexcept;
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> device_at(ColorSpaceKind kind, size_t depth, bool defaults) noexcept;
	// Section 33's families, called inside `resolve_at`'s allocation guard.
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> indexed_at(const PdfArray& array, size_t depth, bool defaults);
	[[nodiscard]] PdfResult<std::shared_ptr<const ColorSpace>> spot_at(
		const PdfArray& array, bool device_n, size_t depth, bool defaults);
	// A tint transform: the one this interpretation already parsed from that object, or a new one within
	// `kMaxColorFunctionBytes`; null when it cannot be used (its diagnostic recorded).
	[[nodiscard]] PdfResult<std::shared_ptr<const Function>> tint_transform(const PdfValue& value);

	const Document& document_;
	const PdfDict* resources_;
	std::vector<uint64_t> visiting_; // the indirect objects and resource names on the current resolution path
	// What `named` and `device` gave, so selecting a space again returns the same space (and its decoded profile).
	std::vector<std::pair<uint64_t, std::shared_ptr<const ColorSpace>>> resolved_;
	// Tint transforms parsed in this interpretation, by object number, and the bytes their tables hold.
	std::vector<std::pair<uint64_t, std::shared_ptr<const Function>>> functions_;
	size_t function_bytes_ = 0;
	// Allocation failure met while resolving a value, which the public call returns rather than a fallback.
	std::optional<PdfError> fatal_;
	[[nodiscard]] std::optional<PdfValue> value_of(const PdfValue& value);
};

namespace detail {

// Test hooks: a Windows step made to fail, the `TranslateColors` calls made, and the default CMYK profile switched off.
enum class IccFailureForTesting : uint8_t {
	None,
	SrgbProfile,
	Transform,
	Translate,
};
void set_icc_failure_for_testing(IccFailureForTesting failure) noexcept;
[[nodiscard]] size_t translate_calls_for_testing() noexcept;
void set_default_cmyk_profile_enabled_for_testing(bool enabled) noexcept;
[[nodiscard]] size_t icc_profile_decodes_for_testing() noexcept;

} // namespace detail

} // namespace recto::pdf
