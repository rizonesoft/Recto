// Embedded font programs loaded in memory (ISO 32000-1 sections 9.6.3, 9.8 and
// 9.9; D01 T02 section 2): DirectWrite for the programs it accepts, and a
// TrueType `glyf` reader for the ones it refuses.
#include "font.h"

#include "font_charstrings.h"

#include "filters.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <wrl/client.h>
#pragma comment(lib, "dwrite.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <span>
#include <unordered_map>
#include <string_view>

namespace recto::pdf {

using Microsoft::WRL::ComPtr;

namespace detail {

thread_local FontLoaderStage g_font_failure_stage = FontLoaderStage::Factory;
thread_local int32_t g_font_failure_hresult = 0;

ScopedFontLoaderFailure::ScopedFontLoaderFailure(FontLoaderStage stage, int32_t hresult) noexcept
	: previous_stage_(g_font_failure_stage)
	, previous_hresult_(g_font_failure_hresult)
{
	g_font_failure_stage = stage;
	g_font_failure_hresult = hresult;
}

ScopedFontLoaderFailure::~ScopedFontLoaderFailure() noexcept
{
	g_font_failure_stage = previous_stage_;
	g_font_failure_hresult = previous_hresult_;
}

std::atomic<int64_t> g_live_font_program_owners{0};

int64_t live_font_program_owners() noexcept
{
	return g_live_font_program_owners.load(std::memory_order_acquire);
}

} // namespace detail

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------------------------------------------------

// A font failure with its step and, when Windows returned one, its HRESULT. It is not recorded here: the public
// functions record the failure they return, once, with those numbers in the ring and ETW (D01 T02 section 25), so a
// rejection the `glyf` reader recovers from leaves no damaged-font entry.
[[nodiscard]] PdfError font_error(
	ErrorCode code,
	std::string_view message,
	DecoderStep step,
	std::optional<HRESULT> hr = std::nullopt) noexcept
{
	PdfError error(code);
	error.message = PdfErrorMessage(message);
	error.decoder_step = step;
	if (hr.has_value()) {
		error.platform_status = static_cast<uint32_t>(*hr);
	}
	return error;
}

// Records the failure a public function returns.
template <typename T>
[[nodiscard]] PdfResult<T> recorded(PdfResult<T>&& result) noexcept
{
	if (!result.has_value()) {
		record_diagnostic(result.error());
	}
	return std::move(result);
}

[[nodiscard]] PdfError glyf_error(ErrorCode code, std::string_view message) noexcept
{
	return font_error(code, message, DecoderStep::NativeGlyf);
}

// A DirectWrite status that means the program is not a font DirectWrite can use, so the `glyf` reader may try it.
[[nodiscard]] bool is_font_rejection(HRESULT hr) noexcept
{
	return hr == DWRITE_E_FILEFORMAT || hr == DWRITE_E_UNSUPPORTEDOPERATION || hr == E_INVALIDARG;
}

[[nodiscard]] PdfError dwrite_failure(HRESULT hr, std::string_view step_name, DecoderStep step) noexcept
{
	if (hr == E_OUTOFMEMORY) {
		return font_error(ErrorCode::OutOfMemory, "DirectWrite ran out of memory loading a font", step, hr);
	}
	if (step == DecoderStep::DWriteFontFace && is_font_rejection(hr)) {
		return font_error(ErrorCode::DamagedFormat, "DirectWrite rejected the embedded font program", step, hr);
	}
	char text[160];
	const int n = std::snprintf(text, sizeof(text), "DirectWrite %.*s failed (HRESULT 0x%08lX)",
		static_cast<int>(step_name.size()), step_name.data(), static_cast<unsigned long>(hr));
	return font_error(ErrorCode::InternalError,
		std::string_view(text, n > 0 ? std::min<size_t>(static_cast<size_t>(n), sizeof(text) - 1u) : 0u), step, hr);
}

// Runs one DirectWrite step, or returns the failure a test injected for it (detail::ScopedFontLoaderFailure).
template <typename Step>
[[nodiscard]] HRESULT dwrite_step(detail::FontLoaderStage stage, Step&& step) noexcept
{
	if (detail::g_font_failure_hresult != 0 && detail::g_font_failure_stage == stage) {
		return static_cast<HRESULT>(detail::g_font_failure_hresult);
	}
	try {
		return step();
	} catch (const std::bad_alloc&) {
		return E_OUTOFMEMORY;
	} catch (...) {
		return E_UNEXPECTED;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// The shared factory, the in-memory loader and the owner of a font's bytes
// ---------------------------------------------------------------------------------------------------------------------

// A font program's bytes for DirectWrite: the stream holds the shared program, so the bytes live for as long as
// DirectWrite reads them, whatever the caller and the font do with their own copies.
class FontProgramStream final : public IDWriteFontFileStream {
public:
	explicit FontProgramStream(std::shared_ptr<const std::vector<uint8_t>> program) noexcept
		: program_(std::move(program))
	{
		detail::g_live_font_program_owners.fetch_add(1, std::memory_order_acq_rel);
	}

	FontProgramStream(const FontProgramStream&) = delete;
	FontProgramStream& operator=(const FontProgramStream&) = delete;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) noexcept override
	{
		if (object == nullptr) {
			return E_POINTER;
		}
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileStream)) {
			*object = static_cast<IDWriteFontFileStream*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() noexcept override
	{
		return static_cast<ULONG>(references_.fetch_add(1, std::memory_order_relaxed) + 1);
	}

	ULONG STDMETHODCALLTYPE Release() noexcept override
	{
		const ULONG remaining = static_cast<ULONG>(references_.fetch_sub(1, std::memory_order_acq_rel) - 1);
		if (remaining == 0) {
			delete this;
		}
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE ReadFileFragment(
		const void** fragment_start,
		UINT64 offset,
		UINT64 size,
		void** fragment_context) noexcept override
	{
		if (fragment_start == nullptr || fragment_context == nullptr) {
			return E_POINTER;
		}
		*fragment_context = nullptr;
		const UINT64 total = program_->size();
		if (offset > total || size > total - offset) {
			*fragment_start = nullptr;
			return E_FAIL;
		}
		*fragment_start = program_->data() + offset;
		return S_OK;
	}

	void STDMETHODCALLTYPE ReleaseFileFragment(void*) noexcept override {}

	HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* file_size) noexcept override
	{
		if (file_size == nullptr) {
			return E_POINTER;
		}
		*file_size = program_->size();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* last_write_time) noexcept override
	{
		if (last_write_time != nullptr) {
			*last_write_time = 0u;
		}
		return E_NOTIMPL; // an embedded program has no file time
	}

private:
	~FontProgramStream() noexcept
	{
		detail::g_live_font_program_owners.fetch_sub(1, std::memory_order_acq_rel);
	}

	std::atomic<ULONG> references_{1};
	const std::shared_ptr<const std::vector<uint8_t>> program_;
};

// The programs DirectWrite may ask for, by the 64-bit key each font's file reference carries. An entry is weak: the
// font owns its program, and its entry leaves the registry when the font is destroyed, so the registry and the loader
// keep nothing of a closed document (the built-in in-memory loader keeps every program it was ever given until the
// loader itself is released, `IDWriteInMemoryFontFileLoader::GetFileCount`).
std::mutex g_font_registry_mutex;
std::unordered_map<uint64_t, std::weak_ptr<const std::vector<uint8_t>>> g_font_registry;
uint64_t g_next_font_key = 1u;

} // namespace

size_t detail::registered_font_programs() noexcept
{
	const std::lock_guard lock(g_font_registry_mutex);
	return g_font_registry.size();
}

namespace {

// The process's one font file loader: it turns a key into a stream over the registered program.
class FontProgramLoader final : public IDWriteFontFileLoader {
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) noexcept override
	{
		if (object == nullptr) {
			return E_POINTER;
		}
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileLoader)) {
			*object = static_cast<IDWriteFontFileLoader*>(this);
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	// Lives for the process (it stays registered with the factory), so it is not reference-counted.
	ULONG STDMETHODCALLTYPE AddRef() noexcept override
	{
		return 1;
	}

	ULONG STDMETHODCALLTYPE Release() noexcept override
	{
		return 1;
	}

	HRESULT STDMETHODCALLTYPE CreateStreamFromKey(
		const void* key,
		UINT32 key_size,
		IDWriteFontFileStream** stream) noexcept override
	{
		if (stream == nullptr) {
			return E_POINTER;
		}
		*stream = nullptr;
		if (key == nullptr || key_size != sizeof(uint64_t)) {
			return E_INVALIDARG;
		}
		uint64_t id = 0;
		std::memcpy(&id, key, sizeof(id));
		std::shared_ptr<const std::vector<uint8_t>> program;
		{
			const std::lock_guard lock(g_font_registry_mutex);
			const auto it = g_font_registry.find(id);
			if (it != g_font_registry.end()) {
				program = it->second.lock();
			}
		}
		if (program == nullptr) {
			return E_FAIL; // the font was closed
		}
		try {
			*stream = new FontProgramStream(std::move(program));
		} catch (const std::bad_alloc&) {
			return E_OUTOFMEMORY;
		}
		return S_OK;
	}
};

// The factory and the loader, created on first use and kept for the life of the process: the loader is never
// unregistered while a face could exist, so they are deliberately never released (releasing them from a static
// destructor at exit could also run after DirectWrite itself has shut down).
struct SharedDWrite {
	IDWriteFactory5* factory{nullptr};
	FontProgramLoader* loader{nullptr};
};

std::mutex g_shared_dwrite_mutex;
SharedDWrite g_shared_dwrite{};

[[nodiscard]] HRESULT shared_factory(IDWriteFactory5*& out) noexcept
{
	return dwrite_step(detail::FontLoaderStage::Factory, [&]() -> HRESULT {
		const std::lock_guard lock(g_shared_dwrite_mutex);
		if (g_shared_dwrite.factory == nullptr) {
			IDWriteFactory5* factory = nullptr;
			const HRESULT hr = DWriteCreateFactory(
				DWRITE_FACTORY_TYPE_SHARED,
				__uuidof(IDWriteFactory5),
				reinterpret_cast<IUnknown**>(&factory));
			if (FAILED(hr)) {
				return hr;
			}
			g_shared_dwrite.factory = factory;
		}
		out = g_shared_dwrite.factory;
		return S_OK;
	});
}

[[nodiscard]] HRESULT shared_loader(IDWriteFactory5* factory, FontProgramLoader*& out) noexcept
{
	return dwrite_step(detail::FontLoaderStage::Loader, [&]() -> HRESULT {
		const std::lock_guard lock(g_shared_dwrite_mutex);
		if (g_shared_dwrite.loader == nullptr) {
			auto* loader = new FontProgramLoader();
			const HRESULT hr = factory->RegisterFontFileLoader(loader);
			if (FAILED(hr)) {
				delete loader;
				return hr;
			}
			g_shared_dwrite.loader = loader;
		}
		out = g_shared_dwrite.loader;
		return S_OK;
	});
}

// ---------------------------------------------------------------------------------------------------------------------
// Big-endian reads over a bounded span
// ---------------------------------------------------------------------------------------------------------------------

class ByteReader {
public:
	explicit ByteReader(std::span<const uint8_t> data) noexcept : data_(data) {}

