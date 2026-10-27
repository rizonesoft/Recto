// Colour spaces (ISO 32000-1 section 8.6; D01 T02 section 32).
#include "color.h"

#include "document.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <icm.h>

#pragma comment(lib, "mscms.lib")

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <new>
#include <set>
#include <string>
#include <utility>

namespace recto::pdf {

namespace {

std::atomic<detail::IccFailureForTesting> g_icc_failure{detail::IccFailureForTesting::None};
std::atomic<size_t> g_translate_calls{0u};
std::atomic<bool> g_default_cmyk_enabled{true};
std::atomic<size_t> g_profile_decodes{0u};

[[nodiscard]] float unit(double v) noexcept
{
	return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

// The `§3` device conversions: gray and RGB as given, CMYK by `1 - min(1, c + k)` per channel.
[[nodiscard]] std::array<double, 3> device_rgb(ColorSpaceKind kind, std::span<const double> c) noexcept
{
	switch (kind) {
	case ColorSpaceKind::DeviceGray:
		return {c[0], c[0], c[0]};
	case ColorSpaceKind::DeviceCMYK: {
		const double k = std::clamp(c[3], 0.0, 1.0);
		return {1.0 - std::min(1.0, std::clamp(c[0], 0.0, 1.0) + k), 1.0 - std::min(1.0, std::clamp(c[1], 0.0, 1.0) + k),
			1.0 - std::min(1.0, std::clamp(c[2], 0.0, 1.0) + k)};
	}
	default:
		return {c[0], c[1], c[2]};
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// CIE XYZ to sRGB (IEC 61966-2-1), Bradford adaptation to D65
// ---------------------------------------------------------------------------------------------------------------------

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<double, 9>; // row-major

constexpr Vec3 kD65 = {0.95047, 1.0, 1.08883};
constexpr Mat3 kBradford = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
constexpr Mat3 kBradfordInverse = {0.9869929, -0.1470543, 0.1599627, 0.4323053, 0.5183603, 0.0492912, -0.0085287, 0.0400428, 0.9684867};
// IEC 61966-2-1's XYZ (D65) to linear sRGB, at the precision of its primaries.
constexpr Mat3 kXyzToLinearSrgb = {3.2404542, -1.5371385, -0.4985314, -0.9692660, 1.8760108, 0.0415560, 0.0556434, -0.2040259, 1.0572252};

[[nodiscard]] Vec3 multiply(const Mat3& m, const Vec3& v) noexcept
{
	return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
		m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

[[nodiscard]] double srgb_encode(double linear) noexcept
{
	const double v = std::clamp(linear, 0.0, 1.0);
	return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

// XYZ relative to `white` to sRGB, the black point mapped to black first (a linear black point scaling: each component
// moved from [black, white] to [0, white]).
[[nodiscard]] Vec3 xyz_to_srgb(Vec3 xyz, const Vec3& white, const Vec3& black) noexcept
{
	for (size_t i = 0; i < 3; ++i) {
		if (black[i] > 0.0 && black[i] < white[i]) {
			xyz[i] = (xyz[i] - black[i]) * white[i] / (white[i] - black[i]);
		}
	}
	// Bradford: the cone responses scaled from the space's white to D65.
	const Vec3 source = multiply(kBradford, white);
	const Vec3 target = multiply(kBradford, kD65);
	Vec3 cone = multiply(kBradford, xyz);
	for (size_t i = 0; i < 3; ++i) {
		cone[i] *= source[i] != 0.0 ? target[i] / source[i] : 1.0;
	}
	const Vec3 adapted = multiply(kBradfordInverse, cone);
	const Vec3 linear = multiply(kXyzToLinearSrgb, adapted);
	return {srgb_encode(linear[0]), srgb_encode(linear[1]), srgb_encode(linear[2])};
}

[[nodiscard]] double safe_pow(double base, double exponent) noexcept
{
	return base <= 0.0 ? 0.0 : std::pow(base, exponent);
}

// ISO 32000-1 8.6.5.2 to 8.6.5.4.
[[nodiscard]] Vec3 cie_to_srgb(ColorSpaceKind kind, const CieParams& p, std::span<const double> c) noexcept
{
	const Vec3 white{p.white[0], p.white[1], p.white[2]};
	const Vec3 black{p.black[0], p.black[1], p.black[2]};
	Vec3 xyz{};
	if (kind == ColorSpaceKind::CalGray) {
		const double y = safe_pow(std::clamp(c[0], 0.0, 1.0), p.gamma[0]);
		xyz = {white[0] * y, white[1] * y, white[2] * y};
	} else if (kind == ColorSpaceKind::CalRGB) {
		const double a = safe_pow(std::clamp(c[0], 0.0, 1.0), p.gamma[0]);
		const double b = safe_pow(std::clamp(c[1], 0.0, 1.0), p.gamma[1]);
		const double cc = safe_pow(std::clamp(c[2], 0.0, 1.0), p.gamma[2]);
		const auto& m = p.matrix; // [XA YA ZA XB YB ZB XC YC ZC]
		xyz = {m[0] * a + m[3] * b + m[6] * cc, m[1] * a + m[4] * b + m[7] * cc, m[2] * a + m[5] * b + m[8] * cc};
	} else {
		const double l = std::clamp(c[0], 0.0, 100.0);
		const double astar = std::clamp(c[1], p.range[0], p.range[1]);
		const double bstar = std::clamp(c[2], p.range[2], p.range[3]);
		const double m = (l + 16.0) / 116.0;
		const double lp = m + astar / 500.0;
		const double n = m - bstar / 200.0;
		const auto g = [](double x) { return x >= 6.0 / 29.0 ? x * x * x : (108.0 / 841.0) * (x - 4.0 / 29.0); };
		xyz = {white[0] * g(lp), white[1] * g(m), white[2] * g(n)};
	}
	return xyz_to_srgb(xyz, white, black);
}

// ---------------------------------------------------------------------------------------------------------------------
// Profiles and the Windows Color System
// ---------------------------------------------------------------------------------------------------------------------

[[nodiscard]] uint32_t be32(std::span<const uint8_t> b, size_t at) noexcept
{
	return (uint32_t{b[at]} << 24u) | (uint32_t{b[at + 1u]} << 16u) | (uint32_t{b[at + 2u]} << 8u) | b[at + 3u];
}

[[nodiscard]] uint64_t bytes_key(std::span<const uint8_t> bytes) noexcept
{
	uint64_t h = 0xCBF29CE484222325ull; // FNV-1a, with the top bit set so no object number can equal it
	for (const uint8_t b : bytes) {
		h = (h ^ b) * 0x100000001B3ull;
	}
	return h | (uint64_t{1} << 63u);
}

[[nodiscard]] PdfResult<bool> unusable(std::string_view why) noexcept
{
	(void)make_pdf_error(ErrorCode::DamagedFormat, why); // records the diagnostic
	return false;
}

[[nodiscard]] DWORD win_intent(RenderingIntent intent) noexcept
{
	switch (intent) {
	case RenderingIntent::Perceptual:
		return INTENT_PERCEPTUAL;
	case RenderingIntent::Saturation:
		return INTENT_SATURATION;
	case RenderingIntent::AbsoluteColorimetric:
		return INTENT_ABSOLUTE_COLORIMETRIC;
	case RenderingIntent::RelativeColorimetric:
		break;
	}
	return INTENT_RELATIVE_COLORIMETRIC;
}

[[nodiscard]] std::optional<std::vector<uint8_t>> read_file(const std::wstring& path, size_t limit)
{
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	if (!in.good()) {
		return std::nullopt;
	}
	const std::streamoff size = in.tellg();
	if (size <= 0 || static_cast<uint64_t>(size) > limit) {
		return std::nullopt;
	}
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	in.seekg(0);
	in.read(reinterpret_cast<char*>(bytes.data()), size);
	return in.good() ? std::optional(std::move(bytes)) : std::nullopt;
}

} // namespace

RenderingIntent rendering_intent_named(std::string_view name) noexcept
{
	if (name == "AbsoluteColorimetric") {
		return RenderingIntent::AbsoluteColorimetric;
	}
	if (name == "Saturation") {
		return RenderingIntent::Saturation;
	}
	if (name == "Perceptual") {
		return RenderingIntent::Perceptual;
	}
	return RenderingIntent::RelativeColorimetric;
}

PdfResult<std::shared_ptr<const IccProfile>> IccProfile::make(
	std::vector<uint8_t> bytes, uint32_t components, uint64_t key) noexcept
{
	try {
		if (bytes.size() > kMaxIccProfileBytes) {
			return make_pdf_error(ErrorCode::ResourceLimit, "ICC profile exceeds its size limit");
		}
		if (bytes.size() < 132u) {
			return make_pdf_error(ErrorCode::DamagedFormat, "ICC profile is shorter than its header");
		}
		const uint32_t declared = be32(bytes, 0u);
		if (declared < 132u || declared > bytes.size() || std::memcmp(bytes.data() + 36, "acsp", 4u) != 0) {
			return make_pdf_error(ErrorCode::DamagedFormat, "ICC profile header is malformed");
		}
		const auto space = std::string_view(reinterpret_cast<const char*>(bytes.data() + 16), 4u);
		const bool lab = components == 3u && space == "Lab ";
		const bool matches = (components == 1u && space == "GRAY") || (components == 3u && space == "RGB ") || lab ||
			(components == 4u && space == "CMYK");
		if (!matches) {
			return make_pdf_error(ErrorCode::DamagedFormat, "ICC profile colour space does not match /N");
		}
		auto profile = std::make_shared<IccProfile>();
		// A copy of exactly the declared bytes, so padding after them is never kept (a shrunk vector keeps its
		// capacity, which the document's budget would not see).
		profile->bytes.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(declared));
		profile->components = components;
		profile->lab = lab;
		profile->key = key != 0u ? key : bytes_key(profile->bytes);
		return std::shared_ptr<const IccProfile>(std::move(profile));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading an ICC profile");
	}
}

struct ColorProfileCache::Impl {
	std::map<std::pair<uint64_t, uint8_t>, HTRANSFORM> transforms; // a null transform: it could not be built
	std::map<uint64_t, std::shared_ptr<const IccProfile>> read;    // profiles decoded, by stream object number
	size_t read_bytes = 0;
	std::set<uint64_t> rejected; // profile streams whose profile cannot be used: not decoded again
	size_t decoded_bytes = 0;    // all profile decoding, against `kMaxDecodedProfileBytes`
	std::set<uint64_t> profiles;
	size_t profile_bytes = 0;
	bool default_read = false;
	std::shared_ptr<const IccProfile> default_cmyk;

	~Impl()
	{
		for (const auto& [key, transform] : transforms) {
			if (transform != nullptr) {
				DeleteColorTransform(transform);
			}
		}
	}

	[[nodiscard]] HTRANSFORM build(const IccProfile& profile, RenderingIntent intent) noexcept
	{
		if (g_icc_failure.load() == detail::IccFailureForTesting::SrgbProfile) {
			return nullptr;
		}
		std::array<wchar_t, MAX_PATH + 1> srgb_path{};
		DWORD srgb_size = static_cast<DWORD>(srgb_path.size() * sizeof(wchar_t));
		if (!GetStandardColorSpaceProfileW(nullptr, LCS_sRGB, srgb_path.data(), &srgb_size)) {
			return nullptr;
		}
		PROFILE source{PROFILE_MEMBUFFER, const_cast<uint8_t*>(profile.bytes.data()), static_cast<DWORD>(profile.bytes.size())};
		PROFILE target{PROFILE_FILENAME, srgb_path.data(),
			static_cast<DWORD>((std::wcslen(srgb_path.data()) + 1u) * sizeof(wchar_t))};
		HPROFILE handles[2] = {OpenColorProfileW(&source, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING),
			OpenColorProfileW(&target, PROFILE_READ, FILE_SHARE_READ, OPEN_EXISTING)};
		HTRANSFORM transform = nullptr;
		if (handles[0] != nullptr && handles[1] != nullptr &&
			g_icc_failure.load() != detail::IccFailureForTesting::Transform) {
			// One intent per profile: the source profile's table for that intent, and the same for sRGB.
			DWORD intents[2] = {win_intent(intent), win_intent(intent)};
			transform = CreateMultiProfileTransform(handles, 2u, intents, 2u, BEST_MODE, INDEX_DONT_CARE);
		}
		for (HPROFILE h : handles) {
			if (h != nullptr) {
				CloseColorProfile(h);
			}
		}
		return transform;
	}
};

ColorProfileCache::ColorProfileCache() noexcept = default;
ColorProfileCache::~ColorProfileCache() = default;

size_t ColorProfileCache::transform_count() const noexcept
{
	if (impl_ == nullptr) {
		return 0u;
	}
	return static_cast<size_t>(std::count_if(impl_->transforms.begin(), impl_->transforms.end(),
		[](const auto& entry) { return entry.second != nullptr; }));
}

PdfResult<bool> ColorProfileCache::to_srgb(const IccProfile& profile, RenderingIntent intent, std::span<const double> in,
	size_t count, std::span<double> out) noexcept
{
	try {
		if (impl_ == nullptr) {
			impl_ = std::make_unique<Impl>();
		}
		const std::pair<uint64_t, uint8_t> key{profile.key, static_cast<uint8_t>(intent)};
		auto it = impl_->transforms.find(key);
		if (it == impl_->transforms.end()) {
			const bool new_profile = !impl_->profiles.contains(profile.key);
			// Every build attempted counts, a failed one too, so profiles Windows refuses cannot grow the cache.
			if (impl_->transforms.size() >= kMaxCachedColorTransforms ||
				(new_profile && impl_->profile_bytes + profile.bytes.size() > kMaxCachedProfileBytes)) {
				(void)make_pdf_error(ErrorCode::ResourceLimit, "Colour transforms past the document's limit"); // recorded
				return false;
			}
			if (new_profile) {
				impl_->profiles.insert(profile.key);
				impl_->profile_bytes += profile.bytes.size();
			}
			// The entry first, so nothing allocates (or can throw) while Windows holds a transform no entry owns.
			it = impl_->transforms.emplace(key, nullptr).first;
			HTRANSFORM transform = impl_->build(profile, intent);
			it->second = transform;
			if (transform == nullptr) {
				return unusable("ICC profile or its transform to sRGB could not be built");
			}
		}
		if (it->second == nullptr) {
			return false; // its diagnostic was recorded when the build failed
		}
		COLORTYPE type = COLOR_GRAY;
		if (profile.lab) {
			type = COLOR_Lab;
		} else if (profile.components == 3u) {
			type = COLOR_RGB;
		} else if (profile.components == 4u) {
			type = COLOR_CMYK;
		}
		std::vector<COLOR> source(count);
		std::vector<COLOR> target(count);
		const auto word = [](double v) { return static_cast<WORD>(std::lround(std::clamp(v, 0.0, 1.0) * 65535.0)); };
		for (size_t i = 0; i < count; ++i) {
			const double* c = in.data() + i * profile.components;
			if (type == COLOR_GRAY) {
				source[i].gray.gray = word(c[0]);
			} else if (type == COLOR_Lab) {
				source[i].Lab.L = word(c[0]);
				source[i].Lab.a = word(c[1]);
				source[i].Lab.b = word(c[2]);
			} else if (type == COLOR_RGB) {
				source[i].rgb.red = word(c[0]);
				source[i].rgb.green = word(c[1]);
				source[i].rgb.blue = word(c[2]);
			} else {
				source[i].cmyk.cyan = word(c[0]);
				source[i].cmyk.magenta = word(c[1]);
				source[i].cmyk.yellow = word(c[2]);
				source[i].cmyk.black = word(c[3]);
			}
		}
		g_translate_calls.fetch_add(1u);
		if (g_icc_failure.load() == detail::IccFailureForTesting::Translate ||
			!TranslateColors(it->second, source.data(), static_cast<DWORD>(count), type, target.data(), COLOR_RGB)) {
			return unusable("ICC colour translation failed");
		}
		for (size_t i = 0; i < count; ++i) {
			out[3u * i] = target[i].rgb.red / 65535.0;
			out[3u * i + 1u] = target[i].rgb.green / 65535.0;
			out[3u * i + 2u] = target[i].rgb.blue / 65535.0;
		}
		return true;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory converting colours");
	}
}

bool ColorProfileCache::may_decode(uint64_t key) const noexcept
{
	if (impl_ == nullptr) {
		return true;
	}
	return !impl_->rejected.contains(key) && impl_->decoded_bytes < kMaxDecodedProfileBytes;
}

void ColorProfileCache::note_decoded(size_t bytes) noexcept
{
	try {
		if (impl_ == nullptr) {
			impl_ = std::make_unique<Impl>();
		}
		impl_->decoded_bytes += bytes;
	} catch (const std::bad_alloc&) {
		// nothing to charge against: the next decode creates the state again
	}
}

void ColorProfileCache::reject(uint64_t key) noexcept
{
	try {
		if (impl_ == nullptr) {
			impl_ = std::make_unique<Impl>();
		}
		if (impl_->rejected.size() < 65'536u) {
			impl_->rejected.insert(key);
		}
	} catch (const std::bad_alloc&) {
		// not remembered: it is tried again, within the decoding budget
	}
}

std::shared_ptr<const IccProfile> ColorProfileCache::profile(uint64_t key) const noexcept
{
	if (impl_ == nullptr) {
		return nullptr;
	}
	const auto it = impl_->read.find(key);
	return it != impl_->read.end() ? it->second : nullptr;
}

PdfResult<bool> ColorProfileCache::remember(const std::shared_ptr<const IccProfile>& profile) noexcept
{
	try {
		if (impl_ == nullptr) {
			impl_ = std::make_unique<Impl>();
		}
		if (impl_->read.contains(profile->key)) {
			return true;
		}
		if (impl_->read_bytes + profile->bytes.size() > kMaxCachedProfileBytes) {
			return false;
		}
		impl_->read.emplace(profile->key, profile);
		impl_->read_bytes += profile->bytes.size();
		return true;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory keeping an ICC profile");
	}
}

PdfResult<std::shared_ptr<const IccProfile>> ColorProfileCache::default_cmyk(const Document& document) noexcept
{
	try {
		if (impl_ == nullptr) {
			impl_ = std::make_unique<Impl>();
		}
		if (!g_default_cmyk_enabled.load()) {
			return std::shared_ptr<const IccProfile>{};
		}
		if (impl_->default_read) {
			return impl_->default_cmyk;
		}
		// The first output intent with a four-component profile; the lookup is marked complete only when it completes.
		if (const PdfValue* intents = document.catalog().get("OutputIntents")) {
			auto list = document.resolve_value(*intents);
			if (!list.has_value() && list.error().code == ErrorCode::OutOfMemory) {
				return list.error();
			}
			if (list.has_value() && list->is_array()) {
				// Bounded work: each profile stream is tried once however often the intents name it, at most
				// `kMaxOutputIntentProfiles` distinct ones, their decoded bytes within the profile budget.
				std::set<uint32_t> tried;
				size_t examined = 0;
				size_t decoded_bytes = 0;
				for (const PdfValue& item : *list->as_array()) {
					if (examined >= kMaxOutputIntentProfiles || decoded_bytes >= kMaxCachedProfileBytes) {
						break;
					}
					auto intent = document.resolve_value(item);
					if (!intent.has_value() && intent.error().code == ErrorCode::OutOfMemory) {
						return intent.error();
					}
					if (!intent.has_value() || !intent->is_dict()) {
						continue;
					}
					const PdfValue* dest = intent->as_dict()->get("DestOutputProfile");
					const std::optional<IndirectRef> ref = dest != nullptr ? dest->as_ref() : std::nullopt;
					if (ref.has_value() && !tried.insert(ref->obj_num).second) {
						continue; // this profile was tried already
					}
					auto stream = dest != nullptr ? document.resolve_value(*dest) : PdfResult<PdfValue>(PdfValue{});
					if (!stream.has_value() && stream.error().code == ErrorCode::OutOfMemory) {
						return stream.error();
					}
					if (!stream.has_value() || !stream->is_stream()) {
						continue;
					}
					const PdfValue* n_entry = stream->as_stream()->dict.get("N");
					auto n_value = n_entry != nullptr ? document.resolve_value(*n_entry) : PdfResult<PdfValue>(PdfValue{});
					if (!n_value.has_value() && n_value.error().code == ErrorCode::OutOfMemory) {
						return n_value.error();
					}
					if (!n_value.has_value() || n_value->as_int() != 4) {
						continue;
					}
					++examined;
					g_profile_decodes.fetch_add(1u);
					auto bytes = document.decode_stream(*stream->as_stream(), kMaxIccProfileBytes);
					if (!bytes.has_value()) {
						if (bytes.error().code == ErrorCode::OutOfMemory) {
							return bytes.error();
						}
						continue;
					}
					decoded_bytes += bytes->size();
					auto profile = IccProfile::make(std::move(*bytes), 4u, ref.has_value() ? ref->obj_num : 0u);
					if (profile.has_value()) {
						impl_->default_cmyk = std::move(*profile);
						impl_->default_read = true;
						return impl_->default_cmyk;
					}
					if (profile.error().code == ErrorCode::OutOfMemory) {
						return profile.error();
					}
				}
			}
		}
		// Windows' RSWOP.icm (a recorded default) in the colour directory, `System32\spool\drivers\color`.
		std::array<wchar_t, MAX_PATH + 1> dir{};
		const UINT length = GetSystemDirectoryW(dir.data(), static_cast<UINT>(dir.size()));
		if (length > 0u && length < dir.size()) {
			if (auto bytes = read_file(std::wstring(dir.data()) + L"\\spool\\drivers\\color\\RSWOP.icm", kMaxIccProfileBytes)) {
				auto profile = IccProfile::make(std::move(*bytes), 4u, 0u);
				if (profile.has_value()) {
					impl_->default_cmyk = std::move(*profile);
				} else if (profile.error().code == ErrorCode::OutOfMemory) {
					return profile.error();
				}
			}
		}
		impl_->default_read = true;
		return impl_->default_cmyk;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading the default CMYK profile");
	}
}

ColorProfileCache& Document::color_profiles() const
{
	if (color_profiles_ == nullptr) {
		color_profiles_ = std::make_shared<ColorProfileCache>();
	}
	return *color_profiles_;
}

// ---------------------------------------------------------------------------------------------------------------------
// ColorSpace
// ---------------------------------------------------------------------------------------------------------------------

std::shared_ptr<const ColorSpace> ColorSpace::device(ColorSpaceKind kind) noexcept
{
	// Static spaces shared through the aliasing constructor, which owns and allocates nothing, so no use can throw.
	static const ColorSpace kGray(ColorSpaceKind::DeviceGray);
	static const ColorSpace kRgb(ColorSpaceKind::DeviceRGB);
	static const ColorSpace kCmyk(ColorSpaceKind::DeviceCMYK);
	const ColorSpace* space = kind == ColorSpaceKind::DeviceCMYK ? &kCmyk : kind == ColorSpaceKind::DeviceRGB ? &kRgb : &kGray;
	return std::shared_ptr<const ColorSpace>(std::shared_ptr<const ColorSpace>{}, space);
}

std::shared_ptr<const ColorSpace> ColorSpace::cie(ColorSpaceKind kind, const CieParams& params)
{
	auto space = std::shared_ptr<ColorSpace>(new ColorSpace());
	space->kind_ = kind;
	space->cie_ = params;
	return space;
}

std::shared_ptr<const ColorSpace> ColorSpace::icc(
	std::shared_ptr<const IccProfile> profile, std::vector<double> range, std::shared_ptr<const ColorSpace> alternate)
{
	auto space = std::shared_ptr<ColorSpace>(new ColorSpace());
	space->kind_ = ColorSpaceKind::ICCBased;
	space->profile_ = std::move(profile);
	space->range_ = std::move(range);
	space->alternate_ = std::move(alternate);
	return space;
}

std::shared_ptr<const ColorSpace> ColorSpace::indexed(
	std::shared_ptr<const ColorSpace> base, uint32_t hival, std::vector<uint8_t> lookup)
{
	auto space = std::shared_ptr<ColorSpace>(new ColorSpace());
	space->kind_ = ColorSpaceKind::Indexed;
	space->base_ = std::move(base);
	space->hival_ = hival;
	space->lookup_ = std::move(lookup);
	return space;
}

std::shared_ptr<const ColorSpace> ColorSpace::separation(
	std::string name, std::shared_ptr<const ColorSpace> alternate, std::shared_ptr<const Function> tint)
{
	auto space = std::shared_ptr<ColorSpace>(new ColorSpace());
	space->kind_ = ColorSpaceKind::Separation;
	space->names_.push_back(std::move(name));
	space->alternate_ = std::move(alternate);
	space->tint_ = std::move(tint);
	return space;
}

std::shared_ptr<const ColorSpace> ColorSpace::device_n(std::vector<std::string> names,
	std::shared_ptr<const ColorSpace> alternate, std::shared_ptr<const Function> tint, DeviceNAttributes attributes)
{
	auto space = std::shared_ptr<ColorSpace>(new ColorSpace());
	space->kind_ = ColorSpaceKind::DeviceN;
	space->names_ = std::move(names);
	space->alternate_ = std::move(alternate);
	space->tint_ = std::move(tint);
	space->attributes_ = std::move(attributes);
	return space;
}

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpace::from_icc_profile(
	std::span<const uint8_t> bytes, uint32_t components) noexcept
{
	try {
		const ColorSpaceKind device_kind = components == 1u ? ColorSpaceKind::DeviceGray
			: components == 4u ? ColorSpaceKind::DeviceCMYK
							   : ColorSpaceKind::DeviceRGB;
		auto profile = IccProfile::make(std::vector<uint8_t>(bytes.begin(), bytes.end()), components, 0u);
		if (!profile.has_value()) {
			if (profile.error().code == ErrorCode::OutOfMemory) {
				return profile.error();
			}
			return device(device_kind);
		}
		std::vector<double> range;
		if ((*profile)->lab) {
			range = {0.0, 100.0, -128.0, 127.0, -128.0, 127.0};
		}
		return icc(std::move(*profile), std::move(range), device(device_kind));
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory reading an image's ICC profile");
	}
}

size_t ColorSpace::components() const noexcept
{
	switch (kind_) {
	case ColorSpaceKind::DeviceGray:
	case ColorSpaceKind::CalGray:
	case ColorSpaceKind::Indexed:
	case ColorSpaceKind::Separation:
		return 1u;
	case ColorSpaceKind::DeviceCMYK:
		return 4u;
	case ColorSpaceKind::ICCBased:
		return profile_ != nullptr ? profile_->components : 3u;
	case ColorSpaceKind::DeviceN:
		return names_.size();
	default:
		return 3u;
	}
}

ColorComponents ColorSpace::initial() const noexcept
{
	ColorComponents out{};
	if (kind_ == ColorSpaceKind::DeviceCMYK) {
		out[3] = 1.0;
	} else if (kind_ == ColorSpaceKind::Lab) {
		// Table 74: L*, a* and b* of 0, each within its range.
		out[1] = std::clamp(0.0, cie_.range[0], cie_.range[1]);
		out[2] = std::clamp(0.0, cie_.range[2], cie_.range[3]);
	} else if (kind_ == ColorSpaceKind::ICCBased) {
		for (size_t i = 0; i < components() && i < out.size(); ++i) {
			out[i] = std::clamp(0.0, component_range(i).first, component_range(i).second);
		}
	} else if (kind_ == ColorSpaceKind::Separation || kind_ == ColorSpaceKind::DeviceN) {
		// Table 74: every tint 1.0, the full colorant.
		for (size_t i = 0; i < components() && i < out.size(); ++i) {
			out[i] = 1.0;
		}
	}
	return out; // Indexed: index 0
}

bool ColorSpace::paints() const noexcept
{
	switch (kind_) {
	case ColorSpaceKind::Indexed:
		return base_ == nullptr || base_->paints();
	case ColorSpaceKind::Separation:
	case ColorSpaceKind::DeviceN:
		return std::any_of(names_.begin(), names_.end(), [](const std::string& name) { return name != "None"; });
	default:
		return true;
	}
}

std::pair<double, double> ColorSpace::component_range(size_t component) const noexcept
{
	if (kind_ == ColorSpaceKind::Lab) {
		if (component == 0u) {
			return {0.0, 100.0};
		}
		return component == 1u ? std::pair{cie_.range[0], cie_.range[1]} : std::pair{cie_.range[2], cie_.range[3]};
	}
	if (kind_ == ColorSpaceKind::ICCBased && range_.size() >= 2u * (component + 1u)) {
		return {range_[2u * component], range_[2u * component + 1u]};
	}
	if (kind_ == ColorSpaceKind::Indexed) {
		return {0.0, static_cast<double>(hival_)};
	}
	return {0.0, 1.0};
}

PdfResult<void> ColorSpace::to_srgb(
	std::span<const double> in, size_t count, RenderingIntent intent, const Document& document, std::span<double> out) const noexcept
{
	const size_t n = components();
	if (kind_ == ColorSpaceKind::Indexed) {
		// Each index rounded and clamped to 0 to hival, its palette entry's bytes scaled to the base's ranges, and the
		// whole run converted by the base in one call (one Windows call for an ICC base).
		const size_t base_n = base_->components();
		std::vector<double> entries;
		try {
			entries.resize(count * base_n);
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory converting colours");
		}
		for (size_t i = 0; i < count; ++i) {
			const double index = std::isfinite(in[i]) ? std::round(in[i]) : 0.0;
			const auto entry = static_cast<size_t>(std::clamp(index, 0.0, static_cast<double>(hival_)));
			for (size_t j = 0; j < base_n; ++j) {
				const auto [lo, hi] = base_->component_range(j);
				entries[i * base_n + j] = lo + lookup_[entry * base_n + j] / 255.0 * (hi - lo);
			}
		}
		return base_->to_srgb(entries, count, intent, document, out);
	}
	if (kind_ == ColorSpaceKind::Separation && names_.front() == "All") {
		// 8.6.6.4: `/All` applies its tint to every colorant at once and its alternate space and tint transform are
		// ignored; on a composite display every colorant at tint t is gray 1 - t (a registration mark is black).
		for (size_t i = 0; i < count; ++i) {
			const double t = std::isfinite(in[i]) ? std::clamp(in[i], 0.0, 1.0) : 0.0;
			out[3u * i] = out[3u * i + 1u] = out[3u * i + 2u] = 1.0 - t;
		}
		return {};
	}
	if (kind_ == ColorSpaceKind::Separation || kind_ == ColorSpaceKind::DeviceN) {
		// Each colour's tints, clipped to 0 to 1, through the tint transform into the alternate space, converted there
		// in one call. A failed transform gives its lowest values, recorded once for the run.
		const size_t alt_n = alternate_->components();
		std::vector<double> alternates;
		try {
			alternates.resize(count * alt_n);
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory converting colours");
		}
		ColorComponents tints{};
		std::array<double, kMaxFunctionOutputs> result{};
		bool failed = false;
		for (size_t i = 0; i < count; ++i) {
			for (size_t c = 0; c < n; ++c) {
				const double t = in[i * n + c];
				tints[c] = std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : 0.0;
			}
			failed = !tint_->evaluate(std::span<const double>(tints).first(n), result) || failed;
			std::copy_n(result.begin(), alt_n, alternates.begin() + static_cast<std::ptrdiff_t>(i * alt_n));
		}
		if (failed) {
			(void)make_pdf_error(ErrorCode::DamagedFormat, "A tint transform failed; its colours take its lowest values");
		}
		return alternate_->to_srgb(alternates, count, intent, document, out);
	}
	ColorProfileCache* cache_slot = nullptr;
	try {
		cache_slot = &document.color_profiles();
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory creating the colour profile cache");
	}
	ColorProfileCache& cache = *cache_slot;
	const IccProfile* profile = profile_.get();
	std::shared_ptr<const IccProfile> cmyk;
	if (kind_ == ColorSpaceKind::DeviceCMYK) {
		RECTO_PDF_TRY(cmyk, cache.default_cmyk(document));
		profile = cmyk.get();
	}
	if (profile != nullptr) {
		// `/Range` scaled to the profile's 0 to 1.
		std::vector<double> scaled;
		try {
			scaled.assign(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(count * n));
		} catch (const std::bad_alloc&) {
			return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory converting colours");
		}
		for (size_t i = 0; i < scaled.size(); ++i) {
			const auto [lo, hi] = component_range(i % n);
			scaled[i] = hi > lo ? (std::clamp(scaled[i], lo, hi) - lo) / (hi - lo) : 0.0;
		}
		RECTO_PDF_TRY(const bool converted, cache.to_srgb(*profile, intent, scaled, count, out));
		if (converted) {
			return {};
		}
		if (alternate_ != nullptr && alternate_->components() == n) {
			return alternate_->to_srgb(in, count, intent, document, out);
		}
	}
	for (size_t i = 0; i < count; ++i) {
		const std::span<const double> c = in.subspan(i * n, n);
		Vec3 rgb{};
		switch (kind_) {
		case ColorSpaceKind::CalGray:
		case ColorSpaceKind::CalRGB:
		case ColorSpaceKind::Lab:
			rgb = cie_to_srgb(kind_, cie_, c);
			break;
		case ColorSpaceKind::ICCBased: {
			// No usable profile or alternate: the device space of its component count.
			const ColorSpaceKind device_kind = n == 1u ? ColorSpaceKind::DeviceGray
				: n == 4u ? ColorSpaceKind::DeviceCMYK
						  : ColorSpaceKind::DeviceRGB;
			rgb = device_rgb(device_kind, c);
			break;
		}
		default:
			rgb = device_rgb(kind_, c);
			break;
		}
		out[3u * i] = rgb[0];
		out[3u * i + 1u] = rgb[1];
		out[3u * i + 2u] = rgb[2];
	}
	return {};
}

PdfResult<ColorRgba> ColorSpace::rgba(std::span<const double> colour, RenderingIntent intent, const Document& document) const noexcept
{
	std::array<double, 3> rgb{};
	RECTO_PDF_RETURN_IF_ERROR(to_srgb(colour.first(components()), 1u, intent, document, rgb));
	return ColorRgba{unit(rgb[0]), unit(rgb[1]), unit(rgb[2]), 1.0f};
}

PdfResult<std::vector<uint8_t>> ColorSpace::convert_samples(std::span<const uint8_t> row, size_t width, uint32_t bits,
	std::span<const double> decode, RenderingIntent intent, const Document& document) const noexcept
{
	try {
		if (bits != 1u && bits != 2u && bits != 4u && bits != 8u && bits != 16u) {
			return make_pdf_error(ErrorCode::DamagedFormat, "Image samples have an unsupported bit depth");
		}
		const size_t n = components();
		const uint64_t sample_bits = uint64_t{width} * n * bits;
		if (width != 0u && sample_bits / width / n != bits) {
			return make_pdf_error(ErrorCode::DamagedFormat, "Image row size overflows");
		}
		const uint64_t needed = (sample_bits + 7u) / 8u;
		if (row.size() < needed) {
			return make_pdf_error(ErrorCode::DamagedFormat, "Image row is shorter than its samples");
		}
		const bool use_decode = decode.size() == 2u * n;
		const double max_value = static_cast<double>((uint32_t{1} << bits) - 1u);
		// An Indexed image's samples are palette indices: its default /Decode is [0 2^bits-1] (8.9.5.2, Table 90).
		const auto decoded_range = [&](size_t c) {
			if (use_decode) {
				return std::pair{decode[2u * c], decode[2u * c + 1u]};
			}
			return kind_ == ColorSpaceKind::Indexed ? std::pair{0.0, max_value} : component_range(c);
		};
		const auto finish = [&](const std::vector<double>& rgb, auto&& rgb_of) {
			std::vector<uint8_t> out(width * 4u);
			for (size_t i = 0; i < width; ++i) {
				const size_t at = rgb_of(i);
				for (size_t k = 0; k < 3u; ++k) {
					out[4u * i + k] = static_cast<uint8_t>(std::lround(std::clamp(rgb[3u * at + k], 0.0, 1.0) * 255.0));
				}
				out[4u * i + 3u] = 255u;
			}
			return out;
		};
		if (n == 1u && bits <= 8u && width > (size_t{1} << bits)) {
			// One component of at most 8 bits: every possible sample converted once (a palette, a tint table) and each
			// pixel looked up, so a wide row costs 2^bits conversions, one Windows call for an ICC space.
			const size_t entries = size_t{1} << bits;
			const auto [lo, hi] = decoded_range(0u);
			std::vector<double> table(entries);
			for (size_t t = 0; t < entries; ++t) {
				table[t] = lo + static_cast<double>(t) * (hi - lo) / max_value;
			}
			std::vector<double> table_rgb(entries * 3u);
			RECTO_PDF_RETURN_IF_ERROR(to_srgb(table, entries, intent, document, table_rgb));
			const uint32_t mask = (1u << bits) - 1u;
			return finish(table_rgb, [&](size_t i) {
				const uint64_t bit = uint64_t{i} * bits;
				const uint32_t shift = 8u - bits - static_cast<uint32_t>(bit % 8u);
				return static_cast<size_t>((row[bit / 8u] >> shift) & mask);
			});
		}
		std::vector<double> values(width * n);
		uint64_t bit = 0;
		for (size_t i = 0; i < values.size(); ++i, bit += bits) {
			uint32_t v = 0;
			if (bits == 16u) {
				v = (uint32_t{row[bit / 8u]} << 8u) | row[bit / 8u + 1u];
			} else if (bits == 8u) {
				v = row[bit / 8u];
			} else {
				const uint32_t shift = 8u - bits - static_cast<uint32_t>(bit % 8u);
				v = (row[bit / 8u] >> shift) & ((1u << bits) - 1u);
			}
			const auto [lo, hi] = decoded_range(i % n);
			values[i] = lo + v * (hi - lo) / max_value;
		}
		std::vector<double> rgb(width * 3u);
		RECTO_PDF_RETURN_IF_ERROR(to_srgb(values, width, intent, document, rgb));
		return finish(rgb, [](size_t i) { return i; });
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory converting image samples");
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// ColorSpaces
// ---------------------------------------------------------------------------------------------------------------------

namespace {

[[nodiscard]] std::optional<ColorSpaceKind> device_named(std::string_view name) noexcept
{
	if (name == "DeviceGray") {
		return ColorSpaceKind::DeviceGray;
	}
	if (name == "DeviceRGB") {
		return ColorSpaceKind::DeviceRGB;
	}
	if (name == "DeviceCMYK") {
		return ColorSpaceKind::DeviceCMYK;
	}
	return std::nullopt;
}

[[nodiscard]] uint64_t name_key(std::string_view name) noexcept
{
	return bytes_key(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(name.data()), name.size()));
}

template <typename Resolve>
[[nodiscard]] std::optional<std::array<double, 3>> triple(Resolve&& resolve, const PdfDict& dict, std::string_view key)
{
	const PdfValue* v = dict.get(key);
	if (v == nullptr) {
		return std::nullopt;
	}
	const std::optional<PdfValue> r = resolve(*v);
	if (!r.has_value() || !r->is_array() || r->as_array()->size() != 3u) {
		return std::nullopt;
	}
	std::array<double, 3> out{};
	for (size_t i = 0; i < 3u; ++i) {
		const std::optional<PdfValue> e = resolve((*r->as_array())[i]);
		const std::optional<double> n = e.has_value() ? e->as_number() : std::nullopt;
		if (!n.has_value() || !std::isfinite(*n)) {
			return std::nullopt;
		}
		out[i] = *n;
	}
	return out;
}

template <size_t N, typename Resolve>
[[nodiscard]] std::optional<std::array<double, N>> numbers(Resolve&& resolve, const PdfDict& dict, std::string_view key)
{
	const PdfValue* v = dict.get(key);
	if (v == nullptr) {
		return std::nullopt;
	}
	const std::optional<PdfValue> r = resolve(*v);
	if (!r.has_value() || !r->is_array() || r->as_array()->size() != N) {
		return std::nullopt;
	}
	std::array<double, N> out{};
	for (size_t i = 0; i < N; ++i) {
		const std::optional<PdfValue> e = resolve((*r->as_array())[i]);
		const std::optional<double> n = e.has_value() ? e->as_number() : std::nullopt;
		if (!n.has_value() || !std::isfinite(*n)) {
			return std::nullopt;
		}
		out[i] = *n;
	}
	return out;
}

} // namespace

std::optional<PdfValue> ColorSpaces::value_of(const PdfValue& value)
{
	auto r = document_.resolve_value(value);
	if (!r.has_value()) {
		if (r.error().code == ErrorCode::OutOfMemory && !fatal_.has_value()) {
			fatal_ = r.error();
		}
		return std::nullopt;
	}
	return std::move(*r);
}

ColorSpaces::ColorSpaces(const Document& document, const PdfDict* color_space_resources) noexcept
	: document_(document)
	, resources_(color_space_resources)
{
}

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpaces::device(ColorSpaceKind kind) noexcept
{
	try {
		const uint64_t key = static_cast<uint64_t>(kind) + 1u;
		for (const auto& [k, space] : resolved_) {
			if (k == key) {
				return space;
			}
		}
		RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, device_at(kind, 0u, true));
		if (fatal_.has_value()) {
			return *std::exchange(fatal_, std::nullopt);
		}
		resolved_.emplace_back(key, space);
		return space;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory resolving a colour space");
	}
}

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpaces::named(std::string_view name) noexcept
{
	try {
		const uint64_t key = name_key(name);
		for (const auto& [k, space] : resolved_) {
			if (k == key) {
				return space;
			}
		}
		RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, resolve_at(PdfValue(PdfName(name)), 0u, true));
		if (fatal_.has_value()) {
			return *std::exchange(fatal_, std::nullopt);
		}
		if (resolved_.size() < 4'096u) {
			resolved_.emplace_back(key, space);
		}
		return space;
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory resolving a colour space");
	}
}

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpaces::resolve(const PdfValue& value) noexcept
{
	RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, resolve_at(value, 0u, true));
	if (fatal_.has_value()) {
		return *std::exchange(fatal_, std::nullopt);
	}
	return space;
}

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpaces::device_at(ColorSpaceKind kind, size_t depth, bool defaults) noexcept
{
	if (defaults && resources_ != nullptr) {
		const std::string_view key = kind == ColorSpaceKind::DeviceGray ? "DefaultGray"
			: kind == ColorSpaceKind::DeviceRGB                          ? "DefaultRGB"
																		 : "DefaultCMYK";
		if (const PdfValue* value = resources_->get(key)) {
			// Inside its own definition a default does not apply again (8.6.5.6).
			RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, resolve_at(*value, depth + 1u, false));
			if (space != nullptr && space->components() == ColorSpace::device(kind)->components()) {
				return space;
			}
		}
	}
	return ColorSpace::device(kind);
}

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpaces::resolve_at(const PdfValue& value, size_t depth, bool defaults) noexcept
{
	try {
		if (depth > kMaxColorSpaceDepth) {
			return ColorSpace::device(ColorSpaceKind::DeviceGray);
		}
		// An indirect value, followed once on this path.
		if (const std::optional<IndirectRef> ref = value.as_ref()) {
			if (std::find(visiting_.begin(), visiting_.end(), uint64_t{ref->obj_num}) != visiting_.end()) {
				return ColorSpace::device(ColorSpaceKind::DeviceGray); // a cycle
			}
			auto resolved = document_.resolve_value(value);
			if (!resolved.has_value()) {
				if (resolved.error().code == ErrorCode::OutOfMemory) {
					return resolved.error();
				}
				return std::shared_ptr<const ColorSpace>{};
			}
			visiting_.push_back(ref->obj_num);
			auto out = resolve_at(*resolved, depth + 1u, defaults);
			visiting_.pop_back();
			return out;
		}
		if (const std::optional<std::string_view> name = value.as_name()) {
			if (const std::optional<ColorSpaceKind> kind = device_named(*name)) {
				return device_at(*kind, depth, defaults);
			}
			if (*name == "Pattern" || resources_ == nullptr) {
				return std::shared_ptr<const ColorSpace>{};
			}
			const PdfValue* entry = resources_->get(*name);
			if (entry == nullptr) {
				return std::shared_ptr<const ColorSpace>{};
			}
			const uint64_t key = name_key(*name);
			if (std::find(visiting_.begin(), visiting_.end(), key) != visiting_.end()) {
				return ColorSpace::device(ColorSpaceKind::DeviceGray); // resources naming one another
			}
			visiting_.push_back(key);
			auto out = resolve_at(*entry, depth + 1u, defaults);
			visiting_.pop_back();
			return out;
		}
		const PdfArray* array = value.as_array();
		if (array == nullptr || array->empty()) {
			return std::shared_ptr<const ColorSpace>{};
		}
		const std::optional<PdfValue> family_value = value_of((*array)[0]);
		const std::optional<std::string_view> family = family_value.has_value() ? family_value->as_name() : std::nullopt;
		if (!family.has_value()) {
			return std::shared_ptr<const ColorSpace>{};
		}
		if (const std::optional<ColorSpaceKind> kind = device_named(*family)) {
			return device_at(*kind, depth, defaults);
		}
		const auto second = [&]() -> std::optional<PdfValue> {
			if (array->size() < 2u) {
				return std::nullopt;
			}
			return value_of((*array)[1]);
		};
		const auto resolve = [this](const PdfValue& v) { return value_of(v); };
		if (*family == "CalGray" || *family == "CalRGB" || *family == "Lab") {
			const ColorSpaceKind kind = *family == "CalGray" ? ColorSpaceKind::CalGray
				: *family == "CalRGB"                        ? ColorSpaceKind::CalRGB
															 : ColorSpaceKind::Lab;
			const ColorSpaceKind fallback = kind == ColorSpaceKind::CalGray ? ColorSpaceKind::DeviceGray : ColorSpaceKind::DeviceRGB;
			const std::optional<PdfValue> params_value = second();
			const PdfDict* dict = params_value.has_value() ? params_value->as_dict() : nullptr;
			if (dict == nullptr) {
				return ColorSpace::device(fallback);
			}
			CieParams p;
			const auto white = triple(resolve, *dict, "WhitePoint");
			if (!white.has_value() || (*white)[0] <= 0.0 || (*white)[2] <= 0.0 || (*white)[1] != 1.0) {
				return ColorSpace::device(fallback); // a malformed white point: the device space, as readers do
			}
			p.white = *white;
			if (const auto black = triple(resolve, *dict, "BlackPoint");
				black.has_value() && (*black)[0] >= 0.0 && (*black)[1] >= 0.0 && (*black)[2] >= 0.0) {
				p.black = *black;
			}
			if (kind == ColorSpaceKind::CalGray) {
				if (const PdfValue* g = dict->get("Gamma")) {
					const std::optional<PdfValue> r = value_of(*g);
					if (const std::optional<double> gamma = r.has_value() ? r->as_number() : std::nullopt; gamma.has_value() && *gamma > 0.0) {
						p.gamma = {*gamma, *gamma, *gamma};
					}
				}
			} else if (kind == ColorSpaceKind::CalRGB) {
				if (const auto gamma = triple(resolve, *dict, "Gamma");
					gamma.has_value() && (*gamma)[0] > 0.0 && (*gamma)[1] > 0.0 && (*gamma)[2] > 0.0) {
					p.gamma = *gamma;
				}
				if (const auto matrix = numbers<9>(resolve, *dict, "Matrix")) {
					p.matrix = *matrix;
				}
			} else if (const auto range = numbers<4>(resolve, *dict, "Range");
					   range.has_value() && (*range)[0] <= (*range)[1] && (*range)[2] <= (*range)[3]) {
				p.range = *range;
			}
			return ColorSpace::cie(kind, p);
		}
		if (*family == "ICCBased") {
			if (array->size() < 2u) {
				return std::shared_ptr<const ColorSpace>{};
			}
			const std::optional<IndirectRef> ref = (*array)[1].as_ref();
			if (ref.has_value() && std::find(visiting_.begin(), visiting_.end(), uint64_t{ref->obj_num}) != visiting_.end()) {
				return ColorSpace::device(ColorSpaceKind::DeviceGray); // an /Alternate chain back to itself
			}
			const std::optional<PdfValue> stream_value = second();
			const PdfStream* stream = stream_value.has_value() ? stream_value->as_stream() : nullptr;
			if (stream == nullptr) {
				return std::shared_ptr<const ColorSpace>{};
			}
			std::optional<int64_t> n_value;
			if (const PdfValue* n_entry = stream->dict.get("N")) {
				if (const std::optional<PdfValue> n_resolved = value_of(*n_entry)) {
					n_value = n_resolved->as_int();
				}
			}
			const uint32_t n = n_value == 1 ? 1u : n_value == 4 ? 4u : 3u;
			const ColorSpaceKind device_kind = n == 1u ? ColorSpaceKind::DeviceGray
				: n == 4u ? ColorSpaceKind::DeviceCMYK
						  : ColorSpaceKind::DeviceRGB;
			// The alternate (resolved with this stream on the path, so a chain back to it ends), else the device space.
			std::shared_ptr<const ColorSpace> alternate;
			if (ref.has_value()) {
				visiting_.push_back(ref->obj_num);
			}
			if (const PdfValue* alt = stream->dict.get("Alternate")) {
				auto resolved_alt = resolve_at(*alt, depth + 1u, defaults);
				if (!resolved_alt.has_value()) {
					if (ref.has_value()) {
						visiting_.pop_back();
					}
					return resolved_alt.error();
				}
				alternate = std::move(*resolved_alt);
			}
			if (ref.has_value()) {
				visiting_.pop_back();
			}
			if (alternate == nullptr || alternate->components() != n) {
				alternate = ColorSpace::device(device_kind);
			}
			std::vector<double> range;
			if (const PdfValue* r = stream->dict.get("Range")) {
				const std::optional<PdfValue> rv = value_of(*r);
				if (rv.has_value() && rv->is_array() && rv->as_array()->size() == 2u * n) {
					for (const PdfValue& e : *rv->as_array()) {
						const std::optional<PdfValue> element = value_of(e); // each element may be indirect too
						const std::optional<double> x = element.has_value() ? element->as_number() : std::nullopt;
						if (!x.has_value() || !std::isfinite(*x)) {
							range.clear();
							break;
						}
						range.push_back(*x);
					}
					for (size_t i = 0; i + 1u < range.size(); i += 2u) {
						if (!(range[i] < range[i + 1u])) {
							range.clear(); // reversed or empty: the default ranges
							break;
						}
					}
				}
				if (range.empty()) {
					(void)make_pdf_error(ErrorCode::DamagedFormat, "ICCBased /Range is not ordered pairs, one per component");
				}
			}
			if (n_value != 1 && n_value != 3 && n_value != 4) {
				(void)make_pdf_error(ErrorCode::DamagedFormat, "ICCBased /N is not 1, 3 or 4"); // recorded
				return alternate;
			}
			ColorProfileCache& cache = document_.color_profiles();
			std::shared_ptr<const IccProfile> profile = ref.has_value() ? cache.profile(ref->obj_num) : nullptr;
			if (profile == nullptr || profile->components != n) {
				// A stream rejected before is not decoded again, and all decoding is bounded per document.
				if (ref.has_value() && !cache.may_decode(ref->obj_num)) {
					return alternate;
				}
				g_profile_decodes.fetch_add(1u);
				auto bytes = document_.decode_stream(*stream, kMaxIccProfileBytes);
				if (!bytes.has_value()) {
					if (bytes.error().code == ErrorCode::OutOfMemory) {
						return bytes.error();
					}
					if (ref.has_value()) {
						cache.reject(ref->obj_num);
					}
					return alternate; // the decode recorded its own diagnostic
				}
				cache.note_decoded(bytes->size());
				auto made = IccProfile::make(std::move(*bytes), n, ref.has_value() ? ref->obj_num : 0u);
				if (!made.has_value()) {
					if (made.error().code == ErrorCode::OutOfMemory) {
						return made.error();
					}
					if (ref.has_value()) {
						cache.reject(ref->obj_num);
					}
					return alternate; // recorded by make_pdf_error
				}
				profile = std::move(*made);
				// Every profile a document keeps is within its budget, whoever holds it; past it, the alternate.
				bool kept = true;
				if (ref.has_value()) {
					RECTO_PDF_TRY(kept, cache.remember(profile));
				}
				if (!kept) {
					(void)make_pdf_error(ErrorCode::ResourceLimit, "ICC profiles past the document's limit"); // recorded
					return alternate;
				}
			}
			if (range.empty() && profile->lab) {
				range = {0.0, 100.0, -128.0, 127.0, -128.0, 127.0}; // a Lab profile's data are L*, a* and b*
			}
			return ColorSpace::icc(std::move(profile), std::move(range), std::move(alternate));
		}
		if (*family == "Indexed") {
			return indexed_at(*array, depth, defaults);
		}
		if (*family == "Separation" || *family == "DeviceN") {
			return spot_at(*array, *family == "DeviceN", depth, defaults);
		}
		if (*family != "Pattern") { // section 16's
			(void)make_pdf_error(ErrorCode::DamagedFormat, "Colour space family is not one this reader knows");
		}
		return std::shared_ptr<const ColorSpace>{};
	} catch (const std::bad_alloc&) {
		return make_pdf_error(ErrorCode::OutOfMemory, "Out of memory resolving a colour space");
	}
}

namespace {

// A space a Separation or DeviceN may draw into (8.6.6.4): a device, CIE-based or ICCBased space.
[[nodiscard]] bool usable_alternate(const std::shared_ptr<const ColorSpace>& space) noexcept
{
	return space != nullptr && space->kind() <= ColorSpaceKind::ICCBased;
}

} // namespace

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpaces::indexed_at(const PdfArray& array, size_t depth, bool defaults)
{
	// [/Indexed base hival lookup] (8.6.6.3).
	const auto unusable = [](std::string_view why) {
		(void)make_pdf_error(ErrorCode::DamagedFormat, why); // recorded
		return std::shared_ptr<const ColorSpace>{};
	};
	if (array.size() < 4u) {
		return unusable("Indexed colour space does not have a base, hival and lookup");
	}
	RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> base, resolve_at(array[1], depth + 1u, defaults));
	if (base == nullptr || base->kind() == ColorSpaceKind::Indexed) {
		return unusable("Indexed colour space has no usable base");
	}
	const std::optional<PdfValue> hival_value = value_of(array[2]);
	std::optional<double> hival_number = hival_value.has_value() ? hival_value->as_number() : std::nullopt;
	if (!hival_number.has_value() || !std::isfinite(*hival_number) || *hival_number < 0.0 ||
		*hival_number != std::floor(*hival_number)) {
		return unusable("Indexed colour space hival is not a non-negative integer");
	}
	uint32_t hival = 255u;
	if (*hival_number > 255.0) {
		(void)make_pdf_error(ErrorCode::DamagedFormat, "Indexed colour space hival is past 255; read as 255");
	} else {
		hival = static_cast<uint32_t>(*hival_number);
	}
	const std::optional<PdfValue> lookup_value = value_of(array[3]);
	std::vector<uint8_t> lookup;
	if (lookup_value.has_value() && lookup_value->as_string() != nullptr) {
		const std::span<const uint8_t> bytes = lookup_value->as_string()->span();
		lookup.assign(bytes.begin(), bytes.end());
	} else if (lookup_value.has_value() && lookup_value->as_stream() != nullptr) {
		auto bytes = document_.decode_stream(*lookup_value->as_stream(), kMaxIndexedLookupBytes);
		if (!bytes.has_value()) {
			if (bytes.error().code == ErrorCode::OutOfMemory) {
				return bytes.error();
			}
			return unusable("Indexed colour space lookup stream cannot be decoded");
		}
		lookup = std::move(*bytes);
	} else {
		return unusable("Indexed colour space lookup is neither a string nor a stream");
	}
	const size_t needed = base->components() * (size_t{hival} + 1u);
	if (lookup.size() < needed) {
		// The defined entries still draw; the missing ones are zero bytes, as MuPDF reads a short table.
		(void)make_pdf_error(ErrorCode::DamagedFormat, "Indexed colour space lookup is short; padded with zeros");
	}
	lookup.resize(needed, 0u);
	return ColorSpace::indexed(std::move(base), hival, std::move(lookup));
}

PdfResult<std::shared_ptr<const ColorSpace>> ColorSpaces::spot_at(
	const PdfArray& array, bool device_n, size_t depth, bool defaults)
{
	// [/Separation name alternate tint] (8.6.6.4) and [/DeviceN names alternate tint attributes] (8.6.6.5).
	const auto unusable = [](std::string_view why) {
		(void)make_pdf_error(ErrorCode::DamagedFormat, why); // recorded
		return std::shared_ptr<const ColorSpace>{};
	};
	if (array.size() < 4u) {
		return unusable("Separation or DeviceN colour space does not have its names, alternate and tint transform");
	}
	std::vector<std::string> names;
	const std::optional<PdfValue> names_value = value_of(array[1]);
	if (!device_n) {
		const std::optional<std::string_view> name = names_value.has_value() ? names_value->as_name() : std::nullopt;
		if (!name.has_value()) {
			return unusable("Separation colour space has no colorant name");
		}
		names.emplace_back(*name);
	} else {
		const PdfArray* list = names_value.has_value() ? names_value->as_array() : nullptr;
		if (list == nullptr || list->empty() || list->size() > kMaxColorComponents) {
			return unusable("DeviceN colour space does not name 1 to 32 colorants");
		}
		for (const PdfValue& element : *list) {
			const std::optional<PdfValue> name_value = value_of(element);
			const std::optional<std::string_view> name = name_value.has_value() ? name_value->as_name() : std::nullopt;
			if (!name.has_value()) {
				return unusable("DeviceN colour space names a colorant with something other than a name");
			}
			names.emplace_back(*name);
		}
	}
	RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> alternate, resolve_at(array[2], depth + 1u, defaults));
	if (!usable_alternate(alternate)) {
		return unusable("Separation or DeviceN alternate space is not a device, CIE-based or ICCBased space");
	}
	RECTO_PDF_TRY(std::shared_ptr<const Function> tint, tint_transform(array[3]));
	if (tint == nullptr) {
		return std::shared_ptr<const ColorSpace>{}; // recorded
	}
	if (tint->inputs() != names.size() || tint->outputs() < alternate->components()) {
		return unusable("Tint transform does not take one input per colorant or give the alternate's components");
	}
	if (!device_n) {
		return ColorSpace::separation(std::move(names.front()), std::move(alternate), std::move(tint));
	}
	ColorSpace::DeviceNAttributes attributes;
	const std::optional<PdfValue> attributes_value = array.size() >= 5u ? value_of(array[4]) : std::nullopt;
	if (const PdfDict* dict = attributes_value.has_value() ? attributes_value->as_dict() : nullptr) {
		if (const PdfValue* subtype = dict->get("Subtype")) {
			const std::optional<PdfValue> v = value_of(*subtype);
			attributes.nchannel = v.has_value() && v->as_name() == std::optional<std::string_view>("NChannel");
		}
		if (const PdfValue* colorants = dict->get("Colorants")) {
			const std::optional<PdfValue> v = value_of(*colorants);
			if (const PdfDict* map = v.has_value() ? v->as_dict() : nullptr) {
				size_t examined = 0;
				for (const auto& [name, entry] : *map) {
					// At most the bound examined, accepted or not, and only a Separation array resolved, so a
					// /Colorants dictionary can never fan out into other DeviceN spaces.
					if (++examined > kMaxDeviceNColorants) {
						break;
					}
					const std::optional<PdfValue> colorant = value_of(entry);
					const PdfArray* colorant_array = colorant.has_value() ? colorant->as_array() : nullptr;
					const std::optional<PdfValue> head =
						colorant_array != nullptr && !colorant_array->empty() ? value_of((*colorant_array)[0]) : std::nullopt;
					if (!head.has_value() || head->as_name() != std::optional<std::string_view>("Separation")) {
						continue;
					}
					RECTO_PDF_TRY(std::shared_ptr<const ColorSpace> space, resolve_at(entry, depth + 1u, defaults));
					if (space != nullptr && space->kind() == ColorSpaceKind::Separation) {
						attributes.colorants.emplace_back(name.value, std::move(space));
					}
				}
			}
		}
		if (const PdfValue* process = dict->get("Process")) {
			const std::optional<PdfValue> v = value_of(*process);
			if (const PdfDict* process_dict = v.has_value() ? v->as_dict() : nullptr) {
				if (const PdfValue* space = process_dict->get("ColorSpace")) {
					RECTO_PDF_TRY(attributes.process, resolve_at(*space, depth + 1u, defaults));
				}
				if (const PdfValue* components = process_dict->get("Components")) {
					const std::optional<PdfValue> list = value_of(*components);
					if (const PdfArray* names_list = list.has_value() ? list->as_array() : nullptr) {
						for (const PdfValue& element : *names_list) {
							const std::optional<PdfValue> name_value = value_of(element);
							const std::optional<std::string_view> name =
								name_value.has_value() ? name_value->as_name() : std::nullopt;
							if (name.has_value() && attributes.process_components.size() < kMaxColorComponents) {
								attributes.process_components.emplace_back(*name);
							}
						}
					}
				}
			}
		}
	}
	return ColorSpace::device_n(std::move(names), std::move(alternate), std::move(tint), std::move(attributes));
}

