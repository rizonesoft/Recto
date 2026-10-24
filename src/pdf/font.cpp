// Embedded font programs loaded in memory (ISO 32000-1 sections 9.6.3, 9.8 and
// 9.9; D01 T02 section 2): DirectWrite for the programs it accepts, and a
// TrueType `glyf` reader for the ones it refuses.
#include "font.h"

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
	bool long_loca{false};
	TableSpan hmtx{};
	TableSpan loca{};
	TableSpan glyf{};
};

namespace {

[[nodiscard]] PdfResult<std::unique_ptr<const LoadedFont::GlyfTables>> parse_glyf_tables(const ByteReader& font) noexcept;

// The glyph's data inside `glyf`, from `loca`, checked at load to run forwards and stay inside `glyf`.
[[nodiscard]] std::span<const uint8_t> glyph_data(
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
	return font.sub(t.glyf.offset + start, end - start);
}

// Per-call outline budgets (font.h): components visited and points held.
struct ExpansionBudget {
	size_t visits{0};
	size_t points{0};
};

[[nodiscard]] PdfResult<void> expand_glyph(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	uint16_t glyph,
	size_t depth,
	ExpansionBudget& budget,
	ExpandedGlyph& out) noexcept;

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
		const size_t next = static_cast<size_t>(end_pt) + 1u;
		if (next < point_count) {
			return glyf_error(ErrorCode::DamagedFormat, "Glyph contour ends run backwards");
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

[[nodiscard]] PdfResult<void> expand_composite(
	const ByteReader& font,
	const LoadedFont::GlyfTables& t,
	const ByteReader& data,
	size_t depth,
	ExpansionBudget& budget,
	ExpandedGlyph& out) noexcept
{
	constexpr uint16_t kArgsAreWords = 0x0001u;
	constexpr uint16_t kArgsAreXyValues = 0x0002u;
	constexpr uint16_t kHaveScale = 0x0008u;
	constexpr uint16_t kMoreComponents = 0x0020u;
	constexpr uint16_t kHaveXyScale = 0x0040u;
	constexpr uint16_t kHaveTwoByTwo = 0x0080u;
	constexpr uint16_t kScaledComponentOffset = 0x0800u;
	constexpr uint16_t kUnscaledComponentOffset = 0x1000u;

	const size_t composite_base = out.points.size();
	size_t at = 10u;
	for (;;) {
		uint16_t flags = 0;
		uint16_t component = 0;
		if (!data.u16(at, flags) || !data.u16(at + 2u, component)) {
			return glyf_error(ErrorCode::DamagedFormat, "Composite glyph record is truncated");
		}
		at += 4u;
		int32_t arg1 = 0;
		int32_t arg2 = 0;
		const bool xy = (flags & kArgsAreXyValues) != 0u;
		if ((flags & kArgsAreWords) != 0u) {
			uint16_t a = 0;
			uint16_t b = 0;
			if (!data.u16(at, a) || !data.u16(at + 2u, b)) {
				return glyf_error(ErrorCode::DamagedFormat, "Composite glyph arguments are truncated");
			}
			at += 4u;
			arg1 = xy ? static_cast<int16_t>(a) : static_cast<int32_t>(a);
			arg2 = xy ? static_cast<int16_t>(b) : static_cast<int32_t>(b);
		} else {
			uint8_t a = 0;
			uint8_t b = 0;
			if (!data.u8(at, a) || !data.u8(at + 1u, b)) {
				return glyf_error(ErrorCode::DamagedFormat, "Composite glyph arguments are truncated");
			}
			at += 2u;
			arg1 = xy ? static_cast<int8_t>(a) : static_cast<int32_t>(a);
			arg2 = xy ? static_cast<int8_t>(b) : static_cast<int32_t>(b);
		}
		// x' = a x + c y, y' = b x + d y (the OpenType `glyf` component transform).
		double a = 1.0;
		double b = 0.0;
		double c = 0.0;
		double d = 1.0;
		if ((flags & kHaveScale) != 0u) {
			uint16_t s = 0;
			if (!data.u16(at, s)) {
				return glyf_error(ErrorCode::DamagedFormat, "Composite glyph scale is truncated");
			}
			at += 2u;
			a = d = f2dot14(s);
		} else if ((flags & kHaveXyScale) != 0u) {
			uint16_t sx = 0;
			uint16_t sy = 0;
			if (!data.u16(at, sx) || !data.u16(at + 2u, sy)) {
				return glyf_error(ErrorCode::DamagedFormat, "Composite glyph scale is truncated");
			}
			at += 4u;
			a = f2dot14(sx);
			d = f2dot14(sy);
		} else if ((flags & kHaveTwoByTwo) != 0u) {
			uint16_t m[4] = {};
			for (size_t i = 0; i < 4u; ++i) {
				if (!data.u16(at + 2u * i, m[i])) {
					return glyf_error(ErrorCode::DamagedFormat, "Composite glyph transform is truncated");
				}
			}
			at += 8u;
			a = f2dot14(m[0]);
			b = f2dot14(m[1]);
			c = f2dot14(m[2]);
			d = f2dot14(m[3]);
		}
		if (component >= t.glyph_count) {
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
		RECTO_PDF_RETURN_IF_ERROR(expand_glyph(font, t, component, depth + 1u, budget, out));
		for (size_t i = child_base; i < out.points.size(); ++i) {
			GlyfPoint& p = out.points[i];
			const double x = p.x;
			p.x = a * x + c * p.y;
			p.y = b * x + d * p.y;
		}
		double dx = 0.0;
		double dy = 0.0;
		if (xy) {
			dx = arg1;
			dy = arg2;
			if ((flags & kScaledComponentOffset) != 0u && (flags & kUnscaledComponentOffset) == 0u) {
				const double ox = dx;
				dx = a * ox + c * dy;
				dy = b * ox + d * dy;
			}
		} else {
			// Point matching: the component's point `arg2` lands on the composite's point `arg1` placed so far.
			const size_t parent = composite_base + static_cast<size_t>(arg1);
			const size_t child = child_base + static_cast<size_t>(arg2);
			if (parent >= child_base || child >= out.points.size()) {
				return glyf_error(ErrorCode::DamagedFormat, "Composite glyph anchors name points that do not exist");
			}
			dx = out.points[parent].x - out.points[child].x;
			dy = out.points[parent].y - out.points[child].y;
		}
		for (size_t i = child_base; i < out.points.size(); ++i) {
			out.points[i].x += dx;
			out.points[i].y += dy;
		}
		if ((flags & kMoreComponents) == 0u) {
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
	ExpandedGlyph& out) noexcept
{
	if (depth > kMaxCompositeDepth) {
		PdfError error = glyf_error(ErrorCode::ResourceLimit, "Composite glyphs nest deeper than their limit");
		error.refusal_reason = RefusalReason::CompositeDepth;
		error.observed = depth;
		error.bound = kMaxCompositeDepth;
		return error;
	}
	const ByteReader data(glyph_data(font, t, glyph));
	if (data.size() == 0u) {
		return {}; // an empty glyph, such as a space
	}
	int16_t contours = 0;
	if (data.size() < 10u || !data.i16(0u, contours)) {
		return glyf_error(ErrorCode::DamagedFormat, "Glyph header is truncated");
	}
	if (contours >= 0) {
		return expand_simple(data, contours, budget, out);
	}
	return expand_composite(font, t, data, depth, budget, out);
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
		!font.u16(hhea.offset + 34u, t.h_metrics)) {
		return glyf_error(ErrorCode::DamagedFormat, "Font head, maxp or hhea table is truncated");
	}
	if (t.units_per_em < 16u || t.units_per_em > 16384u || (loca_format != 0 && loca_format != 1) || glyph_count == 0u ||
		t.h_metrics == 0u) {
		return glyf_error(ErrorCode::DamagedFormat, "Font head, maxp or hhea values are out of range");
	}
	// A `numberOfHMetrics` past the glyph count (DirectWrite refuses it) names records no glyph uses: the first
	// `glyph_count` are read.
	t.h_metrics = std::min(t.h_metrics, glyph_count);
	t.glyph_count = glyph_count;
	t.long_loca = loca_format == 1;
	t.hmtx = hmtx;
	t.loca = loca;
	t.glyf = glyf;
	const size_t hmtx_needed = 4u * t.h_metrics + 2u * (static_cast<size_t>(glyph_count) - t.h_metrics);
	const size_t loca_needed = (t.long_loca ? 4u : 2u) * (static_cast<size_t>(glyph_count) + 1u);
	if (hmtx.length < hmtx_needed || loca.length < loca_needed) {
		return glyf_error(ErrorCode::DamagedFormat, "Font hmtx or loca table is shorter than its glyph count needs");
	}
	// Every glyph's data runs forwards and stays inside `glyf`, so later reads need no checks of their own.
	uint32_t previous = 0;
	for (size_t i = 0; i <= glyph_count; ++i) {
		uint32_t offset = 0;
		if (t.long_loca) {
			(void)font.u32(loca.offset + 4u * i, offset);
		} else {
			uint16_t half = 0;
			(void)font.u16(loca.offset + 2u * i, half);
			offset = 2u * static_cast<uint32_t>(half);
		}
		if (offset < previous || offset > glyf.length) {
			return glyf_error(ErrorCode::DamagedFormat, "Font loca offsets run backwards or past the glyf table");
		}
		previous = offset;
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
	std::unique_ptr<const GlyfTables> glyf) noexcept
	: kind_(kind)
	, program_(std::move(program))
	, units_per_em_(units_per_em)
	, glyph_count_(glyph_count)
	, dwrite_(std::move(dwrite))
	, glyf_(std::move(glyf))
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
		const double scale = 1.0 / static_cast<double>(units_per_em_);
		if (dwrite_ != nullptr) {
			DWRITE_GLYPH_METRICS metrics{};
			const HRESULT hr = dwrite_->face->GetDesignGlyphMetrics(&glyph, 1u, &metrics, FALSE);
			if (FAILED(hr)) {
				return dwrite_failure(hr, "GetDesignGlyphMetrics", DecoderStep::DWriteFontFace);
			}
			return static_cast<double>(metrics.advanceWidth) * scale;
		}
		// `hmtx`: one (advance, left side bearing) record per glyph up to `numberOfHMetrics`; later glyphs repeat the
		// last advance.
		const ByteReader font(*program_);
		const size_t record = std::min<size_t>(glyph, static_cast<size_t>(glyf_->h_metrics) - 1u);
		uint16_t width = 0;
		(void)font.u16(glyf_->hmtx.offset + 4u * record, width);
		return static_cast<double>(width) * scale;
	}());
}

PdfResult<std::optional<PdfRect>> LoadedFont::ink_box(uint16_t glyph) const noexcept
{
	return recorded([&]() noexcept -> PdfResult<std::optional<PdfRect>> {
		if (glyph >= glyph_count_) {
			return font_error(ErrorCode::DamagedFormat, "Glyph index past the font's glyph count", DecoderStep::None);
		}
		const double scale = 1.0 / static_cast<double>(units_per_em_);
		if (dwrite_ != nullptr) {
			// The black box from the side bearings: left and right from the horizontal origin and advance, top from the
			// vertical origin (DirectWrite's design units, y up from the baseline).
			DWRITE_GLYPH_METRICS m{};
			const HRESULT hr = dwrite_->face->GetDesignGlyphMetrics(&glyph, 1u, &m, FALSE);
			if (FAILED(hr)) {
				return dwrite_failure(hr, "GetDesignGlyphMetrics", DecoderStep::DWriteFontFace);
			}
			const double width = static_cast<double>(m.advanceWidth) - m.leftSideBearing - m.rightSideBearing;
			const double height = static_cast<double>(m.advanceHeight) - m.topSideBearing - m.bottomSideBearing;
			if (!(width > 0.0) || !(height > 0.0)) {
				return std::optional<PdfRect>{};
			}
			const double top = static_cast<double>(m.verticalOriginY) - m.topSideBearing;
			return std::optional<PdfRect>(PdfRect{
				m.leftSideBearing * scale,
				(top - height) * scale,
				(m.leftSideBearing + width) * scale,
				top * scale});
		}
		// The `glyf` font: the box of the expanded points, on- and off-curve, which holds the outline (a quadratic stays
		// inside the hull of its control points).
		ExpandedGlyph expanded;
		ExpansionBudget budget;
		RECTO_PDF_RETURN_IF_ERROR(expand_glyph(ByteReader(*program_), *glyf_, glyph, 0u, budget, expanded));
		if (expanded.points.empty()) {
			return std::optional<PdfRect>{};
		}
		double x_min = expanded.points[0].x;
		double y_min = expanded.points[0].y;
		double x_max = x_min;
		double y_max = y_min;
		for (const GlyfPoint& p : expanded.points) {
			x_min = std::min(x_min, p.x);
			y_min = std::min(y_min, p.y);
			x_max = std::max(x_max, p.x);
			y_max = std::max(y_max, p.y);
		}
		return std::optional<PdfRect>(PdfRect{x_min * scale, y_min * scale, x_max * scale, y_max * scale});
	}());
}

PdfResult<GlyphOutline> LoadedFont::outline(uint16_t glyph) const noexcept
{
	return recorded([&]() noexcept -> PdfResult<GlyphOutline> {
		if (glyph >= glyph_count_) {
			return font_error(ErrorCode::DamagedFormat, "Glyph index past the font's glyph count", DecoderStep::None);
		}
		const double scale = 1.0 / static_cast<double>(units_per_em_);
		if (dwrite_ != nullptr) {
			// An em size of the font's units per em gives the outline in design units; the sink scales it to text space.
			OutlineSink sink(scale);
			const HRESULT hr = dwrite_->face->GetGlyphRunOutline(
				static_cast<FLOAT>(units_per_em_), &glyph, nullptr, nullptr, 1u, FALSE, FALSE, &sink);
			if (FAILED(hr)) {
				return dwrite_failure(hr, "GetGlyphRunOutline", DecoderStep::DWriteFontFace);
			}
			return sink.take();
		}
		ExpandedGlyph expanded;
		ExpansionBudget budget;
		RECTO_PDF_RETURN_IF_ERROR(expand_glyph(ByteReader(*program_), *glyf_, glyph, 0u, budget, expanded));
		return outline_from_points(expanded, scale);
	}());
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
					LoadedFontKind::OutlineOnly, std::move(program), units_per_em, glyph_count, nullptr, std::move(tables)));
			} catch (const std::bad_alloc&) {
				return glyf_error(ErrorCode::OutOfMemory, "Out of memory loading a font");
			}
		}

		DWRITE_FONT_METRICS metrics{};
		face->GetMetrics(&metrics);
		const UINT16 glyph_count = face->GetGlyphCount();
		const LoadedFontKind kind =
			face->GetType() == DWRITE_FONT_FACE_TYPE_CFF ? LoadedFontKind::OpenType : LoadedFontKind::TrueType;
		try {
			state->face = std::move(face);
			return std::shared_ptr<const LoadedFont>(new LoadedFont(
				kind, std::move(program), metrics.designUnitsPerEm, glyph_count, std::move(state), nullptr));
		} catch (const std::bad_alloc&) {
			return font_error(ErrorCode::OutOfMemory, "Out of memory loading a font", DecoderStep::DWriteFontFace);
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

} // namespace recto::pdf