	[[nodiscard]] bool u8(size_t at, uint8_t& out) const noexcept
	{
		if (at >= data_.size()) {
			return false;
		}
		out = data_[at];
		return true;
	}

	[[nodiscard]] bool u16(size_t at, uint16_t& out) const noexcept
	{
		if (at > data_.size() || data_.size() - at < 2u) {
			return false;
		}
		out = static_cast<uint16_t>((data_[at] << 8) | data_[at + 1u]);
		return true;
	}

	[[nodiscard]] bool i16(size_t at, int16_t& out) const noexcept
	{
		uint16_t raw = 0;
		if (!u16(at, raw)) {
			return false;
		}
		out = static_cast<int16_t>(raw);
		return true;
	}

	[[nodiscard]] bool u32(size_t at, uint32_t& out) const noexcept
	{
		if (at > data_.size() || data_.size() - at < 4u) {
			return false;
		}
		out = (static_cast<uint32_t>(data_[at]) << 24) | (static_cast<uint32_t>(data_[at + 1u]) << 16) |
			(static_cast<uint32_t>(data_[at + 2u]) << 8) | static_cast<uint32_t>(data_[at + 3u]);
		return true;
	}

	[[nodiscard]] size_t size() const noexcept
	{
		return data_.size();
	}

	[[nodiscard]] std::span<const uint8_t> sub(size_t at, size_t length) const noexcept
	{
		return data_.subspan(at, length);
	}

private:
	std::span<const uint8_t> data_;
};

// A table's place in the font program, checked to lie inside it.
struct TableSpan {
	size_t offset{0};
	size_t length{0};
	bool present{false};
};

// The table directory's entry for `tag`, or a refusal when the directory or the entry runs outside the program.
[[nodiscard]] PdfResult<TableSpan> find_table(const ByteReader& font, std::string_view tag) noexcept
{
	uint16_t num_tables = 0;
	if (!font.u16(4u, num_tables)) {
		return glyf_error(ErrorCode::DamagedFormat, "Font table directory is truncated");
	}
	if (static_cast<size_t>(num_tables) > (font.size() - 12u) / 16u) {
		return glyf_error(ErrorCode::DamagedFormat, "Font table directory runs past the font program");
	}
	for (size_t i = 0; i < num_tables; ++i) {
		const size_t entry = 12u + 16u * i;
		uint32_t offset = 0;
		uint32_t length = 0;
		if (!font.u32(entry + 8u, offset) || !font.u32(entry + 12u, length)) {
			return glyf_error(ErrorCode::DamagedFormat, "Font table directory is truncated");
		}
		const std::span<const uint8_t> name = font.sub(entry, 4u);
		if (std::string_view(reinterpret_cast<const char*>(name.data()), 4u) != tag) {
			continue;
		}
		if (offset > font.size() || length > font.size() - offset) {
			return glyf_error(ErrorCode::DamagedFormat, "Font table lies outside the font program");
		}
		return TableSpan{offset, length, true};
	}
	return TableSpan{};
}

// ---------------------------------------------------------------------------------------------------------------------
// Outlines from TrueType points
// ---------------------------------------------------------------------------------------------------------------------

struct GlyfPoint {
	double x{0.0};
	double y{0.0};
	bool on_curve{true};
};

// Expanded TrueType contours: every point (composites already placed) and the index past each contour's last point.
struct ExpandedGlyph {
	std::vector<GlyfPoint> points;
	std::vector<size_t> contour_ends;
};

// Converts quadratic TrueType contours to the cubic verbs of `display_list.h`, scaled by `scale` (1 / units per em):
// between two off-curve points the on-curve point is implied at their midpoint, a contour of off-curve points only
// starts at the midpoint of its first two, and each quadratic `(p0, c, p1)` becomes the exact cubic
// `(p0, p0 + 2/3 (c - p0), p1 + 2/3 (c - p1), p1)`.
[[nodiscard]] PdfResult<GlyphOutline> outline_from_points(const ExpandedGlyph& glyph, double scale) noexcept
{
	try {
		GlyphOutline out;
		const auto at = [&](const GlyfPoint& p) noexcept { return PdfPoint{p.x * scale, p.y * scale}; };
		const auto mid = [&](const GlyfPoint& a, const GlyfPoint& b) noexcept {
			return PdfPoint{(a.x + b.x) * 0.5 * scale, (a.y + b.y) * 0.5 * scale};
		};
		size_t begin = 0;
		for (const size_t end : glyph.contour_ends) {
			const size_t n = end - begin;
			if (n == 0u) {
				begin = end;
				continue;
			}
			const std::span<const GlyfPoint> contour(glyph.points.data() + begin, n);
			// The start: the first on-curve point, or the midpoint of the first two points when none is on-curve. The
			// walk then visits every other point once, in contour order, and ends back at the start.
			size_t first_on = n;
			for (size_t i = 0; i < n; ++i) {
				if (contour[i].on_curve) {
					first_on = i;
					break;
				}
			}
			const bool off_only = first_on == n;
			const PdfPoint start = off_only ? mid(contour[0], contour[n > 1u ? 1u : 0u]) : at(contour[first_on]);
			const size_t walk_from = off_only ? 1u : first_on + 1u;
			const size_t walk_count = off_only ? n : n - 1u;
			out.verbs.push_back(PathVerb::MoveTo);
			out.points.push_back(start);
			PdfPoint current = start;
			const GlyfPoint* control = nullptr;
			const auto quad_to = [&](const GlyfPoint& c, PdfPoint end_point) {
				const PdfPoint cp = at(c);
				out.verbs.push_back(PathVerb::CubicBezierTo);
				out.points.push_back(PdfPoint{current.x + 2.0 / 3.0 * (cp.x - current.x), current.y + 2.0 / 3.0 * (cp.y - current.y)});
				out.points.push_back(
					PdfPoint{end_point.x + 2.0 / 3.0 * (cp.x - end_point.x), end_point.y + 2.0 / 3.0 * (cp.y - end_point.y)});
				out.points.push_back(end_point);
				current = end_point;
			};
			for (size_t k = 0; k < walk_count; ++k) {
				const GlyfPoint& p = contour[(walk_from + k) % n];
				if (p.on_curve) {
					if (control != nullptr) {
						quad_to(*control, at(p));
						control = nullptr;
					} else {
						out.verbs.push_back(PathVerb::LineTo);
						out.points.push_back(at(p));
						current = at(p);
					}
				} else {
					if (control != nullptr) {
						quad_to(*control, mid(*control, p));
					}
					control = &p;
				}
			}
			// Back to the start: a pending control point curves there; otherwise the close draws the line.
			if (control != nullptr) {
				quad_to(*control, start);
			}
			out.verbs.push_back(PathVerb::CloseSubpath);
			begin = end;
		}
		return out;
	} catch (const std::bad_alloc&) {
		return glyf_error(ErrorCode::OutOfMemory, "Out of memory converting a glyph outline");
	}
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The `glyf` reader
// ---------------------------------------------------------------------------------------------------------------------

// The TrueType tables the `glyf` reader needs, validated once when the font loads: every later read stays inside them.
struct LoadedFont::GlyfTables {
	uint16_t units_per_em{1000};
	uint16_t glyph_count{0};
	uint16_t h_metrics{0};
	int16_t ascender{0};  // hhea, for the vertical phantom points without vmtx
	int16_t descender{0};
	uint16_t v_metrics{0}; // vhea numOfLongVerMetrics, 0 without vertical metrics
	bool long_loca{false};
	TableSpan hmtx{};
	TableSpan vmtx{};
	TableSpan loca{};
	TableSpan glyf{};
};

namespace {

[[nodiscard]] PdfResult<std::unique_ptr<const LoadedFont::GlyfTables>> parse_glyf_tables(const ByteReader& font) noexcept;

// The glyph's data inside `glyf`, from `loca`, or none when its `loca` range runs backwards or past `glyf`: each glyph
// is checked as it is read, so one broken entry costs that glyph, not the font.
[[nodiscard]] std::optional<std::span<const uint8_t>> glyph_data(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	uint16_t glyph) noexcept
{
	uint32_t start = 0;
	uint32_t end = 0;
	if (t.long_loca) {
		(void)font.u32(t.loca.offset + 4u * glyph, start);
		(void)font.u32(t.loca.offset + 4u * (glyph + 1u), end);
	} else {
		uint16_t s = 0;
		uint16_t e = 0;
		(void)font.u16(t.loca.offset + 2u * glyph, s);
		(void)font.u16(t.loca.offset + 2u * (glyph + 1u), e);
		start = 2u * static_cast<uint32_t>(s);
		end = 2u * static_cast<uint32_t>(e);
	}
	if (end < start || end > t.glyf.length) {
		return std::nullopt;
	}
	return font.sub(t.glyf.offset + start, end - start);
}

// Per-call outline budgets (font.h): components visited and points held.
struct ExpansionBudget {
	size_t visits{0};
	size_t points{0};
};

// A glyph's four phantom points (OpenType `glyf`): its origin, its advance point, and its top and bottom, in the
// glyph's own coordinates. Point-matched composites may attach to them, and the origin places the outline.
using PhantomPoints = std::array<GlyfPoint, 4>;

// The glyph's `hmtx` advance and left side bearing: one record per glyph up to `numberOfHMetrics`; later glyphs repeat
// the last advance and take their bearing from the trailing array (validated at load).
void hmtx_entry(const ByteReader& font, const LoadedFont::GlyfTables& t, uint16_t glyph, uint16_t& advance, int16_t& lsb) noexcept
{
	const size_t record = std::min<size_t>(glyph, static_cast<size_t>(t.h_metrics) - 1u);
	(void)font.u16(t.hmtx.offset + 4u * record, advance);
	if (glyph < t.h_metrics) {
		(void)font.i16(t.hmtx.offset + 4u * glyph + 2u, lsb);
	} else {
		(void)font.i16(t.hmtx.offset + 4u * t.h_metrics + 2u * (static_cast<size_t>(glyph) - t.h_metrics), lsb);
	}
}

// The phantom points of `glyph` from its own metrics: the origin sits `lsb` to the left of `x_min`, and the top sits
// the glyph's `vmtx` top side bearing above `y_max`, its advance height above the bottom; a font without vertical
// metrics puts them at the `hhea` ascender and descender.
[[nodiscard]] PhantomPoints own_phantoms(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	uint16_t glyph,
	double x_min,
	double y_max) noexcept
{
	uint16_t advance = 0;
	int16_t lsb = 0;
	hmtx_entry(font, t, glyph, advance, lsb);
	const double origin = x_min - static_cast<double>(lsb);
	double top = static_cast<double>(t.ascender);
	double bottom = static_cast<double>(t.descender);
	if (t.v_metrics != 0u) {
		const size_t record = std::min<size_t>(glyph, static_cast<size_t>(t.v_metrics) - 1u);
		uint16_t advance_height = 0;
		int16_t tsb = 0;
		(void)font.u16(t.vmtx.offset + 4u * record, advance_height);
		if (glyph < t.v_metrics) {
			(void)font.i16(t.vmtx.offset + 4u * glyph + 2u, tsb);
		} else {
			(void)font.i16(t.vmtx.offset + 4u * t.v_metrics + 2u * (static_cast<size_t>(glyph) - t.v_metrics), tsb);
		}
		top = y_max + static_cast<double>(tsb);
		bottom = top - static_cast<double>(advance_height);
	}
	return PhantomPoints{
		GlyfPoint{origin, 0.0, true},
		GlyfPoint{origin + static_cast<double>(advance), 0.0, true},
		GlyfPoint{0.0, top, true},
		GlyfPoint{0.0, bottom, true}};
}

[[nodiscard]] PdfResult<void> expand_glyph(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	uint16_t glyph,
	size_t depth,
	ExpansionBudget& budget,
	ExpandedGlyph& out,
	PhantomPoints& phantoms) noexcept;

[[nodiscard]] PdfResult<void> expand_simple(
	const ByteReader& data,
	int16_t contours,
	ExpansionBudget& budget,
	ExpandedGlyph& out) noexcept
{
	if (contours == 0) {
		return {};
	}
	const size_t contour_count = static_cast<size_t>(contours);
	size_t at = 10u;
	std::vector<size_t> ends;
	try {
		ends.reserve(contour_count);
	} catch (const std::bad_alloc&) {
		return glyf_error(ErrorCode::OutOfMemory, "Out of memory reading a glyph");
	}
	size_t point_count = 0;
	for (size_t i = 0; i < contour_count; ++i) {
		uint16_t end_pt = 0;
		if (!data.u16(at, end_pt)) {
			return glyf_error(ErrorCode::DamagedFormat, "Glyph contour ends are truncated");
		}
		at += 2u;
		// Each contour ends past the one before: a contour has at least one point, so contours never outnumber the
		// points the budget counts.
		const size_t next = static_cast<size_t>(end_pt) + 1u;
		if (next <= point_count) {
			return glyf_error(ErrorCode::DamagedFormat, "Glyph contour ends do not increase");
		}
		point_count = next;
		ends.push_back(point_count);
	}
	// A simple glyph's own 65,535-point ceiling holds by its 16-bit end points; the call's budget is checked here,
	// before any point is stored.
	if (point_count > kMaxExpandedGlyphPoints - std::min(budget.points, kMaxExpandedGlyphPoints)) {
		PdfError error = glyf_error(ErrorCode::ResourceLimit, "Glyph outline holds more points than its limit allows");
		error.refusal_reason = RefusalReason::GlyphPoints;
		error.observed = budget.points + point_count;
		error.bound = kMaxExpandedGlyphPoints;
		return error;
	}
	uint16_t instruction_length = 0;
	if (!data.u16(at, instruction_length)) {
		return glyf_error(ErrorCode::DamagedFormat, "Glyph instructions are truncated");
	}
	at += 2u + instruction_length;
	std::vector<uint8_t> flags;
	try {
		flags.reserve(point_count);
	} catch (const std::bad_alloc&) {
		return glyf_error(ErrorCode::OutOfMemory, "Out of memory reading a glyph");
	}
	while (flags.size() < point_count) {
		uint8_t flag = 0;
		if (!data.u8(at++, flag)) {
			return glyf_error(ErrorCode::DamagedFormat, "Glyph flags are truncated");
		}
		flags.push_back(flag);
		if ((flag & 0x08u) != 0u) { // REPEAT_FLAG
			uint8_t repeat = 0;
			if (!data.u8(at++, repeat)) {
				return glyf_error(ErrorCode::DamagedFormat, "Glyph flags are truncated");
			}
			if (repeat > point_count - flags.size()) {
				return glyf_error(ErrorCode::DamagedFormat, "Glyph flags repeat past the glyph's points");
			}
			flags.insert(flags.end(), repeat, flag);
		}
	}
	const size_t base = out.points.size();
	try {
		out.points.resize(base + point_count);
		out.contour_ends.reserve(out.contour_ends.size() + contour_count);
	} catch (const std::bad_alloc&) {
		return glyf_error(ErrorCode::OutOfMemory, "Out of memory reading a glyph");
	}
	// x then y coordinates: a short (8-bit) delta whose sign the "same or positive" bit gives, else either the same
	// value (bit set) or a signed 16-bit delta.
	for (int axis = 0; axis < 2; ++axis) {
		const uint8_t short_bit = axis == 0 ? 0x02u : 0x04u;
		const uint8_t same_bit = axis == 0 ? 0x10u : 0x20u;
		int32_t value = 0;
		for (size_t i = 0; i < point_count; ++i) {
			const uint8_t flag = flags[i];
			if ((flag & short_bit) != 0u) {
				uint8_t delta = 0;
				if (!data.u8(at++, delta)) {
					return glyf_error(ErrorCode::DamagedFormat, "Glyph coordinates are truncated");
				}
				value += (flag & same_bit) != 0u ? delta : -static_cast<int32_t>(delta);
			} else if ((flag & same_bit) == 0u) {
				int16_t delta = 0;
				if (!data.i16(at, delta)) {
					return glyf_error(ErrorCode::DamagedFormat, "Glyph coordinates are truncated");
				}
				at += 2u;
				value += delta;
			}
			GlyfPoint& p = out.points[base + i];
			(axis == 0 ? p.x : p.y) = static_cast<double>(value);
			p.on_curve = (flag & 0x01u) != 0u;
		}
	}
	for (const size_t end : ends) {
		out.contour_ends.push_back(base + end);
	}
	budget.points += point_count;
	return {};
}

[[nodiscard]] double f2dot14(uint16_t raw) noexcept
{
	return static_cast<double>(static_cast<int16_t>(raw)) / 16384.0;
}

constexpr uint16_t kArgsAreWords = 0x0001u;
constexpr uint16_t kArgsAreXyValues = 0x0002u;
constexpr uint16_t kHaveScale = 0x0008u;
constexpr uint16_t kMoreComponents = 0x0020u;
constexpr uint16_t kHaveXyScale = 0x0040u;
constexpr uint16_t kHaveTwoByTwo = 0x0080u;
constexpr uint16_t kUseMyMetrics = 0x0200u;
constexpr uint16_t kScaledComponentOffset = 0x0800u;
constexpr uint16_t kUnscaledComponentOffset = 0x1000u;

// One component record of a composite glyph.
struct ComponentRecord {
	uint16_t flags{0};
	uint16_t glyph{0};
	int32_t arg1{0};
	int32_t arg2{0};
	double a{1.0}; // x' = a x + c y, y' = b x + d y (the OpenType `glyf` component transform)
	double b{0.0};
	double c{0.0};
	double d{1.0};
};

// Reads the component record at `at`, moving `at` past it.
[[nodiscard]] PdfResult<ComponentRecord> read_component(const ByteReader& data, size_t& at) noexcept
{
	ComponentRecord r{};
	if (!data.u16(at, r.flags) || !data.u16(at + 2u, r.glyph)) {
		return glyf_error(ErrorCode::DamagedFormat, "Composite glyph record is truncated");
	}
	at += 4u;
	const bool xy = (r.flags & kArgsAreXyValues) != 0u;
	if ((r.flags & kArgsAreWords) != 0u) {
		uint16_t a = 0;
		uint16_t b = 0;
		if (!data.u16(at, a) || !data.u16(at + 2u, b)) {
			return glyf_error(ErrorCode::DamagedFormat, "Composite glyph arguments are truncated");
		}
		at += 4u;
		r.arg1 = xy ? static_cast<int16_t>(a) : static_cast<int32_t>(a);
		r.arg2 = xy ? static_cast<int16_t>(b) : static_cast<int32_t>(b);
	} else {
		uint8_t a = 0;
		uint8_t b = 0;
		if (!data.u8(at, a) || !data.u8(at + 1u, b)) {
			return glyf_error(ErrorCode::DamagedFormat, "Composite glyph arguments are truncated");
		}
		at += 2u;
		r.arg1 = xy ? static_cast<int8_t>(a) : static_cast<int32_t>(a);
		r.arg2 = xy ? static_cast<int8_t>(b) : static_cast<int32_t>(b);
	}
	if ((r.flags & kHaveScale) != 0u) {
		uint16_t s = 0;
		if (!data.u16(at, s)) {
			return glyf_error(ErrorCode::DamagedFormat, "Composite glyph scale is truncated");
		}
		at += 2u;
		r.a = r.d = f2dot14(s);
	} else if ((r.flags & kHaveXyScale) != 0u) {
		uint16_t sx = 0;
		uint16_t sy = 0;
		if (!data.u16(at, sx) || !data.u16(at + 2u, sy)) {
			return glyf_error(ErrorCode::DamagedFormat, "Composite glyph scale is truncated");
		}
		at += 4u;
		r.a = f2dot14(sx);
		r.d = f2dot14(sy);
	} else if ((r.flags & kHaveTwoByTwo) != 0u) {
		uint16_t m[4] = {};
		for (size_t i = 0; i < 4u; ++i) {
			if (!data.u16(at + 2u * i, m[i])) {
				return glyf_error(ErrorCode::DamagedFormat, "Composite glyph transform is truncated");
			}
		}
		at += 8u;
		r.a = f2dot14(m[0]);
		r.b = f2dot14(m[1]);
		r.c = f2dot14(m[2]);
		r.d = f2dot14(m[3]);
	}
	return r;
}

[[nodiscard]] PdfResult<void> expand_composite(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	const ByteReader& data,
	uint16_t glyph,
	size_t depth,
	ExpansionBudget& budget,
	ExpandedGlyph& out,
	PhantomPoints& phantoms) noexcept
{
	int16_t x_min = 0;
	int16_t y_max = 0;
	(void)data.i16(2u, x_min);
	(void)data.i16(8u, y_max);
	phantoms = own_phantoms(font, t, glyph, static_cast<double>(x_min), static_cast<double>(y_max));
	const size_t composite_base = out.points.size();
	size_t at = 10u;
	for (;;) {
		RECTO_PDF_TRY(const ComponentRecord r, read_component(data, at));
		if (r.glyph >= t.glyph_count) {
			return glyf_error(ErrorCode::DamagedFormat, "Composite glyph names a glyph past the font's glyph count");
		}
		// Counted before the component is expanded, so a compact font cannot spend more than the budget.
		if (++budget.visits > kMaxComponentVisits) {
			PdfError error = glyf_error(ErrorCode::ResourceLimit, "Composite glyph visits more components than its limit");
			error.refusal_reason = RefusalReason::ComponentVisits;
			error.observed = budget.visits;
			error.bound = kMaxComponentVisits;
			return error;
		}
		const size_t child_base = out.points.size();
		PhantomPoints child_phantoms{};
		RECTO_PDF_RETURN_IF_ERROR(expand_glyph(font, t, r.glyph, depth + 1u, budget, out, child_phantoms));
		const auto transform = [&](GlyfPoint& p) noexcept {
			const double x = p.x;
			p.x = r.a * x + r.c * p.y;
			p.y = r.b * x + r.d * p.y;
		};
		for (size_t i = child_base; i < out.points.size(); ++i) {
			transform(out.points[i]);
		}
		for (GlyfPoint& p : child_phantoms) {
			transform(p);
		}
		double dx = 0.0;
		double dy = 0.0;
		if ((r.flags & kArgsAreXyValues) != 0u) {
			dx = r.arg1;
			dy = r.arg2;
			if ((r.flags & kScaledComponentOffset) != 0u && (r.flags & kUnscaledComponentOffset) == 0u) {
				const double ox = dx;
				dx = r.a * ox + r.c * dy;
				dy = r.b * ox + r.d * dy;
			}
		} else {
			// Point matching: the component's point `arg2` lands on the composite's point `arg1` placed so far. Either
			// may be a phantom point: the four after the glyph's own points.
			const size_t placed = child_base - composite_base;
			const size_t child_count = out.points.size() - child_base;
			const size_t p1 = static_cast<size_t>(r.arg1);
			const size_t p2 = static_cast<size_t>(r.arg2);
			if (p1 >= placed + 4u || p2 >= child_count + 4u) {
				return glyf_error(ErrorCode::DamagedFormat, "Composite glyph anchors name points that do not exist");
			}
			const GlyfPoint& parent = p1 < placed ? out.points[composite_base + p1] : phantoms[p1 - placed];
			const GlyfPoint& child = p2 < child_count ? out.points[child_base + p2] : child_phantoms[p2 - child_count];
			dx = parent.x - child.x;
			dy = parent.y - child.y;
		}
		for (size_t i = child_base; i < out.points.size(); ++i) {
			out.points[i].x += dx;
			out.points[i].y += dy;
		}
		if ((r.flags & kUseMyMetrics) != 0u) {
			// The composite takes this component's metrics: its placed phantom points.
			for (size_t i = 0; i < phantoms.size(); ++i) {
				phantoms[i] = GlyfPoint{child_phantoms[i].x + dx, child_phantoms[i].y + dy, true};
			}
		}
		if ((r.flags & kMoreComponents) == 0u) {
			return {};
		}
	}
}

PdfResult<void> expand_glyph(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	uint16_t glyph,
	size_t depth,
	ExpansionBudget& budget,
	ExpandedGlyph& out,
	PhantomPoints& phantoms) noexcept
{
	if (depth > kMaxCompositeDepth) {
		PdfError error = glyf_error(ErrorCode::ResourceLimit, "Composite glyphs nest deeper than their limit");
		error.refusal_reason = RefusalReason::CompositeDepth;
		error.observed = depth;
		error.bound = kMaxCompositeDepth;
		return error;
	}
	const auto span = glyph_data(font, t, glyph);
	if (!span.has_value()) {
		return glyf_error(ErrorCode::DamagedFormat, "Glyph loca range runs backwards or past the glyf table");
	}
	const ByteReader data(*span);
	if (data.size() == 0u) {
		// An empty glyph, such as a space: its origin is where its bearing puts it from nothing, at 0.
		uint16_t advance = 0;
		int16_t lsb = 0;
		hmtx_entry(font, t, glyph, advance, lsb);
		phantoms = own_phantoms(font, t, glyph, static_cast<double>(lsb), 0.0);
		return {};
	}
	int16_t contours = 0;
	int16_t x_min = 0;
	int16_t y_max = 0;
	if (data.size() < 10u || !data.i16(0u, contours) || !data.i16(2u, x_min) || !data.i16(8u, y_max)) {
		return glyf_error(ErrorCode::DamagedFormat, "Glyph header is truncated");
	}
	if (contours >= 0) {
		phantoms = own_phantoms(font, t, glyph, static_cast<double>(x_min), static_cast<double>(y_max));
		return expand_simple(data, contours, budget, out);
	}
	return expand_composite(font, t, data, glyph, depth, budget, out, phantoms);
}

// The glyph expanded and placed at its origin (its first phantom point), as a renderer draws it.
[[nodiscard]] PdfResult<ExpandedGlyph> expand_placed(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	uint16_t glyph) noexcept
{
	ExpandedGlyph expanded;
	ExpansionBudget budget;
	PhantomPoints phantoms{};
	RECTO_PDF_RETURN_IF_ERROR(expand_glyph(font, t, glyph, 0u, budget, expanded, phantoms));
	for (GlyfPoint& p : expanded.points) {
		p.x -= phantoms[0].x;
		p.y -= phantoms[0].y;
	}
	return expanded;
}

// The glyph whose metrics a glyph uses: itself, or the component a composite marks USE_MY_METRICS, followed through
// nested composites. The records read count against the same per-call budgets as an expansion (`ResourceLimit` past
// them), so a composite of millions of records cannot make a metrics query long.
[[nodiscard]] PdfResult<uint16_t> metrics_glyph(const ByteReader& font, const LoadedFont::GlyfTables& t, uint16_t glyph) noexcept
{
	size_t visits = 0;
	for (size_t depth = 0;; ++depth) {
		if (depth > kMaxCompositeDepth) {
			PdfError error = glyf_error(ErrorCode::ResourceLimit, "Composite glyphs nest deeper than their limit");
			error.refusal_reason = RefusalReason::CompositeDepth;
			error.observed = depth;
			error.bound = kMaxCompositeDepth;
			return error;
		}
		const auto span = glyph_data(font, t, glyph);
		if (!span.has_value()) {
			return glyf_error(ErrorCode::DamagedFormat, "Glyph loca range runs backwards or past the glyf table");
		}
		const ByteReader data(*span);
		int16_t contours = 0;
		if (data.size() < 10u || !data.i16(0u, contours) || contours >= 0) {
			return glyph;
		}
		size_t at = 10u;
		std::optional<uint16_t> next;
		for (;;) {
			if (++visits > kMaxComponentVisits) {
				PdfError error = glyf_error(ErrorCode::ResourceLimit, "Composite glyph visits more components than its limit");
				error.refusal_reason = RefusalReason::ComponentVisits;
				error.observed = visits;
				error.bound = kMaxComponentVisits;
				return error;
			}
			RECTO_PDF_TRY(const ComponentRecord r, read_component(data, at));
			if ((r.flags & kUseMyMetrics) != 0u && r.glyph < t.glyph_count) {
				next = r.glyph;
			}
			if ((r.flags & kMoreComponents) == 0u) {
				break;
			}
		}
		if (!next.has_value()) {
			return glyph;
		}
		glyph = *next;
	}
}

PdfResult<std::unique_ptr<const LoadedFont::GlyfTables>> parse_glyf_tables(const ByteReader& font) noexcept
{
	RECTO_PDF_TRY(const TableSpan head, find_table(font, "head"));
	RECTO_PDF_TRY(const TableSpan maxp, find_table(font, "maxp"));
	RECTO_PDF_TRY(const TableSpan hhea, find_table(font, "hhea"));
	RECTO_PDF_TRY(const TableSpan hmtx, find_table(font, "hmtx"));
	RECTO_PDF_TRY(const TableSpan loca, find_table(font, "loca"));
	RECTO_PDF_TRY(const TableSpan glyf, find_table(font, "glyf"));
	if (!head.present || !maxp.present || !hhea.present || !hmtx.present || !loca.present || !glyf.present) {
		return glyf_error(ErrorCode::DamagedFormat, "Font program lacks a table its glyf outlines need");
	}
	LoadedFont::GlyfTables t{};
	int16_t loca_format = 0;
	uint16_t glyph_count = 0;
	if (head.length < 54u || maxp.length < 6u || hhea.length < 36u || !font.u16(head.offset + 18u, t.units_per_em) ||
		!font.i16(head.offset + 50u, loca_format) || !font.u16(maxp.offset + 4u, glyph_count) ||
		!font.u16(hhea.offset + 34u, t.h_metrics) || !font.i16(hhea.offset + 4u, t.ascender) ||
		!font.i16(hhea.offset + 6u, t.descender)) {
		return glyf_error(ErrorCode::DamagedFormat, "Font head, maxp or hhea table is truncated");
	}
	if (t.units_per_em < 16u || t.units_per_em > 16384u || (loca_format != 0 && loca_format != 1) || glyph_count == 0u ||
		t.h_metrics == 0u) {
		return glyf_error(ErrorCode::DamagedFormat, "Font head, maxp or hhea values are out of range");
	}
	// A `numberOfHMetrics` past the glyph count (DirectWrite refuses it) names records no glyph uses: the first ones
	// are read (clamped below, once the glyph count `loca` can hold is known).
	t.long_loca = loca_format == 1;
	t.hmtx = hmtx;
	t.loca = loca;
	t.glyf = glyf;
	// A maxp count past the glyphs `loca` can hold (DirectWrite accepts it) reads the glyphs that exist; each glyph's
	// `loca` range is checked when it is read.
	const size_t loca_glyphs = loca.length / (t.long_loca ? 4u : 2u);
	if (loca_glyphs < 2u) {
		return glyf_error(ErrorCode::DamagedFormat, "Font loca table holds no glyph");
	}
	t.glyph_count = static_cast<uint16_t>(std::min<size_t>(glyph_count, loca_glyphs - 1u));
	t.h_metrics = std::min(t.h_metrics, t.glyph_count);
	const size_t hmtx_needed = 4u * t.h_metrics + 2u * (static_cast<size_t>(t.glyph_count) - t.h_metrics);
	if (hmtx.length < hmtx_needed) {
		return glyf_error(ErrorCode::DamagedFormat, "Font hmtx table is shorter than its glyph count needs");
	}
	// Vertical metrics, when both tables are there and hold what `vhea` says.
	RECTO_PDF_TRY(const TableSpan vhea, find_table(font, "vhea"));
	RECTO_PDF_TRY(const TableSpan vmtx, find_table(font, "vmtx"));
	uint16_t v_metrics = 0;
	if (vhea.present && vmtx.present && vhea.length >= 36u && font.u16(vhea.offset + 34u, v_metrics) && v_metrics != 0u) {
		v_metrics = std::min(v_metrics, t.glyph_count);
		if (vmtx.length >= 4u * v_metrics + 2u * (static_cast<size_t>(t.glyph_count) - v_metrics)) {
			t.v_metrics = v_metrics;
			t.vmtx = vmtx;
		}
	}
	try {
		return std::unique_ptr<const LoadedFont::GlyfTables>(new LoadedFont::GlyfTables(t));
	} catch (const std::bad_alloc&) {
		return glyf_error(ErrorCode::OutOfMemory, "Out of memory loading a font");
	}
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// DirectWrite faces
// ---------------------------------------------------------------------------------------------------------------------

// The face and the registry key of its program. The face is released before the key leaves the registry.
struct LoadedFont::DWriteState {
	uint64_t key{0};
	ComPtr<IDWriteFontFace> face;

	DWriteState() noexcept = default;
	DWriteState(const DWriteState&) = delete;
	DWriteState& operator=(const DWriteState&) = delete;

	~DWriteState() noexcept
	{
		face.Reset();
		const std::lock_guard lock(g_font_registry_mutex);
		g_font_registry.erase(key);
	}
};

namespace {

// Collects a DirectWrite glyph outline (`GetGlyphRunOutline`, y down in DIPs) as text-space verbs and points (y up).
class OutlineSink final : public IDWriteGeometrySink {
public:
	explicit OutlineSink(double scale) noexcept : scale_(scale) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) noexcept override
	{
		if (object == nullptr) {
			return E_POINTER;
		}
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteGeometrySink)) {
			*object = static_cast<IDWriteGeometrySink*>(this);
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	// Lives on the caller's stack for one call: reference counting is not needed.
	ULONG STDMETHODCALLTYPE AddRef() noexcept override
	{
		return 1;
	}

	ULONG STDMETHODCALLTYPE Release() noexcept override
	{
		return 1;
	}

	void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) noexcept override {}

	void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) noexcept override {}

	void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F start, D2D1_FIGURE_BEGIN) noexcept override
	{
		add(PathVerb::MoveTo, {start});
	}

	void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 count) noexcept override
	{
		for (UINT32 i = 0; i < count; ++i) {
			add(PathVerb::LineTo, {points[i]});
		}
	}