PdfResult<std::shared_ptr<const Function>> ColorSpaces::tint_transform(const PdfValue& value)
{
	const std::optional<IndirectRef> ref = value.as_ref();
	if (ref.has_value()) {
		for (const auto& [number, function] : functions_) {
			if (number == ref->obj_num) {
				return function;
			}
		}
	}
	auto parsed = Function::parse(document_, value);
	if (!parsed.has_value()) {
		if (parsed.error().code == ErrorCode::OutOfMemory) {
			return parsed.error();
		}
		return std::shared_ptr<const Function>{}; // the parse recorded why
	}
	const size_t bytes = (*parsed)->table_bytes();
	if (function_bytes_ + bytes > kMaxColorFunctionBytes) {
		record_diagnostic(make_refusal_error(ErrorCode::ResourceLimit, RefusalReason::SampleTableBytes,
			function_bytes_ + bytes, kMaxColorFunctionBytes, "Tint transform tables past the page's limit"));
		return std::shared_ptr<const Function>{};
	}
	function_bytes_ += bytes;
	if (ref.has_value()) {
		functions_.emplace_back(ref->obj_num, *parsed); // bounded by the byte budget, each function at least its node
	}
	return std::move(*parsed);
}

namespace detail {

void set_icc_failure_for_testing(IccFailureForTesting failure) noexcept
{
	g_icc_failure.store(failure);
}

size_t translate_calls_for_testing() noexcept
{
	return g_translate_calls.load();
}

size_t icc_profile_decodes_for_testing() noexcept
{
	return g_profile_decodes.load();
}

void set_default_cmyk_profile_enabled_for_testing(bool enabled) noexcept
{
	g_default_cmyk_enabled.store(enabled);
}

} // namespace detail

} // namespace recto::pdf