	void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 count) noexcept override
	{
		for (UINT32 i = 0; i < count; ++i) {
			add(PathVerb::CubicBezierTo, {beziers[i].point1, beziers[i].point2, beziers[i].point3});
		}
	}

	void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) noexcept override
	{
		add(PathVerb::CloseSubpath, {});
	}

	HRESULT STDMETHODCALLTYPE Close() noexcept override
	{
		return S_OK;
	}

	[[nodiscard]] PdfResult<GlyphOutline> take() noexcept
	{
		if (out_of_memory_) {
			return font_error(ErrorCode::OutOfMemory, "Out of memory collecting a glyph outline", DecoderStep::DWriteFontFace);
		}
		if (over_budget_) {
			PdfError error = font_error(ErrorCode::ResourceLimit, "Glyph outline holds more points than its limit allows",
				DecoderStep::DWriteFontFace);
			error.refusal_reason = RefusalReason::GlyphPoints;
			error.observed = outline_.points.size() + 1u;
			error.bound = kMaxExpandedGlyphPoints;
			return error;
		}
		return std::move(outline_);
	}

private:
	void add(PathVerb verb, std::initializer_list<D2D1_POINT_2F> points) noexcept
	{
		if (out_of_memory_ || over_budget_) {
			return;
		}
		if (points.size() > kMaxExpandedGlyphPoints - outline_.points.size()) {
			over_budget_ = true;
			return;
		}
		try {
			outline_.verbs.push_back(verb);
			for (const D2D1_POINT_2F& p : points) {
				outline_.points.push_back(PdfPoint{static_cast<double>(p.x) * scale_, -static_cast<double>(p.y) * scale_});
			}
		} catch (const std::bad_alloc&) {
			out_of_memory_ = true;
		}
	}

	double scale_{1.0};
	GlyphOutline outline_{};
	bool out_of_memory_{false};
	bool over_budget_{false};
};

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// LoadedFont
// ---------------------------------------------------------------------------------------------------------------------

LoadedFont::LoadedFont(
	LoadedFontKind kind,
	std::shared_ptr<const std::vector<uint8_t>> program,
	uint16_t units_per_em,
	uint16_t glyph_count,
	std::unique_ptr<const DWriteState> dwrite,
	std::unique_ptr<const GlyfTables> glyf,
	std::unique_ptr<const CharstringProgram> charstrings) noexcept
	: kind_(kind)
	, program_(std::move(program))
	, units_per_em_(units_per_em)
	, glyph_count_(glyph_count)
	, dwrite_(std::move(dwrite))
	, glyf_(std::move(glyf))
	, charstrings_(std::move(charstrings))
{
}

LoadedFont::~LoadedFont() noexcept = default;

IDWriteFontFace* LoadedFont::dwrite_face() const noexcept
{
	return dwrite_ != nullptr ? dwrite_->face.Get() : nullptr;
}

PdfResult<double> LoadedFont::advance(uint16_t glyph) const noexcept
{
	return recorded([&]() noexcept -> PdfResult<double> {
		if (glyph >= glyph_count_) {
			return font_error(ErrorCode::DamagedFormat, "Glyph index past the font's glyph count", DecoderStep::None);
		}
		if (charstrings_ != nullptr) {
			return charstrings::advance(*charstrings_, *program_, glyph);
		}
		const double scale = 1.0 / static_cast<double>(units_per_em_);
		if (dwrite_ != nullptr) {
			RECTO_PDF_TRY(const uint16_t metrics_of, dwrite_metrics_glyph(glyph));
			DWRITE_GLYPH_METRICS metrics{};
			const HRESULT hr = dwrite_->face->GetDesignGlyphMetrics(&metrics_of, 1u, &metrics, FALSE);
			if (FAILED(hr)) {
				return dwrite_failure(hr, "GetDesignGlyphMetrics", DecoderStep::DWriteFontFace);
			}
			return static_cast<double>(metrics.advanceWidth) * scale;
		}
		// The `hmtx` advance of the glyph whose metrics this one uses (USE_MY_METRICS).
		const ByteReader font(*program_);
		RECTO_PDF_TRY(const uint16_t metrics, metrics_glyph(font, *glyf_, glyph));
		uint16_t width = 0;
		int16_t lsb = 0;
		hmtx_entry(font, *glyf_, metrics, width, lsb);
		return static_cast<double>(width) * scale;
	}());
}

namespace {

// The box of a placed glyph's points, on- and off-curve, which holds its outline (a quadratic stays inside the hull of
// its control points), in text space; none for a glyph with no points.
[[nodiscard]] std::optional<PdfRect> points_box(const ExpandedGlyph& glyph, double scale) noexcept
{
	if (glyph.points.empty()) {
		return std::nullopt;
	}
	double x_min = glyph.points[0].x;
	double y_min = glyph.points[0].y;
	double x_max = x_min;
	double y_max = y_min;
	for (const GlyfPoint& p : glyph.points) {
		x_min = std::min(x_min, p.x);
		y_min = std::min(y_min, p.y);
		x_max = std::max(x_max, p.x);
		y_max = std::max(y_max, p.y);
	}
	return PdfRect{x_min * scale, y_min * scale, x_max * scale, y_max * scale};
}

// DirectWrite's black box from the side bearings: left and right from the horizontal origin and advance, top from
// the vertical origin (design units, y up from the baseline), in text space.
[[nodiscard]] PdfResult<std::optional<PdfRect>> dwrite_ink_box(IDWriteFontFace* face, uint16_t glyph, double scale) noexcept
{
	DWRITE_GLYPH_METRICS m{};
	const HRESULT hr = face->GetDesignGlyphMetrics(&glyph, 1u, &m, FALSE);
	if (FAILED(hr)) {
		return dwrite_failure(hr, "GetDesignGlyphMetrics", DecoderStep::DWriteFontFace);
	}
	const double width = static_cast<double>(m.advanceWidth) - m.leftSideBearing - m.rightSideBearing;
	const double height = static_cast<double>(m.advanceHeight) - m.topSideBearing - m.bottomSideBearing;
	if (!(width > 0.0) || !(height > 0.0)) {
		return std::optional<PdfRect>{};
	}
	const double top = static_cast<double>(m.verticalOriginY) - m.topSideBearing;
	return std::optional<PdfRect>(
		PdfRect{m.leftSideBearing * scale, (top - height) * scale, (m.leftSideBearing + width) * scale, top * scale});
}

// A DirectWrite outline; an em size of the font's units per em gives it in design units, which the sink scales.
[[nodiscard]] PdfResult<GlyphOutline> dwrite_outline(IDWriteFontFace* face, uint16_t glyph, uint16_t units_per_em) noexcept
{
	OutlineSink sink(1.0 / static_cast<double>(units_per_em));
	const HRESULT hr =
		face->GetGlyphRunOutline(static_cast<FLOAT>(units_per_em), &glyph, nullptr, nullptr, 1u, FALSE, FALSE, &sink);
	if (FAILED(hr)) {
		return dwrite_failure(hr, "GetGlyphRunOutline", DecoderStep::DWriteFontFace);
	}
	return sink.take();
}

} // namespace

PdfResult<uint16_t> LoadedFont::dwrite_metrics_glyph(uint16_t glyph) const noexcept
{
	// The advance follows USE_MY_METRICS on both routes (the OpenType glyf rule; DirectWrite itself keeps the
	// composite's own hmtx value), so a font gives the same advance however it loaded.
	if (glyf_ == nullptr || glyph >= glyf_->glyph_count) {
		return glyph;
	}
	return metrics_glyph(ByteReader(*program_), *glyf_, glyph);
}

PdfResult<std::optional<PdfRect>> LoadedFont::ink_box(uint16_t glyph) const noexcept
{
	return recorded([&]() noexcept -> PdfResult<std::optional<PdfRect>> {
		if (glyph >= glyph_count_) {
			return font_error(ErrorCode::DamagedFormat, "Glyph index past the font's glyph count", DecoderStep::None);
		}
		if (charstrings_ != nullptr) {
			RECTO_PDF_TRY(const GlyphShape shape, charstrings::shape(*charstrings_, *program_, glyph));
			return shape.ink_box;
		}
		const double scale = 1.0 / static_cast<double>(units_per_em_);
		if (dwrite_ != nullptr) {
			return dwrite_ink_box(dwrite_->face.Get(), glyph, scale);
		}
		RECTO_PDF_TRY(const ExpandedGlyph placed, expand_placed(ByteReader(*program_), *glyf_, glyph));
		return points_box(placed, scale);
	}());
}

PdfResult<void> LoadedFont::glyf_check(uint16_t glyph) const noexcept
{
	// The glyf expansion with its budgets, before DirectWrite or a renderer expands the glyph itself. A glyph the
	// glyf tables do not hold (DirectWrite counted more) cannot be bounded, so it is refused.
	if (glyf_ == nullptr) {
		return {};
	}
	if (glyph >= glyf_->glyph_count) {
		return font_error(ErrorCode::DamagedFormat, "Glyph past the glyphs the font's loca table holds", DecoderStep::NativeGlyf);
	}
	RECTO_PDF_TRY(const ExpandedGlyph placed, expand_placed(ByteReader(*program_), *glyf_, glyph));
	(void)placed;
	return {};
}

PdfResult<void> LoadedFont::check_glyph(uint16_t glyph) const noexcept
{
	return recorded([&]() noexcept -> PdfResult<void> {
		if (glyph >= glyph_count_) {
			return font_error(ErrorCode::DamagedFormat, "Glyph index past the font's glyph count", DecoderStep::None);
		}
		if (charstrings_ != nullptr) {
			// The glyph's whole program runs under its budgets, so a renderer never meets an unbounded one.
			RECTO_PDF_TRY(const GlyphShape shape, charstrings::shape(*charstrings_, *program_, glyph));
			(void)shape;
			return {};
		}
		RECTO_PDF_RETURN_IF_ERROR(glyf_check(glyph));
		return {};
	}());
}

PdfResult<GlyphOutline> LoadedFont::outline(uint16_t glyph) const noexcept
{
	return recorded([&]() noexcept -> PdfResult<GlyphOutline> {
		if (glyph >= glyph_count_) {
			return font_error(ErrorCode::DamagedFormat, "Glyph index past the font's glyph count", DecoderStep::None);
		}
		if (charstrings_ != nullptr) {
			RECTO_PDF_TRY(GlyphShape shape, charstrings::shape(*charstrings_, *program_, glyph));
			return std::move(shape.outline);
		}
		if (dwrite_ != nullptr) {
			RECTO_PDF_RETURN_IF_ERROR(glyf_check(glyph));
			return dwrite_outline(dwrite_->face.Get(), glyph, units_per_em_);
		}
		RECTO_PDF_TRY(const ExpandedGlyph placed, expand_placed(ByteReader(*program_), *glyf_, glyph));
		return outline_from_points(placed, 1.0 / static_cast<double>(units_per_em_));
	}());
}

PdfResult<GlyphShape> LoadedFont::shape(uint16_t glyph) const noexcept
{
	return recorded([&]() noexcept -> PdfResult<GlyphShape> {
		if (glyph >= glyph_count_) {
			return font_error(ErrorCode::DamagedFormat, "Glyph index past the font's glyph count", DecoderStep::None);
		}
		if (charstrings_ != nullptr) {
			return charstrings::shape(*charstrings_, *program_, glyph);
		}
		const double scale = 1.0 / static_cast<double>(units_per_em_);
		GlyphShape shape{};
		if (dwrite_ != nullptr) {
			RECTO_PDF_TRY(const uint16_t metrics_of, dwrite_metrics_glyph(glyph));
			DWRITE_GLYPH_METRICS metrics{};
			const HRESULT hr = dwrite_->face->GetDesignGlyphMetrics(&metrics_of, 1u, &metrics, FALSE);
			if (FAILED(hr)) {
				return dwrite_failure(hr, "GetDesignGlyphMetrics", DecoderStep::DWriteFontFace);
			}
			shape.advance = static_cast<double>(metrics.advanceWidth) * scale;
			RECTO_PDF_TRY(shape.ink_box, dwrite_ink_box(dwrite_->face.Get(), glyph, scale));
			RECTO_PDF_RETURN_IF_ERROR(glyf_check(glyph));
			RECTO_PDF_TRY(shape.outline, dwrite_outline(dwrite_->face.Get(), glyph, units_per_em_));
			return shape;
		}
		// One expansion gives the ink box and the outline.
		const ByteReader font(*program_);
		RECTO_PDF_TRY(const uint16_t metrics, metrics_glyph(font, *glyf_, glyph));
		uint16_t width = 0;
		int16_t lsb = 0;
		hmtx_entry(font, *glyf_, metrics, width, lsb);
		shape.advance = static_cast<double>(width) * scale;
		RECTO_PDF_TRY(const ExpandedGlyph placed, expand_placed(font, *glyf_, glyph));
		shape.ink_box = points_box(placed, scale);
		RECTO_PDF_TRY(shape.outline, outline_from_points(placed, scale));
		return shape;
	}());
}

std::optional<uint16_t> LoadedFont::glyph_for_name(std::string_view name) const noexcept
{
	if (charstrings_ == nullptr) {
		return std::nullopt;
	}
	const auto& names = charstrings_->names;
	const auto it = std::lower_bound(
		names.begin(), names.end(), name, [](const auto& entry, std::string_view key) { return entry.first < key; });
	if (it == names.end() || it->first != name) {
		return std::nullopt;
	}
	return it->second;
}

std::optional<uint16_t> LoadedFont::glyph_for_cid(uint32_t cid) const noexcept
{
	if (charstrings_ == nullptr) {
		return std::nullopt;
	}
	const auto& cids = charstrings_->cids;
	const auto it =
		std::lower_bound(cids.begin(), cids.end(), cid, [](const auto& entry, uint32_t key) { return entry.first < key; });
	if (it == cids.end() || it->first != cid) {
		return std::nullopt;
	}
	return it->second;
}

bool LoadedFont::cid_keyed() const noexcept
{
	return charstrings_ != nullptr && charstrings_->kind == CharstringProgram::Kind::CffCid;
}

const GlyphNames* LoadedFont::built_in_encoding() const noexcept
{
	if (charstrings_ == nullptr || !charstrings_->built_in.has_value()) {
		return nullptr;
	}
	return &*charstrings_->built_in;
}

// ---------------------------------------------------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------------------------------------------------

namespace {

// The program read by the `glyf` reader alone (DirectWrite refused it), or the refusal that leaves it unusable.
[[nodiscard]] PdfResult<std::unique_ptr<const LoadedFont::GlyfTables>> glyf_after_rejection(
	const std::vector<uint8_t>& program,
	HRESULT rejection) noexcept;

} // namespace

PdfResult<std::shared_ptr<const LoadedFont>> load_embedded_font(
	std::shared_ptr<const std::vector<uint8_t>> program,
	FontProgramFormat format) noexcept
{
	return recorded([&]() noexcept -> PdfResult<std::shared_ptr<const LoadedFont>> {
		if (program == nullptr || program->size() < 12u) {
			return font_error(ErrorCode::DamagedFormat, "Embedded font program is empty or truncated", DecoderStep::NativeGlyf);
		}
		if (program->size() > kMaxDecodedStreamBytes) {
			PdfError error = font_error(
				ErrorCode::ResourceLimit, "Embedded font program is larger than its limit allows", DecoderStep::NativeGlyf);
			error.refusal_reason = RefusalReason::FontProgramBytes;
			error.observed = program->size();
			error.bound = kMaxDecodedStreamBytes;
			return error;
		}
		(void)format; // TrueType and OpenType are told apart by the program's own sfnt version, which DirectWrite reads

		IDWriteFactory5* factory = nullptr;
		if (const HRESULT hr = shared_factory(factory); FAILED(hr)) {
			return dwrite_failure(hr, "CreateFactory", DecoderStep::DWriteFactory);
		}
	FontProgramLoader* loader = nullptr;
		if (const HRESULT hr = shared_loader(factory, loader); FAILED(hr)) {
			return dwrite_failure(hr, "RegisterFontFileLoader", DecoderStep::DWriteFontLoader);
		}

		// The program's registry entry, owned from here by `state` so every early return removes it again.
		std::unique_ptr<LoadedFont::DWriteState> state;
		try {
			state = std::make_unique<LoadedFont::DWriteState>();
			const std::lock_guard lock(g_font_registry_mutex);
			state->key = g_next_font_key++;
			g_font_registry.emplace(state->key, program);
		} catch (const std::bad_alloc&) {
			return font_error(ErrorCode::OutOfMemory, "Out of memory loading a font", DecoderStep::DWriteFontReference);
		}
		ComPtr<IDWriteFontFile> file;
		if (const HRESULT hr = dwrite_step(detail::FontLoaderStage::Reference, [&]() -> HRESULT {
				return factory->CreateCustomFontFileReference(
					&state->key, static_cast<UINT32>(sizeof(state->key)), loader, file.ReleaseAndGetAddressOf());
			});
			FAILED(hr)) {
			return dwrite_failure(hr, "CreateCustomFontFileReference", DecoderStep::DWriteFontReference);
		}

		ComPtr<IDWriteFontFace> face;
		const HRESULT face_hr = dwrite_step(detail::FontLoaderStage::Face, [&]() -> HRESULT {
			BOOL supported = FALSE;
			DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
			DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
			UINT32 faces = 0;
			HRESULT hr = file->Analyze(&supported, &file_type, &face_type, &faces);
			if (FAILED(hr)) {
				return hr;
			}
			if (!supported || faces == 0u) {
				return DWRITE_E_FILEFORMAT;
			}
			IDWriteFontFile* files[] = {file.Get()};
			hr = factory->CreateFontFace(face_type, 1u, files, 0u, DWRITE_FONT_SIMULATIONS_NONE, face.ReleaseAndGetAddressOf());
			if (FAILED(hr)) {
				return hr;
			}
			// DirectWrite reads some tables only when they are used: asking for metrics and one glyph now finds a program
			// it cannot use before the font is handed out.
			DWRITE_FONT_METRICS metrics{};
			face->GetMetrics(&metrics);
			if (metrics.designUnitsPerEm == 0u || face->GetGlyphCount() == 0u) {
				return DWRITE_E_FILEFORMAT;
			}
			UINT16 probe = 0;
			DWRITE_GLYPH_METRICS glyph_metrics{};
			return face->GetDesignGlyphMetrics(&probe, 1u, &glyph_metrics, FALSE);
		});
		if (FAILED(face_hr)) {
			if (!is_font_rejection(face_hr)) {
				return dwrite_failure(face_hr, "CreateFontFace", DecoderStep::DWriteFontFace);
			}
			RECTO_PDF_TRY(std::unique_ptr<const LoadedFont::GlyfTables> tables, glyf_after_rejection(*program, face_hr));
			const uint16_t units_per_em = tables->units_per_em;
			const uint16_t glyph_count = tables->glyph_count;
			try {
				return std::shared_ptr<const LoadedFont>(new LoadedFont(
					LoadedFontKind::OutlineOnly, std::move(program), units_per_em, glyph_count, nullptr, std::move(tables), nullptr));
			} catch (const std::bad_alloc&) {
				return glyf_error(ErrorCode::OutOfMemory, "Out of memory loading a font");
			}
		}

		DWRITE_FONT_METRICS metrics{};
		face->GetMetrics(&metrics);
		const UINT16 glyph_count = face->GetGlyphCount();
		const LoadedFontKind kind =
			face->GetType() == DWRITE_FONT_FACE_TYPE_CFF ? LoadedFontKind::OpenType : LoadedFontKind::TrueType;
		// A TrueType face keeps its glyf tables too, so `check_glyph` and `outline` can bound a glyph before DirectWrite
		// expands it.
		std::unique_ptr<const LoadedFont::GlyfTables> glyf_tables;
		if (kind == LoadedFontKind::TrueType) {
			// Every TrueType glyph is bounded before DirectWrite expands it, so a program whose glyf tables cannot be
			// read is refused, whatever DirectWrite thinks of it; a broken `loca` entry costs only its own glyph.
			RECTO_PDF_TRY(glyf_tables, parse_glyf_tables(ByteReader(*program)));
		}
		try {
			state->face = std::move(face);
			return std::shared_ptr<const LoadedFont>(new LoadedFont(
				kind, std::move(program), metrics.designUnitsPerEm, glyph_count, std::move(state), std::move(glyf_tables), nullptr));
		} catch (const std::bad_alloc&) {
			return font_error(ErrorCode::OutOfMemory, "Out of memory loading a font", DecoderStep::DWriteFontFace);
		}
	}());
}

PdfResult<std::shared_ptr<const LoadedFont>> load_charstring_font(
	std::shared_ptr<const std::vector<uint8_t>> program,
	CharstringFormat format,
	Type1Lengths lengths) noexcept
{
	return recorded([&]() noexcept -> PdfResult<std::shared_ptr<const LoadedFont>> {
		if (program == nullptr || program->empty()) {
			return font_error(ErrorCode::DamagedFormat, "Embedded font program is empty", DecoderStep::NativeCharstring);
		}
		RECTO_PDF_TRY(std::unique_ptr<const LoadedFont::CharstringProgram> parsed,
			charstrings::parse(*program, format, lengths));
		const uint16_t glyph_count = parsed->glyph_count;
		try {
			// Units per em are the charstring units of the default matrix; every outline is already in text space.
			return std::shared_ptr<const LoadedFont>(new LoadedFont(LoadedFontKind::OutlineOnly, std::move(program),
				uint16_t{1000u}, glyph_count, nullptr, nullptr, std::move(parsed)));
		} catch (const std::bad_alloc&) {
			return font_error(ErrorCode::OutOfMemory, "Out of memory loading a font", DecoderStep::NativeCharstring);
		}
	}());
}

namespace {

PdfResult<std::unique_ptr<const LoadedFont::GlyfTables>> glyf_after_rejection(
	const std::vector<uint8_t>& program,
	HRESULT rejection) noexcept
{
	auto tables = parse_glyf_tables(ByteReader(program));
	if (!tables.has_value()) {
		// No usable glyf outlines either: the glyf reader's refusal, after DirectWrite's (kept as the prior step).
		PdfError error = std::move(tables.error());
		error.prior_step = DecoderStep::DWriteFontFace;
		error.prior_status = static_cast<uint32_t>(rejection);
		return error;
	}
	return std::move(*tables);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Standard 14 faces (D01 T02 section 27)
// ---------------------------------------------------------------------------------------------------------------------

namespace detail {

thread_local std::optional<Standard14Font> g_standard14_family_font;
thread_local std::wstring_view g_standard14_family;

ScopedStandard14Family::ScopedStandard14Family(Standard14Font font, std::wstring_view family) noexcept
	: previous_font_(g_standard14_family_font)
	, previous_family_(g_standard14_family)
{
	g_standard14_family_font = font;
	g_standard14_family = family;
}

ScopedStandard14Family::~ScopedStandard14Family() noexcept
{
	g_standard14_family_font = previous_font_;
	g_standard14_family = previous_family_;
}

} // namespace detail

namespace {

// The largest system font file a standard face is read from: Windows' own faces are a few megabytes.
constexpr uint64_t kMaxStandard14FaceBytes = uint64_t{64} << 20u;

// The faces loaded so far, each kept for the life of the process once loaded (a failure is not kept). One caller at a
// time loads each font, so callers that ask at once wait for the one read and decode instead of each doing their own.
std::mutex g_standard14_mutex;
std::array<std::shared_ptr<const LoadedFont>, kStandard14FontCount> g_standard14_faces;
std::array<std::mutex, kStandard14FontCount> g_standard14_load_mutexes;
std::atomic<uint64_t> g_standard14_face_reads{0};

// A family or face Windows does not have: named, never replaced by another font.
[[nodiscard]] PdfError standard14_face_missing(std::wstring_view family, Standard14Font font, std::string_view what) noexcept
{
	const std::string_view base = standard14_metrics(font).base_font;
	char text[200];
	const int n = std::snprintf(text, sizeof(text), "Windows has no %.*s for the standard font %.*s (family \"%.*ls\")",
		static_cast<int>(what.size()), what.data(), static_cast<int>(base.size()), base.data(),
		static_cast<int>(std::min<size_t>(family.size(), 64u)), family.data());
	return font_error(ErrorCode::UnsupportedFeature,
		std::string_view(text, n > 0 ? std::min<size_t>(static_cast<size_t>(n), sizeof(text) - 1u) : 0u),
		DecoderStep::DWriteSystemFont);
}

struct Standard14Program {
	std::shared_ptr<const std::vector<uint8_t>> bytes;
	FontProgramFormat format = FontProgramFormat::TrueType;
};

// Finds `family` at the font's weight and style in the system collection and reads that face's file, whole, through
// its own loader. Every DirectWrite object it goes through is released when it returns; the bytes are the font's own.
[[nodiscard]] PdfResult<Standard14Program> read_standard14_program(Standard14Font font, std::wstring_view family) noexcept
{
	try {
		const Standard14Face want = standard14_face(font);
		IDWriteFactory5* factory = nullptr;
		if (const HRESULT hr = shared_factory(factory); FAILED(hr)) {
			return dwrite_failure(hr, "DWriteCreateFactory", DecoderStep::DWriteFactory);
		}
		const std::wstring family_name(family); // FindFamilyName takes a terminated string
		ComPtr<IDWriteFontCollection> collection;
		ComPtr<IDWriteFontFamily> font_family;
		ComPtr<IDWriteFont> system_font;
		ComPtr<IDWriteFontFace> face;
		BOOL exists = FALSE;
		bool simulated = false;
		const HRESULT find_hr = dwrite_step(detail::FontLoaderStage::SystemFont, [&]() -> HRESULT {
			HRESULT hr = factory->GetSystemFontCollection(collection.ReleaseAndGetAddressOf(), FALSE);
			UINT32 index = 0;
			if (SUCCEEDED(hr)) {
				hr = collection->FindFamilyName(family_name.c_str(), &index, &exists);
			}
			if (FAILED(hr) || !exists) {
				return hr;
			}
			hr = collection->GetFontFamily(index, font_family.ReleaseAndGetAddressOf());
			if (SUCCEEDED(hr)) {
				hr = font_family->GetFirstMatchingFont(want.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL, want.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
					system_font.ReleaseAndGetAddressOf());
			}
			if (FAILED(hr)) {
				return hr;
			}
			// DirectWrite returns the closest face it has: one it would only simulate (bolding or slanting another), or a
			// real face of another weight, stretch or style, is not the face the font needs.
			const DWRITE_FONT_STYLE style = system_font->GetStyle();
			simulated = system_font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE ||
				system_font->GetWeight() != (want.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL) ||
				system_font->GetStretch() != DWRITE_FONT_STRETCH_NORMAL ||
				(want.italic ? style == DWRITE_FONT_STYLE_NORMAL : style != DWRITE_FONT_STYLE_NORMAL);
			return simulated ? S_OK : system_font->CreateFontFace(face.ReleaseAndGetAddressOf());
		});
		if (FAILED(find_hr)) {
			return dwrite_failure(find_hr, "system font lookup", DecoderStep::DWriteSystemFont);
		}
		if (!exists) {
			return standard14_face_missing(family, font, "font family");
		}
		if (simulated) {
			return standard14_face_missing(family, font, "face of the font's weight, width and style");
		}
		const DWRITE_FONT_FACE_TYPE type = face->GetType();
		if ((type != DWRITE_FONT_FACE_TYPE_TRUETYPE && type != DWRITE_FONT_FACE_TYPE_CFF) || face->GetIndex() != 0u) {
			return standard14_face_missing(family, font, "single TrueType or OpenType face file");
		}

		UINT32 file_count = 0;
		ComPtr<IDWriteFontFile> file;
		ComPtr<IDWriteFontFileLoader> loader;
		ComPtr<IDWriteFontFileStream> stream;
		UINT64 size = 0;
		const HRESULT file_hr = dwrite_step(detail::FontLoaderStage::SystemFontFile, [&]() -> HRESULT {
			HRESULT hr = face->GetFiles(&file_count, nullptr);
			if (FAILED(hr) || file_count != 1u) {
				return FAILED(hr) ? hr : DWRITE_E_FILEFORMAT;
			}
			hr = face->GetFiles(&file_count, file.ReleaseAndGetAddressOf());
			const void* key = nullptr;
			UINT32 key_size = 0;
			if (SUCCEEDED(hr)) {
				hr = file->GetReferenceKey(&key, &key_size);
			}
			if (SUCCEEDED(hr)) {
				hr = file->GetLoader(loader.ReleaseAndGetAddressOf());
			}
			if (SUCCEEDED(hr)) {
				hr = loader->CreateStreamFromKey(key, key_size, stream.ReleaseAndGetAddressOf());
			}
			return SUCCEEDED(hr) ? stream->GetFileSize(&size) : hr;
		});
		if (FAILED(file_hr)) {
			return dwrite_failure(file_hr, "system font file", DecoderStep::DWriteFontFile);
		}
		if (size == 0u || size > kMaxStandard14FaceBytes) {
			return font_error(ErrorCode::ResourceLimit, "A standard font's Windows face file is empty or too large to read",
				DecoderStep::DWriteFontFile);
		}
		auto bytes = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(size));
		const HRESULT read_hr = dwrite_step(detail::FontLoaderStage::SystemFontFile, [&]() -> HRESULT {
			const void* fragment = nullptr;
			void* context = nullptr;
			const HRESULT hr = stream->ReadFileFragment(&fragment, 0u, size, &context);
			if (FAILED(hr)) {
				return hr;
			}
			std::memcpy(bytes->data(), fragment, static_cast<size_t>(size));
			stream->ReleaseFileFragment(context);
			return S_OK;
		});
		if (FAILED(read_hr)) {
			return dwrite_failure(read_hr, "system font file read", DecoderStep::DWriteFontFile);
		}
		g_standard14_face_reads.fetch_add(1u, std::memory_order_relaxed);
		return Standard14Program{std::move(bytes),
			type == DWRITE_FONT_FACE_TYPE_CFF ? FontProgramFormat::OpenType : FontProgramFormat::TrueType};
	} catch (const std::bad_alloc&) {
		return font_error(ErrorCode::OutOfMemory, "Out of memory reading a standard font's Windows face",
			DecoderStep::DWriteFontFile);
	}
}

} // namespace

namespace detail {

void reset_standard14_faces() noexcept
{
	const std::lock_guard lock(g_standard14_mutex);
	for (std::shared_ptr<const LoadedFont>& face : g_standard14_faces) {
		face.reset();
	}
}

uint64_t standard14_face_reads() noexcept
{
	return g_standard14_face_reads.load(std::memory_order_relaxed);
}

} // namespace detail

PdfResult<std::shared_ptr<const LoadedFont>> load_standard14_face(Standard14Font font) noexcept
{
	const size_t slot = static_cast<size_t>(font);
	const bool overridden = detail::g_standard14_family_font == font;
	std::unique_lock<std::mutex> loading;
	if (!overridden) {
		{
			const std::lock_guard lock(g_standard14_mutex);
			if (g_standard14_faces[slot] != nullptr) {
				return g_standard14_faces[slot];
			}
		}
		// The font's loader lock, then the slot again: a caller that waited finds the face the first one loaded.
		loading = std::unique_lock(g_standard14_load_mutexes[slot]);
		const std::lock_guard lock(g_standard14_mutex);
		if (g_standard14_faces[slot] != nullptr) {
			return g_standard14_faces[slot];
		}
	}
	PdfResult<Standard14Program> program =
		recorded(read_standard14_program(font, overridden ? detail::g_standard14_family : standard14_face(font).family));
	if (!program.has_value()) {
		return std::move(program.error());
	}
	// load_embedded_font records its own failure.
	RECTO_PDF_TRY(std::shared_ptr<const LoadedFont> loaded, load_embedded_font(std::move(program->bytes), program->format));
	if (overridden) {
		return loaded;
	}
	const std::lock_guard lock(g_standard14_mutex);
	if (g_standard14_faces[slot] == nullptr) {
		g_standard14_faces[slot] = std::move(loaded); // the first face loaded is the one every caller shares
	}
	return g_standard14_faces[slot];
}

namespace {

// The glyph `point` maps to in `font`'s face, or none.
[[nodiscard]] PdfResult<std::optional<Standard14FaceGlyph>> glyph_in_face(Standard14Font font, UINT32 point) noexcept
{
	RECTO_PDF_TRY(std::shared_ptr<const LoadedFont> face, load_standard14_face(font));
	IDWriteFontFace* dwrite = face->dwrite_face();
	if (dwrite == nullptr) {
		return recorded(PdfResult<std::optional<Standard14FaceGlyph>>(font_error(ErrorCode::UnsupportedFeature,
			"A standard font's Windows face loaded without DirectWrite, so its cmap cannot be read",
			DecoderStep::DWriteFontFace)));
	}
	UINT16 glyph = 0;
	if (const HRESULT hr = dwrite->GetGlyphIndices(&point, 1u, &glyph); FAILED(hr)) {
		return recorded(PdfResult<std::optional<Standard14FaceGlyph>>(
			dwrite_failure(hr, "GetGlyphIndices", DecoderStep::DWriteFontFace)));
	}
	if (glyph == 0u) {
		return std::optional<Standard14FaceGlyph>{};
	}
	return std::optional<Standard14FaceGlyph>{Standard14FaceGlyph{std::move(face), glyph}};
}

// Symbol codes the Windows `Symbol` face has no glyph for, and the Unicode scalar `Segoe UI Symbol` draws them with:
// only the Euro, which Adobe added to Symbol in 1997 (Adobe Glyph List: Euro, U+20AC).
constexpr std::array<std::pair<uint8_t, char32_t>, 1> kSymbolCodesFromSegoe = {{{uint8_t{0xA0}, char32_t{0x20AC}}}};

} // namespace

PdfResult<std::optional<Standard14FaceGlyph>> standard14_face_glyph(Standard14Font font, char32_t code_point) noexcept
{
	if (font != Standard14Font::Symbol || code_point >= 0x100u) {
		return glyph_in_face(font, static_cast<UINT32>(code_point));
	}
	RECTO_PDF_TRY(std::optional<Standard14FaceGlyph> glyph, glyph_in_face(font, 0xF000u | static_cast<UINT32>(code_point)));
	if (glyph.has_value()) {
		return glyph;
	}
	for (const auto& [code, unicode] : kSymbolCodesFromSegoe) {
		if (code == code_point) {
			return glyph_in_face(Standard14Font::ZapfDingbats, static_cast<UINT32>(unicode)); // Segoe UI Symbol
		}
	}
	return std::optional<Standard14FaceGlyph>{};
}

} // namespace recto::pdf
